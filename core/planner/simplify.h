#ifndef CORE_PLANNER_SIMPLIFY_H
#define CORE_PLANNER_SIMPLIFY_H

#include "diagnostics.h"
#include "pipeline_plan.h"

bool rx_build_execution_plan(
    const RxProgramIr *program,
    RxExecutionPlan *plan,
    RxDiagnosticBag *diagnostics);

bool rx_simplify_execution_plan(
    RxExecutionPlan *plan,
    RxDiagnosticBag *diagnostics);

#endif
