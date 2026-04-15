#ifndef PROFILER_H
#define PROFILER_H

#include <stdint.h>

#ifndef ENABLE_PROFILER
#define ENABLE_PROFILER 1
#endif

typedef struct BenchProfile
{
    uint64_t init_list_calls;
    uint64_t init_list_ns;
    uint64_t resize_calls;
    uint64_t resize_ns;
    uint64_t resize_items_copied;
    uint64_t freelist_calls;
    uint64_t freelist_ns;

    uint64_t push_back_calls;
    uint64_t push_back_ns;
    uint64_t push_front_calls;
    uint64_t push_front_ns;
    uint64_t pop_calls;
    uint64_t pop_ns;
    uint64_t popstart_calls;
    uint64_t popstart_ns;
    uint64_t list_get_calls;
    uint64_t list_get_ns;
    uint64_t list_isempty_calls;
    uint64_t list_isempty_ns;

    uint64_t range_calls;
    uint64_t range_ns;
    uint64_t range_items;

    uint64_t pop_all_calls;
    uint64_t pop_all_ns;
    uint64_t pop_all_transform_ns;
    uint64_t pop_all_drain_ns;
    uint64_t pop_all_emitted;

    uint64_t map_apply_calls;
    uint64_t map_apply_ns;
    uint64_t map_apply_items;

    uint64_t filter_apply_calls;
    uint64_t filter_apply_ns;
    uint64_t filter_apply_items;

    uint64_t reduce_apply_calls;
    uint64_t reduce_apply_ns;
    uint64_t reduce_apply_items;
} BenchProfile;

extern BenchProfile g_bench_profile;

uint64_t profiler_now_ns(void);
void profiler_reset(void);
void profiler_print_report(void);

#if ENABLE_PROFILER
#define PROFILE_NOW_NS() profiler_now_ns()
#define PROFILE_INC(field) (g_bench_profile.field++)
#define PROFILE_ADD(field, value) (g_bench_profile.field += (value))
#else
#define PROFILE_NOW_NS() ((uint64_t)0)
#define PROFILE_INC(field) ((void)0)
#define PROFILE_ADD(field, value) ((void)0)
#endif

#endif
