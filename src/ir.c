#include "ir.h"
#include "ast.h"
#include "error.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>

#define NOVAL (OpValue){ .type = VAL_NONE }
#define STARTING_PROG_CAP 16
#define SOURCE(ast) (Source){ .scope = ast->scope.full, .func = ast->scope.func, .module = ast->scope.module }

#define LOADING_VALUE_WILL_CORRUPT(type) (type == AST_CALL || type == AST_MATH || type == AST_CONDITION)

static IR program;
static OpValue temp_var;

static unsigned int label_count;
static unsigned int cur_loop_label;
static unsigned int cur_end_loop_label;

static void push(OpType type, OpValue dst, OpValue src) {
    if (program.op_count + 1 >= program.op_capacity) {
        program.op_capacity *= 2;
        program.ops = realloc(program.ops, program.op_capacity * sizeof(Op));
    }

    program.ops[program.op_count++] = (Op){ .type = type, .dst = dst, .src = src };
}

void push_stmt(AST *ast);

OpValue ast_to_value(AST *ast) {
    if (ast == NULL || ast->type == AST_NOP)
        return NOVAL;

    switch (ast->type) {
        case AST_INT: return (OpValue){ .type = VAL_INT, .int_const = ast->constant.i64 };
        case AST_VAR: return (OpValue){ .type = VAL_VAR, .source = SOURCE(ast->var.sym), .var = ast->var.name };
        case AST_CALL:
            push_stmt(ast);
            return (OpValue){ .type = VAL_RET, .source = (Source){ .scope = GLOBAL, .func = ast->call.name, .module = ast->scope.module } };
        case AST_MATH:
            push_stmt(ast);
            return (OpValue){ .type = VAL_REG, .reg = TEMP_REG };
        case AST_PARENS: return ast_to_value(ast->parens);
        case AST_CONDITION:
            push_stmt(ast);
            return (OpValue){ .type = VAL_REG, .reg = TEMP_REG };
        case AST_NOT: {
            OpValue temp_reg = (OpValue){ .type = VAL_REG, .reg = TEMP_REG };
            push(OP_LOAD, temp_reg, ast_to_value(ast->not_value));
            push(OP_NOT, temp_reg, temp_reg);
            return (OpValue){ .type = VAL_REG, .reg = TEMP_REG };
        }
        case AST_UNARY: {
            OpValue temp_reg = (OpValue){ .type = VAL_REG, .reg = TEMP_REG };
            push(OP_LOAD, temp_reg, ast_to_value(ast->not_value));
            push(OP_NEG, temp_reg, temp_reg);
            return (OpValue){ .type = VAL_REG, .reg = TEMP_REG };
        }
        default: break;
    }

    assert(false);
    return NOVAL;
}

IR ast_to_ir(AST *ast) {
    program = (IR){ .ops = malloc(STARTING_PROG_CAP * sizeof(Op)), .op_count = 0, .op_capacity = STARTING_PROG_CAP };
    label_count = 0;

    // Temporary variable.
    temp_var = (OpValue){ .type = VAL_VAR, .source = SOURCE(ast), .var = "@temp" };
    push(OP_NEW_VAR, NOVAL, temp_var);

    for (size_t i = 0; i < ast->root.size; i++)
        push_stmt(ast->root.items[i]);

    push(OP_NOP, NOVAL, NOVAL);
    return program;
}

void push_func(AST *ast) {
    push(OP_FUNC_BEGIN, NOVAL, (OpValue){ .source = SOURCE(ast), .ident = ast->func.name });
    label_count = 0;

    for (size_t i = 0; i < ast->func.params.size; i++)
        push(OP_NEW_VAR, NOVAL, (OpValue){ .type = VAL_VAR, .source = SOURCE(ast->func.params.items[i]), .var = ast->func.params.items[i]->decl.name });

    for (size_t i = 0; i < ast->func.body.size; i++)
        push_stmt(ast->func.body.items[i]);

    if (ast->func.body.size == 0 || (ast->func.body.items[ast->func.body.size - 1]->type != AST_RET))
        push(OP_RET, NOVAL, NOVAL);

    push(OP_FUNC_END, NOVAL, NOVAL);
}

