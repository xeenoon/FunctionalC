#include "c_model.h"

#include <stdlib.h>
#include <string.h>

static void rx_c_function_reset(RxCFunction *function);
static void rx_c_stmt_reset(RxCStmt *stmt);
static bool rx_c_program_reserve(RxCProgram *program, int min_capacity);
static bool rx_c_block_reserve(RxCBlock *block, int min_capacity);
static bool rx_c_program_add_top_level(RxCProgram *program, RxCTopLevelKind kind, RxCStringId text_id, RxCStringId extra_text_id);
static RxCStringId intern_or_invalid(RxCProgram *program, const char *text);
static bool rx_c_block_clone_into(RxCBlock *dst, const RxCBlock *src);
static bool rx_c_stmt_clone_into(RxCStmt *dst, const RxCStmt *src);
static RxCStmt *rx_c_block_append_stmt(RxCBlock *block, RxCStmtKind kind);

void rx_c_string_pool_init(RxCStringPool *pool)
{
    memset(pool, 0, sizeof(*pool));
}

void rx_c_string_pool_reset(RxCStringPool *pool)
{
    if (pool == NULL)
    {
        return;
    }
    for (int index = 0; index < pool->count; ++index)
    {
        free(pool->items[index]);
    }
    free(pool->items);
    memset(pool, 0, sizeof(*pool));
}

static bool rx_c_string_pool_reserve(RxCStringPool *pool, int min_capacity)
{
    if (pool->capacity >= min_capacity)
    {
        return true;
    }
    int next_capacity = pool->capacity > 0 ? pool->capacity * 2 : 16;
    while (next_capacity < min_capacity)
    {
        next_capacity *= 2;
    }
    char **next_items = realloc(pool->items, (size_t)next_capacity * sizeof(*next_items));
    if (next_items == NULL)
    {
        return false;
    }
    pool->items = next_items;
    pool->capacity = next_capacity;
    return true;
}

RxCStringId rx_c_string_pool_intern(RxCStringPool *pool, const char *text)
{
    if (pool == NULL || text == NULL)
    {
        return UINT32_MAX;
    }
    for (int index = 0; index < pool->count; ++index)
    {
        if (strcmp(pool->items[index], text) == 0)
        {
            return (RxCStringId)index;
        }
    }
    if (!rx_c_string_pool_reserve(pool, pool->count + 1))
    {
        return UINT32_MAX;
    }
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (copy == NULL)
    {
        return UINT32_MAX;
    }
    memcpy(copy, text, length + 1);
    pool->items[pool->count] = copy;
    pool->count += 1;
    return (RxCStringId)(pool->count - 1);
}

const char *rx_c_string_pool_get(const RxCStringPool *pool, RxCStringId id)
{
    if (pool == NULL || id >= (RxCStringId)pool->count)
    {
        return NULL;
    }
    return pool->items[id];
}

static RxCStringId intern_or_invalid(RxCProgram *program, const char *text)
{
    return program == NULL || text == NULL ? UINT32_MAX : rx_c_string_pool_intern(&program->strings, text);
}

void rx_c_program_init(RxCProgram *program)
{
    memset(program, 0, sizeof(*program));
    rx_c_string_pool_init(&program->strings);
}

void rx_c_type_init(RxCType *type)
{
    if (type != NULL)
    {
        memset(type, 0, sizeof(*type));
        type->text_id = UINT32_MAX;
    }
}

void rx_c_type_reset(RxCType *type)
{
    if (type == NULL)
    {
        return;
    }
    if (type->child != NULL)
    {
        rx_c_type_reset(type->child);
        free(type->child);
    }
    memset(type, 0, sizeof(*type));
}

bool rx_c_type_set_raw(RxCProgram *program, RxCType *type, const char *text)
{
    if (type == NULL)
    {
        return false;
    }
    rx_c_type_reset(type);
    type->kind = RX_C_TYPE_RAW;
    type->text_id = intern_or_invalid(program, text);
    return type->text_id != UINT32_MAX;
}

