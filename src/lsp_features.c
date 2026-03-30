#include "lsp_server.h"
#include "lsp_json.h"
#include "util.h"
#include "ast.h"
#include "ast_ops.h"
#include "lexer.h"
#include "mron_emit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/* Feature 1: Document Symbols                                         */
/* ================================================================== */

static void emit_symbol(LspJson *j, const char *name, int kind,
                        int line, int col, int end_line, int end_col,
                        AstNode *value) {
    lj_object_start(j);
    lj_key(j, "name"); lj_string(j, name);
    lj_key(j, "kind"); lj_int(j, kind);
    lj_key(j, "range"); lj_range(j, line, col, end_line, end_col);
    lj_key(j, "selectionRange"); lj_range(j, line, col, line, col + (int)strlen(name));

    /* Recurse into records */
    if (value && value->type == AST_RECORD && value->data.record.count > 0) {
        lj_key(j, "children"); lj_array_start(j);
        for (size_t i = 0; i < value->data.record.count; i++) {
            const char *k = value->data.record.pairs[i].key;
            AstNode *v = value->data.record.pairs[i].value;
            int sk = 13; /* Variable */
            if (v->type == AST_RECORD) sk = 2; /* Module -> Object-like */
            else if (v->type == AST_LIST) sk = 18; /* Array */
            else if (v->type == AST_STRING) sk = 15; /* String */
            else if (v->type == AST_NUMBER) sk = 16; /* Number */
            else if (v->type == AST_BOOL) sk = 17; /* Boolean */
            else if (v->type == AST_NULL) sk = 21; /* Null */
            emit_symbol(j, k, sk, v->line - 1, 0, v->line - 1, 80, v);
        }
        lj_array_end(j);
    }
    lj_object_end(j);
}

char *lsp_handle_document_symbol(LspDocument *doc) {
    LspJson j;
    lj_init(&j);
    lj_array_start(&j);

    if (doc->ast && doc->ast->type == AST_RECORD) {
        for (size_t i = 0; i < doc->ast->data.record.count; i++) {
            const char *key = doc->ast->data.record.pairs[i].key;
            AstNode *val = doc->ast->data.record.pairs[i].value;
            int kind = 13; /* Variable */
            if (val->type == AST_RECORD) kind = 2; /* Module */
            else if (val->type == AST_LIST) kind = 18; /* Array */
            else if (val->type == AST_STRING) kind = 15;
            else if (val->type == AST_NUMBER) kind = 16;
            else if (val->type == AST_BOOL) kind = 17;
            else if (val->type == AST_NULL) kind = 21;

            int line = val->line - 1;
            emit_symbol(&j, key, kind, line, 0, line, 80, val);
        }
    }

    lj_array_end(&j);
    return lj_finish(&j);
}

/* ================================================================== */
/* Feature 2: Folding Ranges                                           */
/* ================================================================== */

char *lsp_handle_folding_range(LspDocument *doc) {
    LspJson j;
    lj_init(&j);
    lj_array_start(&j);

    if (doc->tokens) {
        /* Fold matching delimiters using a stack */
        int stack_lines[256];
        int sp = 0;

        for (size_t i = 0; i < doc->tokens->count; i++) {
            Token *t = &doc->tokens->tokens[i];
            if (t->type == TOKEN_LBRACE || t->type == TOKEN_LBRACKET ||
                t->type == TOKEN_LPAREN) {
                if (sp < 256) {
                    stack_lines[sp] = t->line;
                    sp++;
                }
            } else if (t->type == TOKEN_RBRACE || t->type == TOKEN_RBRACKET ||
                       t->type == TOKEN_RPAREN) {
                if (sp > 0) {
                    sp--;
                    int start_line = stack_lines[sp] - 1;
                    int end_line = t->line - 1;
                    if (end_line > start_line) {
                        lj_object_start(&j);
                        lj_key(&j, "startLine"); lj_int(&j, start_line);
                        lj_key(&j, "endLine"); lj_int(&j, end_line);
                        lj_key(&j, "kind"); lj_string(&j, "region");
                        lj_object_end(&j);
                    }
                }
            }
        }
    }

    /* Fold comment blocks: scan source for ### ... ### */
    if (doc->content) {
        const char *p = doc->content;
        int line = 0;
        while (*p) {
            if (p[0] == '#' && p[1] == '#' && p[2] == '#') {
                int start_line = line;
                p += 3;
                /* Find closing ### */
                while (*p) {
                    if (p[0] == '#' && p[1] == '#' && p[2] == '#') {
                        int end_line = line;
                        if (end_line > start_line) {
                            lj_object_start(&j);
                            lj_key(&j, "startLine"); lj_int(&j, start_line);
                            lj_key(&j, "endLine"); lj_int(&j, end_line);
                            lj_key(&j, "kind"); lj_string(&j, "comment");
                            lj_object_end(&j);
                        }
                        p += 3;
                        break;
                    }
                    if (*p == '\n') line++;
                    p++;
                }
                continue;
            }
            if (*p == '\n') line++;
            p++;
        }
    }

    lj_array_end(&j);
    return lj_finish(&j);
}

