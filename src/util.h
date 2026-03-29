#ifndef MRON_UTIL_H
#define MRON_UTIL_H

#include <stddef.h>

/* Dynamic string builder */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StringBuilder;

void sb_init(StringBuilder *sb);
void sb_append(StringBuilder *sb, const char *str);
void sb_append_char(StringBuilder *sb, char c);
void sb_append_n(StringBuilder *sb, const char *str, size_t n);
void sb_append_indent(StringBuilder *sb, int depth);
char *sb_finish(StringBuilder *sb);
void sb_free(StringBuilder *sb);

/* File I/O */
char *read_file(const char *path);

/* String utility */
char *mron_strdup(const char *s);

/* Error reporting */
void mron_error(const char *filename, int line, int col, const char *fmt, ...);

#endif
