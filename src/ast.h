#ifndef MRON_AST_H
#define MRON_AST_H

#include <stddef.h>

typedef enum {
    AST_STRING,
    AST_NUMBER,
    AST_BOOL,
    AST_NULL,
    AST_RECORD,
    AST_LIST,
} AstNodeType;

typedef struct AstNode AstNode;

typedef struct {
    char *key;
    AstNode *value;
} KeyValue;

struct AstNode {
    AstNodeType type;
    int line;
    int col;
    union {
        char *string_val;   /* AST_STRING: decoded string content */
        char *number_str;   /* AST_NUMBER: clean number string (no _ or ,) */
        int bool_val;       /* AST_BOOL: 1=true, 0=false */
        struct {
            KeyValue *pairs;
            size_t count;
            size_t capacity;
        } record;           /* AST_RECORD */
        struct {
            AstNode **items;
            size_t count;
            size_t capacity;
        } list;             /* AST_LIST */
    } data;
};

AstNode *ast_new_string(const char *val, int line, int col);
AstNode *ast_new_number(const char *num_str, int line, int col);
AstNode *ast_new_bool(int val, int line, int col);
AstNode *ast_new_null(int line, int col);
AstNode *ast_new_record(int line, int col);
AstNode *ast_new_list(int line, int col);

void ast_record_add(AstNode *record, const char *key, AstNode *value);
void ast_list_add(AstNode *list, AstNode *item);

void ast_free(AstNode *node);

const char *ast_type_name(AstNodeType type);

#endif
