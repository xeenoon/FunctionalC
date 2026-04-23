#ifndef CORE_PLANNER_PIPELINE_IR_H
#define CORE_PLANNER_PIPELINE_IR_H

#include "common.h"
#include "function_registry.h"

typedef enum
{
    RX_LITERAL_NONE,
    RX_LITERAL_INT,
    RX_LITERAL_LONG,
    RX_LITERAL_POINTER,
    RX_LITERAL_SYMBOL
} RxLiteralKind;

typedef struct
{
    RxLiteralKind kind;
    union
    {
        int int_value;
        long long_value;
        const void *pointer_value;
        const char *symbol_name;
    } as;
} RxLiteral;

typedef enum
{
    RX_BINDING_NONE,
    RX_BINDING_LITERAL,
    RX_BINDING_FUNCTION_NAME,
    RX_BINDING_SOURCE_REF,
    RX_BINDING_RUNTIME_VALUE
} RxBindingKind;

typedef struct
{
    RxBindingKind kind;
    RxArgumentType value_type;
    union
    {
        RxLiteral literal;
        const char *function_name;
        int source_index;
        const char *runtime_symbol;
    } as;
} RxBinding;

typedef struct
{
    const RxFunctionSignature *signature;
    RxBinding arguments[RX_MAX_CALL_ARGUMENTS];
} RxSourceCall;

typedef struct
{
    const RxFunctionSignature *signature;
    RxBinding arguments[RX_MAX_CALL_ARGUMENTS];
} RxStageCall;

typedef struct
{
    const char *name;
    RxSourceCall source;
    RxStageCall *stages;
    int stage_count;
} RxPipelineIr;

typedef struct
{
    RxPipelineIr *pipelines;
    int pipeline_count;
} RxProgramIr;

#endif
