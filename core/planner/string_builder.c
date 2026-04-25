#include "string_builder.h"

#include <inttypes.h>
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

bool rx_string_builder_append_c_profile_prelude(RxStringBuilder *builder)
{
    return rx_string_builder_append(
        builder,
        "#ifdef RX_PLANNER_PROFILE\n"
        "typedef struct {\n"
        "    const char *name;\n"
        "    uint64_t hits;\n"
        "    uint64_t total_ns;\n"
        "} RxProfileSlot;\n\n");
}

bool rx_string_builder_append_c_profile_slots_header(RxStringBuilder *builder, const char *pipeline_name)
{
    return rx_string_builder_append_format(builder, "static RxProfileSlot rx_profile_slots_%s[] = {\n", pipeline_name);
}

bool rx_string_builder_append_c_profile_slot_item(RxStringBuilder *builder, const char *label, int index)
{
    return rx_string_builder_append_format(builder, "    {\"%s[%d]\", 0, 0},\n", label, index);
}

bool rx_string_builder_append_c_profile_suffix_prefix(RxStringBuilder *builder, const char *pipeline_name)
{
    return rx_string_builder_append(
               builder,
               "};\n"
               "static uint64_t rx_profile_diff_ns(struct timespec start, struct timespec end) {\n"
               "    return (uint64_t)(end.tv_sec - start.tv_sec) * 1000000000ULL + (uint64_t)(end.tv_nsec - start.tv_nsec);\n"
               "}\n"
               "#define RX_PROFILE_STAGE_BEGIN(ID) struct timespec __rx_stage_start_##ID, __rx_stage_end_##ID; clock_gettime(CLOCK_MONOTONIC, &__rx_stage_start_##ID)\n"
               "#define RX_PROFILE_STAGE_END(ID) do { clock_gettime(CLOCK_MONOTONIC, &__rx_stage_end_##ID); rx_profile_slots_")
        && rx_string_builder_append(builder, pipeline_name)
        && rx_string_builder_append(
               builder,
               "[ID].hits += 1; rx_profile_slots_")
        && rx_string_builder_append(builder, pipeline_name);
}

bool rx_string_builder_append_c_profile_suffix_tail(RxStringBuilder *builder)
{
    return rx_string_builder_append(
        builder,
        "[ID].total_ns += rx_profile_diff_ns(__rx_stage_start_##ID, __rx_stage_end_##ID); } while (0)\n"
        "#else\n"
        "#define RX_PROFILE_STAGE_BEGIN(ID) do { } while (0)\n"
        "#define RX_PROFILE_STAGE_END(ID) do { } while (0)\n"
        "#endif\n\n");
}

bool rx_string_builder_append_c_loop_external_buffer(RxStringBuilder *builder)
{
    return rx_string_builder_append(builder, "    for (intptr_t src = 0; src < N; ++src) {\n")
        && rx_string_builder_append(builder, "        intptr_t value = (intptr_t)records[src];\n");
}

bool rx_string_builder_append_c_loop_external_window_header(RxStringBuilder *builder, int window_size, int inner_n)
{
    return rx_string_builder_append_format(builder, "    void *window_items[%d];\n", window_size)
        && rx_string_builder_append_format(builder, "    for (intptr_t src = 0; src + (intptr_t)%d <= N; ++src) {\n", inner_n);
}

bool rx_string_builder_append_c_loop_external_window_item(RxStringBuilder *builder, int index, intptr_t offset)
{
    return rx_string_builder_append_format(builder, "        window_items[%d] = records[src + %" PRIdPTR "];\n", index, offset);
}

bool rx_string_builder_append_c_loop_external_window_value(RxStringBuilder *builder)
{
    return rx_string_builder_append(builder, "        intptr_t value = (intptr_t)window_items;\n");
}

bool rx_string_builder_append_c_loop_range_header(RxStringBuilder *builder)
{
    return rx_string_builder_append(builder, "    for (intptr_t src = 1; src <= N; ++src) {\n");
}

bool rx_string_builder_append_c_loop_range_value_src(RxStringBuilder *builder)
{
    return rx_string_builder_append(builder, "        intptr_t value = src;\n");
}

bool rx_string_builder_append_c_loop_break_flag(RxStringBuilder *builder)
{
    return rx_string_builder_append(builder, "        bool should_break = false;\n");
}

bool rx_string_builder_append_c_extern_pair_map(RxStringBuilder *builder, const char *name)
{
    return rx_string_builder_append_format(builder, "extern void *%s(void *left_raw, void *right_raw);\n", name);
}

bool rx_string_builder_append_c_extern_triple_map(RxStringBuilder *builder, const char *name)
{
    return rx_string_builder_append_format(builder, "extern void *%s(void *zipped_left_raw, void *zipped_right_raw, void *right_raw);\n", name);
}

bool rx_string_builder_append_c_extern_map(RxStringBuilder *builder, const char *name)
{
    return rx_string_builder_append_format(builder, "extern void *%s(void *raw);\n", name);
}

bool rx_string_builder_append_c_extern_map_into(RxStringBuilder *builder, const char *name)
{
    return rx_string_builder_append_format(builder, "extern void %s(void *raw_out, void *raw);\n", name);
}

bool rx_string_builder_append_c_extern_predicate(RxStringBuilder *builder, const char *name)
{
    return rx_string_builder_append_format(builder, "extern bool %s(void *raw);\n", name);
}

bool rx_string_builder_append_c_extern_accum(RxStringBuilder *builder, const char *name)
{
    return rx_string_builder_append_format(builder, "extern void *%s(void *raw_accum, void *raw_next);\n", name);
}

