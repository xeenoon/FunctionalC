#ifndef LOWERING_H
#define LOWERING_H

#include <stdbool.h>

#include "dsl_ast.h"
#include "ir.h"

bool lower_program(const ProgramAst *ast, ProgramIr *ir);

#endif
