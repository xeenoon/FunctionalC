#ifndef OBSERVABLE_H
#define OBSERVABLE_H

#include "list.h"
#include <stdarg.h>
typedef struct Observable Observable;  // forward declaration
typedef struct Query Query;  // forward declaration

typedef void (*Subscriber)(void *arg);
typedef bool (*BooleanFunction)(void*);
typedef void* (*ModifierFunction)(void*);
typedef void* (*AccumulatorFunction)(void*, void*);
typedef bool (*ComparisonFunction)(void*, void*);
typedef struct { BooleanFunction pred; } FilterCtx;
typedef struct { ModifierFunction pred; } MapCtx;
typedef struct { AccumulatorFunction pred; void *accum;} ScanCtx;
typedef struct { AccumulatorFunction pred; void *accum;} ReduceCtx;
typedef struct { ComparisonFunction pred; void *endat;} TakeUntilCtx;
typedef struct { Observable *o; long ms; int amt;} IntervalCtx;

typedef List *(*PipeFunc)(List *data, void *ctx);
typedef Observable *(*FactoryFn)();

typedef struct Observable
{
    List *data;
    bool complete;
    Subscriber subscriber;
    Query *emit_handler;
    Observable *pipe;
    FactoryFn on_subscription;
} Observable;

typedef struct Query {
    List *(*func)(List *data, void *ctx);
    void *ctx;
} Query;

Observable *create_observable();
void subscribe(Observable *o, Subscriber subscriber);
void push_observable(Observable *o, void *data);
void pop_all(Observable *o);
Observable *range(int min, int max); //Inclusive range
Observable *pipe(Observable *self, int count, ...);
Query *filter(BooleanFunction boolfunc);
Query *map(ModifierFunction m);
Query *scan(AccumulatorFunction accum);
Query *scanfrom(AccumulatorFunction accum, void* from);
Query *reduce(AccumulatorFunction accum);
Query *last();
Observable *zip(int count, ...);
Query *mergeMap(Observable *o);
Observable *interval(long ms);
Query *takeUntil(void *comp);
Observable *of(int count, ...);
Observable *never();
Observable *empty();
Observable *from(List *data);
Observable *timer(long ms, int period);


#endif