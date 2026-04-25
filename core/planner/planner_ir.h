#ifndef CORE_PLANNER_PLANNER_IR_H
#define CORE_PLANNER_PLANNER_IR_H

#include "diagnostics.h"
#include "loop_ir.h"

typedef struct
{
    RxStateSlotKind kind;
    const char *name;
    RxArgumentType value_type;
    RxLiteral initial_value;
} RxPlannerIrStateSlot;

typedef struct
{
    RxLoopOpKind kind;
    const RxPlannedStage *stage;
    int state_slot_index;
    int aux_state_slot_index;
} RxPlannerIrOp;

typedef struct
{
    const char *pipeline_name;
    int segment_index;
    RxLoopSourceKind source_kind;
    int source_count;
    int source_inner_n;
    RxPlannerIrStateSlot *state_slots;
    int state_slot_count;
    RxPlannerIrOp *ops;
    int op_count;
    bool returns_scalar;
    bool preserves_observable_layout;
} RxPlannerIrPipeline;

typedef struct
{
    RxPlannerIrPipeline *pipelines;
    int pipeline_count;
} RxPlannerIrProgram;

void rx_planner_ir_init(RxPlannerIrProgram *program);
void rx_planner_ir_reset(RxPlannerIrProgram *program);

bool rx_build_planner_ir(
    const RxLoweredProgram *lowered,
    RxPlannerIrProgram *out_program,
    RxDiagnosticBag *diagnostics);

bool rx_validate_planner_ir(
    const RxPlannerIrProgram *program,
    RxDiagnosticBag *diagnostics);

#endif
