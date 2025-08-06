#ifndef IR_H
#define IR_H

#include "ast.h"
#include <stdint.h>
#include <stdbool.h>

#define TEMP_REG 0

typedef struct {
    char *scope;
    char *func;
    char *module;
} Source;

typedef struct {
    enum {
        VAL_NONE,
        VAL_INT,
        VAL_STRING,
        VAL_REG,
        VAL_VAR,
        VAL_IDENT,
        VAL_RET,
        VAL_STACK,
        VAL_BRANCH,
        VAL__RES__
    } type;

    Source source;

    union {
        struct {
            int64_t int_const;
        };

        unsigned int reg;
        char *var;
        char *ident;
        char *string;
        unsigned int branch;
    };
} OpValue;

typedef enum {
    OP_NOP,
    OP_FUNC_BEGIN,
    OP_FUNC_END,
    OP_RET,
    OP_NEW_VAR,
    OP_LOAD,
    OP_STORE,
    OP_CALL,
    OP_INLINE_ASM,
    OP_PUSH,
    OP_POP,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_SHL,
    OP_SHR,
    OP_AND,
    OP_OR,
    OP_XOR,
    OP_NOT,
    OP_NEG,
    OP_SWP,
    OP_COMPARE,
    OP_EQ,
    OP_NEQ,
    OP_LT,
    OP_LTE,
    OP_GT,
    OP_GTE,
    OP_BRANCH_TRUE,
    OP_BRANCH_FALSE,
    OP_JUMP,
    OP_NEW_BRANCH,
    OP_REF,
    OP_DEREF,
    OP_STORE_DEREF
} OpType;

typedef struct {
    OpType type;
    OpValue dst;
    OpValue src;
} Op;

typedef struct {
    Op *ops;
    size_t op_count;
    size_t op_capacity;
} IR;

IR ast_to_ir(AST *ast);
void delete_ir(IR *ir);
char *ir_to_string(IR *ir, bool show_nops);

#endif