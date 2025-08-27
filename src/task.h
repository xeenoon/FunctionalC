#ifndef TASK_H
#define TASK_H

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include "stopwatch.h"

typedef void (*Action)(void *arg);

typedef struct TimedTask
{
    long time_ms; // absolute time (ms since stopwatch start)
    void *ctx;
    Action action;
    struct TimedTask *next;
} TimedTask;

// Globals for task system
static TimedTask *tasks_head = NULL;
static pthread_mutex_t tasks_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t tasks_cond;
static pthread_t tasks_thread;
static bool tasks_running = false;


static void timespec_from_monotonic_ms(struct timespec *ts, long ms_from_now);
static bool insert_task_locked(TimedTask *task);
void insert_task_at(long abs_time_ms, void *ctx, Action action);
void insert_task_in(long delay_ms, void *ctx, Action action);
static void *tasks_worker(void *unused);
void start_task_system();
void stop_task_system();
#endif