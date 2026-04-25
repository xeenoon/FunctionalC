#ifndef CORE_PLANNER_GRAPH_OPT_H
#define CORE_PLANNER_GRAPH_OPT_H

#include "c_codegen.h"

bool rx_try_emit_graph_optimized_loop_body(
    const RxPlannerIrPipeline *pipeline,
    const RxCCodegenOptions *options,
    RxStringBuilder *out);

#endif
