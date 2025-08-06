#include "compile.h"
#include "parser.h"
#include "ast.h"
#include "ir.h"
#include "optimizer.h"
#include "backend.h"
#include "error.h"
#include "symbol_table.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <ctype.h>

#define STDLIB_PATH "/usr/local/share/minstral-basic/basic.mb"

int compile(char *infile, char *outfile, unsigned int flags) {
    create_duplicates();
    create_symbol_table();
    AST *stdlib_root = NULL;

    if (!(flags & COMP_FREESTANDING)) {
        stdlib_root = parse_root(STDLIB_PATH);

        if (error_count() > 0) {
            delete_ast(stdlib_root);
            delete_duplicates();
            delete_symbol_table();
            return EXIT_FAILURE;
        }
    }

    AST *root = parse_root(infile);

    if (error_count() > 0) {
        if (stdlib_root != NULL)
            delete_ast(stdlib_root);

        delete_ast(root);
        delete_duplicates();
        delete_symbol_table();
        return EXIT_FAILURE;
    }

    if (stdlib_root != NULL && !(flags & COMP_OMIT_LIBS)) {
        // Copy stdlib nodes over into the main program root.
        for (size_t i = 0; i < stdlib_root->root.size; i++)
            astlist_push(&root->root, stdlib_root->root.items[i]);

        // Delete the stdlib root.
        free(stdlib_root->root.items);
        free(stdlib_root->scope.full);
        free(stdlib_root->scope.func);
        free(stdlib_root->scope.file);
        free(stdlib_root->scope.module);
        free(stdlib_root);
        stdlib_root = NULL;
    }

    IR ir = ast_to_ir(root);

    if (!(flags & COMP_UNOPTIMIZED))
        optimize_ir(&ir);

    char *code = (flags & COMP_IR) ? ir_to_string(&ir, flags & COMP_IR_NOPS) : emit_asm(&ir);
    
    delete_ir(&ir);
    delete_ast(root);
    delete_symbol_table();

    if (flags & COMP_OMIT_LIBS && stdlib_root != NULL)
        delete_ast(stdlib_root);
    else
        delete_duplicates();

    if (flags & COMP_UPPERCASE) {
        const size_t len = strlen(code);

        for (size_t i = 0; i < len; i++) {
            if (isalpha(code[i]) && code[i] <= 'z')
                code[i] = toupper(code[i]);
        }
    }

    char *outasm;
    
    if ((flags & COMP_DONT_ASSEMBLE) && (flags & COMP_OUTFILE_WAS_SPECIFIED))
        outasm = mystrdup(outfile);
    else
        outasm = replace_file_extension(infile, (flags & COMP_IR) ? "ir" : "min", true);

    FILE *f = fopen(outasm, "w");

    if (f == NULL) {
        log_error(infile, 0, 0);
        fprintf(stderr, "failed to write to file '%s'\n", outasm);
        free(code);
        return EXIT_FAILURE;
    }

    fputs(code, f);
    free(code);
    fclose(f);

    if ((flags & COMP_DONT_ASSEMBLE) || flags & COMP_IR) {
        free(outasm);
        return EXIT_SUCCESS;
    }

    char *cmd = malloc(strlen(outfile) + strlen(outasm) + 22);
    sprintf(cmd, "mas asm -o %s %s", outfile, outasm);

    if (system(cmd) != 0) {
        log_error(infile, 0, 0);
        fprintf(stderr, "failed to assemble '%s'\n", outasm);
        free(cmd);
        free(outasm);
        return EXIT_FAILURE;
    } else if (remove(outasm) != 0) {
        log_error(infile, 0, 0);
        fprintf(stderr, "failed to remove '%s'\n", outasm);
        free(outasm);
        free(cmd);
        return EXIT_FAILURE;
    }

    free(outasm);

    if (!(flags & COMP_RUN)) {
        free(cmd);
        return EXIT_SUCCESS;
    }

    sprintf(cmd, "mas exe ./%s", outfile);
    int runstatus = system(cmd);
    free(cmd);
    return runstatus;
}