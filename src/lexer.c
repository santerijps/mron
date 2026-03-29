#include "lexer.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

typedef struct {
    const char *source;
    const char *filename;
    size_t pos;
    int line;
    int col;
    int has_error;
} LexerState;

static char lex_peek(LexerState *L) {
    return L->source[L->pos];
}

static char lex_advance(LexerState *L) {
    char c = L->source[L->pos];
    if (c == '\0') return c;
    L->pos++;
    if (c == '\n') {
        L->line++;
        L->col = 1;
    } else {
        L->col++;
    }
    return c;
}

static int is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int is_ident_start(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

static int is_ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '-';
}

static int is_number_char(char c) {
    return isdigit((unsigned char)c) || c == '_' || c == ',' || c == '.';
}

static void skip_whitespace_and_comments(LexerState *L) {
    for (;;) {
        /* Skip whitespace */
        while (is_whitespace(lex_peek(L))) {
            lex_advance(L);
        }

        /* Check for comments */
        if (lex_peek(L) != '#') break;

        /* Check for multi-line comment ### */
        if (L->source[L->pos + 1] == '#' && L->source[L->pos + 2] == '#') {
            int start_line = L->line;
            int start_col = L->col;
            lex_advance(L); /* # */
            lex_advance(L); /* # */
            lex_advance(L); /* # */

            /* Read until closing ### */
            for (;;) {
                char c = lex_peek(L);
                if (c == '\0') {
                    mron_error(L->filename, start_line, start_col,
                               "unterminated multi-line comment (started here)");
                    L->has_error = 1;
                    return;
                }
                if (c == '#' && L->source[L->pos + 1] == '#' && L->source[L->pos + 2] == '#') {
                    lex_advance(L); /* # */
                    lex_advance(L); /* # */
                    lex_advance(L); /* # */
                    break;
                }
                lex_advance(L);
            }
        } else {
            /* Single-line comment: skip to end of line */
            while (lex_peek(L) != '\0' && lex_peek(L) != '\n') {
                lex_advance(L);
            }
        }
    }
}

static void token_array_add(TokenArray *arr, Token tok) {
    if (arr->count >= arr->capacity) {
        size_t new_cap = arr->capacity == 0 ? 64 : arr->capacity * 2;
        Token *new_tokens = realloc(arr->tokens, new_cap * sizeof(Token));
        if (!new_tokens) {
            fprintf(stderr, "error: out of memory\n");
            exit(1);
        }
        arr->tokens = new_tokens;
        arr->capacity = new_cap;
    }
    arr->tokens[arr->count++] = tok;
}

static Token make_token(TokenType type, const char *value, int line, int col) {
    Token t;
    t.type = type;
    t.value = value ? mron_strdup(value) : NULL;
    t.line = line;
    t.col = col;
    return t;
}

static Token make_simple_token(TokenType type, int line, int col) {
    return make_token(type, NULL, line, col);
}

static Token lex_string(LexerState *L) {
    int start_line = L->line;
    int start_col = L->col;
    lex_advance(L); /* consume opening " */

    StringBuilder sb;
    sb_init(&sb);

    for (;;) {
        char c = lex_peek(L);
        if (c == '\0') {
            sb_free(&sb);
            mron_error(L->filename, start_line, start_col, "unterminated string literal");
            return make_token(TOKEN_ERROR, "unterminated string", start_line, start_col);
        }
        if (c == '\n') {
            sb_free(&sb);
            mron_error(L->filename, L->line, L->col,
                       "newline inside string literal (string started at %d:%d)",
                       start_line, start_col);
            return make_token(TOKEN_ERROR, "newline in string", start_line, start_col);
        }
        if (c == '"') {
            lex_advance(L); /* consume closing " */
            break;
        }
        if (c == '\\') {
            lex_advance(L); /* consume backslash */
            char esc = lex_peek(L);
            if (esc == '\0') {
                sb_free(&sb);
                mron_error(L->filename, L->line, L->col,
                           "unexpected end of file after backslash in string");
                return make_token(TOKEN_ERROR, "unterminated escape", start_line, start_col);
            }
            lex_advance(L); /* consume escape char */
            switch (esc) {
            case '"':  sb_append_char(&sb, '"'); break;
            case '\\': sb_append_char(&sb, '\\'); break;
            case '/':  sb_append_char(&sb, '/'); break;
            case 'n':  sb_append_char(&sb, '\n'); break;
            case 't':  sb_append_char(&sb, '\t'); break;
            case 'r':  sb_append_char(&sb, '\r'); break;
            case 'b':  sb_append_char(&sb, '\b'); break;
            case 'f':  sb_append_char(&sb, '\f'); break;
            default:
                sb_free(&sb);
                mron_error(L->filename, L->line, L->col - 1,
                           "invalid escape sequence '\\%c'", esc);
                return make_token(TOKEN_ERROR, "invalid escape", start_line, start_col);
            }
        } else {
            lex_advance(L);
            sb_append_char(&sb, c);
        }
    }

    char *str = sb_finish(&sb);
    Token tok = make_token(TOKEN_STRING, str, start_line, start_col);
    free(str);
    return tok;
}

