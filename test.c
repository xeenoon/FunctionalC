#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/*
 * Hand-written kernel for:
 * zip(range(1, N), range(1, N))
 *   -> pairMap(pairSum)
 *   -> scan(sum)
 *   -> map(div10)
 *   -> distinctUntilChanged(identity)
 *   -> skipWhile(lt5000)
 *   -> 50 alternating map(add1)/map(sub1)
 *   -> last()
 *
 * The alternating map tail is net identity, so the minimal steady-state
 * only needs the divided distinct bucket and last emitted value.
 */
static int64_t run_once(int N)
{
    intptr_t scan_accum = 0;
    intptr_t last_bucket = 0;
    intptr_t last_value = 0;
    bool has_last_bucket = false;
    bool has_last_value = false;
    bool skip_while_passed = false;

    for (intptr_t src = 1; src <= (intptr_t)N; ++src)
    {
        /* zip(range, range) + pairSum */
        scan_accum += src + src;

        /* div10(scan(sum(...))) */
        intptr_t bucket = scan_accum / 10;

        /* distinctUntilChanged(identity) */
        if (has_last_bucket && bucket == last_bucket)
        {
            continue;
        }
        last_bucket = bucket;
        has_last_bucket = true;

        /* skipWhile(lt5000) */
        if (!skip_while_passed && bucket < 5000)
        {
            continue;
        }
        skip_while_passed = true;

        /* 50 alternating add1/sub1 maps cancel out */
        last_value = bucket;
        has_last_value = true;
    }

    return has_last_value ? (int64_t)last_value : 0;
}

int main(int argc, char **argv)
{
    int N = argc > 1 ? atoi(argv[1]) : 1000000;
    int RUNS = argc > 2 ? atoi(argv[2]) : 1;
    int64_t result = 0;
    int64_t total_ns = 0;

    for (int run = 0; run < RUNS; ++run)
    {
        struct timespec start;
        struct timespec end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        result = run_once(N);
        clock_gettime(CLOCK_MONOTONIC, &end);
        total_ns += (int64_t)(end.tv_sec - start.tv_sec) * 1000000000LL
            + (int64_t)(end.tv_nsec - start.tv_nsec);
    }

    printf(
        "{\"result\": %" PRId64 ", \"average_ms\": %.5f, \"runs\": %d, \"n\": %d}\n",
        result,
        (double)total_ns / RUNS / 1e6,
        RUNS,
        N);
    return 0;
}
