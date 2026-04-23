#ifndef CORE_PLANNER_LOWER_H
#define CORE_PLANNER_LOWER_H

#include "diagnostics.h"
#include "loop_ir.h"

bool rx_lower_execution_plan(
    const RxExecutionPlan *plan,
    RxLoweredProgram *lowered,
    RxDiagnosticBag *diagnostics);

bool rx_lower_pipeline_segment(
    const RxPlannedPipeline *pipeline,
    const RxPipelineSegment *segment,
    RxLoweredPipeline *lowered,
    RxDiagnosticBag *diagnostics);

#endif
