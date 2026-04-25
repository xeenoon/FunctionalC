#include "astgen.h"
#include "c_render.h"
#include "graph_opt.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const char *name;
    char *return_type;
    char *parameter_decls[4];
    char *parameter_names[4];
    int parameter_count;
    char *body;
} RxInlineFunction;

typedef struct
{
    const char *items[256];
    int count;
} RxSymbolList;

typedef struct
{
    char *items[128];
    int count;
} RxOwnedNameList;

static bool try_extract_inline_function(
    const char *source_text,
    const char *name,
    RxInlineFunction *function);
static void free_inline_function(RxInlineFunction *function);
static char *copy_trimmed_range(const char *start, const char *end);

static bool is_ident_start(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
}

static bool is_ident_char(char ch)
{
    return is_ident_start(ch) || (ch >= '0' && ch <= '9');
}

static bool is_keyword_name(const char *name)
{
    return strcmp(name, "if") == 0
        || strcmp(name, "for") == 0
        || strcmp(name, "while") == 0
        || strcmp(name, "switch") == 0
        || strcmp(name, "return") == 0
        || strcmp(name, "sizeof") == 0
        || strcmp(name, "memset") == 0
        || strcmp(name, "memcpy") == 0
        || strcmp(name, "malloc") == 0
        || strcmp(name, "calloc") == 0
        || strcmp(name, "free") == 0
        || strcmp(name, "snprintf") == 0
        || strcmp(name, "strcmp") == 0
        || strcmp(name, "strtoll") == 0
        || strcmp(name, "atoi") == 0
        || strcmp(name, "clock_gettime") == 0;
}

static bool symbol_list_append_unique(RxSymbolList *list, const char *name)
{
    if (list == NULL || name == NULL || *name == '\0')
    {
        return true;
    }
    for (int index = 0; index < list->count; ++index)
    {
        if (strcmp(list->items[index], name) == 0)
        {
            return true;
        }
    }
    if (list->count >= (int)(sizeof(list->items) / sizeof(list->items[0])))
    {
        return false;
    }
    char *owned_name = copy_trimmed_range(name, name + strlen(name));
    if (owned_name == NULL)
    {
        return false;
    }
    list->items[list->count++] = owned_name;
    return true;
}

static void owned_name_list_free(RxOwnedNameList *list)
{
    if (list == NULL)
    {
        return;
    }
    for (int index = 0; index < list->count; ++index)
    {
        free(list->items[index]);
    }
    memset(list, 0, sizeof(*list));
}

static bool owned_name_list_contains(const RxOwnedNameList *list, const char *name)
{
    if (list == NULL || name == NULL)
    {
        return false;
    }
    for (int index = 0; index < list->count; ++index)
    {
        if (strcmp(list->items[index], name) == 0)
        {
            return true;
        }
    }
    return false;
}

static bool owned_name_list_append_unique(RxOwnedNameList *list, const char *name)
{
    if (list == NULL || name == NULL || *name == '\0')
    {
        return true;
    }
    if (owned_name_list_contains(list, name))
    {
        return true;
    }
    if (list->count >= (int)(sizeof(list->items) / sizeof(list->items[0])))
    {
        return false;
    }
    char *copy = copy_trimmed_range(name, name + strlen(name));
    if (copy == NULL)
    {
        return false;
    }
    list->items[list->count++] = copy;
    return true;
}

static bool collect_storage_symbols(const RxPlannerIrPipeline *pipeline, RxSymbolList *symbols)
{
    memset(symbols, 0, sizeof(*symbols));
    if (pipeline == NULL)
    {
        return true;
    }

    for (int index = 0; index < pipeline->state_slot_count; ++index)
    {
        const RxPlannerIrStateSlot *slot = &pipeline->state_slots[index];
        if (slot->value_type == RX_ARG_VOID_PTR
            && slot->initial_value.kind == RX_LITERAL_SYMBOL
            && !symbol_list_append_unique(symbols, slot->initial_value.as.symbol_name))
        {
            return false;
        }
    }

    for (int index = 0; index < pipeline->op_count; ++index)
    {
        const RxPlannerIrOp *op = &pipeline->ops[index];
        const RxPlannedStage *stage = op->stage;
        if (stage == NULL)
        {
            continue;
        }
        if ((op->kind == RX_OP_CALL_MAP_INTO || op->kind == RX_OP_CALL_SCAN_MUT || op->kind == RX_OP_CALL_REDUCE_MUT)
            && stage->secondary_argument.kind == RX_BINDING_LITERAL
            && stage->secondary_argument.as.literal.kind == RX_LITERAL_SYMBOL
            && !symbol_list_append_unique(symbols, stage->secondary_argument.as.literal.as.symbol_name))
        {
            return false;
        }
    }
    return true;
}

static bool collect_inline_helper_closure(const char *source_text, const char *name, RxSymbolList *symbols);

static bool collect_called_helpers_from_body(const char *source_text, const char *body, RxSymbolList *symbols)
{
    for (const char *cursor = body; *cursor != '\0'; ++cursor)
    {
        if (cursor[0] == '/' && cursor[1] == '/')
        {
            cursor += 2;
            while (*cursor != '\0' && *cursor != '\n')
            {
                ++cursor;
            }
            if (*cursor == '\0')
            {
                break;
            }
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '*')
        {
            cursor += 2;
            while (cursor[0] != '\0' && !(cursor[0] == '*' && cursor[1] == '/'))
            {
                ++cursor;
            }
            if (cursor[0] == '\0')
            {
                break;
            }
            ++cursor;
            continue;
        }
        if (*cursor == '"' || *cursor == '\'')
        {
            char quote = *cursor;
            ++cursor;
            while (*cursor != '\0')
            {
                if (*cursor == '\\' && cursor[1] != '\0')
                {
                    cursor += 2;
                    continue;
                }
                if (*cursor == quote)
                {
                    break;
                }
                ++cursor;
            }
            if (*cursor == '\0')
            {
                break;
            }
            continue;
        }
        if (!is_ident_start(*cursor) || (cursor > body && is_ident_char(cursor[-1])))
        {
            continue;
        }
        const char *name_start = cursor;
        while (is_ident_char(*cursor))
        {
            ++cursor;
        }
        const char *name_end = cursor;
        const char *after = cursor;
        while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n')
        {
            ++after;
        }
        if (*after != '(')
        {
            continue;
        }
        char candidate[128];
        size_t length = (size_t)(name_end - name_start);
        if (length == 0 || length + 1 > sizeof(candidate))
        {
            continue;
        }
        memcpy(candidate, name_start, length);
        candidate[length] = '\0';
        if (is_keyword_name(candidate))
        {
            continue;
        }
        if (!collect_inline_helper_closure(source_text, candidate, symbols))
        {
            return false;
        }
    }
    return true;
}

static bool collect_inline_helper_closure(const char *source_text, const char *name, RxSymbolList *symbols)
{
    RxInlineFunction function;
    if (!try_extract_inline_function(source_text, name, &function))
    {
        return true;
    }
    bool added = false;
    for (int index = 0; index < symbols->count; ++index)
    {
        if (strcmp(symbols->items[index], name) == 0)
        {
            free_inline_function(&function);
            return true;
        }
    }
    if (!symbol_list_append_unique(symbols, name))
    {
        free_inline_function(&function);
        return false;
    }
    added = true;
    if (added && !collect_called_helpers_from_body(source_text, function.body, symbols))
    {
        free_inline_function(&function);
        return false;
    }
    free_inline_function(&function);
    return true;
}

static bool op_uses_shared_value_view(RxLoopOpKind kind)
{
    switch (kind)
    {
        case RX_OP_CALL_FILTER:
        case RX_OP_APPLY_TAKE_WHILE:
        case RX_OP_APPLY_SKIP_WHILE:
        case RX_OP_APPLY_DISTINCT_UNTIL_CHANGED:
        case RX_OP_CALL_REDUCE_MUT:
            return true;
        default:
            return false;
    }
}

static bool op_preserves_current_value(RxLoopOpKind kind)
{
    switch (kind)
    {
        case RX_OP_CALL_FILTER:
        case RX_OP_APPLY_TAKE_WHILE:
        case RX_OP_APPLY_SKIP_WHILE:
        case RX_OP_APPLY_DISTINCT_UNTIL_CHANGED:
            return true;
        default:
            return false;
    }
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

static const char *skip_storage_qualifiers(const char *type_text)
{
    const char *cursor = type_text;
    for (;;)
    {
        while (*cursor == ' ' || *cursor == '\t')
        {
            ++cursor;
        }
        if (strncmp(cursor, "static ", 7) == 0)
        {
            cursor += 7;
            continue;
        }
        if (strncmp(cursor, "extern ", 7) == 0)
        {
            cursor += 7;
            continue;
        }
        if (strncmp(cursor, "inline ", 7) == 0)
        {
            cursor += 7;
            continue;
        }
        if (strncmp(cursor, "__inline ", 9) == 0)
        {
            cursor += 9;
            continue;
        }
        break;
    }
    return cursor;
}

static void free_inline_function(RxInlineFunction *function)
{
    free(function->return_type);
    for (int index = 0; index < function->parameter_count; ++index)
    {
        free(function->parameter_decls[index]);
        free(function->parameter_names[index]);
    }
    free(function->body);
    memset(function, 0, sizeof(*function));
}

static bool token_matches_at(const char *cursor, const char *token)
{
    size_t length = strlen(token);
    if (strncmp(cursor, token, length) != 0)
    {
        return false;
    }
    if (is_ident_char(cursor[-1]))
    {
        return false;
    }
    return !is_ident_char(cursor[length]);
}

static bool contains_banned_syntax(const char *body)
{
    for (const char *cursor = body; *cursor != '\0'; ++cursor)
    {
        if (*cursor == '#')
        {
            return true;
        }
        if ((cursor == body || !is_ident_char(cursor[-1]))
            && (token_matches_at(cursor, "goto")
                || token_matches_at(cursor, "switch")
                || token_matches_at(cursor, "case")
                || token_matches_at(cursor, "default")))
        {
            return true;
        }
    }
    return false;
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
    RxInlineFunction *function)
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

        const char *decl_start = cursor;
        const char *decl_end = cursor;
        while (decl_end < params_end && *decl_end != ',')
        {
            ++decl_end;
        }

        const char *name_end = decl_end;
        while (name_end > decl_start && !is_ident_char(name_end[-1]))
        {
            --name_end;
        }
        const char *name_start = name_end;
        while (name_start > decl_start && is_ident_char(name_start[-1]))
        {
            --name_start;
        }

        if (function->parameter_count >= 4)
        {
            return false;
        }

        function->parameter_decls[function->parameter_count] = copy_trimmed_range(decl_start, decl_end);
        function->parameter_names[function->parameter_count] = copy_trimmed_range(name_start, name_end);
        if (function->parameter_decls[function->parameter_count] == NULL
            || function->parameter_names[function->parameter_count] == NULL)
        {
            return false;
        }
        function->parameter_count += 1;
        cursor = decl_end + 1;
    }

    return true;
}

static bool try_extract_inline_function(
    const char *source_text,
    const char *name,
    RxInlineFunction *function)
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

    const char *return_start = name_pos;
    while (return_start > source_text && return_start[-1] != '\n' && return_start[-1] != ';' && return_start[-1] != '}')
    {
        --return_start;
    }
    function->return_type = copy_trimmed_range(return_start, name_pos);
    if (function->return_type == NULL)
    {
        return false;
    }

    const char *params_start = strchr(name_pos, '(');
    if (params_start == NULL)
    {
        free_inline_function(function);
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
        free_inline_function(function);
        return false;
    }
    --params_end;

    if (!parse_parameters(params_start, params_end, function))
    {
        free_inline_function(function);
        return false;
    }

    const char *body_start = params_end + 1;
    while (*body_start == ' ' || *body_start == '\t' || *body_start == '\r' || *body_start == '\n')
    {
        ++body_start;
    }
    if (*body_start != '{')
    {
        free_inline_function(function);
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
        free_inline_function(function);
        return false;
    }
    --body_end;

    function->body = copy_trimmed_range(body_start, body_end);
    if (function->body == NULL || contains_banned_syntax(function->body))
    {
        free_inline_function(function);
        return false;
    }

    return true;
}

static bool emit_rewritten_body(
    RxStringBuilder *out,
    const char *body,
    const char *result_name,
    const char *done_label)
{
    const char *cursor = body;
    while (*cursor != '\0')
    {
        if ((cursor == body || !is_ident_char(cursor[-1])) && token_matches_at(cursor, "return"))
        {
            cursor += strlen("return");
            while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
            {
                ++cursor;
            }
            const char *expr_start = cursor;
            int paren_depth = 0;
            int brace_depth = 0;
            while (*cursor != '\0')
            {
                if (*cursor == '(')
                {
                    paren_depth += 1;
                }
                else if (*cursor == ')')
                {
                    paren_depth -= 1;
                }
                else if (*cursor == '{')
                {
                    brace_depth += 1;
                }
                else if (*cursor == '}')
                {
                    brace_depth -= 1;
                }
                else if (*cursor == ';' && paren_depth == 0 && brace_depth == 0)
                {
                    break;
                }
                ++cursor;
            }
            if (*cursor != ';')
            {
                return false;
            }

            char *expr = copy_trimmed_range(expr_start, cursor);
            if (expr == NULL)
            {
                return false;
            }
            bool ok = rx_string_builder_append_format(out, "%s = %s; goto %s;", result_name, expr, done_label);
            free(expr);
            if (!ok)
            {
                return false;
            }
            ++cursor;
            continue;
        }

        char chunk[2] = { *cursor, '\0' };
        if (!rx_string_builder_append(out, chunk))
        {
            return false;
        }
        ++cursor;
    }
    return true;
}

