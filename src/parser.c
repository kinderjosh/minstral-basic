#include "parser.h"
#include "token.h"
#include "lexer.h"
#include "ast.h"
#include "symbol_table.h"
#include "error.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <stdint.h>
#include <errno.h>
#include <inttypes.h>

#define NOP(ln, col) create_ast(AST_NOP, ln, col)
#define STARTING_TOK_CAP 32

// Parser flags.
#define IN_FIRST_PASS (0x01)
#define IN_MATH (0x02)
#define IN_CONDITION (0x04)
#define IN_LOOP (0x08)
#define IN_IF (0x10)

char *cur_scope;
char *cur_func;
char *cur_file;
char *cur_module;

// Freeing any one of these will cause an invalid read
// in another AST pointer, so we have to free them at the end.
static ASTList duplicates;

Parser create_parser(char *file) {
    Lexer lex = create_lexer(file);
    Token tok;

    Token *tokens = malloc(STARTING_TOK_CAP * sizeof(Token));
    size_t token_count = 0;
    size_t token_capacity = STARTING_TOK_CAP;

    while ((tok = lex_next_token(&lex)).type != TOK_EOF) {
        // An extra +1 for the EOF.
        if (token_count + 2 >= token_capacity) {
            token_capacity *= 2;
            tokens = realloc(tokens, token_capacity * sizeof(Token));
        }

        tokens[token_count++] = tok;
    }

    // EOF token.
    tokens[token_count++] = tok;
    delete_lexer(&lex);
    return (Parser){ .file = file, .tokens = tokens, .token_count = token_count, .tok = &tokens[0], .pos = 0, .flags = IN_FIRST_PASS };
}

void delete_parser(Parser *prs) {
    for (size_t i = 0; i < prs->token_count; i++)
        delete_token(&prs->tokens[i]);

    free(prs->tokens);
}

static void eat(Parser *prs, TokenType type) {
    if (type != prs->tok->type) {
        log_error(prs->file, prs->tok->ln, prs->tok->col);
        fprintf(stderr, "found token '%s' when expecting '%s'\n", tokentype_to_string(prs->tok->type), tokentype_to_string(type));
        show_error(prs->file, prs->tok->ln, prs->tok->col);
    }

    if (prs->tok->type != TOK_EOF)
        prs->tok = &prs->tokens[++prs->pos];
}

static Token *peek(Parser *prs, int offset) {
    if (prs->pos + offset >= prs->token_count)
        return &prs->tokens[prs->token_count - 1];
    else if ((int)prs->pos + offset < 1)
        return &prs->tokens[0];
    
    return &prs->tokens[prs->pos + offset];
}

void eat_until(Parser *prs, TokenType type) {
    while (prs->tok->type != TOK_EOF && prs->tok->type != type)
        eat(prs, prs->tok->type);
}

void eat_until_value(Parser *prs, char *value) {
    while (prs->tok->type != TOK_EOF && (prs->tok->type != TOK_ID || strcmp(prs->tok->value, value) != 0))
        eat(prs, prs->tok->type);
}

AST *parse_value(Parser *prs, char *target_type);

bool is_math(Parser *prs) {
    switch (prs->tok->type) {
        case TOK_PLUS:
        case TOK_MINUS:
        case TOK_STAR:
        case TOK_SLASH:
        case TOK_PERCENT:
        case TOK_SHL:
        case TOK_SHR: return true;
        case TOK_AND:
        case TOK_OR:
        case TOK_XOR: return peek(prs, 1)->type != prs->tok->type; // && and || are conditionals.
        // NOT and LOG_NOT aren't chained, as they are unary ops.
        default: break;
    }

    return false;
}

bool value_is_float(AST *ast) {
    (void)ast;
    return false;
    //char *type = get_value_type(ast);
    //return strcmp(type, "f32") == 0 || strcmp(type, "f64") == 0;
}

char *get_value_type(AST *ast) {
    (void)ast;
    return "i64";
}

AST *parse_oper(Parser *prs) {
    AST *ast = create_ast(AST_OPER, prs->tok->ln, prs->tok->col);
    ast->oper = prs->tok->type;
    eat(prs, prs->tok->type);
    return ast;
}

