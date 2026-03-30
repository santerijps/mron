#ifdef _WIN32
/* Rename Windows' TokenType enum value to avoid clash with lexer.h */
#define TokenType WinTokenType
#include <windows.h>
#undef TokenType
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "ast.h"
#include "ast_ops.h"
#include "lexer.h"
#include "parser.h"
#include "json_emit.h"
#include "mron_emit.h"
#include "json_parse.h"
#include "yaml_emit.h"
#include "toml_emit.h"

#ifndef MRON_VERSION
#define MRON_VERSION "dev"
#endif

typedef enum {
    FORMAT_MRON,
    FORMAT_JSON,
    FORMAT_YAML,
    FORMAT_TOML,
    FORMAT_UNKNOWN,
} FileFormat;

static FileFormat detect_format(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if (!dot) return FORMAT_UNKNOWN;
    if (strcmp(dot, ".mron") == 0) return FORMAT_MRON;
    if (strcmp(dot, ".json") == 0) return FORMAT_JSON;
    if (strcmp(dot, ".yaml") == 0 || strcmp(dot, ".yml") == 0) return FORMAT_YAML;
    if (strcmp(dot, ".toml") == 0) return FORMAT_TOML;
    return FORMAT_UNKNOWN;
}

static const char *format_name(FileFormat f) {
    switch (f) {
    case FORMAT_MRON:  return "mron";
    case FORMAT_JSON:  return "json";
    case FORMAT_YAML:  return "yaml";
    case FORMAT_TOML:  return "toml";
    default:           return "unknown";
    }
}

static FileFormat parse_format_name(const char *s) {
    if (strcmp(s, "mron") == 0) return FORMAT_MRON;
    if (strcmp(s, "json") == 0) return FORMAT_JSON;
    if (strcmp(s, "yaml") == 0 || strcmp(s, "yml") == 0) return FORMAT_YAML;
    if (strcmp(s, "toml") == 0) return FORMAT_TOML;
    return FORMAT_UNKNOWN;
}

/* Auto-detect format from content (first non-whitespace character) */
static FileFormat detect_format_from_content(const char *src) {
    while (*src == ' ' || *src == '\t' || *src == '\n' || *src == '\r') src++;
    if (*src == '{' || *src == '[') return FORMAT_JSON;
    return FORMAT_MRON;
}

static void print_version(void) {
    printf("mronc %s\n", MRON_VERSION);
}

static void print_usage(FILE *out) {
    fprintf(out,
        "Usage: mronc [options] <input-file> [<input-file2> ...]\n"
        "       mronc [options] -                (read from stdin)\n"
        "       mronc --diff <file1> <file2>     (semantic diff)\n"
        "       mronc --merge <file1> <file2>    (deep merge)\n"
        "\n"
        "Translates between MRON, JSON, YAML, and TOML formats.\n"
        "Input format is auto-detected from the file extension.\n"
        "\n"
        "Conversion options:\n"
        "  -o, --output <file>   Write output to a file (or directory for batch)\n"
        "  -t, --to <format>     Force output format: mron, json, yaml, toml\n"
        "      --from <format>   Force input format: mron, json\n"
        "  -p, --pretty          Pretty-print JSON output\n"
        "  -m, --minify          Minify output (compact MRON / compact JSON)\n"
        "      --sort-keys       Sort record keys alphabetically\n"
        "      --indent <n>      Set indentation width in spaces (default: varies)\n"
        "      --strip-comments  Re-emit MRON with comments stripped\n"
        "\n"
        "Query & transform:\n"
        "  -q, --query <path>    Extract a value by dot-path (e.g. server.port)\n"
        "      --merge <file>    Deep-merge a second file over the input\n"
        "      --diff <file>     Semantic diff between input and this file\n"
        "\n"
        "Validation:\n"
        "  -c, --check           Validate input without producing output\n"
        "      --strict          Warn about stylistic issues (with --check)\n"
        "      --schema <file>   Validate input against a schema file\n"
        "\n"
        "Other:\n"
        "  -w, --watch           Watch input file and re-convert on change\n"
        "  -v, --verbose         Show processing details on stderr\n"
        "  -V, --version         Print version and exit\n"
        "  -h, --help            Show this help message\n"
        "\n"
        "Examples:\n"
        "  mronc config.mron                     Convert MRON to JSON (stdout)\n"
        "  mronc config.mron -o config.json      Convert to a JSON file\n"
        "  mronc config.mron -t yaml             Convert MRON to YAML\n"
        "  mronc config.mron -p --indent 4       Pretty JSON, 4-space indent\n"
        "  mronc config.mron -m                  Minified MRON output\n"
        "  mronc config.mron -q server.port      Query a single value\n"
        "  mronc base.mron --merge overrides.mron  Deep merge two files\n"
        "  mronc old.mron --diff new.mron        Show semantic differences\n"
        "  mronc config.mron -c --strict         Strict validation\n"
        "  mronc data.mron --schema schema.mron  Schema validation\n"
        "  mronc config.mron --sort-keys         Sorted key output\n"
        "  mronc config.mron -w -o out.json      Watch and auto-convert\n"
        "  mronc a.mron b.mron -o outdir/        Batch convert to directory\n"
        "  mronc --from mron - < input.txt       Read from stdin\n");
}

