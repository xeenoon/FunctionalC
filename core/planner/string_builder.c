#include "string_builder.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool rx_string_builder_reserve(RxStringBuilder *builder, size_t extra)
{
    size_t required = builder->length + extra + 1;
    if (required <= builder->capacity)
    {
        return true;
    }

    size_t next_capacity = builder->capacity == 0 ? 256 : builder->capacity;
    while (next_capacity < required)
    {
        next_capacity *= 2;
    }

    char *data = realloc(builder->data, next_capacity);
    if (data == NULL)
    {
        return false;
    }

    builder->data = data;
    builder->capacity = next_capacity;
    return true;
}

void rx_string_builder_init(RxStringBuilder *builder)
{
    memset(builder, 0, sizeof(*builder));
}

void rx_string_builder_reset(RxStringBuilder *builder)
{
    free(builder->data);
    memset(builder, 0, sizeof(*builder));
}

bool rx_string_builder_append(RxStringBuilder *builder, const char *text)
{
    size_t length = strlen(text);
    if (!rx_string_builder_reserve(builder, length))
    {
        return false;
    }

    memcpy(builder->data + builder->length, text, length + 1);
    builder->length += length;
    return true;
}

bool rx_string_builder_append_format(RxStringBuilder *builder, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (length < 0)
    {
        va_end(args);
        return false;
    }

    if (!rx_string_builder_reserve(builder, (size_t)length))
    {
        va_end(args);
        return false;
    }

    vsnprintf(builder->data + builder->length, builder->capacity - builder->length, format, args);
    builder->length += (size_t)length;
    va_end(args);
    return true;
}
