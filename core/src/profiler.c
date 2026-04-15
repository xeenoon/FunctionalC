#define _POSIX_C_SOURCE 200809L
#include "profiler.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

BenchProfile g_bench_profile;

uint64_t profiler_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

void profiler_reset(void)
{
    memset(&g_bench_profile, 0, sizeof(g_bench_profile));
}

static void print_row(const char *label, uint64_t ns, uint64_t calls)
{
    double ms = (double)ns / 1e6;
    double avg_us = calls ? (double)ns / (double)calls / 1e3 : 0.0;
    printf("%-20s %10.3f ms  %12llu calls  %10.3f us/call\n",
           label,
           ms,
           (unsigned long long)calls,
           avg_us);
}

void profiler_print_report(void)
{
    printf("\nProfiler report\n");
    printf("------------------------------\n");
    print_row("range", g_bench_profile.range_ns, g_bench_profile.range_calls);
    printf("%-20s %12llu items\n", "range items", (unsigned long long)g_bench_profile.range_items);
    print_row("pop_all total", g_bench_profile.pop_all_ns, g_bench_profile.pop_all_calls);
    print_row("pop_all xform", g_bench_profile.pop_all_transform_ns, g_bench_profile.pop_all_calls);
    print_row("pop_all drain", g_bench_profile.pop_all_drain_ns, g_bench_profile.pop_all_calls);
    printf("%-20s %12llu items\n", "subscriber emits", (unsigned long long)g_bench_profile.pop_all_emitted);
    printf("\n");
    print_row("map_apply", g_bench_profile.map_apply_ns, g_bench_profile.map_apply_calls);
    printf("%-20s %12llu items\n", "map items", (unsigned long long)g_bench_profile.map_apply_items);
    print_row("filter_apply", g_bench_profile.filter_apply_ns, g_bench_profile.filter_apply_calls);
    printf("%-20s %12llu items\n", "filter items", (unsigned long long)g_bench_profile.filter_apply_items);
    print_row("reduce_apply", g_bench_profile.reduce_apply_ns, g_bench_profile.reduce_apply_calls);
    printf("%-20s %12llu items\n", "reduce items", (unsigned long long)g_bench_profile.reduce_apply_items);
    printf("\n");
    print_row("init_list", g_bench_profile.init_list_ns, g_bench_profile.init_list_calls);
    print_row("resize", g_bench_profile.resize_ns, g_bench_profile.resize_calls);
    printf("%-20s %12llu items\n", "resize copied", (unsigned long long)g_bench_profile.resize_items_copied);
    print_row("freelist", g_bench_profile.freelist_ns, g_bench_profile.freelist_calls);
    print_row("push_back", g_bench_profile.push_back_ns, g_bench_profile.push_back_calls);
    print_row("push_front", g_bench_profile.push_front_ns, g_bench_profile.push_front_calls);
    print_row("pop", g_bench_profile.pop_ns, g_bench_profile.pop_calls);
    print_row("popstart", g_bench_profile.popstart_ns, g_bench_profile.popstart_calls);
    print_row("list_get", g_bench_profile.list_get_ns, g_bench_profile.list_get_calls);
    print_row("list_isempty", g_bench_profile.list_isempty_ns, g_bench_profile.list_isempty_calls);
    printf("------------------------------\n");
}