bool rx_c_type_set_named(RxCProgram *program, RxCType *type, const char *text)
{
    if (type == NULL)
    {
        return false;
    }
    rx_c_type_reset(type);
    type->kind = RX_C_TYPE_NAMED;
    type->text_id = intern_or_invalid(program, text);
    return type->text_id != UINT32_MAX;
}

bool rx_c_type_clone(RxCType *dst, const RxCType *src)
{
    if (dst == NULL || src == NULL)
    {
        return false;
    }
    rx_c_type_reset(dst);
    *dst = *src;
    dst->child = NULL;
    if (src->child != NULL)
    {
        dst->child = calloc(1, sizeof(*dst->child));
        if (dst->child == NULL)
        {
            return false;
        }
        rx_c_type_init(dst->child);
        if (!rx_c_type_clone(dst->child, src->child))
        {
            return false;
        }
    }
    return true;
}

bool rx_c_type_set_pointer_to(RxCProgram *program, RxCType *type, const RxCType *child)
{
    (void)program;
    if (type == NULL || child == NULL)
    {
        return false;
    }
    rx_c_type_reset(type);
    type->kind = RX_C_TYPE_POINTER;
    type->child = calloc(1, sizeof(*type->child));
    if (type->child == NULL)
    {
        return false;
    }
    rx_c_type_init(type->child);
    return rx_c_type_clone(type->child, child);
}

void rx_c_expr_init(RxCExpr *expr)
{
    if (expr != NULL)
    {
        memset(expr, 0, sizeof(*expr));
        expr->text_id = UINT32_MAX;
        expr->op_id = UINT32_MAX;
        rx_c_type_init(&expr->cast_type);
    }
}

void rx_c_expr_reset(RxCExpr *expr)
{
    if (expr == NULL)
    {
        return;
    }
    if (expr->left != NULL)
    {
        rx_c_expr_reset(expr->left);
        free(expr->left);
    }
    if (expr->right != NULL)
    {
        rx_c_expr_reset(expr->right);
        free(expr->right);
    }
    for (int index = 0; index < expr->arg_count; ++index)
    {
        if (expr->args[index] != NULL)
        {
            rx_c_expr_reset(expr->args[index]);
            free(expr->args[index]);
        }
    }
    free(expr->args);
    rx_c_type_reset(&expr->cast_type);
    memset(expr, 0, sizeof(*expr));
}

bool rx_c_expr_set_raw(RxCProgram *program, RxCExpr *expr, const char *text)
{
    if (expr == NULL)
    {
        return false;
    }
    rx_c_expr_reset(expr);
    expr->kind = RX_C_EXPR_RAW;
    expr->text_id = intern_or_invalid(program, text);
    return expr->text_id != UINT32_MAX;
}

bool rx_c_expr_set_ident(RxCProgram *program, RxCExpr *expr, const char *name)
{
    if (expr == NULL)
    {
        return false;
    }
    rx_c_expr_reset(expr);
    expr->kind = RX_C_EXPR_IDENT;
    expr->text_id = intern_or_invalid(program, name);
    return expr->text_id != UINT32_MAX;
}

bool rx_c_expr_set_int(RxCExpr *expr, int64_t value)
{
    if (expr == NULL)
    {
        return false;
    }
    rx_c_expr_reset(expr);
    expr->kind = RX_C_EXPR_INT;
    expr->int_value = value;
    return true;
}

bool rx_c_expr_set_bool(RxCExpr *expr, bool value)
{
    if (expr == NULL)
    {
        return false;
    }
    rx_c_expr_reset(expr);
    expr->kind = RX_C_EXPR_BOOL;
    expr->bool_value = value;
    return true;
}

