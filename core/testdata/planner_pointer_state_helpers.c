#include <stdint.h>
#include <stdlib.h>

void *box_value(void *raw)
{
    intptr_t *box = malloc(sizeof(*box));
    if (box == NULL)
    {
        return NULL;
    }
    *box = (intptr_t)raw;
    return box;
}

void *sum_boxed(void *raw_accum, void *raw_next)
{
    intptr_t *accum = raw_accum;
    intptr_t *next = raw_next;
    if (next == NULL)
    {
        return accum;
    }
    if (accum == NULL)
    {
        accum = malloc(sizeof(*accum));
        if (accum == NULL)
        {
            free(next);
            return NULL;
        }
        *accum = 0;
    }
    *accum += *next;
    free(next);
    return accum;
}

void *unbox_value(void *raw)
{
    intptr_t *box = raw;
    return box != NULL ? (void *)*box : NULL;
}

void *sum(void *raw_accum, void *raw_next)
{
    intptr_t accum = (intptr_t)raw_accum;
    intptr_t next = (intptr_t)raw_next;
    return (void *)(accum + next);
}
