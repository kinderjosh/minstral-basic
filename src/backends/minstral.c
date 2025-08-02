#include "../backend.h"
#include "../ir.h"
#include "../utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>

#define STARTING_SECT_CAP 128
#define STARTING_VAR_CAP 8
#define TABLE_SIZE 1000

typedef struct {
    Source source;
    char *name;
    bool used;
} Variable;

static Variable variables[TABLE_SIZE];
static char *data_sect;
static size_t data_sect_size = 0;
static size_t data_sect_cap = STARTING_SECT_CAP;

static char *subroutines;
static size_t subroutines_size = 0;
static size_t subroutines_cap = STARTING_SECT_CAP;

void data_sect_append(char *data) {
    const size_t len = strlen(data);

    if (data_sect_size + len + 1 >= data_sect_cap) {
        data_sect_cap *= 2;
        data_sect = realloc(data_sect, data_sect_cap);
    }

    strcat(data_sect, data);
    data_sect_size += len;
}

void subroutines_append(char *data) {
    const size_t len = strlen(data);

    if (subroutines_size + len + 1 >= subroutines_cap) {
        subroutines_cap *= 2;
        subroutines = realloc(subroutines, subroutines_cap);
    }

    strcat(subroutines, data);
    subroutines_size += len;
}

uint32_t hash_FNV1a(const char *data, size_t size) {
    uint32_t h = 2166136261UL;

    for (size_t i = 0; i < size; i++) {
        h ^= data[i];
        h *= 16777619;
    }

    return h % TABLE_SIZE;
}

Variable *find_variable(Source *source, char *name) {
    char *data = malloc(strlen(source->scope) + strlen(name) + 2);
    sprintf(data, "_%s%s", source->scope, name);
    Variable *var = &variables[hash_FNV1a(data, strlen(data))];
    free(data);
    return var;
}

bool variable_exists(Source *source, char *name) {
    return find_variable(source, name)->used;
}

void add_variable(Source *source, char *name) {
    Variable *var = find_variable(source, name);
    assert(!var->used);
    var->name = name;
    var->used = true;

    char *data = malloc(strlen(source->scope) + strlen(name) + 15);
    sprintf(data, "_%s%s dat 0\n", source->scope, name);

    data_sect_append(data);
    free(data);
}

char *emit_stmt(Op *op);

char *emit_asm(IR *ir) {
    size_t capacity = STARTING_SECT_CAP;
    char *code = malloc(STARTING_SECT_CAP);
    strcpy(code, ".text\n");
    size_t code_len = 6;

    data_sect = malloc(STARTING_SECT_CAP);
    data_sect[0] = '\0';

    subroutines = malloc(STARTING_SECT_CAP);
    subroutines[0] = '\0';
    
    bool in_subroutine = false;

    for (size_t i = 0; i < ir->op_count; i++) {
        char *stmt = emit_stmt(&ir->ops[i]);

        if (ir->ops[i].type == OP_FUNC_BEGIN)
            in_subroutine = true;

        if (in_subroutine) {
            subroutines_append(stmt);
            free(stmt);

            if (ir->ops[i].type == OP_FUNC_END)
                in_subroutine = false;
            
            continue;
        }

        // Yeahhh I don't wanna call strlen() a billion times.
        const size_t stmt_len = strlen(stmt);

        // Extra +4 for "hlt\n"
        if (code_len + stmt_len + 5 >= capacity) {
            while (code_len + stmt_len + 5 >= capacity)
                capacity *= 2;

            code = realloc(code, capacity);
        }

        strcat(code, stmt);
        free(stmt);
        code_len += stmt_len;
    }

    strcat(code, "hlt\n");
    code_len += 4;

    code = realloc(code, code_len + subroutines_size + 1);
    strcat(code, subroutines);
    free(subroutines);
    code_len += subroutines_size;

    if (data_sect_size > 0) {
        code = realloc(code, (code_len + data_sect_size + 7));
        strcat(code, ".data\n");
        strcat(code, data_sect);
    }

    free(data_sect);
    return code;
}

