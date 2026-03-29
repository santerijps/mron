#include "json_emit.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

static void emit_value(StringBuilder *sb, AstNode *node);

static void emit_json_string(StringBuilder *sb, const char *str) {
    sb_append_char(sb, '"');
    for (const char *p = str; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
        case '"':  sb_append(sb, "\\\""); break;
        case '\\': sb_append(sb, "\\\\"); break;
        case '\b': sb_append(sb, "\\b"); break;
        case '\f': sb_append(sb, "\\f"); break;
        case '\n': sb_append(sb, "\\n"); break;
        case '\r': sb_append(sb, "\\r"); break;
        case '\t': sb_append(sb, "\\t"); break;
        default:
            if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                sb_append(sb, buf);
            } else {
                sb_append_char(sb, (char)c);
            }
            break;
        }
    }
    sb_append_char(sb, '"');
}

static void emit_record(StringBuilder *sb, AstNode *node) {
    sb_append_char(sb, '{');
    for (size_t i = 0; i < node->data.record.count; i++) {
        if (i > 0) sb_append_char(sb, ',');
        emit_json_string(sb, node->data.record.pairs[i].key);
        sb_append_char(sb, ':');
        emit_value(sb, node->data.record.pairs[i].value);
    }
    sb_append_char(sb, '}');
}

static void emit_list(StringBuilder *sb, AstNode *node) {
    sb_append_char(sb, '[');
    for (size_t i = 0; i < node->data.list.count; i++) {
        if (i > 0) sb_append_char(sb, ',');
        emit_value(sb, node->data.list.items[i]);
    }
    sb_append_char(sb, ']');
}

static void emit_value(StringBuilder *sb, AstNode *node) {
    switch (node->type) {
    case AST_STRING:
        emit_json_string(sb, node->data.string_val);
        break;
    case AST_NUMBER:
        sb_append(sb, node->data.number_str);
        break;
    case AST_BOOL:
        sb_append(sb, node->data.bool_val ? "true" : "false");
        break;
    case AST_NULL:
        sb_append(sb, "null");
        break;
    case AST_RECORD:
        emit_record(sb, node);
        break;
    case AST_LIST:
        emit_list(sb, node);
        break;
    }
}

char *json_emit(AstNode *root) {
    if (!root) return NULL;
    StringBuilder sb;
    sb_init(&sb);
    emit_value(&sb, root);
    sb_append_char(&sb, '\n');
    return sb_finish(&sb);
}