bool rx_c_expr_clone(RxCExpr *dst, const RxCExpr *src)
{
    if (dst == NULL || src == NULL)
    {
        return false;
    }
    rx_c_expr_reset(dst);
    *dst = *src;
    rx_c_type_init(&dst->cast_type);
    dst->left = NULL;
    dst->right = NULL;
    dst->args = NULL;
    dst->arg_count = 0;
    dst->arg_capacity = 0;
    if (!rx_c_type_clone(&dst->cast_type, &src->cast_type))
    {
        return false;
    }
    if (src->left != NULL)
    {
        dst->left = calloc(1, sizeof(*dst->left));
        if (dst->left == NULL)
        {
            return false;
        }
        rx_c_expr_init(dst->left);
        if (!rx_c_expr_clone(dst->left, src->left))
        {
            return false;
        }
    }
    if (src->right != NULL)
    {
        dst->right = calloc(1, sizeof(*dst->right));
        if (dst->right == NULL)
        {
            return false;
        }
        rx_c_expr_init(dst->right);
        if (!rx_c_expr_clone(dst->right, src->right))
        {
            return false;
        }
    }
    if (src->arg_count > 0)
    {
        dst->args = calloc((size_t)src->arg_count, sizeof(*dst->args));
        if (dst->args == NULL)
        {
            return false;
        }
        dst->arg_capacity = src->arg_count;
        dst->arg_count = src->arg_count;
        for (int index = 0; index < src->arg_count; ++index)
        {
            dst->args[index] = calloc(1, sizeof(*dst->args[index]));
            if (dst->args[index] == NULL)
            {
                return false;
            }
            rx_c_expr_init(dst->args[index]);
            if (!rx_c_expr_clone(dst->args[index], src->args[index]))
            {
                return false;
            }
        }
    }
    return true;
}

bool rx_c_expr_set_unary(RxCProgram *program, RxCExpr *expr, const char *op, const RxCExpr *child)
{
    if (expr == NULL || child == NULL)
    {
        return false;
    }
    rx_c_expr_reset(expr);
    expr->kind = RX_C_EXPR_UNARY;
    expr->op_id = intern_or_invalid(program, op);
    if (expr->op_id == UINT32_MAX)
    {
        return false;
    }
    expr->left = calloc(1, sizeof(*expr->left));
    if (expr->left == NULL)
    {
        return false;
    }
    rx_c_expr_init(expr->left);
    return rx_c_expr_clone(expr->left, child);
}

bool rx_c_expr_set_binary(RxCProgram *program, RxCExpr *expr, const char *op, const RxCExpr *left, const RxCExpr *right)
{
    if (expr == NULL || left == NULL || right == NULL)
    {
        return false;
    }
    rx_c_expr_reset(expr);
    expr->kind = RX_C_EXPR_BINARY;
    expr->op_id = intern_or_invalid(program, op);
    if (expr->op_id == UINT32_MAX)
    {
        return false;
    }
    expr->left = calloc(1, sizeof(*expr->left));
    expr->right = calloc(1, sizeof(*expr->right));
    if (expr->left == NULL || expr->right == NULL)
    {
        return false;
    }
    rx_c_expr_init(expr->left);
    rx_c_expr_init(expr->right);
    return rx_c_expr_clone(expr->left, left) && rx_c_expr_clone(expr->right, right);
}

bool rx_c_expr_set_cast(RxCProgram *program, RxCExpr *expr, const RxCType *type, const RxCExpr *child)
{
    (void)program;
    if (expr == NULL || type == NULL || child == NULL)
    {
        return false;
    }
    rx_c_expr_reset(expr);
    expr->kind = RX_C_EXPR_CAST;
    expr->left = calloc(1, sizeof(*expr->left));
    if (expr->left == NULL)
    {
        return false;
    }
    rx_c_expr_init(expr->left);
    return rx_c_type_clone(&expr->cast_type, type) && rx_c_expr_clone(expr->left, child);
}

