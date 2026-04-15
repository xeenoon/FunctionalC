#include "dsl_parser.h"

#include "dsl_lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    Lexer lexer;
} Parser;

static Expr *new_expr(ExprKind kind, const char *text)
{
    Expr *expr = (Expr *)calloc(1, sizeof(Expr));
    expr->kind = kind;
    if (text != NULL)
    {
        strncpy(expr->text, text, sizeof(expr->text) - 1);
    }
    return expr;
}

static bool accept(Parser *parser, TokenKind kind)
{
    if (parser->lexer.current.kind == kind)
    {
        lexer_next(&parser->lexer);
        return true;
    }
    return false;
}

static bool expect(Parser *parser, TokenKind kind, const char *label)
{
    if (!accept(parser, kind))
    {
        fprintf(stderr, "parse error: expected %s, got '%s'\n", label, parser->lexer.current.text);
        return false;
    }
    return true;
}

static bool expect_ident(Parser *parser, char *out, size_t out_size)
{
    if (parser->lexer.current.kind != TOK_IDENT)
    {
        fprintf(stderr, "parse error: expected identifier, got '%s'\n", parser->lexer.current.text);
        return false;
    }
    strncpy(out, parser->lexer.current.text, out_size - 1);
    lexer_next(&parser->lexer);
    return true;
}

static Expr *parse_expr(Parser *parser)
{
    if (parser->lexer.current.kind == TOK_IDENT)
    {
        char name[64];
        strncpy(name, parser->lexer.current.text, sizeof(name) - 1);
        lexer_next(&parser->lexer);

        if (accept(parser, TOK_LPAREN))
        {
            Expr *call = new_expr(EXPR_CALL, name);
            if (!accept(parser, TOK_RPAREN))
            {
                while (true)
                {
                    if (call->arg_count >= 8)
                    {
                        return NULL;
                    }
                    call->args[call->arg_count++] = parse_expr(parser);
                    if (call->args[call->arg_count - 1] == NULL)
                    {
                        return NULL;
                    }
                    if (accept(parser, TOK_COMMA))
                    {
                        continue;
                    }
                    if (!expect(parser, TOK_RPAREN, "')'"))
                    {
                        return NULL;
                    }
                    break;
                }
            }
            return call;
        }
        return new_expr(EXPR_IDENT, name);
    }

    if (parser->lexer.current.kind == TOK_NUMBER)
    {
        char number[64];
        strncpy(number, parser->lexer.current.text, sizeof(number) - 1);
        lexer_next(&parser->lexer);
        return new_expr(EXPR_NUMBER, number);
    }

    fprintf(stderr, "parse error: unsupported expression token '%s'\n", parser->lexer.current.text);
    return NULL;
}

static bool parse_fn_def(Parser *parser, FnDef *fn)
{
    if (strcmp(parser->lexer.current.text, "fn") != 0)
    {
        return false;
    }
    lexer_next(&parser->lexer);

    if (!expect_ident(parser, fn->name, sizeof(fn->name)) || !expect(parser, TOK_LPAREN, "'('"))
    {
        return false;
    }

    fn->param_count = 0;
    if (!accept(parser, TOK_RPAREN))
    {
        while (true)
        {
            if (fn->param_count >= 2 || !expect_ident(parser, fn->params[fn->param_count], sizeof(fn->params[fn->param_count])))
            {
                return false;
            }
            fn->param_count++;
            if (accept(parser, TOK_COMMA))
            {
                continue;
            }
            if (!expect(parser, TOK_RPAREN, "')'"))
            {
                return false;
            }
            break;
        }
    }

    if (!expect(parser, TOK_LBRACE, "'{'"))
    {
        return false;
    }
    if (strcmp(parser->lexer.current.text, "return") != 0)
    {
        fprintf(stderr, "parse error: expected return, got '%s'\n", parser->lexer.current.text);
        return false;
    }
    lexer_next(&parser->lexer);
    fn->body = parse_expr(parser);
    if (fn->body == NULL)
    {
        return false;
    }
    return expect(parser, TOK_SEMI, "';'") && expect(parser, TOK_RBRACE, "'}'");
}

