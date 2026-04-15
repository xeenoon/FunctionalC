#include "dsl_codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "operator_registry.h"

static void emit_expr(FILE *out, const Expr *expr, const char *value_name, const char *accum_name)
{
    if (expr->kind == EXPR_IDENT)
    {
        if (value_name != NULL && strcmp(expr->text, "x") == 0)
        {
            fprintf(out, "%s", value_name);
            return;
        }
        if (accum_name != NULL && strcmp(expr->text, "accum") == 0)
        {
            fprintf(out, "%s", accum_name);
            return;
        }
        if (value_name != NULL && strcmp(expr->text, "next") == 0)
        {
            fprintf(out, "%s", value_name);
            return;
        }
        fprintf(out, "%s", expr->text);
        return;
    }

    if (expr->kind == EXPR_NUMBER)
    {
        fprintf(out, "%s", expr->text);
        return;
    }

    if (strcmp(expr->text, "identity") == 0 && expr->arg_count == 1)
    {
        emit_expr(out, expr->args[0], value_name, accum_name);
        return;
    }
    if (strcmp(expr->text, "plus") == 0 && expr->arg_count == 2)
    {
        fprintf(out, "((intptr_t)(");
        emit_expr(out, expr->args[0], value_name, accum_name);
        fprintf(out, ") + (intptr_t)(");
        emit_expr(out, expr->args[1], value_name, accum_name);
        fprintf(out, "))");
        return;
    }
    if (strcmp(expr->text, "minus") == 0 && expr->arg_count == 2)
    {
        fprintf(out, "((intptr_t)(");
        emit_expr(out, expr->args[0], value_name, accum_name);
        fprintf(out, ") - (intptr_t)(");
        emit_expr(out, expr->args[1], value_name, accum_name);
        fprintf(out, "))");
        return;
    }
    if (strcmp(expr->text, "mul") == 0 && expr->arg_count == 2)
    {
        fprintf(out, "((intptr_t)(");
        emit_expr(out, expr->args[0], value_name, accum_name);
        fprintf(out, ") * (intptr_t)(");
        emit_expr(out, expr->args[1], value_name, accum_name);
        fprintf(out, "))");
        return;
    }
    if (strcmp(expr->text, "div") == 0 && expr->arg_count == 2)
    {
        fprintf(out, "((intptr_t)(");
        emit_expr(out, expr->args[0], value_name, accum_name);
        fprintf(out, ") / (intptr_t)(");
        emit_expr(out, expr->args[1], value_name, accum_name);
        fprintf(out, "))");
        return;
    }
    if (strcmp(expr->text, "mod") == 0 && expr->arg_count == 2)
    {
        fprintf(out, "((intptr_t)(");
        emit_expr(out, expr->args[0], value_name, accum_name);
        fprintf(out, ") %% (intptr_t)(");
        emit_expr(out, expr->args[1], value_name, accum_name);
        fprintf(out, "))");
        return;
    }
    if (strcmp(expr->text, "eq") == 0 && expr->arg_count == 2)
    {
        fprintf(out, "((intptr_t)(");
        emit_expr(out, expr->args[0], value_name, accum_name);
        fprintf(out, ") == (intptr_t)(");
        emit_expr(out, expr->args[1], value_name, accum_name);
        fprintf(out, "))");
        return;
    }
    if (strcmp(expr->text, "neq") == 0 && expr->arg_count == 2)
    {
        fprintf(out, "((intptr_t)(");
        emit_expr(out, expr->args[0], value_name, accum_name);
        fprintf(out, ") != (intptr_t)(");
        emit_expr(out, expr->args[1], value_name, accum_name);
        fprintf(out, "))");
        return;
    }
    if (strcmp(expr->text, "lt") == 0 && expr->arg_count == 2)
    {
        fprintf(out, "((intptr_t)(");
        emit_expr(out, expr->args[0], value_name, accum_name);
        fprintf(out, ") < (intptr_t)(");
        emit_expr(out, expr->args[1], value_name, accum_name);
        fprintf(out, "))");
        return;
    }
    if (strcmp(expr->text, "lte") == 0 && expr->arg_count == 2)
    {
        fprintf(out, "((intptr_t)(");
        emit_expr(out, expr->args[0], value_name, accum_name);
        fprintf(out, ") <= (intptr_t)(");
        emit_expr(out, expr->args[1], value_name, accum_name);
        fprintf(out, "))");
        return;
    }
    if (strcmp(expr->text, "gt") == 0 && expr->arg_count == 2)
    {
        fprintf(out, "((intptr_t)(");
        emit_expr(out, expr->args[0], value_name, accum_name);
        fprintf(out, ") > (intptr_t)(");
        emit_expr(out, expr->args[1], value_name, accum_name);
        fprintf(out, "))");
        return;
    }
    if (strcmp(expr->text, "gte") == 0 && expr->arg_count == 2)
    {
        fprintf(out, "((intptr_t)(");
        emit_expr(out, expr->args[0], value_name, accum_name);
        fprintf(out, ") >= (intptr_t)(");
        emit_expr(out, expr->args[1], value_name, accum_name);
        fprintf(out, "))");
        return;
    }
    if (strcmp(expr->text, "and") == 0 && expr->arg_count == 2)
    {
        fprintf(out, "(");
        emit_expr(out, expr->args[0], value_name, accum_name);
        fprintf(out, " && ");
        emit_expr(out, expr->args[1], value_name, accum_name);
        fprintf(out, ")");
        return;
    }
    if (strcmp(expr->text, "or") == 0 && expr->arg_count == 2)
    {
        fprintf(out, "(");
        emit_expr(out, expr->args[0], value_name, accum_name);
        fprintf(out, " || ");
        emit_expr(out, expr->args[1], value_name, accum_name);
        fprintf(out, ")");
        return;
    }
    if (strcmp(expr->text, "not") == 0 && expr->arg_count == 1)
    {
        fprintf(out, "(!");
        emit_expr(out, expr->args[0], value_name, accum_name);
        fprintf(out, ")");
        return;
    }

    fprintf(out, "%s(", expr->text);
    for (int i = 0; i < expr->arg_count; ++i)
    {
        if (i > 0)
        {
            fprintf(out, ", ");
        }
        emit_expr(out, expr->args[i], value_name, accum_name);
    }
    fprintf(out, ")");
}

