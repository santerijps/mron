#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define SB_INIT_CAP 256

void sb_init(StringBuilder *sb) {
    sb->cap = SB_INIT_CAP;
    sb->len = 0;
    sb->data = malloc(sb->cap);
    if (!sb->data) {
        fprintf(stderr, "error: out of memory\n");
        exit(1);
    }
    sb->data[0] = '\0';
}

static void sb_grow(StringBuilder *sb, size_t needed) {
    if (sb->len + needed + 1 > sb->cap) {
        while (sb->len + needed + 1 > sb->cap) {
            sb->cap *= 2;
        }
        sb->data = realloc(sb->data, sb->cap);
        if (!sb->data) {
            fprintf(stderr, "error: out of memory\n");
            exit(1);
        }
    }
}

void sb_append(StringBuilder *sb, const char *str) {
    size_t slen = strlen(str);
    sb_grow(sb, slen);
    memcpy(sb->data + sb->len, str, slen);
    sb->len += slen;
    sb->data[sb->len] = '\0';
}

void sb_append_char(StringBuilder *sb, char c) {
    sb_grow(sb, 1);
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
}

void sb_append_n(StringBuilder *sb, const char *str, size_t n) {
    sb_grow(sb, n);
    memcpy(sb->data + sb->len, str, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

void sb_append_indent(StringBuilder *sb, int depth) {
    for (int i = 0; i < depth; i++) {
        sb_append(sb, "    ");
    }
}

char *sb_finish(StringBuilder *sb) {
    char *result = sb->data;
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
    return result;
}

void sb_free(StringBuilder *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: cannot open file '%s'\n", path);
        return NULL;
    }
#ifdef _WIN32
    _fseeki64(f, 0, SEEK_END);
    long long size = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
#else
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
#endif
    if (size < 0) {
        fprintf(stderr, "error: cannot determine size of '%s'\n", path);
        fclose(f);
        return NULL;
    }
    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fprintf(stderr, "error: out of memory reading '%s'\n", path);
        fclose(f);
        return NULL;
    }
    size_t nread = fread(buf, 1, (size_t)size, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}

char *mron_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *dup = malloc(len + 1);
    if (!dup) {
        fprintf(stderr, "error: out of memory\n");
        exit(1);
    }
    memcpy(dup, s, len + 1);
    return dup;
}

void mron_error(const char *filename, int line, int col, const char *fmt, ...) {
    fprintf(stderr, "error: ");
    if (filename) {
        fprintf(stderr, "%s:", filename);
        if (line > 0) {
            fprintf(stderr, "%d:", line);
            if (col > 0) {
                fprintf(stderr, "%d:", col);
            }
        }
        fprintf(stderr, " ");
    }
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}