/* ================================================================== */
/* Feature 3: Hover                                                    */
/* ================================================================== */

char *lsp_handle_hover(LspDocument *doc, int line, int col) {
    if (!doc->tokens) return NULL;

    int ti = lsp_token_at_pos(doc->tokens, line, col);
    if (ti < 0) return NULL;

    Token *tok = &doc->tokens->tokens[ti];
    StringBuilder md;
    sb_init(&md);

    switch (tok->type) {
    case TOKEN_IDENT: {
        /* Check if it's a keyword */
        if (strcmp(tok->value, "true") == 0 || strcmp(tok->value, "yes") == 0) {
            sb_append(&md, "**boolean** `true`\n\nJSON: `true`");
        } else if (strcmp(tok->value, "false") == 0 || strcmp(tok->value, "no") == 0) {
            sb_append(&md, "**boolean** `false`\n\nJSON: `false`");
        } else if (strcmp(tok->value, "null") == 0) {
            sb_append(&md, "**null**\n\nJSON: `null`");
        } else {
            /* It's a key name. If we have the AST, show the type of its value */
            sb_append(&md, "**key** `");
            sb_append(&md, tok->value);
            sb_append(&md, "`");
            if (doc->ast) {
                const AstNode *val = ast_query(doc->ast, tok->value);
                if (val) {
                    sb_append(&md, "\n\nType: ");
                    sb_append(&md, ast_type_name(val->type));
                    if (val->type == AST_RECORD) {
                        char buf[64];
                        snprintf(buf, sizeof(buf), " (%zu keys)",
                                 val->data.record.count);
                        sb_append(&md, buf);
                    } else if (val->type == AST_LIST) {
                        char buf[64];
                        snprintf(buf, sizeof(buf), " (%zu items)",
                                 val->data.list.count);
                        sb_append(&md, buf);
                    }
                }
            }
            /* Check if it's a CSV column header */
            int csv_count = 0;
            CsvBlockInfo *csvs = lsp_find_csv_blocks(doc->tokens, &csv_count);
            for (int b = 0; b < csv_count; b++) {
                for (int c = 0; c < csvs[b].col_count; c++) {
                    if (strcmp(csvs[b].columns[c], tok->value) == 0 &&
                        ti > csvs[b].paren_open && ti < csvs[b].paren_close) {
                        int rows = csvs[b].col_count > 0 ?
                            csvs[b].value_count / csvs[b].col_count : 0;
                        char buf[128];
                        snprintf(buf, sizeof(buf),
                                 "\n\nCSV column %d of %d (%d rows)",
                                 c + 1, csvs[b].col_count, rows);
                        sb_append(&md, buf);
                    }
                }
            }
            lsp_free_csv_blocks(csvs, csv_count);
        }
        break;
    }
    case TOKEN_STRING:
        sb_append(&md, "**string** `\"");
        sb_append(&md, tok->value);
        sb_append(&md, "\"`");
        break;
    case TOKEN_NUMBER:
        sb_append(&md, "**number** `");
        sb_append(&md, tok->value);
        sb_append(&md, "`\n\nJSON: `");
        sb_append(&md, tok->value);
        sb_append(&md, "`");
        break;
    default:
        sb_free(&md);
        return NULL;
    }

    char *md_text = sb_finish(&md);

    LspJson j;
    lj_init(&j);
    lj_object_start(&j);
    lj_key(&j, "contents"); lj_object_start(&j);
      lj_key(&j, "kind"); lj_string(&j, "markdown");
      lj_key(&j, "value"); lj_string(&j, md_text);
    lj_object_end(&j);
    lj_object_end(&j);

    free(md_text);
    return lj_finish(&j);
}

/* ================================================================== */
/* Feature 4: Go to Definition                                         */
/* ================================================================== */

