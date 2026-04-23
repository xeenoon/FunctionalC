#ifndef CORE_PLANNER_DIAGNOSTICS_H
#define CORE_PLANNER_DIAGNOSTICS_H

#include "common.h"

typedef enum
{
    RX_DIAGNOSTIC_NOTE,
    RX_DIAGNOSTIC_WARNING,
    RX_DIAGNOSTIC_ERROR
} RxDiagnosticLevel;

typedef enum
{
    RX_DIAG_NONE = 0,
    RX_DIAG_UNSUPPORTED_SOURCE,
    RX_DIAG_UNSUPPORTED_OPERATOR,
    RX_DIAG_INVALID_ARGUMENT_COUNT,
    RX_DIAG_INVALID_ARGUMENT_TYPE,
    RX_DIAG_DYNAMIC_RUNTIME_VALUE,
    RX_DIAG_UNLOWERABLE_PIPELINE,
    RX_DIAG_INVALID_SEGMENT_BOUNDARY,
    RX_DIAG_LAYOUT_MISMATCH
} RxDiagnosticCode;

typedef struct
{
    RxDiagnosticLevel level;
    RxDiagnosticCode code;
    const char *message;
    const char *symbol_name;
    int chain_index;
    int stage_index;
} RxDiagnostic;

typedef struct
{
    RxDiagnostic *items;
    int count;
    int capacity;
    bool has_error;
} RxDiagnosticBag;

void rx_diagnostic_bag_init(RxDiagnosticBag *bag);
void rx_diagnostic_bag_reset(RxDiagnosticBag *bag);
bool rx_diagnostic_bag_append(RxDiagnosticBag *bag, RxDiagnostic diagnostic);

#endif