void push_call(AST *ast) {
    for (size_t i = 0; i < ast->call.args.size; i++) {
        OpValue temp = (OpValue){ .type = VAL_REG, .reg = TEMP_REG };
        push(OP_LOAD, temp, ast_to_value(ast->call.args.items[i]));
        push(OP_STORE, (OpValue){ .type = VAL_VAR, .source = SOURCE(ast->call.sym->func.params.items[i]), .var = ast->call.sym->func.params.items[i]->decl.name }, temp);
    }

    push(OP_CALL, NOVAL, (OpValue){ .type = VAL_IDENT, .source = SOURCE(ast->call.sym), .ident = ast->call.name });
}

void push_decl(AST *ast) {
    OpValue temp = (OpValue){ .type = VAL_REG, .reg = TEMP_REG };
    OpValue var = (OpValue){ .type = VAL_VAR, .source = SOURCE(ast), .var = ast->decl.name };

    push(OP_NEW_VAR, NOVAL, var);
    push(OP_LOAD, temp, ast_to_value(ast->decl.value));
    push(OP_STORE, var, temp);
}

void push_assign(AST *ast) {
    OpValue temp = (OpValue){ .type = VAL_REG, .reg = TEMP_REG };
    push(OP_LOAD, temp, ast_to_value(ast->assign.value));
    push(OP_STORE, (OpValue){ .type = VAL_VAR, .source = SOURCE(ast->assign.sym), .var = ast->assign.name }, temp);
}

void push_ret(AST *ast) {
    if (ast->ret.value != NULL) {
        OpValue temp = (OpValue){ .type = VAL_REG, .reg = TEMP_REG };
        push(OP_LOAD, temp, ast_to_value(ast->ret.value));
        push(OP_STORE, (OpValue){ .type = VAL_RET, .source = SOURCE(ast) }, temp);
    }

    push(OP_RET, NOVAL, NOVAL);
}

int oper_to_prec(TokenType oper) {
    if (oper == TOK_SHL || oper == TOK_SHR || oper == TOK_AND || oper == TOK_OR || oper == TOK_XOR)
        return 0;
    else if (oper == TOK_PLUS || oper == TOK_MINUS)
        return 1;

    return 2;
}

bool higher_prec_later(ASTList *values, int cur) {
    int cur_prec = oper_to_prec(values->items[cur]->oper);

    for (size_t i = cur + 2; i < values->size; i += 2) {
        if (oper_to_prec(values->items[i]->oper) > cur_prec)
            return true;
    }

    return false;
}

OpType oper_to_optype(TokenType oper) {
    switch (oper) {
        case TOK_PLUS: return OP_ADD;
        case TOK_MINUS: return OP_SUB;
        case TOK_STAR: return OP_MUL;
        case TOK_SLASH: return OP_DIV;
        case TOK_PERCENT: return OP_MOD;
        case TOK_SHL: return OP_SHL;
        case TOK_SHR: return OP_SHR;
        case TOK_AND: return OP_AND;
        case TOK_OR: return OP_OR;
        default: return OP_XOR;
    }
}

