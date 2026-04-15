#include "dsl_codegen.h"
#include "dsl_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    char *buffer = (char *)malloc((size_t)size + 1);
    fread(buffer, 1, (size_t)size, file);
    buffer[size] = '\0';
    fclose(file);
    return buffer;
}

static void usage(void) {
    fprintf(stderr,
            "usage: pipeline_codegen --dsl <file> [--output file] [--binary "
            "file] [--runs expr] [--define NAME=EXPR] [--compile] [--run]\n");
}

static bool parse_define_arg(const char *text, char *name, size_t name_size,
                             char *value, size_t value_size) {
    const char *equals = strchr(text, '=');
    if (equals == NULL) {
        return false;
    }
    size_t name_len = (size_t)(equals - text);
    if (name_len >= name_size) {
        name_len = name_size - 1;
    }
    memcpy(name, text, name_len);
    name[name_len] = '\0';
    strncpy(value, equals + 1, value_size - 1);
    return true;
}

int main(int argc, char **argv) {
    CodegenOptions options;
    Program program;
    memset(&options, 0, sizeof(options));
    strcpy(options.output_file, "out/generated_pipeline.c");
    strcpy(options.binary_file, "out/generated_pipeline.exe");
    strcpy(options.runs_expr, "1");

    char dsl_file[260] = "";
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--dsl") == 0 && i + 1 < argc) {
            strncpy(dsl_file, argv[++i], sizeof(dsl_file) - 1);
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            strncpy(options.output_file, argv[++i],
                    sizeof(options.output_file) - 1);
        } else if (strcmp(argv[i], "--binary") == 0 && i + 1 < argc) {
            strncpy(options.binary_file, argv[++i],
                    sizeof(options.binary_file) - 1);
        } else if (strcmp(argv[i], "--runs") == 0 && i + 1 < argc) {
            strncpy(options.runs_expr, argv[++i],
                    sizeof(options.runs_expr) - 1);
        } else if (strcmp(argv[i], "--define") == 0 && i + 1 < argc) {
            if (options.define_count >= 32 ||
                !parse_define_arg(
                    argv[++i], options.defines[options.define_count],
                    sizeof(options.defines[options.define_count]),
                    options.define_values[options.define_count],
                    sizeof(options.define_values[options.define_count]))) {
                usage();
                return 1;
            }
            options.define_count++;
        } else if (strcmp(argv[i], "--compile") == 0) {
            options.compile_generated = true;
        } else if (strcmp(argv[i], "--run") == 0) {
            options.compile_generated = true;
            options.run_generated = true;
        } else {
            usage();
            return 1;
        }
    }

    if (dsl_file[0] == '\0') {
        usage();
        return 1;
    }

    char *dsl_source = read_file(dsl_file);
    if (dsl_source == NULL) {
        fprintf(stderr, "failed to read %s\n", dsl_file);
        return 1;
    }

    if (!parse_program_text(dsl_source, &program)) {
        free(dsl_source);
        return 1;
    }
    free(dsl_source);

    if (!emit_program_c(&program, &options)) {
        fprintf(stderr, "failed to write %s\n", options.output_file);
        return 1;
    }

    printf("generated %s with %d chain(s)\n", options.output_file,
           program.chain_count);
    for (int i = 0; i < program.chain_count; ++i) {
        printf("  call_%02X\n", i);
    }

    if (options.compile_generated) {
        int rc = compile_generated_program(&options);
        if (rc != 0) {
            fprintf(stderr, "compile failed with exit code %d\n", rc);
            return rc;
        }
    }
    if (options.run_generated) {
        int rc = run_generated_program(&options);
        if (rc != 0) {
            fprintf(stderr, "run failed with exit code %d\n", rc);
            return rc;
        }
    }

    return 0;
}
