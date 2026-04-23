#ifndef CORE_PLANNER_C_CODEGEN_H
#define CORE_PLANNER_C_CODEGEN_H

#include "diagnostics.h"
#include "loop_ir.h"
#include "string_builder.h"

typedef struct
{
    bool emit_main;
    bool emit_helpers;
    bool emit_segment_bridge;
    const char *header_path;
    const char *helper_source_text;
} RxCCodegenOptions;

bool rx_emit_c_program(
    const RxLoweredProgram *program,
    const RxCCodegenOptions *options,
    RxStringBuilder *out,
    RxDiagnosticBag *diagnostics);

bool rx_emit_c_segment_function(
    const RxLoweredPipeline *pipeline,
    const RxCCodegenOptions *options,
    RxStringBuilder *out,
    RxDiagnosticBag *diagnostics);

#endif
