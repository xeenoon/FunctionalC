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
    o->pipe = NULL;
    o->subscriber = NULL;
    o->emit_handler = NULL;
    o->on_subscription = NULL;
    return o;
}

Observable *never()
{
    return create_observable();
}

Observable *empty()
{
    Observable *o = create_observable();
    o->complete = true;
    return o;
}

Observable *from(List *data)
{
    Observable *o = create_observable();
    freelist(o->data);
    return o;
}

Observable *of(int count, ...)
{
    Observable *o = malloc(sizeof(Observable));
    if (!o)
        return NULL;

    o->data = init_list();
    o->pipe = NULL;
    o->subscriber = NULL;
    o->emit_handler = NULL;

    va_list args;
    va_start(args, count);

    for (int i = 0; i < count; i++)
    {
        void *item = va_arg(args, void *); // read each void*
        push_back(o->data, item);          // append to list
    }

    va_end(args);
    return o;
}

void push_observable(Observable *o, void *data)
{
    if (o->complete)
    {
        return;
    }
    push_back(o->data, data);
    pop_all(o);
}

void subscribe(Observable *o, Subscriber subscriber)
{
    if(o->on_subscription != NULL) //Are we a factory?
    {
        push_back(o->data, o->on_subscription()); //Pop out a new one
    }
    o->subscriber = subscriber;
    pop_all(o);
}
bool isStreamComplete(void *item)
{
    if (item == (void *)(long)0XDEADBEEF)
    {
        return true;
    }
    return false;
}
void pop_all(Observable *o)
{
    if (o->complete)
    {
        return;
    }
    List *data = o->data;
    if (list_isempty(data))
    {
        return;
    }

    if (o->emit_handler)
    {
        List *temp = o->emit_handler->func(data, o->emit_handler->ctx);
        Observable *lastpipe = o->pipe;
        while (lastpipe != NULL && lastpipe->emit_handler)
        {
            temp = lastpipe->emit_handler->func(temp, lastpipe->emit_handler->ctx);
            lastpipe = lastpipe->pipe;
        }
        o->data = temp;
        data = temp;
    }

    // printf("Function pointer in popall: %p\n", (void *)o->subscriber);

    while (!list_isempty(data))
    {
        void *d = popstart(data); // pop from front (FIFO)
        if (isStreamComplete(d))
        {
            o->complete = true;
            return;
        }

        if (!o->subscriber)
        {
            // printf("No subscriber\n");
            continue;
        }

        o->subscriber(d);
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
    push_observable(ictx->o, (void *)(long)(ictx->ms * ictx->amt));

    if (ms == 0) // Dont need to emit again?
    {
        return;
    }
    ictx->amt++;
    insert_task_in(ms, ctx, addnext);
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

Observable *timer(long ms, int period)
{
    Observable *result = create_observable();
    IntervalCtx *ctx = malloc(sizeof(IntervalCtx));
    ctx->ms = period;
    ctx->o = result;
    ctx->amt = 0;
    insert_task_in(ms, ctx, addnext);
    return result;
}

Observable *defer(FactoryFn factory)
{
    Observable *result = create_observable();
    result->on_subscription = factory;
    return result;
}

Observable *pipe(Observable *self, int count, ...)
{
    va_list args;
    va_start(args, count);

    Observable *last = self;
    for (int i = 0; i < count; ++i)
    {
        void *raw = va_arg(args, void *);
        Observable *next = create_observable();
        last->emit_handler = raw;
        last->pipe = next;
        last = next;
    }
    va_end(args);
    return self;
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
    TakeUntilCtx *f = ctx;
    List *result = init_list();
    printf("applying\n");
    for (int i = 0; i < data->size; ++i)
    {
        void *item = list_get(data, i);
        if (isStreamComplete(item))
        {
            break;
        }
        printf("Comparing: %d, %d\n", (long)item, (long)f->endat);
        fflush(stdout);
        push_back(result, item);

        if (f->pred(item, f->endat))
        {
            push_back(result, (void *)(long)0XDEADBEEF); // DEADBEEF cannot be allocated by 64 bit linux or windows kernels
        }
    }
    return result;
}
bool pointer_comp(void *a, void *b)
{
    return a == b;
}
Query *takeUntil(void *comp)
{
    TakeUntilCtx *ctx = malloc(sizeof(TakeUntilCtx));
    ctx->pred = pointer_comp;
    ctx->endat = comp;
    printf("Endat: %d\n", (long)ctx->endat);
    Query *result = malloc(sizeof(Query));
    result->func = takeuntil_apply;
    result->ctx = ctx;
    return result;
}

static List *map_apply(List *data, void *ctx)
{
    MapCtx *m = ctx;
    List *result = init_list();
    for (int i = 0; i < data->size; ++i)
    {
        void *item = list_get(data, i);
        if (isStreamComplete(item))
        {
            break;
        }
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
static List *mapTo_apply(List *data, void *ctx)
{
    MapToCtx *m = ctx;
    List *result = init_list();
    for (int i = 0; i < data->size; ++i)
    {
        void *item = list_get(data, i);
        if (isStreamComplete(item))
        {
            break;
        }
        push_back(result, m->to); //Just overwrite with the to
    }
    return result;
}
Query *mapTo(void *newitem)
{
    MapToCtx *ctx = malloc(sizeof(*ctx));
    Query *q = malloc(sizeof(*q));
    q->func = mapTo_apply;
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
        if (isStreamComplete(item))
        {
            break;
        }
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
    void *accum = m->accum;
    for (int i = 0; i < data->size; ++i)
    {
        void *item = list_get(data, i);
        if (isStreamComplete(item))
        {
            break;
        }
        void *r = m->pred(accum, item);
        push_back(result, r);
        accum = r;
    }
    m->accum = accum;

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

Query *scanfrom(AccumulatorFunction accumulator, void *from)
{
    ScanCtx *ctx = malloc(sizeof(*ctx));
    ctx->pred = accumulator;
    ctx->accum = from;
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
        if (isStreamComplete(item))
        {
            break;
        }
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
    push_back(result, peek(data));
    if (isStreamComplete(list_get(result, 0)))
    {
        pop(result);
        push_back(result, list_get(data, data->size - 2));
    }
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
        if (isStreamComplete(peek(observables[i]->data))) // Last element the complete token?
        {
            --msize; // Ignore the token
        }

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