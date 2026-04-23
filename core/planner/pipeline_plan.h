#ifndef CORE_PLANNER_PIPELINE_PLAN_H
#define CORE_PLANNER_PIPELINE_PLAN_H

#include "common.h"
#include "pipeline_ir.h"

typedef enum
{
    RX_PLAN_BACKEND_RUNTIME,
    RX_PLAN_BACKEND_SPECIALIZED_C
} RxPipelineBackend;

typedef enum
{
    RX_SEGMENT_RUNTIME,
    RX_SEGMENT_SPECIALIZED_C
} RxSegmentKind;

typedef enum
{
    RX_STAGE_MAP,
    RX_STAGE_MAP_CHAIN,
    RX_STAGE_FILTER,
    RX_STAGE_SCAN,
    RX_STAGE_SCAN_FROM,
    RX_STAGE_REDUCE,
    RX_STAGE_MAP_TO,
    RX_STAGE_TAKE,
    RX_STAGE_SKIP,
    RX_STAGE_TAKE_WHILE,
    RX_STAGE_SKIP_WHILE,
    RX_STAGE_DISTINCT,
    RX_STAGE_DISTINCT_UNTIL_CHANGED,
    RX_STAGE_LAST,
    RX_STAGE_FIRST,
    RX_STAGE_MERGE_MAP,
    RX_STAGE_ZIP,
    RX_STAGE_UNSUPPORTED
} RxPlannedStageKind;

typedef struct
{
    RxPlannedStageKind kind;
    const RxFunctionSignature *signature;
    RxBinding primary_argument;
    RxBinding secondary_argument;
    int chain_length;
    bool can_start_segment;
    bool can_end_segment;
    bool must_remain_whole;
    bool preserves_runtime_layout;
} RxPlannedStage;

typedef struct
{
    RxSegmentKind kind;
    int start_stage_index;
    int stage_count;
    bool preserves_observable_layout;
    bool requires_runtime_bridge;
} RxPipelineSegment;

typedef struct
{
    const char *name;
    RxPipelineBackend backend;
    RxSourceCall source;
    RxPlannedStage *stages;
    int stage_count;
    RxPipelineSegment *segments;
    int segment_count;
    bool transpile_candidate;
    bool needs_runtime_fallback;
} RxPlannedPipeline;

typedef struct
{
    RxPlannedPipeline *pipelines;
    int pipeline_count;
} RxExecutionPlan;

#endif
