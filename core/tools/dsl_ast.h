#ifndef DSL_AST_H
#define DSL_AST_H

#include <stdbool.h>

typedef enum
{
    EXPR_IDENT,
    EXPR_NUMBER,
    EXPR_CALL
} ExprKind;

typedef struct Expr Expr;

struct Expr
{
    ExprKind kind;
    char text[64];
    Expr *args[8];
    int arg_count;
};

typedef struct
{
    char name[64];
    char params[2][32];
    int param_count;
    Expr *body;
} FnDef;

typedef enum
{
    OP_MAP,
    OP_FILTER,
    OP_REDUCE,
    OP_TAKE
} OperatorKind;

typedef struct
{
    OperatorKind kind;
    char function_name[64];
    char literal[64];
} Operator;

typedef struct
{
    char range_min[64];
    char range_max[64];
    Operator ops[16];
    int op_count;
    char subscriber_target[64];
} Chain;

typedef struct
{
    FnDef functions[64];
    int function_count;
    Chain chains[32];
    int chain_count;
} Program;

#endif
