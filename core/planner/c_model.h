#ifndef CORE_PLANNER_C_MODEL_H
#define CORE_PLANNER_C_MODEL_H

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t RxCStringId;

typedef struct
{
    char **items;
    int count;
    int capacity;
} RxCStringPool;

typedef enum
{
    RX_C_TOPLEVEL_RAW,
    RX_C_TOPLEVEL_INCLUDE_SYSTEM,
    RX_C_TOPLEVEL_INCLUDE_LOCAL,
    RX_C_TOPLEVEL_DEFINE,
    RX_C_TOPLEVEL_FUNCTION
} RxCTopLevelKind;

typedef enum
{
    RX_C_TYPE_RAW,
    RX_C_TYPE_NAMED,
    RX_C_TYPE_POINTER
} RxCTypeKind;

typedef struct RxCType
{
    RxCTypeKind kind;
    RxCStringId text_id;
    struct RxCType *child;
} RxCType;

typedef enum
{
    RX_C_EXPR_RAW,
    RX_C_EXPR_IDENT,
    RX_C_EXPR_INT,
    RX_C_EXPR_BOOL,
    RX_C_EXPR_CALL,
    RX_C_EXPR_CAST,
    RX_C_EXPR_UNARY,
    RX_C_EXPR_BINARY,
    RX_C_EXPR_INDEX
} RxCExprKind;

typedef struct RxCExpr
{
    RxCExprKind kind;
    RxCStringId text_id;
    RxCStringId op_id;
    int64_t int_value;
    bool bool_value;
    struct RxCExpr *left;
    struct RxCExpr *right;
    struct RxCExpr **args;
    int arg_count;
    int arg_capacity;
    RxCType cast_type;
} RxCExpr;

typedef enum
{
    RX_C_STMT_RAW,
    RX_C_STMT_DECL,
    RX_C_STMT_EXPR,
    RX_C_STMT_ASSIGN,
    RX_C_STMT_RETURN,
    RX_C_STMT_IF,
    RX_C_STMT_FOR,
    RX_C_STMT_WHILE,
    RX_C_STMT_BREAK,
    RX_C_STMT_CONTINUE,
    RX_C_STMT_BLOCK
} RxCStmtKind;

typedef struct RxCBlock RxCBlock;

typedef struct RxCStmt
{
    RxCStmtKind kind;
    RxCStringId text_id;
    RxCStringId name_id;
    RxCType type;
    RxCExpr expr;
    RxCExpr init_expr;
    RxCExpr condition;
    RxCExpr update;
    RxCBlock *then_block;
    RxCBlock *else_block;
    RxCBlock *body_block;
} RxCStmt;

struct RxCBlock
{
    RxCStmt *items;
    int count;
    int capacity;
};

typedef struct
{
    RxCType type;
    RxCStringId name_id;
} RxCParam;

typedef struct
{
    RxCType return_type;
    RxCStringId name_id;
    RxCParam *params;
    int param_count;
    int param_capacity;
    RxCBlock body;
} RxCFunction;

typedef struct
{
    RxCTopLevelKind kind;
    RxCStringId text_id;
    RxCStringId extra_text_id;
    RxCFunction function;
} RxCTopLevel;

typedef struct
{
    RxCStringPool strings;
    RxCTopLevel *items;
    int count;
    int capacity;
} RxCProgram;

void rx_c_string_pool_init(RxCStringPool *pool);
void rx_c_string_pool_reset(RxCStringPool *pool);
RxCStringId rx_c_string_pool_intern(RxCStringPool *pool, const char *text);
const char *rx_c_string_pool_get(const RxCStringPool *pool, RxCStringId id);

void rx_c_program_init(RxCProgram *program);
void rx_c_program_reset(RxCProgram *program);
bool rx_c_program_add_raw(RxCProgram *program, const char *text);
bool rx_c_program_add_line(RxCProgram *program, const char *text);
bool rx_c_program_add_include_system(RxCProgram *program, const char *header);
bool rx_c_program_add_include_local(RxCProgram *program, const char *header);
bool rx_c_program_add_define(RxCProgram *program, const char *name, const char *value);