static bool try_parse_single_call_body(
    const char *body,
    char *callee_name,
    size_t callee_name_size,
    char **arguments,
    int *argument_count)
{
    const char *cursor = body;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
    {
        ++cursor;
    }
    if (!is_ident_start(*cursor))
    {
        return false;
    }
    const char *name_start = cursor;
    while (is_ident_char(*cursor))
    {
        ++cursor;
    }
    size_t name_length = (size_t)(cursor - name_start);
    if (name_length == 0 || name_length + 1 > callee_name_size)
    {
        return false;
    }
    memcpy(callee_name, name_start, name_length);
    callee_name[name_length] = '\0';
    while (*cursor == ' ' || *cursor == '\t')
    {
        ++cursor;
    }
    if (*cursor != '(')
    {
        return false;
    }
    ++cursor;

    *argument_count = 0;
    const char *arg_start = cursor;
    int paren_depth = 0;
    for (;;)
    {
        char ch = *cursor;
        if (ch == '\0')
        {
            return false;
        }
        if (ch == '(')
        {
            paren_depth += 1;
        }
        else if (ch == ')')
        {
            if (paren_depth == 0)
            {
                if (cursor > arg_start)
                {
                    if (*argument_count >= 4)
                    {
                        return false;
                    }
                    arguments[*argument_count] = copy_trimmed_range(arg_start, cursor);
                    if (arguments[*argument_count] == NULL)
                    {
                        return false;
                    }
                    *argument_count += 1;
                }
                ++cursor;
                break;
            }
            paren_depth -= 1;
        }
        else if (ch == ',' && paren_depth == 0)
        {
            if (*argument_count >= 4)
            {
                return false;
            }
            arguments[*argument_count] = copy_trimmed_range(arg_start, cursor);
            if (arguments[*argument_count] == NULL)
            {
                return false;
            }
            *argument_count += 1;
            ++cursor;
            while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
            {
                ++cursor;
            }
            arg_start = cursor;
            continue;
        }
        ++cursor;
    }
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
    {
        ++cursor;
    }
    if (*cursor != ';')
    {
        return false;
    }
    ++cursor;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
    {
        ++cursor;
    }
    return *cursor == '\0';
}

static void skip_c_space_and_comments(const char **cursor)
{
    for (;;)
    {
        while (**cursor == ' ' || **cursor == '\t' || **cursor == '\r' || **cursor == '\n')
        {
            ++(*cursor);
        }
        if ((*cursor)[0] == '/' && (*cursor)[1] == '/')
        {
            *cursor += 2;
            while (**cursor != '\0' && **cursor != '\n')
            {
                ++(*cursor);
            }
            continue;
        }
        if ((*cursor)[0] == '/' && (*cursor)[1] == '*')
        {
            *cursor += 2;
            while ((*cursor)[0] != '\0' && !((*cursor)[0] == '*' && (*cursor)[1] == '/'))
            {
                ++(*cursor);
            }
            if ((*cursor)[0] == '*' && (*cursor)[1] == '/')
            {
                *cursor += 2;
            }
            continue;
        }
        break;
    }
}

static char *extract_next_top_level_statement(const char **cursor_ptr)
{
    const char *cursor = *cursor_ptr;
    skip_c_space_and_comments(&cursor);
    if (*cursor == '\0')
    {
        *cursor_ptr = cursor;
        return NULL;
    }

    const char *start = cursor;
    int paren_depth = 0;
    int brace_depth = 0;
    bool in_string = false;
    char string_quote = '\0';
    while (*cursor != '\0')
    {
        char ch = *cursor;
        if (in_string)
        {
            if (ch == '\\' && cursor[1] != '\0')
            {
                cursor += 2;
                continue;
            }
            if (ch == string_quote)
            {
                in_string = false;
            }
            ++cursor;
            continue;
        }
        if (ch == '"' || ch == '\'')
        {
            in_string = true;
            string_quote = ch;
            ++cursor;
            continue;
        }
        if (ch == '/' && cursor[1] == '/')
        {
            cursor += 2;
            while (*cursor != '\0' && *cursor != '\n')
            {
                ++cursor;
            }
            continue;
        }
        if (ch == '/' && cursor[1] == '*')
        {
            cursor += 2;
            while (cursor[0] != '\0' && !(cursor[0] == '*' && cursor[1] == '/'))
            {
                ++cursor;
            }
            if (cursor[0] == '*' && cursor[1] == '/')
            {
                cursor += 2;
            }
            continue;
        }
        if (ch == '(')
        {
            paren_depth += 1;
        }
        else if (ch == ')')
        {
            paren_depth -= 1;
        }
        else if (ch == '{')
        {
            brace_depth += 1;
        }
        else if (ch == '}')
        {
            if (brace_depth == 0)
            {
                ++cursor;
                break;
            }
            brace_depth -= 1;
            if (brace_depth == 0)
            {
                ++cursor;
                break;
            }
        }
        else if (ch == ';' && paren_depth == 0 && brace_depth == 0)
        {
            ++cursor;
            break;
        }
        ++cursor;
    }
    *cursor_ptr = cursor;
    return copy_trimmed_range(start, cursor);
}

static bool statement_contains_output_write(const char *stmt, const char *out_name)
{
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "%s->", out_name);
    const char *cursor = stmt;
    bool saw_call_write = false;
    while ((cursor = strstr(cursor, pattern)) != NULL)
    {
        const char *after = cursor + strlen(pattern);
        while (is_ident_char(*after))
        {
            ++after;
        }
        while (*after == ' ' || *after == '\t')
        {
            ++after;
        }
        if (*after == '[')
        {
            while (*after != '\0' && *after != ']')
            {
                ++after;
            }
            if (*after == ']')
            {
                ++after;
            }
            while (*after == ' ' || *after == '\t')
            {
                ++after;
            }
        }
        if ((*after == '=' && after[1] != '=') || (*after == ',' && !saw_call_write && strchr(stmt, '(') != NULL))
        {
            return true;
        }
        if (*after == ',')
        {
            saw_call_write = true;
        }
        cursor = after;
    }
    return false;
}

static bool collect_written_output_fields(const char *stmt, const char *out_name, RxOwnedNameList *fields)
{
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "%s->", out_name);
    const char *cursor = stmt;
    bool saw_call_write = false;
    while ((cursor = strstr(cursor, pattern)) != NULL)
    {
        const char *field_start = cursor + strlen(pattern);
        const char *field_end = field_start;
        while (is_ident_char(*field_end))
        {
            ++field_end;
        }
        if (field_end == field_start)
        {
            cursor = field_end;
            continue;
        }
        const char *after = field_end;
        while (*after == ' ' || *after == '\t')
        {
            ++after;
        }
        if (*after == '[')
        {
            while (*after != '\0' && *after != ']')
            {
                ++after;
            }
            if (*after == ']')
            {
                ++after;
            }
            while (*after == ' ' || *after == '\t')
            {
                ++after;
            }
        }
        if ((*after == '=' && after[1] != '=') || (*after == ',' && !saw_call_write && strchr(stmt, '(') != NULL))
        {
            char *field = copy_trimmed_range(field_start, field_end);
            if (field == NULL)
            {
                return false;
            }
            bool ok = owned_name_list_append_unique(fields, field);
            free(field);
            if (!ok)
            {
                return false;
            }
        }
        if (*after == ',')
        {
            saw_call_write = true;
        }
        cursor = field_end;
    }
    return true;
}

static bool append_output_fields_from_text(const char *text, const RxOwnedNameList *aliases, RxOwnedNameList *fields)
{
    if (text == NULL)
    {
        return true;
    }
    for (int alias_index = 0; alias_index < aliases->count; ++alias_index)
    {
        char pattern[128];
        snprintf(pattern, sizeof(pattern), "%s->", aliases->items[alias_index]);
        const char *cursor = text;
        while ((cursor = strstr(cursor, pattern)) != NULL)
        {
            const char *field_start = cursor + strlen(pattern);
            const char *field_end = field_start;
            while (is_ident_char(*field_end))
            {
                ++field_end;
            }
            if (field_end > field_start)
            {
                char *field = copy_trimmed_range(field_start, field_end);
                if (field == NULL)
                {
                    return false;
                }
                bool ok = owned_name_list_append_unique(fields, field);
                free(field);
                if (!ok)
                {
                    return false;
                }
            }
            cursor = field_end;
        }
    }
    return true;
}

static bool collect_param_aliases_and_fields(
    const char *body,
    const char *param_name,
    RxOwnedNameList *aliases,
    RxOwnedNameList *fields)
{
    const char *cursor = body;
    if (!owned_name_list_append_unique(aliases, param_name))
    {
        return false;
    }
    while (true)
    {
        char *stmt = extract_next_top_level_statement(&cursor);
        if (stmt == NULL)
        {
            break;
        }
        if (strstr(stmt, "=") != NULL)
        {
            for (int alias_index = 0; alias_index < aliases->count; ++alias_index)
            {
                const char *alias = aliases->items[alias_index];
                const char *pos = strstr(stmt, alias);
                if (pos != NULL && pos > stmt)
                {
                    const char *eq = strchr(stmt, '=');
                    if (eq != NULL && pos > eq)
                    {
                        const char *name_end = eq;
                        while (name_end > stmt && !is_ident_char(name_end[-1]))
                        {
                            --name_end;
                        }
                        const char *name_start = name_end;
                        while (name_start > stmt && is_ident_char(name_start[-1]))
                        {
                            --name_start;
                        }
                        if (name_end > name_start)
                        {
                            char *new_alias = copy_trimmed_range(name_start, name_end);
                            if (new_alias == NULL)
                            {
                                free(stmt);
                                return false;
                            }
                            bool ok = owned_name_list_append_unique(aliases, new_alias);
                            free(new_alias);
                            if (!ok)
                            {
                                free(stmt);
                                return false;
                            }
                        }
                    }
                }
            }
        }
        if (!append_output_fields_from_text(stmt, aliases, fields))
        {
            free(stmt);
            return false;
        }
        free(stmt);
    }
    return true;
}

static bool collect_downstream_output_fields(
    const RxPlannerIrPipeline *pipeline,
    int start_index,
    const RxCCodegenOptions *options,
    RxOwnedNameList *fields)
{
    for (int index = start_index; index < pipeline->op_count; ++index)
    {
        const RxPlannerIrOp *op = &pipeline->ops[index];
        const RxPlannedStage *stage = op->stage;
        if (stage == NULL || stage->primary_argument.kind != RX_BINDING_FUNCTION_NAME)
        {
            if (!op_preserves_current_value(op->kind))
            {
                break;
            }
            continue;
        }
        const char *fn = stage->primary_argument.as.function_name;
        RxInlineFunction function;
        if (!try_extract_inline_function(options != NULL ? options->helper_source_text : NULL, fn, &function))
        {
            if (!op_preserves_current_value(op->kind))
            {
                break;
            }
            continue;
        }
        const char *target_param =
            (op->kind == RX_OP_CALL_REDUCE_MUT || op->kind == RX_OP_CALL_SCAN || op->kind == RX_OP_CALL_SCAN_MUT || op->kind == RX_OP_CALL_REDUCE)
                ? (function.parameter_count >= 2 ? function.parameter_names[1] : NULL)
                : (function.parameter_count >= 1 ? function.parameter_names[0] : NULL);
        RxOwnedNameList aliases = { 0 };
        bool ok = target_param != NULL && collect_param_aliases_and_fields(function.body, target_param, &aliases, fields);
        owned_name_list_free(&aliases);
        free_inline_function(&function);
        if (!ok)
        {
            return false;
        }
        if (!op_preserves_current_value(op->kind))
        {
            break;
        }
    }
    return true;
}

static bool emit_reduced_output_helper_body(
    RxStringBuilder *out,
    const char *body,
    const char *out_param_name,
    const RxOwnedNameList *live_fields)
{
    const char *cursor = body;
    char *statements[256] = { 0 };
    int statement_count = 0;
    while (true)
    {
        char *stmt = extract_next_top_level_statement(&cursor);
        if (stmt == NULL)
        {
            break;
        }
        if (statement_count >= (int)(sizeof(statements) / sizeof(statements[0])))
        {
            free(stmt);
            return false;
        }
        statements[statement_count++] = stmt;
    }

    bool keep_flags[256] = { false };
    RxOwnedNameList live = { 0 };
    for (int index = 0; index < live_fields->count; ++index)
    {
        if (!owned_name_list_append_unique(&live, live_fields->items[index]))
        {
            owned_name_list_free(&live);
            for (int free_index = 0; free_index < statement_count; ++free_index) free(statements[free_index]);
            return false;
        }
    }

    for (int stmt_index = statement_count - 1; stmt_index >= 0; --stmt_index)
    {
        char *stmt = statements[stmt_index];
        bool keep = true;
        if (statement_contains_output_write(stmt, out_param_name))
        {
            RxOwnedNameList written = { 0 };
            RxOwnedNameList aliases = { 0 };
            RxOwnedNameList read_fields = { 0 };
            keep = false;
            if (!collect_written_output_fields(stmt, out_param_name, &written)
                || !owned_name_list_append_unique(&aliases, out_param_name)
                || !append_output_fields_from_text(stmt, &aliases, &read_fields))
            {
                owned_name_list_free(&written);
                owned_name_list_free(&aliases);
                owned_name_list_free(&read_fields);
                owned_name_list_free(&live);
                for (int free_index = 0; free_index < statement_count; ++free_index) free(statements[free_index]);
                return false;
            }
            for (int index = 0; index < written.count; ++index)
            {
                if (owned_name_list_contains(&live, written.items[index]))
                {
                    keep = true;
                    break;
                }
            }
            if (keep)
            {
                for (int index = 0; index < read_fields.count; ++index)
                {
                    if (!owned_name_list_append_unique(&live, read_fields.items[index]))
                    {
                        owned_name_list_free(&written);
                        owned_name_list_free(&aliases);
                        owned_name_list_free(&read_fields);
                        owned_name_list_free(&live);
                        for (int free_index = 0; free_index < statement_count; ++free_index) free(statements[free_index]);
                        return false;
                    }
                }
            }
            owned_name_list_free(&written);
            owned_name_list_free(&aliases);
            owned_name_list_free(&read_fields);
        }
        keep_flags[stmt_index] = keep;
    }

    bool kept_any = false;
    for (int stmt_index = 0; stmt_index < statement_count; ++stmt_index)
    {
        if (keep_flags[stmt_index])
        {
            if (!rx_string_builder_append(out, statements[stmt_index])
                || !rx_string_builder_append(out, "\n"))
            {
                owned_name_list_free(&live);
                for (int free_index = 0; free_index < statement_count; ++free_index) free(statements[free_index]);
                return false;
            }
            kept_any = true;
        }
    }
    owned_name_list_free(&live);
    for (int free_index = 0; free_index < statement_count; ++free_index) free(statements[free_index]);
    return kept_any;
}