static char *value_to_string(OpValue *value) {
    char *code;

    switch (value->type) {
        case VAL_INT:
            code = malloc(32);
            sprintf(code, "%" PRId64, value->int_const);
            return code;
        case VAL_VAR:
            code = malloc(strlen(value->source.scope) + strlen(value->var) + 3);
            sprintf(code, "_%s%s", value->source.scope, value->var);
            return code;
        case VAL_RET:
            code = malloc(strlen(value->source.func) + 6);
            sprintf(code, "_%s@ret", value->source.func);
            return code;
        case VAL_REG:
            assert(value->reg == TEMP_REG);
            return calloc(1, sizeof(char));
        case VAL_STACK: return mystrdup("^");
        case VAL_BRANCH:
            code = malloc(strlen(value->source.func) + 32);
            sprintf(code, "_%s@l%u", value->source.func, value->branch);
            return code;
        default: break;
    }

    assert(false);
    return calloc(1, sizeof(char));
}

char *emit_func_begin(Op *op) {
    // Return value.
    char *ret_var = malloc(strlen(op->src.ident) + 6);
    sprintf(ret_var, "%s@ret", op->src.ident);
    add_variable(&op->src.source, ret_var);
    free(ret_var);

    char *code = malloc(strlen(op->src.ident) + 11);
    sprintf(code, "_%s dsr\n", op->src.ident);
    return code;
}

char *emit_new_var(Op *op) {
    add_variable(&op->src.source, op->src.var);
    return calloc(1, sizeof(char));
}

char *emit_load(Op *op) {
    if (op->src.type == VAL_REG && op->src.reg == TEMP_REG)
        return calloc(1, sizeof(char));

    char *src = value_to_string(&op->src);
    char *code = malloc(strlen(src) + 8);
    sprintf(code, "lda %s\n", src);
    free(src);
    return code;
}

char *emit_store(Op *op) {
    char *dst = value_to_string(&op->dst);
    char *code = malloc(strlen(dst) + 8);
    sprintf(code, "sta %s\n", dst);
    free(dst);
    return code;
}

char *emit_call(Op *op) {
    char *code = malloc(strlen(op->src.ident) + 11);
    sprintf(code, "csr _%s\n", op->src.ident);
    return code;
}

char *emit_inline_asm(Op *op) {
    const size_t len = strlen(op->src.string);

    if (len == 0)
        return calloc(1, sizeof(char));
    else if (op->src.string[len - 1] == '\n')
        return mystrdup(op->src.string);

    char *copy = malloc(len + 2);
    sprintf(copy, "%s\n", op->src.string);
    return copy;
}

char *emit_push(Op *op) {
    char *src = value_to_string(&op->src);
    char *code = malloc(strlen(src) + 8);
    sprintf(code, "psh %s\n", src);
    free(src);
    return code;
}

char *emit_pop(Op *op) {
    if (op->dst.type == VAL_REG && op->dst.reg == TEMP_REG)
        return mystrdup("pop\n");

    char *dst = value_to_string(&op->dst);
    char *code = malloc(strlen(dst) + 8);
    sprintf(code, "pop %s\n", dst);
    free(dst);
    return code;
}

char *emit_math(Op *op) {
    // Value is already loaded in the accumulator,
    // alter the top of stack directly.
    if (op->src.type == VAL_REG && op->src.reg == TEMP_REG && op->dst.type == VAL_STACK)
        op->src.type = VAL_STACK;

    char *src = value_to_string(&op->src);
    char *code = malloc(strlen(src) + 8);

    switch (op->type) {
        case OP_ADD:
            sprintf(code, "add %s\n", src);
            break;
        case OP_SUB:
            sprintf(code, "sub %s\n", src);
            break;
        case OP_MUL:
            sprintf(code, "mul %s\n", src);
            break;
        case OP_DIV:
            sprintf(code, "div %s\n", src);
            break;
        case OP_MOD:
            sprintf(code, "mod %s\n", src);
            break;
        case OP_SHL:
            sprintf(code, "shl %s\n", src);
            break;
        case OP_SHR:
            sprintf(code, "shl %s\n", src);
            break;
        case OP_AND:
            sprintf(code, "and %s\n", src);
            break;
        case OP_OR:
            sprintf(code, "or %s\n", src);
            break;
        case OP_XOR:
            sprintf(code, "xor %s\n", src);
            break;
        case OP_NOT:
            if (op->src.type == VAL_REG && op->src.reg == TEMP_REG)
                strcpy(code, "not\n");
            else
                sprintf(code, "not %s\n", src);
            break;
        default:
            if (op->src.type == VAL_REG && op->src.reg == TEMP_REG)
                strcpy(code, "neg\n");
            else
                sprintf(code, "neg %s\n", src);
            break;
    }

    free(src);
    return code;
}

