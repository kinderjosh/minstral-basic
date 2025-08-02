#ifndef PARSER_H
#define PARSER_H

#include "token.h"
#include "ast.h"
#include <stdio.h>

typedef struct {
    char *file;
    Token *tokens;
    size_t token_count;
    Token *tok;
    size_t pos;
    unsigned int flags;
} Parser;

AST *parse_stmt(Parser *prs);
AST *parse_root(char *file);
void create_duplicates();
void delete_duplicates();

#endif