static bool emit_inline_call_block(
    RxStringBuilder *out,
    const char *source_text,
    const RxInlineFunction *function,
    const char *const *arguments,
    int argument_count,
    const char *result_name,
    const char *done_label,
    int inline_depth)
{
    if (!rx_string_builder_append(out, "        {\n"))
    {
        return false;
    }
    for (int index = 0; index < function->parameter_count && index < argument_count; ++index)
    {
        if (!rx_string_builder_append_format(out, "            %s = %s;\n", function->parameter_decls[index], arguments[index]))
        {
            return false;
        }
    }
    if (inline_depth < 2)
    {
        char callee_name[128];
        char *nested_args[4] = { 0 };
        int nested_arg_count = 0;
        if (try_parse_single_call_body(function->body, callee_name, sizeof(callee_name), nested_args, &nested_arg_count))
        {
            RxInlineFunction nested;
            if (try_extract_inline_function(source_text, callee_name, &nested))
            {
                char nested_done_label[48];
                snprintf(nested_done_label, sizeof(nested_done_label), "%s_nested_%d", done_label, inline_depth + 1);
                bool ok = emit_inline_call_block(
                    out,
                    source_text,
                    &nested,
                    (const char *const *)nested_args,
                    nested_arg_count,
                    result_name,
                    nested_done_label,
                    inline_depth + 1);
                free_inline_function(&nested);
                for (int index = 0; index < nested_arg_count; ++index)
                {
                    free(nested_args[index]);
                }
                if (!ok)
                {
                    return false;
                }
                if (!rx_string_builder_append_format(out, "\n%s:;\n        }\n", done_label))
                {
                    return false;
                }
                return true;
            }
        }
        for (int index = 0; index < nested_arg_count; ++index)
        {
            free(nested_args[index]);
        }
    }
    if (!emit_rewritten_body(out, function->body, result_name, done_label))
    {
        return false;
    }
    if (!rx_string_builder_append_format(out, "\n%s:;\n        }\n", done_label))
    {
        return false;
    }
    return true;
}

static bool emit_function_declarations(const RxPlannerIrPipeline *pipeline, const RxCCodegenOptions *options, RxStringBuilder *out)
{
    for (int index = 0; index < pipeline->op_count; ++index)
    {
        const RxPlannerIrOp *op = &pipeline->ops[index];
        if (op->stage == NULL || op->stage->primary_argument.kind != RX_BINDING_FUNCTION_NAME)
        {
            continue;
        }

        const char *name = op->stage->primary_argument.as.function_name;
        RxInlineFunction function;
        bool inlined = try_extract_inline_function(options != NULL ? options->helper_source_text : NULL, name, &function);
        if (inlined)
        {
            free_inline_function(&function);
            continue;
        }

        switch (op->kind)
        {
            case RX_OP_CALL_PAIR_MAP:
                if (!rx_string_builder_append_c_extern_pair_map(out, name)) return false;
                break;
            case RX_OP_CALL_TRIPLE_MAP:
                if (!rx_string_builder_append_c_extern_triple_map(out, name)) return false;
                break;
            case RX_OP_CALL_MAP:
            case RX_OP_CALL_MAP_INTO:
            case RX_OP_CALL_MAP_CHAIN:
            case RX_OP_CALL_MAP_TO:
            case RX_OP_APPLY_DISTINCT:
            case RX_OP_APPLY_DISTINCT_UNTIL_CHANGED:
                if (op->kind == RX_OP_CALL_MAP_INTO)
                {
                    if (!rx_string_builder_append_c_extern_map_into(out, name)) return false;
                }
                else
                {
                    if (!rx_string_builder_append_c_extern_map(out, name)) return false;
                }
                break;
            case RX_OP_CALL_FILTER:
            case RX_OP_APPLY_TAKE_WHILE:
            case RX_OP_APPLY_SKIP_WHILE:
                if (!rx_string_builder_append_c_extern_predicate(out, name)) return false;
                break;
            case RX_OP_CALL_SCAN:
            case RX_OP_CALL_SCAN_MUT:
            case RX_OP_CALL_REDUCE:
            case RX_OP_CALL_REDUCE_MUT:
                if (op->kind == RX_OP_CALL_SCAN_MUT || op->kind == RX_OP_CALL_REDUCE_MUT)
                {
                    if (!rx_string_builder_append_c_extern_accum_mut(out, name)) return false;
                }
                else
                {
                    if (!rx_string_builder_append_c_extern_accum(out, name)) return false;
                }
                break;
            default:
                break;
        }
    }
    return true;
}

static const char *op_label(const RxPlannerIrOp *op)
{
    switch (op->kind)
    {
        case RX_OP_CALL_PAIR_MAP: return "pairMap";
        case RX_OP_CALL_TRIPLE_MAP: return "tripleMap";
        case RX_OP_CALL_MAP: return "map";
        case RX_OP_CALL_MAP_INTO: return "mapInto";
        case RX_OP_CALL_FILTER: return "filter";
        case RX_OP_CALL_SCAN: return "scan";
        case RX_OP_CALL_SCAN_MUT: return "scanMut";
        case RX_OP_CALL_REDUCE: return "reduce";
        case RX_OP_CALL_REDUCE_MUT: return "reduceMut";
        case RX_OP_CALL_MAP_TO: return "mapTo";
        case RX_OP_APPLY_TAKE: return "take";
        case RX_OP_APPLY_SKIP: return "skip";
        case RX_OP_APPLY_TAKE_WHILE: return "takeWhile";
        case RX_OP_APPLY_SKIP_WHILE: return "skipWhile";
        case RX_OP_APPLY_DISTINCT_UNTIL_CHANGED: return "distinctUntilChanged";
        case RX_OP_APPLY_LAST: return "last";
        case RX_OP_APPLY_FIRST: return "first";
        default: return "unknown";
    }
}

static bool emit_profile_decls(const RxPlannerIrPipeline *pipeline, RxStringBuilder *out)
{
    if (!rx_string_builder_append_c_profile_prelude(out))
    {
        return false;
    }

    if (!rx_string_builder_append_c_profile_slots_header(out, pipeline->pipeline_name != NULL ? pipeline->pipeline_name : "pipeline"))
    {
        return false;
    }
    for (int index = 0; index < pipeline->op_count; ++index)
    {
        if (!rx_string_builder_append_c_profile_slot_item(out, op_label(&pipeline->ops[index]), index))
        {
            return false;
        }
    }
    if (!rx_string_builder_append_c_profile_suffix_prefix(out, pipeline->pipeline_name != NULL ? pipeline->pipeline_name : "pipeline"))
    {
        return false;
    }
    if (!rx_string_builder_append_c_profile_suffix_tail(out))
    {
        return false;
    }
    return true;
}

static bool emit_inline_helper_clones(const RxPlannerIrPipeline *pipeline, const RxCCodegenOptions *options, RxStringBuilder *out)
{
    if (pipeline == NULL || options == NULL || options->helper_source_text == NULL)
    {
        return true;
    }

    RxSymbolList helpers = { 0 };
    for (int index = 0; index < pipeline->op_count; ++index)
    {
        const RxPlannerIrOp *op = &pipeline->ops[index];
        if (op->stage == NULL || op->stage->primary_argument.kind != RX_BINDING_FUNCTION_NAME)
        {
            continue;
        }
        if (!collect_inline_helper_closure(options->helper_source_text, op->stage->primary_argument.as.function_name, &helpers))
        {
            return false;
        }
    }
    if (helpers.count == 0)
    {
        return true;
    }

    for (int index = 0; index < helpers.count; ++index)
    {
        if (!rx_string_builder_append_format(out, "#define %s __rx_inline_%s\n", helpers.items[index], helpers.items[index]))
        {
            return false;
        }
    }
    if (!rx_string_builder_append(out, "\n"))
    {
        return false;
    }

    for (int index = 0; index < helpers.count; ++index)
    {
        RxInlineFunction function;
        if (!try_extract_inline_function(options->helper_source_text, helpers.items[index], &function))
        {
            continue;
        }
        if (!rx_string_builder_append_format(
                out,
                "static inline __attribute__((always_inline)) %s %s(",
                skip_storage_qualifiers(function.return_type),
                function.name))
        {
            free_inline_function(&function);
            return false;
        }
        for (int param_index = 0; param_index < function.parameter_count; ++param_index)
        {
            if (param_index > 0 && !rx_string_builder_append(out, ", "))
            {
                free_inline_function(&function);
                return false;
            }
            if (!rx_string_builder_append(out, function.parameter_decls[param_index]))
            {
                free_inline_function(&function);
                return false;
            }
        }
        if (!rx_string_builder_append(out, ");\n"))
        {
            free_inline_function(&function);
            return false;
        }
        free_inline_function(&function);
    }
    if (!rx_string_builder_append(out, "\n"))
    {
        return false;
    }

    for (int index = 0; index < helpers.count; ++index)
    {
        RxInlineFunction function;
        if (!try_extract_inline_function(options->helper_source_text, helpers.items[index], &function))
        {
            continue;
        }
        if (!rx_string_builder_append_format(
                out,
                "static inline __attribute__((always_inline)) %s %s(",
                skip_storage_qualifiers(function.return_type),
                function.name))
        {
            free_inline_function(&function);
            return false;
        }
        for (int param_index = 0; param_index < function.parameter_count; ++param_index)
        {
            if (param_index > 0 && !rx_string_builder_append(out, ", "))
            {
                free_inline_function(&function);
                return false;
            }
            if (!rx_string_builder_append(out, function.parameter_decls[param_index]))
            {
                free_inline_function(&function);
                return false;
            }
        }
        if (!rx_string_builder_append(out, ") {\n"))
        {
            free_inline_function(&function);
            return false;
        }
        if (!rx_string_builder_append(out, function.body)
            || !rx_string_builder_append(out, "\n}\n\n"))
        {
            free_inline_function(&function);
            return false;
        }
        free_inline_function(&function);
    }

    return true;
}

static bool emit_inline_helper_undefs(const RxPlannerIrPipeline *pipeline, const RxCCodegenOptions *options, RxStringBuilder *out)
{
    if (pipeline == NULL || options == NULL || options->helper_source_text == NULL)
    {
        return true;
    }

    RxSymbolList helpers = { 0 };
    for (int index = 0; index < pipeline->op_count; ++index)
    {
        const RxPlannerIrOp *op = &pipeline->ops[index];
        if (op->stage == NULL || op->stage->primary_argument.kind != RX_BINDING_FUNCTION_NAME)
        {
            continue;
        }
        if (!collect_inline_helper_closure(options->helper_source_text, op->stage->primary_argument.as.function_name, &helpers))
        {
            return false;
        }
    }
    for (int index = 0; index < helpers.count; ++index)
    {
        if (!rx_string_builder_append_format(out, "#undef %s\n", helpers.items[index]))
        {
            return false;
        }
    }
    return helpers.count == 0 || rx_string_builder_append(out, "\n");
}

static bool emit_profile_dump_function(const RxPlannerIrPipeline *pipeline, RxStringBuilder *out)
{
    const char *name = pipeline->pipeline_name != NULL ? pipeline->pipeline_name : "pipeline";
    if (!rx_string_builder_append_format(
            out,
            "#ifdef RX_PLANNER_PROFILE\n"
            "void rx_dump_profile_%s(FILE *stream) {\n"
            "    FILE *target = stream != NULL ? stream : stderr;\n"
            "    for (size_t index = 0; index < sizeof(rx_profile_slots_%s) / sizeof(rx_profile_slots_%s[0]); ++index) {\n"
            "        const double avg_ns = rx_profile_slots_%s[index].hits == 0\n"
            "            ? 0.0\n"
            "            : (double)rx_profile_slots_%s[index].total_ns / (double)rx_profile_slots_%s[index].hits;\n"
            "        fprintf(target,\n"
            "                \"planner profile %%s[%%zu] hits=%%llu total_ms=%%.5f avg_ns=%%.2f\\n\",\n"
            "                rx_profile_slots_%s[index].name,\n"
            "                index,\n"
            "                (unsigned long long)rx_profile_slots_%s[index].hits,\n"
            "                (double)rx_profile_slots_%s[index].total_ns / 1e6,\n"
            "                avg_ns);\n"
            "    }\n"
            "}\n"
            "#else\n"
            "void rx_dump_profile_%s(FILE *stream) {\n"
            "    (void)stream;\n"
            "}\n"
            "#endif\n\n",
            name,
            name,
            name,
            name,
            name,
            name,
            name,
            name,
            name,
            name))
    {
        return false;
    }
    return true;
}