AST *parse_math(Parser *prs, AST *first) {
    bool was_in_math = prs->flags & IN_MATH;

    if (!was_in_math)
        prs->flags |= IN_MATH;

    if (first == NULL)
        first = parse_value(prs, NULL);

    AST *ast = create_ast(AST_MATH, first->ln, first->col);
    ast->math.values = create_astlist();
    ast->math.is_float = false;

    astlist_push(&ast->math.values, first);

    if (value_is_float(first))
        ast->math.is_float = true;

    AST *value;
    char *type;

    while (is_math(prs)) {
        astlist_push(&ast->math.values, parse_oper(prs));

        value = parse_value(prs, NULL);
        type = get_value_type(value);

        if (!ast->math.is_float && (strcmp(type, "f32") == 0 || strcmp(type, "f64") == 0))
            ast->math.is_float = true;

        astlist_push(&ast->math.values, value);
    }

    if (!was_in_math)
        prs->flags &= ~IN_MATH;

    return ast;
}

bool is_conditional_and_or(Parser *prs) {
    return strcmp(prs->tok->value, "and") == 0 || strcmp(prs->tok->value, "or") == 0;
}

bool is_condition(Parser *prs) {
    switch (prs->tok->type) {
        case TOK_EQ:
        case TOK_NEQ:
        case TOK_LT:
        case TOK_LTE:
        case TOK_GT:
        case TOK_GTE: return true;
        case TOK_ID: return is_conditional_and_or(prs);
        default: break;
    }

    return false;
}

AST *parse_condition(Parser *prs, AST *begin) {
    bool was_in_condition = prs->flags & IN_CONDITION;

    if (!was_in_condition)
        prs->flags |= IN_CONDITION;

    if (begin == NULL)
        begin = parse_value(prs, NULL);

    AST *ast = create_ast(AST_CONDITION, begin->ln, begin->col);
    ast->condition.values = create_astlist();
    ast->condition.is_float = value_is_float(begin);

    if (!is_condition(prs) || is_conditional_and_or(prs)) {
        astlist_push(&ast->condition.values, begin);

        AST *oper = create_ast(AST_OPER, begin->ln, begin->col);
        oper->oper = TOK_NEQ;
        astlist_push(&ast->condition.values, oper);

        AST *constval = create_ast(AST_INT, begin->ln, begin->col);
        constval->constant.i64 = 0;
        astlist_push(&ast->condition.values, constval);
    } else
        astlist_push(&ast->condition.values, begin);

    while (is_condition(prs)) {
        // && or ||, not a left hand side, need a left, oper and right value after.
        if (is_conditional_and_or(prs)) {
            AST *oper = create_ast(AST_OPER, prs->tok->ln, prs->tok->col);

            if (strcmp(prs->tok->value, "and") == 0)
                oper->oper = TOK_AND;
            else if (strcmp(prs->tok->value, "or") == 0)
                oper->oper = TOK_OR;
            else {
                oper->oper = prs->tok->type;
                eat(prs, prs->tok->type);
            }

            eat(prs, prs->tok->type);
            astlist_push(&ast->condition.values, oper);

            AST *lhs = parse_value(prs, NULL);

            if (!ast->condition.is_float)
                ast->condition.is_float = value_is_float(lhs);

            if (!is_condition(prs) || is_conditional_and_or(prs)) {
                astlist_push(&ast->condition.values, lhs);

                AST *oper = create_ast(AST_OPER, lhs->ln, lhs->col);
                oper->oper = TOK_NEQ;
                astlist_push(&ast->condition.values, oper);

                AST *constval = create_ast(AST_INT, lhs->ln, lhs->col);
                constval->constant.i64 = 0;
                astlist_push(&ast->condition.values, constval);
            } else
                astlist_push(&ast->condition.values, lhs);

            continue;
        }

        AST *oper = create_ast(AST_OPER, prs->tok->ln, prs->tok->col);
        oper->oper = prs->tok->type;
        eat(prs, prs->tok->type);
        astlist_push(&ast->condition.values, oper);

        AST *rhs = parse_value(prs, NULL);
        astlist_push(&ast->condition.values, rhs);

        if (!ast->condition.is_float)
            ast->condition.is_float = value_is_float(rhs);
    }

    if (!was_in_condition)
        prs->flags &= ~IN_CONDITION;

    return ast;
}

