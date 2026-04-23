#include "dsl_parser.h"

#include "dsl_lexer.h"
#include "operator_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Lexer lexer;
} Parser;

static Expr *new_expr(ExprKind kind, const char *text) {
    Expr *expr = (Expr *)calloc(1, sizeof(Expr));
    expr->kind = kind;
    if (text != NULL) {
        strncpy(expr->text, text, sizeof(expr->text) - 1);
    }
    return expr;
}

static SourceAst *new_source(SourceKind kind) {
    SourceAst *source = (SourceAst *)calloc(1, sizeof(SourceAst));
    source->kind = kind;
    return source;
}

static bool accept(Parser *parser, TokenKind kind) {
    if (parser->lexer.current.kind == kind) {
        lexer_next(&parser->lexer);
        return true;
    }
    return false;
}

static bool expect(Parser *parser, TokenKind kind, const char *label) {
    if (!accept(parser, kind)) {
        fprintf(stderr, "parse error: expected %s, got '%s'\n", label,
                parser->lexer.current.text);
        return false;
    }
    return true;
}

static bool expect_ident(Parser *parser, char *out, size_t out_size) {
    if (parser->lexer.current.kind != TOK_IDENT) {
        fprintf(stderr, "parse error: expected identifier, got '%s'\n",
                parser->lexer.current.text);
        return false;
    }
    strncpy(out, parser->lexer.current.text, out_size - 1);
    lexer_next(&parser->lexer);
    return true;
}

static bool expect_value(Parser *parser, char *out, size_t out_size) {
    if (parser->lexer.current.kind != TOK_IDENT &&
        parser->lexer.current.kind != TOK_NUMBER) {
        fprintf(stderr, "parse error: expected value, got '%s'\n",
                parser->lexer.current.text);
        return false;
    }
    strncpy(out, parser->lexer.current.text, out_size - 1);
    lexer_next(&parser->lexer);
    return true;
}

static Expr *parse_expr(Parser *parser) {
    if (parser->lexer.current.kind == TOK_IDENT) {
        char name[64];
        strncpy(name, parser->lexer.current.text, sizeof(name) - 1);
        lexer_next(&parser->lexer);

        if (accept(parser, TOK_LPAREN)) {
            Expr *call = new_expr(EXPR_CALL, name);
            if (!accept(parser, TOK_RPAREN)) {
                while (true) {
                    if (call->arg_count >= 8) {
                        return NULL;
                    }
                    call->args[call->arg_count++] = parse_expr(parser);
                    if (call->args[call->arg_count - 1] == NULL) {
                        return NULL;
                    }
                    if (accept(parser, TOK_COMMA)) {
                        continue;
                    }
                    if (!expect(parser, TOK_RPAREN, "')'")) {
                        return NULL;
                    }
                    break;
                }
            }
            return call;
        }
        return new_expr(EXPR_IDENT, name);
    }

    if (parser->lexer.current.kind == TOK_NUMBER) {
        char number[64];
        strncpy(number, parser->lexer.current.text, sizeof(number) - 1);
        lexer_next(&parser->lexer);
        return new_expr(EXPR_NUMBER, number);
    }

    fprintf(stderr, "parse error: unsupported expression token '%s'\n",
            parser->lexer.current.text);
    return NULL;
}

static bool parse_fn_def(Parser *parser, FnDef *fn) {
    lexer_next(&parser->lexer);
    if (!expect_ident(parser, fn->name, sizeof(fn->name)) ||
        !expect(parser, TOK_LPAREN, "'('")) {
        return false;
    }

    fn->param_count = 0;
    if (!accept(parser, TOK_RPAREN)) {
        while (true) {
            if (fn->param_count >= 2 ||
                !expect_ident(parser, fn->params[fn->param_count],
                              sizeof(fn->params[fn->param_count]))) {
                return false;
            }
            fn->param_count++;
            if (accept(parser, TOK_COMMA)) {
                continue;
            }
            if (!expect(parser, TOK_RPAREN, "')'")) {
                return false;
            }
            break;
        }
    }

    if (!expect(parser, TOK_LBRACE, "'{'")) {
        return false;
    }
    if (strcmp(parser->lexer.current.text, "return") != 0) {
        fprintf(stderr, "parse error: expected return, got '%s'\n",
                parser->lexer.current.text);
        return false;
    }
    lexer_next(&parser->lexer);
    fn->body = parse_expr(parser);
    if (fn->body == NULL) {
        return false;
    }
    return expect(parser, TOK_SEMI, "';'") && expect(parser, TOK_RBRACE, "'}'");
}

