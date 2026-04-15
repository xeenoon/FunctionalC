#ifndef IR_H
#define IR_H

#include <stdbool.h>

#include "dsl_ast.h"

typedef enum
{
    FUNCTION_VALUE,
    FUNCTION_PREDICATE,
    FUNCTION_ACCUMULATOR,
    FUNCTION_PROJECTOR,
    FUNCTION_FACTORY,
    FUNCTION_UNKNOWN
} FunctionKind;

typedef struct
{
    const FnDef *def;
    FunctionKind kind;
    bool used;
} FunctionInfo;

typedef struct
{
    SourceAst source;
    OperatorAst ops[32];
    int op_count;
    char subscriber_target[64];
} ChainIr;

typedef struct
{
    FunctionInfo functions[64];
    int function_count;
    ChainIr chains[32];
    int chain_count;
} ProgramIr;

#endif
