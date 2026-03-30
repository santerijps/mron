#ifndef MRON_LSP_TRANSPORT_H
#define MRON_LSP_TRANSPORT_H

/* Initialize transport (binary mode on Windows). */
void lsp_transport_init(void);

/* Read a JSON-RPC message from stdin.
   Returns malloc'd JSON string, or NULL on EOF/error. */
char *lsp_read_message(void);

/* Write a JSON-RPC message to stdout with Content-Length header. */
void lsp_write_message(const char *json);

/* Write to the LSP log (stderr). */
void lsp_log(const char *fmt, ...);

#endif