void push_math(AST *ast) {
    ASTList *values = &ast->math.values;

    OpValue temp_reg = (OpValue){ .type = VAL_REG, .reg = TEMP_REG };

    size_t mid_opers[values->size / 2];
    size_t mid_oper_count = 0;

    size_t low_opers[values->size / 2];
    size_t low_oper_count = 0;

    push(OP_LOAD, temp_reg, ast_to_value(values->items[0]));
    push(OP_PUSH, NOVAL, temp_reg);

    for (size_t i = 1; i < values->size; i += 2) {
        TokenType oper = values->items[i]->oper;
        AST *value = values->items[i + 1];

        if (oper_to_prec(oper) == 2) {
            push(OP_LOAD, temp_reg, ast_to_value(value));
            push(OP_STORE, temp_var, temp_reg);

            push(OP_POP, temp_reg, NOVAL);
            push(oper_to_optype(oper), temp_reg, temp_var);
            push(OP_PUSH, NOVAL, temp_reg);
            continue;
        }
        
        if (oper_to_prec(oper) == 1 && higher_prec_later(values, i) && 
                (i + 2 > values->size || oper_to_prec(values->items[i + 2]->oper) != oper_to_prec(oper))) {

            push(OP_LOAD, temp_reg, ast_to_value(value));
            push(OP_PUSH, NOVAL, temp_reg);

            mid_opers[mid_oper_count++] = oper;
            continue;
        }
    
        if (mid_oper_count > 0) {
            for (size_t j = 0; j < mid_oper_count; j++) {
                push(OP_POP, temp_var, NOVAL);
                push(OP_POP, temp_reg, NOVAL);

                push(oper_to_optype(mid_opers[j]), temp_reg, temp_var);
                push(OP_PUSH, NOVAL, temp_reg);
            }

            mid_oper_count = 0;
        }

        if (oper_to_prec(oper) == 0 && higher_prec_later(values, i) && 
                (i + 2 > values->size || oper_to_prec(values->items[i + 2]->oper) != oper_to_prec(oper))) {

            push(OP_LOAD, temp_reg, ast_to_value(value));
            push(OP_PUSH, NOVAL, temp_reg);

            low_opers[low_oper_count++] = oper;
            continue;
        }

        if (low_oper_count > 0) {
            for (size_t j = 0; j < low_oper_count; j++) {
                push(OP_POP, temp_var, NOVAL);
                push(OP_POP, temp_reg, NOVAL);

                push(oper_to_optype(low_opers[j]), temp_reg, temp_var);
                push(OP_PUSH, NOVAL, temp_reg);
            }

            low_oper_count = 0;
        }

        push(OP_LOAD, temp_reg, ast_to_value(value));
        push(OP_STORE, temp_var, temp_reg);

        push(OP_POP, temp_reg, NOVAL);
        push(oper_to_optype(oper), temp_reg, temp_var);
        push(OP_PUSH, NOVAL, temp_reg);
    }

    for (size_t j = 0; j < mid_oper_count; j++) {
        push(OP_POP, temp_var, NOVAL);
        push(OP_POP, temp_reg, NOVAL);

        push(oper_to_optype(mid_opers[j]), temp_reg, temp_var);
        push(OP_PUSH, NOVAL, temp_reg);
    }

    for (size_t j = 0; j < low_oper_count; j++) {
        push(OP_POP, temp_var, NOVAL);
        push(OP_POP, temp_reg, NOVAL);

        push(oper_to_optype(low_opers[j]), temp_reg, temp_var);
        push(OP_PUSH, NOVAL, temp_reg);
    }

    push(OP_POP, temp_reg, NOVAL);
}

void push_condition(AST *ast) {
    ASTList *values = &ast->condition.values;
    size_t count = values->size;

    OpValue temp_reg = (OpValue){ .type = VAL_REG, .reg = TEMP_REG };

    unsigned int done_label = label_count++;
    bool pushed = false;

    for (size_t i = 2; i < count; i += 4) {
        AST *left = values->items[i - 2];
        AST *right = values->items[i];

        TokenType oper = values->items[i - 1]->oper;
        OpType set_op;

        bool pushed_res = false;

        push(OP_LOAD, temp_reg, ast_to_value(left));

        if (LOADING_VALUE_WILL_CORRUPT(right->type)) {
            push(OP_PUSH, NOVAL, temp_reg);
            push(OP_LOAD, temp_var, ast_to_value(right));
            push(OP_POP, NOVAL, temp_reg);
            push(OP_COMPARE, temp_reg, temp_var);
        } else
            push(OP_COMPARE, temp_reg, ast_to_value(right));

        switch (oper) {
            case TOK_EQ:
                set_op = OP_EQ;
                break;
            case TOK_NEQ:
                set_op = OP_NEQ;
                break;
            case TOK_LT:
                set_op = OP_LT;
                break;
            case TOK_LTE:
                set_op = OP_LTE;
                break;
            case TOK_GT:
                set_op = OP_GT;
                break;
            default:
                set_op = OP_GTE;
                break;
        }

        push(set_op, temp_reg, NOVAL);

        TokenType last_oper = i > 3 ? values->items[i - 3]->oper : 0;
        TokenType next_oper = i + 1 == count ? 0 : values->items[i + 1]->oper;

        if (pushed_res || last_oper == TOK_AND) {
            push(OP_STORE, temp_var, temp_reg);
            push(OP_POP, temp_reg, NOVAL);
        }

        if (last_oper == TOK_AND && i != 2) {
            push(OP_AND, temp_reg, temp_var);

            if (next_oper != TOK_AND)
                push(OP_BRANCH_TRUE, (OpValue){ .type = VAL_BRANCH, .source = SOURCE(ast), .branch = done_label }, NOVAL);
        } else {
            bool just_popped = false;

            if (last_oper != TOK_AND && next_oper != TOK_AND && count > 3) {
                if (pushed) {
                    push(OP_STORE, temp_var, temp_reg);
                    push(OP_POP, temp_reg, NOVAL);
                    just_popped = true;
                }

                push(OP_BRANCH_TRUE, (OpValue){ .type = VAL_BRANCH, .source = SOURCE(ast), .branch = done_label }, NOVAL);
            }

            if (last_oper == TOK_OR) {
                if (!just_popped) {
                    push(OP_STORE, temp_var, temp_reg);
                    push(OP_POP, temp_reg, NOVAL);
                }

                push(OP_OR, temp_reg, temp_var);
            }
        }

        if (count > 3 && (next_oper == TOK_AND || next_oper == TOK_OR)) {
            push(OP_PUSH, NOVAL, temp_reg);
            pushed = true;
        }
    }

    push(OP_NEW_BRANCH, NOVAL, (OpValue){ .type = VAL_BRANCH, .source = SOURCE(ast), .branch = done_label });
}

