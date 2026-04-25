#include "planner_ir.h"

#include <stdlib.h>
#include <string.h>

static void reset_literal(RxLiteral *literal)
{
    if (literal == NULL)
    {
        return;
    }
    if (literal->kind == RX_LITERAL_SYMBOL)
    {
        free((void *)literal->as.symbol_name);
    }
    memset(literal, 0, sizeof(*literal));
}

void rx_planner_ir_init(RxPlannerIrProgram *program)
{
    memset(program, 0, sizeof(*program));
}

void rx_planner_ir_reset(RxPlannerIrProgram *program)
{
    if (program == NULL)
    {
        return;
    }
    for (int pipeline_index = 0; pipeline_index < program->pipeline_count; ++pipeline_index)
    {
        RxPlannerIrPipeline *pipeline = &program->pipelines[pipeline_index];
        for (int slot_index = 0; slot_index < pipeline->state_slot_count; ++slot_index)
        {
            free((void *)pipeline->state_slots[slot_index].name);
            reset_literal(&pipeline->state_slots[slot_index].initial_value);
        }
        free(pipeline->state_slots);
        free(pipeline->ops);
        free((void *)pipeline->pipeline_name);
    }
    free(program->pipelines);
    memset(program, 0, sizeof(*program));
}

static char *dup_text(const char *text)
{
    if (text == NULL)
    {
        return NULL;
    }
    size_t length = strlen(text) + 1;
    char *copy = malloc(length);
    if (copy != NULL)
    {
        memcpy(copy, text, length);
    }
    return copy;
}

static bool clone_literal(const RxLiteral *src, RxLiteral *dst)
{
    memset(dst, 0, sizeof(*dst));
    *dst = *src;
    if (src->kind == RX_LITERAL_SYMBOL && src->as.symbol_name != NULL)
    {
        dst->as.symbol_name = dup_text(src->as.symbol_name);
        if (dst->as.symbol_name == NULL)
        {
            return false;
        }
    }
    return true;
}

bool rx_build_planner_ir(
    const RxLoweredProgram *lowered,
    RxPlannerIrProgram *out_program,
    RxDiagnosticBag *diagnostics)
{
    (void)diagnostics;
    if (out_program == NULL)
    {
        return false;
    }
    rx_planner_ir_reset(out_program);
    if (lowered == NULL || lowered->pipeline_count <= 0)
    {
        return true;
    }
    out_program->pipelines = calloc((size_t)lowered->pipeline_count, sizeof(*out_program->pipelines));
    if (out_program->pipelines == NULL)
    {
        return false;
    }
    out_program->pipeline_count = lowered->pipeline_count;
    for (int pipeline_index = 0; pipeline_index < lowered->pipeline_count; ++pipeline_index)
    {
        const RxLoweredPipeline *src = &lowered->pipelines[pipeline_index];
        RxPlannerIrPipeline *dst = &out_program->pipelines[pipeline_index];
        dst->pipeline_name = dup_text(src->pipeline_name != NULL ? src->pipeline_name : "pipeline");
        dst->segment_index = src->segment_index;
        dst->source_kind = src->source_kind;
        dst->source_count = src->source_count;
        dst->source_inner_n = src->source_inner_n;
        dst->returns_scalar = src->returns_scalar;
        dst->preserves_observable_layout = src->preserves_observable_layout;
        if (dst->pipeline_name == NULL)
        {
            return false;
        }
        if (src->state_slot_count > 0)
        {
            dst->state_slots = calloc((size_t)src->state_slot_count, sizeof(*dst->state_slots));
            if (dst->state_slots == NULL)
            {
                return false;
            }
            dst->state_slot_count = src->state_slot_count;
            for (int slot_index = 0; slot_index < src->state_slot_count; ++slot_index)
            {
                dst->state_slots[slot_index].kind = src->state_slots[slot_index].kind;
                dst->state_slots[slot_index].value_type = src->state_slots[slot_index].value_type;
                dst->state_slots[slot_index].name = dup_text(src->state_slots[slot_index].name);
                if (dst->state_slots[slot_index].name == NULL
                    || !clone_literal(&src->state_slots[slot_index].initial_value, &dst->state_slots[slot_index].initial_value))
                {
                    return false;
                }
            }
        }
        if (src->op_count > 0)
        {
            dst->ops = calloc((size_t)src->op_count, sizeof(*dst->ops));
            if (dst->ops == NULL)
            {
                return false;
            }
            dst->op_count = src->op_count;
            for (int op_index = 0; op_index < src->op_count; ++op_index)
            {
                dst->ops[op_index].kind = src->ops[op_index].kind;
                dst->ops[op_index].stage = src->ops[op_index].stage;
                dst->ops[op_index].state_slot_index = src->ops[op_index].state_slot_index;
                dst->ops[op_index].aux_state_slot_index = src->ops[op_index].aux_state_slot_index;
            }
        }
    }
    return true;
}

bool rx_validate_planner_ir(
    const RxPlannerIrProgram *program,
    RxDiagnosticBag *diagnostics)
{
    (void)diagnostics;
    if (program == NULL)
    {
        return false;
    }
    for (int pipeline_index = 0; pipeline_index < program->pipeline_count; ++pipeline_index)
    {
        const RxPlannerIrPipeline *pipeline = &program->pipelines[pipeline_index];
        if (pipeline->pipeline_name == NULL)
        {
            return false;
        }
        for (int slot_index = 0; slot_index < pipeline->state_slot_count; ++slot_index)
        {
            if (pipeline->state_slots[slot_index].name == NULL)
            {
                return false;
            }
        }
        for (int op_index = 0; op_index < pipeline->op_count; ++op_index)
        {
            const RxPlannerIrOp *op = &pipeline->ops[op_index];
            if (op->state_slot_index >= pipeline->state_slot_count
                || op->aux_state_slot_index >= pipeline->state_slot_count)
            {
                return false;
            }
        }
    }
    return true;
}
