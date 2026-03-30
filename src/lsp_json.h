#ifndef MRON_LSP_JSON_H
#define MRON_LSP_JSON_H

#include "util.h"

/*
 * Streaming JSON builder for constructing LSP protocol messages.
 * Uses a stack to track comma insertion in nested objects/arrays.
 */
typedef struct {
    StringBuilder sb;
    int first[64];   /* stack of "is first item in container" flags */
    int depth;
} LspJson;

void lj_init(LspJson *j);
char *lj_finish(LspJson *j);   /* returns malloc'd JSON string */

void lj_object_start(LspJson *j);
void lj_object_end(LspJson *j);
void lj_array_start(LspJson *j);
void lj_array_end(LspJson *j);

void lj_key(LspJson *j, const char *name);

void lj_string(LspJson *j, const char *val);
void lj_string_len(LspJson *j, const char *val, size_t len);
void lj_int(LspJson *j, int val);
void lj_bool(LspJson *j, int val);
void lj_null(LspJson *j);
void lj_raw(LspJson *j, const char *json);  /* embed pre-built JSON */

/* Convenience: build an LSP Position object {line, character} */
void lj_position(LspJson *j, int line, int character);
/* Convenience: build an LSP Range object */
void lj_range(LspJson *j, int sl, int sc, int el, int ec);

#endif
