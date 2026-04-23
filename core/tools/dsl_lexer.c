#include "dsl_lexer.h"

#include <ctype.h>
#include <string.h>

static void skip_ws(Lexer *lexer) {
    while (lexer->pos < lexer->length) {
        if (isspace((unsigned char)lexer->source[lexer->pos])) {
            lexer->pos++;
            continue;
        }
        if (lexer->source[lexer->pos] == '/' &&
            lexer->pos + 1 < lexer->length &&
            lexer->source[lexer->pos + 1] == '/') {
            lexer->pos += 2;
            while (lexer->pos < lexer->length &&
                   lexer->source[lexer->pos] != '\n') {
                lexer->pos++;
            }
            continue;
        }
        break;
    }
}

void lexer_init(Lexer *lexer, const char *source) {
    lexer->source = source;
    lexer->length = strlen(source);
    lexer->pos = 0;
    lexer_next(lexer);
}

void lexer_next(Lexer *lexer) {
    skip_ws(lexer);
    lexer->current.start = lexer->pos;
    lexer->current.end = lexer->pos;
    lexer->current.text[0] = '\0';

    if (lexer->pos >= lexer->length) {
        lexer->current.kind = TOK_EOF;
        strcpy(lexer->current.text, "<eof>");
        return;
    }

    char ch = lexer->source[lexer->pos];
    if (isalpha((unsigned char)ch) || ch == '_') {
        size_t start = lexer->pos++;
        while (lexer->pos < lexer->length) {
            char next = lexer->source[lexer->pos];
            if (!isalnum((unsigned char)next) && next != '_') {
                break;
            }
            lexer->pos++;
        }
        size_t len = lexer->pos - start;
        memcpy(lexer->current.text, lexer->source + start, len);
        lexer->current.text[len] = '\0';
        lexer->current.kind = TOK_IDENT;
    } else if (isdigit((unsigned char)ch)) {
        size_t start = lexer->pos++;
        while (lexer->pos < lexer->length &&
               isdigit((unsigned char)lexer->source[lexer->pos])) {
            lexer->pos++;
        }
        size_t len = lexer->pos - start;
        memcpy(lexer->current.text, lexer->source + start, len);
        lexer->current.text[len] = '\0';
        lexer->current.kind = TOK_NUMBER;
    } else if (ch == '=' && lexer->pos + 1 < lexer->length &&
               lexer->source[lexer->pos + 1] == '>') {
        lexer->current.kind = TOK_ARROW;
        strcpy(lexer->current.text, "=>");
        lexer->pos += 2;
    } else {
        lexer->pos++;
        lexer->current.text[0] = ch;
        lexer->current.text[1] = '\0';
        switch (ch) {
        case '(':
            lexer->current.kind = TOK_LPAREN;
            break;
        case ')':
            lexer->current.kind = TOK_RPAREN;
            break;
        case '{':
            lexer->current.kind = TOK_LBRACE;
            break;
        case '}':
            lexer->current.kind = TOK_RBRACE;
            break;
        case ',':
            lexer->current.kind = TOK_COMMA;
            break;
        case '.':
            lexer->current.kind = TOK_DOT;
            break;
        case ';':
            lexer->current.kind = TOK_SEMI;
            break;
        case '=':
            lexer->current.kind = TOK_EQUAL;
            break;
        default:
            lexer->current.kind = TOK_EOF;
            break;
        }
    }

    lexer->current.end = lexer->pos;
}
