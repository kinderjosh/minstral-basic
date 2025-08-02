#include "token.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

Token create_token(TokenType type, char *value, size_t ln, size_t col) {
    return (Token){ .type = type, .value = value, .ln = ln, .col = col };
}

void delete_token(Token *tok) {
    free(tok->value);
}

char *tokentype_to_string(TokenType type) {
    switch (type) {
        case TOK_EOF: return "eof";
        case TOK_EOL: return "eol";
        case TOK_ID: return "identifier";
        case TOK_INT: return "int";
        case TOK_FLOAT: return "float";
        case TOK_STRING: return "string";
        case TOK_LPAREN: return "lparen";
        case TOK_RPAREN: return "rparen";
        case TOK_LBRACE: return "lbrace";
        case TOK_RBRACE: return "rbrace";
        case TOK_COMMA: return "comma";
        case TOK_EQUAL: return "equal";
        case TOK_AT: return "at";
        case TOK_PLUS: return "plus";
        case TOK_MINUS: return "minus";
        case TOK_STAR: return "star";
        case TOK_SLASH: return "slash";
        case TOK_PERCENT: return "percent";
        case TOK_SHL: return "lshift";
        case TOK_SHR: return "rshift";
        case TOK_AND: return "and";
        case TOK_OR: return "or";
        case TOK_XOR: return "xor";
        case TOK_NOT: return "not";
        case TOK_EQ: return "eq";
        case TOK_NEQ: return "neq";
        case TOK_LT: return "lt";
        case TOK_LTE: return "lte";
        case TOK_GT: return "gt";
        case TOK_GTE: return "gte";
        case TOK_LOG_NOT: return "logical not";
        default: break;
    }

    assert(false);
    return "undefined";
}