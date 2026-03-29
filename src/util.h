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
void sb_ensure(StringBuilder *sb, size_t needed);
void sb_append(StringBuilder *sb, const char *str);
void sb_append_n(StringBuilder *sb, const char *str, size_t n);
void sb_append_indent(StringBuilder *sb, int depth);
char *sb_finish(StringBuilder *sb);
void sb_free(StringBuilder *sb);

/* Inline fast-path for single character append */
static inline void sb_append_char(StringBuilder *sb, char c) {
    if (sb->len + 2 > sb->cap) {
        sb_ensure(sb, 1);
    }
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
}

/* File I/O */
char *read_file(const char *path);

/* String utility */
char *mron_strdup(const char *s);

/* Simple hash set for duplicate key detection */
typedef struct KeySetEntry {
    const char *key;
    unsigned int hash;
    struct KeySetEntry *next;
} KeySetEntry;

typedef struct {
    KeySetEntry **buckets;
    size_t bucket_count;
    size_t count;
} KeySet;

void keyset_init(KeySet *ks, size_t initial_cap);
int keyset_insert(KeySet *ks, const char *key);  /* returns 0 if new, 1 if duplicate */
void keyset_free(KeySet *ks);

/* Error reporting */
void mron_error(const char *filename, int line, int col, const char *fmt, ...);

#endif