char *lsp_handle_definition(LspDocument *doc, int line, int col) {
    if (!doc->tokens) return NULL;

    int ti = lsp_token_at_pos(doc->tokens, line, col);
    if (ti < 0) return NULL;

    /* If the token is inside a CSV list body, jump to its column header */
    int csv_count = 0;
    CsvBlockInfo *csvs = lsp_find_csv_blocks(doc->tokens, &csv_count);

    for (int b = 0; b < csv_count; b++) {
        CsvBlockInfo *blk = &csvs[b];
        /* Check if token is a value in this CSV block */
        for (int v = 0; v < blk->value_count; v++) {
            if (blk->value_indices[v] == ti) {
                /* This is value #v, which belongs to column v % col_count */
                int col_idx = v % blk->col_count;
                /* Find the column header token */
                int header_tok_idx = -1;
                int col_n = 0;
                for (int k = blk->paren_open + 1; k < blk->paren_close; k++) {
                    if (doc->tokens->tokens[k].type == TOKEN_IDENT) {
                        if (col_n == col_idx) { header_tok_idx = k; break; }
                        col_n++;
                    }
                }
                if (header_tok_idx >= 0) {
                    Token *ht = &doc->tokens->tokens[header_tok_idx];
                    LspJson j; lj_init(&j);
                    lj_object_start(&j);
                    lj_key(&j, "uri"); lj_string(&j, doc->uri);
                    lj_key(&j, "range");
                    lj_range(&j, ht->line - 1, ht->col - 1,
                                 ht->line - 1, ht->col - 1 + lsp_token_length(ht));
                    lj_object_end(&j);
                    lsp_free_csv_blocks(csvs, csv_count);
                    return lj_finish(&j);
                }
            }
        }
    }

    lsp_free_csv_blocks(csvs, csv_count);
    return NULL;
}

/* ================================================================== */
/* Feature 5: Completion                                               */
/* ================================================================== */

char *lsp_handle_completion(LspDocument *doc, int line, int col) {
    LspJson j;
    lj_init(&j);
    lj_array_start(&j);

    (void)col;

    /* Boolean/null keyword completions */
    const char *keywords[] = {"true", "false", "yes", "no", "null"};
    int kw_kinds[] = {6, 6, 6, 6, 6}; /* Keyword=6 in CompletionItemKind */
    for (int i = 0; i < 5; i++) {
        lj_object_start(&j);
        lj_key(&j, "label"); lj_string(&j, keywords[i]);
        lj_key(&j, "kind"); lj_int(&j, kw_kinds[i]);
        lj_object_end(&j);
    }

    /* Sibling key completions: if we're inside a record, suggest keys from
       sibling records at the same level */
    if (doc->ast && doc->ast->type == AST_RECORD) {
        /* Collect all keys used in top-level records to suggest */
        KeySet seen;
        keyset_init(&seen, 32);

        for (size_t i = 0; i < doc->ast->data.record.count; i++) {
            const char *key = doc->ast->data.record.pairs[i].key;
            if (!keyset_insert(&seen, key)) {
                lj_object_start(&j);
                lj_key(&j, "label"); lj_string(&j, key);
                lj_key(&j, "kind"); lj_int(&j, 5); /* Field */
                lj_key(&j, "detail"); lj_string(&j, "existing key");
                lj_object_end(&j);
            }
        }

        /* If cursor is inside a nested record, suggest its sibling keys */
        if (doc->tokens) {
            /* Find the innermost record at this line */
            int tgt_line = line + 1;
            int depth = 0;
            int in_record = 0;
            (void)in_record;
            for (size_t i = 0; i < doc->tokens->count; i++) {
                Token *t = &doc->tokens->tokens[i];
                if (t->type == TOKEN_LBRACE) {
                    depth++;
                    if (t->line <= tgt_line) in_record = 1;
                }
                if (t->type == TOKEN_RBRACE) {
                    depth--;
                }

                /* Suggest keys from nested record if cursor is inside */
                if (t->type == TOKEN_IDENT && t->line == tgt_line && depth > 0) {
                    if (!keyset_insert(&seen, t->value)) {
                        lj_object_start(&j);
                        lj_key(&j, "label"); lj_string(&j, t->value);
                        lj_key(&j, "kind"); lj_int(&j, 5);
                        lj_key(&j, "detail"); lj_string(&j, "nested key");
                        lj_object_end(&j);
                    }
                }
            }
        }
        keyset_free(&seen);
    }

    lj_array_end(&j);
    return lj_finish(&j);
}

/* ================================================================== */
/* Feature 6: Code Actions                                             */
/* ================================================================== */

static void emit_text_edit_action(LspJson *j, const char *title,
                                  const char *uri,
                                  int sl, int sc, int el, int ec,
                                  const char *new_text) {
    lj_object_start(j);
    lj_key(j, "title"); lj_string(j, title);
    lj_key(j, "kind"); lj_string(j, "quickfix");
    lj_key(j, "edit"); lj_object_start(j);
      lj_key(j, "changes"); lj_object_start(j);
        lj_key(j, uri); lj_array_start(j);
          lj_object_start(j);
            lj_key(j, "range"); lj_range(j, sl, sc, el, ec);
            lj_key(j, "newText"); lj_string(j, new_text);
          lj_object_end(j);
        lj_array_end(j);
      lj_object_end(j);
    lj_object_end(j);
    lj_object_end(j);
}

