#ifndef MRON_LEXER_H
#define MRON_LEXER_H

#include <stddef.h>

typedef enum {
    TOKEN_IDENT,
    TOKEN_STRING,
    TOKEN_NUMBER,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_EOF,
    TOKEN_ERROR,
} TokenType;

typedef struct {
    TokenType type;
    char *value;       /* raw or processed token text */
    int line;
    int col;
} Token;

typedef struct {
    Token *tokens;
    size_t count;
    size_t capacity;
    const char *filename;
    int has_error;
} TokenArray;

/* Tokenize MRON source. Returns NULL on allocation failure. */
TokenArray *lexer_tokenize(const char *source, const char *filename);
void lexer_free(TokenArray *arr);
const char *token_type_name(TokenType type);

#endif
