#include "planner.h"

#include <stdlib.h>
#include <string.h>

#include "operator_registry.h"

static bool source_is_fusible(const SourceAst *source)
{
    return source->kind == SOURCE_RANGE || source->kind == SOURCE_OF || source->kind == SOURCE_EMPTY;
}

bool build_execution_plan(const ProgramIr *ir, ExecutionPlan *plan)
{
    memset(plan, 0, sizeof(*plan));
    plan->ir = *ir;
    plan->chain_count = ir->chain_count;
    if (ir->chain_count > 0) {
        plan->chains = calloc((size_t)ir->chain_count, sizeof(*plan->chains));
        if (plan->chains == NULL) {
            return false;
        }
    }

    for (int ci = 0; ci < ir->chain_count; ++ci)
    {
        const ChainIr *chain = &ir->chains[ci];
        bool fusible = source_is_fusible(&chain->source);
        for (int oi = 0; oi < chain->op_count && fusible; ++oi)
        {
            const OperatorInfo *info = find_operator_info(chain->ops[oi].kind);
            if (info == NULL || !info->fusible)
            {
                fusible = false;
            }
        }
        plan->chains[ci].backend = fusible ? BACKEND_FUSED : BACKEND_RUNTIME;
    }

    return true;
}