static int opt_match(const char *arg, const char *shortopt, const char *longopt) {
    return (shortopt && strcmp(arg, shortopt) == 0) ||
           (longopt && strcmp(arg, longopt) == 0);
}

static char *read_stdin_source(void) {
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

/* Parse source into AST based on format */
static AstNode *parse_source(const char *source, const char *fname, FileFormat fmt, int verbose) {
    AstNode *ast = NULL;
    if (fmt == FORMAT_MRON) {
        TokenArray *tokens = lexer_tokenize(source, fname);
        if (!tokens) return NULL;
        ast = parser_parse(tokens);
        lexer_free(tokens);
    } else if (fmt == FORMAT_JSON) {
        ast = json_parse(source, fname);
    } else {
        fprintf(stderr, "error: cannot parse %s format as input\n", format_name(fmt));
        return NULL;
    }
    if (ast && verbose) {
        if (ast->type == AST_RECORD) {
            fprintf(stderr, "Parsed %zu top-level key(s)\n", ast->data.record.count);
        } else if (ast->type == AST_LIST) {
            fprintf(stderr, "Parsed list with %zu item(s)\n", ast->data.list.count);
        }
    }
    return ast;
}

/* Emit AST to string based on format and options */
static char *emit_output(AstNode *ast, FileFormat out_fmt, int pretty, int minify,
                         int indent_set, int indent_width, int sort_keys) {
    /* Sort keys if requested (modifies AST in-place) */
    if (sort_keys) ast_sort_keys(ast);

    switch (out_fmt) {
    case FORMAT_JSON:
        if (minify) return json_emit(ast);
        if (indent_set) return json_emit_indent(ast, indent_width);
        if (pretty) return json_emit_pretty(ast);
        return json_emit(ast);
    case FORMAT_MRON:
        if (minify) return mron_emit_minify(ast);
        if (indent_set) return mron_emit_indent(ast, indent_width);
        return mron_emit(ast);
    case FORMAT_YAML: return yaml_emit(ast);
    case FORMAT_TOML: return toml_emit(ast);
    default: return NULL;
    }
}

/* Determine default output format when --to is not specified */
static FileFormat default_output_format(FileFormat input_fmt) {
    switch (input_fmt) {
    case FORMAT_MRON: return FORMAT_JSON;
    case FORMAT_JSON: return FORMAT_MRON;
    default:          return FORMAT_JSON;
    }
}

/* Emit a queried AST node as a simple value */
static void print_query_result(const AstNode *node) {
    switch (node->type) {
    case AST_STRING:
        printf("%s\n", node->data.string_val);
        break;
    case AST_NUMBER:
        printf("%s\n", node->data.number_str);
        break;
    case AST_BOOL:
        printf("%s\n", node->data.bool_val ? "true" : "false");
        break;
    case AST_NULL:
        printf("null\n");
        break;
    case AST_RECORD:
    case AST_LIST: {
        /* For composites, emit as JSON for readability */
        char *s = json_emit_pretty((AstNode *)node);
        if (s) { fputs(s, stdout); free(s); }
        break;
    }
    }
}

/* Get file modification time. Returns 0 on failure. */
static long long get_mtime(const char *path) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) return 0;
    ULARGE_INTEGER uli;
    uli.LowPart = data.ftLastWriteTime.dwLowDateTime;
    uli.HighPart = data.ftLastWriteTime.dwHighDateTime;
    return (long long)uli.QuadPart;
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (long long)st.st_mtime;
#endif
}

