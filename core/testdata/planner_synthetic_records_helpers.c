#include <stdint.h>

void *add1(void *raw)
{
    intptr_t value = (intptr_t)raw;
    return (void *)(value + 1);
}

void *sum(void *raw_accum, void *raw_next)
{
    intptr_t accum = (intptr_t)raw_accum;
    intptr_t next = (intptr_t)raw_next;
    return (void *)(accum + next);
}
