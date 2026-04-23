#include "lower.h"

#include <stdlib.h>
#include <string.h>

static bool append_diag(
    RxDiagnosticBag *diagnostics,
    RxDiagnosticCode code,
    const char *message,
    const char *symbol_name,
    int chain_index,
    int stage_index)
{
    return rx_diagnostic_bag_append(
        diagnostics,
        (RxDiagnostic){
            RX_DIAGNOSTIC_ERROR,
            code,
            message,
            symbol_name,
            chain_index,
            stage_index,
        });
}

static bool append_state_slot(
    RxLoweredPipeline *lowered,
    RxStateSlot slot,
    int *index_out)
{
    int next_count = lowered->state_slot_count + 1;
    RxStateSlot *slots = realloc(lowered->state_slots, (size_t)next_count * sizeof(*slots));
    if (slots == NULL)
    {
        return false;
    }
    lowered->state_slots = slots;
    lowered->state_slots[lowered->state_slot_count] = slot;
    *index_out = lowered->state_slot_count;
    lowered->state_slot_count = next_count;
    return true;
}

static bool append_op(
    RxLoweredPipeline *lowered,
    RxLoopOp op)
{
    int next_count = lowered->op_count + 1;
    RxLoopOp *ops = realloc(lowered->ops, (size_t)next_count * sizeof(*ops));
    if (ops == NULL)
    {
        return false;
    }
    lowered->ops = ops;
    lowered->ops[lowered->op_count] = op;
    lowered->op_count = next_count;
    return true;
}

static RxLiteral literal_int(int value)
{
    RxLiteral literal;
    memset(&literal, 0, sizeof(literal));
    literal.kind = RX_LITERAL_INT;
    literal.as.int_value = value;
    return literal;
}