bool rx_c_expr_set_call(RxCProgram *program, RxCExpr *expr, const char *callee, const RxCExpr *args, int arg_count)
{
    if (expr == NULL || callee == NULL || arg_count < 0)
    {
        return false;
    }
    rx_c_expr_reset(expr);
    expr->kind = RX_C_EXPR_CALL;
    expr->text_id = intern_or_invalid(program, callee);
    if (expr->text_id == UINT32_MAX)
    {
        return false;
    }
    if (arg_count > 0)
    {
        expr->args = calloc((size_t)arg_count, sizeof(*expr->args));
        if (expr->args == NULL)
        {
            return false;
        }
        expr->arg_capacity = arg_count;
        expr->arg_count = arg_count;
        for (int index = 0; index < arg_count; ++index)
        {
            expr->args[index] = calloc(1, sizeof(*expr->args[index]));
            if (expr->args[index] == NULL)
            {
                return false;
            }
            rx_c_expr_init(expr->args[index]);
            if (!rx_c_expr_clone(expr->args[index], &args[index]))
            {
                return false;
            }
        }
    }
    return true;
}

bool rx_c_expr_set_index(RxCExpr *expr, const RxCExpr *base, const RxCExpr *index)
{
    if (expr == NULL || base == NULL || index == NULL)
    {
        return false;
    }
    rx_c_expr_reset(expr);
    expr->kind = RX_C_EXPR_INDEX;
    expr->left = calloc(1, sizeof(*expr->left));
    expr->right = calloc(1, sizeof(*expr->right));
    if (expr->left == NULL || expr->right == NULL)
    {
        return false;
    }
    rx_c_expr_init(expr->left);
    rx_c_expr_init(expr->right);
    return rx_c_expr_clone(expr->left, base) && rx_c_expr_clone(expr->right, index);
}

void rx_c_block_init(RxCBlock *block)
{
    memset(block, 0, sizeof(*block));
}

static void rx_c_stmt_reset(RxCStmt *stmt)
{
    if (stmt == NULL)
    {
        return;
    }
    if (stmt->then_block != NULL)
    {
        rx_c_block_reset(stmt->then_block);
        free(stmt->then_block);
    }
    if (stmt->else_block != NULL)
    {
        rx_c_block_reset(stmt->else_block);
        free(stmt->else_block);
    }
    if (stmt->body_block != NULL)
    {
        rx_c_block_reset(stmt->body_block);
        free(stmt->body_block);
    }
    rx_c_type_reset(&stmt->type);
    rx_c_expr_reset(&stmt->expr);
    rx_c_expr_reset(&stmt->init_expr);
    rx_c_expr_reset(&stmt->condition);
    rx_c_expr_reset(&stmt->update);
    memset(stmt, 0, sizeof(*stmt));
}

void rx_c_block_reset(RxCBlock *block)
{
    if (block == NULL)
    {
        return;
    }
    for (int index = 0; index < block->count; ++index)
    {
        rx_c_stmt_reset(&block->items[index]);
    }
    free(block->items);
    memset(block, 0, sizeof(*block));
}

static void rx_c_function_reset(RxCFunction *function)
{
    if (function == NULL)
    {
        return;
    }
    for (int index = 0; index < function->param_count; ++index)
    {
        rx_c_type_reset(&function->params[index].type);
    }
    free(function->params);
    rx_c_type_reset(&function->return_type);
    rx_c_block_reset(&function->body);
    memset(function, 0, sizeof(*function));
}

void rx_c_program_reset(RxCProgram *program)
{
    if (program == NULL)
    {
        return;
    }
    for (int index = 0; index < program->count; ++index)
    {
        rx_c_function_reset(&program->items[index].function);
    }
    rx_c_string_pool_reset(&program->strings);
    free(program->items);
    memset(program, 0, sizeof(*program));
}