AST *parse_value(Parser *prs, char *target_type) {
    (void)target_type;

    AST *value = parse_stmt(prs);

    switch (value->type) {
        case AST_NOP:
        case AST_INT:
        case AST_VAR:
        case AST_CALL:
        case AST_MATH:
        case AST_PARENS:
        case AST_CONDITION: break;
        default:
            log_error(prs->file, value->ln, value->col);
            fprintf(stderr, "invalid value '%s'\n", asttype_to_string(value->type));
            show_error(prs->file, value->ln, value->col);
            break;
    }

    if (!(prs->flags & IN_MATH) && is_math(prs))
        value = parse_math(prs, value);

    if (!(prs->flags & IN_CONDITION) && is_condition(prs))
        value = parse_condition(prs, value);

    return value;
}

void skip_body(Parser *prs) {
    eat(prs, TOK_ID);
    unsigned int indents = 1;

    while (prs->tok->type != TOK_EOF && indents > 0) {
        if (prs->tok->type == TOK_ID && 
                (strcmp(prs->tok->value, "if") == 0 || strcmp(prs->tok->value, "for") == 0 || strcmp(prs->tok->value, "while") == 0))
            indents++;
        else if (prs->tok->type == TOK_ID && strcmp(prs->tok->value, "end") == 0)
            indents--;

        eat(prs, prs->tok->type);
    }
}

ASTList parse_body(Parser *prs, bool single_stmt) {
    ASTList body = create_astlist();

    while (prs->tok->type != TOK_EOF && strcmp(prs->tok->value, "end") != 0) {
        if (prs->flags & IN_IF && strcmp(prs->tok->value, "else") == 0)
            return body;

        AST *stmt = parse_stmt(prs);

        switch (stmt->type) {
            case AST_NOP:
                delete_ast(stmt);
                continue;
            case AST_DECL:
            case AST_ASSIGN:
            case AST_CALL:
            case AST_RET:
            case AST_ASM_BLOCK:
            case AST_IF:
            case AST_FOR:
            case AST_WHILE: break;
            default:
                log_error(prs->file, stmt->ln, stmt->col);
                fprintf(stderr, "invalid statement '%s' in subroutine '%s'\n", asttype_to_string(stmt->type), cur_func);
                show_error(prs->file, stmt->ln, stmt->col);
                break;
        }

        astlist_push(&body, stmt);

        // Only allow one statement.
        if (single_stmt)
            break;
    }

    if (!single_stmt)
        eat(prs, TOK_ID);

    return body;
}

AST *parse_subroutine(Parser *prs) {
    const size_t ln = prs->tok->ln;
    const size_t col = prs->tok->col;

    char *name = mystrdup(prs->tok->value);
    eat(prs, TOK_ID);

    AST *sym = find_symbol(AST_FUNC, name, GLOBAL, cur_module);

    if (sym != NULL && (prs->flags & IN_FIRST_PASS)) {
        log_error(prs->file, ln, col);
        fprintf(stderr, "redefinition of subroutine '%s'; first defined at %s:%zu:%zu\n", name, sym->scope.file, sym->ln, sym->col);
        show_error(prs->file, ln, col);

        eat_until(prs, TOK_RPAREN);
        eat(prs, TOK_RPAREN);
        skip_body(prs);
        free(name);
        return NOP(ln, col);
    } else if (sym != NULL) {
        // In the second pass, we've already added the function
        // to the symbol table, we're only interested in the body now.
        eat_until(prs, TOK_RPAREN);
        eat(prs, TOK_RPAREN);

        free(cur_scope);
        free(cur_func);

        cur_scope = mystrdup(name);
        cur_func = mystrdup(name);
        free(name);

        sym->func.ret = NULL;
        sym->func.body = parse_body(prs, false);

        free(cur_scope);
        free(cur_func);

        cur_scope = mystrdup(GLOBAL);
        cur_func = calloc(1, sizeof(char));
        return sym; // Return the complete AST.
    }

    // In the first pass, we only care about adding the function
    // to the symbol table, we don't want the body yet.
    AST *ast = create_ast(AST_FUNC, ln, col);
    ast->func.name = name;
    ast->func.type = mystrdup("i64");

    free(cur_scope);
    free(cur_func);

    cur_scope = mystrdup(name);
    cur_func = mystrdup(name);

    ast->func.params = create_astlist();
    eat(prs, TOK_LPAREN);

    while (prs->tok->type != TOK_EOF && prs->tok->type != TOK_RPAREN) {
        if (ast->func.params.size > 0)
            eat(prs, TOK_COMMA);

        char *param = mystrdup(prs->tok->value);
        AST *param_sym = find_symbol(AST_DECL, param, cur_scope, cur_module);

        if (param_sym != NULL) {
            log_error(prs->file, prs->tok->ln, prs->tok->col);
            fprintf(stderr, "redefinition of variable '%s'; first defined at %s:%zu:%zu\n", param, param_sym->scope.file ,param_sym->ln, param_sym->col);
            show_error(prs->file, prs->tok->ln, prs->tok->col);
            free(param);
        } else {
            param_sym = create_ast(AST_DECL, prs->tok->ln, prs->tok->col);
            param_sym->decl.name = param;
            param_sym->decl.type = mystrdup("i64");
            param_sym->decl.value = NULL;
            add_symbol(param_sym);
            astlist_push(&ast->func.params, param_sym);
        }

        eat(prs, TOK_ID);
    }

    eat(prs, TOK_RPAREN);
    add_symbol(ast);

    free(cur_scope);
    free(cur_func);

    cur_scope = mystrdup(GLOBAL);
    cur_func = calloc(1, sizeof(char));

    // Skip parsing any local variables in the first pass.
    skip_body(prs);
    return NOP(ln, col);
}