bool rx_lower_pipeline_segment(
    const RxPlannedPipeline *pipeline,
    const RxPipelineSegment *segment,
    RxLoweredPipeline *lowered,
    RxDiagnosticBag *diagnostics)
{
    memset(lowered, 0, sizeof(*lowered));
    lowered->pipeline_name = pipeline->name;
    lowered->segment_index = (int)(segment - pipeline->segments);
    lowered->returns_scalar = true;
    lowered->preserves_observable_layout = false;

    if (pipeline->source.signature == NULL)
    {
        append_diag(
            diagnostics,
            RX_DIAG_UNSUPPORTED_SOURCE,
            "Lowering currently only supports range() and zip_range() sources",
            NULL,
            0,
            -1);
        return false;
    }

    if (strcmp(pipeline->source.signature->name, "range") == 0)
    {
        lowered->source_kind = RX_LOOP_SOURCE_RANGE;
    }
    else if (strcmp(pipeline->source.signature->name, "zip_range") == 0)
    {
        lowered->source_kind = RX_LOOP_SOURCE_ZIP_RANGE;
    }
    else
    {
        append_diag(
            diagnostics,
            RX_DIAG_UNSUPPORTED_SOURCE,
            "Lowering currently only supports range() and zip_range() sources",
            pipeline->source.signature->name,
            0,
            -1);
        return false;
    }

    for (int index = 0; index < segment->stage_count; ++index)
    {
        const RxPlannedStage *stage = &pipeline->stages[segment->start_stage_index + index];
        RxLoopOp op;
        memset(&op, 0, sizeof(op));
        op.stage = stage;
        op.state_slot_index = -1;
        op.aux_state_slot_index = -1;

        switch (stage->kind)
        {
            case RX_STAGE_PAIR_MAP:
                op.kind = RX_OP_CALL_PAIR_MAP;
                break;
            case RX_STAGE_MAP:
                op.kind = RX_OP_CALL_MAP;
                break;
            case RX_STAGE_FILTER:
                op.kind = RX_OP_CALL_FILTER;
                break;
            case RX_STAGE_SCAN:
            {
                op.kind = RX_OP_CALL_SCAN;
                if (!append_state_slot(
                        lowered,
                        (RxStateSlot){ RX_STATE_SCAN_ACCUM, "scan_accum", RX_ARG_INT, literal_int(0) },
                        &op.state_slot_index))
                {
                    return false;
                }
                break;
            }
            case RX_STAGE_SCAN_FROM:
            {
                op.kind = RX_OP_CALL_SCAN;
                RxLiteral initial = stage->secondary_argument.kind == RX_BINDING_LITERAL
                    ? stage->secondary_argument.as.literal
                    : literal_int(0);
                if (!append_state_slot(
                        lowered,
                        (RxStateSlot){ RX_STATE_SCAN_ACCUM, "scan_accum", RX_ARG_INT, initial },
                        &op.state_slot_index))
                {
                    return false;
                }
                break;
            }
            case RX_STAGE_REDUCE:
            {
                op.kind = RX_OP_CALL_REDUCE;
                RxLiteral initial = stage->secondary_argument.kind == RX_BINDING_LITERAL
                    ? stage->secondary_argument.as.literal
                    : literal_int(0);
                if (!append_state_slot(
                        lowered,
                        (RxStateSlot){ RX_STATE_REDUCE_ACCUM, "reduce_accum", RX_ARG_INT, initial },
                        &op.state_slot_index))
                {
                    return false;
                }
                break;
            }
            case RX_STAGE_MAP_TO:
                op.kind = RX_OP_CALL_MAP_TO;
                break;
            case RX_STAGE_TAKE:
                op.kind = RX_OP_APPLY_TAKE;
                if (!append_state_slot(
                        lowered,
                        (RxStateSlot){ RX_STATE_TAKE_COUNT, "take_count", RX_ARG_INT, literal_int(0) },
                        &op.state_slot_index))
                {
                    return false;
                }
                break;
            case RX_STAGE_SKIP:
                op.kind = RX_OP_APPLY_SKIP;
                if (!append_state_slot(
                        lowered,
                        (RxStateSlot){ RX_STATE_SKIP_COUNT, "skip_count", RX_ARG_INT, literal_int(0) },
                        &op.state_slot_index))
                {
                    return false;
                }
                break;
            case RX_STAGE_TAKE_WHILE:
                op.kind = RX_OP_APPLY_TAKE_WHILE;
                break;
            case RX_STAGE_SKIP_WHILE:
                op.kind = RX_OP_APPLY_SKIP_WHILE;
                if (!append_state_slot(
                        lowered,
                        (RxStateSlot){ RX_STATE_SKIP_WHILE_PASSED, "skip_while_passed", RX_ARG_INT, literal_int(0) },
                        &op.state_slot_index))
                {
                    return false;
                }
                break;
            case RX_STAGE_DISTINCT_UNTIL_CHANGED:
                op.kind = RX_OP_APPLY_DISTINCT_UNTIL_CHANGED;
                if (!append_state_slot(
                        lowered,
                        (RxStateSlot){ RX_STATE_LAST_KEY, "last_key", RX_ARG_INT, literal_int(0) },
                        &op.state_slot_index))
                {
                    return false;
                }
                if (!append_state_slot(
                        lowered,
                        (RxStateSlot){ RX_STATE_HAS_LAST_KEY, "has_last_key", RX_ARG_INT, literal_int(0) },
                        &op.aux_state_slot_index))
                {
                    return false;
                }
                break;
            case RX_STAGE_LAST:
                op.kind = RX_OP_APPLY_LAST;
                if (!append_state_slot(
                        lowered,
                        (RxStateSlot){ RX_STATE_LAST_VALUE, "last_value", RX_ARG_INT, literal_int(0) },
                        &op.state_slot_index))
                {
                    return false;
                }
                if (!append_state_slot(
                        lowered,
                        (RxStateSlot){ RX_STATE_HAS_LAST_VALUE, "has_last_value", RX_ARG_INT, literal_int(0) },
                        &op.aux_state_slot_index))
                {
                    return false;
                }
                break;
            case RX_STAGE_FIRST:
                op.kind = RX_OP_APPLY_FIRST;
                break;
            default:
                append_diag(
                    diagnostics,
                    RX_DIAG_UNLOWERABLE_PIPELINE,
                    "Encountered an unsupported stage during lowering",
                    stage->signature != NULL ? stage->signature->name : NULL,
                    0,
                    segment->start_stage_index + index);
                return false;
        }

        if (!append_op(lowered, op))
        {
            return false;
        }
    }

    return !diagnostics->has_error;
}

bool rx_lower_execution_plan(
    const RxExecutionPlan *plan,
    RxLoweredProgram *lowered,
    RxDiagnosticBag *diagnostics)
{
    memset(lowered, 0, sizeof(*lowered));
    if (plan == NULL || plan->pipeline_count <= 0)
    {
        return true;
    }

    lowered->pipeline_count = plan->pipeline_count;
    lowered->pipelines = calloc((size_t)plan->pipeline_count, sizeof(*lowered->pipelines));
    if (lowered->pipelines == NULL)
    {
        return false;
    }

    for (int index = 0; index < plan->pipeline_count; ++index)
    {
        const RxPlannedPipeline *pipeline = &plan->pipelines[index];
        if (!pipeline->transpile_candidate || pipeline->segment_count != 1)
        {
            append_diag(
                diagnostics,
                RX_DIAG_UNLOWERABLE_PIPELINE,
                "Planner backend can only lower fully self-contained pipelines",
                pipeline->name,
                index,
                -1);
            return false;
        }
        if (!rx_lower_pipeline_segment(pipeline, &pipeline->segments[0], &lowered->pipelines[index], diagnostics))
        {
            return false;
        }
    }

    return !diagnostics->has_error;
}
