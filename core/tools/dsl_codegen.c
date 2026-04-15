#include "dsl_codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        fprintf(out, "%s", expr->text);
        return;
    }

    if (expr->kind == EXPR_NUMBER)
    {
        fprintf(out, "%s", expr->text);
        return;
    }

    if (expr->kind == EXPR_CALL)
    {
        const char *name = expr->text;
        if (strcmp(name, "plus") == 0 && expr->arg_count == 2)
        {
            fprintf(out, "((intptr_t)(");
            emit_expr(out, expr->args[0], value_name, accum_name);
            fprintf(out, ") + (intptr_t)(");
            emit_expr(out, expr->args[1], value_name, accum_name);
            fprintf(out, "))");
            return;
        }
        if (strcmp(name, "mul") == 0 && expr->arg_count == 2)
        {
            fprintf(out, "((intptr_t)(");
            emit_expr(out, expr->args[0], value_name, accum_name);
            fprintf(out, ") * (intptr_t)(");
            emit_expr(out, expr->args[1], value_name, accum_name);
            fprintf(out, "))");
            return;
        }
        if (strcmp(name, "mod") == 0 && expr->arg_count == 2)
        {
            fprintf(out, "((intptr_t)(");
            emit_expr(out, expr->args[0], value_name, accum_name);
            fprintf(out, ") %% (intptr_t)(");
            emit_expr(out, expr->args[1], value_name, accum_name);
            fprintf(out, "))");
            return;
        }
        if (strcmp(name, "eq") == 0 && expr->arg_count == 2)
        {
            fprintf(out, "((intptr_t)(");
            emit_expr(out, expr->args[0], value_name, accum_name);
            fprintf(out, ") == (intptr_t)(");
            emit_expr(out, expr->args[1], value_name, accum_name);
            fprintf(out, "))");
            return;
        }

        fprintf(out, "%s(", name);
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
}

static void emit_function_def(FILE *out, const FnDef *fn, const Chain *chains, int chain_count)
{
    bool used_as_filter = false;
    bool used_as_reduce = false;
    for (int ci = 0; ci < chain_count; ++ci)
    {
        for (int oi = 0; oi < chains[ci].op_count; ++oi)
        {
            const Operator *op = &chains[ci].ops[oi];
            if (strcmp(op->function_name, fn->name) != 0)
            {
                continue;
            }
            if (op->kind == OP_FILTER)
            {
                used_as_filter = true;
            }
            else if (op->kind == OP_REDUCE)
            {
                used_as_reduce = true;
            }
        }
    }

    if (used_as_filter)
    {
        fprintf(out, "static bool %s(void *x)\n{\n    return ", fn->name);
        emit_expr(out, fn->body, "(intptr_t)x", NULL);
        fprintf(out, ";\n}\n\n");
    }
    else if (used_as_reduce)
    {
        fprintf(out, "static void *%s(void *accum, void *next)\n{\n    return (void *)(intptr_t)(", fn->name);
        emit_expr(out, fn->body, "(intptr_t)next", "(intptr_t)accum");
        fprintf(out, ");\n}\n\n");
    }
    else
    {
        fprintf(out, "static void *%s(void *x)\n{\n    return (void *)(intptr_t)(", fn->name);
        emit_expr(out, fn->body, "(intptr_t)x", NULL);
        fprintf(out, ");\n}\n\n");
    }
}

