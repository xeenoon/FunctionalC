#include "function_registry.h"

#include <string.h>

static const RxFunctionSignature k_default_functions[] = {
    { "range", RX_CALL_SOURCE, 2, 2, RX_ARG_OBSERVABLE_PTR,
      { RX_ARG_INT, RX_ARG_INT, RX_ARG_NONE, RX_ARG_NONE },
      false, true, false, false, false, RX_SEGMENT_RULE_FLEXIBLE },
    { "zip_range", RX_CALL_SOURCE, 1, 1, RX_ARG_OBSERVABLE_PTR,
      { RX_ARG_INT, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, true, false, false, false, RX_SEGMENT_RULE_FLEXIBLE },
    { "zip_merge_map_range", RX_CALL_SOURCE, 1, 1, RX_ARG_OBSERVABLE_PTR,
      { RX_ARG_INT, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, true, false, false, false, RX_SEGMENT_RULE_FLEXIBLE },
    { "of", RX_CALL_SOURCE, 1, -1, RX_ARG_OBSERVABLE_PTR,
      { RX_ARG_INT, RX_ARG_VARIADIC_VOID_PTR, RX_ARG_NONE, RX_ARG_NONE },
      false, false, false, false, false, RX_SEGMENT_RULE_FLEXIBLE },
    { "empty", RX_CALL_SOURCE, 0, 0, RX_ARG_OBSERVABLE_PTR,
      { RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, false, false, false, false, RX_SEGMENT_RULE_FLEXIBLE },
    { "never", RX_CALL_SOURCE, 0, 0, RX_ARG_OBSERVABLE_PTR,
      { RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, false, false, false, false, RX_SEGMENT_RULE_MUST_STAY_WHOLE },
    { "from", RX_CALL_SOURCE, 1, 1, RX_ARG_OBSERVABLE_PTR,
      { RX_ARG_LIST_PTR, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, false, false, false, false, RX_SEGMENT_RULE_MUST_STAY_WHOLE },
    { "interval", RX_CALL_SOURCE, 1, 1, RX_ARG_OBSERVABLE_PTR,
      { RX_ARG_LONG, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      true, false, false, false, false, RX_SEGMENT_RULE_MUST_STAY_WHOLE },
    { "timer", RX_CALL_SOURCE, 2, 2, RX_ARG_OBSERVABLE_PTR,
      { RX_ARG_LONG, RX_ARG_INT, RX_ARG_NONE, RX_ARG_NONE },
      true, false, false, false, false, RX_SEGMENT_RULE_MUST_STAY_WHOLE },
    { "defer", RX_CALL_SOURCE, 1, 1, RX_ARG_OBSERVABLE_PTR,
      { RX_ARG_FACTORY_FN, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, false, false, false, false, RX_SEGMENT_RULE_MUST_STAY_WHOLE },
    { "zip", RX_CALL_SOURCE, 2, -1, RX_ARG_OBSERVABLE_PTR,
      { RX_ARG_INT, RX_ARG_VARIADIC_OBSERVABLE_PTR, RX_ARG_NONE, RX_ARG_NONE },
      false, false, true, true, true, RX_SEGMENT_RULE_MUST_STAY_WHOLE },
    { "map", RX_CALL_OPERATOR, 1, 1, RX_ARG_QUERY_PTR,
      { RX_ARG_MODIFIER_FN, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, true, false, false, false, RX_SEGMENT_RULE_FLEXIBLE },
    { "pairMap", RX_CALL_OPERATOR, 1, 1, RX_ARG_QUERY_PTR,
      { RX_ARG_MODIFIER_FN, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, true, false, false, false, RX_SEGMENT_RULE_FLEXIBLE },
    { "tripleMap", RX_CALL_OPERATOR, 1, 1, RX_ARG_QUERY_PTR,
      { RX_ARG_MODIFIER_FN, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, true, false, false, false, RX_SEGMENT_RULE_FLEXIBLE },
    { "filter", RX_CALL_OPERATOR, 1, 1, RX_ARG_QUERY_PTR,
      { RX_ARG_BOOLEAN_FN, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, true, false, false, false, RX_SEGMENT_RULE_FLEXIBLE },
    { "scan", RX_CALL_OPERATOR, 1, 1, RX_ARG_QUERY_PTR,
      { RX_ARG_ACCUMULATOR_FN, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, true, false, false, false, RX_SEGMENT_RULE_FLEXIBLE },
    { "scanfrom", RX_CALL_OPERATOR, 2, 2, RX_ARG_QUERY_PTR,
      { RX_ARG_ACCUMULATOR_FN, RX_ARG_VOID_PTR, RX_ARG_NONE, RX_ARG_NONE },
      false, true, false, false, false, RX_SEGMENT_RULE_FLEXIBLE },
    { "reduce", RX_CALL_OPERATOR, 1, 2, RX_ARG_QUERY_PTR,
      { RX_ARG_ACCUMULATOR_FN, RX_ARG_VOID_PTR, RX_ARG_NONE, RX_ARG_NONE },
      false, true, false, false, false, RX_SEGMENT_RULE_MUST_END_SEGMENT },
    { "mapTo", RX_CALL_OPERATOR, 1, 1, RX_ARG_QUERY_PTR,
      { RX_ARG_VOID_PTR, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, true, false, false, false, RX_SEGMENT_RULE_FLEXIBLE },
    { "take", RX_CALL_OPERATOR, 1, 1, RX_ARG_QUERY_PTR,
      { RX_ARG_INT, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, true, false, false, false, RX_SEGMENT_RULE_FLEXIBLE },
    { "skip", RX_CALL_OPERATOR, 1, 1, RX_ARG_QUERY_PTR,
      { RX_ARG_INT, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, true, false, false, false, RX_SEGMENT_RULE_FLEXIBLE },
    { "takeWhile", RX_CALL_OPERATOR, 1, 1, RX_ARG_QUERY_PTR,
      { RX_ARG_BOOLEAN_FN, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, true, false, false, false, RX_SEGMENT_RULE_FLEXIBLE },
    { "skipWhile", RX_CALL_OPERATOR, 1, 1, RX_ARG_QUERY_PTR,
      { RX_ARG_BOOLEAN_FN, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, true, false, false, false, RX_SEGMENT_RULE_FLEXIBLE },
    { "distinct", RX_CALL_OPERATOR, 1, 1, RX_ARG_QUERY_PTR,
      { RX_ARG_MODIFIER_FN, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, false, false, false, false, RX_SEGMENT_RULE_MUST_STAY_WHOLE },
    { "distinctUntilChanged", RX_CALL_OPERATOR, 1, 1, RX_ARG_QUERY_PTR,
      { RX_ARG_MODIFIER_FN, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, true, false, false, false, RX_SEGMENT_RULE_FLEXIBLE },
    { "last", RX_CALL_OPERATOR, 0, 0, RX_ARG_QUERY_PTR,
      { RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, true, false, false, false, RX_SEGMENT_RULE_MUST_END_SEGMENT },
    { "first", RX_CALL_OPERATOR, 0, 0, RX_ARG_QUERY_PTR,
      { RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, true, false, false, false, RX_SEGMENT_RULE_MUST_END_SEGMENT },
    { "mergeMap", RX_CALL_OPERATOR, 1, 1, RX_ARG_QUERY_PTR,
      { RX_ARG_OBSERVABLE_PTR, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, false, true, true, true, RX_SEGMENT_RULE_MUST_STAY_WHOLE },
    { "takeUntil", RX_CALL_OPERATOR, 1, 1, RX_ARG_QUERY_PTR,
      { RX_ARG_VOID_PTR, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, false, false, false, false, RX_SEGMENT_RULE_MUST_STAY_WHOLE },
    { "skipUntil", RX_CALL_OPERATOR, 1, 1, RX_ARG_QUERY_PTR,
      { RX_ARG_VOID_PTR, RX_ARG_NONE, RX_ARG_NONE, RX_ARG_NONE },
      false, false, false, false, false, RX_SEGMENT_RULE_MUST_STAY_WHOLE },
    { "buffer", RX_CALL_OPERATOR, 2, 2, RX_ARG_QUERY_PTR,
      { RX_ARG_OBSERVABLE_PTR, RX_ARG_OBSERVABLE_PTR, RX_ARG_NONE, RX_ARG_NONE },
      false, false, true, true, true, RX_SEGMENT_RULE_MUST_STAY_WHOLE },
    { "throttleTime", RX_CALL_OPERATOR, 2, 2, RX_ARG_QUERY_PTR,
      { RX_ARG_OBSERVABLE_PTR, RX_ARG_INT, RX_ARG_NONE, RX_ARG_NONE },
      true, false, false, false, false, RX_SEGMENT_RULE_MUST_STAY_WHOLE },
};

static const RxFunctionRegistry k_default_registry = {
    k_default_functions,
    sizeof(k_default_functions) / sizeof(k_default_functions[0]),
};

const RxFunctionRegistry *rx_default_function_registry(void)
{
    return &k_default_registry;
}

const RxFunctionSignature *rx_find_function_signature(
    const RxFunctionRegistry *registry,
    const char *name)
{
    if (registry == NULL || name == NULL)
    {
        return NULL;
    }

    for (size_t index = 0; index < registry->count; ++index)
    {
        const RxFunctionSignature *signature = &registry->items[index];
        if (strcmp(signature->name, name) == 0)
        {
            return signature;
        }
    }

    return NULL;
}
