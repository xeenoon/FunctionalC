#include "c_render.h"

#include <inttypes.h>

static bool emit_indent(RxStringBuilder *out, int indent)
{
    for (int index = 0; index < indent; ++index)
    {
        if (!rx_string_builder_append(out, "    "))
        {
            return false;
        }
    }
    return true;
}

static bool render_type(const RxCProgram *program, const RxCType *type, RxStringBuilder *out)
{
    if (type == NULL)
    {
        return rx_string_builder_append(out, "void");
    }
    switch (type->kind)
    {
        case RX_C_TYPE_RAW:
        case RX_C_TYPE_NAMED:
        {
            const char *text = rx_c_string_pool_get(&program->strings, type->text_id);
            return text != NULL && rx_string_builder_append(out, text);
        }
        case RX_C_TYPE_POINTER:
            return render_type(program, type->child, out) && rx_string_builder_append(out, "*");
        default:
            return false;
    }
}

static bool render_expr(const RxCProgram *program, const RxCExpr *expr, RxStringBuilder *out)
{
    if (expr == NULL)
    {
        return false;
    }
    switch (expr->kind)
    {
        case RX_C_EXPR_RAW:
        case RX_C_EXPR_IDENT:
        {
            const char *text = rx_c_string_pool_get(&program->strings, expr->text_id);
            return text != NULL && rx_string_builder_append(out, text);
        }
        case RX_C_EXPR_INT:
            return rx_string_builder_append_format(out, "%" PRId64, expr->int_value);
        case RX_C_EXPR_BOOL:
            return rx_string_builder_append(out, expr->bool_value ? "true" : "false");
        case RX_C_EXPR_UNARY:
        {
            const char *op = rx_c_string_pool_get(&program->strings, expr->op_id);
            return op != NULL
                && rx_string_builder_append(out, "(")
                && rx_string_builder_append(out, op)
                && render_expr(program, expr->left, out)
                && rx_string_builder_append(out, ")");
        }
        case RX_C_EXPR_BINARY:
        {
            const char *op = rx_c_string_pool_get(&program->strings, expr->op_id);
            return op != NULL
                && rx_string_builder_append(out, "(")
                && render_expr(program, expr->left, out)
                && rx_string_builder_append(out, " ")
                && rx_string_builder_append(out, op)
                && rx_string_builder_append(out, " ")
                && render_expr(program, expr->right, out)
                && rx_string_builder_append(out, ")");
        }
        case RX_C_EXPR_CAST:
            return rx_string_builder_append(out, "((")
                && render_type(program, &expr->cast_type, out)
                && rx_string_builder_append(out, ")")
                && render_expr(program, expr->left, out)
                && rx_string_builder_append(out, ")");
        case RX_C_EXPR_CALL:
        {
            const char *callee = rx_c_string_pool_get(&program->strings, expr->text_id);
            if (callee == NULL
                || !rx_string_builder_append(out, callee)
                || !rx_string_builder_append(out, "("))
            {
                return false;
            }
            for (int index = 0; index < expr->arg_count; ++index)
            {
                if ((index > 0 && !rx_string_builder_append(out, ", "))
                    || !render_expr(program, expr->args[index], out))
                {
                    return false;
                }
            }
            return rx_string_builder_append(out, ")");
        }
        case RX_C_EXPR_INDEX:
            return render_expr(program, expr->left, out)
                && rx_string_builder_append(out, "[")
                && render_expr(program, expr->right, out)
                && rx_string_builder_append(out, "]");
        default:
            return false;
    }
}

static bool render_block(const RxCProgram *program, const RxCBlock *block, RxStringBuilder *out, int indent);