static bool rx_c_program_reserve(RxCProgram *program, int min_capacity)
{
    if (program->capacity >= min_capacity)
    {
        return true;
    }
    int next_capacity = program->capacity > 0 ? program->capacity * 2 : 16;
    while (next_capacity < min_capacity)
    {
        next_capacity *= 2;
    }
    RxCTopLevel *next_items = realloc(program->items, (size_t)next_capacity * sizeof(*next_items));
    if (next_items == NULL)
    {
        return false;
    }
    program->items = next_items;
    program->capacity = next_capacity;
    return true;
}

static bool rx_c_block_reserve(RxCBlock *block, int min_capacity)
{
    if (block->capacity >= min_capacity)
    {
        return true;
    }
    int next_capacity = block->capacity > 0 ? block->capacity * 2 : 16;
    while (next_capacity < min_capacity)
    {
        next_capacity *= 2;
    }
    RxCStmt *next_items = realloc(block->items, (size_t)next_capacity * sizeof(*next_items));
    if (next_items == NULL)
    {
        return false;
    }
    block->items = next_items;
    block->capacity = next_capacity;
    return true;
}

static bool rx_c_program_add_top_level(RxCProgram *program, RxCTopLevelKind kind, RxCStringId text_id, RxCStringId extra_text_id)
{
    if (program == NULL || !rx_c_program_reserve(program, program->count + 1))
    {
        return false;
    }
    RxCTopLevel *item = &program->items[program->count];
    memset(item, 0, sizeof(*item));
    item->kind = kind;
    item->text_id = text_id;
    item->extra_text_id = extra_text_id;
    program->count += 1;
    return true;
}

bool rx_c_program_add_line(RxCProgram *program, const char *text)
{
    return rx_c_program_add_raw(program, text);
}

bool rx_c_program_add_raw(RxCProgram *program, const char *text)
{
    RxCStringId id = intern_or_invalid(program, text);
    return id != UINT32_MAX && rx_c_program_add_top_level(program, RX_C_TOPLEVEL_RAW, id, UINT32_MAX);
}

bool rx_c_program_add_include_system(RxCProgram *program, const char *header)
{
    RxCStringId id = intern_or_invalid(program, header);
    return id != UINT32_MAX && rx_c_program_add_top_level(program, RX_C_TOPLEVEL_INCLUDE_SYSTEM, id, UINT32_MAX);
}

bool rx_c_program_add_include_local(RxCProgram *program, const char *header)
{
    RxCStringId id = intern_or_invalid(program, header);
    return id != UINT32_MAX && rx_c_program_add_top_level(program, RX_C_TOPLEVEL_INCLUDE_LOCAL, id, UINT32_MAX);
}

bool rx_c_program_add_define(RxCProgram *program, const char *name, const char *value)
{
    RxCStringId name_id = intern_or_invalid(program, name);
    RxCStringId value_id = intern_or_invalid(program, value);
    return name_id != UINT32_MAX
        && value_id != UINT32_MAX
        && rx_c_program_add_top_level(program, RX_C_TOPLEVEL_DEFINE, name_id, value_id);
}

static RxCStmt *rx_c_block_append_stmt(RxCBlock *block, RxCStmtKind kind)
{
    if (block == NULL || !rx_c_block_reserve(block, block->count + 1))
    {
        return NULL;
    }
    RxCStmt *stmt = &block->items[block->count];
    memset(stmt, 0, sizeof(*stmt));
    stmt->kind = kind;
    stmt->text_id = UINT32_MAX;
    stmt->name_id = UINT32_MAX;
    rx_c_type_init(&stmt->type);
    rx_c_expr_init(&stmt->expr);
    rx_c_expr_init(&stmt->init_expr);
    rx_c_expr_init(&stmt->condition);
    rx_c_expr_init(&stmt->update);
    block->count += 1;
    return stmt;
}

bool rx_c_block_add_raw(RxCProgram *program, RxCBlock *block, const char *text)
{
    RxCStmt *stmt = rx_c_block_append_stmt(block, RX_C_STMT_RAW);
    if (stmt == NULL)
    {
        return false;
    }
    stmt->text_id = intern_or_invalid(program, text);
    return stmt->text_id != UINT32_MAX;
}

