#define _POSIX_C_SOURCE 200809L
#define EDU_COMPLEX_SHARED_ONLY
#define EDU_COMPLEX_BENCHMARK_ALREADY_INCLUDED
#define EDU_COMPLEX_MANUAL_HELPERS_ONLY
#include "edu_complex_benchmark.c"
#include "edu_complex_benchmark_manual.c"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void *planner_storage_block = NULL;
static size_t planner_storage_bytes = 0;
static DatabaseState *planner_state = NULL;
static EnrichedRecord *planner_enriched_scratch = NULL;
static SchoolSnapshot *planner_snapshot_scratch = NULL;
static AuditDigest *planner_digest = NULL;

size_t rx_storage_bytes_edu_complex_planner(size_t (*resolve)(const char *name, size_t *alignment));
void rx_bind_storage_edu_complex_planner(
    void *base,
    size_t (*resolve)(const char *name, size_t *alignment),
    void (*bind)(const char *name, void *ptr));

static size_t planner_state_storage_bytes(void)
{
    return align_up_size(sizeof(DatabaseState), alignof(SchoolAggregate)) +
        (size_t)8192 * sizeof(SchoolAggregate);
}

static size_t planner_storage_resolve(const char *name, size_t *alignment)
{
    if (strcmp(name, "planner_state") == 0)
    {
        if (alignment != NULL)
        {
            *alignment = alignof(DatabaseState);
        }
        return planner_state_storage_bytes();
    }
    if (strcmp(name, "planner_enriched_scratch") == 0)
    {
        if (alignment != NULL)
        {
            *alignment = alignof(EnrichedRecord);
        }
        return sizeof(EnrichedRecord);
    }
    if (strcmp(name, "planner_snapshot_scratch") == 0)
    {
        if (alignment != NULL)
        {
            *alignment = alignof(SchoolSnapshot);
        }
        return sizeof(SchoolSnapshot);
    }
    if (strcmp(name, "planner_digest") == 0)
    {
        if (alignment != NULL)
        {
            *alignment = alignof(AuditDigest);
        }
        return sizeof(AuditDigest);
    }
    if (alignment != NULL)
    {
        *alignment = 1u;
    }
    return 0;
}

static void planner_storage_bind(const char *name, void *ptr)
{
    if (strcmp(name, "planner_state") == 0)
    {
        planner_state = (DatabaseState *)ptr;
        planner_state->capacity = 8192;
        planner_state->table = (SchoolAggregate *)((char *)ptr + align_up_size(sizeof(DatabaseState), alignof(SchoolAggregate)));
        return;
    }
    if (strcmp(name, "planner_enriched_scratch") == 0)
    {
        planner_enriched_scratch = (EnrichedRecord *)ptr;
        return;
    }
    if (strcmp(name, "planner_snapshot_scratch") == 0)
    {
        planner_snapshot_scratch = (SchoolSnapshot *)ptr;
        return;
    }
    if (strcmp(name, "planner_digest") == 0)
    {
        planner_digest = (AuditDigest *)ptr;
    }
}

static void ensure_planner_storage(void)
{
    if (planner_storage_block != NULL)
    {
        return;
    }

    planner_storage_bytes = rx_storage_bytes_edu_complex_planner(planner_storage_resolve);
    planner_storage_block = calloc(1, planner_storage_bytes);
    if (planner_storage_block == NULL)
    {
        return;
    }

    /* OPT_REUSE_PLANNER_1:
     * Reuse plan: the planner harness now owns one arena block for all symbol-backed planner storage instead of
     * four independent allocations. The generated pipeline keeps stable pointers to this block across all runs.
     * Why reuse is safe: planner scratch/state objects have one owner, fixed size, and are fully reset before
     * each benchmark run. No pointer from the arena is freed or resized independently.
     * Compiler plan summary:
     *   1. Identify named mutable stage/state symbols with run-long lifetime.
     *   2. Compute a packed layout from type sizes and alignments.
     *   3. Replace separate malloc/calloc calls with one arena allocation.
     *   4. Rewrite symbol bindings as fixed offsets into the arena.
     */
    rx_bind_storage_edu_complex_planner(
        planner_storage_block,
        planner_storage_resolve,
        planner_storage_bind);
}

static void reset_planner_run_state(void)
{
    ensure_planner_storage();
    memset(planner_state->table, 0, (size_t)planner_state->capacity * sizeof(*planner_state->table));
    reset_database_state(planner_state);
    memset(planner_enriched_scratch, 0, sizeof(*planner_enriched_scratch));
    memset(planner_snapshot_scratch, 0, sizeof(*planner_snapshot_scratch));
    memset(planner_digest, 0, sizeof(*planner_digest));
}

#ifndef EDU_COMPLEX_PLANNER_GENERATED
#error "EDU_COMPLEX_PLANNER_GENERATED must point to the generated planner C file"
#endif

#include EDU_COMPLEX_PLANNER_GENERATED

int main(int argc, char **argv)
{
    long long total_ns = 0;
    int runs = 3;
    ManualDataset *dataset;
    ManualZippedBuffer *zipped;

    if (argc < 2)
    {
        fprintf(stderr, "usage: edu_complex_benchmark_planner <data.bin> [runs]\n");
        return 1;
    }

    dataset = load_manual_dataset(argv[1]);
    runs = argc > 2 ? atoi(argv[2]) : 3;
    if (dataset == NULL || dataset->records == NULL)
    {
        return 1;
    }
    zipped = build_manual_zipped_buffer(dataset->records);
    if (zipped == NULL)
    {
        free_manual_dataset(dataset);
        return 1;
    }

    for (int run = 0; run < runs; ++run)
    {
        struct timespec start;
        struct timespec end;
        reset_planner_run_state();
        clock_gettime(CLOCK_MONOTONIC, &start);
        g_result = (AuditDigest *)(intptr_t)run_edu_complex_planner(zipped->items, zipped->count);
        clock_gettime(CLOCK_MONOTONIC, &end);
        total_ns += (long long)(end.tv_sec - start.tv_sec) * 1000000000LL +
            (long long)(end.tv_nsec - start.tv_nsec);
    }

    print_result(g_result, (double)total_ns / runs / 1e6, runs);
#ifdef RX_PLANNER_PROFILE
    rx_dump_profile_edu_complex_planner(stderr);
#endif
    free_manual_zipped_buffer(zipped);
    free_manual_dataset(dataset);
    free(planner_storage_block);
    return 0;
}