void push_block(ASTList *block) {
    for (size_t i = 0; i < block->size; i++)
        push_stmt(block->items[i]);
}

void push_if(AST *ast) {
    if (ast->if_stmt.body.size == 0 && ast->if_stmt.else_body.size == 0)
        return;

    push_condition(ast->if_stmt.condition);

    unsigned int true_label = label_count++;
    unsigned int false_label = label_count++;
    unsigned int final_label = ast->if_stmt.else_body.size > 0 ? label_count++ : false_label;

    push(OP_BRANCH_TRUE, (OpValue){ .type = VAL_BRANCH, .source = SOURCE(ast), .branch = true_label }, NOVAL);
    push(OP_JUMP, (OpValue){ .type = VAL_BRANCH, .source = SOURCE(ast), .branch = false_label }, NOVAL);

    push(OP_NEW_BRANCH, NOVAL, (OpValue){ .type = VAL_BRANCH, .source = SOURCE(ast), .branch = true_label });
    push_block(&ast->if_stmt.body);

    if (ast->if_stmt.else_body.size > 0) {
        push(OP_JUMP, (OpValue){ .type = VAL_BRANCH, .source = SOURCE(ast), .branch = final_label }, NOVAL);

        push(OP_NEW_BRANCH, NOVAL, (OpValue){ .type = VAL_BRANCH, .source = SOURCE(ast), .branch = false_label });
        push_block(&ast->if_stmt.else_body);
    }

    push(OP_NEW_BRANCH, NOVAL, (OpValue){ .type = VAL_BRANCH, .source = SOURCE(ast), .branch = final_label });
}

void push_for(AST *ast) {
    if (ast->for_stmt.counter->type == AST_DECL || ast->for_stmt.counter->type == AST_ASSIGN)
        push_stmt(ast->for_stmt.counter);

    unsigned int condition_label = label_count++;
    unsigned int next_loop_label = label_count++;
    unsigned int final_label = label_count++;

    OpValue temp_reg = (OpValue){ .type = VAL_REG, .reg = TEMP_REG };

    push(OP_NEW_BRANCH, NOVAL, (OpValue){ .type = VAL_BRANCH, .source = SOURCE(ast), .branch = condition_label });

    OpValue var;

    if (ast->for_stmt.counter->type == AST_VAR)
        var = (OpValue){ .type = VAL_VAR, .source = SOURCE(ast->for_stmt.counter->var.sym), .var = ast->for_stmt.counter->var.name };
    else if (ast->for_stmt.counter->type == AST_DECL)
        var = (OpValue){ .type = VAL_VAR, .source = SOURCE(ast->for_stmt.counter), .var = ast->for_stmt.counter->decl.name };
    else
        var = (OpValue){ .type = VAL_VAR, .source = SOURCE(ast->for_stmt.counter->assign.sym), .var = ast->for_stmt.counter->assign.name };

    push(OP_LOAD, temp_reg, var);
    push(OP_COMPARE, temp_reg, ast_to_value(ast->for_stmt.end));
    push(ast->for_stmt.reverse ? OP_LT : OP_GTE, temp_reg, NOVAL);
    push(OP_BRANCH_FALSE, (OpValue){ .type = VAL_BRANCH, .source = SOURCE(ast), .branch = final_label }, temp_reg);

    unsigned int before_loop_label = cur_loop_label;
    unsigned int before_end_loop_label = cur_end_loop_label;

    cur_loop_label = next_loop_label;
    cur_end_loop_label = final_label;

    push_block(&ast->for_stmt.body);

    push(OP_NEW_BRANCH, NOVAL, (OpValue){ .type = VAL_BRANCH, .source = SOURCE(ast), .branch = next_loop_label });

    push(OP_LOAD, temp_reg, var);
    push(OP_ADD, temp_reg, ast_to_value(ast->for_stmt.step));
    push(OP_STORE, var, temp_reg);

    cur_loop_label = before_loop_label;
    cur_end_loop_label = before_end_loop_label;

    push(OP_JUMP, (OpValue){ .type = VAL_BRANCH, .source = SOURCE(ast), .branch = condition_label }, NOVAL);
    push(OP_NEW_BRANCH, NOVAL, (OpValue){ .type = VAL_BRANCH, .source = SOURCE(ast), .branch = final_label });
}