bool rx_c_block_add_decl(RxCProgram *program, RxCBlock *block, const char *type_text, const char *name_text, const char *init_text)
{
    RxCType type;
    rx_c_type_init(&type);
    bool ok = rx_c_type_set_raw(program, &type, type_text)
        && rx_c_block_add_decl_expr(program, block, &type, name_text, NULL);
    if (ok && init_text != NULL)
    {
        RxCStmt *stmt = &block->items[block->count - 1];
        ok = rx_c_expr_set_raw(program, &stmt->init_expr, init_text);
    }
    rx_c_type_reset(&type);
    return ok;
}

bool rx_c_block_add_decl_expr(RxCProgram *program, RxCBlock *block, const RxCType *type, const char *name_text, const RxCExpr *init_expr)
{
    RxCStmt *stmt = rx_c_block_append_stmt(block, RX_C_STMT_DECL);
    if (stmt == NULL)
    {
        return false;
    }
    stmt->name_id = intern_or_invalid(program, name_text);
    return stmt->name_id != UINT32_MAX
        && rx_c_type_clone(&stmt->type, type)
        && (init_expr == NULL || rx_c_expr_clone(&stmt->init_expr, init_expr));
}

bool rx_c_block_add_expr(RxCProgram *program, RxCBlock *block, const char *expr_text)
{
    RxCExpr expr;
    rx_c_expr_init(&expr);
    bool ok = rx_c_expr_set_raw(program, &expr, expr_text) && rx_c_block_add_expr_stmt(program, block, &expr);
    rx_c_expr_reset(&expr);
    return ok;
}

bool rx_c_block_add_expr_stmt(RxCProgram *program, RxCBlock *block, const RxCExpr *expr)
{
    (void)program;
    RxCStmt *stmt = rx_c_block_append_stmt(block, RX_C_STMT_EXPR);
    if (stmt == NULL)
    {
        return false;
    }
    return rx_c_expr_clone(&stmt->expr, expr);
}

bool rx_c_block_add_assign_stmt(RxCProgram *program, RxCBlock *block, const RxCExpr *left, const RxCExpr *right)
{
    RxCExpr expr;
    rx_c_expr_init(&expr);
    bool ok = rx_c_expr_set_binary(program, &expr, "=", left, right)
        && rx_c_block_add_expr_stmt(program, block, &expr);
    rx_c_expr_reset(&expr);
    return ok;
}

bool rx_c_block_add_return(RxCProgram *program, RxCBlock *block, const char *expr_text)
{
    RxCExpr expr;
    rx_c_expr_init(&expr);
    bool ok = rx_c_expr_set_raw(program, &expr, expr_text) && rx_c_block_add_return_expr(program, block, &expr);
    rx_c_expr_reset(&expr);
    return ok;
}

bool rx_c_block_add_return_expr(RxCProgram *program, RxCBlock *block, const RxCExpr *expr)
{
    (void)program;
    RxCStmt *stmt = rx_c_block_append_stmt(block, RX_C_STMT_RETURN);
    if (stmt == NULL)
    {
        return false;
    }
    return rx_c_expr_clone(&stmt->expr, expr);
}

bool rx_c_block_add_break(RxCProgram *program, RxCBlock *block)
{
    (void)program;
    return rx_c_block_append_stmt(block, RX_C_STMT_BREAK) != NULL;
}

bool rx_c_block_add_continue(RxCProgram *program, RxCBlock *block)
{
    (void)program;
    return rx_c_block_append_stmt(block, RX_C_STMT_CONTINUE) != NULL;
}

