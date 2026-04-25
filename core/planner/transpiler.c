#include "transpiler.h"

#include <stdlib.h>
#include <string.h>

#include "lower.h"
#include "simplify.h"

static void free_pipeline_ir(RxPipelineIr *pipeline)
{
    free(pipeline->stages);
    memset(pipeline, 0, sizeof(*pipeline));
}

static void free_execution_plan(RxExecutionPlan *plan)
{
    for (int index = 0; index < plan->pipeline_count; ++index)
    {
        free(plan->pipelines[index].stages);
        free(plan->pipelines[index].segments);
    }
    free(plan->pipelines);
    memset(plan, 0, sizeof(*plan));
}

static void free_lowered_program(RxLoweredProgram *program)
{
    for (int index = 0; index < program->pipeline_count; ++index)
    {
        free(program->pipelines[index].state_slots);
        free(program->pipelines[index].ops);
    }
    free(program->pipelines);
    memset(program, 0, sizeof(*program));
}

void rx_transpiler_init(
    RxTranspiler *transpiler,
    const RxFunctionRegistry *registry)
{
    memset(transpiler, 0, sizeof(*transpiler));
    transpiler->registry = registry != NULL ? registry : rx_default_function_registry();
    rx_planner_ir_init(&transpiler->planner_ir);
    rx_diagnostic_bag_init(&transpiler->diagnostics);
}

void rx_transpiler_reset(RxTranspiler *transpiler)
{
    for (int index = 0; index < transpiler->program.pipeline_count; ++index)
    {
        free_pipeline_ir(&transpiler->program.pipelines[index]);
    }
    free(transpiler->program.pipelines);
    memset(&transpiler->program, 0, sizeof(transpiler->program));

    free_execution_plan(&transpiler->plan);
    free_lowered_program(&transpiler->lowered);
    rx_planner_ir_reset(&transpiler->planner_ir);
    free(transpiler->compiled_segments);
    transpiler->compiled_segments = NULL;
    transpiler->compiled_segment_count = 0;
    rx_diagnostic_bag_reset(&transpiler->diagnostics);
    rx_diagnostic_bag_init(&transpiler->diagnostics);
}

bool rx_transpiler_plan(RxTranspiler *transpiler)
{
    free_execution_plan(&transpiler->plan);
    return rx_build_execution_plan(&transpiler->program, &transpiler->plan, &transpiler->diagnostics)
        && rx_simplify_execution_plan(&transpiler->plan, &transpiler->diagnostics);
}

bool rx_transpiler_lower(RxTranspiler *transpiler)
{
    free_lowered_program(&transpiler->lowered);
    return rx_lower_execution_plan(&transpiler->plan, &transpiler->lowered, &transpiler->diagnostics);
}

bool rx_transpiler_emit(
    RxTranspiler *transpiler,
    const RxCCodegenOptions *options,
    RxStringBuilder *out)
{
    return rx_build_planner_ir(&transpiler->lowered, &transpiler->planner_ir, &transpiler->diagnostics)
        && rx_validate_planner_ir(&transpiler->planner_ir, &transpiler->diagnostics)
        && rx_emit_c_program(&transpiler->planner_ir, options, out, &transpiler->diagnostics);
}

bool rx_transpiler_emit_segment(
    RxTranspiler *transpiler,
    int pipeline_index,
    int segment_index,
    const RxCCodegenOptions *options,
    RxStringBuilder *out)
{
    if (pipeline_index < 0 || pipeline_index >= transpiler->lowered.pipeline_count)
    {
        return false;
    }
    if (segment_index != 0)
    {
        return false;
    }
    if (!rx_build_planner_ir(&transpiler->lowered, &transpiler->planner_ir, &transpiler->diagnostics)
        || !rx_validate_planner_ir(&transpiler->planner_ir, &transpiler->diagnostics))
    {
        return false;
    }
    return rx_emit_c_segment_function(
        &transpiler->planner_ir.pipelines[pipeline_index],
        options,
        out,
        &transpiler->diagnostics);
}
