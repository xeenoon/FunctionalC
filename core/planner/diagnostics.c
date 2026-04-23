#include "diagnostics.h"

#include <stdlib.h>
#include <string.h>

void rx_diagnostic_bag_init(RxDiagnosticBag *bag)
{
    memset(bag, 0, sizeof(*bag));
}

void rx_diagnostic_bag_reset(RxDiagnosticBag *bag)
{
    free(bag->items);
    memset(bag, 0, sizeof(*bag));
}

bool rx_diagnostic_bag_append(RxDiagnosticBag *bag, RxDiagnostic diagnostic)
{
    if (bag->count == bag->capacity)
    {
        int next_capacity = bag->capacity == 0 ? 8 : bag->capacity * 2;
        RxDiagnostic *items = realloc(bag->items, (size_t)next_capacity * sizeof(*items));
        if (items == NULL)
        {
            return false;
        }
        bag->items = items;
        bag->capacity = next_capacity;
    }

    bag->items[bag->count++] = diagnostic;
    if (diagnostic.level == RX_DIAGNOSTIC_ERROR)
    {
        bag->has_error = true;
    }
    return true;
}
