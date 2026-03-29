#include "parser.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    TokenArray *tokens;
    size_t pos;
} Parser;

static Token *peek(Parser *P) {
    return &P->tokens->tokens[P->pos];
}

static Token *advance(Parser *P) {
    Token *tok = &P->tokens->tokens[P->pos];
    if (tok->type != TOKEN_EOF) {
        P->pos++;
    }
    return tok;
}

static int check(Parser *P, TokenType type) {
    return peek(P)->type == type;
}

static Token *expect(Parser *P, TokenType type) {
    Token *tok = peek(P);
    if (tok->type != type) {
        mron_error(P->tokens->filename, tok->line, tok->col,
                   "expected %s, got %s", token_type_name(type), token_type_name(tok->type));
        return NULL;
    }
    return advance(P);
}

static AstNode *parse_value(Parser *P);

static AstNode *parse_list(Parser *P) {
    Token *open = expect(P, TOKEN_LBRACKET);
    if (!open) return NULL;

    AstNode *list = ast_new_list(open->line, open->col);

    while (!check(P, TOKEN_RBRACKET) && !check(P, TOKEN_EOF)) {
        AstNode *val = parse_value(P);
        if (!val) {
            ast_free(list);
            return NULL;
        }
        ast_list_add(list, val);
    }

    if (!expect(P, TOKEN_RBRACKET)) {
        ast_free(list);
        return NULL;
    }
    return list;
}

/* Parse CSV-style list: header already signals ( key1 key2 ... ) [ val val ... ] */
static AstNode *parse_csv_list(Parser *P) {
    Token *open_paren = expect(P, TOKEN_LPAREN);
    if (!open_paren) return NULL;

    /* Collect header keys */
    char **headers = NULL;
    size_t header_count = 0;
    size_t header_cap = 0;

    while (!check(P, TOKEN_RPAREN) && !check(P, TOKEN_EOF)) {
        Token *key_tok = expect(P, TOKEN_IDENT);
        if (!key_tok) {
            for (size_t i = 0; i < header_count; i++) free(headers[i]);
            free(headers);
            return NULL;
        }
        if (header_count >= header_cap) {
            header_cap = header_cap == 0 ? 8 : header_cap * 2;
            headers = realloc(headers, header_cap * sizeof(char *));
            if (!headers) { fprintf(stderr, "error: out of memory\n"); exit(1); }
        }
        headers[header_count++] = mron_strdup(key_tok->value);
    }

    if (!expect(P, TOKEN_RPAREN)) {
        for (size_t i = 0; i < header_count; i++) free(headers[i]);
        free(headers);
        return NULL;
    }

    if (header_count == 0) {
        mron_error(P->tokens->filename, open_paren->line, open_paren->col,
                   "CSV-style header must contain at least one key");
        free(headers);
        return NULL;
    }

    /* Parse the list body */
    Token *open_bracket = expect(P, TOKEN_LBRACKET);
    if (!open_bracket) {
        for (size_t i = 0; i < header_count; i++) free(headers[i]);
        free(headers);
        return NULL;
    }

    /* Collect all values */
    AstNode **values = NULL;
    size_t value_count = 0;
    size_t value_cap = 0;

    while (!check(P, TOKEN_RBRACKET) && !check(P, TOKEN_EOF)) {
        AstNode *val = parse_value(P);
        if (!val) {
            for (size_t i = 0; i < value_count; i++) ast_free(values[i]);
            free(values);
            for (size_t i = 0; i < header_count; i++) free(headers[i]);
            free(headers);
            return NULL;
        }
        if (value_count >= value_cap) {
            value_cap = value_cap == 0 ? 16 : value_cap * 2;
            values = realloc(values, value_cap * sizeof(AstNode *));
            if (!values) { fprintf(stderr, "error: out of memory\n"); exit(1); }
        }
        values[value_count++] = val;
    }

    Token *close_bracket = expect(P, TOKEN_RBRACKET);
    if (!close_bracket) {
        for (size_t i = 0; i < value_count; i++) ast_free(values[i]);
        free(values);
        for (size_t i = 0; i < header_count; i++) free(headers[i]);
        free(headers);
        return NULL;
    }

    /* Validate that total values is a multiple of header count */
    if (value_count % header_count != 0) {
        mron_error(P->tokens->filename, open_bracket->line, open_bracket->col,
                   "CSV-style list has %zu values but header has %zu keys "
                   "(values must be a multiple of key count)",
                   value_count, header_count);
        for (size_t i = 0; i < value_count; i++) ast_free(values[i]);
        free(values);
        for (size_t i = 0; i < header_count; i++) free(headers[i]);
        free(headers);
        return NULL;
    }

    /* Group values into records */
    AstNode *list = ast_new_list(open_bracket->line, open_bracket->col);
    size_t row_count = value_count / header_count;

    for (size_t r = 0; r < row_count; r++) {
        AstNode *record = ast_new_record(values[r * header_count]->line,
                                         values[r * header_count]->col);
        for (size_t c = 0; c < header_count; c++) {
            ast_record_add(record, headers[c], values[r * header_count + c]);
        }
        ast_list_add(list, record);
    }

    /* Cleanup (values are now owned by the records, don't free them) */
    free(values);
    for (size_t i = 0; i < header_count; i++) free(headers[i]);
    free(headers);

    return list;
}

