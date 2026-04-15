#define _POSIX_C_SOURCE 200809L
#include "observable.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

static long result_sum = 0;

void accumulate(void *v) {
    result_sum = (long)v;
}

void *square(void *v) {
    long x = (long)v;
    return (void *)(x * x);
}

bool isEven_pred(void *v) {
    return ((long)v) % 2 == 0;
}

void *add(void *accum, void *next) {
    return (void *)((long)accum + (long)next);
}

int main(int argc, char *argv[]) {
    int N = argc > 1 ? atoi(argv[1]) : 1000000;
    int RUNS = argc > 2 ? atoi(argv[2]) : 5;
    long total_ns = 0;

    for (int run = 0; run < RUNS; ++run) {
        result_sum = 0;
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        Observable *o = range(1, N);
        o = pipe(o, 3, map(square), filter(isEven_pred), reduce(add));
        subscribe(o, accumulate);

        clock_gettime(CLOCK_MONOTONIC, &end);
        long ns = (end.tv_sec - start.tv_sec) * 1000000000L +
                  (end.tv_nsec - start.tv_nsec);
        total_ns += ns;
    }

    printf("{\"result\": %ld, \"average_ms\": %.2f, \"runs\": %d, \"n\": %d}\n",
           result_sum, (double)total_ns / RUNS / 1e6, RUNS, N);
    return 0;
}
