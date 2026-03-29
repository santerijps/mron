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

void sb_ensure(StringBuilder *sb, size_t needed) {
    size_t required = sb->len + needed + 1;
    if (required > sb->cap) {
        size_t new_cap = sb->cap;
        while (new_cap < required) new_cap *= 2;
        sb->data = realloc(sb->data, new_cap);
        if (!sb->data) {
            fprintf(stderr, "error: out of memory\n");
            exit(1);
        }
        sb->cap = new_cap;
    }
}

static void sb_grow(StringBuilder *sb, size_t needed) {
    sb_ensure(sb, needed);
}

void sb_append(StringBuilder *sb, const char *str) {
    size_t slen = strlen(str);
    sb_grow(sb, slen);
    memcpy(sb->data + sb->len, str, slen);
    sb->len += slen;
    sb->data[sb->len] = '\0';
}

/* sb_append_char is now inline in util.h */

void sb_append_n(StringBuilder *sb, const char *str, size_t n) {
    sb_grow(sb, n);
    memcpy(sb->data + sb->len, str, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

void sb_append_indent(StringBuilder *sb, int depth) {
    size_t total = (size_t)depth * 4;
    if (total == 0) return;
    sb_grow(sb, total);
    memset(sb->data + sb->len, ' ', total);
    sb->len += total;
    sb->data[sb->len] = '\0';
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

/* --- KeySet (hash set for duplicate key detection) --- */

static unsigned int keyset_hash(const char *key) {
    unsigned int h = 5381;
    for (const char *p = key; *p; p++) {
        h = ((h << 5) + h) ^ (unsigned char)*p;
    }
    return h;
}

void keyset_init(KeySet *ks, size_t initial_cap) {
    if (initial_cap < 16) initial_cap = 16;
    ks->buckets = calloc(initial_cap, sizeof(KeySetEntry *));
    if (!ks->buckets) {
        fprintf(stderr, "error: out of memory\n");
        exit(1);
    }
    ks->bucket_count = initial_cap;
    ks->count = 0;
}

static void keyset_rehash(KeySet *ks) {
    size_t new_cap = ks->bucket_count * 2;
    KeySetEntry **new_buckets = calloc(new_cap, sizeof(KeySetEntry *));
    if (!new_buckets) {
        fprintf(stderr, "error: out of memory\n");
        exit(1);
    }
    for (size_t i = 0; i < ks->bucket_count; i++) {
        KeySetEntry *e = ks->buckets[i];
        while (e) {
            KeySetEntry *next = e->next;
            size_t idx = e->hash & (new_cap - 1);
            e->next = new_buckets[idx];
            new_buckets[idx] = e;
            e = next;
        }
    }
    free(ks->buckets);
    ks->buckets = new_buckets;
    ks->bucket_count = new_cap;
}

int keyset_insert(KeySet *ks, const char *key) {
    unsigned int h = keyset_hash(key);
    size_t idx = h & (ks->bucket_count - 1);
    for (KeySetEntry *e = ks->buckets[idx]; e; e = e->next) {
        if (e->hash == h && strcmp(e->key, key) == 0) {
            return 1; /* duplicate */
        }
    }
    /* Insert new entry */
    KeySetEntry *entry = malloc(sizeof(KeySetEntry));
    if (!entry) {
        fprintf(stderr, "error: out of memory\n");
        exit(1);
    }
    entry->key = mron_strdup(key); /* owns a copy */
    entry->hash = h;
    entry->next = ks->buckets[idx];
    ks->buckets[idx] = entry;
    ks->count++;
    if (ks->count > ks->bucket_count) {
        keyset_rehash(ks);
    }
    return 0; /* new key */
}

void keyset_free(KeySet *ks) {
    for (size_t i = 0; i < ks->bucket_count; i++) {
        KeySetEntry *e = ks->buckets[i];
        while (e) {
            KeySetEntry *next = e->next;
            free((char *)e->key);
            free(e);
            e = next;
        }
    }
    free(ks->buckets);
    ks->buckets = NULL;
    ks->bucket_count = 0;
    ks->count = 0;
}
