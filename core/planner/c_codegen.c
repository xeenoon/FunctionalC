#include "c_codegen.h"
#include "graph_opt.h"

#include <inttypes.h>
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

    const char *body_start = strchr(params_end, '{');
    if (body_start == NULL)
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

static bool emit_inline_call_block(
    RxStringBuilder *out,
    const RxInlineFunction *function,
    const char *const *arguments,
    int argument_count,
    const char *result_name,
    const char *done_label)
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

static bool emit_function_declarations(const RxLoweredPipeline *pipeline, const RxCCodegenOptions *options, RxStringBuilder *out)
{
    for (int index = 0; index < pipeline->op_count; ++index)
    {
        const RxLoopOp *op = &pipeline->ops[index];
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
                if (!rx_string_builder_append_format(out, "extern void *%s(void *left_raw, void *right_raw);\n", name)) return false;
                break;
            case RX_OP_CALL_TRIPLE_MAP:
                if (!rx_string_builder_append_format(out, "extern void *%s(void *zipped_left_raw, void *zipped_right_raw, void *right_raw);\n", name)) return false;
                break;
            case RX_OP_CALL_MAP:
            case RX_OP_CALL_MAP_CHAIN:
            case RX_OP_CALL_MAP_TO:
            case RX_OP_APPLY_DISTINCT:
            case RX_OP_APPLY_DISTINCT_UNTIL_CHANGED:
                if (!rx_string_builder_append_format(out, "extern void *%s(void *raw);\n", name)) return false;
                break;
            case RX_OP_CALL_FILTER:
            case RX_OP_APPLY_TAKE_WHILE:
            case RX_OP_APPLY_SKIP_WHILE:
                if (!rx_string_builder_append_format(out, "extern bool %s(void *raw);\n", name)) return false;
                break;
            case RX_OP_CALL_SCAN:
            case RX_OP_CALL_REDUCE:
                if (!rx_string_builder_append_format(out, "extern void *%s(void *raw_accum, void *raw_next);\n", name)) return false;
                break;
            default:
                break;
        }
    }
    return true;
}

