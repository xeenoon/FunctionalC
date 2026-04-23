#ifndef DSL_AST_H
#define DSL_AST_H

#include <stdbool.h>

typedef enum { EXPR_IDENT, EXPR_NUMBER, EXPR_CALL } ExprKind;

typedef struct Expr Expr;

struct Expr {
    ExprKind kind;
    char text[64];
    Expr *args[8];
    int arg_count;
};

typedef struct {
    char name[64];
    char params[2][32];
    int param_count;
    Expr *body;
} FnDef;

typedef enum {
    SOURCE_RANGE,
    SOURCE_OF,
    SOURCE_EMPTY,
    SOURCE_NEVER,
    SOURCE_INTERVAL,
    SOURCE_TIMER,
    SOURCE_DEFER,
    SOURCE_FROM,
    SOURCE_ZIP
} SourceKind;

typedef struct SourceAst SourceAst;

struct SourceAst {
    SourceKind kind;
    char values[8][64];
    int value_count;
    SourceAst *sources[8];
    int source_count;
};

typedef enum {
    OP_MAP,
    OP_FILTER,
    OP_REDUCE,
    OP_SCAN,
    OP_SCANFROM,
    OP_MAP_TO,
    OP_TAKE,
    OP_SKIP,
    OP_TAKE_WHILE,
    OP_SKIP_WHILE,
    OP_DISTINCT,
    OP_DISTINCT_UNTIL_CHANGED,
    OP_TAKE_UNTIL,
    OP_SKIP_UNTIL,
    OP_LAST,
    OP_FIRST,
    OP_MERGE_MAP,
    OP_BUFFER,
    OP_THROTTLE_TIME
} OperatorKind;

typedef struct {
    OperatorKind kind;
    char symbol[64];
    char extra[64];
    SourceAst *source_arg;
    bool has_extra;
} OperatorAst;

typedef struct {
    SourceAst *source;
    OperatorAst ops[32];
    int op_count;
    char subscriber_target[64];
} ChainAst;

typedef struct {
    FnDef functions[64];
    int function_count;
    ChainAst chains[32];
    int chain_count;
} ProgramAst;

#endif