static bool emit_storage_helpers(const RxPlannerIrPipeline *pipeline, RxStringBuilder *out)
{
    RxSymbolList symbols;
    const char *name = pipeline->pipeline_name != NULL ? pipeline->pipeline_name : "pipeline";
    if (!collect_storage_symbols(pipeline, &symbols))
    {
        return false;
    }

    if (!rx_string_builder_append(out,
            "static size_t rx_align_up_size(size_t value, size_t alignment) {\n"
            "    const size_t mask = alignment > 0 ? alignment - 1u : 0u;\n"
            "    return alignment > 0 ? (value + mask) & ~mask : value;\n"
            "}\n\n"))
    {
        return false;
    }

    if (symbols.count == 0)
    {
        return rx_string_builder_append_format(
            out,
            "size_t rx_storage_bytes_%s(size_t (*resolve)(const char *name, size_t *alignment)) {\n"
            "    (void)resolve;\n"
            "    return 0;\n"
            "}\n\n"
            "void rx_bind_storage_%s(void *base, size_t (*resolve)(const char *name, size_t *alignment), void (*bind)(const char *name, void *ptr)) {\n"
            "    (void)base;\n"
            "    (void)resolve;\n"
            "    (void)bind;\n"
            "}\n\n",
            name,
            name);
    }

    if (!rx_string_builder_append_format(
            out,
            "size_t rx_storage_bytes_%s(size_t (*resolve)(const char *name, size_t *alignment)) {\n"
            "    size_t cursor = 0;\n"
            "    for (size_t index = 0; index < %d; ++index) {\n"
            "        size_t alignment = 1;\n"
            "        const char *symbol = NULL;\n"
            "        size_t bytes = 0;\n"
            "        switch (index) {\n",
            name,
            symbols.count))
    {
        return false;
    }
    for (int index = 0; index < symbols.count; ++index)
    {
        if (!rx_string_builder_append_format(
                out,
                "            case %d: symbol = \"%s\"; break;\n",
                index,
                symbols.items[index]))
        {
            return false;
        }
    }
    if (!rx_string_builder_append(
            out,
            "            default: break;\n"
            "        }\n"
            "        if (symbol == NULL) { continue; }\n"
            "        bytes = resolve != NULL ? resolve(symbol, &alignment) : 0;\n"
            "        cursor = rx_align_up_size(cursor, alignment > 0 ? alignment : 1u);\n"
            "        cursor += bytes;\n"
            "    }\n"
            "    return cursor;\n"
            "}\n\n"))
    {
        return false;
    }

    if (!rx_string_builder_append_format(
            out,
            "void rx_bind_storage_%s(void *base, size_t (*resolve)(const char *name, size_t *alignment), void (*bind)(const char *name, void *ptr)) {\n"
            "    size_t cursor = 0;\n"
            "    for (size_t index = 0; index < %d; ++index) {\n"
            "        size_t alignment = 1;\n"
            "        const char *symbol = NULL;\n"
            "        size_t bytes = 0;\n"
            "        switch (index) {\n",
            name,
            symbols.count))
    {
        return false;
    }
    for (int index = 0; index < symbols.count; ++index)
    {
        if (!rx_string_builder_append_format(
                out,
                "            case %d: symbol = \"%s\"; break;\n",
                index,
                symbols.items[index]))
        {
            return false;
        }
    }
    if (!rx_string_builder_append(
            out,
            "            default: break;\n"
            "        }\n"
            "        if (symbol == NULL) { continue; }\n"
            "        bytes = resolve != NULL ? resolve(symbol, &alignment) : 0;\n"
            "        cursor = rx_align_up_size(cursor, alignment > 0 ? alignment : 1u);\n"
            "        if (bind != NULL) {\n"
            "            bind(symbol, (char *)base + cursor);\n"
            "        }\n"
            "        cursor += bytes;\n"
            "    }\n"
            "}\n\n"))
    {
        return false;
    }

    return true;
}

static bool emit_state_slot(RxStringBuilder *out, const RxPlannerIrStateSlot *slot)
{
    if (slot->value_type == RX_ARG_VOID_PTR)
    {
            switch (slot->initial_value.kind)
            {
            case RX_LITERAL_SYMBOL:
                return rx_string_builder_append_c_state_slot_void_symbol(out, slot->name, slot->initial_value.as.symbol_name);
            case RX_LITERAL_POINTER:
                return rx_string_builder_append_c_state_slot_void_ptr(out, slot->name, (uintptr_t)slot->initial_value.as.pointer_value);
            case RX_LITERAL_LONG:
                return rx_string_builder_append_c_state_slot_void_intptr(out, slot->name, (intptr_t)slot->initial_value.as.long_value);
            case RX_LITERAL_INT:
                return rx_string_builder_append_c_state_slot_void_intptr(out, slot->name, (intptr_t)slot->initial_value.as.int_value);
            default:
                return rx_string_builder_append_c_state_slot_void_null(out, slot->name);
            }
        }

    switch (slot->kind)
    {
        case RX_STATE_SKIP_WHILE_PASSED:
        case RX_STATE_HAS_LAST_VALUE:
        case RX_STATE_HAS_LAST_KEY:
            return rx_string_builder_append_c_state_slot_bool(out, slot->name, slot->initial_value.kind == RX_LITERAL_INT && slot->initial_value.as.int_value != 0);
        default:
            if (slot->initial_value.kind == RX_LITERAL_INT)
            {
                return rx_string_builder_append_c_state_slot_intptr(out, slot->name, (intptr_t)slot->initial_value.as.int_value);
            }
            if (slot->initial_value.kind == RX_LITERAL_LONG)
            {
                return rx_string_builder_append_c_state_slot_intptr(out, slot->name, (intptr_t)slot->initial_value.as.long_value);
            }
            return rx_string_builder_append_c_state_slot_intptr_zero(out, slot->name);
    }
}

static const char *state_slot_arg_expr(const RxPlannerIrStateSlot *slot, char *buffer, size_t buffer_size)
{
    if (slot != NULL && slot->value_type == RX_ARG_VOID_PTR)
    {
        snprintf(buffer, buffer_size, "%s", slot->name);
    }
    else if (slot != NULL)
    {
        snprintf(buffer, buffer_size, "(void *)(intptr_t)%s", slot->name);
    }
    else
    {
        snprintf(buffer, buffer_size, "NULL");
    }
    return buffer;
}

static bool emit_state_slot_assign(RxStringBuilder *out, const RxPlannerIrStateSlot *slot, const char *expr)
{
    if (slot != NULL && slot->value_type == RX_ARG_VOID_PTR)
    {
        return rx_string_builder_append_c_state_slot_assign_ptr(out, slot->name, expr);
    }
    return rx_string_builder_append_c_state_slot_assign_intptr(out, slot->name, expr);
}

static bool pipeline_has_breaking_op(const RxPlannerIrPipeline *pipeline)
{
    for (int index = 0; index < pipeline->op_count; ++index)
    {
        switch (pipeline->ops[index].kind)
        {
            case RX_OP_APPLY_TAKE:
            case RX_OP_APPLY_TAKE_WHILE:
            case RX_OP_APPLY_FIRST:
                return true;
            default:
                break;
        }
    }
    return false;
}

static intptr_t op_literal_value(const RxPlannerIrOp *op)
{
    if (op == NULL || op->stage == NULL || op->stage->primary_argument.kind != RX_BINDING_LITERAL)
    {
        return 0;
    }
    if (op->stage->primary_argument.as.literal.kind == RX_LITERAL_LONG)
    {
        return (intptr_t)op->stage->primary_argument.as.literal.as.long_value;
    }
    return (intptr_t)op->stage->primary_argument.as.literal.as.int_value;
}

static bool pipeline_uses_external_buffer(const RxPlannerIrPipeline *pipeline)
{
    return pipeline != NULL
        && (pipeline->source_kind == RX_LOOP_SOURCE_EXTERNAL_BUFFER
            || pipeline->source_kind == RX_LOOP_SOURCE_EXTERNAL_WINDOW);
}