static bool parse_operator(Parser *parser, Operator *op)
{
    char name[64];
    if (!expect_ident(parser, name, sizeof(name)) || !expect(parser, TOK_LPAREN, "'('"))
    {
        return false;
    }

    if (strcmp(name, "map") == 0)
    {
        op->kind = OP_MAP;
        if (!expect_ident(parser, op->function_name, sizeof(op->function_name)))
        {
            return false;
        }
        return expect(parser, TOK_RPAREN, "')'");
    }
    if (strcmp(name, "filter") == 0)
    {
        op->kind = OP_FILTER;
        if (!expect_ident(parser, op->function_name, sizeof(op->function_name)))
        {
            return false;
        }
        return expect(parser, TOK_RPAREN, "')'");
    }
    if (strcmp(name, "reduce") == 0)
    {
        op->kind = OP_REDUCE;
        if (!expect_ident(parser, op->function_name, sizeof(op->function_name)) || !expect(parser, TOK_COMMA, "','"))
        {
            return false;
        }
        if (parser->lexer.current.kind != TOK_NUMBER && parser->lexer.current.kind != TOK_IDENT)
        {
            fprintf(stderr, "parse error: expected reduce init literal\n");
            return false;
        }
        strncpy(op->literal, parser->lexer.current.text, sizeof(op->literal) - 1);
        lexer_next(&parser->lexer);
        return expect(parser, TOK_RPAREN, "')'");
    }
    if (strcmp(name, "take") == 0)
    {
        op->kind = OP_TAKE;
        if (parser->lexer.current.kind != TOK_NUMBER && parser->lexer.current.kind != TOK_IDENT)
        {
            fprintf(stderr, "parse error: expected take literal\n");
            return false;
        }
        strncpy(op->literal, parser->lexer.current.text, sizeof(op->literal) - 1);
        lexer_next(&parser->lexer);
        return expect(parser, TOK_RPAREN, "')'");
    }

    fprintf(stderr, "parse error: unsupported operator '%s'\n", name);
    return false;
}

static bool parse_chain(Parser *parser, Chain *chain)
{
    if (strcmp(parser->lexer.current.text, "range") != 0)
    {
        return false;
    }
    lexer_next(&parser->lexer);
    if (!expect(parser, TOK_LPAREN, "'('"))
    {
        return false;
    }
    if (parser->lexer.current.kind != TOK_NUMBER && parser->lexer.current.kind != TOK_IDENT)
    {
        return false;
    }
    strncpy(chain->range_min, parser->lexer.current.text, sizeof(chain->range_min) - 1);
    lexer_next(&parser->lexer);
    if (!expect(parser, TOK_COMMA, "','"))
    {
        return false;
    }
    if (parser->lexer.current.kind != TOK_NUMBER && parser->lexer.current.kind != TOK_IDENT)
    {
        return false;
    }
    strncpy(chain->range_max, parser->lexer.current.text, sizeof(chain->range_max) - 1);
    lexer_next(&parser->lexer);
    if (!expect(parser, TOK_RPAREN, "')'") || !expect(parser, TOK_DOT, "'.'"))
    {
        return false;
    }
    if (strcmp(parser->lexer.current.text, "pipe") != 0)
    {
        return false;
    }
    lexer_next(&parser->lexer);
    if (!expect(parser, TOK_LPAREN, "'('"))
    {
        return false;
    }

    chain->op_count = 0;
    if (!accept(parser, TOK_RPAREN))
    {
        while (true)
        {
            if (chain->op_count >= 16 || !parse_operator(parser, &chain->ops[chain->op_count]))
            {
                return false;
            }
            chain->op_count++;
            if (accept(parser, TOK_COMMA))
            {
                continue;
            }
            if (!expect(parser, TOK_RPAREN, "')'"))
            {
                return false;
            }
            break;
        }
    }

    if (!expect(parser, TOK_DOT, "'.'"))
    {
        return false;
    }
    if (strcmp(parser->lexer.current.text, "subscribe") != 0)
    {
        return false;
    }
    lexer_next(&parser->lexer);
    if (!expect(parser, TOK_LPAREN, "'('"))
    {
        return false;
    }

    char lambda_param[32];
    char assigned_from[32];
    if (!expect_ident(parser, lambda_param, sizeof(lambda_param)) ||
        !expect(parser, TOK_ARROW, "'=>'") ||
        !expect_ident(parser, chain->subscriber_target, sizeof(chain->subscriber_target)))
    {
        return false;
    }
    if (!expect(parser, TOK_EQUAL, "'='"))
    {
        return false;
    }
    if (!expect_ident(parser, assigned_from, sizeof(assigned_from)))
    {
        return false;
    }
    if (strcmp(lambda_param, assigned_from) != 0)
    {
        fprintf(stderr, "parse error: subscribe must assign from lambda parameter\n");
        return false;
    }
    return expect(parser, TOK_RPAREN, "')'") && expect(parser, TOK_SEMI, "';'");
}

bool parse_program_text(const char *source, Program *program)
{
    Parser parser;
    memset(&parser, 0, sizeof(parser));
    memset(program, 0, sizeof(*program));
    lexer_init(&parser.lexer, source);

    while (parser.lexer.current.kind != TOK_EOF)
    {
        if (strcmp(parser.lexer.current.text, "fn") == 0)
        {
            if (program->function_count >= 64 || !parse_fn_def(&parser, &program->functions[program->function_count]))
            {
                return false;
            }
            program->function_count++;
        }
        else if (strcmp(parser.lexer.current.text, "range") == 0)
        {
            if (program->chain_count >= 32 || !parse_chain(&parser, &program->chains[program->chain_count]))
            {
                return false;
            }
            program->chain_count++;
        }
        else
        {
            fprintf(stderr, "parse error: unexpected token '%s'\n", parser.lexer.current.text);
            return false;
        }
    }

    return true;
}
