#ifndef COMPILE_H
#define COMPILE_H

#include <stdbool.h>

#define COMP_DONT_ASSEMBLE (0x01)
#define COMP_RUN (0x02)
#define COMP_UPPERCASE (0x04)
#define COMP_OUTFILE_WAS_SPECIFIED (0x08)
#define COMP_UNOPTIMIZED (0x10)
#define COMP_IR (0x20)
#define COMP_IR_NOPS (0x40)
#define COMP_FREESTANDING (0x80)
#define COMP_OMIT_LIBS (0x100)

int compile(char *infile, char *outfile, unsigned int flags);

#endif