char *emit_swp(Op *op) {
    char *dst = value_to_string(&op->dst);
    char *code = malloc(strlen(dst) + 8);
    sprintf(code, "swp %s\n", dst);
    free(dst);
    return code;
}

char *emit_compare(Op *op) {
    char *src = value_to_string(&op->src);
    char *code = malloc(strlen(src) + 8);
    sprintf(code, "cmp %s\n", src);
    free(src);
    return code;
}

char *emit_status(Op *op) {
    char *dst = value_to_string(&op->dst);
    char *code = malloc(strlen(dst) + 8);

    switch (op->type) {
        case OP_EQ:
            sprintf(code, "seq %s\n", dst);
            break;
        case OP_NEQ:
            sprintf(code, "sne %s\n", dst);
            break;
        case OP_LT:
            sprintf(code, "slt %s\n", dst);
            break;
        case OP_LTE:
            sprintf(code, "sle %s\n", dst);
            break;
        case OP_GT:
            sprintf(code, "sgt %s\n", dst);
            break;
        default:
            sprintf(code, "sge %s\n", dst);
            break;
    }

    free(dst);
    return code;
}

char *emit_branch_bool(Op *op) {
    char *dst = value_to_string(&op->dst);
    char *code = malloc(strlen(dst) + 16);
    sprintf(code, "cmp 0\n"
                  "b%s %s\n", op->type == OP_BRANCH_TRUE ? "ne" : "eq", dst);

    free(dst);
    return code;
}

char *emit_new_branch(Op *op) {
    char *code = malloc(strlen(op->src.source.func) + 32);
    sprintf(code, "_%s@l%u\n", op->src.source.func, op->src.branch);
    return code;
}

char *emit_jump(Op *op) {
    char *dst = value_to_string(&op->dst);
    char *code = malloc(strlen(dst) + 8);
    sprintf(code, "jmp %s\n", dst);
    free(dst);
    return code;
}

char *emit_stmt(Op *op) {
    switch (op->type) {
        case OP_FUNC_END:
        case OP_NOP: return calloc(1, sizeof(char));
        case OP_FUNC_BEGIN: return emit_func_begin(op);
        case OP_RET: return mystrdup("rsr\n");
        case OP_NEW_VAR: return emit_new_var(op);
        case OP_LOAD: return emit_load(op);
        case OP_STORE: return emit_store(op);
        case OP_CALL: return emit_call(op);
        case OP_INLINE_ASM: return emit_inline_asm(op);
        case OP_PUSH: return emit_push(op);
        case OP_POP: return emit_pop(op);
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_MOD:
        case OP_SHL:
        case OP_SHR:
        case OP_AND:
        case OP_OR:
        case OP_XOR:
        case OP_NOT:
        case OP_NEG: return emit_math(op);
        case OP_SWP: return emit_swp(op);
        case OP_COMPARE: return emit_compare(op);
        case OP_EQ:
        case OP_NEQ:
        case OP_LT:
        case OP_LTE:
        case OP_GT:
        case OP_GTE: return emit_status(op);
        case OP_BRANCH_TRUE:
        case OP_BRANCH_FALSE: return emit_branch_bool(op);
        case OP_NEW_BRANCH: return emit_new_branch(op);
        case OP_JUMP: return emit_jump(op);
        default: break;
    }

    assert(false);
    return calloc(1, sizeof(char));
}