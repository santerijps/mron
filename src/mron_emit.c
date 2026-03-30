#include "mron_emit.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

static void emit_value(StringBuilder *sb, AstNode *node, int depth, int inline_mode);
static void emit_record_pairs(StringBuilder *sb, AstNode *node, int depth);

static void emit_mron_string(StringBuilder *sb, const char *str) {
    sb_append_char(sb, '"');
    for (const char *p = str; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
        case '"':  sb_append(sb, "\\\""); break;
        case '\\': sb_append(sb, "\\\\"); break;
        case '\n': sb_append(sb, "\\n"); break;
        case '\t': sb_append(sb, "\\t"); break;
        case '\r': sb_append(sb, "\\r"); break;
        case '\b': sb_append(sb, "\\b"); break;
        case '\f': sb_append(sb, "\\f"); break;
        default:
            sb_append_char(sb, (char)c);
            break;
        }
    }
    sb_append_char(sb, '"');
}

/* Check if a list can be emitted in CSV-style:
   all items are records with identical key sets and only primitive values. */
static int can_emit_csv(AstNode *list) {
    if (list->data.list.count == 0) return 0;

    for (size_t i = 0; i < list->data.list.count; i++) {
        if (list->data.list.items[i]->type != AST_RECORD) return 0;
    }

    AstNode *first = list->data.list.items[0];
    if (first->data.record.count == 0) return 0;

    for (size_t i = 1; i < list->data.list.count; i++) {
        AstNode *item = list->data.list.items[i];
        if (item->data.record.count != first->data.record.count) return 0;
        for (size_t j = 0; j < first->data.record.count; j++) {
            if (strcmp(item->data.record.pairs[j].key,
                       first->data.record.pairs[j].key) != 0) {
                return 0;
            }
        }
    }

    /* Check all values are primitive */
    for (size_t i = 0; i < list->data.list.count; i++) {
        AstNode *item = list->data.list.items[i];
        for (size_t j = 0; j < item->data.record.count; j++) {
            AstNodeType t = item->data.record.pairs[j].value->type;
            if (t == AST_RECORD || t == AST_LIST) return 0;
        }
    }

    return 1;
}

/* Check if a list contains only simple (non-composite) values */
static int is_simple_list(AstNode *list) {
    for (size_t i = 0; i < list->data.list.count; i++) {
        AstNodeType t = list->data.list.items[i]->type;
        if (t == AST_RECORD || t == AST_LIST) return 0;
    }
    return 1;
}

static void emit_value(StringBuilder *sb, AstNode *node, int depth, int inline_mode) {
    switch (node->type) {
    case AST_STRING:
        emit_mron_string(sb, node->data.string_val);
        break;
    case AST_NUMBER:
        sb_append(sb, node->data.number_str);
        break;
    case AST_BOOL:
        sb_append(sb, node->data.bool_val ? "yes" : "no");
        break;
    case AST_NULL:
        sb_append(sb, "null");
        break;
    case AST_RECORD:
        if (node->data.record.count == 0) {
            sb_append(sb, "{}");
        } else {
            sb_append(sb, "{\n");
            emit_record_pairs(sb, node, depth + 1);
            sb_append_indent(sb, depth);
            sb_append_char(sb, '}');
        }
        break;
    case AST_LIST:
        if (node->data.list.count == 0) {
            sb_append(sb, "[]");
        } else if (inline_mode || is_simple_list(node)) {
            /* Inline list: [ val1 val2 val3 ] */
            sb_append(sb, "[ ");
            for (size_t i = 0; i < node->data.list.count; i++) {
                if (i > 0) sb_append_char(sb, ' ');
                emit_value(sb, node->data.list.items[i], depth, 1);
            }
            sb_append(sb, " ]");
        } else {
            /* Multi-line list */
            sb_append(sb, "[\n");
            for (size_t i = 0; i < node->data.list.count; i++) {
                sb_append_indent(sb, depth + 1);
                emit_value(sb, node->data.list.items[i], depth + 1, 0);
                sb_append_char(sb, '\n');
            }
            sb_append_indent(sb, depth);
            sb_append_char(sb, ']');
        }
        break;
    }
}

