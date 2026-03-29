#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "ast.h"
#include "lexer.h"
#include "parser.h"
#include "json_emit.h"
#include "mron_emit.h"
#include "json_parse.h"

typedef enum {
    FORMAT_MRON,
    FORMAT_JSON,
    FORMAT_UNKNOWN,
} FileFormat;

static FileFormat detect_format(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if (!dot) return FORMAT_UNKNOWN;
    if (strcmp(dot, ".mron") == 0) return FORMAT_MRON;
    if (strcmp(dot, ".json") == 0) return FORMAT_JSON;
    return FORMAT_UNKNOWN;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <input-file> [-o <output-file>]\n"
        "\n"
        "Translates between MRON and JSON formats.\n"
        "The input format is detected from the file extension.\n"
        "\n"
        "  .mron input -> JSON output\n"
        "  .json input -> MRON output\n"
        "\n"
        "Options:\n"
        "  -o <file>  Write output to file instead of stdout\n"
        "  -h         Show this help message\n",
        prog);
}

int main(int argc, char *argv[]) {
    const char *input_file = NULL;
    const char *output_file = NULL;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: -o requires an argument\n");
                return 1;
            }
            output_file = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            if (input_file) {
                fprintf(stderr, "error: multiple input files specified\n");
                return 1;
            }
            input_file = argv[i];
        }
    }

    if (!input_file) {
        fprintf(stderr, "error: no input file specified\n");
        print_usage(argv[0]);
        return 1;
    }

    FileFormat input_format = detect_format(input_file);
    if (input_format == FORMAT_UNKNOWN) {
        fprintf(stderr, "error: cannot determine format of '%s' "
                        "(expected .mron or .json extension)\n", input_file);
        return 1;
    }

    /* Read input file */
    char *source = read_file(input_file);
    if (!source) return 1;

    AstNode *ast = NULL;
    char *output = NULL;

    if (input_format == FORMAT_MRON) {
        /* MRON -> JSON */
        TokenArray *tokens = lexer_tokenize(source, input_file);
        if (!tokens) {
            free(source);
            return 1;
        }

        ast = parser_parse(tokens);
        lexer_free(tokens);

        if (!ast) {
            free(source);
            return 1;
        }

        output = json_emit(ast);
    } else {
        /* JSON -> MRON */
        ast = json_parse(source, input_file);
        if (!ast) {
            free(source);
            return 1;
        }

        if (ast->type != AST_RECORD) {
            fprintf(stderr, "error: MRON requires a top-level object/record, "
                            "but JSON root is %s\n", ast_type_name(ast->type));
            ast_free(ast);
            free(source);
            return 1;
        }

        output = mron_emit(ast);
    }

    free(source);

    if (!output) {
        ast_free(ast);
        return 1;
    }

    /* Write output */
    if (output_file) {
        FILE *f = fopen(output_file, "w");
        if (!f) {
            fprintf(stderr, "error: cannot open output file '%s'\n", output_file);
            free(output);
            ast_free(ast);
            return 1;
        }
        fputs(output, f);
        fclose(f);
    } else {
        fputs(output, stdout);
    }

    free(output);
    ast_free(ast);
    return 0;
}