RxCBlock *rx_c_stmt_set_if_else(RxCProgram *program, RxCBlock *block, const char *condition_text, bool create_else_block)
{
    RxCExpr condition;
    rx_c_expr_init(&condition);
    RxCBlock *result = NULL;
    if (rx_c_expr_set_raw(program, &condition, condition_text))
    {
        result = rx_c_stmt_set_if_expr(program, block, &condition);
        if (result != NULL && create_else_block)
        {
            RxCStmt *stmt = &block->items[block->count - 1];
            stmt->else_block = calloc(1, sizeof(*stmt->else_block));
            if (stmt->else_block == NULL)
            {
                result = NULL;
            }
            else
            {
                rx_c_block_init(stmt->else_block);
            }
        }
    }
    rx_c_expr_reset(&condition);
    return result;
}

RxCBlock *rx_c_stmt_set_if_expr(RxCProgram *program, RxCBlock *block, const RxCExpr *condition)
{
    (void)program;
    RxCStmt *stmt = rx_c_block_append_stmt(block, RX_C_STMT_IF);
    if (stmt == NULL)
    {
        return NULL;
    }
    if (!rx_c_expr_clone(&stmt->condition, condition))
    {
        return NULL;
    }
    stmt->then_block = calloc(1, sizeof(*stmt->then_block));
    if (stmt->then_block == NULL)
    {
        return NULL;
    }
    rx_c_block_init(stmt->then_block);
    return stmt->then_block;
}

RxCBlock *rx_c_stmt_set_if(RxCProgram *program, RxCBlock *block, const char *condition_text)
{
    return rx_c_stmt_set_if_else(program, block, condition_text, false);
}

RxCBlock *rx_c_stmt_set_for(RxCProgram *program, RxCBlock *block, const char *init_text, const char *condition_text, const char *update_text)
{
    RxCStmt *stmt = rx_c_block_append_stmt(block, RX_C_STMT_FOR);
    if (stmt == NULL)
    {
        return NULL;
    }
    stmt->init_expr.kind = RX_C_EXPR_RAW;
    stmt->condition.kind = RX_C_EXPR_RAW;
    stmt->update.kind = RX_C_EXPR_RAW;
    stmt->init_expr.text_id = init_text != NULL ? intern_or_invalid(program, init_text) : UINT32_MAX;
    stmt->condition.text_id = condition_text != NULL ? intern_or_invalid(program, condition_text) : UINT32_MAX;
    stmt->update.text_id = update_text != NULL ? intern_or_invalid(program, update_text) : UINT32_MAX;
    stmt->body_block = calloc(1, sizeof(*stmt->body_block));
    if (stmt->body_block == NULL)
    {
        return NULL;
    }
    rx_c_block_init(stmt->body_block);
    return stmt->body_block;
}

RxCBlock *rx_c_stmt_set_while_expr(RxCProgram *program, RxCBlock *block, const RxCExpr *condition)
{
    (void)program;
    RxCStmt *stmt = rx_c_block_append_stmt(block, RX_C_STMT_WHILE);
    if (stmt == NULL || !rx_c_expr_clone(&stmt->condition, condition))
    {
        return NULL;
    }
    stmt->body_block = calloc(1, sizeof(*stmt->body_block));
    if (stmt->body_block == NULL)
    {
        return NULL;
    }
    rx_c_block_init(stmt->body_block);
    return stmt->body_block;
}

RxCBlock *rx_c_block_add_nested_block(RxCProgram *program, RxCBlock *block)
{
    (void)program;
    RxCStmt *stmt = rx_c_block_append_stmt(block, RX_C_STMT_BLOCK);
    if (stmt == NULL)
    {
        return NULL;
    }
    stmt->body_block = calloc(1, sizeof(*stmt->body_block));
    if (stmt->body_block == NULL)
    {
        return NULL;
    }
    rx_c_block_init(stmt->body_block);
    return stmt->body_block;
}

