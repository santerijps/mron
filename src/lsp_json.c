#include "lsp_json.h"
#include <stdio.h>
#include <string.h>

void lj_init(LspJson *j) {
    sb_init(&j->sb);
    j->depth = 0;
}

char *lj_finish(LspJson *j) {
    return sb_finish(&j->sb);
}

static void lj_comma(LspJson *j) {
    if (j->depth > 0 && !j->first[j->depth - 1]) {
        sb_append_char(&j->sb, ',');
    }
    if (j->depth > 0) {
        j->first[j->depth - 1] = 0;
    }
}

void lj_object_start(LspJson *j) {
    lj_comma(j);
    sb_append_char(&j->sb, '{');
    if (j->depth < 64) {
        j->first[j->depth] = 1;
        j->depth++;
    }
}

void lj_object_end(LspJson *j) {
    if (j->depth > 0) j->depth--;
    sb_append_char(&j->sb, '}');
}

void lj_array_start(LspJson *j) {
    lj_comma(j);
    sb_append_char(&j->sb, '[');
    if (j->depth < 64) {
        j->first[j->depth] = 1;
        j->depth++;
    }
}

void lj_array_end(LspJson *j) {
    if (j->depth > 0) j->depth--;
    sb_append_char(&j->sb, ']');
}

void lj_key(LspJson *j, const char *name) {
    lj_comma(j);
    sb_append_char(&j->sb, '"');
    sb_append(&j->sb, name);
    sb_append_char(&j->sb, '"');
    sb_append_char(&j->sb, ':');
    /* Mark as "first" again so the value doesn't get a comma */
    if (j->depth > 0) j->first[j->depth - 1] = 1;
}

static void lj_escape_string(LspJson *j, const char *val, size_t len) {
    sb_append_char(&j->sb, '"');
    for (size_t i = 0; i < len; i++) {
        char c = val[i];
        switch (c) {
        case '"':  sb_append(&j->sb, "\\\""); break;
        case '\\': sb_append(&j->sb, "\\\\"); break;
        case '\n': sb_append(&j->sb, "\\n"); break;
        case '\r': sb_append(&j->sb, "\\r"); break;
        case '\t': sb_append(&j->sb, "\\t"); break;
        case '\b': sb_append(&j->sb, "\\b"); break;
        case '\f': sb_append(&j->sb, "\\f"); break;
        default:
            if ((unsigned char)c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                sb_append(&j->sb, buf);
            } else {
                sb_append_char(&j->sb, c);
            }
        }
    }
    sb_append_char(&j->sb, '"');
}

void lj_string(LspJson *j, const char *val) {
    lj_comma(j);
    lj_escape_string(j, val, strlen(val));
}

void lj_string_len(LspJson *j, const char *val, size_t len) {
    lj_comma(j);
    lj_escape_string(j, val, len);
}

void lj_int(LspJson *j, int val) {
    lj_comma(j);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", val);
    sb_append(&j->sb, buf);
}

void lj_bool(LspJson *j, int val) {
    lj_comma(j);
    sb_append(&j->sb, val ? "true" : "false");
}

void lj_null(LspJson *j) {
    lj_comma(j);
    sb_append(&j->sb, "null");
}

void lj_raw(LspJson *j, const char *json) {
    lj_comma(j);
    sb_append(&j->sb, json);
}

void lj_position(LspJson *j, int line, int character) {
    lj_object_start(j);
    lj_key(j, "line"); lj_int(j, line);
    lj_key(j, "character"); lj_int(j, character);
    lj_object_end(j);
}

void lj_range(LspJson *j, int sl, int sc, int el, int ec) {
    lj_object_start(j);
    lj_key(j, "start"); lj_position(j, sl, sc);
    lj_key(j, "end"); lj_position(j, el, ec);
    lj_object_end(j);
}