static void emit_function_def(FILE *out, const FunctionInfo *fn)
{
    if (!fn->used)
    {
        return;
    }

    if (fn->kind == FUNCTION_PREDICATE)
    {
        fprintf(out, "static bool %s(void *x)\n{\n    return ", fn->def->name);
        emit_expr(out, fn->def->body, "(intptr_t)x", NULL);
        fprintf(out, ";\n}\n\n");
    }
    else if (fn->kind == FUNCTION_ACCUMULATOR)
    {
        fprintf(out, "static void *%s(void *accum, void *next)\n{\n    return (void *)(intptr_t)(", fn->def->name);
        emit_expr(out, fn->def->body, "(intptr_t)next", "(intptr_t)accum");
        fprintf(out, ");\n}\n\n");
    }
    else
    {
        fprintf(out, "static void *%s(void *x)\n{\n    return (void *)(intptr_t)(", fn->def->name);
        emit_expr(out, fn->def->body, "(intptr_t)x", NULL);
        fprintf(out, ");\n}\n\n");
    }
}

static void emit_value_as_ptr(FILE *out, const char *text)
{
    fprintf(out, "(void *)(intptr_t)(%s)", text);
}

static bool chain_has_operator(const ChainIr *chain, OperatorKind kind)
{
    for (int i = 0; i < chain->op_count; ++i)
    {
        if (chain->ops[i].kind == kind)
        {
            return true;
        }
    }
    return false;
}