static SourceAst *parse_source(Parser *parser);

static bool parse_source_arg_list(Parser *parser, SourceAst *source,
                                  bool nested_sources) {
    if (accept(parser, TOK_RPAREN)) {
        return true;
    }

    while (true) {
        if (nested_sources && parser->lexer.current.kind == TOK_IDENT) {
            const char *name = parser->lexer.current.text;
            if (strcmp(name, "range") == 0 || strcmp(name, "of") == 0 ||
                strcmp(name, "empty") == 0 || strcmp(name, "never") == 0 ||
                strcmp(name, "interval") == 0 || strcmp(name, "timer") == 0 ||
                strcmp(name, "defer") == 0 || strcmp(name, "from") == 0 ||
                strcmp(name, "zip") == 0) {
                if (source->source_count >= 8) {
                    return false;
                }
                source->sources[source->source_count++] = parse_source(parser);
                if (source->sources[source->source_count - 1] == NULL) {
                    return false;
                }
            } else {
                if (source->value_count >= 8 ||
                    !expect_value(
                        parser, source->values[source->value_count],
                        sizeof(source->values[source->value_count]))) {
                    return false;
                }
                source->value_count++;
            }
        } else {
            if (source->value_count >= 8 ||
                !expect_value(parser, source->values[source->value_count],
                              sizeof(source->values[source->value_count]))) {
                return false;
            }
            source->value_count++;
        }

        if (accept(parser, TOK_COMMA)) {
            continue;
        }
        return expect(parser, TOK_RPAREN, "')'");
    }
}

static SourceAst *parse_source(Parser *parser) {
    char name[64];
    SourceKind kind;

    if (!expect_ident(parser, name, sizeof(name))) {
        return NULL;
    }

    if (strcmp(name, "range") == 0)
        kind = SOURCE_RANGE;
    else if (strcmp(name, "of") == 0)
        kind = SOURCE_OF;
    else if (strcmp(name, "empty") == 0)
        kind = SOURCE_EMPTY;
    else if (strcmp(name, "never") == 0)
        kind = SOURCE_NEVER;
    else if (strcmp(name, "interval") == 0)
        kind = SOURCE_INTERVAL;
    else if (strcmp(name, "timer") == 0)
        kind = SOURCE_TIMER;
    else if (strcmp(name, "defer") == 0)
        kind = SOURCE_DEFER;
    else if (strcmp(name, "from") == 0)
        kind = SOURCE_FROM;
    else if (strcmp(name, "zip") == 0)
        kind = SOURCE_ZIP;
    else {
        fprintf(stderr, "parse error: unsupported source '%s'\n", name);
        return NULL;
    }

    if (!expect(parser, TOK_LPAREN, "'('")) {
        return NULL;
    }

    SourceAst *source = new_source(kind);
    if (source == NULL) {
        return NULL;
    }
    if (!parse_source_arg_list(parser, source, kind == SOURCE_ZIP)) {
        return NULL;
    }
    return source;
}

static bool parse_operator(Parser *parser, OperatorAst *op) {
    char name[64];
    if (!expect_ident(parser, name, sizeof(name)) ||
        !expect(parser, TOK_LPAREN, "'('")) {
        return false;
    }

    const OperatorInfo *info = find_operator_info_by_name(name);
    if (info == NULL) {
        fprintf(stderr, "parse error: unsupported operator '%s'\n", name);
        return false;
    }

    memset(op, 0, sizeof(*op));
    op->kind = info->kind;

    switch (info->argument_kind) {
    case ARGUMENT_NONE:
        return expect(parser, TOK_RPAREN, "')'");
    case ARGUMENT_FUNCTION:
        if (!expect_ident(parser, op->symbol, sizeof(op->symbol))) {
            return false;
        }
        return expect(parser, TOK_RPAREN, "')'");
    case ARGUMENT_LITERAL:
        if (!expect_value(parser, op->extra, sizeof(op->extra))) {
            return false;
        }
        op->has_extra = true;
        return expect(parser, TOK_RPAREN, "')'");
    case ARGUMENT_FUNCTION_AND_LITERAL:
        if (!expect_ident(parser, op->symbol, sizeof(op->symbol))) {
            return false;
        }
        if (accept(parser, TOK_COMMA)) {
            if (!expect_value(parser, op->extra, sizeof(op->extra))) {
                return false;
            }
            op->has_extra = true;
        }
        return expect(parser, TOK_RPAREN, "')'");
    case ARGUMENT_SOURCE:
        op->source_arg = parse_source(parser);
        if (op->source_arg == NULL) {
            return false;
        }
        return expect(parser, TOK_RPAREN, "')'");
    }
    return false;
}

