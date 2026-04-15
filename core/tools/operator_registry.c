#include "operator_registry.h"

#include <string.h>

static const OperatorInfo k_operator_table[] = {
    { OP_MAP, "map", "map", true, false, ARGUMENT_FUNCTION, FUNCTION_VALUE },
    { OP_FILTER, "filter", "filter", true, false, ARGUMENT_FUNCTION, FUNCTION_PREDICATE },
    { OP_REDUCE, "reduce", "reduce", true, false, ARGUMENT_FUNCTION_AND_LITERAL, FUNCTION_ACCUMULATOR },
    { OP_SCAN, "scan", "scan", true, false, ARGUMENT_FUNCTION, FUNCTION_ACCUMULATOR },
    { OP_SCANFROM, "scanfrom", "scanfrom", true, false, ARGUMENT_FUNCTION_AND_LITERAL, FUNCTION_ACCUMULATOR },
    { OP_MAP_TO, "mapTo", "mapTo", true, false, ARGUMENT_LITERAL, FUNCTION_UNKNOWN },
    { OP_TAKE, "take", "take", true, false, ARGUMENT_LITERAL, FUNCTION_UNKNOWN },
    { OP_SKIP, "skip", "skip", true, false, ARGUMENT_LITERAL, FUNCTION_UNKNOWN },
    { OP_TAKE_WHILE, "takeWhile", "takeWhile", true, false, ARGUMENT_FUNCTION, FUNCTION_PREDICATE },
    { OP_SKIP_WHILE, "skipWhile", "skipWhile", true, false, ARGUMENT_FUNCTION, FUNCTION_PREDICATE },
    { OP_DISTINCT, "distinct", "distinct", false, false, ARGUMENT_FUNCTION, FUNCTION_PROJECTOR },
    { OP_DISTINCT_UNTIL_CHANGED, "distinctUntilChanged", "distinctUntilChanged", true, false, ARGUMENT_FUNCTION, FUNCTION_PROJECTOR },
    { OP_TAKE_UNTIL, "takeUntil", "takeUntil", true, false, ARGUMENT_LITERAL, FUNCTION_UNKNOWN },
    { OP_SKIP_UNTIL, "skipUntil", "skipUntil", true, false, ARGUMENT_LITERAL, FUNCTION_UNKNOWN },
    { OP_LAST, "last", "last", true, false, ARGUMENT_NONE, FUNCTION_UNKNOWN },
    { OP_FIRST, "first", "first", true, false, ARGUMENT_NONE, FUNCTION_UNKNOWN },
    { OP_MERGE_MAP, "mergeMap", "mergeMap", false, false, ARGUMENT_SOURCE, FUNCTION_UNKNOWN },
    { OP_BUFFER, "buffer", "buffer", false, true, ARGUMENT_SOURCE, FUNCTION_UNKNOWN },
    { OP_THROTTLE_TIME, "throttleTime", "throttleTime", false, true, ARGUMENT_LITERAL, FUNCTION_UNKNOWN },
};

const OperatorInfo *find_operator_info(OperatorKind kind)
{
    int count = (int)(sizeof(k_operator_table) / sizeof(k_operator_table[0]));
    for (int i = 0; i < count; ++i)
    {
        if (k_operator_table[i].kind == kind)
        {
            return &k_operator_table[i];
        }
    }
    return NULL;
}

const OperatorInfo *find_operator_info_by_name(const char *name)
{
    int count = (int)(sizeof(k_operator_table) / sizeof(k_operator_table[0]));
    for (int i = 0; i < count; ++i)
    {
        if (strcmp(k_operator_table[i].dsl_name, name) == 0)
        {
            return &k_operator_table[i];
        }
    }
    return NULL;
}

const char *source_kind_name(SourceKind kind)
{
    switch (kind)
    {
    case SOURCE_RANGE:
        return "range";
    case SOURCE_OF:
        return "of";
    case SOURCE_EMPTY:
        return "empty";
    case SOURCE_NEVER:
        return "never";
    case SOURCE_INTERVAL:
        return "interval";
    case SOURCE_TIMER:
        return "timer";
    case SOURCE_DEFER:
        return "defer";
    case SOURCE_FROM:
        return "from";
    case SOURCE_ZIP:
        return "zip";
    }
    return "unknown";
}
