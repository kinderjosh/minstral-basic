#include "ast.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

extern char *cur_scope;
extern char *cur_func;
extern char *cur_file;
extern char *cur_module;

ASTList create_astlist() {
    return (ASTList){ .items = malloc(16 * sizeof(AST *)), .size = 0, .capacity = 16 };
}

void astlist_push(ASTList *list, AST *item) {
    if (list->size + 1 >= list->capacity) {
        list->capacity *= 2;
        list->items = realloc(list->items, list->capacity * sizeof(AST *));
    }

    list->items[list->size++] = item;
}

void delete_astlist(ASTList *list) {
    for (size_t i = 0; i < list->size; i++)
        delete_ast(list->items[i]);

    free(list->items);
}

AST *create_ast(ASTType type, size_t ln, size_t col) {
    AST *ast = malloc(sizeof(AST));
    ast->type = type;
    ast->scope.full = mystrdup(cur_scope);
    ast->scope.func = mystrdup(cur_func);
    ast->scope.file = mystrdup(cur_file);
    ast->scope.module = mystrdup(cur_module);
    ast->ln = ln;
    ast->col = col;
    ast->dont_free = false;
    return ast;
}

void delete_ast(AST *ast) {
    if (ast->dont_free)
        return;

    switch (ast->type) {
        case AST_ROOT:
            delete_astlist(&ast->root);
            break;
        case AST_STRING:
            free(ast->constant.string);
            break;
        case AST_VAR:
            free(ast->var.name);
            break;
        case AST_FUNC:
            free(ast->func.name);
            free(ast->func.type);
            delete_astlist(&ast->func.params);
            delete_astlist(&ast->func.body);
            break;
        case AST_CALL:
            free(ast->call.name);
            delete_astlist(&ast->call.args);
            break;
        case AST_DECL:
            free(ast->decl.name);
            free(ast->decl.type);

            if (ast->decl.value != NULL)
                delete_ast(ast->decl.value);
            break;
        case AST_ASSIGN:
            free(ast->assign.name);
            delete_ast(ast->assign.value);
            break;
        case AST_RET:
            if (ast->ret.value != NULL)
                delete_ast(ast->ret.value);
            break;
        case AST_ASM_BLOCK:
            free(ast->asm_block);
            break;
        case AST_MATH:
            delete_astlist(&ast->math.values);
            break;
        case AST_PARENS:
            delete_ast(ast->parens);
            break;
        case AST_CONDITION:
            delete_astlist(&ast->condition.values);
            break;
        case AST_IF:
            delete_ast(ast->if_stmt.condition);
            delete_astlist(&ast->if_stmt.body);
            delete_astlist(&ast->if_stmt.else_body);
            break;
        case AST_FOR:
            delete_ast(ast->for_stmt.counter);
            delete_ast(ast->for_stmt.end);
            delete_ast(ast->for_stmt.step);
            delete_astlist(&ast->for_stmt.body);
            break;
        case AST_WHILE:
            delete_ast(ast->while_stmt.condition);
            delete_astlist(&ast->while_stmt.body);
            break;
        case AST_NOT:
            delete_ast(ast->not_value);
            break;
        case AST_UNARY:
            delete_ast(ast->unary_value);
            break;
        case AST_LOOP_WORD:
            free(ast->loop_word);
            break;
        case AST__RES__:
            delete_ast(ast->__res__);
            break;
        case AST_INDEX:
            delete_ast(ast->index.base);
            delete_ast(ast->index.index);

            if (ast->index.value != NULL)
                delete_ast(ast->index.value);
            break;
        default: break;
    }

    free(ast->scope.full);
    free(ast->scope.func);
    free(ast->scope.file);
    free(ast->scope.module);
    free(ast);
}

char *asttype_to_string(ASTType type) {
    switch (type) {
        case AST_NOP: return "nop";
        case AST_ROOT: return "root";
        case AST_INT: return "int";
        case AST_STRING: return "string";
        case AST_VAR: return "variable";
        case AST_FUNC: return "subroutine";
        case AST_CALL: return "call";
        case AST_DECL: return "variable declaration";
        case AST_ASSIGN: return "variable assignment";
        case AST_RET: return "return";
        case AST_ASM_BLOCK: return "assembly block";
        case AST_OPER: return "operator";
        case AST_MATH: return "math expression";
        case AST_PARENS: return "parentheses";
        case AST_CONDITION: return "condition";
        case AST_IF: return "if";
        case AST_FOR: return "for";
        case AST_WHILE: return "while";
        case AST_NOT: return "not";
        case AST_UNARY: return "unary";
        case AST_LOOP_WORD: return "loop word";
        case AST__RES__: return "__res__";
        case AST_INDEX: return "index";
    }

    assert(false);
    return "undefined";
}
