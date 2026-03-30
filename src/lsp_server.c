#include "lsp_server.h"
#include "lsp_transport.h"
#include "lsp_json.h"
#include "util.h"
#include "ast.h"
#include "ast_ops.h"
#include "lexer.h"
#include "parser.h"
#include "json_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/* Document store                                                      */
/* ================================================================== */

static LspDocument docs[LSP_MAX_DOCS];
static int doc_count = 0;
static int server_initialized = 0;

/* Path to schema file, set via workspace config */
static char *schema_path = NULL;
static AstNode *schema_ast = NULL;

void lsp_server_init(void) {
    doc_count = 0;
    server_initialized = 0;
    schema_path = NULL;
    schema_ast = NULL;
}

void lsp_server_shutdown(void) {
    for (int i = 0; i < doc_count; i++) {
        free(docs[i].uri);
        free(docs[i].content);
        if (docs[i].tokens) lexer_free(docs[i].tokens);
        if (docs[i].ast) ast_free(docs[i].ast);
    }
    doc_count = 0;
    free(schema_path);
    if (schema_ast) ast_free(schema_ast);
}

LspDocument *lsp_doc_open(const char *uri, const char *content, int version) {
    if (doc_count >= LSP_MAX_DOCS) return NULL;
    LspDocument *doc = &docs[doc_count++];
    doc->uri = mron_strdup(uri);
    doc->content = mron_strdup(content);
    doc->version = version;
    doc->tokens = NULL;
    doc->ast = NULL;
    lsp_doc_reparse(doc);
    return doc;
}

LspDocument *lsp_doc_find(const char *uri) {
    for (int i = 0; i < doc_count; i++) {
        if (strcmp(docs[i].uri, uri) == 0) return &docs[i];
    }
    return NULL;
}

void lsp_doc_update(LspDocument *doc, const char *content, int version) {
    free(doc->content);
    doc->content = mron_strdup(content);
    doc->version = version;
    lsp_doc_reparse(doc);
}

void lsp_doc_close(const char *uri) {
    for (int i = 0; i < doc_count; i++) {
        if (strcmp(docs[i].uri, uri) == 0) {
            free(docs[i].uri);
            free(docs[i].content);
            if (docs[i].tokens) lexer_free(docs[i].tokens);
            if (docs[i].ast) ast_free(docs[i].ast);
            docs[i] = docs[doc_count - 1];
            doc_count--;
            return;
        }
    }
}

void lsp_doc_reparse(LspDocument *doc) {
    if (doc->tokens) { lexer_free(doc->tokens); doc->tokens = NULL; }
    if (doc->ast) { ast_free(doc->ast); doc->ast = NULL; }

    doc->tokens = lexer_tokenize(doc->content, doc->uri);
    if (doc->tokens && !doc->tokens->has_error) {
        /* Redirect stderr during parse to suppress messages to log */
        doc->ast = parser_parse(doc->tokens);
    }
}

/* ================================================================== */
/* JSON navigation helpers                                             */
/* ================================================================== */

const AstNode *lsp_obj_get(const AstNode *obj, const char *key) {
    if (!obj || obj->type != AST_RECORD) return NULL;
    for (size_t i = 0; i < obj->data.record.count; i++) {
        if (strcmp(obj->data.record.pairs[i].key, key) == 0)
            return obj->data.record.pairs[i].value;
    }
    return NULL;
}

const char *lsp_obj_string(const AstNode *obj, const char *key) {
    const AstNode *n = lsp_obj_get(obj, key);
    return (n && n->type == AST_STRING) ? n->data.string_val : NULL;
}

int lsp_obj_int(const AstNode *obj, const char *key, int def) {
    const AstNode *n = lsp_obj_get(obj, key);
    if (n && n->type == AST_NUMBER) return atoi(n->data.number_str);
    return def;
}

/* ================================================================== */
/* Token utilities                                                     */
/* ================================================================== */

int lsp_token_length(const Token *tok) {
    if (!tok || !tok->value) return 0;
    return (int)strlen(tok->value);
}

int lsp_token_at_pos(const TokenArray *tokens, int line, int col) {
    if (!tokens) return -1;
    /* 1-based line/col in tokens, 0-based line/col from LSP */
    int tgt_line = line + 1;
    int tgt_col = col + 1;
    int best = -1;
    for (size_t i = 0; i < tokens->count; i++) {
        const Token *t = &tokens->tokens[i];
        if (t->type == TOKEN_EOF) break;
        if (t->line == tgt_line) {
            int end_col = t->col + lsp_token_length(t);
            if (tgt_col >= t->col && tgt_col < end_col) return (int)i;
            /* Track closest token on the same line */
            if (best < 0 || abs(t->col - tgt_col) <
                abs(tokens->tokens[best].col - tgt_col))
                best = (int)i;
        }
    }
    return best;
}

/* ================================================================== */
/* CSV block analysis                                                  */
/* ================================================================== */

