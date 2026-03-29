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

/* --- Pretty-printed JSON emitter --- */

static void emit_pretty_value(StringBuilder *sb, AstNode *node, int depth);

static void emit_pretty_indent(StringBuilder *sb, int depth) {
    for (int i = 0; i < depth; i++) {
        sb_append(sb, "  ");
    }
}

static void emit_pretty_record(StringBuilder *sb, AstNode *node, int depth) {
    if (node->data.record.count == 0) {
        sb_append(sb, "{}");
        return;
    }
    sb_append(sb, "{\n");
    for (size_t i = 0; i < node->data.record.count; i++) {
        if (i > 0) sb_append(sb, ",\n");
        emit_pretty_indent(sb, depth + 1);
        emit_json_string(sb, node->data.record.pairs[i].key);
        sb_append(sb, ": ");
        emit_pretty_value(sb, node->data.record.pairs[i].value, depth + 1);
    }
    sb_append_char(sb, '\n');
    emit_pretty_indent(sb, depth);
    sb_append_char(sb, '}');
}

static void emit_pretty_list(StringBuilder *sb, AstNode *node, int depth) {
    if (node->data.list.count == 0) {
        sb_append(sb, "[]");
        return;
    }
    sb_append(sb, "[\n");
    for (size_t i = 0; i < node->data.list.count; i++) {
        if (i > 0) sb_append(sb, ",\n");
        emit_pretty_indent(sb, depth + 1);
        emit_pretty_value(sb, node->data.list.items[i], depth + 1);
    }
    sb_append_char(sb, '\n');
    emit_pretty_indent(sb, depth);
    sb_append_char(sb, ']');
}

static void emit_pretty_value(StringBuilder *sb, AstNode *node, int depth) {
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
        emit_pretty_record(sb, node, depth);
        break;
    case AST_LIST:
        emit_pretty_list(sb, node, depth);
        break;
    }
}

char *json_emit_pretty(AstNode *root) {
    if (!root) return NULL;
    StringBuilder sb;
    sb_init(&sb);
    emit_pretty_value(&sb, root, 0);
    sb_append_char(&sb, '\n');
    return sb_finish(&sb);
}
