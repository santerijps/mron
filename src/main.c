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

#ifndef MRON_VERSION
#define MRON_VERSION "dev"
#endif

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

static void print_version(void) {
    printf("mronc %s\n", MRON_VERSION);
}

static void print_usage(FILE *out) {
    fprintf(out,
        "Usage: mronc [options] <input-file>\n"
        "       mronc [options] -          (read from stdin, requires --from)\n"
        "\n"
        "Translates between MRON and JSON formats.\n"
        "The input format is detected from the file extension.\n"
        "\n"
        "Options:\n"
        "  -o, --output <file>   Write output to a file instead of stdout\n"
        "  -p, --pretty          Pretty-print JSON output (2-space indent)\n"
        "  -c, --check           Validate input without producing output\n"
        "      --from <format>   Force input format: mron or json\n"
        "  -v, --verbose         Show processing details on stderr\n"
        "  -V, --version         Print version and exit\n"
        "  -h, --help            Show this help message\n"
        "\n"
        "Examples:\n"
        "  mronc config.mron                  Convert MRON to JSON (stdout)\n"
        "  mronc config.mron -o config.json   Convert MRON to a JSON file\n"
        "  mronc data.json -o data.mron       Convert JSON to MRON\n"
        "  mronc config.mron -p               Pretty-print the JSON output\n"
        "  mronc config.mron -c               Validate without converting\n"
        "  mronc --from mron - < input.txt    Read MRON from stdin\n");
}

static int opt_match(const char *arg, const char *shortopt, const char *longopt) {
    return (shortopt && strcmp(arg, shortopt) == 0) ||
           (longopt && strcmp(arg, longopt) == 0);
}

static char *read_stdin(void) {
    StringBuilder sb;
    sb_init(&sb);
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) {
        sb_append_n(&sb, buf, n);
    }
    if (ferror(stdin)) {
        fprintf(stderr, "error: failed to read from stdin\n");
        sb_free(&sb);
        return NULL;
    }
    return sb_finish(&sb);
}

int main(int argc, char *argv[]) {
    const char *input_file = NULL;
    const char *output_file = NULL;
    const char *force_format = NULL;
    int pretty = 0;
    int check_only = 0;
    int verbose = 0;
    int end_of_opts = 0;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (!end_of_opts && strcmp(argv[i], "--") == 0) {
            end_of_opts = 1;
            continue;
        }

        if (!end_of_opts && argv[i][0] == '-' && argv[i][1] != '\0'
            && strcmp(argv[i], "-") != 0) {

            if (opt_match(argv[i], "-o", "--output")) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "error: %s requires an argument\n", argv[i]);
                    return 1;
                }
                output_file = argv[++i];
            } else if (opt_match(argv[i], NULL, "--from")) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "error: --from requires an argument (mron or json)\n");
                    return 1;
                }
                force_format = argv[++i];
                if (strcmp(force_format, "mron") != 0 &&
                    strcmp(force_format, "json") != 0) {
                    fprintf(stderr, "error: --from must be 'mron' or 'json', got '%s'\n",
                            force_format);
                    return 1;
                }
            } else if (opt_match(argv[i], "-p", "--pretty")) {
                pretty = 1;
            } else if (opt_match(argv[i], "-c", "--check")) {
                check_only = 1;
            } else if (opt_match(argv[i], "-v", "--verbose")) {
                verbose = 1;
            } else if (opt_match(argv[i], "-V", "--version")) {
                print_version();
                return 0;
            } else if (opt_match(argv[i], "-h", "--help")) {
                print_usage(stdout);
                return 0;
            } else {
                fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
                fprintf(stderr, "Try 'mronc --help' for usage information.\n");
                return 1;
            }
        } else {
            /* Positional argument */
            if (input_file) {
                fprintf(stderr, "error: unexpected argument '%s' "
                                "(only one input file allowed)\n", argv[i]);
                return 1;
            }
            input_file = argv[i];
        }
    }

    if (!input_file) {
        print_usage(stderr);
        return 1;
    }

    /* Determine input format */
    int reading_stdin = (strcmp(input_file, "-") == 0);
    FileFormat input_format;

    if (force_format) {
        input_format = (strcmp(force_format, "mron") == 0) ? FORMAT_MRON : FORMAT_JSON;
    } else if (reading_stdin) {
        fprintf(stderr, "error: reading from stdin requires --from mron or --from json\n");
        return 1;
    } else {
        input_format = detect_format(input_file);
        if (input_format == FORMAT_UNKNOWN) {
            fprintf(stderr, "error: cannot determine format of '%s' "
                            "(expected .mron or .json extension)\n", input_file);
            fprintf(stderr, "Hint: use --from mron or --from json to specify the format.\n");
            return 1;
        }
    }

    /* Read input */
    char *source;
    if (reading_stdin) {
        if (verbose) fprintf(stderr, "Reading from stdin...\n");
        source = read_stdin();
    } else {
        if (verbose) fprintf(stderr, "Reading %s...\n", input_file);
        source = read_file(input_file);
    }
    if (!source) return 1;

    if (verbose) {
        fprintf(stderr, "Input: %zu bytes, format: %s\n",
                strlen(source),
                input_format == FORMAT_MRON ? "mron" : "json");
    }

    AstNode *ast = NULL;
    char *output = NULL;

    if (input_format == FORMAT_MRON) {
        /* MRON -> JSON */
        const char *fname = reading_stdin ? "<stdin>" : input_file;
        TokenArray *tokens = lexer_tokenize(source, fname);
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

        if (verbose) {
            fprintf(stderr, "Parsed %zu top-level key(s)\n",
                    ast->data.record.count);
        }

        if (!check_only) {
            output = pretty ? json_emit_pretty(ast) : json_emit(ast);
        }
    } else {
        /* JSON -> MRON */
        const char *fname = reading_stdin ? "<stdin>" : input_file;
        ast = json_parse(source, fname);
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

        if (verbose) {
            fprintf(stderr, "Parsed %zu top-level key(s)\n",
                    ast->data.record.count);
        }

        if (!check_only) {
            output = mron_emit(ast);
        }
    }

    free(source);

    if (check_only) {
        if (verbose) fprintf(stderr, "Validation passed\n");
        ast_free(ast);
        return 0;
    }

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
        if (verbose) {
            fprintf(stderr, "Wrote %zu bytes to %s\n", strlen(output), output_file);
        }
    } else {
        fputs(output, stdout);
    }

    free(output);
    ast_free(ast);
    return 0;
}