CsvBlockInfo *lsp_find_csv_blocks(const TokenArray *tokens, int *out_count) {
    *out_count = 0;
    if (!tokens) return NULL;

    CsvBlockInfo *blocks = NULL;
    int cap = 0;

    for (size_t i = 0; i + 3 < tokens->count; i++) {
        /* Pattern: IDENT LPAREN ... RPAREN LBRACKET ... RBRACKET */
        if (tokens->tokens[i].type != TOKEN_IDENT) continue;
        if (tokens->tokens[i + 1].type != TOKEN_LPAREN) continue;

        int key_idx = (int)i;
        int paren_open = (int)i + 1;

        /* Find RPAREN */
        int paren_close = -1;
        size_t j = i + 2;
        while (j < tokens->count && tokens->tokens[j].type != TOKEN_RPAREN &&
               tokens->tokens[j].type != TOKEN_EOF) j++;
        if (j < tokens->count && tokens->tokens[j].type == TOKEN_RPAREN) {
            paren_close = (int)j;
        } else continue;

        /* Collect column names */
        int col_count = 0;
        char **columns = NULL;
        for (size_t k = (size_t)paren_open + 1; k < (size_t)paren_close; k++) {
            if (tokens->tokens[k].type == TOKEN_IDENT) {
                columns = realloc(columns, ((size_t)col_count + 1) * sizeof(char *));
                columns[col_count++] = mron_strdup(tokens->tokens[k].value);
            }
        }
        if (col_count == 0) { free(columns); continue; }

        /* Expect LBRACKET after RPAREN */
        if ((size_t)paren_close + 1 >= tokens->count ||
            tokens->tokens[paren_close + 1].type != TOKEN_LBRACKET) {
            for (int k = 0; k < col_count; k++) free(columns[k]);
            free(columns);
            continue;
        }
        int bracket_open = paren_close + 1;

        /* Find matching RBRACKET */
        int bracket_close = -1;
        int depth = 1;
        j = (size_t)bracket_open + 1;
        while (j < tokens->count && depth > 0) {
            if (tokens->tokens[j].type == TOKEN_LBRACKET ||
                tokens->tokens[j].type == TOKEN_LBRACE) depth++;
            else if (tokens->tokens[j].type == TOKEN_RBRACKET ||
                     tokens->tokens[j].type == TOKEN_RBRACE) depth--;
            if (depth == 0) { bracket_close = (int)j; break; }
            j++;
        }
        if (bracket_close < 0) {
            for (int k = 0; k < col_count; k++) free(columns[k]);
            free(columns);
            continue;
        }

        /* Collect value token indices (flat values between brackets) */
        int *value_indices = NULL;
        int value_count = 0;
        for (j = (size_t)bracket_open + 1; j < (size_t)bracket_close; j++) {
            TokenType tt = tokens->tokens[j].type;
            if (tt == TOKEN_STRING || tt == TOKEN_NUMBER || tt == TOKEN_IDENT) {
                value_indices = realloc(value_indices,
                    ((size_t)value_count + 1) * sizeof(int));
                value_indices[value_count++] = (int)j;
            }
        }

        /* Store block */
        if (*out_count >= cap) {
            cap = cap == 0 ? 8 : cap * 2;
            blocks = realloc(blocks, (size_t)cap * sizeof(CsvBlockInfo));
        }
        CsvBlockInfo *b = &blocks[*out_count];
        b->key_idx = key_idx;
        b->paren_open = paren_open;
        b->paren_close = paren_close;
        b->bracket_open = bracket_open;
        b->bracket_close = bracket_close;
        b->columns = columns;
        b->col_count = col_count;
        b->value_indices = value_indices;
        b->value_count = value_count;
        (*out_count)++;

        /* Skip past this CSV block */
        i = (size_t)bracket_close;
    }
    return blocks;
}

void lsp_free_csv_blocks(CsvBlockInfo *blocks, int count) {
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < blocks[i].col_count; j++) free(blocks[i].columns[j]);
        free(blocks[i].columns);
        free(blocks[i].value_indices);
    }
    free(blocks);
}

const CsvBlockInfo *lsp_csv_block_for_token(const CsvBlockInfo *blocks,
                                            int block_count, int token_idx) {
    for (int i = 0; i < block_count; i++) {
        if (token_idx >= blocks[i].bracket_open &&
            token_idx <= blocks[i].bracket_close)
            return &blocks[i];
    }
    return NULL;
}

/* ================================================================== */
/* Diagnostics                                                         */
/* ================================================================== */

static void emit_diag(LspJson *j, int sl, int sc, int el, int ec,
                      int severity, const char *source, const char *message) {
    lj_object_start(j);
    lj_key(j, "range"); lj_range(j, sl, sc, el, ec);
    lj_key(j, "severity"); lj_int(j, severity);
    lj_key(j, "source"); lj_string(j, source);
    lj_key(j, "message"); lj_string(j, message);
    lj_object_end(j);
}

static int is_keyword(const char *v) {
    return strcmp(v, "true") == 0 || strcmp(v, "false") == 0 ||
           strcmp(v, "yes") == 0  || strcmp(v, "no") == 0 ||
           strcmp(v, "null") == 0;
}

/* Detect duplicate keys at every nesting level by scanning tokens.
   Returns the number of diagnostics emitted. */