bool emit_program_c(const Program *program, const CodegenOptions *options)
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
    for (int ci = 0; ci < program->chain_count; ++ci)
    {
        fprintf(out, "static intptr_t %s = 0;\n", program->chains[ci].subscriber_target);
        fprintf(out, "static void assign_%s(void *v) { %s = (intptr_t)v; }\n",
                program->chains[ci].subscriber_target,
                program->chains[ci].subscriber_target);
    }
    fprintf(out, "\n");

    for (int fi = 0; fi < program->function_count; ++fi)
    {
        emit_function_def(out, &program->functions[fi], program->chains, program->chain_count);
    }

    for (int ci = 0; ci < program->chain_count; ++ci)
    {
        const Chain *chain = &program->chains[ci];
        fprintf(out, "static List *call_%02X_apply(List *data, void *ctx)\n{\n", ci);
        fprintf(out, "    (void)ctx;\n");
        fprintf(out, "    List *result = init_list_with_capacity(data->size > 0 ? data->size : 1);\n");
        fprintf(out, "    intptr_t taken = 0;\n");
        fprintf(out, "    void *accum = NULL;\n");
        for (int oi = 0; oi < chain->op_count; ++oi)
        {
            if (chain->ops[oi].kind == OP_REDUCE)
            {
                fprintf(out, "    accum = (void *)(intptr_t)(%s);\n", chain->ops[oi].literal);
                break;
            }
        }
        fprintf(out, "    for (int i = 0; i < data->size; ++i)\n    {\n");
        fprintf(out, "        void *value = list_get(data, i);\n");
        fprintf(out, "        if (value == (void *)(long)0XDEADBEEF) { break; }\n");
        for (int oi = 0; oi < chain->op_count; ++oi)
        {
            const Operator *op = &chain->ops[oi];
            if (op->kind == OP_MAP)
            {
                fprintf(out, "        value = %s(value);\n", op->function_name);
            }
            else if (op->kind == OP_FILTER)
            {
                fprintf(out, "        if (!%s(value)) { continue; }\n", op->function_name);
            }
            else if (op->kind == OP_TAKE)
            {
                fprintf(out, "        if (taken >= (intptr_t)(%s)) { break; }\n", op->literal);
                fprintf(out, "        taken++;\n");
            }
            else if (op->kind == OP_REDUCE)
            {
                fprintf(out, "        accum = %s(accum, value);\n", op->function_name);
            }
        }
        bool has_reduce = false;
        for (int oi = 0; oi < chain->op_count; ++oi)
        {
            if (chain->ops[oi].kind == OP_REDUCE)
            {
                has_reduce = true;
                break;
            }
        }
        if (!has_reduce)
        {
            fprintf(out, "        push_back(result, value);\n");
        }
        fprintf(out, "    }\n");
        if (has_reduce)
        {
            fprintf(out, "    push_back(result, accum);\n");
        }
        fprintf(out, "    return result;\n}\n\n");

        fprintf(out, "static Query *call_%02X_query(void)\n{\n", ci);
        fprintf(out, "    Query *q = malloc(sizeof(*q));\n");
        fprintf(out, "    q->func = call_%02X_apply;\n", ci);
        fprintf(out, "    q->ctx = NULL;\n");
        fprintf(out, "    q->kind = QUERY_KIND_GENERIC;\n");
        fprintf(out, "    return q;\n}\n\n");

        fprintf(out, "Observable *call_%02X(Observable *self)\n{\n", ci);
        fprintf(out, "    self->emit_handler = call_%02X_query();\n", ci);
        fprintf(out, "    self->pipe = NULL;\n");
        fprintf(out, "    return self;\n}\n\n");
    }

    fprintf(out, "int main(void)\n{\n");
    fprintf(out, "    int RUNS = %s;\n    int64_t total_ns = 0;\n    profiler_reset();\n", options->runs_expr);
    fprintf(out, "    for (int run = 0; run < RUNS; ++run)\n    {\n");
    for (int ci = 0; ci < program->chain_count; ++ci)
    {
        fprintf(out, "        %s = 0;\n", program->chains[ci].subscriber_target);
    }
    fprintf(out, "        struct timespec start, end;\n");
    fprintf(out, "        clock_gettime(CLOCK_MONOTONIC, &start);\n");
    for (int ci = 0; ci < program->chain_count; ++ci)
    {
        fprintf(out, "        Observable *o_%d = range(%s, %s);\n", ci, program->chains[ci].range_min, program->chains[ci].range_max);
        fprintf(out, "        o_%d->complete = false;\n", ci);
        fprintf(out, "        o_%d = call_%02X(o_%d);\n", ci, ci, ci);
        fprintf(out, "        subscribe(o_%d, assign_%s);\n", ci, program->chains[ci].subscriber_target);
    }
    fprintf(out, "        clock_gettime(CLOCK_MONOTONIC, &end);\n");
    fprintf(out, "        int64_t ns = (int64_t)(end.tv_sec - start.tv_sec) * 1000000000LL + (int64_t)(end.tv_nsec - start.tv_nsec);\n");
    fprintf(out, "        total_ns += ns;\n    }\n");
    fprintf(out, "    printf(\"C   result : %%lld\\n\", (long long)%s);\n", program->chains[program->chain_count - 1].subscriber_target);
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
             "gcc -DENABLE_PROFILER=0 -Wall -Wextra -g -O0 -I./core/src %s core/src/observable.c core/src/list.c core/src/task.c core/src/stopwatch.c core/src/profiler.c -o %s",
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