static Token lex_number(LexerState *L, int negative) {
    int start_line = L->line;
    int start_col = L->col;
    StringBuilder raw;
    sb_init(&raw);

    if (negative) {
        lex_advance(L); /* consume '-' */
        sb_append_char(&raw, '-');
    }

    /* Read all valid number characters */
    while (isdigit((unsigned char)lex_peek(L)) || is_number_char(lex_peek(L))) {
        sb_append_char(&raw, lex_peek(L));
        lex_advance(L);
    }

    char *raw_str = sb_finish(&raw);

    /* Validate: must end with a digit */
    size_t raw_len = strlen(raw_str);
    size_t check_start = negative ? 1 : 0;
    if (raw_len <= check_start || !isdigit((unsigned char)raw_str[raw_len - 1])) {
        mron_error(L->filename, start_line, start_col, "number must end with a digit: '%s'", raw_str);
        free(raw_str);
        return make_token(TOKEN_ERROR, "invalid number", start_line, start_col);
    }

    /* Validate: at most one period */
    int period_count = 0;
    for (size_t i = check_start; i < raw_len; i++) {
        if (raw_str[i] == '.') period_count++;
    }
    if (period_count > 1) {
        mron_error(L->filename, start_line, start_col,
                   "number has more than one decimal point: '%s'", raw_str);
        free(raw_str);
        return make_token(TOKEN_ERROR, "invalid number", start_line, start_col);
    }

    /* Clean number: strip underscores and commas */
    StringBuilder clean;
    sb_init(&clean);
    for (size_t i = 0; i < raw_len; i++) {
        if (raw_str[i] != '_' && raw_str[i] != ',') {
            sb_append_char(&clean, raw_str[i]);
        }
    }
    free(raw_str);

    char *clean_str = sb_finish(&clean);
    Token tok = make_token(TOKEN_NUMBER, clean_str, start_line, start_col);
    free(clean_str);
    return tok;
}

static Token lex_ident(LexerState *L) {
    int start_line = L->line;
    int start_col = L->col;
    StringBuilder sb;
    sb_init(&sb);

    while (is_ident_char(lex_peek(L))) {
        sb_append_char(&sb, lex_peek(L));
        lex_advance(L);
    }

    char *str = sb_finish(&sb);
    Token tok = make_token(TOKEN_IDENT, str, start_line, start_col);
    free(str);
    return tok;
}

TokenArray *lexer_tokenize(const char *source, const char *filename) {
    TokenArray *arr = calloc(1, sizeof(TokenArray));
    if (!arr) return NULL;
    arr->filename = filename;
    arr->has_error = 0;

    LexerState L;
    L.source = source;
    L.filename = filename;
    L.pos = 0;
    L.line = 1;
    L.col = 1;
    L.has_error = 0;

    for (;;) {
        skip_whitespace_and_comments(&L);
        if (L.has_error) {
            arr->has_error = 1;
            token_array_add(arr, make_token(TOKEN_ERROR, "unterminated comment", L.line, L.col));
            token_array_add(arr, make_simple_token(TOKEN_EOF, L.line, L.col));
            break;
        }
        char c = lex_peek(&L);

        if (c == '\0') {
            token_array_add(arr, make_simple_token(TOKEN_EOF, L.line, L.col));
            break;
        }

        Token tok;
        switch (c) {
        case '{':
            tok = make_simple_token(TOKEN_LBRACE, L.line, L.col);
            lex_advance(&L);
            break;
        case '}':
            tok = make_simple_token(TOKEN_RBRACE, L.line, L.col);
            lex_advance(&L);
            break;
        case '[':
            tok = make_simple_token(TOKEN_LBRACKET, L.line, L.col);
            lex_advance(&L);
            break;
        case ']':
            tok = make_simple_token(TOKEN_RBRACKET, L.line, L.col);
            lex_advance(&L);
            break;
        case '(':
            tok = make_simple_token(TOKEN_LPAREN, L.line, L.col);
            lex_advance(&L);
            break;
        case ')':
            tok = make_simple_token(TOKEN_RPAREN, L.line, L.col);
            lex_advance(&L);
            break;
        case '"':
            tok = lex_string(&L);
            break;
        default:
            if (isdigit((unsigned char)c)) {
                tok = lex_number(&L, 0);
            } else if (c == '-' && isdigit((unsigned char)L.source[L.pos + 1])) {
                tok = lex_number(&L, 1);
            } else if (is_ident_start(c)) {
                tok = lex_ident(&L);
            } else {
                mron_error(L.filename, L.line, L.col,
                           "unexpected character '%c' (0x%02X)", c, (unsigned char)c);
                tok = make_token(TOKEN_ERROR, "unexpected character", L.line, L.col);
                lex_advance(&L);
            }
            break;
        }

        if (tok.type == TOKEN_ERROR) {
            arr->has_error = 1;
        }
        token_array_add(arr, tok);

        if (tok.type == TOKEN_ERROR) {
            /* Add EOF after error to stop parsing */
            token_array_add(arr, make_simple_token(TOKEN_EOF, L.line, L.col));
            break;
        }
    }

    return arr;
}

void lexer_free(TokenArray *arr) {
    if (!arr) return;
    for (size_t i = 0; i < arr->count; i++) {
        free(arr->tokens[i].value);
    }
    free(arr->tokens);
    free(arr);
}

const char *token_type_name(TokenType type) {
    switch (type) {
    case TOKEN_IDENT:    return "identifier";
    case TOKEN_STRING:   return "string";
    case TOKEN_NUMBER:   return "number";
    case TOKEN_LBRACE:   return "'{'";
    case TOKEN_RBRACE:   return "'}'";
    case TOKEN_LBRACKET: return "'['";
    case TOKEN_RBRACKET: return "']'";
    case TOKEN_LPAREN:   return "'('";
    case TOKEN_RPAREN:   return "')'";
    case TOKEN_EOF:      return "end of file";
    case TOKEN_ERROR:    return "error";
    }
    return "unknown";
}