static bool emit_loop_body(const RxPlannerIrPipeline *pipeline, const RxCCodegenOptions *options, RxStringBuilder *out)
{
    unsigned inline_counter = 0;
    if (pipeline->source_kind == RX_LOOP_SOURCE_ZIP_RANGE
        || pipeline->source_kind == RX_LOOP_SOURCE_ZIP_SYNTHETIC_RECORDS)
    {
        if ((pipeline->source_kind == RX_LOOP_SOURCE_ZIP_SYNTHETIC_RECORDS
                && (!rx_string_builder_append_format(
                        out,
                        "    for (intptr_t src = 1, zip_n = (intptr_t)%d < (intptr_t)%d ? (intptr_t)%d : (intptr_t)%d; src <= zip_n; ++src) {\n",
                        pipeline->source_count,
                        pipeline->source_inner_n,
                        pipeline->source_count,
                        pipeline->source_inner_n)
                    || !rx_string_builder_append(out, "        intptr_t left = src;\n")
                    || !rx_string_builder_append(out, "        intptr_t right = src;\n")
                    || !rx_string_builder_append(out, "        intptr_t value = 0;\n")))
            || (pipeline->source_kind == RX_LOOP_SOURCE_ZIP_RANGE
                && (!rx_string_builder_append(out, "    for (intptr_t src = 1; src <= N; ++src) {\n")
                    || !rx_string_builder_append(out, "        intptr_t left = src;\n")
                    || !rx_string_builder_append(out, "        intptr_t right = src;\n")
                    || !rx_string_builder_append(out, "        intptr_t value = 0;\n"))))
        {
            return false;
        }
    }
    else if (pipeline->source_kind == RX_LOOP_SOURCE_ZIP_MERGE_MAP_RANGE)
    {
        if (!rx_string_builder_append_format(out, "    for (intptr_t right = 1; right <= (intptr_t)%d; ++right) {\n", pipeline->source_inner_n)
            || !rx_string_builder_append(out, "        for (intptr_t src = 1; src <= N; ++src) {\n")
            || !rx_string_builder_append(out, "            intptr_t zipped_left = src;\n")
            || !rx_string_builder_append(out, "            intptr_t zipped_right = src;\n")
            || !rx_string_builder_append(out, "            intptr_t value = 0;\n"))
        {
            return false;
        }
    }
    else if (pipeline->source_kind == RX_LOOP_SOURCE_SYNTHETIC_RECORDS)
    {
        if (!rx_string_builder_append_format(out, "    for (intptr_t src = 1; src <= (intptr_t)%d; ++src) {\n", pipeline->source_count)
            || !rx_string_builder_append(out, "        intptr_t value = src;\n"))
        {
            return false;
        }
    }
    else if (pipeline->source_kind == RX_LOOP_SOURCE_EXTERNAL_BUFFER)
    {
        if (!rx_string_builder_append_c_loop_external_buffer(out))
        {
            return false;
        }
    }
    else if (pipeline->source_kind == RX_LOOP_SOURCE_EXTERNAL_WINDOW)
    {
        if (!rx_string_builder_append_c_loop_external_window_header(out, pipeline->source_inner_n, pipeline->source_inner_n))
        {
            return false;
        }
        for (int index = 0; index < pipeline->source_inner_n; ++index)
        {
            if (!rx_string_builder_append_c_loop_external_window_item(out, index, (intptr_t)index))
            {
                return false;
            }
        }
        if (!rx_string_builder_append_c_loop_external_window_value(out))
        {
            return false;
        }
    }
    else if (!rx_string_builder_append_c_loop_range_header(out)
             || !rx_string_builder_append_c_loop_range_value_src(out))
    {
        return false;
    }
    if (pipeline_has_breaking_op(pipeline)
        && !rx_string_builder_append_c_loop_break_flag(out))
    {
        return false;
    }

    for (int index = 0; index < pipeline->op_count; ++index)
    {
        const RxPlannerIrOp *op = &pipeline->ops[index];
        const RxPlannedStage *stage = op->stage;
        const char *fn = stage != NULL && stage->primary_argument.kind == RX_BINDING_FUNCTION_NAME ? stage->primary_argument.as.function_name : NULL;
        const char *helper_source_text = options != NULL ? options->helper_source_text : NULL;
        bool use_shared_value_view = op_uses_shared_value_view(op->kind);
        bool starts_shared_value_run =
            use_shared_value_view
            && (index == 0
                || !op_uses_shared_value_view(pipeline->ops[index - 1].kind)
                || !op_preserves_current_value(pipeline->ops[index - 1].kind));
        const char *value_arg_expr = use_shared_value_view ? "__rx_value_view" : "(void *)(intptr_t)value";
        RxInlineFunction function;
        bool can_inline = fn != NULL && try_extract_inline_function(helper_source_text, fn, &function);
        char result_name[32];
        char done_label[32];
        if (can_inline)
        {
            snprintf(result_name, sizeof(result_name), "__rx_result_%X", inline_counter);
            snprintf(done_label, sizeof(done_label), "__rx_done_%X", inline_counter);
            inline_counter += 1;
        }

        if (starts_shared_value_run
            && !rx_string_builder_append(out, "        void *__rx_value_view = (void *)(intptr_t)value;\n"))
        {
            if (can_inline) free_inline_function(&function);
            return false;
        }

        if (!rx_string_builder_append_format(out, "        RX_PROFILE_STAGE_BEGIN(%d);\n", index))
        {
            if (can_inline) free_inline_function(&function);
            return false;
        }

        switch (op->kind)
        {
            case RX_OP_CALL_PAIR_MAP:
            {
                if (can_inline)
                {
                    const char *args[] = { "(void *)(intptr_t)left", "(void *)(intptr_t)right" };
                    if (!rx_string_builder_append_format(out, "        %s %s = 0;\n", skip_storage_qualifiers(function.return_type), result_name)
                        || !emit_inline_call_block(out, helper_source_text, &function, args, 2, result_name, done_label, 0)
                        || !rx_string_builder_append_format(out, "        value = (intptr_t)%s;\n", result_name))
                    {
                        free_inline_function(&function);
                        return false;
                    }
                }
                else if (!rx_string_builder_append_format(out, "        value = (intptr_t)%s((void *)(intptr_t)left, (void *)(intptr_t)right);\n", fn))
                {
                    return false;
                }
                break;
            }
            case RX_OP_CALL_TRIPLE_MAP:
            {
                if (can_inline)
                {
                    const char *args[] = { "(void *)(intptr_t)zipped_left", "(void *)(intptr_t)zipped_right", "(void *)(intptr_t)right" };
                    if (!rx_string_builder_append_format(out, "        %s %s = 0;\n", skip_storage_qualifiers(function.return_type), result_name)
                        || !emit_inline_call_block(out, helper_source_text, &function, args, 3, result_name, done_label, 0)
                        || !rx_string_builder_append_format(out, "        value = (intptr_t)%s;\n", result_name))
                    {
                        free_inline_function(&function);
                        return false;
                    }
                }
                else if (!rx_string_builder_append_format(out, "        value = (intptr_t)%s((void *)(intptr_t)zipped_left, (void *)(intptr_t)zipped_right, (void *)(intptr_t)right);\n", fn))
                {
                    return false;
                }
                break;
            }
            case RX_OP_CALL_MAP:
            {
                if (can_inline)
                {
                    const char *args[] = { "(void *)(intptr_t)value" };
                    if (!rx_string_builder_append_format(out, "        %s %s = 0;\n", skip_storage_qualifiers(function.return_type), result_name)
                        || !emit_inline_call_block(out, helper_source_text, &function, args, 1, result_name, done_label, 0)
                        || !rx_string_builder_append_format(out, "        value = (intptr_t)%s;\n", result_name))
                    {
                        free_inline_function(&function);
                        return false;
                    }
                }
                else if (!rx_string_builder_append_format(out, "        value = (intptr_t)%s((void *)(intptr_t)value);\n", fn))
                {
                    return false;
                }
                break;
            }
            case RX_OP_CALL_MAP_INTO:
            {
                const char *scratch =
                    stage != NULL
                    && stage->secondary_argument.kind == RX_BINDING_LITERAL
                    && stage->secondary_argument.as.literal.kind == RX_LITERAL_SYMBOL
                    ? stage->secondary_argument.as.literal.as.symbol_name
                    : NULL;
                if (scratch == NULL)
                {
                    if (can_inline) free_inline_function(&function);
                    return false;
                }
                if (can_inline)
                {
                    bool emitted_reduced = false;
                    char callee_name[128];
                    char *nested_args[4] = { 0 };
                    int nested_arg_count = 0;
                    RxOwnedNameList live_fields = { 0 };
                    RxInlineFunction nested;
                    memset(&nested, 0, sizeof(nested));
                    if (false
                        && helper_source_text != NULL
                        && (index + 1) < pipeline->op_count
                        && op_uses_shared_value_view(pipeline->ops[index + 1].kind)
                        && collect_downstream_output_fields(pipeline, index + 1, options, &live_fields)
                        && live_fields.count > 0
                        && try_parse_single_call_body(function.body, callee_name, sizeof(callee_name), nested_args, &nested_arg_count)
                        && try_extract_inline_function(helper_source_text, callee_name, &nested)
                        && nested.parameter_count > 0)
                    {
                        const char *wrapper_args[] = { scratch, "(void *)(intptr_t)value" };
                        if (!rx_string_builder_append(out, "        {\n"))
                        {
                            free_inline_function(&nested);
                            owned_name_list_free(&live_fields);
                            for (int nested_index = 0; nested_index < nested_arg_count; ++nested_index) free(nested_args[nested_index]);
                            free_inline_function(&function);
                            return false;
                        }
                        for (int arg_index = 0; arg_index < function.parameter_count && arg_index < 2; ++arg_index)
                        {
                            if (!rx_string_builder_append_format(out, "            %s = %s;\n", function.parameter_decls[arg_index], wrapper_args[arg_index]))
                            {
                                free_inline_function(&nested);
                                owned_name_list_free(&live_fields);
                                for (int nested_index = 0; nested_index < nested_arg_count; ++nested_index) free(nested_args[nested_index]);
                                free_inline_function(&function);
                                return false;
                            }
                        }
                        for (int nested_index = 0; nested_index < nested.parameter_count && nested_index < nested_arg_count; ++nested_index)
                        {
                            if (!rx_string_builder_append_format(out, "            %s = %s;\n", nested.parameter_decls[nested_index], nested_args[nested_index]))
                            {
                                free_inline_function(&nested);
                                owned_name_list_free(&live_fields);
                                for (int free_index = 0; free_index < nested_arg_count; ++free_index) free(nested_args[free_index]);
                                free_inline_function(&function);
                                return false;
                            }
                        }
                        if (!emit_reduced_output_helper_body(out, nested.body, nested.parameter_names[0], &live_fields)
                            || !rx_string_builder_append(out, "        }\n")
                            || !rx_string_builder_append_format(out, "        value = (intptr_t)%s;\n", scratch))
                        {
                            free_inline_function(&nested);
                            owned_name_list_free(&live_fields);
                            for (int free_index = 0; free_index < nested_arg_count; ++free_index) free(nested_args[free_index]);
                            free_inline_function(&function);
                            return false;
                        }
                        emitted_reduced = true;
                    }
                    for (int free_index = 0; free_index < nested_arg_count; ++free_index)
                    {
                        free(nested_args[free_index]);
                    }
                    if (nested.name != NULL)
                    {
                        free_inline_function(&nested);
                    }
                    owned_name_list_free(&live_fields);
                    if (!emitted_reduced)
                    {
                        const char *args[] = { scratch, "(void *)(intptr_t)value" };
                        if (!emit_inline_call_block(out, helper_source_text, &function, args, 2, result_name, done_label, 0)
                            || !rx_string_builder_append_format(out, "        value = (intptr_t)%s;\n", scratch))
                        {
                            free_inline_function(&function);
                            return false;
                        }
                    }
                }
                else if (!rx_string_builder_append_format(out, "        %s(%s, (void *)(intptr_t)value);\n", fn, scratch)
                    || !rx_string_builder_append_format(out, "        value = (intptr_t)%s;\n", scratch))
                {
                    if (can_inline) free_inline_function(&function);
                    return false;
                }
                break;
            }
            case RX_OP_CALL_FILTER:
            {
                if (can_inline)
                {
                    const char *args[] = { value_arg_expr };
                    if (!rx_string_builder_append_format(out, "        %s %s = false;\n", skip_storage_qualifiers(function.return_type), result_name)
                        || !emit_inline_call_block(out, helper_source_text, &function, args, 1, result_name, done_label, 0)
                        || !rx_string_builder_append_format(out, "        if (!%s) { continue; }\n", result_name))
                    {
                        free_inline_function(&function);
                        return false;
                    }
                }
                else if (!rx_string_builder_append_format(out, "        if (!%s(%s)) { continue; }\n", fn, value_arg_expr))
                {
                    return false;
                }
                break;
            }
            case RX_OP_CALL_SCAN:
            {
                char arg0[96];
                state_slot_arg_expr(&pipeline->state_slots[op->state_slot_index], arg0, sizeof(arg0));
                if (can_inline)
                {
                    const char *args[] = { arg0, "(void *)(intptr_t)value" };
                    if (!rx_string_builder_append_format(out, "        %s %s = 0;\n", skip_storage_qualifiers(function.return_type), result_name)
                        || !emit_inline_call_block(out, helper_source_text, &function, args, 2, result_name, done_label, 0)
                        || !emit_state_slot_assign(out, &pipeline->state_slots[op->state_slot_index], result_name))
                    {
                        free_inline_function(&function);
                        return false;
                    }
                }
                else if (!rx_string_builder_append_format(
                        out,
                        pipeline->state_slots[op->state_slot_index].value_type == RX_ARG_VOID_PTR
                            ? "        %s = %s(%s, (void *)(intptr_t)value);\n"
                            : "        %s = (intptr_t)%s(%s, (void *)(intptr_t)value);\n",
                        pipeline->state_slots[op->state_slot_index].name,
                        fn,
                        arg0))
                {
                    if (can_inline) free_inline_function(&function);
                    return false;
                }
                if (!rx_string_builder_append_format(
                        out,
                        pipeline->state_slots[op->state_slot_index].value_type == RX_ARG_VOID_PTR
                            ? "        value = (intptr_t)%s;\n"
                            : "        value = %s;\n",
                        pipeline->state_slots[op->state_slot_index].name))
                {
                    if (can_inline) free_inline_function(&function);
                    return false;
                }
                break;
            }
            case RX_OP_CALL_REDUCE:
            {
                char arg0[96];
                state_slot_arg_expr(&pipeline->state_slots[op->state_slot_index], arg0, sizeof(arg0));
                if (can_inline)
                {
                    const char *args[] = { arg0, "(void *)(intptr_t)value" };
                    if (!rx_string_builder_append_format(out, "        %s %s = 0;\n", skip_storage_qualifiers(function.return_type), result_name)
                        || !emit_inline_call_block(out, helper_source_text, &function, args, 2, result_name, done_label, 0)
                        || !emit_state_slot_assign(out, &pipeline->state_slots[op->state_slot_index], result_name))
                    {
                        free_inline_function(&function);
                        return false;
                    }
                }
                else if (!rx_string_builder_append_format(
                        out,
                        pipeline->state_slots[op->state_slot_index].value_type == RX_ARG_VOID_PTR
                            ? "        %s = %s(%s, (void *)(intptr_t)value);\n"
                            : "        %s = (intptr_t)%s(%s, (void *)(intptr_t)value);\n",
                        pipeline->state_slots[op->state_slot_index].name,
                        fn,
                        arg0))
                {
                    return false;
                }
                if (!rx_string_builder_append_format(
                        out,
                        pipeline->state_slots[op->state_slot_index].value_type == RX_ARG_VOID_PTR
                            ? "        value = (intptr_t)%s;\n"
                            : "        value = %s;\n",
                        pipeline->state_slots[op->state_slot_index].name))
                {
                    if (can_inline) free_inline_function(&function);
                    return false;
                }
                break;
            }
            case RX_OP_CALL_SCAN_MUT:
            {
                char arg0[96];
                state_slot_arg_expr(&pipeline->state_slots[op->state_slot_index], arg0, sizeof(arg0));
                if (can_inline)
                {
                    const char *args[] = { arg0, "(void *)(intptr_t)value" };
                    if (!emit_inline_call_block(out, helper_source_text, &function, args, 2, result_name, done_label, 0)
                        || !rx_string_builder_append_format(
                            out,
                            pipeline->state_slots[op->state_slot_index].value_type == RX_ARG_VOID_PTR
                                ? "        value = (intptr_t)%s;\n"
                                : "        value = %s;\n",
                            pipeline->state_slots[op->state_slot_index].name))
                    {
                        free_inline_function(&function);
                        return false;
                    }
                }
                else if (!rx_string_builder_append_format(out, "        %s(%s, (void *)(intptr_t)value);\n", fn, arg0)
                    || !rx_string_builder_append_format(
                        out,
                        pipeline->state_slots[op->state_slot_index].value_type == RX_ARG_VOID_PTR
                            ? "        value = (intptr_t)%s;\n"
                            : "        value = %s;\n",
                        pipeline->state_slots[op->state_slot_index].name))
                {
                    if (can_inline) free_inline_function(&function);
                    return false;
                }
                break;
            }
            case RX_OP_CALL_REDUCE_MUT:
            {
                char arg0[96];
                state_slot_arg_expr(&pipeline->state_slots[op->state_slot_index], arg0, sizeof(arg0));
                if (can_inline)
                {
                    const char *args[] = { arg0, value_arg_expr };
                    if (!emit_inline_call_block(out, helper_source_text, &function, args, 2, result_name, done_label, 0)
                        || ((index + 1) < pipeline->op_count
                            && !rx_string_builder_append_format(
                                out,
                                pipeline->state_slots[op->state_slot_index].value_type == RX_ARG_VOID_PTR
                                    ? "        value = (intptr_t)%s;\n"
                                    : "        value = %s;\n",
                                pipeline->state_slots[op->state_slot_index].name)))
                    {
                        free_inline_function(&function);
                        return false;
                    }
                }
                else if (!rx_string_builder_append_format(out, "        %s(%s, %s);\n", fn, arg0, value_arg_expr)
                    || ((index + 1) < pipeline->op_count
                        && !rx_string_builder_append_format(
                            out,
                            pipeline->state_slots[op->state_slot_index].value_type == RX_ARG_VOID_PTR
                                ? "        value = (intptr_t)%s;\n"
                                : "        value = %s;\n",
                            pipeline->state_slots[op->state_slot_index].name)))
                {
                    if (can_inline) free_inline_function(&function);
                    return false;
                }
                break;
            }
            case RX_OP_APPLY_TAKE:
            {
                intptr_t limit = op_literal_value(op);
                if (!rx_string_builder_append_format(out, "        if (%s >= %" PRIdPTR ") { break; }\n", pipeline->state_slots[op->state_slot_index].name, limit)
                    || !rx_string_builder_append_format(out, "        %s += 1;\n", pipeline->state_slots[op->state_slot_index].name)
                    || !rx_string_builder_append_format(out, "        if (%s >= %" PRIdPTR ") { should_break = true; }\n", pipeline->state_slots[op->state_slot_index].name, limit))
                {
                    return false;
                }
                break;
            }
            case RX_OP_APPLY_SKIP:
            {
                intptr_t limit = op_literal_value(op);
                if (!rx_string_builder_append_format(out, "        if (%s < %" PRIdPTR ") {\n", pipeline->state_slots[op->state_slot_index].name, limit)
                    || !rx_string_builder_append_format(out, "            %s += 1;\n", pipeline->state_slots[op->state_slot_index].name)
                    || !rx_string_builder_append(out, "            continue;\n        }\n"))
                {
                    return false;
                }
                break;
            }
            case RX_OP_APPLY_TAKE_WHILE:
            {
                if (can_inline)
                {
                    const char *args[] = { value_arg_expr };
                    if (!rx_string_builder_append_format(out, "        %s %s = false;\n", skip_storage_qualifiers(function.return_type), result_name)
                        || !emit_inline_call_block(out, helper_source_text, &function, args, 1, result_name, done_label, 0)
                        || !rx_string_builder_append_format(out, "        if (!%s) { break; }\n", result_name))
                    {
                        free_inline_function(&function);
                        return false;
                    }
                }
                else if (!rx_string_builder_append_format(out, "        if (!%s(%s)) { break; }\n", fn, value_arg_expr))
                {
                    return false;
                }
                break;
            }
            case RX_OP_APPLY_DISTINCT_UNTIL_CHANGED:
            {
                if (can_inline)
                {
                    const char *args[] = { value_arg_expr };
                    if (!rx_string_builder_append_format(out, "        %s %s = 0;\n", skip_storage_qualifiers(function.return_type), result_name)
                        || !emit_inline_call_block(out, helper_source_text, &function, args, 1, result_name, done_label, 0)
                        || !rx_string_builder_append_format(out, "        intptr_t key_%d = (intptr_t)%s;\n", index, result_name))
                    {
                        free_inline_function(&function);
                        return false;
                    }
                }
                else if (!rx_string_builder_append_format(out, "        intptr_t key_%d = (intptr_t)%s(%s);\n", index, fn, value_arg_expr))
                {
                    return false;
                }
                if (!rx_string_builder_append_format(out, "        if (%s && key_%d == %s) { continue; }\n", pipeline->state_slots[op->aux_state_slot_index].name, index, pipeline->state_slots[op->state_slot_index].name)
                    || !rx_string_builder_append_format(out, "        %s = key_%d;\n", pipeline->state_slots[op->state_slot_index].name, index)
                    || !rx_string_builder_append_format(out, "        %s = true;\n", pipeline->state_slots[op->aux_state_slot_index].name))
                {
                    if (can_inline) free_inline_function(&function);
                    return false;
                }
                break;
            }
            case RX_OP_APPLY_SKIP_WHILE:
            {
                if (can_inline)
                {
                    const char *args[] = { value_arg_expr };
                    if (!rx_string_builder_append_format(out, "        %s %s = false;\n", skip_storage_qualifiers(function.return_type), result_name)
                        || !emit_inline_call_block(out, helper_source_text, &function, args, 1, result_name, done_label, 0)
                        || !rx_string_builder_append_format(out, "        if (!%s && %s) { continue; }\n", pipeline->state_slots[op->state_slot_index].name, result_name))
                    {
                        free_inline_function(&function);
                        return false;
                    }
                }
                else if (!rx_string_builder_append_format(out, "        if (!%s && %s(%s)) { continue; }\n", pipeline->state_slots[op->state_slot_index].name, fn, value_arg_expr))
                {
                    return false;
                }
                if (!rx_string_builder_append_format(out, "        %s = true;\n", pipeline->state_slots[op->state_slot_index].name))
                {
                    if (can_inline) free_inline_function(&function);
                    return false;
                }
                break;
            }
            case RX_OP_APPLY_FIRST:
                if (!rx_string_builder_append(out, "        should_break = true;\n"))
                {
                    return false;
                }
                break;
            case RX_OP_APPLY_LAST:
                if (!rx_string_builder_append_format(out, "        %s = value;\n", pipeline->state_slots[op->state_slot_index].name)
                    || !rx_string_builder_append_format(out, "        %s = true;\n", pipeline->state_slots[op->aux_state_slot_index].name))
                {
                    return false;
                }
                break;
            default:
                if (can_inline) free_inline_function(&function);
                return false;
        }

        if (!rx_string_builder_append_format(out, "        RX_PROFILE_STAGE_END(%d);\n", index))
        {
            if (can_inline) free_inline_function(&function);
            return false;
        }
        if (can_inline) free_inline_function(&function);
    }

    if (pipeline_has_breaking_op(pipeline)
        && !rx_string_builder_append(out, "        if (should_break) { break; }\n"))
    {
        return false;
    }

    if (pipeline->source_kind == RX_LOOP_SOURCE_ZIP_MERGE_MAP_RANGE)
    {
        return rx_string_builder_append(out, "        }\n    }\n");
    }
    return rx_string_builder_append(out, "    }\n");
}

