#define _POSIX_C_SOURCE 199309L // enables clock_gettime in glibc
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include "stopwatch.h"

void stopwatch_start(Stopwatch *sw)
{
    clock_gettime(CLOCK_MONOTONIC, &sw->start);
}

long stopwatch_elapsed_ms(Stopwatch *sw)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    long seconds = now.tv_sec - sw->start.tv_sec;
    long nsec = now.tv_nsec - sw->start.tv_nsec;

    return seconds * 1000 + nsec / 1000000;
}