void rx_c_type_init(RxCType *type);
void rx_c_type_reset(RxCType *type);
bool rx_c_type_set_raw(RxCProgram *program, RxCType *type, const char *text);
bool rx_c_type_set_named(RxCProgram *program, RxCType *type, const char *text);
bool rx_c_type_set_pointer_to(RxCProgram *program, RxCType *type, const RxCType *child);
bool rx_c_type_clone(RxCType *dst, const RxCType *src);

void rx_c_expr_init(RxCExpr *expr);
void rx_c_expr_reset(RxCExpr *expr);
bool rx_c_expr_set_raw(RxCProgram *program, RxCExpr *expr, const char *text);
bool rx_c_expr_set_ident(RxCProgram *program, RxCExpr *expr, const char *name);
bool rx_c_expr_set_int(RxCExpr *expr, int64_t value);
bool rx_c_expr_set_bool(RxCExpr *expr, bool value);
bool rx_c_expr_set_unary(RxCProgram *program, RxCExpr *expr, const char *op, const RxCExpr *child);
bool rx_c_expr_set_binary(RxCProgram *program, RxCExpr *expr, const char *op, const RxCExpr *left, const RxCExpr *right);
bool rx_c_expr_set_cast(RxCProgram *program, RxCExpr *expr, const RxCType *type, const RxCExpr *child);
bool rx_c_expr_set_call(RxCProgram *program, RxCExpr *expr, const char *callee, const RxCExpr *args, int arg_count);
bool rx_c_expr_set_index(RxCExpr *expr, const RxCExpr *base, const RxCExpr *index);
bool rx_c_expr_clone(RxCExpr *dst, const RxCExpr *src);

void rx_c_block_init(RxCBlock *block);
void rx_c_block_reset(RxCBlock *block);
bool rx_c_block_add_raw(RxCProgram *program, RxCBlock *block, const char *text);
bool rx_c_block_add_decl(RxCProgram *program, RxCBlock *block, const char *type_text, const char *name_text, const char *init_text);
bool rx_c_block_add_decl_expr(RxCProgram *program, RxCBlock *block, const RxCType *type, const char *name_text, const RxCExpr *init_expr);
bool rx_c_block_add_expr(RxCProgram *program, RxCBlock *block, const char *expr_text);
bool rx_c_block_add_expr_stmt(RxCProgram *program, RxCBlock *block, const RxCExpr *expr);
bool rx_c_block_add_assign_stmt(RxCProgram *program, RxCBlock *block, const RxCExpr *left, const RxCExpr *right);
bool rx_c_block_add_return(RxCProgram *program, RxCBlock *block, const char *expr_text);
bool rx_c_block_add_return_expr(RxCProgram *program, RxCBlock *block, const RxCExpr *expr);
bool rx_c_block_add_break(RxCProgram *program, RxCBlock *block);
bool rx_c_block_add_continue(RxCProgram *program, RxCBlock *block);
RxCBlock *rx_c_stmt_set_if(RxCProgram *program, RxCBlock *block, const char *condition_text);
RxCBlock *rx_c_stmt_set_if_expr(RxCProgram *program, RxCBlock *block, const RxCExpr *condition);
RxCBlock *rx_c_stmt_set_if_else(RxCProgram *program, RxCBlock *block, const char *condition_text, bool create_else_block);
RxCBlock *rx_c_stmt_set_for(RxCProgram *program, RxCBlock *block, const char *init_text, const char *condition_text, const char *update_text);
RxCBlock *rx_c_stmt_set_while_expr(RxCProgram *program, RxCBlock *block, const RxCExpr *condition);
RxCBlock *rx_c_block_add_nested_block(RxCProgram *program, RxCBlock *block);
bool rx_c_program_add_function(RxCProgram *program, const char *return_type, const char *name, const RxCParam *params, int param_count, const RxCBlock *body);

#endif
