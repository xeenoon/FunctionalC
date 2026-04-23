#include "simplify.h"

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

static RxPlannedStageKind planned_stage_kind(const RxFunctionSignature *signature)
{
    if (signature == NULL)
    {
        return RX_STAGE_UNSUPPORTED;
    }

    if (strcmp(signature->name, "map") == 0)
    {
        return RX_STAGE_MAP;
    }
    if (strcmp(signature->name, "pairMap") == 0)
    {
        return RX_STAGE_PAIR_MAP;
    }
    if (strcmp(signature->name, "filter") == 0)
    {
        return RX_STAGE_FILTER;
    }
    if (strcmp(signature->name, "scan") == 0)
    {
        return RX_STAGE_SCAN;
    }
    if (strcmp(signature->name, "scanfrom") == 0)
    {
        return RX_STAGE_SCAN_FROM;
    }
    if (strcmp(signature->name, "reduce") == 0)
    {
        return RX_STAGE_REDUCE;
    }
    if (strcmp(signature->name, "mapTo") == 0)
    {
        return RX_STAGE_MAP_TO;
    }
    if (strcmp(signature->name, "take") == 0)
    {
        return RX_STAGE_TAKE;
    }
    if (strcmp(signature->name, "skip") == 0)
    {
        return RX_STAGE_SKIP;
    }
    if (strcmp(signature->name, "takeWhile") == 0)
    {
        return RX_STAGE_TAKE_WHILE;
    }
    if (strcmp(signature->name, "skipWhile") == 0)
    {
        return RX_STAGE_SKIP_WHILE;
    }
    if (strcmp(signature->name, "distinctUntilChanged") == 0)
    {
        return RX_STAGE_DISTINCT_UNTIL_CHANGED;
    }
    if (strcmp(signature->name, "last") == 0)
    {
        return RX_STAGE_LAST;
    }
    if (strcmp(signature->name, "first") == 0)
    {
        return RX_STAGE_FIRST;
    }
    return RX_STAGE_UNSUPPORTED;
}

static bool source_is_self_contained(const RxSourceCall *source)
{
    return source->signature != NULL
        && (strcmp(source->signature->name, "range") == 0
            || strcmp(source->signature->name, "zip_range") == 0);
}

static bool stage_is_self_contained(const RxStageCall *stage)
{
    if (stage->signature == NULL || !stage->signature->transpile_candidate)
    {
        return false;
    }

    switch (planned_stage_kind(stage->signature))
    {
        case RX_STAGE_MAP:
        case RX_STAGE_PAIR_MAP:
        case RX_STAGE_FILTER:
        case RX_STAGE_SCAN:
        case RX_STAGE_SCAN_FROM:
        case RX_STAGE_REDUCE:
        case RX_STAGE_MAP_TO:
        case RX_STAGE_TAKE:
        case RX_STAGE_SKIP:
        case RX_STAGE_TAKE_WHILE:
        case RX_STAGE_SKIP_WHILE:
        case RX_STAGE_DISTINCT_UNTIL_CHANGED:
        case RX_STAGE_LAST:
        case RX_STAGE_FIRST:
            return true;
        default:
            return false;
    }
}

