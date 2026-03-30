#ifndef MRON_AST_OPS_H
#define MRON_AST_OPS_H

#include <stdio.h>
#include "ast.h"

/* Deep-clone an AST tree. Caller must free the result with ast_free(). */
AstNode *ast_clone(const AstNode *node);

/* Sort all record keys alphabetically, recursively. Modifies in-place. */
void ast_sort_keys(AstNode *node);

/* Deep-merge 'overlay' into 'base'. Returns a new tree. Both inputs unchanged.
   - Records: overlay keys override base keys; new keys are appended.
   - Non-record overlay values replace base entirely. */
AstNode *ast_merge(const AstNode *base, const AstNode *overlay);

/* Query a value by dot-path (e.g. "server.port", "items.0").
   Returns a borrowed pointer into the tree (do NOT free), or NULL if not found.
   Supports record keys and integer list indices. */
const AstNode *ast_query(const AstNode *root, const char *path);

/* Semantic diff between two AST trees. Prints differences to 'out'.
   Returns the number of differences found (0 = identical). */
int ast_diff(const AstNode *a, const AstNode *b, FILE *out);

/* Schema validation: check that 'data' conforms to the structure of 'schema'.
   Schema is an AST where leaf string values act as type constraints:
     "string", "number", "bool", "any" — or any value means "any".
   Records in schema mean the data must have a record with those keys.
   Lists in schema with one element mean all data items must match that element type.
   Prints violations to 'out'. Returns count of violations (0 = valid). */
int ast_validate_schema(const AstNode *data, const AstNode *schema, FILE *out);

#endif
