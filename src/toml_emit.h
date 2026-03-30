#ifndef MRON_TOML_EMIT_H
#define MRON_TOML_EMIT_H

#include "ast.h"

/* Emit TOML from an AST. Caller must free the returned string. */
char *toml_emit(AstNode *root);

#endif
