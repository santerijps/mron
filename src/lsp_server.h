#ifndef MRON_LSP_SERVER_H
#define MRON_LSP_SERVER_H

#include "ast.h"
#include "lexer.h"

/* ------------------------------------------------------------------ */
/* Document store                                                      */
/* ------------------------------------------------------------------ */
typedef struct {
    char *uri;
    char *content;
    int version;
    TokenArray *tokens;    /* lexer output (kept for positional features) */
    AstNode *ast;          /* parsed AST (NULL if parse failed) */
} LspDocument;

#define LSP_MAX_DOCS 128

/* ------------------------------------------------------------------ */
/* CSV block info (for MRON-specific features)                         */
/* ------------------------------------------------------------------ */
typedef struct {
    int key_idx;            /* token index of the key identifier */
    int paren_open;         /* token index of LPAREN */
    int paren_close;        /* token index of RPAREN */
    int bracket_open;       /* token index of LBRACKET */
    int bracket_close;      /* token index of RBRACKET */
    char **columns;         /* column names */
    int col_count;
    int *value_indices;     /* token indices of each value in the body */
    int value_count;
} CsvBlockInfo;

/* ------------------------------------------------------------------ */
/* Server lifecycle                                                    */
/* ------------------------------------------------------------------ */
void lsp_server_init(void);
void lsp_server_shutdown(void);

/* Handle a JSON-RPC message. Returns 1 if the server should exit. */
int lsp_handle_message(const char *json);

/* ------------------------------------------------------------------ */
/* Document management                                                 */
/* ------------------------------------------------------------------ */
LspDocument *lsp_doc_open(const char *uri, const char *content, int version);
LspDocument *lsp_doc_find(const char *uri);
void lsp_doc_update(LspDocument *doc, const char *content, int version);
void lsp_doc_close(const char *uri);
void lsp_doc_reparse(LspDocument *doc);

/* ------------------------------------------------------------------ */
/* Feature: publish diagnostics                                        */
/* ------------------------------------------------------------------ */
void lsp_publish_diagnostics(LspDocument *doc);

/* ------------------------------------------------------------------ */
/* Helper: navigate parsed JSON (AstNode from json_parse)              */
/* ------------------------------------------------------------------ */
const AstNode *lsp_obj_get(const AstNode *obj, const char *key);
const char *lsp_obj_string(const AstNode *obj, const char *key);
int lsp_obj_int(const AstNode *obj, const char *key, int def);

/* ------------------------------------------------------------------ */
/* CSV analysis                                                        */
/* ------------------------------------------------------------------ */
CsvBlockInfo *lsp_find_csv_blocks(const TokenArray *tokens, int *out_count);
void lsp_free_csv_blocks(CsvBlockInfo *blocks, int count);

/* Find which CSV block (if any) a token index falls into */
const CsvBlockInfo *lsp_csv_block_for_token(const CsvBlockInfo *blocks,
                                            int block_count, int token_idx);

/* ------------------------------------------------------------------ */
/* Feature handlers (implemented in lsp_features.c)                    */
/* ------------------------------------------------------------------ */
char *lsp_handle_document_symbol(LspDocument *doc);
char *lsp_handle_folding_range(LspDocument *doc);
char *lsp_handle_hover(LspDocument *doc, int line, int col);
char *lsp_handle_definition(LspDocument *doc, int line, int col);
char *lsp_handle_completion(LspDocument *doc, int line, int col);
char *lsp_handle_code_action(LspDocument *doc, int sl, int sc, int el, int ec);
char *lsp_handle_semantic_tokens(LspDocument *doc);
char *lsp_handle_formatting(LspDocument *doc, int indent);
char *lsp_handle_rename(LspDocument *doc, int line, int col, const char *new_name);
char *lsp_handle_document_link(LspDocument *doc);
char *lsp_handle_selection_range(LspDocument *doc, int *lines, int *cols, int count);
char *lsp_handle_inlay_hint(LspDocument *doc, int start_line, int end_line);
char *lsp_handle_document_color(LspDocument *doc);
char *lsp_handle_code_lens(LspDocument *doc);

/* ------------------------------------------------------------------ */
/* Utility: find token at a given line/col                             */
/* ------------------------------------------------------------------ */
int lsp_token_at_pos(const TokenArray *tokens, int line, int col);
int lsp_token_length(const Token *tok);

#endif