bool rx_emit_c_segment_function(const RxPlannerIrPipeline *pipeline, const RxCCodegenOptions *options, RxStringBuilder *out, RxDiagnosticBag *diagnostics)
{
    (void)diagnostics;
    if (!rx_string_builder_append_c_segment_signature(
            out,
            pipeline->pipeline_name != NULL ? pipeline->pipeline_name : "pipeline",
            pipeline_uses_external_buffer(pipeline)))
    {
        return false;
    }
    for (int index = 0; index < pipeline->state_slot_count; ++index)
    {
        if (!emit_state_slot(out, &pipeline->state_slots[index]))
        {
            return false;
        }
    }

    size_t loop_start = out->length;
    bool used_graph = false;
    if (options != NULL && options->enable_graph_optimizations && options->helper_source_text != NULL)
    {
        used_graph = rx_try_emit_graph_optimized_loop_body(pipeline, options, out);
        if (!used_graph)
        {
            out->length = loop_start;
            if (out->data != NULL)
            {
                out->data[out->length] = '\0';
            }
        }
    }
    if (!used_graph && !emit_loop_body(pipeline, options, out))
    {
        return false;
    }

    const RxPlannerIrOp *last_op = pipeline->op_count > 0 ? &pipeline->ops[pipeline->op_count - 1] : NULL;
    if (last_op != NULL && last_op->kind == RX_OP_APPLY_LAST)
    {
        if (!rx_string_builder_append_c_return_last(out, pipeline->state_slots[last_op->aux_state_slot_index].name, pipeline->state_slots[last_op->state_slot_index].name))
        {
            return false;
        }
    }
    else if (last_op != NULL
        && (last_op->kind == RX_OP_CALL_REDUCE || last_op->kind == RX_OP_CALL_REDUCE_MUT))
    {
        if (!rx_string_builder_append_c_return_name(
                out,
                pipeline->state_slots[last_op->state_slot_index].name,
                pipeline->state_slots[last_op->state_slot_index].value_type == RX_ARG_VOID_PTR))
        {
            return false;
        }
    }
    else if (!rx_string_builder_append_c_return_zero(out))
    {
        return false;
    }
    return rx_string_builder_append_c_function_end(out);
}

static bool add_builder_chunk(RxCProgram *program, RxStringBuilder *chunk)
{
    bool ok = true;
    if (chunk->data != NULL && chunk->length > 0)
    {
        ok = rx_c_program_add_raw(program, chunk->data);
    }
    rx_string_builder_reset(chunk);
    rx_string_builder_init(chunk);
    return ok;
}

static bool add_builder_lines_to_block(RxCProgram *program, RxCBlock *block, const char *text)
{
    if (program == NULL || block == NULL || text == NULL)
    {
        return false;
    }
    const char *line_start = text;
    const char *cursor = text;
    while (*cursor != '\0')
    {
        if (*cursor == '\n')
        {
            size_t length = (size_t)(cursor - line_start);
            if (length > 0)
            {
                char *line = malloc(length + 1);
                if (line == NULL)
                {
                    return false;
                }
                memcpy(line, line_start, length);
                line[length] = '\0';
                bool ok = rx_c_block_add_raw(program, block, line);
                free(line);
                if (!ok)
                {
                    return false;
                }
            }
            line_start = cursor + 1;
        }
        ++cursor;
    }
    if (cursor != line_start)
    {
        size_t length = (size_t)(cursor - line_start);
        char *line = malloc(length + 1);
        if (line == NULL)
        {
            return false;
        }
        memcpy(line, line_start, length);
        line[length] = '\0';
        bool ok = rx_c_block_add_raw(program, block, line);
        free(line);
        if (!ok)
        {
            return false;
        }
    }
    return true;
}

static bool add_builder_function(
    RxCProgram *program,
    const char *return_type,
    const char *name,
    const char **param_types,
    const char **param_names,
    int param_count,
    RxStringBuilder *body_text)
{
    RxCBlock body;
    rx_c_block_init(&body);
    bool ok = add_builder_lines_to_block(program, &body, body_text != NULL && body_text->data != NULL ? body_text->data : "");
    RxCParam params[4];
    if (ok)
    {
        for (int index = 0; index < param_count; ++index)
        {
            rx_c_type_init(&params[index].type);
            ok = rx_c_type_set_raw(program, &params[index].type, param_types[index]);
            params[index].name_id = rx_c_string_pool_intern(&program->strings, param_names[index]);
            if (!ok || params[index].name_id == UINT32_MAX)
            {
                ok = false;
                break;
            }
        }
    }
    if (ok)
    {
        ok = rx_c_program_add_function(program, return_type, name, params, param_count, &body);
    }
    for (int index = 0; index < param_count; ++index)
    {
        rx_c_type_reset(&params[index].type);
    }
    rx_c_block_reset(&body);
    rx_string_builder_reset(body_text);
    rx_string_builder_init(body_text);
    return ok;
}

static char *format_owned(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (length < 0)
    {
        va_end(args);
        return NULL;
    }
    char *buffer = malloc((size_t)length + 1);
    if (buffer == NULL)
    {
        va_end(args);
        return NULL;
    }
    vsnprintf(buffer, (size_t)length + 1, format, args);
    va_end(args);
    return buffer;
}

static bool add_raw_exprf(RxCProgram *program, RxCBlock *block, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (length < 0)
    {
        va_end(args);
        return false;
    }
    char *buffer = malloc((size_t)length + 1);
    if (buffer == NULL)
    {
        va_end(args);
        return false;
    }
    vsnprintf(buffer, (size_t)length + 1, format, args);
    va_end(args);
    bool ok = rx_c_block_add_expr(program, block, buffer);
    free(buffer);
    return ok;
}

static bool make_ident_expr(RxCProgram *program, RxCExpr *out_expr, const char *name)
{
    rx_c_expr_init(out_expr);
    return rx_c_expr_set_ident(program, out_expr, name);
}

static bool make_int_expr(RxCExpr *out_expr, int64_t value)
{
    rx_c_expr_init(out_expr);
    return rx_c_expr_set_int(out_expr, value);
}

static bool make_raw_expr(RxCProgram *program, RxCExpr *out_expr, const char *text)
{
    rx_c_expr_init(out_expr);
    return rx_c_expr_set_raw(program, out_expr, text);
}

static bool make_index_expr(RxCProgram *program, RxCExpr *out_expr, const char *base_name, int64_t index_value)
{
    RxCExpr base;
    RxCExpr index;
    rx_c_expr_init(&base);
    rx_c_expr_init(&index);
    bool ok = make_ident_expr(program, &base, base_name)
        && make_int_expr(&index, index_value)
        && rx_c_expr_set_index(out_expr, &base, &index);
    rx_c_expr_reset(&base);
    rx_c_expr_reset(&index);
    return ok;
}

static bool make_index_expr_from_expr(RxCProgram *program, RxCExpr *out_expr, const char *base_name, const RxCExpr *index_expr)
{
    RxCExpr base;
    rx_c_expr_init(&base);
    bool ok = make_ident_expr(program, &base, base_name)
        && rx_c_expr_set_index(out_expr, &base, index_expr);
    rx_c_expr_reset(&base);
    return ok;
}

static bool make_call_expr1(RxCProgram *program, RxCExpr *out_expr, const char *callee, const RxCExpr *arg0)
{
    RxCExpr args[1];
    rx_c_expr_init(&args[0]);
    bool ok = rx_c_expr_clone(&args[0], arg0)
        && rx_c_expr_set_call(program, out_expr, callee, args, 1);
    rx_c_expr_reset(&args[0]);
    return ok;
}

static bool make_call_expr2(RxCProgram *program, RxCExpr *out_expr, const char *callee, const RxCExpr *arg0, const RxCExpr *arg1)
{
    RxCExpr args[2];
    rx_c_expr_init(&args[0]);
    rx_c_expr_init(&args[1]);
    bool ok = rx_c_expr_clone(&args[0], arg0)
        && rx_c_expr_clone(&args[1], arg1)
        && rx_c_expr_set_call(program, out_expr, callee, args, 2);
    rx_c_expr_reset(&args[0]);
    rx_c_expr_reset(&args[1]);
    return ok;
}

static bool make_cast_expr_raw_type(RxCProgram *program, RxCExpr *out_expr, const char *type_text, const RxCExpr *child)
{
    RxCType type;
    rx_c_type_init(&type);
    bool ok = rx_c_type_set_raw(program, &type, type_text)
        && rx_c_expr_set_cast(program, out_expr, &type, child);
    rx_c_type_reset(&type);
    return ok;
}

static bool make_binary_expr(RxCProgram *program, RxCExpr *out_expr, const char *op, const RxCExpr *left, const RxCExpr *right)
{
    rx_c_expr_init(out_expr);
    return rx_c_expr_set_binary(program, out_expr, op, left, right);
}

static bool add_expr_stmt_take_ownership(RxCProgram *program, RxCBlock *block, RxCExpr *expr)
{
    bool ok = rx_c_block_add_expr_stmt(program, block, expr);
    rx_c_expr_reset(expr);
    return ok;
}

static bool add_decl_take_ownership(RxCProgram *program, RxCBlock *block, const char *type_text, const char *name_text, RxCExpr *init_expr)
{
    RxCType type;
    rx_c_type_init(&type);
    bool ok = rx_c_type_set_raw(program, &type, type_text)
        && rx_c_block_add_decl_expr(program, block, &type, name_text, init_expr);
    rx_c_type_reset(&type);
    rx_c_expr_reset(init_expr);
    return ok;
}