char *lsp_handle_code_action(LspDocument *doc, int sl, int sc, int el, int ec) {
    LspJson j;
    lj_init(&j);
    lj_array_start(&j);

    (void)el; (void)ec;

    if (!doc->tokens) { lj_array_end(&j); return lj_finish(&j); }

    /* Find token at start of selection */
    int ti = lsp_token_at_pos(doc->tokens, sl, sc);
    if (ti >= 0) {
        Token *tok = &doc->tokens->tokens[ti];

        /* Action: normalize yes -> true, no -> false */
        if (tok->type == TOKEN_IDENT) {
            if (strcmp(tok->value, "yes") == 0) {
                emit_text_edit_action(&j, "Normalize 'yes' to 'true'", doc->uri,
                    tok->line - 1, tok->col - 1,
                    tok->line - 1, tok->col - 1 + 3, "true");
            }
            if (strcmp(tok->value, "no") == 0) {
                emit_text_edit_action(&j, "Normalize 'no' to 'false'", doc->uri,
                    tok->line - 1, tok->col - 1,
                    tok->line - 1, tok->col - 1 + 2, "false");
            }
        }
    }

    /* Action: sort keys (whole document) */
    if (doc->ast && doc->ast->type == AST_RECORD) {
        AstNode *clone = ast_clone(doc->ast);
        if (clone) {
            ast_sort_keys(clone);
            char *sorted = mron_emit(clone);
            ast_free(clone);
            if (sorted) {
                /* Count lines in document */
                int total_lines = 1;
                for (const char *p = doc->content; *p; p++) {
                    if (*p == '\n') total_lines++;
                }
                lj_object_start(&j);
                lj_key(&j, "title"); lj_string(&j, "Sort keys alphabetically");
                lj_key(&j, "kind"); lj_string(&j, "source.sortKeys");
                lj_key(&j, "edit"); lj_object_start(&j);
                  lj_key(&j, "changes"); lj_object_start(&j);
                    lj_key(&j, doc->uri); lj_array_start(&j);
                      lj_object_start(&j);
                        lj_key(&j, "range");
                        lj_range(&j, 0, 0, total_lines, 0);
                        lj_key(&j, "newText"); lj_string(&j, sorted);
                      lj_object_end(&j);
                    lj_array_end(&j);
                  lj_object_end(&j);
                lj_object_end(&j);
                lj_object_end(&j);
                free(sorted);
            }
        }
    }

    /* Action: convert CSV to records / records to CSV (MRON-specific) */
    if (doc->tokens) {
        int csv_count = 0;
        CsvBlockInfo *csvs = lsp_find_csv_blocks(doc->tokens, &csv_count);

        for (int b = 0; b < csv_count; b++) {
            CsvBlockInfo *blk = &csvs[b];
            Token *key_tok = &doc->tokens->tokens[blk->key_idx];

            /* Only offer if selection overlaps this CSV block */
            int blk_start = key_tok->line - 1;
            int blk_end = doc->tokens->tokens[blk->bracket_close].line - 1;
            if (sl > blk_end || sl < blk_start) continue;

            /* Build expanded record syntax */
            StringBuilder sb;
            sb_init(&sb);
            sb_append(&sb, key_tok->value);
            sb_append(&sb, " [\n");

            if (blk->col_count > 0 && blk->value_count % blk->col_count == 0) {
                int rows = blk->value_count / blk->col_count;
                for (int r = 0; r < rows; r++) {
                    sb_append(&sb, "    {\n");
                    for (int c = 0; c < blk->col_count; c++) {
                        int vi = blk->value_indices[r * blk->col_count + c];
                        Token *vt = &doc->tokens->tokens[vi];
                        sb_append(&sb, "        ");
                        sb_append(&sb, blk->columns[c]);
                        sb_append(&sb, " ");
                        if (vt->type == TOKEN_STRING) {
                            sb_append_char(&sb, '"');
                            sb_append(&sb, vt->value);
                            sb_append_char(&sb, '"');
                        } else {
                            sb_append(&sb, vt->value);
                        }
                        sb_append_char(&sb, '\n');
                    }
                    sb_append(&sb, "    }\n");
                }
            }
            sb_append(&sb, "]");
            char *expanded = sb_finish(&sb);

            lj_object_start(&j);
            lj_key(&j, "title"); lj_string(&j, "Expand CSV to record syntax");
            lj_key(&j, "kind"); lj_string(&j, "refactor.rewrite");
            lj_key(&j, "edit"); lj_object_start(&j);
              lj_key(&j, "changes"); lj_object_start(&j);
                lj_key(&j, doc->uri); lj_array_start(&j);
                  lj_object_start(&j);
                    lj_key(&j, "range");
                    lj_range(&j, blk_start, 0, blk_end, 999);
                    lj_key(&j, "newText"); lj_string(&j, expanded);
                  lj_object_end(&j);
                lj_array_end(&j);
              lj_object_end(&j);
            lj_object_end(&j);
            lj_object_end(&j);

            free(expanded);
        }

        lsp_free_csv_blocks(csvs, csv_count);
    }

    lj_array_end(&j);
    return lj_finish(&j);
}

