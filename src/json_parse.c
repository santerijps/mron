#include "json_parse.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#define MAX_JSON_DEPTH 256

typedef struct {
    const char *source;
    const char *filename;
    size_t pos;
    int line;
    int col;
    int depth;
} JsonParser;

static char jp_peek(JsonParser *P) {
    return P->source[P->pos];
}

static char jp_advance(JsonParser *P) {
    char c = P->source[P->pos];
    if (c == '\0') return c;
    P->pos++;
    if (c == '\n') {
        P->line++;
        P->col = 1;
    } else {
        P->col++;
    }
    return c;
}

static void jp_skip_ws(JsonParser *P) {
    while (P->source[P->pos] == ' ' || P->source[P->pos] == '\t' ||
           P->source[P->pos] == '\n' || P->source[P->pos] == '\r') {
        jp_advance(P);
    }
}

static int hex_digit_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_hex4(JsonParser *P) {
    int val = 0;
    for (int i = 0; i < 4; i++) {
        char c = jp_peek(P);
        int d = hex_digit_val(c);
        if (d < 0) {
            mron_error(P->filename, P->line, P->col,
                       "invalid hex digit '%c' in unicode escape", c);
            return -1;
        }
        val = (val << 4) | d;
        jp_advance(P);
    }
    return val;
}