AST *parse_call(Parser *prs, char *name, const size_t ln, const size_t col) {
    AST *sym = find_symbol(AST_FUNC, name, GLOBAL, cur_module);

    if (sym == NULL) {
        log_error(prs->file, ln, col);
        fprintf(stderr, "undefined subroutine '%s'\n", name);
        show_error(prs->file, ln, col);

        free(name);
        eat_until(prs, TOK_RPAREN);
        eat(prs, TOK_RPAREN);
        return NOP(ln, col);
    }

    AST *ast = create_ast(AST_CALL, ln, col);
    ast->call.name = name;
    ast->call.args = create_astlist();
    ast->call.sym = sym;
    eat(prs, TOK_LPAREN);

    while (prs->tok->type != TOK_EOF && prs->tok->type != TOK_RPAREN) {
        if (ast->call.args.size > 0)
            eat(prs, TOK_COMMA);

        astlist_push(&ast->call.args, parse_value(prs, sym->func.params.items[ast->call.args.size]->decl.type));
    }

    eat(prs, TOK_RPAREN);
    return ast;
}

AST *parse_assign(Parser *prs, char *name, const size_t ln, const size_t col) {
    AST *sym = find_symbol(AST_DECL, name, cur_scope, cur_module);

    if (sym == NULL) {
        AST *ast = create_ast(AST_DECL, ln, col);
        ast->decl.name = name;
        ast->decl.type = mystrdup("i64");
        ast->decl.value = parse_value(prs, ast->decl.type);
        add_symbol(ast);
        return ast;
    } else if (prs->flags & IN_FIRST_PASS)
        return NOP(ln, col);

    // This should only get here in the second pass,
    // so we can return the complete AST.
    AST *ast = create_ast(AST_ASSIGN, ln, col);
    ast->assign.name = name;
    ast->assign.sym = sym;
    ast->assign.value = parse_value(prs, sym->decl.type);
    return ast;
}

AST *parse_ret(Parser *prs, size_t ln, size_t col) {
    AST *ast = create_ast(AST_RET, ln, col);

    // Probably aren't trying to return this thing if it's
    // on another line.
    if (prs->tok->ln != ln)
        ast->ret.value = NULL;
    else
        ast->ret.value = parse_value(prs, "i64");

    ast->ret.sym = find_symbol(AST_FUNC, cur_func, GLOBAL, cur_module); // Can be NULL in global.
    return ast;
}

