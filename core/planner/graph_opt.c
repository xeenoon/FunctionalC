#include "graph_opt.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const char *name;
    char *parameter_names[4];
    int parameter_count;
    char *body;
} RxGraphFunction;

typedef enum
{
    RX_EXPR_CONST,
    RX_EXPR_VAR,
    RX_EXPR_ADD,
    RX_EXPR_SUB,
    RX_EXPR_MUL,
    RX_EXPR_DIV,
    RX_EXPR_LT,
    RX_EXPR_EQ
} RxExprKind;

typedef struct RxExpr
{
    RxExprKind kind;
    intptr_t constant;
    char *name;
    struct RxExpr *left;
    struct RxExpr *right;
} RxExpr;

typedef struct
{
    const char *cursor;
    const char *params[4];
    RxExpr *args[4];
    int param_count;
    char *aliases[8];
    const char *alias_targets[8];
    int alias_count;
} RxExprParser;

static bool is_ident_start(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
}

static bool is_ident_char(char ch)
{
    return is_ident_start(ch) || (ch >= '0' && ch <= '9');
}

static char *copy_trimmed_range(const char *start, const char *end)
{
    while (start < end && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n'))
    {
        ++start;
    }
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
    {
        --end;
    }

    size_t length = (size_t)(end - start);
    char *result = malloc(length + 1);
    if (result == NULL)
    {
        return NULL;
    }
    memcpy(result, start, length);
    result[length] = '\0';
    return result;
}

static void free_graph_function(RxGraphFunction *function)
{
    for (int index = 0; index < function->parameter_count; ++index)
    {
        free(function->parameter_names[index]);
    }
    free(function->body);
    memset(function, 0, sizeof(*function));
}

static const char *find_function_name(const char *source, const char *name)
{
    size_t name_length = strlen(name);
    for (const char *cursor = source; (cursor = strstr(cursor, name)) != NULL; ++cursor)
    {
        if ((cursor == source || !is_ident_char(cursor[-1]))
            && cursor[name_length] == '(')
        {
            return cursor;
        }
    }
    return NULL;
}

static bool parse_parameters(
    const char *params_start,
    const char *params_end,
    RxGraphFunction *function)
{
    const char *cursor = params_start;
    while (cursor < params_end)
    {
        while (cursor < params_end && (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n' || *cursor == ','))
        {
            ++cursor;
        }
        if (cursor >= params_end)
        {
            break;
        }

        const char *decl_end = cursor;
        while (decl_end < params_end && *decl_end != ',')
        {
            ++decl_end;
        }

        const char *name_end = decl_end;
        while (name_end > cursor && !is_ident_char(name_end[-1]))
        {
            --name_end;
        }
        const char *name_start = name_end;
        while (name_start > cursor && is_ident_char(name_start[-1]))
        {
            --name_start;
        }

        if (function->parameter_count >= 4)
        {
            return false;
        }

        function->parameter_names[function->parameter_count] = copy_trimmed_range(name_start, name_end);
        if (function->parameter_names[function->parameter_count] == NULL)
        {
            return false;
        }
        function->parameter_count += 1;
        cursor = decl_end + 1;
    }

    return true;
}

static bool try_extract_graph_function(
    const char *source_text,
    const char *name,
    RxGraphFunction *function)
{
    memset(function, 0, sizeof(*function));
    function->name = name;
    if (source_text == NULL || name == NULL)
    {
        return false;
    }

    const char *name_pos = find_function_name(source_text, name);
    if (name_pos == NULL)
    {
        return false;
    }

    const char *params_start = strchr(name_pos, '(');
    if (params_start == NULL)
    {
        return false;
    }
    ++params_start;

    int depth = 1;
    const char *params_end = params_start;
    while (*params_end != '\0' && depth > 0)
    {
        if (*params_end == '(')
        {
            depth += 1;
        }
        else if (*params_end == ')')
        {
            depth -= 1;
        }
        ++params_end;
    }
    if (depth != 0)
    {
        return false;
    }
    --params_end;

    if (!parse_parameters(params_start, params_end, function))
    {
        free_graph_function(function);
        return false;
    }

    const char *body_start = strchr(params_end, '{');
    if (body_start == NULL)
    {
        free_graph_function(function);
        return false;
    }
    ++body_start;

    depth = 1;
    const char *body_end = body_start;
    while (*body_end != '\0' && depth > 0)
    {
        if (*body_end == '{')
        {
            depth += 1;
        }
        else if (*body_end == '}')
        {
            depth -= 1;
        }
        ++body_end;
    }
    if (depth != 0)
    {
        free_graph_function(function);
        return false;
    }
    --body_end;

    function->body = copy_trimmed_range(body_start, body_end);
    if (function->body == NULL)
    {
        free_graph_function(function);
        return false;
    }
    return true;
}

static void rx_free_expr(RxExpr *expr)
{
    if (expr == NULL)
    {
        return;
    }
    rx_free_expr(expr->left);
    rx_free_expr(expr->right);
    free(expr->name);
    free(expr);
}

static RxExpr *rx_new_expr(RxExprKind kind, RxExpr *left, RxExpr *right)
{
    RxExpr *expr = calloc(1, sizeof(*expr));
    if (expr == NULL)
    {
        return NULL;
    }
    expr->kind = kind;
    expr->left = left;
    expr->right = right;
    return expr;
}

static RxExpr *rx_new_const(intptr_t value)
{
    RxExpr *expr = rx_new_expr(RX_EXPR_CONST, NULL, NULL);
    if (expr != NULL)
    {
        expr->constant = value;
    }
    return expr;
}

static RxExpr *rx_new_var(const char *name)
{
    RxExpr *expr = rx_new_expr(RX_EXPR_VAR, NULL, NULL);
    if (expr == NULL)
    {
        return NULL;
    }
    expr->name = copy_trimmed_range(name, name + strlen(name));
    if (expr->name == NULL)
    {
        free(expr);
        return NULL;
    }
    return expr;
}

static RxExpr *rx_clone_expr(const RxExpr *expr)
{
    if (expr == NULL)
    {
        return NULL;
    }
    RxExpr *copy = rx_new_expr(expr->kind, NULL, NULL);
    if (copy == NULL)
    {
        return NULL;
    }
    copy->constant = expr->constant;
    if (expr->name != NULL)
    {
        copy->name = copy_trimmed_range(expr->name, expr->name + strlen(expr->name));
        if (copy->name == NULL)
        {
            rx_free_expr(copy);
            return NULL;
        }
    }
    copy->left = rx_clone_expr(expr->left);
    copy->right = rx_clone_expr(expr->right);
    return copy;
}

static void rx_skip_space(RxExprParser *parser)
{
    while (*parser->cursor == ' ' || *parser->cursor == '\t' || *parser->cursor == '\r' || *parser->cursor == '\n')
    {
        ++parser->cursor;
    }
}

static bool rx_consume(RxExprParser *parser, const char *token)
{
    rx_skip_space(parser);
    size_t length = strlen(token);
    if (strncmp(parser->cursor, token, length) == 0)
    {
        parser->cursor += length;
        return true;
    }
    return false;
}

static char *rx_parse_ident(RxExprParser *parser)
{
    rx_skip_space(parser);
    if (!is_ident_start(*parser->cursor))
    {
        return NULL;
    }
    const char *start = parser->cursor++;
    while (is_ident_char(*parser->cursor))
    {
        ++parser->cursor;
    }
    return copy_trimmed_range(start, parser->cursor);
}

static RxExpr *rx_parse_expr_cmp(RxExprParser *parser);

static RxExpr *rx_parse_primary(RxExprParser *parser)
{
    rx_skip_space(parser);
    if (rx_consume(parser, "("))
    {
        RxExpr *inner = rx_parse_expr_cmp(parser);
        if (inner == NULL || !rx_consume(parser, ")"))
        {
            rx_free_expr(inner);
            return NULL;
        }
        return inner;
    }

    if ((*parser->cursor >= '0' && *parser->cursor <= '9')
        || ((*parser->cursor == '-' || *parser->cursor == '+') && parser->cursor[1] >= '0' && parser->cursor[1] <= '9'))
    {
        char *end = NULL;
        long long value = strtoll(parser->cursor, &end, 10);
        parser->cursor = end;
        return rx_new_const((intptr_t)value);
    }

    char *ident = rx_parse_ident(parser);
    if (ident == NULL)
    {
        return NULL;
    }

    for (int index = 0; index < parser->alias_count; ++index)
    {
        if (strcmp(ident, parser->aliases[index]) == 0)
        {
            free(ident);
            ident = copy_trimmed_range(parser->alias_targets[index], parser->alias_targets[index] + strlen(parser->alias_targets[index]));
            break;
        }
    }

    for (int index = 0; index < parser->param_count; ++index)
    {
        if (strcmp(ident, parser->params[index]) == 0)
        {
            free(ident);
            return rx_clone_expr(parser->args[index]);
        }
    }

    RxExpr *expr = rx_new_var(ident);
    free(ident);
    return expr;
}

static RxExpr *rx_parse_mul(RxExprParser *parser)
{
    RxExpr *expr = rx_parse_primary(parser);
    if (expr == NULL)
    {
        return NULL;
    }
    for (;;)
    {
        if (rx_consume(parser, "*"))
        {
            RxExpr *rhs = rx_parse_primary(parser);
            expr = rx_new_expr(RX_EXPR_MUL, expr, rhs);
        }
        else if (rx_consume(parser, "/"))
        {
            RxExpr *rhs = rx_parse_primary(parser);
            expr = rx_new_expr(RX_EXPR_DIV, expr, rhs);
        }
        else
        {
            break;
        }
        if (expr == NULL || expr->right == NULL)
        {
            rx_free_expr(expr);
            return NULL;
        }
    }
    return expr;
}

static RxExpr *rx_parse_add(RxExprParser *parser)
{
    RxExpr *expr = rx_parse_mul(parser);
    if (expr == NULL)
    {
        return NULL;
    }
    for (;;)
    {
        if (rx_consume(parser, "+"))
        {
            RxExpr *rhs = rx_parse_mul(parser);
            expr = rx_new_expr(RX_EXPR_ADD, expr, rhs);
        }
        else if (rx_consume(parser, "-"))
        {
            RxExpr *rhs = rx_parse_mul(parser);
            expr = rx_new_expr(RX_EXPR_SUB, expr, rhs);
        }
        else
        {
            break;
        }
        if (expr == NULL || expr->right == NULL)
        {
            rx_free_expr(expr);
            return NULL;
        }
    }
    return expr;
}

static RxExpr *rx_parse_expr_cmp(RxExprParser *parser)
{
    RxExpr *expr = rx_parse_add(parser);
    if (expr == NULL)
    {
        return NULL;
    }
    if (rx_consume(parser, "=="))
    {
        RxExpr *rhs = rx_parse_add(parser);
        expr = rx_new_expr(RX_EXPR_EQ, expr, rhs);
    }
    else if (rx_consume(parser, "<"))
    {
        RxExpr *rhs = rx_parse_add(parser);
        expr = rx_new_expr(RX_EXPR_LT, expr, rhs);
    }
    if (expr == NULL || ((expr->kind == RX_EXPR_EQ || expr->kind == RX_EXPR_LT) && expr->right == NULL))
    {
        rx_free_expr(expr);
        return NULL;
    }
    return expr;
}

static RxExpr *rx_simplify_expr(RxExpr *expr)
{
    if (expr == NULL)
    {
        return NULL;
    }
    expr->left = rx_simplify_expr(expr->left);
    expr->right = rx_simplify_expr(expr->right);

    if ((expr->kind == RX_EXPR_ADD || expr->kind == RX_EXPR_SUB || expr->kind == RX_EXPR_MUL || expr->kind == RX_EXPR_DIV || expr->kind == RX_EXPR_LT || expr->kind == RX_EXPR_EQ)
        && expr->left != NULL && expr->right != NULL
        && expr->left->kind == RX_EXPR_CONST && expr->right->kind == RX_EXPR_CONST)
    {
        intptr_t left = expr->left->constant;
        intptr_t right = expr->right->constant;
        intptr_t value = 0;
        switch (expr->kind)
        {
            case RX_EXPR_ADD: value = left + right; break;
            case RX_EXPR_SUB: value = left - right; break;
            case RX_EXPR_MUL: value = left * right; break;
            case RX_EXPR_DIV: value = right != 0 ? left / right : 0; break;
            case RX_EXPR_LT: value = left < right; break;
            case RX_EXPR_EQ: value = left == right; break;
            default: break;
        }
        rx_free_expr(expr->left);
        rx_free_expr(expr->right);
        expr->left = NULL;
        expr->right = NULL;
        expr->kind = RX_EXPR_CONST;
        expr->constant = value;
        return expr;
    }

    if (expr->kind == RX_EXPR_ADD || expr->kind == RX_EXPR_SUB)
    {
        if (expr->right != NULL && expr->right->kind == RX_EXPR_CONST && expr->right->constant == 0)
        {
            RxExpr *left = expr->left;
            expr->left = NULL;
            rx_free_expr(expr->right);
            free(expr);
            return left;
        }
        if (expr->left != NULL
            && (expr->left->kind == RX_EXPR_ADD || expr->left->kind == RX_EXPR_SUB)
            && expr->left->right != NULL
            && expr->left->right->kind == RX_EXPR_CONST
            && expr->right != NULL
            && expr->right->kind == RX_EXPR_CONST)
        {
            intptr_t offset = expr->left->right->constant;
            offset += expr->kind == RX_EXPR_ADD ? expr->right->constant : -expr->right->constant;
            RxExpr *base = expr->left->left;
            expr->left->left = NULL;
            expr->left->right = NULL;
            rx_free_expr(expr->left);
            rx_free_expr(expr->right);
            if (offset == 0)
            {
                free(expr);
                return base;
            }
            expr->kind = offset >= 0 ? RX_EXPR_ADD : RX_EXPR_SUB;
            expr->left = base;
            expr->right = rx_new_const(offset >= 0 ? offset : -offset);
        }
    }

    if (expr->kind == RX_EXPR_MUL && expr->right != NULL && expr->right->kind == RX_EXPR_CONST && expr->right->constant == 1)
    {
        RxExpr *left = expr->left;
        expr->left = NULL;
        rx_free_expr(expr->right);
        free(expr);
        return left;
    }
    if (expr->kind == RX_EXPR_DIV && expr->right != NULL && expr->right->kind == RX_EXPR_CONST && expr->right->constant == 1)
    {
        RxExpr *left = expr->left;
        expr->left = NULL;
        rx_free_expr(expr->right);
        free(expr);
        return left;
    }
    return expr;
}

static bool rx_emit_expr(RxStringBuilder *out, const RxExpr *expr)
{
    if (expr == NULL)
    {
        return false;
    }
    switch (expr->kind)
    {
        case RX_EXPR_CONST:
            return rx_string_builder_append_format(out, "%" PRIdPTR, expr->constant);
        case RX_EXPR_VAR:
            return rx_string_builder_append(out, expr->name);
        default:
        {
            const char *op = expr->kind == RX_EXPR_ADD ? "+" :
                expr->kind == RX_EXPR_SUB ? "-" :
                expr->kind == RX_EXPR_MUL ? "*" :
                expr->kind == RX_EXPR_DIV ? "/" :
                expr->kind == RX_EXPR_LT ? "<" : "==";
            return rx_string_builder_append(out, "(")
                && rx_emit_expr(out, expr->left)
                && rx_string_builder_append_format(out, " %s ", op)
                && rx_emit_expr(out, expr->right)
                && rx_string_builder_append(out, ")");
        }
    }
}

static bool rx_expr_is_named_var(const RxExpr *expr, const char *name)
{
    return expr != NULL
        && expr->kind == RX_EXPR_VAR
        && expr->name != NULL
        && strcmp(expr->name, name) == 0;
}

static bool rx_materialize_current_expr(RxStringBuilder *out, RxExpr **current, bool *has_value_decl)
{
    if (current == NULL || *current == NULL || rx_expr_is_named_var(*current, "value"))
    {
        return true;
    }

    bool ok = false;
    if (*has_value_decl)
    {
        ok = rx_string_builder_append(out, "        value = ")
            && rx_emit_expr(out, *current)
            && rx_string_builder_append(out, ";\n");
    }
    else
    {
        ok = rx_string_builder_append(out, "        intptr_t value = ")
            && rx_emit_expr(out, *current)
            && rx_string_builder_append(out, ";\n");
        *has_value_decl = ok;
    }
    if (!ok)
    {
        return false;
    }

    rx_free_expr(*current);
    *current = rx_new_var("value");
    return *current != NULL;
}

static char *strip_return_wrapper(const char *expr)
{
    const char *prefix = "(void *)(intptr_t)";
    if (strncmp(expr, prefix, strlen(prefix)) == 0)
    {
        const char *cursor = expr + strlen(prefix);
        while (*cursor == ' ' || *cursor == '\t')
        {
            ++cursor;
        }
        if (*cursor == '(' && expr[strlen(expr) - 1] == ')')
        {
            return copy_trimmed_range(cursor + 1, expr + strlen(expr) - 1);
        }
        return copy_trimmed_range(cursor, expr + strlen(expr));
    }
    return copy_trimmed_range(expr, expr + strlen(expr));
}

static bool rx_extract_return_expression(const RxGraphFunction *function, char **expr_out, RxExprParser *parser)
{
    const char *cursor = function->body;
    parser->alias_count = 0;
    while (*cursor != '\0')
    {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
        {
            ++cursor;
        }
        const char *statement_start = cursor;
        while (*cursor != '\0' && *cursor != ';')
        {
            ++cursor;
        }
        const char *statement_end = cursor;
        if (*cursor == ';')
        {
            ++cursor;
        }
        char *statement = copy_trimmed_range(statement_start, statement_end);
        if (statement == NULL || statement[0] == '\0')
        {
            free(statement);
            continue;
        }
        if (strncmp(statement, "return ", 7) == 0)
        {
            char *stripped = strip_return_wrapper(statement + 7);
            free(statement);
            *expr_out = stripped;
            return stripped != NULL;
        }
        if (strncmp(statement, "intptr_t ", 9) == 0)
        {
            char *equals = strchr(statement, '=');
            if (equals != NULL)
            {
                char *name = copy_trimmed_range(statement + 9, equals);
                char *rhs = copy_trimmed_range(equals + 1, statement + strlen(statement));
                if (name == NULL || rhs == NULL)
                {
                    free(name);
                    free(rhs);
                    free(statement);
                    return false;
                }
                char *cast = strstr(rhs, "(intptr_t)");
                if (cast != NULL)
                {
                    char *target = copy_trimmed_range(cast + strlen("(intptr_t)"), rhs + strlen(rhs));
                    if (target == NULL)
                    {
                        free(name);
                        free(rhs);
                        free(statement);
                        return false;
                    }
                    parser->aliases[parser->alias_count] = name;
                    parser->alias_targets[parser->alias_count] = target;
                    parser->alias_count += 1;
                }
                free(rhs);
            }
        }
        else
        {
            free(statement);
            return false;
        }
        free(statement);
    }
    return false;
}

static bool try_parse_function_expression(
    const char *helper_source,
    const char *name,
    const char *const *param_names,
    RxExpr *const *args,
    int arg_count,
    RxExpr **expr_out)
{
    RxGraphFunction function;
    if (!try_extract_graph_function(helper_source, name, &function))
    {
        return false;
    }

    char *expr_text = NULL;
    RxExprParser parser;
    memset(&parser, 0, sizeof(parser));
    for (int index = 0; index < arg_count; ++index)
    {
        parser.params[index] = param_names[index];
        parser.args[index] = args[index];
    }
    parser.param_count = arg_count;
    if (!rx_extract_return_expression(&function, &expr_text, &parser))
    {
        free_graph_function(&function);
        return false;
    }
    parser.cursor = expr_text;
    *expr_out = rx_parse_expr_cmp(&parser);
    rx_skip_space(&parser);
    bool ok = *expr_out != NULL && *parser.cursor == '\0';
    if (ok)
    {
        *expr_out = rx_simplify_expr(*expr_out);
    }
    else
    {
        rx_free_expr(*expr_out);
        *expr_out = NULL;
    }
    for (int index = 0; index < parser.alias_count; ++index)
    {
        free(parser.aliases[index]);
        free((void *)parser.alias_targets[index]);
    }
    free(expr_text);
    free_graph_function(&function);
    return ok;
}

bool rx_try_emit_graph_optimized_loop_body(
    const RxLoweredPipeline *pipeline,
    const RxCCodegenOptions *options,
    RxStringBuilder *out)
{
    RxExpr *current = pipeline->source_kind == RX_LOOP_SOURCE_RANGE ? rx_new_var("src") : NULL;
    bool has_value_decl = false;
    bool ok = true;
    if (pipeline->source_kind == RX_LOOP_SOURCE_ZIP_MERGE_MAP_RANGE)
    {
        if (!rx_string_builder_append_format(out, "    for (intptr_t right = 1; right <= (intptr_t)%d; ++right) {\n", pipeline->source_inner_n)
            || !rx_string_builder_append(out, "        for (intptr_t src = 1; src <= N; ++src) {\n"))
        {
            return false;
        }
    }
    else if (!rx_string_builder_append(out, "    for (intptr_t src = 1; src <= N; ++src) {\n"))
    {
        return false;
    }
    if (pipeline->source_kind == RX_LOOP_SOURCE_ZIP_RANGE)
    {
        ok = rx_string_builder_append(out, "        intptr_t left = src;\n")
            && rx_string_builder_append(out, "        intptr_t right = src;\n");
    }
    else if (pipeline->source_kind == RX_LOOP_SOURCE_ZIP_MERGE_MAP_RANGE)
    {
        ok = rx_string_builder_append(out, "            intptr_t zipped_left = src;\n")
            && rx_string_builder_append(out, "            intptr_t zipped_right = src;\n");
    }
    if (!ok)
    {
        rx_free_expr(current);
        return false;
    }

    for (int index = 0; index < pipeline->op_count; ++index)
    {
        const RxLoopOp *op = &pipeline->ops[index];
        const char *fn = op->stage != NULL && op->stage->primary_argument.kind == RX_BINDING_FUNCTION_NAME
            ? op->stage->primary_argument.as.function_name
            : NULL;
        RxExpr *expr = NULL;
        switch (op->kind)
        {
            case RX_OP_CALL_PAIR_MAP:
            {
                const char *params[] = { "left_raw", "right_raw" };
                RxExpr *args[] = { rx_new_var("left"), rx_new_var("right") };
                ok = try_parse_function_expression(options->helper_source_text, fn, params, args, 2, &expr);
                rx_free_expr(args[0]);
                rx_free_expr(args[1]);
                if (!ok) { break; }
                rx_free_expr(current);
                current = expr;
                has_value_decl = false;
                break;
            }
            case RX_OP_CALL_TRIPLE_MAP:
            {
                const char *params[] = { "zipped_left_raw", "zipped_right_raw", "right_raw" };
                RxExpr *args[] = { rx_new_var("zipped_left"), rx_new_var("zipped_right"), rx_new_var("right") };
                ok = try_parse_function_expression(options->helper_source_text, fn, params, args, 3, &expr);
                rx_free_expr(args[0]);
                rx_free_expr(args[1]);
                rx_free_expr(args[2]);
                if (!ok) { break; }
                rx_free_expr(current);
                current = expr;
                has_value_decl = false;
                break;
            }
            case RX_OP_CALL_MAP:
            {
                const char *params[] = { "raw" };
                RxExpr *args[] = { current };
                ok = try_parse_function_expression(options->helper_source_text, fn, params, args, 1, &expr);
                if (!ok) { break; }
                rx_free_expr(current);
                current = expr;
                has_value_decl = false;
                break;
            }
            case RX_OP_CALL_SCAN:
            {
                const char *params[] = { "raw_accum", "raw_next" };
                RxExpr *args[] = { rx_new_var(pipeline->state_slots[op->state_slot_index].name), current };
                ok = try_parse_function_expression(options->helper_source_text, fn, params, args, 2, &expr);
                rx_free_expr(args[0]);
                if (!ok) { break; }
                ok = rx_string_builder_append_format(out, "        %s = ", pipeline->state_slots[op->state_slot_index].name)
                    && rx_emit_expr(out, expr)
                    && rx_string_builder_append(out, ";\n");
                rx_free_expr(current);
                current = rx_new_var(pipeline->state_slots[op->state_slot_index].name);
                has_value_decl = false;
                rx_free_expr(expr);
                expr = NULL;
                break;
            }
            case RX_OP_APPLY_DISTINCT_UNTIL_CHANGED:
            {
                ok = rx_materialize_current_expr(out, &current, &has_value_decl);
                if (!ok) { break; }
                const char *params[] = { "raw" };
                RxExpr *args[] = { current };
                ok = try_parse_function_expression(options->helper_source_text, fn, params, args, 1, &expr);
                if (!ok) { break; }
                ok = rx_string_builder_append(out, "        intptr_t key = ")
                    && rx_emit_expr(out, expr)
                    && rx_string_builder_append(out, ";\n")
                    && rx_string_builder_append_format(out, "        if (%s && key == %s) { continue; }\n", pipeline->state_slots[op->aux_state_slot_index].name, pipeline->state_slots[op->state_slot_index].name)
                    && rx_string_builder_append_format(out, "        %s = key;\n", pipeline->state_slots[op->state_slot_index].name)
                    && rx_string_builder_append_format(out, "        %s = true;\n", pipeline->state_slots[op->aux_state_slot_index].name);
                rx_free_expr(expr);
                break;
            }
            case RX_OP_APPLY_SKIP_WHILE:
            {
                ok = rx_materialize_current_expr(out, &current, &has_value_decl);
                if (!ok) { break; }
                const char *params[] = { "raw" };
                RxExpr *args[] = { current };
                ok = try_parse_function_expression(options->helper_source_text, fn, params, args, 1, &expr);
                if (!ok) { break; }
                ok = rx_string_builder_append_format(out, "        if (!%s && ", pipeline->state_slots[op->state_slot_index].name)
                    && rx_emit_expr(out, expr)
                    && rx_string_builder_append(out, ") { continue; }\n")
                    && rx_string_builder_append_format(out, "        %s = true;\n", pipeline->state_slots[op->state_slot_index].name);
                rx_free_expr(expr);
                break;
            }
            case RX_OP_APPLY_LAST:
                ok = rx_materialize_current_expr(out, &current, &has_value_decl);
                if (!ok) { break; }
                ok = rx_string_builder_append_format(out, "        %s = ", pipeline->state_slots[op->state_slot_index].name)
                    && rx_emit_expr(out, current)
                    && rx_string_builder_append(out, ";\n")
                    && rx_string_builder_append_format(out, "        %s = true;\n", pipeline->state_slots[op->aux_state_slot_index].name);
                break;
            default:
                ok = false;
                break;
        }
        if (!ok)
        {
            rx_free_expr(expr);
            rx_free_expr(current);
            return false;
        }
    }

    rx_free_expr(current);
    if (pipeline->source_kind == RX_LOOP_SOURCE_ZIP_MERGE_MAP_RANGE)
    {
        return rx_string_builder_append(out, "        }\n    }\n");
    }
    return rx_string_builder_append(out, "    }\n");
}