/* ================================================================== */
/* Feature 7: Semantic Tokens                                          */
/* ================================================================== */
/* Token types: 0=variable, 1=parameter, 2=string, 3=number,
                4=keyword, 5=comment, 6=operator */

char *lsp_handle_semantic_tokens(LspDocument *doc) {
    LspJson j;
    lj_init(&j);
    lj_object_start(&j);
    lj_key(&j, "data"); lj_array_start(&j);

    if (doc->tokens) {
        /* Find CSV blocks for column identification */
        int csv_count = 0;
        CsvBlockInfo *csvs = lsp_find_csv_blocks(doc->tokens, &csv_count);

        int prev_line = 0, prev_col = 0;

        for (size_t i = 0; i < doc->tokens->count; i++) {
            Token *t = &doc->tokens->tokens[i];
            if (t->type == TOKEN_EOF) break;

            int token_type = -1;
            int tlen = lsp_token_length(t);
            int tline = t->line - 1;
            int tcol = t->col - 1;

            switch (t->type) {
            case TOKEN_IDENT:
                /* Determine if this is a key, CSV column, or keyword */
                if (strcmp(t->value, "true") == 0 || strcmp(t->value, "yes") == 0 ||
                    strcmp(t->value, "false") == 0 || strcmp(t->value, "no") == 0 ||
                    strcmp(t->value, "null") == 0) {
                    token_type = 4; /* keyword */
                } else {
                    /* Check if it's a CSV column header */
                    int is_csv_col = 0;
                    for (int b = 0; b < csv_count; b++) {
                        if ((int)i > csvs[b].paren_open &&
                            (int)i < csvs[b].paren_close) {
                            is_csv_col = 1;
                            break;
                        }
                    }
                    token_type = is_csv_col ? 1 : 0; /* parameter or variable */
                }
                break;
            case TOKEN_STRING:
                token_type = 2;
                tlen += 2; /* Include quotes */
                tcol -= 1; /* Token col points to content, adjust for opening quote */
                if (tcol < 0) tcol = 0;
                break;
            case TOKEN_NUMBER:
                token_type = 3;
                break;
            case TOKEN_LBRACE: case TOKEN_RBRACE:
            case TOKEN_LBRACKET: case TOKEN_RBRACKET:
            case TOKEN_LPAREN: case TOKEN_RPAREN:
                token_type = 6; /* operator */
                tlen = 1;
                break;
            default:
                continue;
            }

            if (token_type < 0 || tlen <= 0) continue;

            /* Emit delta-encoded token: deltaLine, deltaStartChar, length, type, modifiers */
            int delta_line = tline - prev_line;
            int delta_col = (delta_line == 0) ? (tcol - prev_col) : tcol;

            lj_int(&j, delta_line);
            lj_int(&j, delta_col);
            lj_int(&j, tlen);
            lj_int(&j, token_type);
            lj_int(&j, 0); /* modifiers */

            prev_line = tline;
            prev_col = tcol;
        }

        lsp_free_csv_blocks(csvs, csv_count);
    }

    lj_array_end(&j);
    lj_object_end(&j);
    return lj_finish(&j);
}

/* ================================================================== */
/* Feature 8: Formatting                                               */
/* ================================================================== */

char *lsp_handle_formatting(LspDocument *doc, int indent) {
    if (!doc->ast) return mron_strdup("[]");

    /* Re-emit through the MRON emitter with requested indentation */
    char *formatted = (indent != 4) ? mron_emit_indent(doc->ast, indent) : mron_emit(doc->ast);
    if (!formatted) return mron_strdup("[]");

    /* Count total lines for replacement range */
    int total_lines = 1;
    for (const char *p = doc->content; *p; p++) {
        if (*p == '\n') total_lines++;
    }

    LspJson j;
    lj_init(&j);
    lj_array_start(&j);
    lj_object_start(&j);
      lj_key(&j, "range"); lj_range(&j, 0, 0, total_lines, 0);
      lj_key(&j, "newText"); lj_string(&j, formatted);
    lj_object_end(&j);
    lj_array_end(&j);

    free(formatted);
    return lj_finish(&j);
}

