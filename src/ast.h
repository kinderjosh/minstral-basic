#ifndef AST_H
#define AST_H

#include "token.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#define GLOBAL "\0"

typedef struct AST AST;

typedef struct ASTList {
    AST **items;
    size_t size;
    size_t capacity;
} ASTList;

typedef enum {
    AST_NOP,
    AST_ROOT,
    AST_INT,
    AST_VAR,
    AST_FUNC,
    AST_CALL,
    AST_DECL,
    AST_ASSIGN,
    AST_RET,
    AST_ASM_BLOCK,
    AST_OPER,
    AST_MATH,
    AST_PARENS,
    AST_CONDITION,
    AST_IF,
    AST_FOR,
    AST_WHILE,
    AST_NOT,
    AST_UNARY
} ASTType;

typedef struct AST {
    ASTType type;

    struct {
        char *full;
        char *func;
        char *file;
        char *module;
    } scope;

    size_t ln;
    size_t col;
    bool dont_free;

    union {
        ASTList root;

        union {
            int64_t i64;
        } constant;

        struct {
            char *name;
            AST *sym;
        } var;

        struct {
            char *name;
            char *type;
            ASTList params;
            ASTList body;
            AST *ret;
        } func;

        struct {
            char *name;
            ASTList args;
            AST *sym;
        } call;

        struct {
            char *name;
            char *type;
            AST *value;
        } decl;

        struct {
            char *name;
            AST *value;
            AST *sym;
        } assign;

        struct {
            AST *value;
            AST *sym;
        } ret;

        char *asm_block;
        TokenType oper;

        struct {
            ASTList values;
            bool is_float;
        } math;

        AST *parens;

        struct {
            ASTList values;
            bool is_float;
        } condition;

        struct {
            AST *condition;
            ASTList body;
            ASTList else_body;
        } if_stmt;

        struct {
            AST *counter;
            AST *start;
            AST *end;
            AST *step;
            ASTList body;
            bool reverse;
        } for_stmt;

        struct {
            AST *condition;
            ASTList body;
        } while_stmt;

        AST *not_value;
        AST *unary_value;
    };
} AST;

ASTList create_astlist();
void delete_astlist(ASTList *list);
void astlist_push(ASTList *list, AST *item);


AST *create_ast(ASTType type, size_t ln, size_t col);
void delete_ast(AST *ast);
char *asttype_to_string(ASTType type);

#endif
