#include "compiled_segment.h"

bool rx_compiled_segment_accepts_runtime_layout(
    const RxCompiledSegment *segment)
{
    return segment != NULL && segment->returns_observable_layout;
}