static AstNode *parse_record_body(Parser *P, TokenType end_token);

static AstNode *parse_record(Parser *P) {
    Token *open = expect(P, TOKEN_LBRACE);
    if (!open) return NULL;

    AstNode *record = ast_new_record(open->line, open->col);

    AstNode *body = parse_record_body(P, TOKEN_RBRACE);
    if (!body) {
        ast_free(record);
        return NULL;
    }

    /* Transfer pairs from body to record */
    ast_free(record);
    record = body;

    if (!expect(P, TOKEN_RBRACE)) {
        ast_free(record);
        return NULL;
    }

    return record;
}

static AstNode *parse_value(Parser *P) {
    Token *tok = peek(P);

    switch (tok->type) {
    case TOKEN_STRING: {
        advance(P);
        return ast_new_string(tok->value, tok->line, tok->col);
    }
    case TOKEN_NUMBER: {
        advance(P);
        return ast_new_number(tok->value, tok->line, tok->col);
    }
    case TOKEN_IDENT: {
        /* Check for boolean/null keywords */
        if (strcmp(tok->value, "true") == 0 || strcmp(tok->value, "yes") == 0) {
            advance(P);
            return ast_new_bool(1, tok->line, tok->col);
        }
        if (strcmp(tok->value, "false") == 0 || strcmp(tok->value, "no") == 0) {
            advance(P);
            return ast_new_bool(0, tok->line, tok->col);
        }
        if (strcmp(tok->value, "null") == 0) {
            advance(P);
            return ast_new_null(tok->line, tok->col);
        }
        mron_error(P->tokens->filename, tok->line, tok->col,
                   "unexpected identifier '%s' in value position "
                   "(expected a string, number, boolean, record, or list)",
                   tok->value);
        return NULL;
    }
    case TOKEN_LBRACE:
        return parse_record(P);
    case TOKEN_LBRACKET:
        return parse_list(P);
    default:
        mron_error(P->tokens->filename, tok->line, tok->col,
                   "expected a value, got %s", token_type_name(tok->type));
        return NULL;
    }
}

/* Parse key-value pairs until end_token is reached.
   Returns a record node containing all pairs. */
static AstNode *parse_record_body(Parser *P, TokenType end_token) {
    AstNode *record = ast_new_record(peek(P)->line, peek(P)->col);

    while (!check(P, end_token) && !check(P, TOKEN_EOF)) {
        /* Expect a key (identifier) */
        Token *key_tok = peek(P);
        if (key_tok->type != TOKEN_IDENT) {
            mron_error(P->tokens->filename, key_tok->line, key_tok->col,
                       "expected a key (identifier), got %s",
                       token_type_name(key_tok->type));
            ast_free(record);
            return NULL;
        }
        advance(P);
        const char *key = key_tok->value;

        /* Check if next token is '(' for CSV-style list */
        AstNode *value;
        if (check(P, TOKEN_LPAREN)) {
            value = parse_csv_list(P);
        } else {
            value = parse_value(P);
        }

        if (!value) {
            ast_free(record);
            return NULL;
        }

        ast_record_add(record, key, value);
    }

    return record;
}

AstNode *parser_parse(TokenArray *tokens) {
    if (!tokens || tokens->has_error) {
        return NULL;
    }

    Parser P;
    P.tokens = tokens;
    P.pos = 0;

    AstNode *root = parse_record_body(&P, TOKEN_EOF);
    if (!root) return NULL;

    if (!check(&P, TOKEN_EOF)) {
        Token *tok = peek(&P);
        mron_error(tokens->filename, tok->line, tok->col,
                   "unexpected token %s after top-level content",
                   token_type_name(tok->type));
        ast_free(root);
        return NULL;
    }

    return root;
}