AST *parse_asm(Parser *prs, size_t ln, size_t col) {
    char *code = malloc(32);
    code[0] = '\0';
    size_t capacity = 32;
    size_t code_len = 0;

    while (prs->tok->type != TOK_EOF && (prs->tok->type != TOK_ID || strcmp(prs->tok->value, "end") != 0)) {
        const size_t len = strlen(prs->tok->value);

        if (code_len + len + 3 >= capacity) {
            capacity *= 2;
            code = realloc(code, capacity);
        }

        strcat(code, prs->tok->value);

        if (prs->tok->type != TOK_AT && peek(prs, 1)->type != TOK_AT)
            strcat(code, " ");

        eat(prs, prs->tok->type);
        code_len += len + 1;

        if (prs->tok->type == TOK_COMMA) {
            eat(prs, TOK_COMMA);
            strcat(code, "\n");
        }
    }

    eat(prs, TOK_ID);

    AST *ast = create_ast(AST_ASM_BLOCK, ln, col);
    ast->asm_block = code;
    return ast;
}

AST *parse_if(Parser *prs, size_t ln, size_t col) {
    AST *ast = create_ast(AST_IF, ln, col);
    ast->if_stmt.condition = parse_condition(prs, NULL);

    char *new_scope = malloc((strlen(cur_scope) + 64) * sizeof(char));
    sprintf(new_scope, "%s@if%zu%zu", cur_scope, ln, col);

    char *old_scope = malloc((strlen(cur_scope) + 1) * sizeof(char));
    strcpy(old_scope, cur_scope);

    free(cur_scope);
    cur_scope = new_scope;

    unsigned int flags = prs->flags;
    prs->flags |= IN_IF;

    ast->if_stmt.body = parse_body(prs, false);

    prs->flags = flags;

    if (strcmp(prs->tok->value, "else") == 0) {
        size_t else_ln = prs->tok->ln;
        eat(prs, TOK_ID);

        char *else_scope = malloc(strlen(cur_scope) + 64);
        sprintf(else_scope, "%s@else%zu%zu", cur_scope, ln, col);

        free(cur_scope);
        cur_scope = else_scope;

        ast->if_stmt.else_body = parse_body(prs, strcmp(prs->tok->value, "if") == 0 && prs->tok->ln == else_ln);
    } else
        ast->if_stmt.else_body = create_astlist(); // Empty.

    free(cur_scope);
    cur_scope = old_scope;
    return ast;
}

AST *parse_compound_math(Parser *prs, AST *dst) {
    // Avoid double free from cloning this node.
    dst->dont_free = true;
    astlist_push(&duplicates, dst);

    AST *oper = create_ast(AST_OPER, prs->tok->ln, prs->tok->col);
    oper->oper = prs->tok->type;
    eat(prs, prs->tok->type);
    eat(prs, TOK_EQUAL);

    AST *value = parse_value(prs, NULL);

    AST *math = create_ast(AST_MATH, value->ln, value->col);
    math->math.values = create_astlist();
    astlist_push(&math->math.values, dst);
    astlist_push(&math->math.values, oper);
    astlist_push(&math->math.values, value);
    math->math.is_float = value_is_float(dst) || value_is_float(value);

    AST *ast;

    switch (dst->type) {
        case AST_VAR:
            ast = create_ast(AST_ASSIGN, dst->ln, dst->col);
            ast->assign.name = mystrdup(dst->var.name);
            ast->assign.sym = dst->var.sym;
            ast->assign.value = math;
            break;
        default:
            assert(false);
            ast = NOP(dst->ln, dst->col);
            break;
    }

    return ast;
}

AST *parse_var(Parser *prs, char *id, AST *sym, size_t ln, size_t col) {
    AST *ast = create_ast(AST_VAR, ln, col);
    ast->var.name = id;
    ast->var.sym = sym;

    if (is_math(prs) && peek(prs, 1)->type == TOK_EQUAL)
        return parse_compound_math(prs, ast);

    return ast;
}

