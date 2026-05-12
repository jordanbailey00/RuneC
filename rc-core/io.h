#ifndef RC_IO_H
#define RC_IO_H

#include <stdbool.h>
#include <stdio.h>

static inline bool rc_read_exact(FILE *f, void *dst, size_t elem_size,
                                 size_t elem_count, const char *path,
                                 const char *what) {
    if (fread(dst, elem_size, elem_count, f) != elem_count) {
        fprintf(stderr, "%s: short read while loading %s\n", path, what);
        return false;
    }
    return true;
}

static inline bool rc_seek(FILE *f, long offset, int origin,
                           const char *path, const char *what) {
    if (fseek(f, offset, origin) != 0) {
        fprintf(stderr, "%s: seek failed while loading %s\n", path, what);
        return false;
    }
    return true;
}

static inline long rc_file_size(FILE *f, const char *path,
                                const char *what) {
    if (!rc_seek(f, 0, SEEK_END, path, what)) return -1;
    long size = ftell(f);
    if (size < 0) {
        fprintf(stderr, "%s: ftell failed while loading %s\n", path, what);
        return -1;
    }
    if (!rc_seek(f, 0, SEEK_SET, path, what)) return -1;
    return size;
}

#endif
