#include "c_codegen.h"

#include <inttypes.h>

static bool emit_function_declarations(
    const RxLoweredPipeline *pipeline,
    RxStringBuilder *out)
{
    for (int index = 0; index < pipeline->op_count; ++index)
    {
        const RxLoopOp *op = &pipeline->ops[index];
        if (op->stage == NULL || op->stage->primary_argument.kind != RX_BINDING_FUNCTION_NAME)
        {
            continue;
        }

        const char *name = op->stage->primary_argument.as.function_name;
        switch (op->kind)
        {
            case RX_OP_CALL_PAIR_MAP:
                if (!rx_string_builder_append_format(out, "extern void *%s(void *left_raw, void *right_raw);\n", name))
                {
                    return false;
                }
                break;
            case RX_OP_CALL_MAP:
            case RX_OP_CALL_MAP_CHAIN:
            case RX_OP_CALL_MAP_TO:
            case RX_OP_APPLY_DISTINCT:
            case RX_OP_APPLY_DISTINCT_UNTIL_CHANGED:
                if (!rx_string_builder_append_format(out, "extern void *%s(void *raw);\n", name))
                {
                    return false;
                }
                break;
            case RX_OP_CALL_FILTER:
            case RX_OP_APPLY_TAKE_WHILE:
            case RX_OP_APPLY_SKIP_WHILE:
                if (!rx_string_builder_append_format(out, "extern bool %s(void *raw);\n", name))
                {
                    return false;
                }
                break;
            case RX_OP_CALL_SCAN:
            case RX_OP_CALL_REDUCE:
                if (!rx_string_builder_append_format(out, "extern void *%s(void *raw_accum, void *raw_next);\n", name))
                {
                    return false;
                }
                break;
            default:
                break;
        }
    }
    return true;
}

static bool emit_state_slot(RxStringBuilder *out, const RxStateSlot *slot)
{
    switch (slot->kind)
    {
        case RX_STATE_SKIP_WHILE_PASSED:
        case RX_STATE_HAS_LAST_VALUE:
        case RX_STATE_HAS_LAST_KEY:
            return rx_string_builder_append_format(
                out,
                "    bool %s = %s;\n",
                slot->name,
                slot->initial_value.kind == RX_LITERAL_INT && slot->initial_value.as.int_value != 0 ? "true" : "false");
        default:
            if (slot->initial_value.kind == RX_LITERAL_INT)
            {
                return rx_string_builder_append_format(
                    out,
                    "    intptr_t %s = %" PRIdPTR ";\n",
                    slot->name,
                    (intptr_t)slot->initial_value.as.int_value);
            }
            if (slot->initial_value.kind == RX_LITERAL_LONG)
            {
                return rx_string_builder_append_format(
                    out,
                    "    intptr_t %s = %" PRIdPTR ";\n",
                    slot->name,
                    (intptr_t)slot->initial_value.as.long_value);
            }
            return rx_string_builder_append_format(out, "    intptr_t %s = 0;\n", slot->name);
    }
}