/* ================================================================== */
/* Feature 9: Rename                                                   */
/* ================================================================== */

char *lsp_handle_rename(LspDocument *doc, int line, int col,
                        const char *new_name) {
    if (!doc->tokens) return mron_strdup("null");

    int ti = lsp_token_at_pos(doc->tokens, line, col);
    if (ti < 0) return mron_strdup("null");

    Token *tok = &doc->tokens->tokens[ti];
    if (tok->type != TOKEN_IDENT) return mron_strdup("null");

    const char *old_name = tok->value;

    /* Check if this is a CSV column header */
    int csv_count = 0;
    CsvBlockInfo *csvs = lsp_find_csv_blocks(doc->tokens, &csv_count);
    int is_csv_header = 0;
    int csv_block_idx = -1;
    int csv_col_idx = -1;

    for (int b = 0; b < csv_count; b++) {
        if (ti > csvs[b].paren_open && ti < csvs[b].paren_close) {
            is_csv_header = 1;
            csv_block_idx = b;
            int cn = 0;
            for (int k = csvs[b].paren_open + 1; k < csvs[b].paren_close; k++) {
                if (doc->tokens->tokens[k].type == TOKEN_IDENT) {
                    if (k == ti) { csv_col_idx = cn; break; }
                    cn++;
                }
            }
            break;
        }
    }

    LspJson j;
    lj_init(&j);
    lj_object_start(&j);
    lj_key(&j, "changes"); lj_object_start(&j);
    lj_key(&j, doc->uri); lj_array_start(&j);

    if (is_csv_header && csv_block_idx >= 0) {
        /* Rename the column header token */
        lj_object_start(&j);
        lj_key(&j, "range");
        lj_range(&j, tok->line - 1, tok->col - 1,
                     tok->line - 1, tok->col - 1 + lsp_token_length(tok));
        lj_key(&j, "newText"); lj_string(&j, new_name);
        lj_object_end(&j);

        /* The CSV values themselves don't store column names, so the rename
           only affects the header. But if the AST was expanded (records created
           from CSV), we'd also need to rename keys in the expanded records.
           Since the AST stores the data, and the source has CSV syntax,
           only the header token needs renaming. */
        (void)csv_col_idx;
    } else {
        /* Rename all occurrences of this identifier as a key */
        for (size_t i = 0; i < doc->tokens->count; i++) {
            Token *t = &doc->tokens->tokens[i];
            if (t->type == TOKEN_IDENT && strcmp(t->value, old_name) == 0) {
                /* Check if this is in a key position (not a keyword value) */
                if (strcmp(t->value, "true") == 0 || strcmp(t->value, "yes") == 0 ||
                    strcmp(t->value, "false") == 0 || strcmp(t->value, "no") == 0 ||
                    strcmp(t->value, "null") == 0) continue;

                lj_object_start(&j);
                lj_key(&j, "range");
                lj_range(&j, t->line - 1, t->col - 1,
                             t->line - 1, t->col - 1 + lsp_token_length(t));
                lj_key(&j, "newText"); lj_string(&j, new_name);
                lj_object_end(&j);
            }
        }
    }

    lj_array_end(&j);
    lj_object_end(&j);
    lj_object_end(&j);

    lsp_free_csv_blocks(csvs, csv_count);
    return lj_finish(&j);
}

/* ================================================================== */
/* Feature 10: Document Links                                          */
/* ================================================================== */

char *lsp_handle_document_link(LspDocument *doc) {
    LspJson j;
    lj_init(&j);
    lj_array_start(&j);

    if (doc->tokens) {
        for (size_t i = 0; i < doc->tokens->count; i++) {
            Token *t = &doc->tokens->tokens[i];
            if (t->type != TOKEN_STRING || !t->value) continue;

            /* Check if the string looks like a file path */
            const char *v = t->value;
            size_t vlen = strlen(v);
            if (vlen < 3) continue;

            /* Detect file extensions */
            const char *ext = strrchr(v, '.');
            if (!ext) continue;
            if (strcmp(ext, ".mron") != 0 && strcmp(ext, ".json") != 0 &&
                strcmp(ext, ".yaml") != 0 && strcmp(ext, ".yml") != 0 &&
                strcmp(ext, ".toml") != 0) continue;

            /* Build a file URI. The link target is relative to doc URI. */
            lj_object_start(&j);
            lj_key(&j, "range");
            /* +1/-1 to account for quotes around the string in source */
            lj_range(&j, t->line - 1, t->col - 1,
                         t->line - 1, t->col - 1 + (int)vlen);
            lj_key(&j, "target"); lj_string(&j, v);
            lj_object_end(&j);
        }
    }

    lj_array_end(&j);
    return lj_finish(&j);
}