static bool render_stmt(const RxCProgram *program, const RxCStmt *stmt, RxStringBuilder *out, int indent)
{
    const char *text = stmt->text_id != UINT32_MAX ? rx_c_string_pool_get(&program->strings, stmt->text_id) : NULL;
    switch (stmt->kind)
    {
        case RX_C_STMT_RAW:
            return emit_indent(out, indent) && rx_string_builder_append(out, text != NULL ? text : "") && rx_string_builder_append(out, "\n");
        case RX_C_STMT_DECL:
            return emit_indent(out, indent)
                && render_type(program, &stmt->type, out)
                && rx_string_builder_append(out, " ")
                && rx_string_builder_append(out, rx_c_string_pool_get(&program->strings, stmt->name_id))
                && (stmt->init_expr.text_id == UINT32_MAX && stmt->init_expr.left == NULL && stmt->init_expr.right == NULL && stmt->init_expr.arg_count == 0
                    ? true
                    : (rx_string_builder_append(out, " = ") && render_expr(program, &stmt->init_expr, out)))
                && rx_string_builder_append(out, ";\n");
        case RX_C_STMT_EXPR:
            return emit_indent(out, indent) && render_expr(program, &stmt->expr, out) && rx_string_builder_append(out, ";\n");
        case RX_C_STMT_ASSIGN:
            return emit_indent(out, indent)
                && render_expr(program, &stmt->expr, out)
                && rx_string_builder_append(out, " = ")
                && render_expr(program, &stmt->init_expr, out)
                && rx_string_builder_append(out, ";\n");
        case RX_C_STMT_RETURN:
            return emit_indent(out, indent) && rx_string_builder_append(out, "return ") && render_expr(program, &stmt->expr, out) && rx_string_builder_append(out, ";\n");
        case RX_C_STMT_IF:
            if (!emit_indent(out, indent)
                || !rx_string_builder_append(out, "if (")
                || !render_expr(program, &stmt->condition, out)
                || !rx_string_builder_append(out, ") {\n")
                || !render_block(program, stmt->then_block, out, indent + 1)
                || !emit_indent(out, indent)
                || !rx_string_builder_append(out, "}"))
            {
                return false;
            }
            if (stmt->else_block != NULL)
            {
                return rx_string_builder_append(out, " else {\n")
                    && render_block(program, stmt->else_block, out, indent + 1)
                    && emit_indent(out, indent)
                    && rx_string_builder_append(out, "}\n");
            }
            return rx_string_builder_append(out, "\n");
        case RX_C_STMT_FOR:
            return emit_indent(out, indent)
                && rx_string_builder_append(out, "for (")
                && (stmt->init_expr.text_id == UINT32_MAX ? true : render_expr(program, &stmt->init_expr, out))
                && rx_string_builder_append(out, "; ")
                && (stmt->condition.text_id == UINT32_MAX && stmt->condition.left == NULL && stmt->condition.right == NULL && stmt->condition.arg_count == 0 ? true : render_expr(program, &stmt->condition, out))
                && rx_string_builder_append(out, "; ")
                && (stmt->update.text_id == UINT32_MAX ? true : render_expr(program, &stmt->update, out))
                && rx_string_builder_append(out, ") {\n")
                && render_block(program, stmt->body_block, out, indent + 1)
                && emit_indent(out, indent)
                && rx_string_builder_append(out, "}\n");
        case RX_C_STMT_WHILE:
            return emit_indent(out, indent)
                && rx_string_builder_append(out, "while (")
                && render_expr(program, &stmt->condition, out)
                && rx_string_builder_append(out, ") {\n")
                && render_block(program, stmt->body_block, out, indent + 1)
                && emit_indent(out, indent)
                && rx_string_builder_append(out, "}\n");
        case RX_C_STMT_BREAK:
            return emit_indent(out, indent) && rx_string_builder_append(out, "break;\n");
        case RX_C_STMT_CONTINUE:
            return emit_indent(out, indent) && rx_string_builder_append(out, "continue;\n");
        case RX_C_STMT_BLOCK:
            return emit_indent(out, indent)
                && rx_string_builder_append(out, "{\n")
                && render_block(program, stmt->body_block, out, indent + 1)
                && emit_indent(out, indent)
                && rx_string_builder_append(out, "}\n");
        default:
            return false;
    }
}

static bool render_block(const RxCProgram *program, const RxCBlock *block, RxStringBuilder *out, int indent)
{
    if (block == NULL)
    {
        return true;
    }
    for (int index = 0; index < block->count; ++index)
    {
        if (!render_stmt(program, &block->items[index], out, indent))
        {
            return false;
        }
    }
    return true;
}

static bool render_function(const RxCProgram *program, const RxCFunction *function, RxStringBuilder *out)
{
    const char *name = rx_c_string_pool_get(&program->strings, function->name_id);
    if (name == NULL
        || !render_type(program, &function->return_type, out)
        || !rx_string_builder_append(out, " ")
        || !rx_string_builder_append(out, name)
        || !rx_string_builder_append(out, "("))
    {
        return false;
    }
    for (int index = 0; index < function->param_count; ++index)
    {
        const char *param_name = rx_c_string_pool_get(&program->strings, function->params[index].name_id);
        if (param_name == NULL
            || (index > 0 && !rx_string_builder_append(out, ", "))
            || !render_type(program, &function->params[index].type, out)
            || !rx_string_builder_append(out, " ")
            || !rx_string_builder_append(out, param_name))
        {
            return false;
        }
    }
    return rx_string_builder_append(out, ") {\n")
        && render_block(program, &function->body, out, 1)
        && rx_string_builder_append(out, "}\n");
}

bool rx_c_render_program(const RxCProgram *program, RxStringBuilder *out)
{
    if (program == NULL || out == NULL)
    {
        return false;
    }
    for (int index = 0; index < program->count; ++index)
    {
        const RxCTopLevel *item = &program->items[index];
        const char *text = item->text_id != UINT32_MAX ? rx_c_string_pool_get(&program->strings, item->text_id) : NULL;
        const char *extra = item->extra_text_id != UINT32_MAX ? rx_c_string_pool_get(&program->strings, item->extra_text_id) : NULL;
        switch (item->kind)
        {
            case RX_C_TOPLEVEL_RAW:
                if (text == NULL || !rx_string_builder_append(out, text) || !rx_string_builder_append(out, "\n"))
                {
                    return false;
                }
                break;
            case RX_C_TOPLEVEL_INCLUDE_SYSTEM:
                if (text == NULL || !rx_string_builder_append(out, "#include <") || !rx_string_builder_append(out, text) || !rx_string_builder_append(out, ">\n"))
                {
                    return false;
                }
                break;
            case RX_C_TOPLEVEL_INCLUDE_LOCAL:
                if (text == NULL || !rx_string_builder_append(out, "#include \"") || !rx_string_builder_append(out, text) || !rx_string_builder_append(out, "\"\n"))
                {
                    return false;
                }
                break;
            case RX_C_TOPLEVEL_DEFINE:
                if (text == NULL || extra == NULL || !rx_string_builder_append(out, "#define ") || !rx_string_builder_append(out, text) || !rx_string_builder_append(out, " ") || !rx_string_builder_append(out, extra) || !rx_string_builder_append(out, "\n"))
                {
                    return false;
                }
                break;
            case RX_C_TOPLEVEL_FUNCTION:
                if (!render_function(program, &item->function, out))
                {
                    return false;
                }
                break;
            default:
                return false;
        }
        if (!rx_string_builder_append(out, "\n"))
        {
            return false;
        }
    }
    return true;
}
