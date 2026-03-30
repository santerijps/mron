#include "yaml_emit.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void emit_yaml_value(StringBuilder *sb, AstNode *node, int depth);

/* Check if a string needs quoting in YAML */
static int yaml_needs_quoting(const char *s) {
    if (!s || !*s) return 1;
    /* Strings that look like YAML special values */
    if (strcmp(s, "true") == 0 || strcmp(s, "false") == 0 ||
        strcmp(s, "yes") == 0 || strcmp(s, "no") == 0 ||
        strcmp(s, "null") == 0 || strcmp(s, "~") == 0 ||
        strcmp(s, "on") == 0 || strcmp(s, "off") == 0) {
        return 1;
    }
    /* Strings starting with special chars */
    if (s[0] == '{' || s[0] == '[' || s[0] == '*' || s[0] == '&' ||
        s[0] == '!' || s[0] == '|' || s[0] == '>' || s[0] == '\'' ||
        s[0] == '"' || s[0] == '%' || s[0] == '@' || s[0] == '#' ||
        s[0] == ',' || s[0] == '-' || s[0] == ':' || s[0] == '?') {
        return 1;
    }
    /* Contains special characters */
    for (const char *p = s; *p; p++) {
        if (*p == ':' || *p == '#' || *p == '\n' || *p == '\r' || *p == '\t') {
            return 1;
        }
    }
    /* Looks like a number */
    char *end;
    (void)strtod(s, &end);
    if (*end == '\0') return 1;
    return 0;
}

static void emit_yaml_string(StringBuilder *sb, const char *str) {
    if (yaml_needs_quoting(str)) {
        sb_append_char(sb, '"');
        for (const char *p = str; *p; p++) {
            unsigned char c = (unsigned char)*p;
            switch (c) {
            case '"':  sb_append(sb, "\\\""); break;
            case '\\': sb_append(sb, "\\\\"); break;
            case '\n': sb_append(sb, "\\n"); break;
            case '\r': sb_append(sb, "\\r"); break;
            case '\t': sb_append(sb, "\\t"); break;
            default:   sb_append_char(sb, (char)c); break;
            }
        }
        sb_append_char(sb, '"');
    } else {
        sb_append(sb, str);
    }
}

static void emit_yaml_indent(StringBuilder *sb, int depth) {
    for (int i = 0; i < depth * 2; i++) {
        sb_append_char(sb, ' ');
    }
}

static void emit_yaml_record(StringBuilder *sb, AstNode *node, int depth) {
    for (size_t i = 0; i < node->data.record.count; i++) {
        if (i > 0 || depth > 0) emit_yaml_indent(sb, depth);
        sb_append(sb, node->data.record.pairs[i].key);
        sb_append(sb, ":");
        AstNode *val = node->data.record.pairs[i].value;
        if (val->type == AST_RECORD && val->data.record.count > 0) {
            sb_append_char(sb, '\n');
            emit_yaml_record(sb, val, depth + 1);
        } else if (val->type == AST_LIST && val->data.list.count > 0) {
            sb_append_char(sb, '\n');
            emit_yaml_value(sb, val, depth + 1);
        } else {
            sb_append_char(sb, ' ');
            emit_yaml_value(sb, val, depth + 1);
            sb_append_char(sb, '\n');
        }
    }
}

static void emit_yaml_value(StringBuilder *sb, AstNode *node, int depth) {
    switch (node->type) {
    case AST_STRING:
        emit_yaml_string(sb, node->data.string_val);
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
        if (node->data.record.count == 0) {
            sb_append(sb, "{}");
        } else {
            emit_yaml_record(sb, node, depth);
        }
        break;
    case AST_LIST:
        if (node->data.list.count == 0) {
            sb_append(sb, "[]");
        } else {
            for (size_t i = 0; i < node->data.list.count; i++) {
                emit_yaml_indent(sb, depth);
                sb_append(sb, "- ");
                AstNode *item = node->data.list.items[i];
                if (item->type == AST_RECORD && item->data.record.count > 0) {
                    /* First key on same line as dash */
                    sb_append(sb, item->data.record.pairs[0].key);
                    sb_append(sb, ":");
                    AstNode *v0 = item->data.record.pairs[0].value;
                    if (v0->type == AST_RECORD || v0->type == AST_LIST) {
                        sb_append_char(sb, '\n');
                        emit_yaml_value(sb, v0, depth + 2);
                    } else {
                        sb_append_char(sb, ' ');
                        emit_yaml_value(sb, v0, depth + 2);
                        sb_append_char(sb, '\n');
                    }
                    /* Remaining keys indented under the dash */
                    for (size_t j = 1; j < item->data.record.count; j++) {
                        emit_yaml_indent(sb, depth + 1);
                        sb_append(sb, item->data.record.pairs[j].key);
                        sb_append(sb, ":");
                        AstNode *vj = item->data.record.pairs[j].value;
                        if (vj->type == AST_RECORD || vj->type == AST_LIST) {
                            sb_append_char(sb, '\n');
                            emit_yaml_value(sb, vj, depth + 2);
                        } else {
                            sb_append_char(sb, ' ');
                            emit_yaml_value(sb, vj, depth + 2);
                            sb_append_char(sb, '\n');
                        }
                    }
                } else {
                    emit_yaml_value(sb, item, depth + 1);
                    sb_append_char(sb, '\n');
                }
            }
        }
        break;
    }
}

char *yaml_emit(AstNode *root) {
    if (!root) return NULL;
    StringBuilder sb;
    sb_init(&sb);
    if (root->type == AST_RECORD) {
        emit_yaml_record(&sb, root, 0);
    } else {
        emit_yaml_value(&sb, root, 0);
        sb_append_char(&sb, '\n');
    }
    return sb_finish(&sb);
}
