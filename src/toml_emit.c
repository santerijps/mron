#include "toml_emit.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

static void emit_toml_string(StringBuilder *sb, const char *str) {
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
                snprintf(buf, sizeof(buf), "\\u%04X", c);
                sb_append(sb, buf);
            } else {
                sb_append_char(sb, (char)c);
            }
            break;
        }
    }
    sb_append_char(sb, '"');
}

/* Check if a value is a primitive (not record or list) */
static int is_primitive(const AstNode *node) {
    return node->type != AST_RECORD && node->type != AST_LIST;
}

/* Check if a list contains only primitives (inline-able as a TOML array) */
static int is_primitive_list(const AstNode *node) {
    if (node->type != AST_LIST) return 0;
    for (size_t i = 0; i < node->data.list.count; i++) {
        if (!is_primitive(node->data.list.items[i])) return 0;
    }
    return 1;
}

/* Check if a list is an array of tables */
static int is_table_array(const AstNode *node) {
    if (node->type != AST_LIST || node->data.list.count == 0) return 0;
    for (size_t i = 0; i < node->data.list.count; i++) {
        if (node->data.list.items[i]->type != AST_RECORD) return 0;
    }
    return 1;
}

static void emit_inline_value(StringBuilder *sb, AstNode *node) {
    switch (node->type) {
    case AST_STRING:
        emit_toml_string(sb, node->data.string_val);
        break;
    case AST_NUMBER:
        sb_append(sb, node->data.number_str);
        break;
    case AST_BOOL:
        sb_append(sb, node->data.bool_val ? "true" : "false");
        break;
    case AST_NULL:
        /* TOML has no null; emit empty string as best approximation */
        sb_append(sb, "\"\"");
        break;
    case AST_RECORD: {
        sb_append(sb, "{ ");
        for (size_t i = 0; i < node->data.record.count; i++) {
            if (i > 0) sb_append(sb, ", ");
            sb_append(sb, node->data.record.pairs[i].key);
            sb_append(sb, " = ");
            emit_inline_value(sb, node->data.record.pairs[i].value);
        }
        sb_append(sb, " }");
        break;
    }
    case AST_LIST: {
        sb_append(sb, "[ ");
        for (size_t i = 0; i < node->data.list.count; i++) {
            if (i > 0) sb_append(sb, ", ");
            emit_inline_value(sb, node->data.list.items[i]);
        }
        sb_append(sb, " ]");
        break;
    }
    }
}

static void emit_table(StringBuilder *sb, AstNode *node,
                       const char *prefix, int is_root);

static void emit_table(StringBuilder *sb, AstNode *node,
                       const char *prefix, int is_root) {
    (void)is_root;
    if (node->type != AST_RECORD) return;

    /* First pass: emit simple key-value pairs */
    for (size_t i = 0; i < node->data.record.count; i++) {
        AstNode *val = node->data.record.pairs[i].value;
        const char *key = node->data.record.pairs[i].key;
        if (is_primitive(val) || is_primitive_list(val)) {
            sb_append(sb, key);
            sb_append(sb, " = ");
            if (val->type == AST_LIST) {
                sb_append(sb, "[ ");
                for (size_t j = 0; j < val->data.list.count; j++) {
                    if (j > 0) sb_append(sb, ", ");
                    emit_inline_value(sb, val->data.list.items[j]);
                }
                sb_append(sb, " ]\n");
            } else {
                emit_inline_value(sb, val);
                sb_append_char(sb, '\n');
            }
        }
    }

    /* Second pass: emit sub-tables */
    for (size_t i = 0; i < node->data.record.count; i++) {
        AstNode *val = node->data.record.pairs[i].value;
        const char *key = node->data.record.pairs[i].key;

        if (val->type == AST_RECORD) {
            sb_append_char(sb, '\n');
            /* Build path */
            char path[1024];
            if (prefix[0]) {
                snprintf(path, sizeof(path), "%s.%s", prefix, key);
            } else {
                snprintf(path, sizeof(path), "%s", key);
            }
            sb_append(sb, "[");
            sb_append(sb, path);
            sb_append(sb, "]\n");
            emit_table(sb, val, path, 0);
        } else if (val->type == AST_LIST && is_table_array(val)) {
            char path[1024];
            if (prefix[0]) {
                snprintf(path, sizeof(path), "%s.%s", prefix, key);
            } else {
                snprintf(path, sizeof(path), "%s", key);
            }
            for (size_t j = 0; j < val->data.list.count; j++) {
                sb_append_char(sb, '\n');
                sb_append(sb, "[[");
                sb_append(sb, path);
                sb_append(sb, "]]\n");
                emit_table(sb, val->data.list.items[j], path, 0);
            }
        } else if (val->type == AST_LIST && !is_primitive_list(val)) {
            /* Mixed list: emit inline */
            sb_append(sb, key);
            sb_append(sb, " = ");
            emit_inline_value(sb, val);
            sb_append_char(sb, '\n');
        }
    }
}

char *toml_emit(AstNode *root) {
    if (!root) return NULL;
    if (root->type != AST_RECORD) {
        /* TOML requires a top-level table */
        return NULL;
    }
    StringBuilder sb;
    sb_init(&sb);
    emit_table(&sb, root, "", 1);
    return sb_finish(&sb);
}