/* Emit a CSV-style list */
static void emit_csv_list(StringBuilder *sb, AstNode *list, int depth) {
    AstNode *first = list->data.list.items[0];

    /* Emit header: (key1 key2 ...) */
    sb_append_char(sb, '(');
    for (size_t j = 0; j < first->data.record.count; j++) {
        if (j > 0) sb_append_char(sb, ' ');
        sb_append(sb, first->data.record.pairs[j].key);
    }
    sb_append(sb, ") [\n");

    /* Emit rows */
    for (size_t i = 0; i < list->data.list.count; i++) {
        sb_append_indent(sb, depth + 1);
        AstNode *record = list->data.list.items[i];
        for (size_t j = 0; j < record->data.record.count; j++) {
            if (j > 0) sb_append_char(sb, ' ');
            emit_value(sb, record->data.record.pairs[j].value, depth + 1, 1);
        }
        sb_append_char(sb, '\n');
    }

    sb_append_indent(sb, depth);
    sb_append_char(sb, ']');
}

static void emit_record_pairs(StringBuilder *sb, AstNode *node, int depth) {
    /* Calculate max key length for alignment within this record */
    size_t max_key_len = 0;
    for (size_t i = 0; i < node->data.record.count; i++) {
        size_t klen = strlen(node->data.record.pairs[i].key);
        if (klen > max_key_len) max_key_len = klen;
    }

    for (size_t i = 0; i < node->data.record.count; i++) {
        KeyValue *kv = &node->data.record.pairs[i];
        sb_append_indent(sb, depth);
        sb_append(sb, kv->key);

        /* Check for CSV-style list */
        if (kv->value->type == AST_LIST && can_emit_csv(kv->value)) {
            sb_append_char(sb, ' ');
            emit_csv_list(sb, kv->value, depth);
        } else {
            /* Pad key for alignment */
            size_t klen = strlen(kv->key);
            for (size_t p = klen; p < max_key_len; p++) {
                sb_append_char(sb, ' ');
            }
            sb_append_char(sb, ' ');
            emit_value(sb, kv->value, depth, 0);
        }
        sb_append_char(sb, '\n');
    }
}

char *mron_emit(AstNode *root) {
    if (!root) return NULL;

    StringBuilder sb;
    sb_init(&sb);

    if (root->type == AST_RECORD) {
        emit_record_pairs(&sb, root, 0);
    } else {
        /* Top-level non-record (shouldn't normally happen in MRON) */
        emit_value(&sb, root, 0, 0);
        sb_append_char(&sb, '\n');
    }

    return sb_finish(&sb);
}

/* ------------------------------------------------------------------ */
/* Custom-indent MRON emitter                                         */
/* ------------------------------------------------------------------ */

static int g_mron_indent_width = 4;

static void ci_emit_value(StringBuilder *sb, AstNode *node, int depth, int inline_mode);
static void ci_emit_record_pairs(StringBuilder *sb, AstNode *node, int depth);

static void ci_emit_indent(StringBuilder *sb, int depth) {
    int total = depth * g_mron_indent_width;
    for (int i = 0; i < total; i++) sb_append_char(sb, ' ');
}

static void ci_emit_value(StringBuilder *sb, AstNode *node, int depth, int inline_mode) {
    switch (node->type) {
    case AST_STRING: emit_mron_string(sb, node->data.string_val); break;
    case AST_NUMBER: sb_append(sb, node->data.number_str); break;
    case AST_BOOL:   sb_append(sb, node->data.bool_val ? "yes" : "no"); break;
    case AST_NULL:   sb_append(sb, "null"); break;
    case AST_RECORD:
        if (node->data.record.count == 0) {
            sb_append(sb, "{}");
        } else {
            sb_append(sb, "{\n");
            ci_emit_record_pairs(sb, node, depth + 1);
            ci_emit_indent(sb, depth);
            sb_append_char(sb, '}');
        }
        break;
    case AST_LIST:
        if (node->data.list.count == 0) {
            sb_append(sb, "[]");
        } else if (inline_mode || is_simple_list(node)) {
            sb_append(sb, "[ ");
            for (size_t i = 0; i < node->data.list.count; i++) {
                if (i > 0) sb_append_char(sb, ' ');
                ci_emit_value(sb, node->data.list.items[i], depth, 1);
            }
            sb_append(sb, " ]");
        } else {
            sb_append(sb, "[\n");
            for (size_t i = 0; i < node->data.list.count; i++) {
                ci_emit_indent(sb, depth + 1);
                ci_emit_value(sb, node->data.list.items[i], depth + 1, 0);
                sb_append_char(sb, '\n');
            }
            ci_emit_indent(sb, depth);
            sb_append_char(sb, ']');
        }
        break;
    }
}