static bool parse_subscribe(Parser *parser, ChainAst *chain) {
    if (!expect(parser, TOK_LPAREN, "'('")) {
        return false;
    }

    if (parser->lexer.current.kind == TOK_IDENT &&
        strcmp(parser->lexer.current.text, "assign") == 0) {
        lexer_next(&parser->lexer);
        if (!expect(parser, TOK_LPAREN, "'('") ||
            !expect_ident(parser, chain->subscriber_target,
                          sizeof(chain->subscriber_target))) {
            return false;
        }
        return expect(parser, TOK_RPAREN, "')'") &&
               expect(parser, TOK_RPAREN, "')'") &&
               expect(parser, TOK_SEMI, "';'");
    }

    {
        char lambda_param[32];
        char assigned_from[32];
        if (!expect_ident(parser, lambda_param, sizeof(lambda_param)) ||
            !expect(parser, TOK_ARROW, "'=>'") ||
            !expect_ident(parser, chain->subscriber_target,
                          sizeof(chain->subscriber_target)) ||
            !expect(parser, TOK_EQUAL, "'='") ||
            !expect_ident(parser, assigned_from, sizeof(assigned_from))) {
            return false;
        }
        if (strcmp(lambda_param, assigned_from) != 0) {
            fprintf(
                stderr,
                "parse error: subscribe must assign from lambda parameter\n");
            return false;
        }
    }
    return expect(parser, TOK_RPAREN, "')'") && expect(parser, TOK_SEMI, "';'");
}

static bool parse_chain(Parser *parser, ChainAst *chain) {
    memset(chain, 0, sizeof(*chain));
    chain->source = parse_source(parser);
    if (chain->source == NULL) {
        return false;
    }
    if (!expect(parser, TOK_DOT, "'.'")) {
        return false;
    }
    if (strcmp(parser->lexer.current.text, "pipe") == 0) {
        lexer_next(&parser->lexer);
        if (!expect(parser, TOK_LPAREN, "'('")) {
            return false;
        }
        if (!accept(parser, TOK_RPAREN)) {
            while (true) {
                if (chain->op_count >= 32 ||
                    !parse_operator(parser, &chain->ops[chain->op_count])) {
                    return false;
                }
                chain->op_count++;
                if (accept(parser, TOK_COMMA)) {
                    continue;
                }
                if (!expect(parser, TOK_RPAREN, "')'")) {
                    return false;
                }
                break;
            }
        }
        if (!expect(parser, TOK_DOT, "'.'")) {
            return false;
        }
    }
    if (strcmp(parser->lexer.current.text, "subscribe") != 0) {
        fprintf(stderr, "parse error: expected subscribe, got '%s'\n",
                parser->lexer.current.text);
        return false;
    }
    lexer_next(&parser->lexer);
    return parse_subscribe(parser, chain);
}

bool parse_program_text(const char *source, ProgramAst *program) {
    Parser parser;
    memset(&parser, 0, sizeof(parser));
    memset(program, 0, sizeof(*program));
    lexer_init(&parser.lexer, source);

    while (parser.lexer.current.kind != TOK_EOF) {
        if (parser.lexer.current.kind == TOK_IDENT &&
            strcmp(parser.lexer.current.text, "fn") == 0) {
            if (program->function_count >= 64 ||
                !parse_fn_def(&parser,
                              &program->functions[program->function_count])) {
                return false;
            }
            program->function_count++;
        } else {
            if (program->chain_count >= 32 ||
                !parse_chain(&parser, &program->chains[program->chain_count])) {
                return false;
            }
            program->chain_count++;
        }
    }

    return true;
}
