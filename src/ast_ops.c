#include "ast_ops.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* ast_clone                                                          */
/* ------------------------------------------------------------------ */

AstNode *ast_clone(const AstNode *node) {
    if (!node) return NULL;
    switch (node->type) {
    case AST_STRING:
        return ast_new_string(node->data.string_val, node->line, node->col);
    case AST_NUMBER:
        return ast_new_number(node->data.number_str, node->line, node->col);
    case AST_BOOL:
        return ast_new_bool(node->data.bool_val, node->line, node->col);
    case AST_NULL:
        return ast_new_null(node->line, node->col);
    case AST_RECORD: {
        AstNode *rec = ast_new_record(node->line, node->col);
        for (size_t i = 0; i < node->data.record.count; i++) {
            AstNode *vc = ast_clone(node->data.record.pairs[i].value);
            ast_record_add(rec, node->data.record.pairs[i].key, vc);
        }
        return rec;
    }
    case AST_LIST: {
        AstNode *lst = ast_new_list(node->line, node->col);
        for (size_t i = 0; i < node->data.list.count; i++) {
            ast_list_add(lst, ast_clone(node->data.list.items[i]));
        }
        return lst;
    }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* ast_sort_keys                                                      */
/* ------------------------------------------------------------------ */

static int kv_compare(const void *a, const void *b) {
    const KeyValue *ka = (const KeyValue *)a;
    const KeyValue *kb = (const KeyValue *)b;
    return strcmp(ka->key, kb->key);
}

void ast_sort_keys(AstNode *node) {
    if (!node) return;
    switch (node->type) {
    case AST_RECORD:
        /* Sort children first, then this record */
        for (size_t i = 0; i < node->data.record.count; i++) {
            ast_sort_keys(node->data.record.pairs[i].value);
        }
        if (node->data.record.count > 1) {
            qsort(node->data.record.pairs, node->data.record.count,
                  sizeof(KeyValue), kv_compare);
        }
        break;
    case AST_LIST:
        for (size_t i = 0; i < node->data.list.count; i++) {
            ast_sort_keys(node->data.list.items[i]);
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* ast_merge                                                          */
/* ------------------------------------------------------------------ */

AstNode *ast_merge(const AstNode *base, const AstNode *overlay) {
    if (!base) return ast_clone(overlay);
    if (!overlay) return ast_clone(base);

    /* If both are records, deep-merge */
    if (base->type == AST_RECORD && overlay->type == AST_RECORD) {
        AstNode *result = ast_clone(base);

        for (size_t oi = 0; oi < overlay->data.record.count; oi++) {
            const char *okey = overlay->data.record.pairs[oi].key;
            const AstNode *oval = overlay->data.record.pairs[oi].value;
            int found = 0;

            for (size_t ri = 0; ri < result->data.record.count; ri++) {
                if (strcmp(result->data.record.pairs[ri].key, okey) == 0) {
                    /* Merge or replace this key */
                    AstNode *merged = ast_merge(result->data.record.pairs[ri].value, oval);
                    ast_free(result->data.record.pairs[ri].value);
                    result->data.record.pairs[ri].value = merged;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                ast_record_add(result, okey, ast_clone(oval));
            }
        }
        return result;
    }

    /* Otherwise overlay wins */
    return ast_clone(overlay);
}

/* ------------------------------------------------------------------ */
/* ast_query                                                          */
/* ------------------------------------------------------------------ */

const AstNode *ast_query(const AstNode *root, const char *path) {
    if (!root || !path || *path == '\0') return root;

    const AstNode *cur = root;
    const char *p = path;

    while (*p && cur) {
        /* Extract the next segment (split on '.') */
        const char *dot = strchr(p, '.');
        size_t seg_len = dot ? (size_t)(dot - p) : strlen(p);

        if (seg_len == 0) {
            p++;
            continue;
        }

        char segment[256];
        if (seg_len >= sizeof(segment)) seg_len = sizeof(segment) - 1;
        memcpy(segment, p, seg_len);
        segment[seg_len] = '\0';

        if (cur->type == AST_RECORD) {
            const AstNode *found = NULL;
            for (size_t i = 0; i < cur->data.record.count; i++) {
                if (strcmp(cur->data.record.pairs[i].key, segment) == 0) {
                    found = cur->data.record.pairs[i].value;
                    break;
                }
            }
            cur = found;
        } else if (cur->type == AST_LIST) {
            /* Try parsing as integer index */
            char *end;
            long idx = strtol(segment, &end, 10);
            if (*end != '\0' || idx < 0 || (size_t)idx >= cur->data.list.count) {
                return NULL;
            }
            cur = cur->data.list.items[idx];
        } else {
            return NULL;
        }

        p += seg_len;
        if (*p == '.') p++;
    }

    return cur;
}

/* ------------------------------------------------------------------ */
/* ast_diff                                                           */
/* ------------------------------------------------------------------ */

static int diff_nodes(const AstNode *a, const AstNode *b,
                      StringBuilder *path, FILE *out, int count);

static void path_push(StringBuilder *path, const char *seg) {
    if (path->len > 0) sb_append_char(path, '.');
    sb_append(path, seg);
}

static void path_push_index(StringBuilder *path, size_t idx) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%zu", idx);
    if (path->len > 0) sb_append_char(path, '.');
    sb_append(path, buf);
}

static const char *type_label(AstNodeType t) {
    switch (t) {
    case AST_STRING: return "string";
    case AST_NUMBER: return "number";
    case AST_BOOL:   return "bool";
    case AST_NULL:   return "null";
    case AST_RECORD: return "record";
    case AST_LIST:   return "list";
    }
    return "unknown";
}

static void print_value_short(const AstNode *n, FILE *out) {
    switch (n->type) {
    case AST_STRING: fprintf(out, "\"%s\"", n->data.string_val); break;
    case AST_NUMBER: fprintf(out, "%s", n->data.number_str); break;
    case AST_BOOL:   fprintf(out, "%s", n->data.bool_val ? "true" : "false"); break;
    case AST_NULL:   fprintf(out, "null"); break;
    case AST_RECORD: fprintf(out, "{...%zu keys}", n->data.record.count); break;
    case AST_LIST:   fprintf(out, "[...%zu items]", n->data.list.count); break;
    }
}

static int diff_nodes(const AstNode *a, const AstNode *b,
                      StringBuilder *path, FILE *out, int count) {
    if (a->type != b->type) {
        fprintf(out, "  %s: type %s -> %s\n",
                path->len ? path->data : "(root)", type_label(a->type), type_label(b->type));
        return count + 1;
    }

    switch (a->type) {
    case AST_STRING:
        if (strcmp(a->data.string_val, b->data.string_val) != 0) {
            fprintf(out, "  %s: \"%s\" -> \"%s\"\n",
                    path->len ? path->data : "(root)",
                    a->data.string_val, b->data.string_val);
            count++;
        }
        break;
    case AST_NUMBER:
        if (strcmp(a->data.number_str, b->data.number_str) != 0) {
            fprintf(out, "  %s: %s -> %s\n",
                    path->len ? path->data : "(root)",
                    a->data.number_str, b->data.number_str);
            count++;
        }
        break;
    case AST_BOOL:
        if (a->data.bool_val != b->data.bool_val) {
            fprintf(out, "  %s: %s -> %s\n",
                    path->len ? path->data : "(root)",
                    a->data.bool_val ? "true" : "false",
                    b->data.bool_val ? "true" : "false");
            count++;
        }
        break;
    case AST_NULL:
        break;
    case AST_RECORD: {
        size_t saved = path->len;
        /* Keys in a but not in b */
        for (size_t i = 0; i < a->data.record.count; i++) {
            const char *key = a->data.record.pairs[i].key;
            const AstNode *bval = NULL;
            for (size_t j = 0; j < b->data.record.count; j++) {
                if (strcmp(b->data.record.pairs[j].key, key) == 0) {
                    bval = b->data.record.pairs[j].value;
                    break;
                }
            }
            path->len = saved;
            path->data[saved] = '\0';
            path_push(path, key);
            if (!bval) {
                fprintf(out, "  %s: removed (was ", path->data);
                print_value_short(a->data.record.pairs[i].value, out);
                fprintf(out, ")\n");
                count++;
            } else {
                count = diff_nodes(a->data.record.pairs[i].value, bval, path, out, count);
            }
        }
        /* Keys in b but not in a */
        for (size_t j = 0; j < b->data.record.count; j++) {
            const char *key = b->data.record.pairs[j].key;
            int found = 0;
            for (size_t i = 0; i < a->data.record.count; i++) {
                if (strcmp(a->data.record.pairs[i].key, key) == 0) { found = 1; break; }
            }
            if (!found) {
                path->len = saved;
                path->data[saved] = '\0';
                path_push(path, key);
                fprintf(out, "  %s: added (", path->data);
                print_value_short(b->data.record.pairs[j].value, out);
                fprintf(out, ")\n");
                count++;
            }
        }
        path->len = saved;
        path->data[saved] = '\0';
        break;
    }
    case AST_LIST: {
        size_t saved = path->len;
        size_t max_len = a->data.list.count > b->data.list.count
                         ? a->data.list.count : b->data.list.count;
        for (size_t i = 0; i < max_len; i++) {
            path->len = saved;
            path->data[saved] = '\0';
            path_push_index(path, i);
            if (i >= a->data.list.count) {
                fprintf(out, "  %s: added (", path->data);
                print_value_short(b->data.list.items[i], out);
                fprintf(out, ")\n");
                count++;
            } else if (i >= b->data.list.count) {
                fprintf(out, "  %s: removed (was ", path->data);
                print_value_short(a->data.list.items[i], out);
                fprintf(out, ")\n");
                count++;
            } else {
                count = diff_nodes(a->data.list.items[i], b->data.list.items[i],
                                   path, out, count);
            }
        }
        path->len = saved;
        path->data[saved] = '\0';
        break;
    }
    }
    return count;
}

int ast_diff(const AstNode *a, const AstNode *b, FILE *out) {
    StringBuilder path;
    sb_init(&path);
    int count = diff_nodes(a, b, &path, out, 0);
    sb_free(&path);
    return count;
}

/* ------------------------------------------------------------------ */
/* ast_validate_schema                                                */
/* ------------------------------------------------------------------ */

static int validate_node(const AstNode *data, const AstNode *schema,
                         StringBuilder *path, FILE *out, int count);

static int validate_node(const AstNode *data, const AstNode *schema,
                         StringBuilder *path, FILE *out, int count) {
    if (!schema || !data) return count;
    const char *pstr = path->len ? path->data : "(root)";

    if (schema->type == AST_STRING) {
        /* Leaf type constraint */
        const char *expected = schema->data.string_val;
        if (strcmp(expected, "any") == 0) {
            return count; /* anything goes */
        }
        int ok = 0;
        if (strcmp(expected, "string") == 0) ok = (data->type == AST_STRING);
        else if (strcmp(expected, "number") == 0) ok = (data->type == AST_NUMBER);
        else if (strcmp(expected, "bool") == 0) ok = (data->type == AST_BOOL);
        else if (strcmp(expected, "null") == 0) ok = (data->type == AST_NULL);
        else if (strcmp(expected, "record") == 0) ok = (data->type == AST_RECORD);
        else if (strcmp(expected, "list") == 0) ok = (data->type == AST_LIST);
        else {
            /* Unknown constraint — treat as "any" */
            return count;
        }
        if (!ok) {
            fprintf(out, "  %s: expected %s, got %s\n", pstr, expected, type_label(data->type));
            count++;
        }
        return count;
    }

    if (schema->type == AST_RECORD) {
        if (data->type != AST_RECORD) {
            fprintf(out, "  %s: expected record, got %s\n", pstr, type_label(data->type));
            return count + 1;
        }
        size_t saved = path->len;
        /* Check each schema key exists in data and matches */
        for (size_t i = 0; i < schema->data.record.count; i++) {
            const char *key = schema->data.record.pairs[i].key;
            path->len = saved;
            path->data[saved] = '\0';
            path_push(path, key);

            const AstNode *dval = NULL;
            for (size_t j = 0; j < data->data.record.count; j++) {
                if (strcmp(data->data.record.pairs[j].key, key) == 0) {
                    dval = data->data.record.pairs[j].value;
                    break;
                }
            }
            if (!dval) {
                fprintf(out, "  %s: missing required key\n", path->data);
                count++;
            } else {
                count = validate_node(dval, schema->data.record.pairs[i].value,
                                      path, out, count);
            }
        }
        path->len = saved;
        path->data[saved] = '\0';
        return count;
    }

    if (schema->type == AST_LIST) {
        if (data->type != AST_LIST) {
            fprintf(out, "  %s: expected list, got %s\n", pstr, type_label(data->type));
            return count + 1;
        }
        /* If schema list has one item, validate all data items against it */
        if (schema->data.list.count == 1) {
            size_t saved = path->len;
            for (size_t i = 0; i < data->data.list.count; i++) {
                path->len = saved;
                path->data[saved] = '\0';
                path_push_index(path, i);
                count = validate_node(data->data.list.items[i],
                                      schema->data.list.items[0],
                                      path, out, count);
            }
            path->len = saved;
            path->data[saved] = '\0';
        }
        return count;
    }

    /* For other schema node types (number/bool/null), just accept anything */
    return count;
}

int ast_validate_schema(const AstNode *data, const AstNode *schema, FILE *out) {
    StringBuilder path;
    sb_init(&path);
    int count = validate_node(data, schema, &path, out, 0);
    sb_free(&path);
    return count;
}
