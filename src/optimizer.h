#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "ir.h"
#include <stdio.h>

typedef struct {
    IR *ir;
    Op *op;
    size_t pos;
} Optimizer;

void optimize_ir(IR *ir);

#endif