#ifndef CORE_PLANNER_STRING_BUILDER_H
#define CORE_PLANNER_STRING_BUILDER_H

#include "common.h"

typedef struct
{
    char *data;
    size_t length;
    size_t capacity;
} RxStringBuilder;

void rx_string_builder_init(RxStringBuilder *builder);
void rx_string_builder_reset(RxStringBuilder *builder);
bool rx_string_builder_append(RxStringBuilder *builder, const char *text);
bool rx_string_builder_append_format(RxStringBuilder *builder, const char *format, ...);

#endif
