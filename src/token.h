#ifndef TOKEN_H
#define TOKEN_H

#include <stdio.h>

typedef enum {
    TOK_EOF,
    TOK_EOL,
    TOK_ID,
    TOK_INT,
    TOK_FLOAT,
    TOK_STRING,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_COMMA,
    TOK_EQUAL,
    TOK_AT,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_SHL,
    TOK_SHR,
    TOK_AND,
    TOK_OR,
    TOK_XOR,
    TOK_NOT,
    TOK_EQ,
    TOK_NEQ,
    TOK_LT,
    TOK_LTE,
    TOK_GT,
    TOK_GTE,
    TOK_LOG_NOT
} TokenType;

typedef struct {
    TokenType type;
    char *value;
    size_t ln;
    size_t col;
} Token;

Token create_token(TokenType type, char *value, size_t ln, size_t col);
void delete_token(Token *tok);
char *tokentype_to_string(TokenType type);

#endif