static void ci_emit_record_pairs(StringBuilder *sb, AstNode *node, int depth) {
    size_t max_key_len = 0;
    for (size_t i = 0; i < node->data.record.count; i++) {
        size_t klen = strlen(node->data.record.pairs[i].key);
        if (klen > max_key_len) max_key_len = klen;
    }
    for (size_t i = 0; i < node->data.record.count; i++) {
        KeyValue *kv = &node->data.record.pairs[i];
        ci_emit_indent(sb, depth);
        sb_append(sb, kv->key);
        if (kv->value->type == AST_LIST && can_emit_csv(kv->value)) {
            sb_append_char(sb, ' ');
            /* CSV header */
            AstNode *first = kv->value->data.list.items[0];
            sb_append_char(sb, '(');
            for (size_t j = 0; j < first->data.record.count; j++) {
                if (j > 0) sb_append_char(sb, ' ');
                sb_append(sb, first->data.record.pairs[j].key);
            }
            sb_append(sb, ") [\n");
            for (size_t r = 0; r < kv->value->data.list.count; r++) {
                ci_emit_indent(sb, depth + 1);
                AstNode *rec = kv->value->data.list.items[r];
                for (size_t j = 0; j < rec->data.record.count; j++) {
                    if (j > 0) sb_append_char(sb, ' ');
                    ci_emit_value(sb, rec->data.record.pairs[j].value, depth + 1, 1);
                }
                sb_append_char(sb, '\n');
            }
            ci_emit_indent(sb, depth);
            sb_append_char(sb, ']');
        } else {
            size_t klen = strlen(kv->key);
            for (size_t p = klen; p < max_key_len; p++) sb_append_char(sb, ' ');
            sb_append_char(sb, ' ');
            ci_emit_value(sb, kv->value, depth, 0);
        }
        sb_append_char(sb, '\n');
    }
}

char *mron_emit_indent(AstNode *root, int indent_width) {
    if (!root) return NULL;
    g_mron_indent_width = indent_width;
    StringBuilder sb;
    sb_init(&sb);
    if (root->type == AST_RECORD) {
        ci_emit_record_pairs(&sb, root, 0);
    } else {
        ci_emit_value(&sb, root, 0, 0);
        sb_append_char(&sb, '\n');
    }
    return sb_finish(&sb);
}

/* ------------------------------------------------------------------ */
/* Minified MRON emitter                                              */
/* ------------------------------------------------------------------ */

static void min_emit_value(StringBuilder *sb, AstNode *node);

static void min_emit_value(StringBuilder *sb, AstNode *node) {
    switch (node->type) {
    case AST_STRING: emit_mron_string(sb, node->data.string_val); break;
    case AST_NUMBER: sb_append(sb, node->data.number_str); break;
    case AST_BOOL:   sb_append(sb, node->data.bool_val ? "yes" : "no"); break;
    case AST_NULL:   sb_append(sb, "null"); break;
    case AST_RECORD:
        if (node->data.record.count == 0) {
            sb_append(sb, "{}");
        } else {
            sb_append(sb, "{");
            for (size_t i = 0; i < node->data.record.count; i++) {
                if (i > 0) sb_append_char(sb, ' ');
                sb_append(sb, node->data.record.pairs[i].key);
                sb_append_char(sb, ' ');
                min_emit_value(sb, node->data.record.pairs[i].value);
            }
            sb_append(sb, "}");
        }
        break;
    case AST_LIST:
        sb_append(sb, "[");
        for (size_t i = 0; i < node->data.list.count; i++) {
            if (i > 0) sb_append_char(sb, ' ');
            min_emit_value(sb, node->data.list.items[i]);
        }
        sb_append(sb, "]");
        break;
    }
}

char *mron_emit_minify(AstNode *root) {
    if (!root) return NULL;
    StringBuilder sb;
    sb_init(&sb);
    if (root->type == AST_RECORD) {
        for (size_t i = 0; i < root->data.record.count; i++) {
            if (i > 0) sb_append_char(&sb, '\n');
            sb_append(&sb, root->data.record.pairs[i].key);
            sb_append_char(&sb, ' ');
            min_emit_value(&sb, root->data.record.pairs[i].value);
        }
        sb_append_char(&sb, '\n');
    } else {
        min_emit_value(&sb, root);
        sb_append_char(&sb, '\n');
    }
    return sb_finish(&sb);
}