/* ================================================================== */
/* Feature 11: Selection Range                                         */
/* ================================================================== */

char *lsp_handle_selection_range(LspDocument *doc, int *lines, int *cols, int count) {
    LspJson j;
    lj_init(&j);
    lj_array_start(&j);

    for (int p = 0; p < count; p++) {
        int ti = lsp_token_at_pos(doc->tokens, lines[p], cols[p]);

        if (ti < 0 || !doc->tokens) {
            lj_null(&j);
            continue;
        }

        Token *tok = &doc->tokens->tokens[ti];
        int tlen = lsp_token_length(tok);
        int tl = tok->line - 1;
        int tc = tok->col - 1;

        /* Token range (innermost) */
        lj_object_start(&j);
        lj_key(&j, "range"); lj_range(&j, tl, tc, tl, tc + tlen);

        /* Find enclosing block for parent range */
        int brace_start = -1, brace_end = -1;
        int depth = 0;
        for (int i = ti; i >= 0; i--) {
            TokenType tt = doc->tokens->tokens[i].type;
            if (tt == TOKEN_RBRACE || tt == TOKEN_RBRACKET) depth++;
            if (tt == TOKEN_LBRACE || tt == TOKEN_LBRACKET) {
                if (depth == 0) { brace_start = i; break; }
                depth--;
            }
        }
        depth = 0;
        for (size_t i = (size_t)ti; i < doc->tokens->count; i++) {
            TokenType tt = doc->tokens->tokens[i].type;
            if (tt == TOKEN_LBRACE || tt == TOKEN_LBRACKET) depth++;
            if (tt == TOKEN_RBRACE || tt == TOKEN_RBRACKET) {
                if (depth == 0) { brace_end = (int)i; break; }
                depth--;
            }
        }

        if (brace_start >= 0 && brace_end >= 0) {
            Token *bs = &doc->tokens->tokens[brace_start];
            Token *be = &doc->tokens->tokens[brace_end];
            lj_key(&j, "parent"); lj_object_start(&j);
            lj_key(&j, "range");
            lj_range(&j, bs->line - 1, bs->col - 1,
                         be->line - 1, be->col);
            /* Document-level parent */
            lj_key(&j, "parent"); lj_object_start(&j);
            lj_key(&j, "range"); lj_range(&j, 0, 0, 99999, 0);
            lj_object_end(&j);
            lj_object_end(&j);
        }

        lj_object_end(&j);
    }

    lj_array_end(&j);
    return lj_finish(&j);
}

/* ================================================================== */
/* Feature 12: Inlay Hints (CSV column labels)                         */
/* ================================================================== */

char *lsp_handle_inlay_hint(LspDocument *doc, int start_line, int end_line) {
    LspJson j;
    lj_init(&j);
    lj_array_start(&j);

    if (doc->tokens) {
        int csv_count = 0;
        CsvBlockInfo *csvs = lsp_find_csv_blocks(doc->tokens, &csv_count);

        for (int b = 0; b < csv_count; b++) {
            CsvBlockInfo *blk = &csvs[b];
            if (blk->col_count == 0) continue;

            /* For each value in the CSV body, show its column name */
            for (int v = 0; v < blk->value_count; v++) {
                int vi = blk->value_indices[v];
                Token *vt = &doc->tokens->tokens[vi];
                int vline = vt->line - 1;

                /* Only emit for visible range */
                if (vline < start_line || vline > end_line) continue;

                int col_idx = v % blk->col_count;
                /* Show hint before the first value in each row */
                if (col_idx == 0 || v == 0) {
                    /* For first column, no hint needed (it's obvious from position) */
                }

                /* Show column name as inlay hint before each value */
                lj_object_start(&j);
                lj_key(&j, "position");
                lj_position(&j, vline, vt->col - 1);
                lj_key(&j, "label"); lj_string(&j, blk->columns[col_idx]);
                lj_key(&j, "kind"); lj_int(&j, 2); /* Parameter */
                lj_key(&j, "paddingRight"); lj_bool(&j, 1);
                lj_object_end(&j);
            }
        }

        lsp_free_csv_blocks(csvs, csv_count);
    }

    lj_array_end(&j);
    return lj_finish(&j);
}

