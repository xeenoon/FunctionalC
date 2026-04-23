#ifndef PLANNER_H
#define PLANNER_H

#include <stdbool.h>

#include "ir.h"

typedef enum
{
    BACKEND_FUSED,
    BACKEND_RUNTIME
} BackendKind;

typedef struct
{
    BackendKind backend;
} ChainPlan;

typedef struct
{
    ProgramIr ir;
    ChainPlan *chains;
    int chain_count;
} ExecutionPlan;

bool build_execution_plan(const ProgramIr *ir, ExecutionPlan *plan);

#endif
