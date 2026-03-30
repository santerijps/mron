#ifndef MRON_YAML_EMIT_H
#define MRON_YAML_EMIT_H

#include "ast.h"

/* Emit YAML from an AST. Caller must free the returned string. */
char *yaml_emit(AstNode *root);

#endif