static void emit_fused_source_loop(FILE *out, const ChainIr *chain)
{
    if (chain->source.kind == SOURCE_RANGE)
    {
        fprintf(out, "    for (intptr_t __src = (intptr_t)(%s); __src <= (intptr_t)(%s); ++__src)\n    {\n", chain->source.values[0], chain->source.values[1]);
        fprintf(out, "        void *value = (void *)(intptr_t)__src;\n");
        return;
    }

    if (chain->source.kind == SOURCE_OF)
    {
        fprintf(out, "    static const intptr_t __source_vals_%d[] = {", chain->op_count);
        for (int i = 0; i < chain->source.value_count; ++i)
        {
            if (i > 0)
            {
                fprintf(out, ", ");
            }
            fprintf(out, "(intptr_t)(%s)", chain->source.values[i]);
        }
        fprintf(out, "};\n");
        fprintf(out, "    for (size_t __i = 0; __i < sizeof(__source_vals_%d) / sizeof(__source_vals_%d[0]); ++__i)\n    {\n", chain->op_count, chain->op_count);
        fprintf(out, "        void *value = (void *)(intptr_t)__source_vals_%d[__i];\n", chain->op_count);
        return;
    }

    fprintf(out, "    for (int __empty = 0; __empty < 0; ++__empty)\n    {\n");
    fprintf(out, "        void *value = NULL;\n");
}