static bool emit_loop_body(
    const RxLoweredPipeline *pipeline,
    RxStringBuilder *out)
{
    if (pipeline->source_kind == RX_LOOP_SOURCE_ZIP_RANGE)
    {
        if (!rx_string_builder_append(out, "    for (intptr_t src = 1; src <= N; ++src) {\n"))
        {
            return false;
        }
        if (!rx_string_builder_append(out, "        intptr_t left = src;\n"))
        {
            return false;
        }
        if (!rx_string_builder_append(out, "        intptr_t right = src;\n"))
        {
            return false;
        }
        if (!rx_string_builder_append(out, "        intptr_t value = 0;\n"))
        {
            return false;
        }
    }
    else if (!rx_string_builder_append(out, "    for (intptr_t src = 1; src <= N; ++src) {\n"))
    {
        return false;
    }
    if (pipeline->source_kind != RX_LOOP_SOURCE_ZIP_RANGE
        && !rx_string_builder_append(out, "        intptr_t value = src;\n"))
    {
        return false;
    }

    for (int index = 0; index < pipeline->op_count; ++index)
    {
        const RxLoopOp *op = &pipeline->ops[index];
        const RxPlannedStage *stage = op->stage;
        const char *fn = stage != NULL && stage->primary_argument.kind == RX_BINDING_FUNCTION_NAME
            ? stage->primary_argument.as.function_name
            : NULL;

        switch (op->kind)
        {
            case RX_OP_CALL_PAIR_MAP:
                if (!rx_string_builder_append_format(
                        out,
                        "        value = (intptr_t)%s((void *)(intptr_t)left, (void *)(intptr_t)right);\n",
                        fn))
                {
                    return false;
                }
                break;
            case RX_OP_CALL_MAP:
                if (!rx_string_builder_append_format(
                        out,
                        "        value = (intptr_t)%s((void *)(intptr_t)value);\n",
                        fn))
                {
                    return false;
                }
                break;
            case RX_OP_CALL_FILTER:
                if (!rx_string_builder_append_format(
                        out,
                        "        if (!%s((void *)(intptr_t)value)) { continue; }\n",
                        fn))
                {
                    return false;
                }
                break;
            case RX_OP_CALL_SCAN:
                if (!rx_string_builder_append_format(
                        out,
                        "        %s = (intptr_t)%s((void *)(intptr_t)%s, (void *)(intptr_t)value);\n",
                        pipeline->state_slots[op->state_slot_index].name,
                        fn,
                        pipeline->state_slots[op->state_slot_index].name))
                {
                    return false;
                }
                if (!rx_string_builder_append_format(
                        out,
                        "        value = %s;\n",
                        pipeline->state_slots[op->state_slot_index].name))
                {
                    return false;
                }
                break;
            case RX_OP_CALL_REDUCE:
                if (!rx_string_builder_append_format(
                        out,
                        "        %s = (intptr_t)%s((void *)(intptr_t)%s, (void *)(intptr_t)value);\n",
                        pipeline->state_slots[op->state_slot_index].name,
                        fn,
                        pipeline->state_slots[op->state_slot_index].name))
                {
                    return false;
                }
                if (!rx_string_builder_append(out, "        continue;\n"))
                {
                    return false;
                }
                break;
            case RX_OP_CALL_MAP_TO:
                if (!rx_string_builder_append_format(
                        out,
                        "        value = %" PRIdPTR ";\n",
                        (intptr_t)stage->primary_argument.as.literal.as.int_value))
                {
                    return false;
                }
                break;
            case RX_OP_APPLY_TAKE:
                if (!rx_string_builder_append_format(
                        out,
                        "        if (%s >= %" PRIdPTR ") { break; }\n",
                        pipeline->state_slots[op->state_slot_index].name,
                        (intptr_t)stage->primary_argument.as.literal.as.int_value))
                {
                    return false;
                }
                if (!rx_string_builder_append_format(
                        out,
                        "        %s += 1;\n",
                        pipeline->state_slots[op->state_slot_index].name))
                {
                    return false;
                }
                break;
            case RX_OP_APPLY_SKIP:
                if (!rx_string_builder_append_format(
                        out,
                        "        if (%s < %" PRIdPTR ") { %s += 1; continue; }\n",
                        pipeline->state_slots[op->state_slot_index].name,
                        (intptr_t)stage->primary_argument.as.literal.as.int_value,
                        pipeline->state_slots[op->state_slot_index].name))
                {
                    return false;
                }
                break;
            case RX_OP_APPLY_TAKE_WHILE:
                if (!rx_string_builder_append_format(
                        out,
                        "        if (!%s((void *)(intptr_t)value)) { break; }\n",
                        fn))
                {
                    return false;
                }
                break;
            case RX_OP_APPLY_SKIP_WHILE:
                if (!rx_string_builder_append_format(
                        out,
                        "        if (!%s && %s((void *)(intptr_t)value)) { continue; }\n",
                        pipeline->state_slots[op->state_slot_index].name,
                        fn))
                {
                    return false;
                }
                if (!rx_string_builder_append_format(
                        out,
                        "        %s = true;\n",
                        pipeline->state_slots[op->state_slot_index].name))
                {
                    return false;
                }
                break;
            case RX_OP_APPLY_DISTINCT_UNTIL_CHANGED:
                if (!rx_string_builder_append_format(
                        out,
                        "        intptr_t key_%d = (intptr_t)%s((void *)(intptr_t)value);\n",
                        index,
                        fn))
                {
                    return false;
                }
                if (!rx_string_builder_append_format(
                        out,
                        "        if (%s && key_%d == %s) { continue; }\n",
                        pipeline->state_slots[op->aux_state_slot_index].name,
                        index,
                        pipeline->state_slots[op->state_slot_index].name))
                {
                    return false;
                }
                if (!rx_string_builder_append_format(
                        out,
                        "        %s = key_%d;\n",
                        pipeline->state_slots[op->state_slot_index].name,
                        index))
                {
                    return false;
                }
                if (!rx_string_builder_append_format(
                        out,
                        "        %s = true;\n",
                        pipeline->state_slots[op->aux_state_slot_index].name))
                {
                    return false;
                }
                break;
            case RX_OP_APPLY_LAST:
                if (!rx_string_builder_append_format(
                        out,
                        "        %s = value;\n",
                        pipeline->state_slots[op->state_slot_index].name))
                {
                    return false;
                }
                if (!rx_string_builder_append_format(
                        out,
                        "        %s = true;\n",
                        pipeline->state_slots[op->aux_state_slot_index].name))
                {
                    return false;
                }
                break;
            case RX_OP_APPLY_FIRST:
                if (!rx_string_builder_append(out, "        return value;\n"))
                {
                    return false;
                }
                break;
            default:
                return false;
        }
    }

    return rx_string_builder_append(out, "    }\n");
}

