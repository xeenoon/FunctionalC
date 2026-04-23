#ifndef CORE_PLANNER_COMPILED_SEGMENT_H
#define CORE_PLANNER_COMPILED_SEGMENT_H

#include "common.h"
#include "loop_ir.h"

typedef struct
{
    RxCompiledSegment compiled;
    const char *symbol_name;
    bool takes_list_input;
    bool returns_list_output;
} RxCompiledSegmentBinding;

bool rx_compiled_segment_accepts_runtime_layout(
    const RxCompiledSegment *segment);

#endif