static int check_duplicate_keys(const TokenArray *tokens, LspJson *j) {
    int found = 0;

    /* Stack of key-sets, one per nesting scope. */
    #define DK_MAX_DEPTH 64
    KeySet scopes[DK_MAX_DEPTH];
    /* first_line/first_col: where we first saw each key (for "first defined here") */
    /* We store per-scope key→line/col in parallel arrays */
    typedef struct { char *key; int line; int col; } KeyLoc;
    KeyLoc *locs[DK_MAX_DEPTH];
    int loc_counts[DK_MAX_DEPTH];
    int loc_caps[DK_MAX_DEPTH];
    int depth = 0;

    keyset_init(&scopes[0], 16);
    locs[0] = NULL; loc_counts[0] = 0; loc_caps[0] = 0;

    /* State: are we expecting a key (1) or a value (0)? */
    int expect_key = 1;

    for (size_t i = 0; i < tokens->count; i++) {
        Token *t = &tokens->tokens[i];
        if (t->type == TOKEN_EOF) break;

        /* Open scope */
        if (t->type == TOKEN_LBRACE) {
            if (depth + 1 < DK_MAX_DEPTH) {
                depth++;
                keyset_init(&scopes[depth], 16);
                locs[depth] = NULL;
                loc_counts[depth] = 0;
                loc_caps[depth] = 0;
            }
            expect_key = 1;
            continue;
        }

        /* Close scope */
        if (t->type == TOKEN_RBRACE) {
            if (depth > 0) {
                keyset_free(&scopes[depth]);
                for (int k = 0; k < loc_counts[depth]; k++)
                    free(locs[depth][k].key);
                free(locs[depth]);
                depth--;
            }
            expect_key = 1;
            continue;
        }

        /* Skip inside lists/parens — keys only exist in record contexts.
           Track bracket/paren depth to skip their contents. */
        if (t->type == TOKEN_LBRACKET || t->type == TOKEN_LPAREN) {
            int inner = 1;
            TokenType open = t->type;
            TokenType close = (open == TOKEN_LBRACKET) ? TOKEN_RBRACKET : TOKEN_RPAREN;
            i++;
            while (i < tokens->count && inner > 0) {
                if (tokens->tokens[i].type == open) inner++;
                else if (tokens->tokens[i].type == close) inner--;
                i++;
            }
            i--; /* loop will increment */
            expect_key = 1;
            continue;
        }

        if (t->type == TOKEN_IDENT && expect_key && !is_keyword(t->value)) {
            /* This looks like a key. Check for duplicate. */
            int first_line = -1;
            /* Find if this key was already seen in this scope */
            for (int k = 0; k < loc_counts[depth]; k++) {
                if (strcmp(locs[depth][k].key, t->value) == 0) {
                    first_line = locs[depth][k].line;
                    break;
                }
            }

            if (keyset_insert(&scopes[depth], t->value)) {
                /* Duplicate! */
                int l = t->line - 1;
                int c = t->col - 1;
                int len = (int)strlen(t->value);
                char msg[256];
                if (first_line >= 0) {
                    snprintf(msg, sizeof(msg),
                             "duplicate key '%s' (first defined on line %d)",
                             t->value, first_line);
                } else {
                    snprintf(msg, sizeof(msg), "duplicate key '%s'", t->value);
                }
                emit_diag(j, l, c, l, c + len, 1, "mron", msg);
                found++;
            } else {
                /* Record first occurrence location */
                if (loc_counts[depth] >= loc_caps[depth]) {
                    loc_caps[depth] = loc_caps[depth] == 0 ? 8 : loc_caps[depth] * 2;
                    locs[depth] = realloc(locs[depth],
                        (size_t)loc_caps[depth] * sizeof(KeyLoc));
                }
                locs[depth][loc_counts[depth]].key = mron_strdup(t->value);
                locs[depth][loc_counts[depth]].line = t->line;
                locs[depth][loc_counts[depth]].col = t->col;
                loc_counts[depth]++;
            }

            expect_key = 0; /* next should be a value */
            continue;
        }

        /* After seeing a value, expect a key again */
        if (!expect_key) {
            expect_key = 1;
        }
    }

    /* Cleanup remaining scopes */
    for (int d = 0; d <= depth; d++) {
        keyset_free(&scopes[d]);
        for (int k = 0; k < loc_counts[d]; k++)
            free(locs[d][k].key);
        free(locs[d]);
    }

    return found;
    #undef DK_MAX_DEPTH
}

/* Detect identifiers in value position that are not keywords. */
static int check_bad_value_idents(const TokenArray *tokens, LspJson *j) {
    int found = 0;
    int expect_key = 1;

    for (size_t i = 0; i < tokens->count; i++) {
        Token *t = &tokens->tokens[i];
        if (t->type == TOKEN_EOF) break;

        if (t->type == TOKEN_LBRACE) { expect_key = 1; continue; }
        if (t->type == TOKEN_RBRACE) { expect_key = 1; continue; }

        if (t->type == TOKEN_LBRACKET || t->type == TOKEN_LPAREN) {
            int inner = 1;
            TokenType open = t->type;
            TokenType close = (open == TOKEN_LBRACKET) ? TOKEN_RBRACKET : TOKEN_RPAREN;
            i++;
            while (i < tokens->count && inner > 0) {
                if (tokens->tokens[i].type == open) inner++;
                else if (tokens->tokens[i].type == close) inner--;
                i++;
            }
            i--;
            expect_key = 1;
            continue;
        }

        if (t->type == TOKEN_IDENT && expect_key && !is_keyword(t->value)) {
            expect_key = 0; /* this is a key, next is value */
            continue;
        }

        if (t->type == TOKEN_IDENT && !expect_key && !is_keyword(t->value)) {
            /* Non-keyword identifier in value position */
            int l = t->line - 1;
            int c = t->col - 1;
            int len = (int)strlen(t->value);
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "unexpected identifier '%s' in value position "
                     "(did you mean \"%s\"?)",
                     t->value, t->value);
            emit_diag(j, l, c, l, c + len, 1, "mron", msg);
            found++;
            expect_key = 1;
            continue;
        }

        /* Any actual value token: next should be a key */
        if (!expect_key) {
            expect_key = 1;
        }
    }
    return found;
}