static void emit_fused_chain(FILE *out, const ChainIr *chain, int index)
{
    bool uses_reduce = chain_has_operator(chain, OP_REDUCE);
    bool uses_scan = chain_has_operator(chain, OP_SCAN) || chain_has_operator(chain, OP_SCANFROM);
    bool uses_last = chain_has_operator(chain, OP_LAST);
    bool uses_distinct_changed = chain_has_operator(chain, OP_DISTINCT_UNTIL_CHANGED);
    bool uses_take = chain_has_operator(chain, OP_TAKE) || chain_has_operator(chain, OP_FIRST);
    bool uses_skip = chain_has_operator(chain, OP_SKIP);
    bool uses_skipwhile = chain_has_operator(chain, OP_SKIP_WHILE);
    bool uses_skipuntil = chain_has_operator(chain, OP_SKIP_UNTIL);

    fprintf(out, "static List *call_%02X_apply(List *data, void *ctx)\n{\n", index);
    fprintf(out, "    (void)data;\n    (void)ctx;\n");
    fprintf(out, "    List *result = init_list();\n");
    if (uses_take)
    {
        fprintf(out, "    intptr_t take_count = 0;\n");
    }
    if (uses_skip)
    {
        fprintf(out, "    intptr_t skip_count = 0;\n");
    }
    if (uses_skipwhile)
    {
        fprintf(out, "    bool skip_while_passed = false;\n");
    }
    if (uses_skipuntil)
    {
        fprintf(out, "    bool skip_until_started = false;\n");
    }
    if (uses_scan)
    {
        fprintf(out, "    void *scan_accum = NULL;\n");
    }
    if (uses_reduce)
    {
        fprintf(out, "    void *reduce_accum = NULL;\n");
    }
    if (uses_last)
    {
        fprintf(out, "    void *last_value = NULL;\n    bool has_last_value = false;\n");
    }
    if (uses_distinct_changed)
    {
        fprintf(out, "    void *last_key = NULL;\n    bool has_last_key = false;\n");
    }

    for (int oi = 0; oi < chain->op_count; ++oi)
    {
        const OperatorAst *op = &chain->ops[oi];
        if (op->kind == OP_SCANFROM)
        {
            fprintf(out, "    scan_accum = ");
            emit_value_as_ptr(out, op->extra);
            fprintf(out, ";\n");
        }
        else if (op->kind == OP_REDUCE && op->has_extra)
        {
            fprintf(out, "    reduce_accum = ");
            emit_value_as_ptr(out, op->extra);
            fprintf(out, ";\n");
        }
    }

    emit_fused_source_loop(out, chain);
    fprintf(out, "        bool emit_value = true;\n");
    fprintf(out, "        bool break_after_emit = false;\n");

    for (int oi = 0; oi < chain->op_count; ++oi)
    {
        const OperatorAst *op = &chain->ops[oi];
        switch (op->kind)
        {
        case OP_MAP:
            fprintf(out, "        value = %s(value);\n", op->symbol);
            break;
        case OP_FILTER:
            fprintf(out, "        if (!%s(value)) { continue; }\n", op->symbol);
            break;
        case OP_MAP_TO:
            fprintf(out, "        value = ");
            emit_value_as_ptr(out, op->extra);
            fprintf(out, ";\n");
            break;
        case OP_TAKE:
            fprintf(out, "        if (take_count >= (intptr_t)(%s)) { break; }\n", op->extra);
            fprintf(out, "        take_count++;\n");
            break;
        case OP_FIRST:
            fprintf(out, "        if (take_count >= 1) { break; }\n");
            fprintf(out, "        take_count++;\n");
            break;
        case OP_SKIP:
            fprintf(out, "        if (skip_count < (intptr_t)(%s)) { skip_count++; continue; }\n", op->extra);
            break;
        case OP_TAKE_WHILE:
            fprintf(out, "        if (!%s(value)) { break; }\n", op->symbol);
            break;
        case OP_SKIP_WHILE:
            fprintf(out, "        if (!skip_while_passed && %s(value)) { continue; }\n", op->symbol);
            fprintf(out, "        skip_while_passed = true;\n");
            break;
        case OP_SCAN:
        case OP_SCANFROM:
            fprintf(out, "        scan_accum = %s(scan_accum, value);\n", op->symbol);
            fprintf(out, "        value = scan_accum;\n");
            break;
        case OP_REDUCE:
            fprintf(out, "        reduce_accum = %s(reduce_accum, value);\n", op->symbol);
            fprintf(out, "        emit_value = false;\n");
            break;
        case OP_LAST:
            fprintf(out, "        last_value = value;\n        has_last_value = true;\n        emit_value = false;\n");
            break;
        case OP_DISTINCT_UNTIL_CHANGED:
            fprintf(out, "        {\n            void *key = %s(value);\n            if (has_last_key && key == last_key) { continue; }\n            last_key = key;\n            has_last_key = true;\n        }\n", op->symbol);
            break;
        case OP_TAKE_UNTIL:
            fprintf(out, "        if (value == ");
            emit_value_as_ptr(out, op->extra);
            fprintf(out, ") { break_after_emit = true; }\n");
            break;
        case OP_SKIP_UNTIL:
            fprintf(out, "        if (!skip_until_started)\n        {\n");
            fprintf(out, "            if (value == ");
            emit_value_as_ptr(out, op->extra);
            fprintf(out, ") { skip_until_started = true; }\n            else { continue; }\n        }\n");
            break;
        default:
            break;
        }
    }

    fprintf(out, "        if (emit_value) { push_back(result, value); }\n");
    fprintf(out, "        if (break_after_emit) { break; }\n");
    fprintf(out, "    }\n");
    if (uses_reduce)
    {
        fprintf(out, "    push_back(result, reduce_accum);\n");
    }
    else if (uses_last)
    {
        fprintf(out, "    if (has_last_value) { push_back(result, last_value); }\n");
    }
    fprintf(out, "    return result;\n}\n\n");

    fprintf(out, "static Query *call_%02X_query(void)\n{\n", index);
    fprintf(out, "    Query *q = malloc(sizeof(*q));\n");
    fprintf(out, "    q->func = call_%02X_apply;\n", index);
    fprintf(out, "    q->ctx = NULL;\n");
    fprintf(out, "    q->kind = QUERY_KIND_GENERIC;\n");
    fprintf(out, "    return q;\n}\n\n");

    fprintf(out, "Observable *call_%02X(Observable *self)\n{\n", index);
    fprintf(out, "    self->emit_handler = call_%02X_query();\n", index);
    fprintf(out, "    self->pipe = NULL;\n");
    fprintf(out, "    return self;\n}\n\n");
}

static void emit_source_expr(FILE *out, const SourceAst *source);

