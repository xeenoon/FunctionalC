#ifndef CORE_PLANNER_ASTGEN_H
#define CORE_PLANNER_ASTGEN_H

#include "diagnostics.h"
#include "c_model.h"
#include "planner_ir.h"
#include "string_builder.h"

typedef struct
{
    bool emit_main;
    bool emit_helpers;
    bool emit_segment_bridge;
    bool enable_graph_optimizations;
    const char *header_path;
    const char *helper_source_text;
} RxCCodegenOptions;

bool rx_emit_c_program(
    const RxPlannerIrProgram *program,
    const RxCCodegenOptions *options,
    RxStringBuilder *out,
    RxDiagnosticBag *diagnostics);

bool rx_build_c_program(
    const RxPlannerIrProgram *program,
    const RxCCodegenOptions *options,
    RxCProgram *out_program,
    RxDiagnosticBag *diagnostics);

bool rx_emit_c_segment_function(
    const RxPlannerIrPipeline *pipeline,
    const RxCCodegenOptions *options,
    RxStringBuilder *out,
    RxDiagnosticBag *diagnostics);

#endif