void lsp_publish_diagnostics(LspDocument *doc) {
    LspJson j;
    lj_init(&j);

    lj_object_start(&j);
    lj_key(&j, "jsonrpc"); lj_string(&j, "2.0");
    lj_key(&j, "method");  lj_string(&j, "textDocument/publishDiagnostics");
    lj_key(&j, "params");  lj_object_start(&j);
      lj_key(&j, "uri"); lj_string(&j, doc->uri);
      lj_key(&j, "version"); lj_int(&j, doc->version);
      lj_key(&j, "diagnostics"); lj_array_start(&j);

      if (doc->tokens) {
          /* Lexer errors */
          for (size_t i = 0; i < doc->tokens->count; i++) {
              Token *t = &doc->tokens->tokens[i];
              if (t->type == TOKEN_ERROR) {
                  int l = t->line - 1;
                  int c = t->col - 1;
                  int len = t->value ? (int)strlen(t->value) : 1;
                  if (len < 1) len = 1;
                  emit_diag(&j, l, c, l, c + 1, 1, "mron",
                            t->value ? t->value : "syntax error");
              }
          }

          if (!doc->tokens->has_error) {
              /* Duplicate key detection (works even when parser fails) */
              int dup_count = check_duplicate_keys(doc->tokens, &j);

              /* Identifier-in-value-position detection */
              int bad_val_count = check_bad_value_idents(doc->tokens, &j);

              /* CSV column count mismatch */
              int csv_mismatch_count = 0;
              {
                  int csv_count = 0;
                  CsvBlockInfo *csv_blocks = lsp_find_csv_blocks(doc->tokens, &csv_count);
                  for (int b = 0; b < csv_count; b++) {
                      CsvBlockInfo *blk = &csv_blocks[b];
                      if (blk->col_count > 0 && blk->value_count % blk->col_count != 0) {
                          Token *kt = &doc->tokens->tokens[blk->key_idx];
                          int rows = blk->value_count / blk->col_count;
                          int extra = blk->value_count % blk->col_count;
                          /* Point at the first leftover value */
                          int first_extra_idx = blk->value_indices[rows * blk->col_count];
                          Token *et = &doc->tokens->tokens[first_extra_idx];
                          int elen = et->value ? (int)strlen(et->value) : 1;
                          if (et->type == TOKEN_STRING) elen += 2; /* quotes */
                          char msg[512];
                          snprintf(msg, sizeof(msg),
                                   "CSV list '%s' expects %d value(s) per row, "
                                   "but row %d only has %d — "
                                   "value %s%s%s starts an incomplete row (%d total values for %d columns)",
                                   kt->value, blk->col_count,
                                   rows + 1, extra,
                                   et->type == TOKEN_STRING ? "\"" : "",
                                   et->value ? et->value : "?",
                                   et->type == TOKEN_STRING ? "\"" : "",
                                   blk->value_count, blk->col_count);
                          emit_diag(&j, et->line - 1, et->col - 1,
                                        et->line - 1, et->col - 1 + elen, 1, "mron", msg);
                          csv_mismatch_count++;
                      }
                  }
                  lsp_free_csv_blocks(csv_blocks, csv_count);
              }

              /* If parser still failed and we didn't find specific issues, check structure */
              if (!doc->ast && dup_count == 0 && bad_val_count == 0 && csv_mismatch_count == 0) {
                  int braces = 0, brackets = 0, parens = 0;
                  int last_open_line = 0, last_open_col = 0;
                  int structural_error = 0;

                  for (size_t i = 0; i < doc->tokens->count; i++) {
                      Token *t = &doc->tokens->tokens[i];
                      switch (t->type) {
                      case TOKEN_LBRACE:   braces++; last_open_line = t->line; last_open_col = t->col; break;
                      case TOKEN_RBRACE:   braces--; break;
                      case TOKEN_LBRACKET: brackets++; last_open_line = t->line; last_open_col = t->col; break;
                      case TOKEN_RBRACKET: brackets--; break;
                      case TOKEN_LPAREN:   parens++; last_open_line = t->line; last_open_col = t->col; break;
                      case TOKEN_RPAREN:   parens--; break;
                      default: break;
                      }
                      if (braces < 0 || brackets < 0 || parens < 0) {
                          const char *msg = braces < 0 ? "unexpected '}' with no matching '{'" :
                                            brackets < 0 ? "unexpected ']' with no matching '['" :
                                            "unexpected ')' with no matching '('";
                          emit_diag(&j, t->line - 1, t->col - 1,
                                        t->line - 1, t->col, 1, "mron", msg);
                          structural_error = 1;
                          break;
                      }
                  }
                  if (!structural_error && (braces > 0 || brackets > 0 || parens > 0)) {
                      char msg[128];
                      if (braces > 0)
                          snprintf(msg, sizeof(msg), "unclosed '{' (opened at line %d)", last_open_line);
                      else if (brackets > 0)
                          snprintf(msg, sizeof(msg), "unclosed '[' (opened at line %d)", last_open_line);
                      else
                          snprintf(msg, sizeof(msg), "unclosed '(' (opened at line %d)", last_open_line);
                      emit_diag(&j, last_open_line - 1, last_open_col - 1,
                                    last_open_line - 1, last_open_col, 1, "mron", msg);
                      structural_error = 1;
                  }
                  if (!structural_error) {
                      /* Try to find where the parser likely choked */
                      /* Look for a number or string in key position */
                      int expect_key = 1;
                      int reported = 0;
                      for (size_t i = 0; i < doc->tokens->count && !reported; i++) {
                          Token *t = &doc->tokens->tokens[i];
                          if (t->type == TOKEN_EOF) break;
                          if (t->type == TOKEN_LBRACE) { expect_key = 1; continue; }
                          if (t->type == TOKEN_RBRACE) { expect_key = 1; continue; }
                          if (t->type == TOKEN_LBRACKET || t->type == TOKEN_LPAREN) {
                              int inner = 1;
                              TokenType open = t->type;
                              TokenType close = (open == TOKEN_LBRACKET) ? TOKEN_RBRACKET : TOKEN_RPAREN;
                              i++;
                              while (i < doc->tokens->count && inner > 0) {
                                  if (doc->tokens->tokens[i].type == open) inner++;
                                  else if (doc->tokens->tokens[i].type == close) inner--;
                                  i++;
                              }
                              i--;
                              expect_key = 1;
                              continue;
                          }
                          if (expect_key) {
                              if (t->type == TOKEN_STRING) {
                                  char msg[256];
                                  snprintf(msg, sizeof(msg),
                                           "expected a key, got string \"%s\" "
                                           "(keys must be unquoted identifiers)",
                                           t->value);
                                  emit_diag(&j, t->line - 1, t->col - 1,
                                                t->line - 1, t->col - 1 + (int)strlen(t->value) + 2,
                                                1, "mron", msg);
                                  reported = 1;
                              } else if (t->type == TOKEN_NUMBER) {
                                  char msg[256];
                                  snprintf(msg, sizeof(msg),
                                           "expected a key, got number %s "
                                           "(keys must be identifiers, not numbers)",
                                           t->value);
                                  emit_diag(&j, t->line - 1, t->col - 1,
                                                t->line - 1, t->col - 1 + (int)strlen(t->value),
                                                1, "mron", msg);
                                  reported = 1;
                              } else if (t->type == TOKEN_IDENT && !is_keyword(t->value)) {
                                  expect_key = 0;
                              } else if (t->type == TOKEN_IDENT) {
                                  expect_key = 1; /* keyword value, still expect key next */
                              } else {
                                  expect_key = 1;
                              }
                          } else {
                              expect_key = 1;
                          }
                      }
                      if (!reported) {
                          emit_diag(&j, 0, 0, 0, 1, 1, "mron", "parse error");
                      }
                  }
              }

          }

          /* Schema validation */
          if (doc->ast && schema_ast) {
              int violations = ast_validate_schema(doc->ast, schema_ast, NULL);
              if (violations > 0) {
                  char msg[128];
                  snprintf(msg, sizeof(msg), "%d schema violation(s)", violations);
                  emit_diag(&j, 0, 0, 0, 1, 2, "mron-schema", msg);
              }
          }
      }

      lj_array_end(&j);
    lj_object_end(&j);
    lj_object_end(&j);

    char *json = lj_finish(&j);
    lsp_write_message(json);
    free(json);
}

