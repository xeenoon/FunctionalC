#ifndef CORE_PLANNER_LOOP_IR_H
#define CORE_PLANNER_LOOP_IR_H

#include "common.h"
#include "pipeline_plan.h"

typedef struct List List;

typedef enum
{
    RX_LOOP_SOURCE_RANGE,
    RX_LOOP_SOURCE_SYNTHETIC_RECORDS,
    RX_LOOP_SOURCE_ZIP_SYNTHETIC_RECORDS,
    RX_LOOP_SOURCE_EXTERNAL_BUFFER,
    RX_LOOP_SOURCE_EXTERNAL_WINDOW,
    RX_LOOP_SOURCE_OF,
    RX_LOOP_SOURCE_ZIP_RANGE,
    RX_LOOP_SOURCE_MERGE_MAP_RANGE,
    RX_LOOP_SOURCE_ZIP_MERGE_MAP_RANGE
} RxLoopSourceKind;

typedef enum
{
    RX_STATE_SCAN_ACCUM,
    RX_STATE_REDUCE_ACCUM,
    RX_STATE_TAKE_COUNT,
    RX_STATE_SKIP_COUNT,
    RX_STATE_SKIP_WHILE_PASSED,
    RX_STATE_LAST_VALUE,
    RX_STATE_HAS_LAST_VALUE,
    RX_STATE_LAST_KEY,
    RX_STATE_HAS_LAST_KEY,
    RX_STATE_DISTINCT_CACHE
} RxStateSlotKind;

typedef struct
{
    RxStateSlotKind kind;
    const char *name;
    RxArgumentType value_type;
    RxLiteral initial_value;
} RxStateSlot;

typedef enum
{
    RX_OP_ASSIGN_INPUT,
    RX_OP_CALL_PAIR_MAP,
    RX_OP_CALL_TRIPLE_MAP,
    RX_OP_CALL_MAP,
    RX_OP_CALL_MAP_INTO,
    RX_OP_CALL_MAP_CHAIN,
    RX_OP_CALL_FILTER,
    RX_OP_CALL_SCAN,
    RX_OP_CALL_SCAN_MUT,
    RX_OP_CALL_REDUCE,
    RX_OP_CALL_REDUCE_MUT,
    RX_OP_CALL_MAP_TO,
    RX_OP_APPLY_TAKE,
    RX_OP_APPLY_SKIP,
    RX_OP_APPLY_TAKE_WHILE,
    RX_OP_APPLY_SKIP_WHILE,
    RX_OP_APPLY_DISTINCT,
    RX_OP_APPLY_DISTINCT_UNTIL_CHANGED,
    RX_OP_APPLY_LAST,
    RX_OP_APPLY_FIRST,
    RX_OP_TERMINAL_RETURN
} RxLoopOpKind;

typedef struct
{
    RxLoopOpKind kind;
    const RxPlannedStage *stage;
    int state_slot_index;
    int aux_state_slot_index;
} RxLoopOp;

typedef struct
{
    const char *pipeline_name;
    int segment_index;
    RxLoopSourceKind source_kind;
    int source_count;
    int source_inner_n;
    RxStateSlot *state_slots;
    int state_slot_count;
    RxLoopOp *ops;
    int op_count;
    bool returns_scalar;
    bool preserves_observable_layout;
} RxLoweredPipeline;

typedef struct
{
    RxLoweredPipeline *pipelines;
    int pipeline_count;
} RxLoweredProgram;

typedef List *(*RxCompiledSegmentFn)(List *data, void *ctx);

typedef struct
{
    RxCompiledSegmentFn fn;
    void *ctx;
    int segment_index;
    int start_stage_index;
    int stage_count;
    bool returns_observable_layout;
} RxCompiledSegment;

#endif
