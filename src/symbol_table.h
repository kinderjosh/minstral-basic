#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include "ast.h"
#include <stdio.h>
#include <stdbool.h>

/*
typedef struct {
    char *name;
    char *path;
    AST *root;
    ASTList visible_symbols;
} Module;

AST *add_module(char *name, char *path, bool fill_in_root);
Module *get_module(char *name);
Module *get_module_by_path(char *path);
char **get_module_paths(size_t *out_module_count);
*/

bool in_scope(char *haystack, char *needle);
void create_symbol_table();
void delete_symbol_table();
void add_symbol(AST *ast);
AST *find_symbol(ASTType type, char *name, char *scope, char *module);

#endif