/* ================================================================== */
/* Send response/notification helpers                                  */
/* ================================================================== */

static void send_response(const AstNode *id_node, const char *result_json) {
    LspJson j;
    lj_init(&j);
    lj_object_start(&j);
    lj_key(&j, "jsonrpc"); lj_string(&j, "2.0");
    lj_key(&j, "id");
    if (id_node) {
        if (id_node->type == AST_NUMBER)
            lj_raw(&j, id_node->data.number_str);
        else if (id_node->type == AST_STRING)
            lj_string(&j, id_node->data.string_val);
        else
            lj_null(&j);
    } else {
        lj_null(&j);
    }
    lj_key(&j, "result");
    if (result_json)
        lj_raw(&j, result_json);
    else
        lj_null(&j);
    lj_object_end(&j);

    char *msg = lj_finish(&j);
    lsp_write_message(msg);
    free(msg);
}

static void send_error(const AstNode *id_node, int code, const char *message) {
    LspJson j;
    lj_init(&j);
    lj_object_start(&j);
    lj_key(&j, "jsonrpc"); lj_string(&j, "2.0");
    lj_key(&j, "id");
    if (id_node && id_node->type == AST_NUMBER)
        lj_raw(&j, id_node->data.number_str);
    else if (id_node && id_node->type == AST_STRING)
        lj_string(&j, id_node->data.string_val);
    else
        lj_null(&j);
    lj_key(&j, "error"); lj_object_start(&j);
      lj_key(&j, "code"); lj_int(&j, code);
      lj_key(&j, "message"); lj_string(&j, message);
    lj_object_end(&j);
    lj_object_end(&j);

    char *msg = lj_finish(&j);
    lsp_write_message(msg);
    free(msg);
}

