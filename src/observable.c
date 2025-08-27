#include "list.h"
#include "observable.h"
#include "stdlib.h"
#include "stdio.h"
#include <inttypes.h>
#include <pthread.h>
#include "task.h"

Observable *create_observable()
{
    Observable *o = malloc(sizeof(Observable));
    o->data = init_list();
    o->pipe = pipe;
    return o;
}

void push_observable(Observable *o, void *data)
{
    push_back(o->data, data);
    pop_all(o);
}

void subscribe(Observable *o, Subscriber subscriber)
{
    o->subscriber = subscriber;
    pop_all(o);
}

void pop_all(Observable *o)
{
    List *q = o->data;
    while (!list_isempty(q))
    {
        void *data = popstart(q); // pop from front (FIFO)
        o->subscriber(data);
    }
}

Observable *range(int min, int max) // Inclusive range
{
    Observable *result = create_observable();
    for (int i = min; i < max + 1; ++i)
    {
        push_back(result->data, (void *)(long)(i));
    }
    return result;
}

void addnext(void *ctx)
{
    IntervalCtx *ictx = (IntervalCtx *)ctx;
    long ms = ictx->ms;
    ictx->amt++;
    insert_task_in(ms, ctx, addnext);
    push_observable(ictx->o, (void*)(long)(ictx->ms * ictx->amt));
}
Observable *interval(long ms)
{
    Observable *result = create_observable();
    IntervalCtx *ctx = malloc(sizeof(IntervalCtx));
    ctx->ms = ms;
    ctx->o = result;
    ctx->amt = 0;
    insert_task_in(ms, ctx, addnext);
    return result;
}

Observable *pipe(Observable *self, ...)
{
    Observable *newObs = malloc(sizeof(Observable));
    newObs->subscriber = NULL;
    newObs->pipe = self->pipe;

    va_list args;
    va_start(args, self);

    List *currdata = self->data;

    while (1)
    {
        void *raw = va_arg(args, void *); // read as void*

        if (raw == NULL)
            break;
        Query *q = (Query *)raw;
        List *temp = q->func(currdata, q->ctx); // Queries must create a new list
        freelist(currdata);                     // We are responsible for disposing the old list
        currdata = temp;
    }
    va_end(args);
    newObs->data = currdata;

    return newObs;
}
static List *filter_apply(List *data, void *ctx)
{
    FilterCtx *f = ctx;
    List *result = init_list();
    for (int i = 0; i < data->size; ++i)
    {
        void *item = list_get(data, i);
        if (f->pred(item))
            push_back(result, item);
    }
    return result;
}

Query *filter(BooleanFunction pred)
{
    FilterCtx *ctx = malloc(sizeof(*ctx));
    ctx->pred = pred;
    Query *q = malloc(sizeof(*q));
    q->func = filter_apply;
    q->ctx = ctx;
    return q;
}

static List *takeuntil_apply(List *data, void *ctx)
{
    FilterCtx *f = ctx;
    List *result = init_list();
    for (int i = 0; i < data->size; ++i)
    {
        void *item = list_get(data, i);
        if (f->pred(item))
            push_back(result, item);
    }
    return result;
}
Query *takeUntil(void *comp)
{
    int asint = (int)(long)comp;
}

static List *map_apply(List *data, void *ctx)
{
    MapCtx *m = ctx;
    List *result = init_list();
    for (int i = 0; i < data->size; ++i)
    {
        void *item = list_get(data, i);
        push_back(result, m->pred(item));
    }
    return result;
}

Query *map(ModifierFunction mapper)
{
    MapCtx *ctx = malloc(sizeof(*ctx));
    ctx->pred = mapper;
    Query *q = malloc(sizeof(*q));
    q->func = map_apply;
    q->ctx = ctx;
    return q;
}

static List *mergemap_apply(List *data, void *ctx)
{
    List *result = init_list();
    Observable *o = (Observable *)ctx;

    for (int oi = 0; oi < o->data->size; ++oi)
    {
        // printf("%d items in o->data, %d items in data\n", o->data->size, data->size);
        void *item = list_get(o->data, oi);
        for (int i = 0; i < data->size; ++i)
        {
            void *start = list_get(data, i);
            List *line = init_list();
            push_back(line, start);
            push_back(line, item);
            push_back(result, line);
        }
    }
    return result;
}
Query *mergeMap(Observable *o)
{
    Query *q = malloc(sizeof(*q));
    q->func = mergemap_apply;
    q->ctx = o; // No context required
    return q;
}

static List *scan_apply(List *data, void *ctx)
{
    ScanCtx *m = ctx;
    List *result = init_list();
    void *accum = NULL;
    for (int i = 0; i < data->size; ++i)
    {
        void *item = list_get(data, i);
        void *r = m->pred(accum, item);
        push_back(result, r);
        accum = r;
    }
    return result;
}

Query *scan(AccumulatorFunction accumulator)
{
    ScanCtx *ctx = malloc(sizeof(*ctx));
    ctx->pred = accumulator;
    ctx->accum = NULL; // Handily also stores as 0 for integer ptr conversions
    Query *q = malloc(sizeof(*q));
    q->func = scan_apply;
    q->ctx = ctx;
    return q;
}

static List *reduce_apply(List *data, void *ctx)
{
    ScanCtx *m = ctx;
    List *result = init_list();
    void *accum = NULL;
    for (int i = 0; i < data->size; ++i)
    {
        void *item = list_get(data, i);
        void *r = m->pred(accum, item);
        accum = r;
    }
    push_back(result, accum);

    return result;
}

Query *reduce(AccumulatorFunction accumulator)
{
    ScanCtx *ctx = malloc(sizeof(*ctx));
    ctx->pred = accumulator;
    ctx->accum = NULL; // Handily also stores as 0 for integer ptr conversions
    Query *q = malloc(sizeof(*q));
    q->func = reduce_apply;
    q->ctx = ctx;
    return q;
}

static List *last_apply(List *data, void *unused)
{
    List *result = init_list();
    push_back(result, pop(data));
    return result;
}
Query *last()
{
    Query *q = malloc(sizeof(*q));
    q->func = last_apply;
    q->ctx = NULL;
    return q;
}

Observable *zip(int count, ...)
{
    va_list args;
    va_start(args, count);

    Observable **observables = malloc(sizeof(Observable *) * count);
    for (int i = 0; i < count; ++i)
    {
        observables[i] = va_arg(args, Observable *);
    }
    va_end(args);

    int shortest = INT32_MAX;
    for (int i = 0; i < count; ++i)
    {
        int msize = observables[i]->data->size;
        if (msize < shortest)
        {
            shortest = msize;
        }
    }

    Observable *result = create_observable();
    for (int i = 0; i < shortest; ++i)
    {
        List *zipped = init_list();
        for (int j = 0; j < count; ++j)
        {
            push_back(zipped, list_get(observables[j]->data, i));
        }
        push_back(result->data, zipped);
    }
    return result;
}