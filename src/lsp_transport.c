#include "lsp_transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

void lsp_transport_init(void) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    /* Disable buffering on stdout for immediate writes */
    setvbuf(stdout, NULL, _IONBF, 0);
}

char *lsp_read_message(void) {
    /* Read headers until empty line */
    int content_length = -1;
    char header[1024];

    for (;;) {
        if (!fgets(header, sizeof(header), stdin)) return NULL;

        /* Empty line (just \r\n) signals end of headers */
        if (strcmp(header, "\r\n") == 0 || strcmp(header, "\n") == 0) break;

        if (strncmp(header, "Content-Length:", 15) == 0) {
            content_length = atoi(header + 15);
        }
    }

    if (content_length <= 0) return NULL;

    char *buf = malloc((size_t)content_length + 1);
    if (!buf) return NULL;

    size_t total = 0;
    while (total < (size_t)content_length) {
        size_t n = fread(buf + total, 1, (size_t)content_length - total, stdin);
        if (n == 0) { free(buf); return NULL; }
        total += n;
    }
    buf[content_length] = '\0';
    return buf;
}

void lsp_write_message(const char *json) {
    size_t len = strlen(json);
    fprintf(stdout, "Content-Length: %zu\r\n\r\n", len);
    fwrite(json, 1, len, stdout);
    fflush(stdout);
}

void lsp_log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[mron-lsp] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    fflush(stderr);
}
