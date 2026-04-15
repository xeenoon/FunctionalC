#ifndef DSL_CODEGEN_H
#define DSL_CODEGEN_H

#include <stdbool.h>

#include "dsl_ast.h"

typedef struct {
    char support_file[260];
    char output_file[260];
    char binary_file[260];
    char runs_expr[64];
    char defines[32][64];
    char define_values[32][128];
    int define_count;
    bool compile_generated;
    bool run_generated;
} CodegenOptions;

bool emit_program_c(const Program *program, const CodegenOptions *options);
int compile_generated_program(const CodegenOptions *options);
int run_generated_program(const CodegenOptions *options);

#endif