static const char *op_label(const RxLoopOp *op)
{
    switch (op->kind)
    {
        case RX_OP_CALL_PAIR_MAP: return "pairMap";
        case RX_OP_CALL_TRIPLE_MAP: return "tripleMap";
        case RX_OP_CALL_MAP: return "map";
        case RX_OP_CALL_FILTER: return "filter";
        case RX_OP_CALL_SCAN: return "scan";
        case RX_OP_CALL_REDUCE: return "reduce";
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

static bool emit_profile_decls(const RxLoweredPipeline *pipeline, RxStringBuilder *out)
{
    if (!rx_string_builder_append(out,
            "#ifdef RX_PLANNER_PROFILE\n"
            "typedef struct {\n"
            "    const char *name;\n"
            "    uint64_t hits;\n"
            "    uint64_t total_ns;\n"
            "} RxProfileSlot;\n\n"))
    {
        return false;
    }

    if (!rx_string_builder_append_format(out, "static RxProfileSlot rx_profile_slots_%s[] = {\n", pipeline->pipeline_name != NULL ? pipeline->pipeline_name : "pipeline"))
    {
        return false;
    }
    for (int index = 0; index < pipeline->op_count; ++index)
    {
        if (!rx_string_builder_append_format(out, "    {\"%s[%d]\", 0, 0},\n", op_label(&pipeline->ops[index]), index))
        {
            return false;
        }
    }
    if (!rx_string_builder_append(out,
            "};\n"
            "static uint64_t rx_profile_diff_ns(struct timespec start, struct timespec end) {\n"
            "    return (uint64_t)(end.tv_sec - start.tv_sec) * 1000000000ULL + (uint64_t)(end.tv_nsec - start.tv_nsec);\n"
            "}\n"
            "#define RX_PROFILE_STAGE_BEGIN(ID) struct timespec __rx_stage_start_##ID, __rx_stage_end_##ID; clock_gettime(CLOCK_MONOTONIC, &__rx_stage_start_##ID)\n"
            "#define RX_PROFILE_STAGE_END(ID) do { clock_gettime(CLOCK_MONOTONIC, &__rx_stage_end_##ID); rx_profile_slots_"))
    {
        return false;
    }
    if (!rx_string_builder_append_format(out, "%s", pipeline->pipeline_name != NULL ? pipeline->pipeline_name : "pipeline")
        || !rx_string_builder_append(out, "[ID].hits += 1; rx_profile_slots_")
        || !rx_string_builder_append_format(out, "%s", pipeline->pipeline_name != NULL ? pipeline->pipeline_name : "pipeline")
        || !rx_string_builder_append(out,
               "[ID].total_ns += rx_profile_diff_ns(__rx_stage_start_##ID, __rx_stage_end_##ID); } while (0)\n"
               "#else\n"
               "#define RX_PROFILE_STAGE_BEGIN(ID) do { } while (0)\n"
               "#define RX_PROFILE_STAGE_END(ID) do { } while (0)\n"
               "#endif\n\n"))
    {
        return false;
    }
    return true;
}

static bool emit_state_slot(RxStringBuilder *out, const RxStateSlot *slot)
{
    switch (slot->kind)
    {
        case RX_STATE_SKIP_WHILE_PASSED:
        case RX_STATE_HAS_LAST_VALUE:
        case RX_STATE_HAS_LAST_KEY:
            return rx_string_builder_append_format(out, "    bool %s = %s;\n", slot->name, slot->initial_value.kind == RX_LITERAL_INT && slot->initial_value.as.int_value != 0 ? "true" : "false");
        default:
            if (slot->initial_value.kind == RX_LITERAL_INT)
            {
                return rx_string_builder_append_format(out, "    intptr_t %s = %" PRIdPTR ";\n", slot->name, (intptr_t)slot->initial_value.as.int_value);
            }
            if (slot->initial_value.kind == RX_LITERAL_LONG)
            {
                return rx_string_builder_append_format(out, "    intptr_t %s = %" PRIdPTR ";\n", slot->name, (intptr_t)slot->initial_value.as.long_value);
            }
            return rx_string_builder_append_format(out, "    intptr_t %s = 0;\n", slot->name);
    }
}

static bool emit_loop_body(const RxLoweredPipeline *pipeline, const RxCCodegenOptions *options, RxStringBuilder *out)
{
    unsigned inline_counter = 0;
    if (pipeline->source_kind == RX_LOOP_SOURCE_ZIP_RANGE)
    {
        if (!rx_string_builder_append(out, "    for (intptr_t src = 1; src <= N; ++src) {\n")
            || !rx_string_builder_append(out, "        intptr_t left = src;\n")
            || !rx_string_builder_append(out, "        intptr_t right = src;\n")
            || !rx_string_builder_append(out, "        intptr_t value = 0;\n"))
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
    else if (!rx_string_builder_append(out, "    for (intptr_t src = 1; src <= N; ++src) {\n")
             || !rx_string_builder_append(out, "        intptr_t value = src;\n"))
    {
        return false;
    }

    for (int index = 0; index < pipeline->op_count; ++index)
    {
        const RxLoopOp *op = &pipeline->ops[index];
        const RxPlannedStage *stage = op->stage;
        const char *fn = stage != NULL && stage->primary_argument.kind == RX_BINDING_FUNCTION_NAME ? stage->primary_argument.as.function_name : NULL;
        RxInlineFunction function;
        bool can_inline = fn != NULL && try_extract_inline_function(options != NULL ? options->helper_source_text : NULL, fn, &function);
        char result_name[32];
        char done_label[32];
        if (can_inline)
        {
            snprintf(result_name, sizeof(result_name), "__rx_result_%X", inline_counter);
            snprintf(done_label, sizeof(done_label), "__rx_done_%X", inline_counter);
            inline_counter += 1;
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
                    if (!rx_string_builder_append_format(out, "        %s %s = 0;\n", function.return_type, result_name)
                        || !emit_inline_call_block(out, &function, args, 2, result_name, done_label)
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
                    if (!rx_string_builder_append_format(out, "        %s %s = 0;\n", function.return_type, result_name)
                        || !emit_inline_call_block(out, &function, args, 3, result_name, done_label)
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
                    if (!rx_string_builder_append_format(out, "        %s %s = 0;\n", function.return_type, result_name)
                        || !emit_inline_call_block(out, &function, args, 1, result_name, done_label)
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
            case RX_OP_CALL_SCAN:
            {
                char arg0[96];
                snprintf(arg0, sizeof(arg0), "(void *)(intptr_t)%s", pipeline->state_slots[op->state_slot_index].name);
                if (can_inline)
                {
                    const char *args[] = { arg0, "(void *)(intptr_t)value" };
                    if (!rx_string_builder_append_format(out, "        %s %s = 0;\n", function.return_type, result_name)
                        || !emit_inline_call_block(out, &function, args, 2, result_name, done_label)
                        || !rx_string_builder_append_format(out, "        %s = (intptr_t)%s;\n", pipeline->state_slots[op->state_slot_index].name, result_name))
                    {
                        free_inline_function(&function);
                        return false;
                    }
                }
                else if (!rx_string_builder_append_format(out, "        %s = (intptr_t)%s((void *)(intptr_t)%s, (void *)(intptr_t)value);\n", pipeline->state_slots[op->state_slot_index].name, fn, pipeline->state_slots[op->state_slot_index].name))
                {
                    return false;
                }
                if (!rx_string_builder_append_format(out, "        value = %s;\n", pipeline->state_slots[op->state_slot_index].name))
                {
                    if (can_inline) free_inline_function(&function);
                    return false;
                }
                break;
            }
            case RX_OP_APPLY_DISTINCT_UNTIL_CHANGED:
            {
                if (can_inline)
                {
                    const char *args[] = { "(void *)(intptr_t)value" };
                    if (!rx_string_builder_append_format(out, "        %s %s = 0;\n", function.return_type, result_name)
                        || !emit_inline_call_block(out, &function, args, 1, result_name, done_label)
                        || !rx_string_builder_append_format(out, "        intptr_t key_%d = (intptr_t)%s;\n", index, result_name))
                    {
                        free_inline_function(&function);
                        return false;
                    }
                }
                else if (!rx_string_builder_append_format(out, "        intptr_t key_%d = (intptr_t)%s((void *)(intptr_t)value);\n", index, fn))
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
                    const char *args[] = { "(void *)(intptr_t)value" };
                    if (!rx_string_builder_append_format(out, "        %s %s = false;\n", function.return_type, result_name)
                        || !emit_inline_call_block(out, &function, args, 1, result_name, done_label)
                        || !rx_string_builder_append_format(out, "        if (!%s && %s) { continue; }\n", pipeline->state_slots[op->state_slot_index].name, result_name))
                    {
                        free_inline_function(&function);
                        return false;
                    }
                }
                else if (!rx_string_builder_append_format(out, "        if (!%s && %s((void *)(intptr_t)value)) { continue; }\n", pipeline->state_slots[op->state_slot_index].name, fn))
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

    if (pipeline->source_kind == RX_LOOP_SOURCE_ZIP_MERGE_MAP_RANGE)
    {
        return rx_string_builder_append(out, "        }\n    }\n");
    }
    return rx_string_builder_append(out, "    }\n");
}

bool rx_emit_c_segment_function(const RxLoweredPipeline *pipeline, const RxCCodegenOptions *options, RxStringBuilder *out, RxDiagnosticBag *diagnostics)
{
    (void)diagnostics;
    if (!rx_string_builder_append_format(out, "static intptr_t run_%s(intptr_t N) {\n", pipeline->pipeline_name != NULL ? pipeline->pipeline_name : "pipeline"))
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

    const RxLoopOp *last_op = pipeline->op_count > 0 ? &pipeline->ops[pipeline->op_count - 1] : NULL;
    if (last_op != NULL && last_op->kind == RX_OP_APPLY_LAST)
    {
        if (!rx_string_builder_append_format(out, "    return %s ? %s : 0;\n", pipeline->state_slots[last_op->aux_state_slot_index].name, pipeline->state_slots[last_op->state_slot_index].name))
        {
            return false;
        }
    }
    else if (!rx_string_builder_append(out, "    return 0;\n"))
    {
        return false;
    }
    return rx_string_builder_append(out, "}\n\n");
}

bool rx_emit_c_program(const RxLoweredProgram *program, const RxCCodegenOptions *options, RxStringBuilder *out, RxDiagnosticBag *diagnostics)
{
    (void)diagnostics;
    if (program == NULL || program->pipeline_count <= 0)
    {
        return true;
    }

    const RxLoweredPipeline *pipeline = &program->pipelines[0];
    if (!rx_string_builder_append(out, "#define _POSIX_C_SOURCE 200809L\n")
        || !rx_string_builder_append(out, "#include <inttypes.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <time.h>\n\n"))
    {
        return false;
    }
    if (options != NULL && options->header_path != NULL)
    {
        if (!rx_string_builder_append_format(out, "#include \"%s\"\n\n", options->header_path))
        {
            return false;
        }
    }
    else if (!emit_function_declarations(pipeline, options, out))
    {
        return false;
    }
    if (!emit_profile_decls(pipeline, out))
    {
        return false;
    }
    if (!rx_emit_c_segment_function(pipeline, options, out, diagnostics))
    {
        return false;
    }

    if (options == NULL || options->emit_main)
    {
        if (!rx_string_builder_append(
                out,
                "int main(int argc, char **argv) {\n"
                "    intptr_t N = argc > 1 ? (intptr_t)strtoll(argv[1], NULL, 10) : 0;\n"
                "    int RUNS = argc > 2 ? atoi(argv[2]) : 1;\n"
                "    int64_t total_ns = 0;\n"
                "    intptr_t result = 0;\n"
                "    for (int run = 0; run < RUNS; ++run) {\n"
                "        struct timespec start, end;\n"
                "        clock_gettime(CLOCK_MONOTONIC, &start);\n"))
        {
            return false;
        }
        if (!rx_string_builder_append_format(out, "        result = run_%s(N);\n", pipeline->pipeline_name != NULL ? pipeline->pipeline_name : "pipeline"))
        {
            return false;
        }
        if (!rx_string_builder_append(
                out,
                "        clock_gettime(CLOCK_MONOTONIC, &end);\n"
                "        total_ns += (int64_t)(end.tv_sec - start.tv_sec) * 1000000000LL + (int64_t)(end.tv_nsec - start.tv_nsec);\n"
                "    }\n"
                "    printf(\"{\\\"result\\\": %" PRIdPTR ", \\\"average_ms\\\": %.5f, \\\"runs\\\": %d, \\\"n\\\": %" PRIdPTR "}\\n\", result, (double)total_ns / RUNS / 1e6, RUNS, N);\n"
                "    return 0;\n"
                "}\n"))
        {
            return false;
        }
    }

    return true;
}
