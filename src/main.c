#include "compile.h"
#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

void help(char *prog) {
    printf("usage: %s <command> [options] <input file>\n"
           "commands:\n"
           "    asm                 produce an assembly file\n"
           "    build               produce a binary file\n"
           "    ir                  produce an ir file\n"
           "    run                 produce and execute a binary file\n"
           "options:\n"
           "    -o <output file>    specify the output filename\n"
           "    -unopt              disable optimization\n"
           "dev options:\n"
           "    -freestanding       don't use the standard library\n"
           "    -nops               show nops in ir output\n"
           "    -no-omit-libs       don't omit library code when assembling\n"
           , prog);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        help(argv[0]);
        return EXIT_FAILURE;
    }

    const char *command = argv[1];
    unsigned int flags = 0;

    if (strcmp(command, "--help") == 0) {
        help(argv[0]);
        return EXIT_SUCCESS;
    } else if (strcmp(command, "asm") == 0)
        flags |= COMP_DONT_ASSEMBLE | COMP_OMIT_LIBS;
    else if (strcmp(command, "ir") == 0)
        flags |= COMP_IR;
    else if (strcmp(command, "run") == 0)
        flags |= COMP_RUN;
    else if (strcmp(command, "build") != 0) {
        log_error(NULL, 0, 0);
        fprintf(stderr, "unknown command '%s'\n", command);
        return EXIT_FAILURE;
    }

    char *infile = NULL;
    char *outfile = "a.out";

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-nops") == 0) {
            if (!(flags & COMP_IR)) {
                log_error(NULL, 0, 0);
                fprintf(stderr, "invalid option '%s' used with command '%s'\n", argv[i], command);
                return EXIT_FAILURE;
            }

            flags |= COMP_IR_NOPS;
        } else if (strcmp(argv[i], "-o") == 0) {
            if (i == argc - 1) {
                log_error(NULL, 0, 0);
                fprintf(stderr, "missing output file for option '-o'\n");
                return EXIT_FAILURE;
            }

            outfile = argv[++i];
            flags |= COMP_OUTFILE_WAS_SPECIFIED;
        } else if (strcmp(argv[i], "-uppercase") == 0)
            flags |= COMP_UPPERCASE;
        else if (strcmp(argv[i], "-unopt") == 0)
            flags |= COMP_UNOPTIMIZED;
        else if (strcmp(argv[i], "-no-omit-libs") == 0) {
            if (!(flags & COMP_OMIT_LIBS)) {
                log_error(NULL, 0, 0);
                fprintf(stderr, "invalid option '%s' used with command '%s'\n", argv[i], command);
                return EXIT_FAILURE;
            }

            flags &= ~COMP_OMIT_LIBS;
        } else if (strcmp(argv[i], "-freestanding") == 0)
            flags |= COMP_FREESTANDING;
        else if (i == argc - 1)
            infile = argv[i];
        else {
            log_error(NULL, 0, 0);
            fprintf(stderr, "unknown option '%s'\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    if (infile == NULL) {
        log_error(NULL, 0, 0);
        fprintf(stderr, "missing input file\n");
        return EXIT_FAILURE;
    }

    return compile(infile, outfile, flags);
}