#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include "stopwatch.h"
#include "task.h"

Stopwatch *globalstopwatch;

// Helper: make timespec for CLOCK_MONOTONIC + ms offset
static void timespec_from_monotonic_ms(struct timespec *ts, long ms_from_now)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long sec = ms_from_now / 1000;
    long rem_ms = ms_from_now % 1000;
    ts->tv_sec = now.tv_sec + sec;
    ts->tv_nsec = now.tv_nsec + rem_ms * 1000000L;
    if (ts->tv_nsec >= 1000000000L)
    {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000L;
    }
}

// Insert helper (assumes mutex locked)
// Insert in sorted order by time_ms (ascending). Returns true if inserted at head.
static bool insert_task_locked(TimedTask *task)
{
    if (tasks_head == NULL || task->time_ms < tasks_head->time_ms)
    {
        task->next = tasks_head;
        tasks_head = task;
        return true;
    }

    TimedTask *cur = tasks_head;
    while (cur->next != NULL && cur->next->time_ms <= task->time_ms)
    {
        cur = cur->next;
    }
    task->next = cur->next;
    cur->next = task;
    return false;
}

// schedule at absolute ms (since stopwatch start)
void insert_task_at(long abs_time_ms, void *ctx, Action action)
{
    TimedTask *t = malloc(sizeof(TimedTask));
    t->time_ms = abs_time_ms;
    t->ctx = ctx;
    t->action = action;
    t->next = NULL;

    pthread_mutex_lock(&tasks_mutex);
    insert_task_locked(t);
    // Wake worker if:
    //  - list was empty (worker may be waiting indefinitely)
    //  - new task is earlier than previous head (worker must reschedule)
    pthread_cond_signal(&tasks_cond);
    pthread_mutex_unlock(&tasks_mutex);
}

// schedule after delay_ms from now
void insert_task_in(long delay_ms, void *ctx, Action action)
{
    long now = stopwatch_elapsed_ms(globalstopwatch);
    insert_task_at(now + delay_ms, ctx, action);
}

// Worker thread
static void *tasks_worker(void *unused)
{
    pthread_mutex_lock(&tasks_mutex);
    while (tasks_running)
    {
        if (tasks_head == NULL)
        {
            // Nothing scheduled: wait until a task is inserted or shutdown
            pthread_cond_wait(&tasks_cond, &tasks_mutex);
            continue;
        }

        long now = stopwatch_elapsed_ms(globalstopwatch);
        if (tasks_head->time_ms <= now)
        {
            // Pop and run all tasks that are due now (in FIFO order for equal times)
            TimedTask *due = tasks_head;
            tasks_head = tasks_head->next;
            // Unlock while executing the action to avoid blocking inserts
            pthread_mutex_unlock(&tasks_mutex);
            // Execute (action may be long-running)
            if (due->action)
                due->action(due->ctx);
            free(due);
            // Re-lock and continue loop
            pthread_mutex_lock(&tasks_mutex);
            continue;
        }
        else
        {
            // Wait until the head is due OR until signalled (insert/cancel/shutdown)
            long wait_ms = tasks_head->time_ms - now;
            struct timespec timeout;
            // We use CLOCK_MONOTONIC for both stopwatch and condition clock.
            timespec_from_monotonic_ms(&timeout, wait_ms);
            // timed wait (returns either on signal or timeout)
            int rc = pthread_cond_timedwait(&tasks_cond, &tasks_mutex, &timeout);
            if (rc == ETIMEDOUT)
            {
                // will loop and execute the due task
                continue;
            }
            else
            {
                // woken by signal (insertion/shutdown). Loop will recompute head/now.
                continue;
            }
        }
    }
    pthread_mutex_unlock(&tasks_mutex);
    return NULL;
}

// Init
void start_task_system()
{
    // init cond to use CLOCK_MONOTONIC
    pthread_condattr_t cattr;
    pthread_condattr_init(&cattr);
    pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC);
    pthread_cond_init(&tasks_cond, &cattr);
    pthread_condattr_destroy(&cattr);

    globalstopwatch = malloc(sizeof(Stopwatch));
    stopwatch_start(globalstopwatch);
    tasks_running = true;
    int rc = pthread_create(&tasks_thread, NULL, tasks_worker, NULL);
    if (rc != 0)
    {
        perror("pthread_create");
        exit(1);
    }
}

void stop_task_system()
{
    pthread_mutex_lock(&tasks_mutex);
    tasks_running = false;
    pthread_cond_signal(&tasks_cond);
    pthread_mutex_unlock(&tasks_mutex);
    pthread_join(tasks_thread, NULL);

    // cleanup remaining tasks
    pthread_mutex_lock(&tasks_mutex);
    TimedTask *t = tasks_head;
    while (t)
    {
        TimedTask *n = t->next;
        free(t);
        t = n;
    }
    tasks_head = NULL;
    pthread_mutex_unlock(&tasks_mutex);

    pthread_cond_destroy(&tasks_cond);
}