AST *parse_for(Parser *prs, size_t ln, size_t col) {
    unsigned int flags = prs->flags;
    prs->flags |= IN_LOOP;

    AST *ast = create_ast(AST_FOR, ln, col);

    if (strcmp(prs->tok->value, "rev") == 0) {
        eat(prs, TOK_ID);
        ast->for_stmt.reverse = true;
    } else
        ast->for_stmt.reverse = false;

    char *new_scope = malloc(strlen(cur_scope) + 64);
    sprintf(new_scope, "%s@for%zu%zu", cur_scope, ln, col);

    char *old_scope = malloc(strlen(cur_scope) + 1);
    strcpy(old_scope, cur_scope);

    free(cur_scope);
    cur_scope = new_scope;

    AST *counter = parse_stmt(prs);

    if (counter->type == AST_VAR)
        ast->for_stmt.start = ast;
    else if (counter->type == AST_DECL)
        ast->for_stmt.start = counter->decl.value;
    else if (counter->type == AST_ASSIGN)
        ast->for_stmt.start = counter->assign.value;
    else {
        log_error(prs->file, counter->ln, counter->col);
        fprintf(stderr, "invalid counter value; expected variable or assignment but found '%s'\n", asttype_to_string(counter->type));
        show_error(prs->file, counter->ln, counter->col);
    }
    
    ast->for_stmt.counter = counter;
    eat(prs, TOK_ID);
    ast->for_stmt.end = parse_value(prs, NULL);

    if (strcmp(prs->tok->value, "step") == 0) {
        eat(prs, TOK_ID);
        ast->for_stmt.step = parse_value(prs, NULL);
    } else {
        ast->for_stmt.step = create_ast(AST_INT, prs->tok->ln, prs->tok->col);
        ast->for_stmt.step->constant.i64 = ast->for_stmt.reverse ? -1 : 1;
    }

    ast->for_stmt.body = parse_body(prs, false);

    prs->flags = flags;

    free(cur_scope);
    cur_scope = old_scope;
    return ast;
}

AST *parse_while(Parser *prs, size_t ln, size_t col) {
    AST *ast = create_ast(AST_WHILE, ln, col);

    char *new_scope = malloc(strlen(cur_scope) + 64);
    sprintf(new_scope, "%s@while%zu%zu", cur_scope, ln, col);

    char *old_scope = malloc(strlen(cur_scope) + 1);
    strcpy(old_scope, cur_scope);

    free(cur_scope);
    cur_scope = new_scope;

    ast->while_stmt.condition = parse_condition(prs, NULL);

    unsigned int flags = prs->flags;
    prs->flags |= IN_LOOP;

    ast->while_stmt.body = parse_body(prs, false);

    prs->flags = flags;

    free(cur_scope);
    cur_scope = old_scope;
    return ast;
}

AST *parse_id(Parser *prs) {
    const size_t ln = prs->tok->ln;
    const size_t col = prs->tok->col;

    char *id = mystrdup(prs->tok->value);
    eat(prs, TOK_ID);

    if (prs->tok->type == TOK_EQUAL) {
        eat(prs, TOK_EQUAL);
        return parse_assign(prs, id, ln, col);
    } else if (prs->tok->type == TOK_LPAREN)
        return parse_call(prs, id, ln, col);
    else if (strcmp(id, "sub") == 0) {
        free(id);
        return parse_subroutine(prs);
    } else if (strcmp(id, "if") == 0) {
        free(id);
        return parse_if(prs, ln, col);
    } else if (strcmp(id, "return") == 0) {
        free(id);
        return parse_ret(prs, ln, col);
    } else if (strcmp(id, "for") == 0) {
        free(id);
        return parse_for(prs, ln, col);
    } else if (strcmp(id, "while") == 0) {
        free(id);
        return parse_while(prs, ln, col);
    } else if (strcmp(id, "asm") == 0) {
        free(id);
        return parse_asm(prs, ln, col);
    }

    AST *sym = find_symbol(AST_DECL, id, cur_scope, cur_module);

    if (sym != NULL)
        return parse_var(prs, id, sym, ln, col);

    // Only show this error once, otherwise it would
    // print twice, once each pass.
    log_error(prs->file, ln, col);
    fprintf(stderr, "undefined identifier '%s'\n", id);
    show_error(prs->file, ln, col);

    free(id);
    return NOP(ln, col);
}

