#include "observable.h"
#include "stdlib.h"
#include "stdio.h"
#include "inttypes.h"
#include "task.h"
#include "stopwatch.h"
#include <time.h>
#include "string.h"
void printno(void *data)
{
    int item = (int)(long)data;
    printf("Outputted: %d\n", item);
}

void printfloat(void *data)
{
    double item = 0;
    memcpy(&item, &data, sizeof(void*));
    printf("Outputted: %f\n", item);
}

bool IsEven(void *v)
{
    return (int)(long)(v) % 2 == 0;
}

void *square(void *v)
{
    int asint = (int)(long)(v);
    return (void *)(long)(asint * asint);
}

void *take2(void *v)
{
    int asint = (int)(long)(v);
    return (void *)(long)(asint - 2);
}

void *add(void *accum, void *next)
{
    return (void *)(long)((int)(long)(accum) + (int)(long)(next));
}
/*
void printzip(void *data)
{
    // We know its a list
    List *aslist = (List *)data;
    for (int i = 0; i < aslist->size; ++i)
    {
        printf("%d, ", (int)(long)list_get(data, i));
    }
    printf("\n");
    freelist(aslist);
}*/
Stopwatch *gs;

void print_task(void *arg)
{
    const char *msg = (const char *)arg;
    printf("[%d ms] Task: %s\n", stopwatch_elapsed_ms(gs), msg);
}

void sleep_ms(long ms)
{
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&req, NULL);
}
static const uint32_t m = 0x80000000; // 2^31
static const uint32_t a = 1103515245;
static const uint32_t c = 12345;

// Hash function: returns next integer in [0, m-1]
uint32_t rng_hash(uint32_t seed)
{
    return (a * seed + c) % m;
}

// Scale function: maps integer hash to float in [-1, 1]
double rng_scale(uint32_t hash)
{
    return (2.0 * hash) / (m - 1) - 1.0;
}

void *scale(void* hash)
{
    void *result = NULL;
    double hashresult = rng_scale((uint32_t)(long)hash);
    memcpy(&result, &hashresult, sizeof(void*));
}
void *hash(void *accum, void *next)
{
    return (void*)(long)rng_hash((uint32_t)(long)accum);
}

Observable *createRngStream(Observable * source, int seed)
{
    return pipe(source, 2, scanfrom(hash, (void*)(long)seed), map(scale));
}

int main()
{
    start_task_system();

    // Observable *observable = range(1,10);
    // observable = observable->pipe(observable, map(take2), NULL);
    Observable *observable2 = range(1, 10);
    // Observable *observable3 = range(10,30);
    // observable3 = observable3->pipe(observable3, map(take2), filter(IsEven), NULL);

    // Observable *observable4 = zip(3, observable, observable2, observable3);
    // observable4 = observable4->pipe(observable4, last(), NULL);

    // Observable *mergemaptest = range(11, 20);
    // mergemaptest = pipe(mergemaptest, 1, mergeMap(observable2));

    // subscribe(mergemaptest, printzip);

    Observable *intervaltest = interval(1000);
    Observable *intervaltest2 = interval(3000);
    intervaltest = pipe(intervaltest, 1, buffer(intervaltest, intervaltest2));
    
    //intervaltest = pipe(intervaltest, 1, takeUntil((void*)(long)(2000)));
    subscribe(intervaltest, printno);
    //subscribe(createRngStream(intervaltest, 30), printfloat);

    while (true)
    {
        sleep_ms(5);
    }

    return 0;
}

int main0()
{
    // Initialize stopwatch
    gs = malloc(sizeof(Stopwatch));
    stopwatch_start(gs);

    // Start task system
    start_task_system();

    // Insert tasks at 10ms, 100ms, 1000ms, 10000ms
    insert_task_in(10, (void *)"10ms", print_task);
    insert_task_in(100, (void *)"100ms", print_task);
    insert_task_in(1000, (void *)"1000ms", print_task);
    insert_task_in(10000, (void *)"10000ms", print_task);

    // Keep main alive long enough to run all tasks
    // In a real system you might join the worker thread instead
    while (stopwatch_elapsed_ms(gs) < 11000)
    {
        sleep_ms(5);
    }

    printf("All tasks completed.\n");
    free(gs);
    return 0;
}