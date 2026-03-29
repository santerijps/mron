#ifndef MRON_MRON_EMIT_H
#define MRON_MRON_EMIT_H

#include "ast.h"

/* Emit formatted MRON from an AST. Caller must free the returned string. */
char *mron_emit(AstNode *root);

#endif
