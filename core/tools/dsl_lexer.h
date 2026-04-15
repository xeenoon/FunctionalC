#ifndef DSL_LEXER_H
#define DSL_LEXER_H

#include <stddef.h>

typedef enum {
    TOK_IDENT,
    TOK_NUMBER,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_COMMA,
    TOK_DOT,
    TOK_SEMI,
    TOK_EQUAL,
    TOK_ARROW,
    TOK_EOF
} TokenKind;

typedef struct {
    TokenKind kind;
    char text[64];
    size_t start;
    size_t end;
} Token;

typedef struct {
    const char *source;
    size_t length;
    size_t pos;
    Token current;
} Lexer;

void lexer_init(Lexer *lexer, const char *source);
void lexer_next(Lexer *lexer);

#endif
