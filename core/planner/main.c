#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "transpiler.h"

static char *rx_strdup_local(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = malloc(length);
    if (copy != NULL)
    {
        memcpy(copy, text, length);
    }
    return copy;
}

static bool append_stage(RxPipelineIr *pipeline, RxStageCall stage)
{
    int next_count = pipeline->stage_count + 1;
    RxStageCall *stages = realloc(pipeline->stages, (size_t)next_count * sizeof(*stages));
    if (stages == NULL)
    {
        return false;
    }
    pipeline->stages = stages;
    pipeline->stages[pipeline->stage_count] = stage;
    pipeline->stage_count = next_count;
    return true;
}

static void init_stage_call(RxStageCall *stage, const RxFunctionSignature *signature)
{
    memset(stage, 0, sizeof(*stage));
    stage->signature = signature;
    stage->can_start_segment = signature->segment_rule != RX_SEGMENT_RULE_MUST_END_SEGMENT;
    stage->can_end_segment = signature->segment_rule != RX_SEGMENT_RULE_MUST_START_SEGMENT;
    stage->must_remain_whole = signature->segment_rule == RX_SEGMENT_RULE_MUST_STAY_WHOLE;
    stage->preserves_runtime_layout = signature->preserves_runtime_layout;
    for (int index = 0; index < RX_MAX_CALL_ARGUMENTS; ++index)
    {
        stage->arguments[index].kind = RX_BINDING_NONE;
        stage->arguments[index].value_type = signature->argument_types[index];
    }
}

static bool parse_int_literal(const char *text, int *value_out)
{
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno != 0 || end == NULL || *end != '\0')
    {
        return false;
    }
    *value_out = (int)value;
    return true;
}

static void set_literal_argument(RxBinding *binding, int value)
{
    binding->kind = RX_BINDING_LITERAL;
    binding->value_type = RX_ARG_INT;
    binding->as.literal.kind = RX_LITERAL_INT;
    binding->as.literal.as.int_value = value;
}

static bool set_function_argument(RxBinding *binding, const char *name)
{
    binding->kind = RX_BINDING_FUNCTION_NAME;
    binding->as.function_name = rx_strdup_local(name);
    return binding->as.function_name != NULL;
}

static bool parse_spec(
    const char *path,
    const RxFunctionRegistry *registry,
    RxProgramIr *program)
{
    memset(program, 0, sizeof(*program));
    FILE *file = fopen(path, "r");
    if (file == NULL)
    {
        return false;
    }

    program->pipeline_count = 1;
    program->pipelines = calloc(1, sizeof(*program->pipelines));
    if (program->pipelines == NULL)
    {
        fclose(file);
        return false;
    }

    RxPipelineIr *pipeline = &program->pipelines[0];
    pipeline->name = "pipeline";

    char line[512];
    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *cursor = line;
        while (*cursor == ' ' || *cursor == '\t')
        {
            ++cursor;
        }
        if (*cursor == '\0' || *cursor == '\n' || *cursor == '#')
        {
            continue;
        }

        char *newline = strchr(cursor, '\n');
        if (newline != NULL)
        {
            *newline = '\0';
        }

        char *token = strtok(cursor, " \t");
        if (token == NULL)
        {
            continue;
        }

        if (strcmp(token, "pipeline") == 0)
        {
            char *name = strtok(NULL, " \t");
            if (name != NULL)
            {
                pipeline->name = rx_strdup_local(name);
                if (pipeline->name == NULL)
                {
                    fclose(file);
                    return false;
                }
            }
            continue;
        }

        if (strcmp(token, "source") == 0)
        {
            char *source_name = strtok(NULL, " \t");
            if (source_name == NULL)
            {
                fclose(file);
                return false;
            }

            const RxFunctionSignature *signature = rx_find_function_signature(registry, source_name);
            if (signature == NULL)
            {
                fclose(file);
                return false;
            }

            pipeline->source.signature = signature;
            pipeline->source.can_start_segment = true;
            pipeline->source.can_end_segment = true;

            if (strcmp(source_name, "range") == 0)
            {
                set_literal_argument(&pipeline->source.arguments[0], 1);
                pipeline->source.arguments[1].kind = RX_BINDING_RUNTIME_VALUE;
                pipeline->source.arguments[1].value_type = RX_ARG_INT;
                pipeline->source.arguments[1].as.runtime_symbol = "N";
            }
            else if (strcmp(source_name, "zip_range") == 0)
            {
                set_literal_argument(&pipeline->source.arguments[0], 2);
            }
            continue;
        }

        const RxFunctionSignature *signature = rx_find_function_signature(registry, token);
        if (signature == NULL)
        {
            fclose(file);
            return false;
        }

        RxStageCall stage;
        init_stage_call(&stage, signature);
        char *arg1 = strtok(NULL, " \t");
        char *arg2 = strtok(NULL, " \t");

        if (strcmp(token, "map") == 0
            || strcmp(token, "pairMap") == 0
            || strcmp(token, "filter") == 0
            || strcmp(token, "scan") == 0
            || strcmp(token, "takeWhile") == 0
            || strcmp(token, "skipWhile") == 0
            || strcmp(token, "distinctUntilChanged") == 0)
        {
            if (arg1 == NULL || !set_function_argument(&stage.arguments[0], arg1))
            {
                fclose(file);
                return false;
            }
        }
        else if (strcmp(token, "scanfrom") == 0)
        {
            int initial = 0;
            if (arg1 == NULL || arg2 == NULL || !set_function_argument(&stage.arguments[0], arg1)
                || !parse_int_literal(arg2, &initial))
            {
                fclose(file);
                return false;
            }
            set_literal_argument(&stage.arguments[1], initial);
        }
        else if (strcmp(token, "reduce") == 0)
        {
            if (arg1 == NULL || !set_function_argument(&stage.arguments[0], arg1))
            {
                fclose(file);
                return false;
            }
            if (arg2 != NULL)
            {
                int initial = 0;
                if (!parse_int_literal(arg2, &initial))
                {
                    fclose(file);
                    return false;
                }
                set_literal_argument(&stage.arguments[1], initial);
            }
        }
        else if (strcmp(token, "take") == 0 || strcmp(token, "skip") == 0)
        {
            int amount = 0;
            if (arg1 == NULL || !parse_int_literal(arg1, &amount))
            {
                fclose(file);
                return false;
            }
            set_literal_argument(&stage.arguments[0], amount);
        }
        else if (strcmp(token, "last") == 0 || strcmp(token, "first") == 0)
        {
        }
        else
        {
            fclose(file);
            return false;
        }

        if (!append_stage(pipeline, stage))
        {
            fclose(file);
            return false;
        }
    }

    fclose(file);
    return true;
}

