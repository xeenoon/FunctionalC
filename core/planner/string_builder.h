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

bool rx_string_builder_append_c_profile_prelude(RxStringBuilder *builder);
bool rx_string_builder_append_c_profile_slots_header(RxStringBuilder *builder, const char *pipeline_name);
bool rx_string_builder_append_c_profile_slot_item(RxStringBuilder *builder, const char *label, int index);
bool rx_string_builder_append_c_profile_suffix_prefix(RxStringBuilder *builder, const char *pipeline_name);
bool rx_string_builder_append_c_profile_suffix_tail(RxStringBuilder *builder);

bool rx_string_builder_append_c_loop_external_buffer(RxStringBuilder *builder);
bool rx_string_builder_append_c_loop_external_window_header(RxStringBuilder *builder, int window_size, int inner_n);
bool rx_string_builder_append_c_loop_external_window_item(RxStringBuilder *builder, int index, intptr_t offset);
bool rx_string_builder_append_c_loop_external_window_value(RxStringBuilder *builder);
bool rx_string_builder_append_c_loop_range_header(RxStringBuilder *builder);
bool rx_string_builder_append_c_loop_range_value_src(RxStringBuilder *builder);
bool rx_string_builder_append_c_loop_break_flag(RxStringBuilder *builder);

bool rx_string_builder_append_c_extern_pair_map(RxStringBuilder *builder, const char *name);
bool rx_string_builder_append_c_extern_triple_map(RxStringBuilder *builder, const char *name);
bool rx_string_builder_append_c_extern_map(RxStringBuilder *builder, const char *name);
bool rx_string_builder_append_c_extern_map_into(RxStringBuilder *builder, const char *name);
bool rx_string_builder_append_c_extern_predicate(RxStringBuilder *builder, const char *name);
bool rx_string_builder_append_c_extern_accum(RxStringBuilder *builder, const char *name);
bool rx_string_builder_append_c_extern_accum_mut(RxStringBuilder *builder, const char *name);

bool rx_string_builder_append_c_state_slot_void_symbol(RxStringBuilder *builder, const char *name, const char *symbol_name);
bool rx_string_builder_append_c_state_slot_void_ptr(RxStringBuilder *builder, const char *name, uintptr_t pointer_value);
bool rx_string_builder_append_c_state_slot_void_intptr(RxStringBuilder *builder, const char *name, intptr_t value);
bool rx_string_builder_append_c_state_slot_void_null(RxStringBuilder *builder, const char *name);
bool rx_string_builder_append_c_state_slot_bool(RxStringBuilder *builder, const char *name, bool value);
bool rx_string_builder_append_c_state_slot_intptr(RxStringBuilder *builder, const char *name, intptr_t value);
bool rx_string_builder_append_c_state_slot_intptr_zero(RxStringBuilder *builder, const char *name);
bool rx_string_builder_append_c_state_slot_assign_ptr(RxStringBuilder *builder, const char *name, const char *expr);
bool rx_string_builder_append_c_state_slot_assign_intptr(RxStringBuilder *builder, const char *name, const char *expr);

bool rx_string_builder_append_c_segment_signature(RxStringBuilder *builder, const char *pipeline_name, bool uses_external_buffer);
bool rx_string_builder_append_c_return_last(RxStringBuilder *builder, const char *has_name, const char *value_name);
bool rx_string_builder_append_c_return_name(RxStringBuilder *builder, const char *name, bool is_ptr);
bool rx_string_builder_append_c_return_zero(RxStringBuilder *builder);
bool rx_string_builder_append_c_function_end(RxStringBuilder *builder);

bool rx_string_builder_append_c_main_external_buffer_stub(RxStringBuilder *builder);
bool rx_string_builder_append_c_main_benchmark_prefix(RxStringBuilder *builder);
bool rx_string_builder_append_c_main_run_call(RxStringBuilder *builder, const char *pipeline_name);
bool rx_string_builder_append_c_main_benchmark_suffix(RxStringBuilder *builder);

#endif
