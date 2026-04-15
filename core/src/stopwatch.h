#ifndef STOPWATCH_H
#define STOPWATCH_H

#include <time.h>

typedef struct Stopwatch {
    struct timespec start;
} Stopwatch;

void stopwatch_start(Stopwatch *sw);
long stopwatch_elapsed_ms(Stopwatch *sw);

#endif // STOPWATCH_H