static void print_diagnostics(const RxDiagnosticBag *bag)
{
    for (int index = 0; index < bag->count; ++index)
    {
        const RxDiagnostic *diagnostic = &bag->items[index];
        fprintf(
            stderr,
            "%s[%d] chain=%d stage=%d symbol=%s: %s\n",
            diagnostic->level == RX_DIAGNOSTIC_ERROR ? "error" : "diag",
            diagnostic->code,
            diagnostic->chain_index,
            diagnostic->stage_index,
            diagnostic->symbol_name != NULL ? diagnostic->symbol_name : "-",
            diagnostic->message != NULL ? diagnostic->message : "-");
    }
}

int main(int argc, char **argv)
{
    const char *spec_path = NULL;
    const char *output_path = NULL;
    const char *header_path = NULL;
    const char *helpers_source_path = NULL;

    for (int index = 1; index < argc; ++index)
    {
        if (strcmp(argv[index], "--spec") == 0 && index + 1 < argc)
        {
            spec_path = argv[++index];
        }
        else if (strcmp(argv[index], "--output") == 0 && index + 1 < argc)
        {
            output_path = argv[++index];
        }
        else if (strcmp(argv[index], "--header") == 0 && index + 1 < argc)
        {
            header_path = argv[++index];
        }
        else if (strcmp(argv[index], "--helpers-source") == 0 && index + 1 < argc)
        {
            helpers_source_path = argv[++index];
        }
    }

    if (spec_path == NULL || output_path == NULL)
    {
        fprintf(stderr, "usage: planner_codegen --spec <spec.txt> --output <out.c> [--header <helpers.h>]\n");
        return 1;
    }

    RxTranspiler transpiler;
    rx_transpiler_init(&transpiler, rx_default_function_registry());
    if (!parse_spec(spec_path, transpiler.registry, &transpiler.program))
    {
        fprintf(stderr, "failed to parse planner spec: %s\n", spec_path);
        rx_transpiler_reset(&transpiler);
        return 1;
    }

    if (!rx_transpiler_plan(&transpiler) || !rx_transpiler_lower(&transpiler))
    {
        print_diagnostics(&transpiler.diagnostics);
        rx_transpiler_reset(&transpiler);
        return 1;
    }

    RxStringBuilder output;
    rx_string_builder_init(&output);
    RxStringBuilder helper_source;
    rx_string_builder_init(&helper_source);
    RxCCodegenOptions options;
    memset(&options, 0, sizeof(options));
    options.emit_main = true;
    options.header_path = header_path;

    if (helpers_source_path != NULL)
    {
        FILE *helpers_file = fopen(helpers_source_path, "r");
        if (helpers_file == NULL)
        {
            fprintf(stderr, "failed to read helpers source: %s\n", helpers_source_path);
            rx_string_builder_reset(&helper_source);
            rx_string_builder_reset(&output);
            rx_transpiler_reset(&transpiler);
            return 1;
        }

        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), helpers_file) != NULL)
        {
            if (!rx_string_builder_append(&helper_source, buffer))
            {
                fclose(helpers_file);
                rx_string_builder_reset(&helper_source);
                rx_string_builder_reset(&output);
                rx_transpiler_reset(&transpiler);
                return 1;
            }
        }
        fclose(helpers_file);
        options.helper_source_text = helper_source.data;
    }

    if (!rx_transpiler_emit(&transpiler, &options, &output))
    {
        print_diagnostics(&transpiler.diagnostics);
        rx_string_builder_reset(&helper_source);
        rx_string_builder_reset(&output);
        rx_transpiler_reset(&transpiler);
        return 1;
    }

    FILE *file = fopen(output_path, "w");
    if (file == NULL)
    {
        fprintf(stderr, "failed to write output: %s\n", output_path);
        rx_string_builder_reset(&output);
        rx_transpiler_reset(&transpiler);
        return 1;
    }
    fputs(output.data != NULL ? output.data : "", file);
    fclose(file);

    rx_string_builder_reset(&helper_source);
    rx_string_builder_reset(&output);
    rx_transpiler_reset(&transpiler);
    return 0;
}
