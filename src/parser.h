#ifndef MRON_PARSER_H
#define MRON_PARSER_H

#include "ast.h"
#include "lexer.h"

/* Parse MRON token stream into an AST. Returns NULL on error. */
AstNode *parser_parse(TokenArray *tokens);

#endif