static bool rx_c_stmt_clone_into(RxCStmt *dst, const RxCStmt *src)
{
    memset(dst, 0, sizeof(*dst));
    dst->kind = src->kind;
    dst->text_id = src->text_id;
    dst->name_id = src->name_id;
    rx_c_type_init(&dst->type);
    rx_c_expr_init(&dst->expr);
    rx_c_expr_init(&dst->init_expr);
    rx_c_expr_init(&dst->condition);
    rx_c_expr_init(&dst->update);
    dst->then_block = NULL;
    dst->else_block = NULL;
    dst->body_block = NULL;
    if (!rx_c_type_clone(&dst->type, &src->type)
        || !rx_c_expr_clone(&dst->expr, &src->expr)
        || !rx_c_expr_clone(&dst->init_expr, &src->init_expr)
        || !rx_c_expr_clone(&dst->condition, &src->condition)
        || !rx_c_expr_clone(&dst->update, &src->update))
    {
        return false;
    }
    if (src->then_block != NULL)
    {
        dst->then_block = calloc(1, sizeof(*dst->then_block));
        if (dst->then_block == NULL)
        {
            return false;
        }
        rx_c_block_init(dst->then_block);
        if (!rx_c_block_clone_into(dst->then_block, src->then_block))
        {
            return false;
        }
    }
    if (src->else_block != NULL)
    {
        dst->else_block = calloc(1, sizeof(*dst->else_block));
        if (dst->else_block == NULL)
        {
            return false;
        }
        rx_c_block_init(dst->else_block);
        if (!rx_c_block_clone_into(dst->else_block, src->else_block))
        {
            return false;
        }
    }
    if (src->body_block != NULL)
    {
        dst->body_block = calloc(1, sizeof(*dst->body_block));
        if (dst->body_block == NULL)
        {
            return false;
        }
        rx_c_block_init(dst->body_block);
        if (!rx_c_block_clone_into(dst->body_block, src->body_block))
        {
            return false;
        }
    }
    return true;
}

static bool rx_c_block_clone_into(RxCBlock *dst, const RxCBlock *src)
{
    if (src == NULL)
    {
        return true;
    }
    for (int index = 0; index < src->count; ++index)
    {
        if (!rx_c_block_reserve(dst, dst->count + 1))
        {
            return false;
        }
        if (!rx_c_stmt_clone_into(&dst->items[dst->count], &src->items[index]))
        {
            return false;
        }
        dst->count += 1;
    }
    return true;
}

bool rx_c_program_add_function(RxCProgram *program, const char *return_type, const char *name, const RxCParam *params, int param_count, const RxCBlock *body)
{
    if (program == NULL || return_type == NULL || name == NULL || body == NULL)
    {
        return false;
    }
    if (!rx_c_program_add_top_level(program, RX_C_TOPLEVEL_FUNCTION, UINT32_MAX, UINT32_MAX))
    {
        return false;
    }
    RxCTopLevel *item = &program->items[program->count - 1];
    rx_c_type_init(&item->function.return_type);
    if (!rx_c_type_set_raw(program, &item->function.return_type, return_type))
    {
        return false;
    }
    item->function.name_id = intern_or_invalid(program, name);
    if (item->function.name_id == UINT32_MAX)
    {
        return false;
    }
    if (param_count > 0)
    {
        item->function.params = calloc((size_t)param_count, sizeof(*item->function.params));
        if (item->function.params == NULL)
        {
            return false;
        }
        item->function.param_capacity = param_count;
        item->function.param_count = param_count;
        for (int index = 0; index < param_count; ++index)
        {
            const char *name_text = rx_c_string_pool_get(&program->strings, params[index].name_id);
            item->function.params[index].name_id = intern_or_invalid(program, name_text);
            rx_c_type_init(&item->function.params[index].type);
            if (!rx_c_type_clone(&item->function.params[index].type, &params[index].type)
                || item->function.params[index].name_id == UINT32_MAX)
            {
                return false;
            }
        }
    }
    rx_c_block_init(&item->function.body);
    return rx_c_block_clone_into(&item->function.body, body);
}