static bool add_raw_declf(RxCProgram *program, RxCBlock *block, const char *type_text, const char *name_text, const char *format, ...)
{
    char *buffer = NULL;
    if (format != NULL)
    {
        va_list args;
        va_start(args, format);
        va_list copy;
        va_copy(copy, args);
        int length = vsnprintf(NULL, 0, format, copy);
        va_end(copy);
        if (length < 0)
        {
            va_end(args);
            return false;
        }
        buffer = malloc((size_t)length + 1);
        if (buffer == NULL)
        {
            va_end(args);
            return false;
        }
        vsnprintf(buffer, (size_t)length + 1, format, args);
        va_end(args);
    }
    bool ok = rx_c_block_add_decl(program, block, type_text, name_text, buffer);
    free(buffer);
    return ok;
}

static bool add_state_slot_decl_ast(RxCProgram *program, RxCBlock *block, const RxPlannerIrStateSlot *slot)
{
    if (slot->value_type == RX_ARG_VOID_PTR)
    {
        switch (slot->initial_value.kind)
        {
            case RX_LITERAL_SYMBOL:
                return rx_c_block_add_decl(program, block, "void *", slot->name, slot->initial_value.as.symbol_name);
            case RX_LITERAL_POINTER:
                return add_raw_declf(program, block, "void *", slot->name, "(void *)%" PRIuPTR, (uintptr_t)slot->initial_value.as.pointer_value);
            case RX_LITERAL_LONG:
                return add_raw_declf(program, block, "void *", slot->name, "(void *)(intptr_t)%" PRIdPTR, (intptr_t)slot->initial_value.as.long_value);
            case RX_LITERAL_INT:
                return add_raw_declf(program, block, "void *", slot->name, "(void *)(intptr_t)%" PRIdPTR, (intptr_t)slot->initial_value.as.int_value);
            default:
                return rx_c_block_add_decl(program, block, "void *", slot->name, "NULL");
        }
    }
    switch (slot->kind)
    {
        case RX_STATE_SKIP_WHILE_PASSED:
        case RX_STATE_HAS_LAST_VALUE:
        case RX_STATE_HAS_LAST_KEY:
            return rx_c_block_add_decl(
                program,
                block,
                "bool",
                slot->name,
                slot->initial_value.kind == RX_LITERAL_INT && slot->initial_value.as.int_value != 0 ? "true" : "false");
        default:
            if (slot->initial_value.kind == RX_LITERAL_INT)
            {
                return add_raw_declf(program, block, "intptr_t", slot->name, "%" PRIdPTR, (intptr_t)slot->initial_value.as.int_value);
            }
            if (slot->initial_value.kind == RX_LITERAL_LONG)
            {
                return add_raw_declf(program, block, "intptr_t", slot->name, "%" PRIdPTR, (intptr_t)slot->initial_value.as.long_value);
            }
            return rx_c_block_add_decl(program, block, "intptr_t", slot->name, "0");
    }
}

static bool build_segment_ast_body(
    RxCProgram *program,
    const RxPlannerIrPipeline *pipeline,
    const RxCCodegenOptions *options,
    RxCBlock *body)
{
    (void)options;
    for (int index = 0; index < pipeline->state_slot_count; ++index)
    {
        if (!add_state_slot_decl_ast(program, body, &pipeline->state_slots[index]))
        {
            return false;
        }
    }

    if (pipeline->source_kind != RX_LOOP_SOURCE_EXTERNAL_BUFFER)
    {
        return false;
    }

    if (!rx_c_block_add_decl(program, body, "intptr_t", "src", "0"))
    {
        return false;
    }
    RxCExpr src_ident;
    RxCExpr n_ident;
    RxCExpr cond_expr;
    RxCExpr one_expr;
    RxCExpr plus_expr;
    RxCExpr update_expr;
    rx_c_expr_init(&src_ident);
    rx_c_expr_init(&n_ident);
    rx_c_expr_init(&cond_expr);
    rx_c_expr_init(&one_expr);
    rx_c_expr_init(&plus_expr);
    rx_c_expr_init(&update_expr);
    bool loop_ok =
        make_ident_expr(program, &src_ident, "src")
        && make_ident_expr(program, &n_ident, "N")
        && make_binary_expr(program, &cond_expr, "<", &src_ident, &n_ident)
        && make_int_expr(&one_expr, 1)
        && make_binary_expr(program, &plus_expr, "+", &src_ident, &one_expr)
        && make_binary_expr(program, &update_expr, "=", &src_ident, &plus_expr);
    RxCBlock *loop = NULL;
    if (loop_ok)
    {
        loop = rx_c_stmt_set_for(program, body, NULL, NULL, NULL);
        if (loop != NULL)
        {
            RxCStmt *loop_stmt = &body->items[body->count - 1];
            loop_ok = rx_c_expr_clone(&loop_stmt->condition, &cond_expr)
                && rx_c_expr_clone(&loop_stmt->update, &update_expr);
        }
    }
    rx_c_expr_reset(&src_ident);
    rx_c_expr_reset(&n_ident);
    rx_c_expr_reset(&cond_expr);
    rx_c_expr_reset(&one_expr);
    rx_c_expr_reset(&plus_expr);
    rx_c_expr_reset(&update_expr);
    if (!loop_ok || loop == NULL)
    {
        return false;
    }

    RxCExpr src_expr;
    RxCExpr record_index_expr;
    RxCExpr cast_value_expr;
    rx_c_expr_init(&src_expr);
    rx_c_expr_init(&record_index_expr);
    rx_c_expr_init(&cast_value_expr);
    bool value_ok =
        make_ident_expr(program, &src_expr, "src")
        && make_index_expr_from_expr(program, &record_index_expr, "records", &src_expr)
        && make_cast_expr_raw_type(program, &cast_value_expr, "intptr_t", &record_index_expr)
        && add_decl_take_ownership(program, loop, "intptr_t", "value", &cast_value_expr);
    rx_c_expr_reset(&src_expr);
    rx_c_expr_reset(&record_index_expr);
    if (!value_ok)
    {
        rx_c_expr_reset(&cast_value_expr);
        return false;
    }

    bool has_breaking = pipeline_has_breaking_op(pipeline);
    if (has_breaking && !rx_c_block_add_decl(program, loop, "bool", "should_break", "false"))
    {
        return false;
    }

    for (int index = 0; index < pipeline->op_count; ++index)
    {
        const RxPlannerIrOp *op = &pipeline->ops[index];
        const RxPlannedStage *stage = op->stage;
        const char *fn = stage != NULL && stage->primary_argument.kind == RX_BINDING_FUNCTION_NAME ? stage->primary_argument.as.function_name : NULL;
        bool use_shared_value_view = op_uses_shared_value_view(op->kind);
        bool starts_shared_value_run =
            use_shared_value_view
            && (index == 0
                || !op_uses_shared_value_view(pipeline->ops[index - 1].kind)
                || !op_preserves_current_value(pipeline->ops[index - 1].kind));
        const char *value_arg_expr = use_shared_value_view ? "__rx_value_view" : "(void *)(intptr_t)value";

        if (starts_shared_value_run)
        {
            RxCExpr value_ident;
            RxCExpr cast_view_expr;
            rx_c_expr_init(&value_ident);
            rx_c_expr_init(&cast_view_expr);
            bool ok =
                make_ident_expr(program, &value_ident, "value")
                && make_cast_expr_raw_type(program, &cast_view_expr, "void *", &value_ident)
                && add_decl_take_ownership(program, loop, "void *", "__rx_value_view", &cast_view_expr);
            rx_c_expr_reset(&value_ident);
            if (!ok)
            {
                return false;
            }
        }
        if (!add_raw_exprf(program, loop, "RX_PROFILE_STAGE_BEGIN(%d)", index))
        {
            return false;
        }

        switch (op->kind)
        {
            case RX_OP_CALL_MAP_INTO:
            {
                const char *scratch =
                    stage != NULL
                    && stage->secondary_argument.kind == RX_BINDING_LITERAL
                    && stage->secondary_argument.as.literal.kind == RX_LITERAL_SYMBOL
                    ? stage->secondary_argument.as.literal.as.symbol_name
                    : NULL;
                if (scratch == NULL)
                {
                    return false;
                }
                RxCExpr scratch_ident;
                RxCExpr value_ident;
                RxCExpr cast_arg_expr;
                RxCExpr call_expr;
                RxCExpr cast_back_expr;
                RxCExpr lhs_value_expr;
                rx_c_expr_init(&scratch_ident);
                rx_c_expr_init(&value_ident);
                rx_c_expr_init(&cast_arg_expr);
                rx_c_expr_init(&call_expr);
                rx_c_expr_init(&cast_back_expr);
                rx_c_expr_init(&lhs_value_expr);
                bool ok =
                    make_ident_expr(program, &scratch_ident, scratch)
                    && make_ident_expr(program, &value_ident, "value")
                    && make_cast_expr_raw_type(program, &cast_arg_expr, "void *", &value_ident)
                    && make_call_expr2(program, &call_expr, fn, &scratch_ident, &cast_arg_expr)
                    && add_expr_stmt_take_ownership(program, loop, &call_expr)
                    && make_cast_expr_raw_type(program, &cast_back_expr, "intptr_t", &scratch_ident)
                    && make_ident_expr(program, &lhs_value_expr, "value")
                    && rx_c_block_add_assign_stmt(program, loop, &lhs_value_expr, &cast_back_expr);
                rx_c_expr_reset(&scratch_ident);
                rx_c_expr_reset(&value_ident);
                rx_c_expr_reset(&cast_arg_expr);
                rx_c_expr_reset(&cast_back_expr);
                rx_c_expr_reset(&lhs_value_expr);
                if (!ok)
                {
                    return false;
                }
                break;
            }
            case RX_OP_CALL_SCAN_MUT:
            {
                const char *state_name = pipeline->state_slots[op->state_slot_index].name;
                RxCExpr state_expr;
                RxCExpr value_expr;
                RxCExpr cast_arg_expr;
                RxCExpr call_expr;
                RxCExpr lhs_expr;
                RxCExpr rhs_expr;
                rx_c_expr_init(&state_expr);
                rx_c_expr_init(&value_expr);
                rx_c_expr_init(&cast_arg_expr);
                rx_c_expr_init(&call_expr);
                rx_c_expr_init(&lhs_expr);
                rx_c_expr_init(&rhs_expr);
                bool ok =
                    make_ident_expr(program, &state_expr, state_name)
                    && make_ident_expr(program, &value_expr, "value")
                    && make_cast_expr_raw_type(program, &cast_arg_expr, "void *", &value_expr)
                    && make_call_expr2(program, &call_expr, fn, &state_expr, &cast_arg_expr)
                    && add_expr_stmt_take_ownership(program, loop, &call_expr)
                    && make_ident_expr(program, &lhs_expr, "value")
                    && (pipeline->state_slots[op->state_slot_index].value_type == RX_ARG_VOID_PTR
                        ? make_cast_expr_raw_type(program, &rhs_expr, "intptr_t", &state_expr)
                        : make_ident_expr(program, &rhs_expr, state_name))
                    && rx_c_block_add_assign_stmt(program, loop, &lhs_expr, &rhs_expr);
                rx_c_expr_reset(&state_expr);
                rx_c_expr_reset(&value_expr);
                rx_c_expr_reset(&cast_arg_expr);
                rx_c_expr_reset(&lhs_expr);
                rx_c_expr_reset(&rhs_expr);
                if (!ok)
                {
                    return false;
                }
                break;
            }
            case RX_OP_CALL_FILTER:
            {
                RxCExpr arg_expr;
                RxCExpr src_value_expr;
                RxCExpr call_expr;
                RxCExpr cond_expr2;
                rx_c_expr_init(&arg_expr);
                rx_c_expr_init(&src_value_expr);
                rx_c_expr_init(&call_expr);
                rx_c_expr_init(&cond_expr2);
                bool ok;
                if (use_shared_value_view)
                {
                    ok = make_ident_expr(program, &arg_expr, "__rx_value_view");
                }
                else
                {
                    ok = make_ident_expr(program, &src_value_expr, "value")
                        && make_cast_expr_raw_type(program, &arg_expr, "void *", &src_value_expr);
                }
                ok = ok
                    && make_call_expr1(program, &call_expr, fn, &arg_expr)
                    && rx_c_expr_set_unary(program, &cond_expr2, "!", &call_expr);
                RxCBlock *then_block = ok ? rx_c_stmt_set_if_expr(program, loop, &cond_expr2) : NULL;
                rx_c_expr_reset(&arg_expr);
                rx_c_expr_reset(&src_value_expr);
                rx_c_expr_reset(&call_expr);
                rx_c_expr_reset(&cond_expr2);
                if (then_block == NULL || !rx_c_block_add_continue(program, then_block))
                {
                    return false;
                }
                break;
            }
            case RX_OP_APPLY_DISTINCT_UNTIL_CHANGED:
            {
                char *key_name = format_owned("key_%d", index);
                RxCExpr arg_expr;
                RxCExpr src_value_expr;
                RxCExpr call_expr;
                RxCExpr key_expr;
                rx_c_expr_init(&arg_expr);
                rx_c_expr_init(&src_value_expr);
                rx_c_expr_init(&call_expr);
                rx_c_expr_init(&key_expr);
                bool ok;
                if (use_shared_value_view)
                {
                    ok = make_ident_expr(program, &arg_expr, "__rx_value_view");
                }
                else
                {
                    ok = make_ident_expr(program, &src_value_expr, "value")
                        && make_cast_expr_raw_type(program, &arg_expr, "void *", &src_value_expr);
                }
                ok = key_name != NULL
                    && ok
                    && make_call_expr1(program, &call_expr, fn, &arg_expr)
                    && make_cast_expr_raw_type(program, &key_expr, "intptr_t", &call_expr)
                    && add_decl_take_ownership(program, loop, "intptr_t", key_name, &key_expr);
                rx_c_expr_reset(&arg_expr);
                rx_c_expr_reset(&src_value_expr);
                rx_c_expr_reset(&call_expr);
                free(key_name);
                if (!ok)
                {
                    return false;
                }
                char *cond = format_owned("(%s && key_%d == %s)", pipeline->state_slots[op->aux_state_slot_index].name, index, pipeline->state_slots[op->state_slot_index].name);
                if (cond == NULL)
                {
                    return false;
                }
                RxCBlock *then_block = rx_c_stmt_set_if(program, loop, cond);
                free(cond);
                if (then_block == NULL
                    || !rx_c_block_add_continue(program, then_block)
                    || !add_raw_exprf(program, loop, "%s = key_%d", pipeline->state_slots[op->state_slot_index].name, index)
                    || !add_raw_exprf(program, loop, "%s = true", pipeline->state_slots[op->aux_state_slot_index].name))
                {
                    return false;
                }
                break;
            }
            case RX_OP_APPLY_SKIP_WHILE:
            {
                RxCExpr passed_expr;
                RxCExpr predicate_arg_expr;
                RxCExpr src_value_expr;
                RxCExpr call_expr;
                RxCExpr not_passed_expr;
                RxCExpr cond_expr2;
                rx_c_expr_init(&passed_expr);
                rx_c_expr_init(&predicate_arg_expr);
                rx_c_expr_init(&src_value_expr);
                rx_c_expr_init(&call_expr);
                rx_c_expr_init(&not_passed_expr);
                rx_c_expr_init(&cond_expr2);
                bool ok =
                    make_ident_expr(program, &passed_expr, pipeline->state_slots[op->state_slot_index].name);
                if (ok && use_shared_value_view)
                {
                    ok = make_ident_expr(program, &predicate_arg_expr, "__rx_value_view");
                }
                else if (ok)
                {
                    ok = make_ident_expr(program, &src_value_expr, "value")
                        && make_cast_expr_raw_type(program, &predicate_arg_expr, "void *", &src_value_expr);
                }
                ok = ok
                    && rx_c_expr_set_unary(program, &not_passed_expr, "!", &passed_expr)
                    && make_call_expr1(program, &call_expr, fn, &predicate_arg_expr)
                    && make_binary_expr(program, &cond_expr2, "&&", &not_passed_expr, &call_expr);
                RxCBlock *then_block = ok ? rx_c_stmt_set_if_expr(program, loop, &cond_expr2) : NULL;
                rx_c_expr_reset(&passed_expr);
                rx_c_expr_reset(&predicate_arg_expr);
                rx_c_expr_reset(&src_value_expr);
                rx_c_expr_reset(&call_expr);
                rx_c_expr_reset(&not_passed_expr);
                rx_c_expr_reset(&cond_expr2);
                if (then_block == NULL
                    || !rx_c_block_add_continue(program, then_block)
                    || !add_raw_exprf(program, loop, "%s = true", pipeline->state_slots[op->state_slot_index].name))
                {
                    return false;
                }
                break;
            }
            case RX_OP_CALL_REDUCE_MUT:
            {
                const char *state_name = pipeline->state_slots[op->state_slot_index].name;
                RxCExpr state_expr;
                RxCExpr arg_expr;
                RxCExpr src_value_expr;
                RxCExpr call_expr;
                RxCExpr lhs_expr;
                RxCExpr rhs_expr;
                rx_c_expr_init(&state_expr);
                rx_c_expr_init(&arg_expr);
                rx_c_expr_init(&src_value_expr);
                rx_c_expr_init(&call_expr);
                rx_c_expr_init(&lhs_expr);
                rx_c_expr_init(&rhs_expr);
                bool ok = make_ident_expr(program, &state_expr, state_name);
                if (ok && use_shared_value_view)
                {
                    ok = make_ident_expr(program, &arg_expr, "__rx_value_view");
                }
                else if (ok)
                {
                    ok = make_ident_expr(program, &src_value_expr, "value")
                        && make_cast_expr_raw_type(program, &arg_expr, "void *", &src_value_expr);
                }
                ok = ok
                    && make_call_expr2(program, &call_expr, fn, &state_expr, &arg_expr)
                    && add_expr_stmt_take_ownership(program, loop, &call_expr);
                if (!ok)
                {
                    rx_c_expr_reset(&state_expr);
                    rx_c_expr_reset(&arg_expr);
                    rx_c_expr_reset(&src_value_expr);
                    return false;
                }
                if ((index + 1) < pipeline->op_count
                    && !(make_ident_expr(program, &lhs_expr, "value")
                        && (pipeline->state_slots[op->state_slot_index].value_type == RX_ARG_VOID_PTR
                            ? make_cast_expr_raw_type(program, &rhs_expr, "intptr_t", &state_expr)
                            : make_ident_expr(program, &rhs_expr, state_name))
                        && rx_c_block_add_assign_stmt(program, loop, &lhs_expr, &rhs_expr)))
                {
                    rx_c_expr_reset(&state_expr);
                    rx_c_expr_reset(&arg_expr);
                    rx_c_expr_reset(&src_value_expr);
                    rx_c_expr_reset(&lhs_expr);
                    rx_c_expr_reset(&rhs_expr);
                    return false;
                }
                rx_c_expr_reset(&state_expr);
                rx_c_expr_reset(&arg_expr);
                rx_c_expr_reset(&src_value_expr);
                rx_c_expr_reset(&lhs_expr);
                rx_c_expr_reset(&rhs_expr);
                break;
            }
            case RX_OP_APPLY_FIRST:
                if (!add_raw_exprf(program, loop, "should_break = true"))
                {
                    return false;
                }
                break;
            case RX_OP_APPLY_LAST:
                if (!add_raw_exprf(program, loop, "%s = value", pipeline->state_slots[op->state_slot_index].name)
                    || !add_raw_exprf(program, loop, "%s = true", pipeline->state_slots[op->aux_state_slot_index].name))
                {
                    return false;
                }
                break;
            default:
                return false;
        }

        if (!add_raw_exprf(program, loop, "RX_PROFILE_STAGE_END(%d)", index))
        {
            return false;
        }
    }

    if (has_breaking)
    {
        RxCBlock *then_block = rx_c_stmt_set_if(program, loop, "should_break");
        if (then_block == NULL || !rx_c_block_add_break(program, then_block))
        {
            return false;
        }
    }

    const RxPlannerIrOp *last_op = pipeline->op_count > 0 ? &pipeline->ops[pipeline->op_count - 1] : NULL;
    if (last_op != NULL && last_op->kind == RX_OP_APPLY_LAST)
    {
        return add_raw_exprf(program, body, "return %s ? %s : 0", pipeline->state_slots[last_op->aux_state_slot_index].name, pipeline->state_slots[last_op->state_slot_index].name);
    }
    if (last_op != NULL && (last_op->kind == RX_OP_CALL_REDUCE || last_op->kind == RX_OP_CALL_REDUCE_MUT))
    {
        return add_raw_exprf(
            program,
            body,
            pipeline->state_slots[last_op->state_slot_index].value_type == RX_ARG_VOID_PTR ? "return (intptr_t)%s" : "return %s",
            pipeline->state_slots[last_op->state_slot_index].name);
    }
    return rx_c_block_add_return(program, body, "0");
}

