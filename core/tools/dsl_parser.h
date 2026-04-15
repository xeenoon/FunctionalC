#ifndef DSL_PARSER_H
#define DSL_PARSER_H

#include "dsl_ast.h"

bool parse_program_text(const char *source, ProgramAst *program);

#endif