/* ================================================================== */
/* Initialize handler                                                  */
/* ================================================================== */

static char *build_initialize_result(void) {
    LspJson j;
    lj_init(&j);

    lj_object_start(&j);
    lj_key(&j, "capabilities"); lj_object_start(&j);

      /* Text document sync: full content on each change */
      lj_key(&j, "textDocumentSync"); lj_object_start(&j);
        lj_key(&j, "openClose"); lj_bool(&j, 1);
        lj_key(&j, "change"); lj_int(&j, 1); /* Full */
      lj_object_end(&j);

      /* Hover */
      lj_key(&j, "hoverProvider"); lj_bool(&j, 1);

      /* Completion */
      lj_key(&j, "completionProvider"); lj_object_start(&j);
        lj_key(&j, "triggerCharacters"); lj_array_start(&j);
          lj_string(&j, "\"");
          lj_string(&j, " ");
        lj_array_end(&j);
      lj_object_end(&j);

      /* Document symbols */
      lj_key(&j, "documentSymbolProvider"); lj_bool(&j, 1);

      /* Folding ranges */
      lj_key(&j, "foldingRangeProvider"); lj_bool(&j, 1);

      /* Go to definition */
      lj_key(&j, "definitionProvider"); lj_bool(&j, 1);

      /* Code actions */
      lj_key(&j, "codeActionProvider"); lj_bool(&j, 1);

      /* Document formatting */
      lj_key(&j, "documentFormattingProvider"); lj_bool(&j, 1);

      /* Rename */
      lj_key(&j, "renameProvider"); lj_object_start(&j);
        lj_key(&j, "prepareProvider"); lj_bool(&j, 1);
      lj_object_end(&j);

      /* Document link */
      lj_key(&j, "documentLinkProvider"); lj_object_start(&j);
        lj_key(&j, "resolveProvider"); lj_bool(&j, 0);
      lj_object_end(&j);

      /* Selection range */
      lj_key(&j, "selectionRangeProvider"); lj_bool(&j, 1);

      /* Inlay hints */
      lj_key(&j, "inlayHintProvider"); lj_bool(&j, 1);

      /* Color provider */
      lj_key(&j, "colorProvider"); lj_bool(&j, 1);

      /* Code lens */
      lj_key(&j, "codeLensProvider"); lj_object_start(&j);
        lj_key(&j, "resolveProvider"); lj_bool(&j, 0);
      lj_object_end(&j);

      /* Semantic tokens */
      lj_key(&j, "semanticTokensProvider"); lj_object_start(&j);
        lj_key(&j, "legend"); lj_object_start(&j);
          lj_key(&j, "tokenTypes"); lj_array_start(&j);
            lj_string(&j, "variable");    /* 0: record keys */
            lj_string(&j, "parameter");   /* 1: CSV header columns */
            lj_string(&j, "string");      /* 2: string values */
            lj_string(&j, "number");      /* 3: number values */
            lj_string(&j, "keyword");     /* 4: true/false/yes/no/null */
            lj_string(&j, "comment");     /* 5: comments */
            lj_string(&j, "operator");    /* 6: delimiters */
          lj_array_end(&j);
          lj_key(&j, "tokenModifiers"); lj_array_start(&j);
          lj_array_end(&j);
        lj_object_end(&j);
        lj_key(&j, "full"); lj_bool(&j, 1);
      lj_object_end(&j);

    lj_object_end(&j); /* capabilities */

    lj_key(&j, "serverInfo"); lj_object_start(&j);
      lj_key(&j, "name"); lj_string(&j, "mron-lsp");
      lj_key(&j, "version"); lj_string(&j, "0.1.0");
    lj_object_end(&j);

    lj_object_end(&j);

    return lj_finish(&j);
}

/* ================================================================== */
/* Message dispatch                                                    */
/* ================================================================== */

