#define _POSIX_C_SOURCE 200809L
#include "observable.h"
#include "profiler.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

static intptr_t result_sum = 0;

void accumulate(void *v) {
    result_sum = (intptr_t)v;
}

void *square(void *v) {
    intptr_t x = (intptr_t)v;
    return (void *)(intptr_t)(x * x);
}

bool isEven_pred(void *v) {
    return ((intptr_t)v) % 2 == 0;
}

void *add(void *accum, void *next) {
    return (void *)(intptr_t)((intptr_t)accum + (intptr_t)next);
}

int main() {
    int N = 1000000;
    int RUNS = 5;
    int64_t total_ns = 0;

    profiler_reset();

    for (int run = 0; run < RUNS; ++run) {
        result_sum = 0;
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        Observable *o = range(1, N);
        o->complete = false;
        o = pipe(o, 3, map(square), filter(isEven_pred), reduce(add));
        subscribe(o, accumulate);

        clock_gettime(CLOCK_MONOTONIC, &end);
        int64_t ns = (int64_t)(end.tv_sec - start.tv_sec) * 1000000000LL +
                     (int64_t)(end.tv_nsec - start.tv_nsec);
        total_ns += ns;
    }

    printf("C   result : %lld\n", (long long)result_sum);
    printf("C   average: %.2f ms  (%d runs, N=%d)\n",
           (double)total_ns / RUNS / 1e6, RUNS, N);
#if ENABLE_PROFILER
    profiler_print_report();
#endif
    return 0;
}
