#ifndef CORE_PLANNER_TRANSPILER_H
#define CORE_PLANNER_TRANSPILER_H

#include "c_codegen.h"
#include "compiled_segment.h"
#include "diagnostics.h"
#include "function_registry.h"
#include "loop_ir.h"
#include "pipeline_ir.h"
#include "pipeline_plan.h"

typedef struct
{
    const RxFunctionRegistry *registry;
    RxProgramIr program;
    RxExecutionPlan plan;
    RxLoweredProgram lowered;
    RxCompiledSegmentBinding *compiled_segments;
    int compiled_segment_count;
    RxDiagnosticBag diagnostics;
} RxTranspiler;

void rx_transpiler_init(
    RxTranspiler *transpiler,
    const RxFunctionRegistry *registry);

void rx_transpiler_reset(RxTranspiler *transpiler);

bool rx_transpiler_plan(RxTranspiler *transpiler);
bool rx_transpiler_lower(RxTranspiler *transpiler);
bool rx_transpiler_emit(
    RxTranspiler *transpiler,
    const RxCCodegenOptions *options,
    RxStringBuilder *out);

bool rx_transpiler_emit_segment(
    RxTranspiler *transpiler,
    int pipeline_index,
    int segment_index,
    const RxCCodegenOptions *options,
    RxStringBuilder *out);

#endif