void push_while(AST *ast) {
    unsigned int condition_label = label_count++;
    unsigned int final_label = label_count++;

    push(OP_NEW_BRANCH, NOVAL, (OpValue){ .type = VAL_BRANCH, .source = SOURCE(ast), .branch = condition_label });
    push_condition(ast->while_stmt.condition);

    push(OP_BRANCH_FALSE, (OpValue){ .type = VAL_BRANCH, .source = SOURCE(ast), .branch = final_label }, NOVAL);

    unsigned int before_loop_label = cur_loop_label;
    unsigned int before_end_loop_label = cur_end_loop_label;

    push_block(&ast->while_stmt.body);

    push(OP_JUMP, (OpValue){ .type = VAL_BRANCH, .source = SOURCE(ast), .branch = condition_label }, NOVAL);
    push(OP_NEW_BRANCH, NOVAL, (OpValue){ .type = VAL_BRANCH, .source = SOURCE(ast), .branch = final_label });

    cur_loop_label = before_loop_label;
    cur_end_loop_label = before_end_loop_label;
}

void push_stmt(AST *ast) {
    switch (ast->type) {
        case AST_FUNC:
            push_func(ast);
            break;
        case AST_CALL:
            push_call(ast);
            break;
        case AST_DECL:
            // Global DECLs have following ASSIGNs.
            //if (strcmp(ast->scope.full, GLOBAL) == 0)
             //   push(OP_NEW_VAR, NOVAL, (OpValue){ .type = VAL_VAR, .source = SOURCE(ast), .var = ast->decl.name });
            //else
                push_decl(ast);
            break;
        case AST_ASSIGN:
            push_assign(ast);
            break;
        case AST_RET:
            push_ret(ast);
            break;
        case AST_ASM_BLOCK:
            push(OP_INLINE_ASM, NOVAL, (OpValue){ .type = VAL_STRING, .string = ast->asm_block });
            break;
        case AST_MATH:
            push_math(ast);
            break;
        case AST_CONDITION:
            push_condition(ast);
            break;
        case AST_IF:
            push_if(ast);
            break;
        case AST_FOR:
            push_for(ast);
            break;
        case AST_WHILE:
            push_while(ast);
            break;
        default:
            assert(false);
            break;
    }
}

void delete_ir(IR *ir) {
    free(ir->ops);
}

static char *value_to_string(OpValue *value) {
    char *string;
    switch (value->type) {
        case VAL_NONE: return calloc(1, sizeof(char));
        case VAL_INT: 
            string = malloc(32);
            sprintf(string, "%" PRId64, value->int_const);
            return string;
        case VAL_REG:
            assert(value->reg == TEMP_REG);
            return mystrdup("@acc");
        case VAL_VAR: return mystrdup(value->var);
        case VAL_RET: return calloc(1, sizeof(char));
        case VAL_STACK: return mystrdup("@stack");
        case VAL_IDENT: return mystrdup(value->ident);
        case VAL_STRING: return mystrdup(value->string);
        case VAL_BRANCH:
            string = malloc(16);
            sprintf(string, "%u", value->branch);
            return string;
        default: break;
    }

    assert(false);
    return calloc(1, sizeof(char));
}

