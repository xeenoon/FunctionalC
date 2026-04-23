#include "lowering.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "operator_registry.h"

static int find_function_index(const ProgramIr *ir, const char *name)
{
    for (int i = 0; i < ir->function_count; ++i)
    {
        if (strcmp(ir->functions[i].def->name, name) == 0)
        {
            return i;
        }
    }
    return -1;
}

static bool mark_function_usage(ProgramIr *ir, const char *name, FunctionKind expected)
{
    int index = find_function_index(ir, name);
    if (index < 0)
    {
        fprintf(stderr, "lowering error: unknown function '%s'\n", name);
        return false;
    }

    FunctionInfo *info = &ir->functions[index];
    info->used = true;
    if (info->kind == FUNCTION_UNKNOWN)
    {
        info->kind = expected;
        return true;
    }

    if (info->kind != expected)
    {
        fprintf(stderr, "lowering error: function '%s' used with conflicting kinds\n", name);
        return false;
    }
    return true;
}

static bool validate_source(const SourceAst *source)
{
    switch (source->kind)
    {
    case SOURCE_RANGE:
        return source->value_count == 2;
    case SOURCE_OF:
        return source->value_count >= 1;
    case SOURCE_EMPTY:
    case SOURCE_NEVER:
        return true;
    case SOURCE_INTERVAL:
        return source->value_count == 1;
    case SOURCE_TIMER:
        return source->value_count == 2;
    case SOURCE_DEFER:
    case SOURCE_FROM:
        return source->value_count == 1;
    case SOURCE_ZIP:
        return source->source_count >= 2;
    }
    return false;
}

bool lower_program(const ProgramAst *ast, ProgramIr *ir)
{
    memset(ir, 0, sizeof(*ir));

    if (ast->function_count > 0) {
        ir->functions = calloc((size_t)ast->function_count, sizeof(*ir->functions));
        if (ir->functions == NULL) {
            fprintf(stderr, "lowering error: out of memory\n");
            return false;
        }
    }

    if (ast->chain_count > 0) {
        ir->chains = calloc((size_t)ast->chain_count, sizeof(*ir->chains));
        if (ir->chains == NULL) {
            fprintf(stderr, "lowering error: out of memory\n");
            return false;
        }
    }

    for (int i = 0; i < ast->function_count; ++i)
    {
        ir->functions[i].def = &ast->functions[i];
        ir->functions[i].kind = FUNCTION_UNKNOWN;
        ir->functions[i].used = false;
    }
    ir->function_count = ast->function_count;
    ir->function_capacity = ast->function_count;

    for (int ci = 0; ci < ast->chain_count; ++ci)
    {
        const ChainAst *chain = &ast->chains[ci];
        ChainIr *out = &ir->chains[ci];
        if (!validate_source(chain->source))
        {
            fprintf(stderr, "lowering error: invalid source '%s'\n", source_kind_name(chain->source->kind));
            return false;
        }

        out->source = *chain->source;
        out->op_count = chain->op_count;
        out->op_capacity = chain->op_count;
        if (chain->op_count > 0) {
            out->ops = calloc((size_t)chain->op_count, sizeof(*out->ops));
            if (out->ops == NULL) {
                fprintf(stderr, "lowering error: out of memory\n");
                return false;
            }
        }
        strncpy(out->subscriber_target, chain->subscriber_target, sizeof(out->subscriber_target) - 1);

        for (int oi = 0; oi < chain->op_count; ++oi)
        {
            const OperatorAst *op = &chain->ops[oi];
            const OperatorInfo *info = find_operator_info(op->kind);
            if (info == NULL)
            {
                fprintf(stderr, "lowering error: unknown operator kind\n");
                return false;
            }

            out->ops[oi] = *op;

            if ((info->argument_kind == ARGUMENT_FUNCTION || info->argument_kind == ARGUMENT_FUNCTION_AND_LITERAL) &&
                !mark_function_usage(ir, op->symbol, info->expected_function_kind))
            {
                return false;
            }
        }
    }

    ir->chain_count = ast->chain_count;
    ir->chain_capacity = ast->chain_count;
    return true;
}
