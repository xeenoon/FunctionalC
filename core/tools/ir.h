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
    OperatorAst *ops;
    int op_count;
    int op_capacity;
    char subscriber_target[64];
} ChainIr;

typedef struct
{
    FunctionInfo *functions;
    int function_count;
    int function_capacity;
    ChainIr *chains;
    int chain_count;
    int chain_capacity;
} ProgramIr;

#endif