int lsp_handle_message(const char *json) {
    /* Parse the JSON-RPC message using our JSON parser */
    AstNode *msg = json_parse(json, "<lsp>");
    if (!msg) {
        lsp_log("Failed to parse incoming message");
        return 0;
    }

    const char *method = lsp_obj_string(msg, "method");
    const AstNode *id_node = lsp_obj_get(msg, "id");
    const AstNode *params = lsp_obj_get(msg, "params");

    if (!method) {
        /* Response to a request we sent (or malformed) - ignore */
        ast_free(msg);
        return 0;
    }

    lsp_log("Received: %s", method);

    /* ----- Lifecycle ----- */

    if (strcmp(method, "initialize") == 0) {
        /* Check for schema setting in initializationOptions */
        if (params) {
            const AstNode *opts = lsp_obj_get(params, "initializationOptions");
            const char *sp = lsp_obj_string(opts, "schema");
            if (sp) {
                free(schema_path);
                schema_path = mron_strdup(sp);
                char *src = read_file(schema_path);
                if (src) {
                    TokenArray *st = lexer_tokenize(src, schema_path);
                    if (st && !st->has_error) {
                        schema_ast = parser_parse(st);
                    }
                    if (st) lexer_free(st);
                    free(src);
                }
                if (schema_ast) lsp_log("Loaded schema: %s", schema_path);
            }
        }
        char *result = build_initialize_result();
        send_response(id_node, result);
        free(result);
        server_initialized = 1;
        ast_free(msg);
        return 0;
    }

    if (strcmp(method, "initialized") == 0) {
        /* Client acknowledged initialization - nothing to do */
        ast_free(msg);
        return 0;
    }

    if (strcmp(method, "shutdown") == 0) {
        send_response(id_node, "null");
        ast_free(msg);
        return 0;
    }

    if (strcmp(method, "exit") == 0) {
        ast_free(msg);
        return 1; /* Signal exit */
    }

    /* ----- Document sync ----- */

    if (strcmp(method, "textDocument/didOpen") == 0) {
        const AstNode *td = lsp_obj_get(params, "textDocument");
        const char *uri = lsp_obj_string(td, "uri");
        const char *text = lsp_obj_string(td, "text");
        int version = lsp_obj_int(td, "version", 0);
        if (uri && text) {
            LspDocument *doc = lsp_doc_open(uri, text, version);
            if (doc) lsp_publish_diagnostics(doc);
        }
        ast_free(msg);
        return 0;
    }

    if (strcmp(method, "textDocument/didChange") == 0) {
        const AstNode *td = lsp_obj_get(params, "textDocument");
        const char *uri = lsp_obj_string(td, "uri");
        int version = lsp_obj_int(td, "version", 0);
        /* Full sync: take last contentChanges entry */
        const AstNode *changes = lsp_obj_get(params, "contentChanges");
        if (changes && changes->type == AST_LIST && changes->data.list.count > 0) {
            const AstNode *last = changes->data.list.items[changes->data.list.count - 1];
            const char *text = lsp_obj_string(last, "text");
            LspDocument *doc = lsp_doc_find(uri);
            if (doc && text) {
                lsp_doc_update(doc, text, version);
                lsp_publish_diagnostics(doc);
            }
        }
        ast_free(msg);
        return 0;
    }

    if (strcmp(method, "textDocument/didClose") == 0) {
        const AstNode *td = lsp_obj_get(params, "textDocument");
        const char *uri = lsp_obj_string(td, "uri");
        if (uri) lsp_doc_close(uri);
        ast_free(msg);
        return 0;
    }

    /* ----- Feature requests ----- */

    /* Helper: get document from params.textDocument.uri */
    const char *uri = NULL;
    LspDocument *doc = NULL;
    if (params) {
        const AstNode *td = lsp_obj_get(params, "textDocument");
        uri = lsp_obj_string(td, "uri");
        if (uri) doc = lsp_doc_find(uri);
    }

    if (strcmp(method, "textDocument/documentSymbol") == 0) {
        char *result = doc ? lsp_handle_document_symbol(doc) : mron_strdup("[]");
        send_response(id_node, result);
        free(result);
    }
    else if (strcmp(method, "textDocument/foldingRange") == 0) {
        char *result = doc ? lsp_handle_folding_range(doc) : mron_strdup("[]");
        send_response(id_node, result);
        free(result);
    }
    else if (strcmp(method, "textDocument/hover") == 0) {
        const AstNode *pos = lsp_obj_get(params, "position");
        int line = lsp_obj_int(pos, "line", 0);
        int col = lsp_obj_int(pos, "character", 0);
        char *result = doc ? lsp_handle_hover(doc, line, col) : NULL;
        send_response(id_node, result ? result : "null");
        free(result);
    }
    else if (strcmp(method, "textDocument/definition") == 0) {
        const AstNode *pos = lsp_obj_get(params, "position");
        int line = lsp_obj_int(pos, "line", 0);
        int col = lsp_obj_int(pos, "character", 0);
        char *result = doc ? lsp_handle_definition(doc, line, col) : NULL;
        send_response(id_node, result ? result : "null");
        free(result);
    }
    else if (strcmp(method, "textDocument/completion") == 0) {
        const AstNode *pos = lsp_obj_get(params, "position");
        int line = lsp_obj_int(pos, "line", 0);
        int col = lsp_obj_int(pos, "character", 0);
        char *result = doc ? lsp_handle_completion(doc, line, col) : mron_strdup("[]");
        send_response(id_node, result);
        free(result);
    }
    else if (strcmp(method, "textDocument/codeAction") == 0) {
        const AstNode *range = lsp_obj_get(params, "range");
        const AstNode *start = lsp_obj_get(range, "start");
        const AstNode *end = lsp_obj_get(range, "end");
        int sl = lsp_obj_int(start, "line", 0);
        int sc = lsp_obj_int(start, "character", 0);
        int el = lsp_obj_int(end, "line", 0);
        int ec = lsp_obj_int(end, "character", 0);
        char *result = doc ? lsp_handle_code_action(doc, sl, sc, el, ec) : mron_strdup("[]");
        send_response(id_node, result);
        free(result);
    }
    else if (strcmp(method, "textDocument/semanticTokens/full") == 0) {
        char *result = doc ? lsp_handle_semantic_tokens(doc) : mron_strdup("{\"data\":[]}");
        send_response(id_node, result);
        free(result);
    }
    else if (strcmp(method, "textDocument/formatting") == 0) {
        const AstNode *options = lsp_obj_get(params, "options");
        int indent = lsp_obj_int(options, "tabSize", 4);
        char *result = doc ? lsp_handle_formatting(doc, indent) : mron_strdup("[]");
        send_response(id_node, result);
        free(result);
    }
    else if (strcmp(method, "textDocument/rename") == 0) {
        const AstNode *pos = lsp_obj_get(params, "position");
        int line = lsp_obj_int(pos, "line", 0);
        int col = lsp_obj_int(pos, "character", 0);
        const char *new_name = lsp_obj_string(params, "newName");
        char *result = (doc && new_name) ?
            lsp_handle_rename(doc, line, col, new_name) : mron_strdup("null");
        send_response(id_node, result);
        free(result);
    }
    else if (strcmp(method, "textDocument/prepareRename") == 0) {
        /* Return the range of the token under cursor */
        const AstNode *pos = lsp_obj_get(params, "position");
        int line = lsp_obj_int(pos, "line", 0);
        int col = lsp_obj_int(pos, "character", 0);
        if (doc && doc->tokens) {
            int ti = lsp_token_at_pos(doc->tokens, line, col);
            if (ti >= 0) {
                Token *t = &doc->tokens->tokens[ti];
                if (t->type == TOKEN_IDENT) {
                    LspJson r; lj_init(&r);
                    lj_range(&r, t->line - 1, t->col - 1,
                                 t->line - 1, t->col - 1 + lsp_token_length(t));
                    char *result = lj_finish(&r);
                    send_response(id_node, result);
                    free(result);
                    ast_free(msg);
                    return 0;
                }
            }
        }
        send_error(id_node, -32600, "Cannot rename this element");
    }
    else if (strcmp(method, "textDocument/documentLink") == 0) {
        char *result = doc ? lsp_handle_document_link(doc) : mron_strdup("[]");
        send_response(id_node, result);
        free(result);
    }
    else if (strcmp(method, "textDocument/selectionRange") == 0) {
        const AstNode *positions = lsp_obj_get(params, "positions");
        if (doc && positions && positions->type == AST_LIST) {
            int count = (int)positions->data.list.count;
            int *lines = malloc((size_t)count * sizeof(int));
            int *cols = malloc((size_t)count * sizeof(int));
            for (int i = 0; i < count; i++) {
                lines[i] = lsp_obj_int(positions->data.list.items[i], "line", 0);
                cols[i] = lsp_obj_int(positions->data.list.items[i], "character", 0);
            }
            char *result = lsp_handle_selection_range(doc, lines, cols, count);
            send_response(id_node, result);
            free(result); free(lines); free(cols);
        } else {
            send_response(id_node, "[]");
        }
    }
    else if (strcmp(method, "textDocument/inlayHint") == 0) {
        const AstNode *range = lsp_obj_get(params, "range");
        int sl = 0, el = 9999;
        if (range) {
            sl = lsp_obj_int(lsp_obj_get(range, "start"), "line", 0);
            el = lsp_obj_int(lsp_obj_get(range, "end"), "line", 9999);
        }
        char *result = doc ? lsp_handle_inlay_hint(doc, sl, el) : mron_strdup("[]");
        send_response(id_node, result);
        free(result);
    }
    else if (strcmp(method, "textDocument/documentColor") == 0) {
        char *result = doc ? lsp_handle_document_color(doc) : mron_strdup("[]");
        send_response(id_node, result);
        free(result);
    }
    else if (strcmp(method, "textDocument/colorPresentation") == 0) {
        /* Return the color as a hex string */
        const AstNode *color = lsp_obj_get(params, "color");
        if (color) {
            double r = 0, g = 0, b = 0, a = 1;
            const AstNode *rn = lsp_obj_get(color, "red");
            const AstNode *gn = lsp_obj_get(color, "green");
            const AstNode *bn = lsp_obj_get(color, "blue");
            const AstNode *an = lsp_obj_get(color, "alpha");
            if (rn && rn->type == AST_NUMBER) r = atof(rn->data.number_str);
            if (gn && gn->type == AST_NUMBER) g = atof(gn->data.number_str);
            if (bn && bn->type == AST_NUMBER) b = atof(bn->data.number_str);
            if (an && an->type == AST_NUMBER) a = atof(an->data.number_str);
            char hex[16];
            if (a < 1.0) {
                snprintf(hex, sizeof(hex), "#%02x%02x%02x%02x",
                    (int)(r*255), (int)(g*255), (int)(b*255), (int)(a*255));
            } else {
                snprintf(hex, sizeof(hex), "#%02x%02x%02x",
                    (int)(r*255), (int)(g*255), (int)(b*255));
            }
            LspJson rj; lj_init(&rj);
            lj_array_start(&rj);
            lj_object_start(&rj);
              lj_key(&rj, "label"); lj_string(&rj, hex);
            lj_object_end(&rj);
            lj_array_end(&rj);
            char *result = lj_finish(&rj);
            send_response(id_node, result);
            free(result);
        } else {
            send_response(id_node, "[]");
        }
    }
    else if (strcmp(method, "textDocument/codeLens") == 0) {
        char *result = doc ? lsp_handle_code_lens(doc) : mron_strdup("[]");
        send_response(id_node, result);
        free(result);
    }
    else {
        /* Unknown method */
        if (id_node) {
            send_error(id_node, -32601, "Method not found");
        }
        /* Notifications without id are silently ignored */
    }

    ast_free(msg);
    return 0;
}