bool rx_string_builder_append_c_extern_accum_mut(RxStringBuilder *builder, const char *name)
{
    return rx_string_builder_append_format(builder, "extern void %s(void *raw_accum, void *raw_next);\n", name);
}

bool rx_string_builder_append_c_state_slot_void_symbol(RxStringBuilder *builder, const char *name, const char *symbol_name)
{
    return rx_string_builder_append_format(builder, "    void *%s = %s;\n", name, symbol_name);
}

bool rx_string_builder_append_c_state_slot_void_ptr(RxStringBuilder *builder, const char *name, uintptr_t pointer_value)
{
    return rx_string_builder_append_format(builder, "    void *%s = (void *)%" PRIuPTR ";\n", name, pointer_value);
}

bool rx_string_builder_append_c_state_slot_void_intptr(RxStringBuilder *builder, const char *name, intptr_t value)
{
    return rx_string_builder_append_format(builder, "    void *%s = (void *)(intptr_t)%" PRIdPTR ";\n", name, value);
}

bool rx_string_builder_append_c_state_slot_void_null(RxStringBuilder *builder, const char *name)
{
    return rx_string_builder_append_format(builder, "    void *%s = NULL;\n", name);
}

bool rx_string_builder_append_c_state_slot_bool(RxStringBuilder *builder, const char *name, bool value)
{
    return rx_string_builder_append_format(builder, "    bool %s = %s;\n", name, value ? "true" : "false");
}

bool rx_string_builder_append_c_state_slot_intptr(RxStringBuilder *builder, const char *name, intptr_t value)
{
    return rx_string_builder_append_format(builder, "    intptr_t %s = %" PRIdPTR ";\n", name, value);
}

bool rx_string_builder_append_c_state_slot_intptr_zero(RxStringBuilder *builder, const char *name)
{
    return rx_string_builder_append_format(builder, "    intptr_t %s = 0;\n", name);
}

bool rx_string_builder_append_c_state_slot_assign_ptr(RxStringBuilder *builder, const char *name, const char *expr)
{
    return rx_string_builder_append_format(builder, "        %s = %s;\n", name, expr);
}

bool rx_string_builder_append_c_state_slot_assign_intptr(RxStringBuilder *builder, const char *name, const char *expr)
{
    return rx_string_builder_append_format(builder, "        %s = (intptr_t)%s;\n", name, expr);
}

bool rx_string_builder_append_c_segment_signature(RxStringBuilder *builder, const char *pipeline_name, bool uses_external_buffer)
{
    return rx_string_builder_append_format(
        builder,
        uses_external_buffer
            ? "intptr_t run_%s(void **records, intptr_t N) {\n"
            : "intptr_t run_%s(intptr_t N) {\n",
        pipeline_name);
}

bool rx_string_builder_append_c_return_last(RxStringBuilder *builder, const char *has_name, const char *value_name)
{
    return rx_string_builder_append_format(builder, "    return %s ? %s : 0;\n", has_name, value_name);
}

bool rx_string_builder_append_c_return_name(RxStringBuilder *builder, const char *name, bool is_ptr)
{
    return rx_string_builder_append_format(builder, is_ptr ? "    return (intptr_t)%s;\n" : "    return %s;\n", name);
}

bool rx_string_builder_append_c_return_zero(RxStringBuilder *builder)
{
    return rx_string_builder_append(builder, "    return 0;\n");
}

bool rx_string_builder_append_c_function_end(RxStringBuilder *builder)
{
    return rx_string_builder_append(builder, "}\n\n");
}

bool rx_string_builder_append_c_main_external_buffer_stub(RxStringBuilder *builder)
{
    return rx_string_builder_append(
        builder,
        "int main(void) {\n"
        "    fprintf(stderr, \"external_buffer pipelines must be embedded via --no-main\\n\");\n"
        "    return 1;\n"
        "}\n");
}

bool rx_string_builder_append_c_main_benchmark_prefix(RxStringBuilder *builder)
{
    return rx_string_builder_append(
        builder,
        "int main(int argc, char **argv) {\n"
        "    intptr_t N = argc > 1 ? (intptr_t)strtoll(argv[1], NULL, 10) : 0;\n"
        "    int RUNS = argc > 2 ? atoi(argv[2]) : 1;\n"
        "    int64_t total_ns = 0;\n"
        "    intptr_t result = 0;\n"
        "    for (int run = 0; run < RUNS; ++run) {\n"
        "        struct timespec start, end;\n"
        "        clock_gettime(CLOCK_MONOTONIC, &start);\n");
}

bool rx_string_builder_append_c_main_run_call(RxStringBuilder *builder, const char *pipeline_name)
{
    return rx_string_builder_append_format(builder, "        result = run_%s(N);\n", pipeline_name);
}

bool rx_string_builder_append_c_main_benchmark_suffix(RxStringBuilder *builder)
{
    return rx_string_builder_append(
        builder,
        "        clock_gettime(CLOCK_MONOTONIC, &end);\n"
        "        total_ns += (int64_t)(end.tv_sec - start.tv_sec) * 1000000000LL + (int64_t)(end.tv_nsec - start.tv_nsec);\n"
        "    }\n"
        "    printf(\"{\\\"result\\\": %" PRIdPTR ", \\\"average_ms\\\": %.5f, \\\"runs\\\": %d, \\\"n\\\": %" PRIdPTR "}\\n\", result, (double)total_ns / RUNS / 1e6, RUNS, N);\n"
        "    return 0;\n"
        "}\n");
}