static bool extract_function_body_text(RxStringBuilder *function_text, RxStringBuilder *body_text)
{
    if (function_text == NULL || function_text->data == NULL || body_text == NULL)
    {
        return false;
    }
    const char *open = strchr(function_text->data, '{');
    const char *close = strrchr(function_text->data, '}');
    if (open == NULL || close == NULL || close <= open)
    {
        return false;
    }
    ++open;
    while (*open == '\n' || *open == '\r')
    {
        ++open;
    }
    while (close > open && (close[-1] == '\n' || close[-1] == '\r'))
    {
        --close;
    }
    size_t length = (size_t)(close - open);
    char *copy = malloc(length + 1);
    if (copy == NULL)
    {
        return false;
    }
    memcpy(copy, open, length);
    copy[length] = '\0';
    bool ok = rx_string_builder_append(body_text, copy);
    free(copy);
    return ok;
}

bool rx_build_c_program(const RxPlannerIrProgram *program, const RxCCodegenOptions *options, RxCProgram *out_program, RxDiagnosticBag *diagnostics)
{
    (void)diagnostics;
    if (program == NULL || program->pipeline_count <= 0)
    {
        return true;
    }

    const RxPlannerIrPipeline *pipeline = &program->pipelines[0];
    RxStringBuilder chunk;
    rx_string_builder_init(&chunk);
    if (!rx_c_program_add_define(out_program, "_POSIX_C_SOURCE", "200809L")
        || !rx_c_program_add_include_system(out_program, "inttypes.h")
        || !rx_c_program_add_include_system(out_program, "stdbool.h")
        || !rx_c_program_add_include_system(out_program, "stdint.h")
        || !rx_c_program_add_include_system(out_program, "stdio.h")
        || !rx_c_program_add_include_system(out_program, "stdlib.h")
        || !rx_c_program_add_include_system(out_program, "time.h"))
    {
        return false;
    }
    if (options != NULL && options->header_path != NULL)
    {
        if (!rx_c_program_add_include_local(out_program, options->header_path))
        {
            return false;
        }
    }
    else if (!emit_function_declarations(pipeline, options, &chunk))
    {
        rx_string_builder_reset(&chunk);
        return false;
    }
    if (!add_builder_chunk(out_program, &chunk))
    {
        return false;
    }
    if (!emit_profile_decls(pipeline, &chunk))
    {
        rx_string_builder_reset(&chunk);
        return false;
    }
    if (!add_builder_chunk(out_program, &chunk))
    {
        return false;
    }
    if (!emit_profile_dump_function(pipeline, &chunk))
    {
        rx_string_builder_reset(&chunk);
        return false;
    }
    if (!add_builder_chunk(out_program, &chunk))
    {
        return false;
    }
    if (!emit_storage_helpers(pipeline, &chunk))
    {
        rx_string_builder_reset(&chunk);
        return false;
    }
    if (!add_builder_chunk(out_program, &chunk))
    {
        return false;
    }
    {
        const char *function_name = pipeline->pipeline_name != NULL ? pipeline->pipeline_name : "pipeline";
        char run_name[256];
        snprintf(run_name, sizeof(run_name), "run_%s", function_name);
        RxCBlock body;
        rx_c_block_init(&body);
        const char *param_types[2];
        const char *param_names[2];
        int param_count = 0;
        if (pipeline_uses_external_buffer(pipeline))
        {
            param_types[param_count] = "void **";
            param_names[param_count] = "records";
            param_count += 1;
        }
        param_types[param_count] = "intptr_t";
        param_names[param_count] = "N";
        param_count += 1;
        bool ok = build_segment_ast_body(out_program, pipeline, options, &body);
        if (ok)
        {
            RxCParam params[4];
            memset(params, 0, sizeof(params));
            for (int index = 0; index < param_count; ++index)
            {
                rx_c_type_init(&params[index].type);
                ok = rx_c_type_set_raw(out_program, &params[index].type, param_types[index]);
                params[index].name_id = rx_c_string_pool_intern(&out_program->strings, param_names[index]);
                if (!ok || params[index].name_id == UINT32_MAX)
                {
                    ok = false;
                    break;
                }
            }
            if (ok)
            {
                ok = rx_c_program_add_function(out_program, "intptr_t", run_name, params, param_count, &body);
            }
            for (int index = 0; index < param_count; ++index)
            {
                rx_c_type_reset(&params[index].type);
            }
        }
        rx_c_block_reset(&body);
        if (!ok)
        {
            if (!rx_emit_c_segment_function(pipeline, options, &chunk, diagnostics))
            {
                rx_string_builder_reset(&chunk);
                return false;
            }
            RxStringBuilder body_text;
            rx_string_builder_init(&body_text);
            bool fallback_ok = extract_function_body_text(&chunk, &body_text)
                && add_builder_function(out_program, "intptr_t", run_name, param_types, param_names, param_count, &body_text);
            rx_string_builder_reset(&body_text);
            rx_string_builder_reset(&chunk);
            rx_string_builder_init(&chunk);
            if (!fallback_ok)
            {
                return false;
            }
        }
    }

    if (options == NULL || options->emit_main)
    {
        if (pipeline_uses_external_buffer(pipeline))
        {
            if (!rx_string_builder_append_c_main_external_buffer_stub(&chunk))
            {
                rx_string_builder_reset(&chunk);
                return false;
            }
            bool ok = add_builder_chunk(out_program, &chunk);
            rx_string_builder_reset(&chunk);
            return ok;
        }
        if (!rx_string_builder_append_c_main_benchmark_prefix(&chunk))
        {
            rx_string_builder_reset(&chunk);
            return false;
        }
        if (!rx_string_builder_append_c_main_run_call(&chunk, pipeline->pipeline_name != NULL ? pipeline->pipeline_name : "pipeline"))
        {
            rx_string_builder_reset(&chunk);
            return false;
        }
        if (!rx_string_builder_append_c_main_benchmark_suffix(&chunk))
        {
            rx_string_builder_reset(&chunk);
            return false;
        }
        {
            bool ok = add_builder_chunk(out_program, &chunk);
            if (!ok)
            {
                return false;
            }
        }
    }
    rx_string_builder_reset(&chunk);
    return true;
}

bool rx_emit_c_program(const RxPlannerIrProgram *program, const RxCCodegenOptions *options, RxStringBuilder *out, RxDiagnosticBag *diagnostics)
{
    RxCProgram c_program;
    rx_c_program_init(&c_program);
    bool ok = rx_build_c_program(program, options, &c_program, diagnostics)
        && rx_c_render_program(&c_program, out);
    rx_c_program_reset(&c_program);
    return ok;
}