bool rx_build_execution_plan(
    const RxProgramIr *program,
    RxExecutionPlan *plan,
    RxDiagnosticBag *diagnostics)
{
    memset(plan, 0, sizeof(*plan));
    if (program == NULL || program->pipeline_count <= 0)
    {
        return true;
    }

    plan->pipeline_count = program->pipeline_count;
    plan->pipelines = calloc((size_t)program->pipeline_count, sizeof(*plan->pipelines));
    if (plan->pipelines == NULL)
    {
        return false;
    }

    for (int pipeline_index = 0; pipeline_index < program->pipeline_count; ++pipeline_index)
    {
        const RxPipelineIr *source_pipeline = &program->pipelines[pipeline_index];
        RxPlannedPipeline *planned = &plan->pipelines[pipeline_index];
        planned->name = source_pipeline->name;
        planned->source = source_pipeline->source;
        planned->backend = RX_PLAN_BACKEND_RUNTIME;
        planned->needs_runtime_fallback = true;

        if (!source_is_self_contained(&source_pipeline->source))
        {
            if (!append_diag(
                    diagnostics,
                    RX_DIAG_UNSUPPORTED_SOURCE,
                    "Only self-contained range() sources are supported in the planner backend",
                    source_pipeline->source.signature != NULL ? source_pipeline->source.signature->name : NULL,
                    pipeline_index,
                    -1))
            {
                return false;
            }
            continue;
        }

        if (source_pipeline->stage_count > 0)
        {
            planned->stages = calloc((size_t)source_pipeline->stage_count, sizeof(*planned->stages));
            if (planned->stages == NULL)
            {
                return false;
            }
        }
        planned->stage_count = source_pipeline->stage_count;
        planned->transpile_candidate = true;

        for (int stage_index = 0; stage_index < source_pipeline->stage_count; ++stage_index)
        {
            const RxStageCall *stage = &source_pipeline->stages[stage_index];
            RxPlannedStage *planned_stage = &planned->stages[stage_index];
            planned_stage->kind = planned_stage_kind(stage->signature);
            planned_stage->signature = stage->signature;
            planned_stage->primary_argument = stage->arguments[0];
            planned_stage->secondary_argument = stage->arguments[1];
            planned_stage->chain_length = 1;
            planned_stage->can_start_segment = stage->can_start_segment;
            planned_stage->can_end_segment = stage->can_end_segment;
            planned_stage->must_remain_whole = stage->must_remain_whole;
            planned_stage->preserves_runtime_layout = stage->preserves_runtime_layout;

            if (!stage_is_self_contained(stage))
            {
                planned->transpile_candidate = false;
                if (!append_diag(
                        diagnostics,
                        RX_DIAG_UNSUPPORTED_OPERATOR,
                        "Planner backend only supports fully self-contained scalar operators",
                        stage->signature != NULL ? stage->signature->name : NULL,
                        pipeline_index,
                        stage_index))
                {
                    return false;
                }
            }

            if (stage->arguments[0].kind == RX_BINDING_RUNTIME_VALUE
                || stage->arguments[1].kind == RX_BINDING_RUNTIME_VALUE)
            {
                planned->transpile_candidate = false;
                if (!append_diag(
                        diagnostics,
                        RX_DIAG_DYNAMIC_RUNTIME_VALUE,
                        "Planner backend does not support runtime-only argument bindings",
                        stage->signature != NULL ? stage->signature->name : NULL,
                        pipeline_index,
                        stage_index))
                {
                    return false;
                }
            }

            if ((planned_stage->kind == RX_STAGE_REDUCE
                    || planned_stage->kind == RX_STAGE_LAST
                    || planned_stage->kind == RX_STAGE_FIRST)
                && stage_index != source_pipeline->stage_count - 1)
            {
                planned->transpile_candidate = false;
                if (!append_diag(
                        diagnostics,
                        RX_DIAG_UNLOWERABLE_PIPELINE,
                        "Terminal operators must be the final stage in a self-contained planner pipeline",
                        stage->signature != NULL ? stage->signature->name : NULL,
                        pipeline_index,
                        stage_index))
                {
                    return false;
                }
            }
        }

        if (planned->transpile_candidate
            && source_pipeline->source.signature != NULL
            && strcmp(source_pipeline->source.signature->name, "zip_range") == 0)
        {
            if (source_pipeline->stage_count == 0 || planned->stages[0].kind != RX_STAGE_PAIR_MAP)
            {
                planned->transpile_candidate = false;
                if (!append_diag(
                        diagnostics,
                        RX_DIAG_UNLOWERABLE_PIPELINE,
                        "zip_range planner pipelines must start with pairMap to become scalar",
                        planned->name,
                        pipeline_index,
                        0))
                {
                    return false;
                }
            }
        }

        if (!planned->transpile_candidate)
        {
            continue;
        }

        planned->backend = RX_PLAN_BACKEND_SPECIALIZED_C;
        planned->needs_runtime_fallback = false;
        planned->segment_count = 1;
        planned->segments = calloc(1, sizeof(*planned->segments));
        if (planned->segments == NULL)
        {
            return false;
        }
        planned->segments[0] = (RxPipelineSegment){
            RX_SEGMENT_SPECIALIZED_C,
            0,
            planned->stage_count,
            false,
            false,
        };
    }

    return !diagnostics->has_error;
}

bool rx_simplify_execution_plan(
    RxExecutionPlan *plan,
    RxDiagnosticBag *diagnostics)
{
    (void)plan;
    return !diagnostics->has_error;
}
