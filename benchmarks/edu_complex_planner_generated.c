#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>

#include <stdbool.h>

#include <stdint.h>

#include <stdio.h>

#include <stdlib.h>

#include <time.h>

#include "edu_complex_planner_helpers.h"

#ifdef RX_PLANNER_PROFILE
typedef struct {
    const char *name;
    uint64_t hits;
    uint64_t total_ns;
} RxProfileSlot;

static RxProfileSlot rx_profile_slots_edu_complex_planner[] = {
    {"mapInto[0]", 0, 0},
    {"scanMut[1]", 0, 0},
    {"mapInto[2]", 0, 0},
    {"filter[3]", 0, 0},
    {"distinctUntilChanged[4]", 0, 0},
    {"skipWhile[5]", 0, 0},
    {"reduceMut[6]", 0, 0},
};
static uint64_t rx_profile_diff_ns(struct timespec start, struct timespec end) {
    return (uint64_t)(end.tv_sec - start.tv_sec) * 1000000000ULL + (uint64_t)(end.tv_nsec - start.tv_nsec);
}
#define RX_PROFILE_STAGE_BEGIN(ID) struct timespec __rx_stage_start_##ID, __rx_stage_end_##ID; clock_gettime(CLOCK_MONOTONIC, &__rx_stage_start_##ID)
#define RX_PROFILE_STAGE_END(ID) do { clock_gettime(CLOCK_MONOTONIC, &__rx_stage_end_##ID); rx_profile_slots_edu_complex_planner[ID].hits += 1; rx_profile_slots_edu_complex_planner[ID].total_ns += rx_profile_diff_ns(__rx_stage_start_##ID, __rx_stage_end_##ID); } while (0)
#else
#define RX_PROFILE_STAGE_BEGIN(ID) do { } while (0)
#define RX_PROFILE_STAGE_END(ID) do { } while (0)
#endif



#ifdef RX_PLANNER_PROFILE
void rx_dump_profile_edu_complex_planner(FILE *stream) {
    FILE *target = stream != NULL ? stream : stderr;
    for (size_t index = 0; index < sizeof(rx_profile_slots_edu_complex_planner) / sizeof(rx_profile_slots_edu_complex_planner[0]); ++index) {
        const double avg_ns = rx_profile_slots_edu_complex_planner[index].hits == 0
            ? 0.0
            : (double)rx_profile_slots_edu_complex_planner[index].total_ns / (double)rx_profile_slots_edu_complex_planner[index].hits;
        fprintf(target,
                "planner profile %s[%zu] hits=%llu total_ms=%.5f avg_ns=%.2f\n",
                rx_profile_slots_edu_complex_planner[index].name,
                index,
                (unsigned long long)rx_profile_slots_edu_complex_planner[index].hits,
                (double)rx_profile_slots_edu_complex_planner[index].total_ns / 1e6,
                avg_ns);
    }
}
#else
void rx_dump_profile_edu_complex_planner(FILE *stream) {
    (void)stream;
}
#endif



static size_t rx_align_up_size(size_t value, size_t alignment) {
    const size_t mask = alignment > 0 ? alignment - 1u : 0u;
    return alignment > 0 ? (value + mask) & ~mask : value;
}

size_t rx_storage_bytes_edu_complex_planner(size_t (*resolve)(const char *name, size_t *alignment)) {
    size_t cursor = 0;
    for (size_t index = 0; index < 4; ++index) {
        size_t alignment = 1;
        const char *symbol = NULL;
        size_t bytes = 0;
        switch (index) {
            case 0: symbol = "planner_state"; break;
            case 1: symbol = "planner_digest"; break;
            case 2: symbol = "planner_enriched_scratch"; break;
            case 3: symbol = "planner_snapshot_scratch"; break;
            default: break;
        }
        if (symbol == NULL) { continue; }
        bytes = resolve != NULL ? resolve(symbol, &alignment) : 0;
        cursor = rx_align_up_size(cursor, alignment > 0 ? alignment : 1u);
        cursor += bytes;
    }
    return cursor;
}

void rx_bind_storage_edu_complex_planner(void *base, size_t (*resolve)(const char *name, size_t *alignment), void (*bind)(const char *name, void *ptr)) {
    size_t cursor = 0;
    for (size_t index = 0; index < 4; ++index) {
        size_t alignment = 1;
        const char *symbol = NULL;
        size_t bytes = 0;
        switch (index) {
            case 0: symbol = "planner_state"; break;
            case 1: symbol = "planner_digest"; break;
            case 2: symbol = "planner_enriched_scratch"; break;
            case 3: symbol = "planner_snapshot_scratch"; break;
            default: break;
        }
        if (symbol == NULL) { continue; }
        bytes = resolve != NULL ? resolve(symbol, &alignment) : 0;
        cursor = rx_align_up_size(cursor, alignment > 0 ? alignment : 1u);
        if (bind != NULL) {
            bind(symbol, (char *)base + cursor);
        }
        cursor += bytes;
    }
}



intptr_t run_edu_complex_planner(void ** records, intptr_t N) {
    void * scan_accum = planner_state;
    intptr_t last_key = 0;
    bool has_last_key = false;
    bool skip_while_passed = false;
    void * reduce_accum = planner_digest;
    intptr_t src = 0;
    for (; (src < N); (src = (src + 1))) {
        intptr_t value = ((intptr_t)records[src]);
        RX_PROFILE_STAGE_BEGIN(0);
        enrich_record_into(planner_enriched_scratch, ((void *)value));
        (value = ((intptr_t)planner_enriched_scratch));
        RX_PROFILE_STAGE_END(0);
        RX_PROFILE_STAGE_BEGIN(1);
        update_database_state_mut(scan_accum, ((void *)value));
        (value = ((intptr_t)scan_accum));
        RX_PROFILE_STAGE_END(1);
        RX_PROFILE_STAGE_BEGIN(2);
        build_school_snapshot_into(planner_snapshot_scratch, ((void *)value));
        (value = ((intptr_t)planner_snapshot_scratch));
        RX_PROFILE_STAGE_END(2);
        void * __rx_value_view = ((void *)value);
        RX_PROFILE_STAGE_BEGIN(3);
        if ((!snapshot_is_relevant(__rx_value_view))) {
            continue;
        }
        RX_PROFILE_STAGE_END(3);
        RX_PROFILE_STAGE_BEGIN(4);
        intptr_t key_4 = ((intptr_t)snapshot_signature(__rx_value_view));
        if ((has_last_key && key_4 == last_key)) {
            continue;
        }
        last_key = key_4;
        has_last_key = true;
        RX_PROFILE_STAGE_END(4);
        RX_PROFILE_STAGE_BEGIN(5);
        if (((!skip_while_passed) && snapshot_is_cold(__rx_value_view))) {
            continue;
        }
        skip_while_passed = true;
        RX_PROFILE_STAGE_END(5);
        RX_PROFILE_STAGE_BEGIN(6);
        accumulate_digest_mut(reduce_accum, __rx_value_view);
        RX_PROFILE_STAGE_END(6);
    }
    return (intptr_t)reduce_accum;
}

