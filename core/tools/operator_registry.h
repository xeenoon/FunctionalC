#ifndef OPERATOR_REGISTRY_H
#define OPERATOR_REGISTRY_H

#include <stdbool.h>

#include "dsl_ast.h"
#include "ir.h"

typedef enum
{
    ARGUMENT_NONE,
    ARGUMENT_FUNCTION,
    ARGUMENT_LITERAL,
    ARGUMENT_FUNCTION_AND_LITERAL,
    ARGUMENT_SOURCE
} OperatorArgumentKind;

typedef struct
{
    OperatorKind kind;
    const char *dsl_name;
    const char *runtime_name;
    bool fusible;
    bool requires_runtime_self;
    OperatorArgumentKind argument_kind;
    FunctionKind expected_function_kind;
} OperatorInfo;

const OperatorInfo *find_operator_info(OperatorKind kind);
const OperatorInfo *find_operator_info_by_name(const char *name);
const char *source_kind_name(SourceKind kind);

#endif
