#ifndef MRON_JSON_EMIT_H
#define MRON_JSON_EMIT_H

#include "ast.h"

/* Emit compact JSON from an AST. Caller must free the returned string. */
char *json_emit(AstNode *root);

/* Emit pretty-printed JSON (2-space indent). Caller must free the returned string. */
char *json_emit_pretty(AstNode *root);

#endif
