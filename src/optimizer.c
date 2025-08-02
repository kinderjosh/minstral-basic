#include "optimizer.h"
#include "ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <stdint.h>

#define IS_ACC(op) (op.type == VAL_REG && op.reg == TEMP_REG)
#define IS_MATH(type) (type >= OP_ADD && type <= OP_XOR)

static void step(Optimizer *opt) {
    if (opt->pos + 1 < opt->ir->op_count)
        opt->op = &opt->ir->ops[++opt->pos];
}

// Peek and skip any NOPs if encountered.
static Op *peek(Optimizer *opt, int offset) {
    Op *op;

    if (opt->pos + offset >= opt->ir->op_count)
        op = &opt->ir->ops[opt->ir->op_count - 1];
    else if ((int)opt->pos + offset < 1)
        op = &opt->ir->ops[0];
    else
        op = &opt->ir->ops[opt->pos + offset];

    while (op->type == OP_NOP && offset + 1 < (int)opt->ir->op_count)
        op = peek(opt, offset < 0 ? offset - 1 : offset + 1);

    return op;
}

static void jump_to(Optimizer *opt, size_t pos) {
    opt->op = &opt->ir->ops[pos];
    opt->pos = pos;
}

// Eliminates ops that have no effect, like loading the accumulator
// into itself.
void dead_code_elimination(Optimizer *opt) {
    if ((opt->op->type == OP_LOAD || opt->op->type == OP_STORE) && IS_ACC(opt->op->dst) && IS_ACC(opt->op->src)) {
        opt->op->type = OP_NOP;
        return;
    }

    Op *next = peek(opt, 1);

    // No point to store into a variable then load the variable back,
    // the value is still in the accumulator.
    if (opt->op->type == OP_STORE && IS_ACC(opt->op->src) && opt->op->dst.type == VAL_VAR &&
            next->type == OP_LOAD && IS_ACC(next->dst) && next->src.type == VAL_VAR &&
            strcmp(opt->op->dst.var, next->src.var) == 0 && 
            strcmp(opt->op->dst.source.scope, next->src.source.scope) == 0) {

        next->type = OP_NOP;
    }
}

// Tries (not perfectly) to evaluate constant expressions into
// as little constants as possible.
void weak_constant_folding(Optimizer *opt) {
    if (opt->op->type != OP_LOAD || !IS_ACC(opt->op->dst) || opt->op->src.type != VAL_INT)
        return;

    Op *next = peek(opt, 1);

    // A common pattern in math expressions is the following:
    // load [int]
    // store @temp
    // load
    // math @temp

    if (!IS_MATH(next->type))
        return;
        
    if (next->src.type == VAL_INT) {
        switch (next->type) {
            case OP_ADD:
                opt->op->src.int_const += next->src.int_const;
                break;
            case OP_SUB:
                opt->op->src.int_const -= next->src.int_const;
                break;
            case OP_MUL:
                opt->op->src.int_const *= next->src.int_const;
                break;
            case OP_DIV:
                opt->op->src.int_const /= next->src.int_const;
                break;
            case OP_MOD:
                opt->op->src.int_const %= next->src.int_const;
                break;
            case OP_SHL:
                opt->op->src.int_const <<= next->src.int_const;
                break;
            case OP_SHR:
                opt->op->src.int_const >>= next->src.int_const;
                break;
            case OP_AND:
                opt->op->src.int_const &= next->src.int_const;
                break;
            case OP_OR:
                opt->op->src.int_const |= next->src.int_const;
                break;
            default:
                opt->op->src.int_const ^= next->src.int_const;
                break;
        }
    } // NOT and NEG don't have int operands.
    else if (opt->op->type == OP_NOT)
        opt->op->src.int_const = !opt->op->src.int_const;
    else if (opt->op->type == OP_NEG)
        opt->op->src.int_const = -opt->op->src.int_const;
    else
        return;

    next->type = OP_NOP;
}

// Reduces stack usage like push and pops by elimination and propogation.
void stack_reduction(Optimizer *opt) {
    Op *next = peek(opt, 1);

    if (opt->op->type == OP_LOAD && next->type == OP_PUSH && IS_ACC(next->src)) {
        opt->op->type = OP_NOP;
        next->src = opt->op->src;
        return;
    } else if (opt->op->type == OP_PUSH && next->type == OP_POP) {
        opt->op->type = OP_LOAD;
        opt->op->dst = (OpValue){ .type = VAL_REG, .reg = TEMP_REG };
        next->type = OP_STORE;
        next->src = (OpValue){ .type = VAL_REG, .reg = TEMP_REG };
    } else if (opt->op->type == OP_POP && IS_ACC(opt->op->dst) && 
            next->type == OP_STORE && IS_ACC(next->src)) {
        opt->op->type = OP_NOP;
        next->type = OP_POP;
        return;
    }

    // A common math pattern:
    // push
    // load
    // store @temp
    // pop @acc
    // math

    if (opt->op->type != OP_PUSH || IS_ACC(opt->op->src) || next->type != OP_LOAD)
        return;

    Op *next2 = peek(opt, 2);
    Op *next3 = peek(opt, 3);

    if (next2->type != OP_STORE || next2->dst.type != VAL_VAR || strcmp(next2->dst.var, "@temp") != 0 ||
            next3->type != OP_POP || !IS_ACC(next3->dst))
        return;

    opt->op->type = OP_NOP;
    next3->type = OP_LOAD;
    next3->src = opt->op->src;
}

static void pass(Optimizer *opt) {
    while (opt->pos + 2 < opt->ir->op_count) {
        dead_code_elimination(opt);
        //weak_constant_folding(opt);
        stack_reduction(opt);
        step(opt);
    }
}

void optimize_ir(IR *ir) {
    if (ir->op_count == 0)
        return;

    Optimizer opt = (Optimizer){ .ir = ir, .op = &ir->ops[0], .pos = 0 };

    // Do three passes.
    pass(&opt);
    jump_to(&opt, 0);

    pass(&opt);
    jump_to(&opt, 0);

    pass(&opt);
}