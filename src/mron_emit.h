#ifndef MRON_MRON_EMIT_H
#define MRON_MRON_EMIT_H

#include "ast.h"

/* Emit formatted MRON from an AST. Caller must free the returned string. */
char *mron_emit(AstNode *root);

/* Emit formatted MRON with custom indent width (spaces per level). */
char *mron_emit_indent(AstNode *root, int indent_width);

/* Emit minified MRON (no alignment, minimal whitespace, no comments). */
char *mron_emit_minify(AstNode *root);

#endif