AST *parse_constant(Parser *prs) {
    AST *ast = create_ast(AST_INT, prs->tok->ln, prs->tok->col);
    errno = 0;
    char *endptr;

    ast->constant.i64 = strtoll(prs->tok->value, &endptr, 10);

    if (endptr == prs->tok->value || *endptr != '\0') {
        log_error(prs->file, prs->tok->ln, prs->tok->col);
        fprintf(stderr, "digit conversion failed\n");
        show_error(prs->file, prs->tok->ln, prs->tok->col);
        ast->constant.i64 = 0;
    } else if (errno == ERANGE || errno == EINVAL) {
        log_error(prs->file, prs->tok->ln, prs->tok->col);
        fprintf(stderr, "digit conversion failed: %s\n", strerror(errno));
        show_error(prs->file, prs->tok->ln, prs->tok->col);
        ast->constant.i64 = 0;
    }

    eat(prs, TOK_INT);
    return ast;
}

AST *parse_parens(Parser *prs) {
    unsigned int flags = prs->flags;
    prs->flags &= ~(IN_MATH | IN_CONDITION);

    AST *ast = create_ast(AST_PARENS, prs->tok->ln, prs->tok->col);
    eat(prs, TOK_LPAREN);
    ast->parens = parse_value(prs, NULL);
    eat(prs, TOK_RPAREN);

    prs->flags = flags;
    return ast;
}

AST *parse_stmt(Parser *prs) {
    while (prs->tok->type == TOK_EOL)
        eat(prs, TOK_EOL);

    switch (prs->tok->type) {
        case TOK_EOF: return NOP(prs->tok->ln, prs->tok->col);
        case TOK_ID: return parse_id(prs);
        case TOK_INT: return parse_constant(prs);
        case TOK_LPAREN: return parse_parens(prs);
        default: break;
    }

    const size_t ln = prs->tok->ln;
    const size_t col = prs->tok->col;

    log_error(prs->file, prs->tok->ln, prs->tok->col);
    fprintf(stderr, "invalid statement '%s'\n", tokentype_to_string(prs->tok->type));
    show_error(prs->file, prs->tok->ln, prs->tok->col);
    eat(prs, prs->tok->type);
    return NOP(ln, col);
}

AST *parse_root(char *file) {
    cur_scope = mystrdup(GLOBAL);
    cur_func = calloc(1, sizeof(char));
    cur_file = mystrdup(file);
    cur_module = mystrdup("__main");

    Parser prs = create_parser(file);
    AST *root = create_ast(AST_ROOT, 1, 1);
    root->root = create_astlist();

    // In the first pass we only care about adding
    // functions and global variables to the symbol table.
    while (prs.tok->type != TOK_EOF) {
        if (prs.tok->type == TOK_ID && strcmp(prs.tok->value, "sub") == 0) {
            AST *stmt = parse_stmt(&prs);

            if (stmt->type == AST_DECL)
                astlist_push(&root->root, stmt);
            else {
                // We can just nuke NOPs straight away.
                assert(stmt->type == AST_NOP);
                delete_ast(stmt);
            }
        } else if (prs.tok->type != TOK_ID)
            eat_until(&prs, TOK_ID);
        else
            eat(&prs, prs.tok->type);
    }

    // Now do the second pass, fill in function bodies and everything else.
    prs.tok = &prs.tokens[0];
    prs.pos = 0;
    prs.flags = 0;

    while (prs.tok->type != TOK_EOF) {
        AST *stmt = parse_stmt(&prs);

        switch (stmt->type) {
            case AST_NOP:
                delete_ast(stmt);
                continue;
            case AST_FUNC:
            case AST_CALL:
            case AST_DECL:
            case AST_ASSIGN:
            case AST_RET:
            case AST_ASM_BLOCK:
            case AST_IF:
            case AST_FOR:
            case AST_WHILE: break;
            default:
                log_error(prs.file, stmt->ln, stmt->col);
                fprintf(stderr, "invalid statement '%s'\n", asttype_to_string(stmt->type));
                show_error(prs.file, stmt->ln, stmt->col);
                break;
        }

        astlist_push(&root->root, stmt);
    }

    delete_parser(&prs);

    free(cur_scope);
    free(cur_func);
    free(cur_file);
    free(cur_module);
    return root;
}

void create_duplicates() {
    duplicates = create_astlist();
}

void delete_duplicates() {
    for (size_t i = 0; i < duplicates.size; i++) {
        duplicates.items[i]->dont_free = false;
        delete_ast(duplicates.items[i]);
    }

    free(duplicates.items);
}