bool rx_emit_c_segment_function(
    const RxLoweredPipeline *pipeline,
    const RxCCodegenOptions *options,
    RxStringBuilder *out,
    RxDiagnosticBag *diagnostics)
{
    (void)options;
    (void)diagnostics;

    if (!rx_string_builder_append_format(
            out,
            "static intptr_t run_%s(intptr_t N) {\n",
            pipeline->pipeline_name != NULL ? pipeline->pipeline_name : "pipeline"))
    {
        return false;
    }

    for (int index = 0; index < pipeline->state_slot_count; ++index)
    {
        if (!emit_state_slot(out, &pipeline->state_slots[index]))
        {
            return false;
        }
    }

    if (!emit_loop_body(pipeline, out))
    {
        return false;
    }

    const RxLoopOp *last_op = pipeline->op_count > 0 ? &pipeline->ops[pipeline->op_count - 1] : NULL;
    if (last_op != NULL && last_op->kind == RX_OP_CALL_REDUCE)
    {
        if (!rx_string_builder_append_format(
                out,
                "    return %s;\n",
                pipeline->state_slots[last_op->state_slot_index].name))
        {
            return false;
        }
    }
    else if (last_op != NULL && last_op->kind == RX_OP_APPLY_LAST)
    {
        if (!rx_string_builder_append_format(
                out,
                "    return %s ? %s : 0;\n",
                pipeline->state_slots[last_op->aux_state_slot_index].name,
                pipeline->state_slots[last_op->state_slot_index].name))
        {
            return false;
        }
    }
    else
    {
        if (!rx_string_builder_append(out, "    return 0;\n"))
        {
            return false;
        }
    }

    return rx_string_builder_append(out, "}\n\n");
}

bool rx_emit_c_program(
    const RxLoweredProgram *program,
    const RxCCodegenOptions *options,
    RxStringBuilder *out,
    RxDiagnosticBag *diagnostics)
{
    (void)diagnostics;

    if (program == NULL || program->pipeline_count <= 0)
    {
        return true;
    }

    const RxLoweredPipeline *pipeline = &program->pipelines[0];

    if (!rx_string_builder_append(out, "#define _POSIX_C_SOURCE 200809L\n"))
    {
        return false;
    }
    if (!rx_string_builder_append(out, "#include <inttypes.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <time.h>\n\n"))
    {
        return false;
    }

    if (options != NULL && options->header_path != NULL)
    {
        if (!rx_string_builder_append_format(out, "#include \"%s\"\n\n", options->header_path))
        {
            return false;
        }
    }
    else if (!emit_function_declarations(pipeline, out))
    {
        return false;
    }

    if (!rx_emit_c_segment_function(pipeline, options, out, diagnostics))
    {
        return false;
    }

    if (options == NULL || options->emit_main)
    {
        if (!rx_string_builder_append(
                out,
                "int main(int argc, char **argv) {\n"
                "    intptr_t N = argc > 1 ? (intptr_t)strtoll(argv[1], NULL, 10) : 0;\n"
                "    int RUNS = argc > 2 ? atoi(argv[2]) : 1;\n"
                "    int64_t total_ns = 0;\n"
                "    intptr_t result = 0;\n"
                "    for (int run = 0; run < RUNS; ++run) {\n"
                "        struct timespec start, end;\n"
                "        clock_gettime(CLOCK_MONOTONIC, &start);\n"))
        {
            return false;
        }
        if (!rx_string_builder_append_format(
                out,
                "        result = run_%s(N);\n",
                pipeline->pipeline_name != NULL ? pipeline->pipeline_name : "pipeline"))
        {
            return false;
        }
        if (!rx_string_builder_append(
                out,
                "        clock_gettime(CLOCK_MONOTONIC, &end);\n"
                "        total_ns += (int64_t)(end.tv_sec - start.tv_sec) * 1000000000LL + (int64_t)(end.tv_nsec - start.tv_nsec);\n"
                "    }\n"
                "    printf(\"{\\\"result\\\": %" PRIdPTR ", \\\"average_ms\\\": %.5f, \\\"runs\\\": %d, \\\"n\\\": %" PRIdPTR "}\\n\", result, (double)total_ns / RUNS / 1e6, RUNS, N);\n"
                "    return 0;\n"
                "}\n"))
        {
            return false;
        }
    }

    return true;
}