static void emit_runtime_operator_expr(FILE *out, const OperatorAst *op, const char *self_name)
{
    const OperatorInfo *info = find_operator_info(op->kind);
    fprintf(out, "%s(", info->runtime_name);
    if (info->requires_runtime_self)
    {
        fprintf(out, "%s", self_name);
        if (info->argument_kind != ARGUMENT_NONE)
        {
            fprintf(out, ", ");
        }
    }

    switch (info->argument_kind)
    {
    case ARGUMENT_NONE:
        break;
    case ARGUMENT_FUNCTION:
        fprintf(out, "%s", op->symbol);
        break;
    case ARGUMENT_LITERAL:
        if (op->kind == OP_MAP_TO || op->kind == OP_TAKE_UNTIL || op->kind == OP_SKIP_UNTIL)
        {
            emit_value_as_ptr(out, op->extra);
        }
        else
        {
            fprintf(out, "%s", op->extra);
        }
        break;
    case ARGUMENT_FUNCTION_AND_LITERAL:
        fprintf(out, "%s", op->symbol);
        if (op->has_extra && op->kind != OP_REDUCE)
        {
            fprintf(out, ", ");
            if (op->kind == OP_SCANFROM)
            {
                emit_value_as_ptr(out, op->extra);
            }
            else
            {
                fprintf(out, "%s", op->extra);
            }
        }
        break;
    case ARGUMENT_SOURCE:
        emit_source_expr(out, op->source_arg);
        break;
    }
    fprintf(out, ")");
}

static void emit_source_expr(FILE *out, const SourceAst *source)
{
    switch (source->kind)
    {
    case SOURCE_RANGE:
        fprintf(out, "range(%s, %s)", source->values[0], source->values[1]);
        break;
    case SOURCE_OF:
        fprintf(out, "of(%d", source->value_count);
        for (int i = 0; i < source->value_count; ++i)
        {
            fprintf(out, ", ");
            emit_value_as_ptr(out, source->values[i]);
        }
        fprintf(out, ")");
        break;
    case SOURCE_EMPTY:
        fprintf(out, "empty()");
        break;
    case SOURCE_NEVER:
        fprintf(out, "never()");
        break;
    case SOURCE_INTERVAL:
        fprintf(out, "interval(%s)", source->values[0]);
        break;
    case SOURCE_TIMER:
        fprintf(out, "timer(%s, %s)", source->values[0], source->values[1]);
        break;
    case SOURCE_DEFER:
        fprintf(out, "defer(%s)", source->values[0]);
        break;
    case SOURCE_FROM:
        fprintf(out, "from(%s)", source->values[0]);
        break;
    case SOURCE_ZIP:
        fprintf(out, "zip(%d", source->source_count);
        for (int i = 0; i < source->source_count; ++i)
        {
            fprintf(out, ", ");
            emit_source_expr(out, source->sources[i]);
        }
        fprintf(out, ")");
        break;
    }
}

static void emit_runtime_chain(FILE *out, const ChainIr *chain, int index)
{
    fprintf(out, "static Observable *call_%02X(Observable *self)\n{\n", index);
    if (chain->op_count == 0)
    {
        fprintf(out, "    return self;\n}\n\n");
        return;
    }

    fprintf(out, "    self = pipe(self, %d", chain->op_count);
    for (int oi = 0; oi < chain->op_count; ++oi)
    {
        fprintf(out, ", ");
        emit_runtime_operator_expr(out, &chain->ops[oi], "self");
    }
    fprintf(out, ");\n");
    fprintf(out, "    return self;\n}\n\n");
}