static void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

/* Build output path for batch mode: outdir/basename.ext */
static char *make_batch_output_path(const char *outdir, const char *input_path,
                                    FileFormat out_fmt) {
    const char *base = strrchr(input_path, '/');
    const char *base2 = strrchr(input_path, '\\');
    if (base2 && (!base || base2 > base)) base = base2;
    if (base) base++;
    else base = input_path;

    /* Strip extension */
    char name[512];
    size_t nlen = strlen(base);
    const char *dot = strrchr(base, '.');
    if (dot) nlen = (size_t)(dot - base);
    if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
    memcpy(name, base, nlen);
    name[nlen] = '\0';

    const char *ext = ".json";
    if (out_fmt == FORMAT_MRON) ext = ".mron";
    else if (out_fmt == FORMAT_YAML) ext = ".yaml";
    else if (out_fmt == FORMAT_TOML) ext = ".toml";

    size_t dir_len = strlen(outdir);
    size_t total = dir_len + 1 + nlen + strlen(ext) + 1;
    char *result = malloc(total);
    if (!result) { fprintf(stderr, "error: out of memory\n"); exit(1); }

    /* Ensure directory separator */
    int needs_sep = (dir_len > 0 && outdir[dir_len - 1] != '/' && outdir[dir_len - 1] != '\\');
    snprintf(result, total, "%s%s%s%s", outdir, needs_sep ? "/" : "", name, ext);
    return result;
}

/* Check if path ends with / or \ (indicates a directory for batch mode) */
static int is_dir_path(const char *path) {
    size_t len = strlen(path);
    return len > 0 && (path[len - 1] == '/' || path[len - 1] == '\\');
}

