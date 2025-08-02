#include "symbol_table.h"
#include "ast.h"
#include "utils.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static ASTList symbol_table;

extern char *cur_scope;
extern char *cur_func;
extern char *cur_file;

void create_symbol_table() {
    symbol_table = create_astlist();
}

void delete_symbol_table() {
    free(symbol_table.items);
}

/*
 * public domain strtok_r() by Charlie Gordon
 *
 *   from comp.lang.c  9/14/2007
 *
 *      http://groups.google.com/group/comp.lang.c/msg/2ab1ecbb86646684
 *
 *     (Declaration that it's public domain):
 *      http://groups.google.com/group/comp.lang.c/msg/7c7b39328fefab9c
 */

char *mystrtok_r(
    char *str,
    const char *delim,
    char **nextp)
{
    char *ret;

    if (str == NULL)
    {
        str = *nextp;
    }

    str += strspn(str, delim);

    if (*str == '\0')
    {
        return NULL;
    }

    ret = str;

    str += strcspn(str, delim);

    if (*str)
    {
        *str++ = '\0';
    }

    *nextp = str;

    return ret;
}

char haystack_cpy[999];
char needle_cpy[999];
char *haystack_save;
char *needle_save;
char *haystack_tok;
char *needle_tok;

// Scopes are handled using '@' as a delimeter between nests.
// For example, the scope of an if statement in the main function:
// main@if24
// It starts with the function, delimits with '@' and has the name
// of the nest followed by the line and column number.
//
// To check if a scope is visible in another, we have to split each
// string by delimiting at '@' and comparing the characters in between.
bool in_scope(char *haystack, char *needle) {
    if (strcmp(haystack, "<global>") == 0 || strcmp(needle, "<global>") == 0)
        return true;
    else if (strlen(needle) < strlen(haystack))
        return false;

    strcpy(haystack_cpy, haystack);
    strcpy(needle_cpy, needle);

    haystack_tok = mystrtok_r(haystack_cpy, "@", &haystack_save);
    needle_tok = mystrtok_r(needle_cpy, "@", &needle_save);
    bool found = true;

    while (haystack_tok != NULL) {
        if (needle_tok == NULL || strcmp(haystack_tok, needle_tok) != 0)
            return false;

        haystack_tok = mystrtok_r(NULL, "@", &haystack_save);
        needle_tok = mystrtok_r(NULL, "@", &needle_save);
    }

    return found;
}

void add_symbol(AST *ast) {
    astlist_push(&symbol_table, ast);
}

AST *find_symbol(ASTType type, char *name, char *scope, char *module) {
    (void)module;

    for (size_t i = 0; i < symbol_table.size; i++) {
        AST *sym = symbol_table.items[i];

        if (sym->type != type || !in_scope(sym->scope.full, scope))
            continue;
        else if ((type == AST_FUNC && strcmp(sym->func.name, name) == 0) || (type == AST_DECL && strcmp(sym->decl.name, name) == 0))
            return sym;
    }

    return NULL;
}
