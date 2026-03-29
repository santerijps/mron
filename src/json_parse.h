#ifndef MRON_JSON_PARSE_H
#define MRON_JSON_PARSE_H

#include "ast.h"

/* Parse a JSON string into an AST. Returns NULL on error. */
AstNode *json_parse(const char *source, const char *filename);

#endif
