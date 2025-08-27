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
typedef struct { BooleanFunction pred; } FilterCtx;
typedef struct { ModifierFunction pred; } MapCtx;
typedef struct { AccumulatorFunction pred; void *accum;} ScanCtx;
typedef struct { AccumulatorFunction pred; void *accum;} ReduceCtx;
typedef struct { Observable *o; long ms; int amt;} IntervalCtx;

typedef List *(*PipeFunc)(List *data, void *ctx);

typedef struct Observable
{
    List *data;
    Subscriber subscriber;
    Query *emit_handler;
    List *pipes;
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
Query *reduce(AccumulatorFunction accum);
Query *last();
Observable *zip(int count, ...);
Query *mergeMap(Observable *o);
Observable *interval(long ms);


#endif