char *op_to_string(Op *op) {
    char *src = value_to_string(&op->src);
    char *dst = value_to_string(&op->dst);
    char *code = malloc(strlen(src) + strlen(dst) + 32);

    switch (op->type) {
        case OP_NOP:
            strcpy(code, "nop\n");
            break;
        case OP_FUNC_BEGIN:
            sprintf(code, "subroutine %s\n", op->src.ident);
            break;
        case OP_FUNC_END:
            sprintf(code, "end %s\n", op->src.ident);
            break;
        case OP_RET:
            strcpy(code, "return\n");
            break;
        case OP_NEW_VAR:
            sprintf(code, "var %s\n", src);
            break;
        case OP_LOAD:
            sprintf(code, "load %s, %s\n", dst, src);
            break;
        case OP_STORE:
            sprintf(code, "store %s, %s\n", dst, src);
            break;
        case OP_CALL:
            sprintf(code, "call %s\n", src);
            break;
        case OP_INLINE_ASM:
            sprintf(code, "asm {\n%s\n}\n", src);
            break;
        case OP_PUSH:
            sprintf(code, "push %s\n", src);
            break;
        case OP_POP:
            sprintf(code, "pop %s\n", dst);
            break;
        case OP_ADD:
            sprintf(code, "add %s, %s\n", dst, src);
            break;
        case OP_SUB:
            sprintf(code, "sub %s, %s\n", dst, src);
            break;
        case OP_MUL:
            sprintf(code, "mul %s, %s\n", dst, src);
            break;
        case OP_DIV:
            sprintf(code, "div %s, %s\n", dst, src);
            break;
        case OP_MOD:
            sprintf(code, "mod %s, %s\n", dst, src);
            break;
        case OP_SHL:
            sprintf(code, "shl %s, %s\n", dst, src);
            break;
        case OP_SHR:
            sprintf(code, "shr %s, %s\n", dst, src);
            break;
        case OP_AND:
            sprintf(code, "and %s, %s\n", dst, src);
            break;
        case OP_OR:
            sprintf(code, "or %s, %s\n", dst, src);
            break;
        case OP_XOR:
            sprintf(code, "xor %s, %s\n", dst, src);
            break;
        case OP_NOT:
            sprintf(code, "not %s\n", src);
            break;
        case OP_NEG:
            sprintf(code, "neg %s\n", src);
            break;
        case OP_SWP:
            sprintf(code, "swap %s, %s\n", dst, src);
            break;
        case OP_COMPARE:
            sprintf(code, "compare %s, %s\n", dst, src);
            break;
        case OP_EQ:
            sprintf(code, "eq %s\n", dst);
            break;
        case OP_NEQ:
            sprintf(code, "neq %s\n", dst);
            break;
        case OP_LT:
            sprintf(code, "lt %s\n", dst);
            break;
        case OP_LTE:
            sprintf(code, "lte %s\n", dst);
            break;
        case OP_GT:
            sprintf(code, "gt %s\n", dst);
            break;
        case OP_GTE:
            sprintf(code, "gte %s\n", dst);
            break;
        case OP_BRANCH_TRUE:
            sprintf(code, "branch true %s\n", dst);
            break;
        case OP_BRANCH_FALSE:
            sprintf(code, "branch false %s\n", dst);
            break;
        case OP_JUMP:
            sprintf(code, "jump %s\n", dst);
            break;
        case OP_NEW_BRANCH:
            sprintf(code, "branch %s:\n", dst);
            break;
    }

    free(src);
    free(dst);
    return code;
}

char *ir_to_string(IR *ir, bool show_nops) {
    char *string = malloc(1024);
    string[0] = '\0';
    size_t capacity = 1024;
    size_t string_len = 0;

    for (size_t i = 0; i < ir->op_count; i++) {
        if (!show_nops && ir->ops[i].type == OP_NOP)
            continue;

        char *op = op_to_string(&ir->ops[i]);
        const size_t len = strlen(op);

        if (string_len + len + 1 >= capacity) {
            capacity *= 2;
            string = realloc(string, capacity);
        }

        strcat(string, op);
        free(op);
        string_len += len;
    }

    return string;
}