/* ================================================================== */
/* Feature 13: Document Color                                          */
/* ================================================================== */

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int is_hex_color(const char *s, int *r, int *g, int *b, int *a) {
    if (!s || s[0] != '#') return 0;
    size_t len = strlen(s);
    *a = 255;

    if (len == 7) { /* #RRGGBB */
        for (int i = 1; i < 7; i++) if (hex_val(s[i]) < 0) return 0;
        *r = hex_val(s[1]) * 16 + hex_val(s[2]);
        *g = hex_val(s[3]) * 16 + hex_val(s[4]);
        *b = hex_val(s[5]) * 16 + hex_val(s[6]);
        return 1;
    }
    if (len == 4) { /* #RGB */
        for (int i = 1; i < 4; i++) if (hex_val(s[i]) < 0) return 0;
        *r = hex_val(s[1]) * 17;
        *g = hex_val(s[2]) * 17;
        *b = hex_val(s[3]) * 17;
        return 1;
    }
    if (len == 9) { /* #RRGGBBAA */
        for (int i = 1; i < 9; i++) if (hex_val(s[i]) < 0) return 0;
        *r = hex_val(s[1]) * 16 + hex_val(s[2]);
        *g = hex_val(s[3]) * 16 + hex_val(s[4]);
        *b = hex_val(s[5]) * 16 + hex_val(s[6]);
        *a = hex_val(s[7]) * 16 + hex_val(s[8]);
        return 1;
    }
    return 0;
}

char *lsp_handle_document_color(LspDocument *doc) {
    LspJson j;
    lj_init(&j);
    lj_array_start(&j);

    if (doc->tokens) {
        for (size_t i = 0; i < doc->tokens->count; i++) {
            Token *t = &doc->tokens->tokens[i];
            if (t->type != TOKEN_STRING || !t->value) continue;

            int r, g, b, a;
            if (is_hex_color(t->value, &r, &g, &b, &a)) {
                lj_object_start(&j);
                lj_key(&j, "range");
                lj_range(&j, t->line - 1, t->col - 1,
                             t->line - 1, t->col - 1 + (int)strlen(t->value));
                lj_key(&j, "color"); lj_object_start(&j);
                  char buf[32];
                  lj_key(&j, "red"); snprintf(buf, sizeof(buf), "%.4f", r / 255.0);
                  lj_raw(&j, buf);
                  lj_key(&j, "green"); snprintf(buf, sizeof(buf), "%.4f", g / 255.0);
                  lj_raw(&j, buf);
                  lj_key(&j, "blue"); snprintf(buf, sizeof(buf), "%.4f", b / 255.0);
                  lj_raw(&j, buf);
                  lj_key(&j, "alpha"); snprintf(buf, sizeof(buf), "%.4f", a / 255.0);
                  lj_raw(&j, buf);
                lj_object_end(&j);
                lj_object_end(&j);
            }
        }
    }

    lj_array_end(&j);
    return lj_finish(&j);
}

/* ================================================================== */
/* Feature 14: Code Lens (Format Preview)                              */
/* ================================================================== */

char *lsp_handle_code_lens(LspDocument *doc) {
    LspJson j;
    lj_init(&j);
    lj_array_start(&j);

    if (doc->ast) {
        /* Show a code lens on line 0 with key/value count */
        int key_count = 0;
        if (doc->ast->type == AST_RECORD)
            key_count = (int)doc->ast->data.record.count;

        char title[128];
        snprintf(title, sizeof(title), "MRON: %d top-level key(s)", key_count);

        lj_object_start(&j);
        lj_key(&j, "range"); lj_range(&j, 0, 0, 0, 0);
        lj_key(&j, "command"); lj_object_start(&j);
          lj_key(&j, "title"); lj_string(&j, title);
          lj_key(&j, "command"); lj_string(&j, "");
        lj_object_end(&j);
        lj_object_end(&j);

        /* Show format previews */
        const char *fmts[] = {"JSON", "YAML", "TOML"};
        const char *cmds[] = {"mron.previewJSON", "mron.previewYAML", "mron.previewTOML"};
        for (int i = 0; i < 3; i++) {
            /* Skip TOML if not a record */
            if (i == 2 && doc->ast->type != AST_RECORD) continue;

            lj_object_start(&j);
            lj_key(&j, "range"); lj_range(&j, 0, 0, 0, 0);
            lj_key(&j, "command"); lj_object_start(&j);
              char label[64];
              snprintf(label, sizeof(label), "Preview as %s", fmts[i]);
              lj_key(&j, "title"); lj_string(&j, label);
              lj_key(&j, "command"); lj_string(&j, cmds[i]);
              lj_key(&j, "arguments"); lj_array_start(&j);
                lj_string(&j, doc->uri);
              lj_array_end(&j);
            lj_object_end(&j);
            lj_object_end(&j);
        }
    }

    lj_array_end(&j);
    return lj_finish(&j);
}