int main(int argc, char *argv[]) {
    /* Options */
    const char *output_path = NULL;
    const char *force_from = NULL;
    const char *force_to = NULL;
    const char *query_path = NULL;
    const char *merge_file = NULL;
    const char *diff_file = NULL;
    const char *schema_file = NULL;
    int pretty = 0;
    int minify = 0;
    int sort_keys = 0;
    int strip_comments = 0;
    int check_only = 0;
    int strict = 0;
    int watch = 0;
    int verbose = 0;
    int indent_set = 0;
    int indent_width = 2;
    int end_of_opts = 0;

    /* Collect input files */
    const char *input_files[256];
    int input_count = 0;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (!end_of_opts && strcmp(argv[i], "--") == 0) {
            end_of_opts = 1;
            continue;
        }

        if (!end_of_opts && argv[i][0] == '-' && argv[i][1] != '\0'
            && strcmp(argv[i], "-") != 0) {

            if (opt_match(argv[i], "-o", "--output")) {
                if (i + 1 >= argc) { fprintf(stderr, "error: %s requires an argument\n", argv[i]); return 1; }
                output_path = argv[++i];
            } else if (opt_match(argv[i], "-t", "--to")) {
                if (i + 1 >= argc) { fprintf(stderr, "error: %s requires an argument\n", argv[i]); return 1; }
                force_to = argv[++i];
                if (parse_format_name(force_to) == FORMAT_UNKNOWN) {
                    fprintf(stderr, "error: --to must be mron, json, yaml, or toml, got '%s'\n", force_to);
                    return 1;
                }
            } else if (opt_match(argv[i], NULL, "--from")) {
                if (i + 1 >= argc) { fprintf(stderr, "error: --from requires an argument\n"); return 1; }
                force_from = argv[++i];
                if (strcmp(force_from, "mron") != 0 && strcmp(force_from, "json") != 0) {
                    fprintf(stderr, "error: --from must be 'mron' or 'json', got '%s'\n", force_from);
                    return 1;
                }
            } else if (opt_match(argv[i], "-q", "--query")) {
                if (i + 1 >= argc) { fprintf(stderr, "error: %s requires a dot-path argument\n", argv[i]); return 1; }
                query_path = argv[++i];
            } else if (opt_match(argv[i], NULL, "--merge")) {
                if (i + 1 >= argc) { fprintf(stderr, "error: --merge requires a file argument\n"); return 1; }
                merge_file = argv[++i];
            } else if (opt_match(argv[i], NULL, "--diff")) {
                if (i + 1 >= argc) { fprintf(stderr, "error: --diff requires a file argument\n"); return 1; }
                diff_file = argv[++i];
            } else if (opt_match(argv[i], NULL, "--schema")) {
                if (i + 1 >= argc) { fprintf(stderr, "error: --schema requires a file argument\n"); return 1; }
                schema_file = argv[++i];
            } else if (opt_match(argv[i], NULL, "--indent")) {
                if (i + 1 >= argc) { fprintf(stderr, "error: --indent requires a number\n"); return 1; }
                indent_width = atoi(argv[++i]);
                if (indent_width < 0 || indent_width > 16) {
                    fprintf(stderr, "error: --indent must be between 0 and 16\n");
                    return 1;
                }
                indent_set = 1;
            } else if (opt_match(argv[i], "-p", "--pretty")) {
                pretty = 1;
            } else if (opt_match(argv[i], "-m", "--minify")) {
                minify = 1;
            } else if (opt_match(argv[i], NULL, "--sort-keys")) {
                sort_keys = 1;
            } else if (opt_match(argv[i], NULL, "--strip-comments")) {
                strip_comments = 1;
            } else if (opt_match(argv[i], "-c", "--check")) {
                check_only = 1;
            } else if (opt_match(argv[i], NULL, "--strict")) {
                strict = 1;
            } else if (opt_match(argv[i], "-w", "--watch")) {
                watch = 1;
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
            /* Positional argument (input file) */
            if (input_count >= 256) {
                fprintf(stderr, "error: too many input files (max 256)\n");
                return 1;
            }
            input_files[input_count++] = argv[i];
        }
    }

    if (input_count == 0) {
        print_usage(stderr);
        return 1;
    }

    /* Validate option combinations */
    if (minify && pretty) {
        fprintf(stderr, "error: --minify and --pretty are mutually exclusive\n");
        return 1;
    }
    if (watch && input_count > 1) {
        fprintf(stderr, "error: --watch only supports a single input file\n");
        return 1;
    }
    if (watch && strcmp(input_files[0], "-") == 0) {
        fprintf(stderr, "error: --watch cannot be used with stdin\n");
        return 1;
    }

    /* --strip-comments is a shortcut for MRON -> MRON conversion */
    if (strip_comments && !force_to) {
        force_to = "mron";
    }

    /* Batch mode: multiple files */
    int batch = (input_count > 1);
    if (batch && output_path && !is_dir_path(output_path)) {
        fprintf(stderr, "error: with multiple input files, --output must be a directory (end with /)\n");
        return 1;
    }

    /* ============================================================== */
    /* DIFF mode                                                       */
    /* ============================================================== */
    if (diff_file) {
        if (input_count != 1) {
            fprintf(stderr, "error: --diff requires exactly one input file\n");
            return 1;
        }
        const char *file_a = input_files[0];
        const char *file_b = diff_file;

        FileFormat fmt_a = force_from ? parse_format_name(force_from) : detect_format(file_a);
        FileFormat fmt_b = force_from ? parse_format_name(force_from) : detect_format(file_b);
        if (fmt_a == FORMAT_UNKNOWN) { fprintf(stderr, "error: cannot determine format of '%s'\n", file_a); return 1; }
        if (fmt_b == FORMAT_UNKNOWN) { fprintf(stderr, "error: cannot determine format of '%s'\n", file_b); return 1; }

        char *src_a = read_file(file_a);
        char *src_b = read_file(file_b);
        if (!src_a || !src_b) { free(src_a); free(src_b); return 1; }

        AstNode *ast_a = parse_source(src_a, file_a, fmt_a, verbose);
        AstNode *ast_b = parse_source(src_b, file_b, fmt_b, verbose);
        free(src_a); free(src_b);
        if (!ast_a || !ast_b) { ast_free(ast_a); ast_free(ast_b); return 1; }

        int diffs = ast_diff(ast_a, ast_b, stdout);
        if (diffs == 0) {
            printf("Files are semantically identical.\n");
        } else {
            printf("%d difference(s) found.\n", diffs);
        }
        ast_free(ast_a); ast_free(ast_b);
        return diffs > 0 ? 1 : 0;
    }

    /* ============================================================== */
    /* MERGE mode                                                      */
    /* ============================================================== */
    if (merge_file) {
        if (input_count != 1) {
            fprintf(stderr, "error: --merge requires exactly one input file\n");
            return 1;
        }
        const char *file_base = input_files[0];
        int reading_stdin = (strcmp(file_base, "-") == 0);

        FileFormat fmt_base;
        if (force_from) {
            fmt_base = parse_format_name(force_from);
        } else if (reading_stdin) {
            fmt_base = FORMAT_MRON;
        } else {
            fmt_base = detect_format(file_base);
        }
        FileFormat fmt_overlay = detect_format(merge_file);
        if (fmt_base == FORMAT_UNKNOWN) { fprintf(stderr, "error: cannot determine format of '%s'\n", file_base); return 1; }
        if (fmt_overlay == FORMAT_UNKNOWN) { fprintf(stderr, "error: cannot determine format of '%s'\n", merge_file); return 1; }

        char *src_base = reading_stdin ? read_stdin_source() : read_file(file_base);
        char *src_overlay = read_file(merge_file);
        if (!src_base || !src_overlay) { free(src_base); free(src_overlay); return 1; }

        AstNode *ast_base = parse_source(src_base, reading_stdin ? "<stdin>" : file_base, fmt_base, verbose);
        AstNode *ast_overlay = parse_source(src_overlay, merge_file, fmt_overlay, verbose);
        free(src_base); free(src_overlay);
        if (!ast_base || !ast_overlay) { ast_free(ast_base); ast_free(ast_overlay); return 1; }

        AstNode *merged = ast_merge(ast_base, ast_overlay);
        ast_free(ast_base); ast_free(ast_overlay);
        if (!merged) { fprintf(stderr, "error: merge failed\n"); return 1; }

        FileFormat out_fmt = force_to ? parse_format_name(force_to) : default_output_format(fmt_base);
        char *output = emit_output(merged, out_fmt, pretty, minify, indent_set, indent_width, sort_keys);
        ast_free(merged);
        if (!output) { fprintf(stderr, "error: emit failed\n"); return 1; }

        if (output_path) {
            FILE *f = fopen(output_path, "w");
            if (!f) { fprintf(stderr, "error: cannot open '%s'\n", output_path); free(output); return 1; }
            fputs(output, f); fclose(f);
            if (verbose) fprintf(stderr, "Wrote %zu bytes to %s\n", strlen(output), output_path);
        } else {
            fputs(output, stdout);
        }
        free(output);
        return 0;
    }

    /* ============================================================== */
    /* Main processing loop (single file, batch, or watch)             */
    /* ============================================================== */

    int exit_code = 0;

    do { /* watch loop (runs once if !watch) */

    for (int fi = 0; fi < input_count; fi++) {
        const char *input_file = input_files[fi];
        int reading_stdin = (strcmp(input_file, "-") == 0);

        /* Determine input format */
        FileFormat input_format;
        if (force_from) {
            input_format = parse_format_name(force_from);
        } else if (reading_stdin) {
            input_format = FORMAT_UNKNOWN; /* will auto-detect from content */
        } else {
            input_format = detect_format(input_file);
            if (input_format == FORMAT_UNKNOWN) {
                fprintf(stderr, "error: cannot determine format of '%s'\n", input_file);
                fprintf(stderr, "Hint: use --from mron or --from json to specify the format.\n");
                exit_code = 1; continue;
            }
        }

        /* Read input */
        char *source;
        if (reading_stdin) {
            if (verbose) fprintf(stderr, "Reading from stdin...\n");
            source = read_stdin_source();
        } else {
            if (verbose) fprintf(stderr, "Reading %s...\n", input_file);
            source = read_file(input_file);
        }
        if (!source) { exit_code = 1; continue; }

        /* Auto-detect stdin format from content (#8) */
        if (input_format == FORMAT_UNKNOWN) {
            input_format = detect_format_from_content(source);
            if (verbose) fprintf(stderr, "Auto-detected format: %s\n", format_name(input_format));
        }

        if (verbose) {
            fprintf(stderr, "Input: %zu bytes, format: %s\n",
                    strlen(source), format_name(input_format));
        }

        const char *fname = reading_stdin ? "<stdin>" : input_file;
        AstNode *ast = parse_source(source, fname, input_format, verbose);
        free(source);
        if (!ast) { exit_code = 1; continue; }

        /* Schema validation (#13) */
        if (schema_file) {
            FileFormat sfmt = detect_format(schema_file);
            if (sfmt == FORMAT_UNKNOWN) sfmt = FORMAT_MRON;
            char *schema_src = read_file(schema_file);
            if (!schema_src) { ast_free(ast); exit_code = 1; continue; }
            AstNode *schema_ast = parse_source(schema_src, schema_file, sfmt, 0);
            free(schema_src);
            if (!schema_ast) { ast_free(ast); exit_code = 1; continue; }

            int violations = ast_validate_schema(ast, schema_ast, stderr);
            ast_free(schema_ast);
            if (violations > 0) {
                fprintf(stderr, "%d schema violation(s) in %s\n", violations, fname);
                if (check_only) { ast_free(ast); exit_code = 1; continue; }
            } else if (verbose) {
                fprintf(stderr, "Schema validation passed for %s\n", fname);
            }
        }

        /* Strict check (#12) */
        if (strict && check_only && ast->type == AST_RECORD) {
            /* Warn about empty records/lists */
            for (size_t i = 0; i < ast->data.record.count; i++) {
                AstNode *val = ast->data.record.pairs[i].value;
                if (val->type == AST_RECORD && val->data.record.count == 0) {
                    fprintf(stderr, "warning: '%s' is an empty record\n",
                            ast->data.record.pairs[i].key);
                }
                if (val->type == AST_LIST && val->data.list.count == 0) {
                    fprintf(stderr, "warning: '%s' is an empty list\n",
                            ast->data.record.pairs[i].key);
                }
                if (val->type == AST_NULL) {
                    fprintf(stderr, "warning: '%s' is null\n",
                            ast->data.record.pairs[i].key);
                }
            }
        }

        /* Check-only mode */
        if (check_only) {
            if (verbose) fprintf(stderr, "Validation passed: %s\n", fname);
            ast_free(ast);
            continue;
        }

        /* Query mode (#4) */
        if (query_path) {
            const AstNode *result = ast_query(ast, query_path);
            if (!result) {
                fprintf(stderr, "error: path '%s' not found\n", query_path);
                ast_free(ast);
                exit_code = 1; continue;
            }
            print_query_result(result);
            ast_free(ast);
            continue;
        }

        /* Determine output format */
        FileFormat out_fmt;
        if (force_to) {
            out_fmt = parse_format_name(force_to);
        } else if (output_path && !batch) {
            FileFormat detected = detect_format(output_path);
            out_fmt = (detected != FORMAT_UNKNOWN) ? detected : default_output_format(input_format);
        } else {
            out_fmt = default_output_format(input_format);
        }

        /* Check MRON record requirement */
        if (out_fmt == FORMAT_MRON && ast->type != AST_RECORD) {
            fprintf(stderr, "error: MRON requires a top-level record, but got %s\n",
                    ast_type_name(ast->type));
            ast_free(ast);
            exit_code = 1; continue;
        }
        if (out_fmt == FORMAT_TOML && ast->type != AST_RECORD) {
            fprintf(stderr, "error: TOML requires a top-level record, but got %s\n",
                    ast_type_name(ast->type));
            ast_free(ast);
            exit_code = 1; continue;
        }

        /* Emit output */
        char *output = emit_output(ast, out_fmt, pretty, minify, indent_set, indent_width, sort_keys);
        ast_free(ast);
        if (!output) { fprintf(stderr, "error: emit failed\n"); exit_code = 1; continue; }

        /* Write output */
        if (output_path) {
            char *actual_path;
            if (batch) {
                actual_path = make_batch_output_path(output_path, input_file, out_fmt);
            } else {
                actual_path = mron_strdup(output_path);
            }
            FILE *f = fopen(actual_path, "w");
            if (!f) {
                fprintf(stderr, "error: cannot open '%s'\n", actual_path);
                free(actual_path); free(output); exit_code = 1; continue;
            }
            fputs(output, f); fclose(f);
            if (verbose) fprintf(stderr, "Wrote %zu bytes to %s\n", strlen(output), actual_path);
            free(actual_path);
        } else {
            fputs(output, stdout);
        }
        free(output);
    } /* end for each input file */

    /* Watch mode (#11): sleep and re-run if file changed */
    if (watch) {
        static long long last_mtime = 0;
        long long cur = get_mtime(input_files[0]);
        if (last_mtime == 0) last_mtime = cur;

        for (;;) {
            sleep_ms(500);
            long long now = get_mtime(input_files[0]);
            if (now != last_mtime) {
                last_mtime = now;
                if (verbose) fprintf(stderr, "File changed, reconverting...\n");
                break;
            }
        }
    }

    } while (watch); /* end watch loop */

    return exit_code;
}