static int encode_utf8(unsigned int cp, char *buf) {
    if (cp <= 0x7F) {
        buf[0] = (char)cp;
        return 1;
    } else if (cp <= 0x7FF) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp <= 0xFFFF) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp <= 0x10FFFF) {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

static AstNode *jp_parse_value(JsonParser *P);

static AstNode *jp_parse_string(JsonParser *P) {
    int start_line = P->line;
    int start_col = P->col;
    jp_advance(P); /* consume opening " */

    StringBuilder sb;
    sb_init(&sb);

    for (;;) {
        char c = jp_peek(P);
        if (c == '\0') {
            sb_free(&sb);
            mron_error(P->filename, start_line, start_col, "unterminated JSON string");
            return NULL;
        }
        if (c == '"') {
            jp_advance(P);
            break;
        }
        if (c == '\\') {
            jp_advance(P);
            char esc = jp_peek(P);
            jp_advance(P);
            switch (esc) {
            case '"':  sb_append_char(&sb, '"'); break;
            case '\\': sb_append_char(&sb, '\\'); break;
            case '/':  sb_append_char(&sb, '/'); break;
            case 'b':  sb_append_char(&sb, '\b'); break;
            case 'f':  sb_append_char(&sb, '\f'); break;
            case 'n':  sb_append_char(&sb, '\n'); break;
            case 'r':  sb_append_char(&sb, '\r'); break;
            case 't':  sb_append_char(&sb, '\t'); break;
            case 'u': {
                int cp = parse_hex4(P);
                if (cp < 0) { sb_free(&sb); return NULL; }

                /* Handle surrogate pairs */
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (jp_peek(P) != '\\') {
                        mron_error(P->filename, P->line, P->col,
                                   "expected low surrogate after high surrogate");
                        sb_free(&sb);
                        return NULL;
                    }
                    jp_advance(P);
                    if (jp_peek(P) != 'u') {
                        mron_error(P->filename, P->line, P->col,
                                   "expected \\u after high surrogate");
                        sb_free(&sb);
                        return NULL;
                    }
                    jp_advance(P);
                    int low = parse_hex4(P);
                    if (low < 0) { sb_free(&sb); return NULL; }
                    if (low < 0xDC00 || low > 0xDFFF) {
                        mron_error(P->filename, P->line, P->col,
                                   "invalid low surrogate 0x%04X", low);
                        sb_free(&sb);
                        return NULL;
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    mron_error(P->filename, P->line, P->col,
                               "unexpected lone low surrogate \\u%04X", cp);
                    sb_free(&sb);
                    return NULL;
                }

                char utf8[4];
                int len = encode_utf8((unsigned int)cp, utf8);
                sb_append_n(&sb, utf8, (size_t)len);
                break;
            }
            default:
                mron_error(P->filename, P->line, P->col,
                           "invalid JSON escape sequence '\\%c'", esc);
                sb_free(&sb);
                return NULL;
            }
        } else {
            jp_advance(P);
            sb_append_char(&sb, c);
        }
    }

    char *str = sb_finish(&sb);
    AstNode *node = ast_new_string(str, start_line, start_col);
    free(str);
    return node;
}

static AstNode *jp_parse_number(JsonParser *P) {
    int start_line = P->line;
    int start_col = P->col;
    StringBuilder sb;
    sb_init(&sb);

    /* Optional minus */
    if (jp_peek(P) == '-') {
        sb_append_char(&sb, jp_advance(P));
    }

    /* Integer part */
    if (jp_peek(P) == '0') {
        sb_append_char(&sb, jp_advance(P));
    } else if (isdigit((unsigned char)jp_peek(P))) {
        while (isdigit((unsigned char)jp_peek(P))) {
            sb_append_char(&sb, jp_advance(P));
        }
    } else {
        mron_error(P->filename, P->line, P->col,
                   "expected digit in number");
        sb_free(&sb);
        return NULL;
    }

    /* Fractional part */
    if (jp_peek(P) == '.') {
        sb_append_char(&sb, jp_advance(P));
        if (!isdigit((unsigned char)jp_peek(P))) {
            mron_error(P->filename, P->line, P->col,
                       "expected digit after decimal point");
            sb_free(&sb);
            return NULL;
        }
        while (isdigit((unsigned char)jp_peek(P))) {
            sb_append_char(&sb, jp_advance(P));
        }
    }

    /* Exponent part */
    if (jp_peek(P) == 'e' || jp_peek(P) == 'E') {
        sb_append_char(&sb, jp_advance(P));
        if (jp_peek(P) == '+' || jp_peek(P) == '-') {
            sb_append_char(&sb, jp_advance(P));
        }
        if (!isdigit((unsigned char)jp_peek(P))) {
            mron_error(P->filename, P->line, P->col,
                       "expected digit in exponent");
            sb_free(&sb);
            return NULL;
        }
        while (isdigit((unsigned char)jp_peek(P))) {
            sb_append_char(&sb, jp_advance(P));
        }
    }

    char *num_str = sb_finish(&sb);
    AstNode *node = ast_new_number(num_str, start_line, start_col);
    free(num_str);
    return node;
}

static int jp_match_keyword(JsonParser *P, const char *kw) {
    size_t len = strlen(kw);
    if (strncmp(P->source + P->pos, kw, len) == 0) {
        char next = P->source[P->pos + len];
        if (isalnum((unsigned char)next) || next == '_') {
            return 0;
        }
        for (size_t i = 0; i < len; i++) {
            jp_advance(P);
        }
        return 1;
    }
    return 0;
}

static AstNode *jp_parse_object(JsonParser *P) {
    int start_line = P->line;
    int start_col = P->col;
    jp_advance(P); /* consume { */

    AstNode *record = ast_new_record(start_line, start_col);

    jp_skip_ws(P);
    if (jp_peek(P) == '}') {
        jp_advance(P);
        return record;
    }

    for (;;) {
        jp_skip_ws(P);
        if (jp_peek(P) != '"') {
            mron_error(P->filename, P->line, P->col,
                       "expected string key in JSON object, got '%c'", jp_peek(P));
            ast_free(record);
            return NULL;
        }

        AstNode *key_node = jp_parse_string(P);
        if (!key_node) {
            ast_free(record);
            return NULL;
        }
        char *key = mron_strdup(key_node->data.string_val);
        ast_free(key_node);

        jp_skip_ws(P);
        if (jp_peek(P) != ':') {
            mron_error(P->filename, P->line, P->col,
                       "expected ':' after key in JSON object");
            free(key);
            ast_free(record);
            return NULL;
        }
        jp_advance(P);

        jp_skip_ws(P);
        AstNode *value = jp_parse_value(P);
        if (!value) {
            free(key);
            ast_free(record);
            return NULL;
        }

        /* Check for duplicate keys */
        for (size_t i = 0; i < record->data.record.count; i++) {
            if (strcmp(record->data.record.pairs[i].key, key) == 0) {
                mron_error(P->filename, P->line, P->col,
                           "duplicate key \"%s\" in JSON object", key);
                ast_free(value);
                free(key);
                ast_free(record);
                return NULL;
            }
        }

        ast_record_add(record, key, value);
        free(key);

        jp_skip_ws(P);
        if (jp_peek(P) == ',') {
            jp_advance(P);
            continue;
        }
        if (jp_peek(P) == '}') {
            jp_advance(P);
            break;
        }

        mron_error(P->filename, P->line, P->col,
                   "expected ',' or '}' in JSON object, got '%c'", jp_peek(P));
        ast_free(record);
        return NULL;
    }

    return record;
}

static AstNode *jp_parse_array(JsonParser *P) {
    int start_line = P->line;
    int start_col = P->col;
    jp_advance(P); /* consume [ */

    AstNode *list = ast_new_list(start_line, start_col);

    jp_skip_ws(P);
    if (jp_peek(P) == ']') {
        jp_advance(P);
        return list;
    }

    for (;;) {
        jp_skip_ws(P);
        AstNode *item = jp_parse_value(P);
        if (!item) {
            ast_free(list);
            return NULL;
        }
        ast_list_add(list, item);

        jp_skip_ws(P);
        if (jp_peek(P) == ',') {
            jp_advance(P);
            continue;
        }
        if (jp_peek(P) == ']') {
            jp_advance(P);
            break;
        }

        mron_error(P->filename, P->line, P->col,
                   "expected ',' or ']' in JSON array, got '%c'", jp_peek(P));
        ast_free(list);
        return NULL;
    }

    return list;
}

static AstNode *jp_parse_value(JsonParser *P) {
    jp_skip_ws(P);
    int line = P->line;
    int col = P->col;
    char c = jp_peek(P);

    switch (c) {
    case '"':
        return jp_parse_string(P);
    case '{':
    {
        if (P->depth >= MAX_JSON_DEPTH) {
            mron_error(P->filename, line, col, "maximum nesting depth exceeded");
            return NULL;
        }
        P->depth++;
        AstNode *node = jp_parse_object(P);
        P->depth--;
        return node;
    }
    case '[':
    {
        if (P->depth >= MAX_JSON_DEPTH) {
            mron_error(P->filename, line, col, "maximum nesting depth exceeded");
            return NULL;
        }
        P->depth++;
        AstNode *node = jp_parse_array(P);
        P->depth--;
        return node;
    }
    case 't':
        if (jp_match_keyword(P, "true")) return ast_new_bool(1, line, col);
        mron_error(P->filename, line, col, "invalid JSON value");
        return NULL;
    case 'f':
        if (jp_match_keyword(P, "false")) return ast_new_bool(0, line, col);
        mron_error(P->filename, line, col, "invalid JSON value");
        return NULL;
    case 'n':
        if (jp_match_keyword(P, "null")) return ast_new_null(line, col);
        mron_error(P->filename, line, col, "invalid JSON value");
        return NULL;
    case '\0':
        mron_error(P->filename, line, col, "unexpected end of JSON input");
        return NULL;
    default:
        if (c == '-' || isdigit((unsigned char)c)) {
            return jp_parse_number(P);
        }
        mron_error(P->filename, line, col,
                   "unexpected character '%c' in JSON", c);
        return NULL;
    }
}

AstNode *json_parse(const char *source, const char *filename) {
    JsonParser P;
    P.source = source;
    P.filename = filename;
    P.pos = 0;
    P.line = 1;
    P.col = 1;
    P.depth = 0;

    AstNode *root = jp_parse_value(&P);
    if (!root) return NULL;

    jp_skip_ws(&P);
    if (jp_peek(&P) != '\0') {
        mron_error(filename, P.line, P.col,
                   "unexpected content after JSON value");
        ast_free(root);
        return NULL;
    }

    return root;
}
