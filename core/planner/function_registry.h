#ifndef CORE_PLANNER_FUNCTION_REGISTRY_H
#define CORE_PLANNER_FUNCTION_REGISTRY_H

#include "common.h"

typedef enum
{
    RX_CALL_SOURCE,
    RX_CALL_OPERATOR,
    RX_CALL_TERMINAL,
    RX_CALL_RUNTIME
} RxCallKind;

typedef enum
{
    RX_SEGMENT_RULE_FLEXIBLE,
    RX_SEGMENT_RULE_MUST_START_SEGMENT,
    RX_SEGMENT_RULE_MUST_END_SEGMENT,
    RX_SEGMENT_RULE_MUST_STAY_WHOLE
} RxSegmentBoundaryRule;

typedef enum
{
    RX_ARG_NONE,
    RX_ARG_INT,
    RX_ARG_LONG,
    RX_ARG_VOID_PTR,
    RX_ARG_LIST_PTR,
    RX_ARG_OBSERVABLE_PTR,
    RX_ARG_QUERY_PTR,
    RX_ARG_SUBSCRIBER_FN,
    RX_ARG_BOOLEAN_FN,
    RX_ARG_MODIFIER_FN,
    RX_ARG_ACCUMULATOR_FN,
    RX_ARG_COMPARISON_FN,
    RX_ARG_FACTORY_FN,
    RX_ARG_VARIADIC_VOID_PTR,
    RX_ARG_VARIADIC_OBSERVABLE_PTR
} RxArgumentType;

typedef struct
{
    const char *name;
    RxCallKind kind;
    int min_arity;
    int max_arity;
    RxArgumentType return_type;
    RxArgumentType argument_types[RX_MAX_CALL_ARGUMENTS];
    bool timer_based;
    bool transpile_candidate;
    bool preserves_runtime_layout;
    bool accepts_runtime_layout_input;
    bool returns_runtime_layout_output;
    RxSegmentBoundaryRule segment_rule;
} RxFunctionSignature;

typedef struct
{
    const RxFunctionSignature *items;
    size_t count;
} RxFunctionRegistry;

const RxFunctionRegistry *rx_default_function_registry(void);
const RxFunctionSignature *rx_find_function_signature(
    const RxFunctionRegistry *registry,
    const char *name);

#endif
