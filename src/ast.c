#include "ast.h"
#include "util.h"
#include <stdlib.h>
#include <stdio.h>

static AstNode *ast_alloc(AstNodeType type, int line, int col) {
    AstNode *node = calloc(1, sizeof(AstNode));
    if (!node) {
        fprintf(stderr, "error: out of memory\n");
        exit(1);
    }
    node->type = type;
    node->line = line;
    node->col = col;
    return node;
}

AstNode *ast_new_string(const char *val, int line, int col) {
    AstNode *node = ast_alloc(AST_STRING, line, col);
    node->data.string_val = mron_strdup(val);
    return node;
}

AstNode *ast_new_number(const char *num_str, int line, int col) {
    AstNode *node = ast_alloc(AST_NUMBER, line, col);
    node->data.number_str = mron_strdup(num_str);
    return node;
}

AstNode *ast_new_bool(int val, int line, int col) {
    AstNode *node = ast_alloc(AST_BOOL, line, col);
    node->data.bool_val = val;
    return node;
}

AstNode *ast_new_null(int line, int col) {
    return ast_alloc(AST_NULL, line, col);
}

AstNode *ast_new_record(int line, int col) {
    AstNode *node = ast_alloc(AST_RECORD, line, col);
    node->data.record.pairs = NULL;
    node->data.record.count = 0;
    node->data.record.capacity = 0;
    return node;
}

AstNode *ast_new_list(int line, int col) {
    AstNode *node = ast_alloc(AST_LIST, line, col);
    node->data.list.items = NULL;
    node->data.list.count = 0;
    node->data.list.capacity = 0;
    return node;
}

void ast_record_add(AstNode *record, const char *key, AstNode *value) {
    if (record->data.record.count >= record->data.record.capacity) {
        size_t new_cap = record->data.record.capacity == 0 ? 8 : record->data.record.capacity * 2;
        KeyValue *new_pairs = realloc(record->data.record.pairs, new_cap * sizeof(KeyValue));
        if (!new_pairs) {
            fprintf(stderr, "error: out of memory\n");
            exit(1);
        }
        record->data.record.pairs = new_pairs;
        record->data.record.capacity = new_cap;
    }
    record->data.record.pairs[record->data.record.count].key = mron_strdup(key);
    record->data.record.pairs[record->data.record.count].value = value;
    record->data.record.count++;
}

void ast_list_add(AstNode *list, AstNode *item) {
    if (list->data.list.count >= list->data.list.capacity) {
        size_t new_cap = list->data.list.capacity == 0 ? 8 : list->data.list.capacity * 2;
        AstNode **new_items = realloc(list->data.list.items, new_cap * sizeof(AstNode *));
        if (!new_items) {
            fprintf(stderr, "error: out of memory\n");
            exit(1);
        }
        list->data.list.items = new_items;
        list->data.list.capacity = new_cap;
    }
    list->data.list.items[list->data.list.count++] = item;
}

void ast_free(AstNode *node) {
    if (!node) return;
    switch (node->type) {
    case AST_STRING:
        free(node->data.string_val);
        break;
    case AST_NUMBER:
        free(node->data.number_str);
        break;
    case AST_BOOL:
    case AST_NULL:
        break;
    case AST_RECORD:
        for (size_t i = 0; i < node->data.record.count; i++) {
            free(node->data.record.pairs[i].key);
            ast_free(node->data.record.pairs[i].value);
        }
        free(node->data.record.pairs);
        break;
    case AST_LIST:
        for (size_t i = 0; i < node->data.list.count; i++) {
            ast_free(node->data.list.items[i]);
        }
        free(node->data.list.items);
        break;
    }
    free(node);
}

const char *ast_type_name(AstNodeType type) {
    switch (type) {
    case AST_STRING: return "string";
    case AST_NUMBER: return "number";
    case AST_BOOL:   return "boolean";
    case AST_NULL:   return "null";
    case AST_RECORD: return "record";
    case AST_LIST:   return "list";
    }
    return "unknown";
}