bool emit_program_c(const ExecutionPlan *plan, const CodegenOptions *options)
{
    FILE *out = fopen(options->output_file, "wb");
    if (out == NULL)
    {
        return false;
    }

    fprintf(out, "#define _POSIX_C_SOURCE 200809L\n");
    fprintf(out, "#ifndef ENABLE_PROFILER\n#define ENABLE_PROFILER 0\n#endif\n");
    fprintf(out, "#include \"observable.h\"\n#include \"profiler.h\"\n#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <time.h>\n\n");

    for (int i = 0; i < options->define_count; ++i)
    {
        fprintf(out, "static intptr_t %s = (intptr_t)(%s);\n", options->defines[i], options->define_values[i]);
    }
    for (int ci = 0; ci < plan->chain_count; ++ci)
    {
        fprintf(out, "static intptr_t %s = 0;\n", plan->ir.chains[ci].subscriber_target);
        fprintf(out, "static void assign_%s(void *v) { %s = (intptr_t)v; }\n",
                plan->ir.chains[ci].subscriber_target,
                plan->ir.chains[ci].subscriber_target);
    }
    fprintf(out, "\n");

    for (int fi = 0; fi < plan->ir.function_count; ++fi)
    {
        emit_function_def(out, &plan->ir.functions[fi]);
    }

    for (int ci = 0; ci < plan->chain_count; ++ci)
    {
        if (plan->chains[ci].backend == BACKEND_FUSED)
        {
            emit_fused_chain(out, &plan->ir.chains[ci], ci);
        }
        else
        {
            emit_runtime_chain(out, &plan->ir.chains[ci], ci);
        }
    }

    fprintf(out, "int main(void)\n{\n");
    fprintf(out, "    int RUNS = %s;\n    int64_t total_ns = 0;\n    profiler_reset();\n", options->runs_expr);
    fprintf(out, "    for (int run = 0; run < RUNS; ++run)\n    {\n");
    for (int ci = 0; ci < plan->chain_count; ++ci)
    {
        fprintf(out, "        %s = 0;\n", plan->ir.chains[ci].subscriber_target);
    }
    fprintf(out, "        struct timespec start, end;\n");
    fprintf(out, "        clock_gettime(CLOCK_MONOTONIC, &start);\n");
    for (int ci = 0; ci < plan->chain_count; ++ci)
    {
        fprintf(out, "        Observable *o_%d = ", ci);
        emit_source_expr(out, &plan->ir.chains[ci].source);
        fprintf(out, ";\n");
        fprintf(out, "        o_%d->on_subscription = NULL;\n", ci);
        fprintf(out, "        o_%d->complete = false;\n", ci);
        fprintf(out, "        o_%d = call_%02X(o_%d);\n", ci, ci, ci);
        fprintf(out, "        subscribe(o_%d, assign_%s);\n", ci, plan->ir.chains[ci].subscriber_target);
    }
    fprintf(out, "        clock_gettime(CLOCK_MONOTONIC, &end);\n");
    fprintf(out, "        int64_t ns = (int64_t)(end.tv_sec - start.tv_sec) * 1000000000LL + (int64_t)(end.tv_nsec - start.tv_nsec);\n");
    fprintf(out, "        total_ns += ns;\n    }\n");
    fprintf(out, "    printf(\"C   result : %%lld\\n\", (long long)%s);\n", plan->ir.chains[plan->chain_count - 1].subscriber_target);
    fprintf(out, "    printf(\"C   average: %%.2f ms  (%%d runs)\\n\", (double)total_ns / RUNS / 1e6, RUNS);\n");
    fprintf(out, "#if ENABLE_PROFILER\n    profiler_print_report();\n#endif\n");
    fprintf(out, "    return 0;\n}\n");

    fclose(out);
    return true;
}

int compile_generated_program(const CodegenOptions *options)
{
    char command[1024];
    snprintf(command, sizeof(command),
             "gcc -DENABLE_PROFILER=0 -w -g -O0 -I./core/src %s core/src/observable.c core/src/list.c core/src/task.c core/src/stopwatch.c core/src/profiler.c -o %s",
             options->output_file, options->binary_file);
    return system(command);
}

int run_generated_program(const CodegenOptions *options)
{
    char path[260];
    char command[512];
    snprintf(path, sizeof(path), "%s", options->binary_file);
    for (size_t i = 0; path[i] != '\0'; ++i)
    {
        if (path[i] == '/')
        {
            path[i] = '\\';
        }
    }
    snprintf(command, sizeof(command), "cmd /c \"\"%s\"\"", path);
    return system(command);
}
