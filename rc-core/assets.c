#include "assets.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#define RC_PACK_MAGIC "RCPK0002"
#define RC_INDEX_MAGIC "RCPI0001"
#define RC_PACK_HEADER_SIZE 64
#define RC_ASSET_IO_CHUNK 65536

typedef struct {
    char *path;
    char *pack_path;
    uint64_t offset;
    uint64_t size;
    uint64_t packed_size;
    unsigned char sha256[32];
    unsigned char compression;
} RcPackEntry;

typedef struct {
    unsigned char *data;
    size_t size;
    size_t pos;
} RcIndexReader;

static RcPackEntry *g_entries;
static size_t g_entry_count;
static size_t g_entry_cap;
static int g_initialized;
static RcAssetBackend g_backend = RC_ASSET_BACKEND_AUTO;
static char g_data_root[1024] = "data";
static char g_pack_dir[1024] = "data/packs";

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    unsigned char data[64];
    size_t datalen;
} SHA256_CTX;

static uint32_t rc_rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32u - n));
}

static void SHA256_Transform(SHA256_CTX *ctx, const unsigned char data[64]) {
    static const uint32_t k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };
    uint32_t m[64];
    for (int i = 0; i < 16; i++) {
        m[i] = ((uint32_t)data[i * 4] << 24)
             | ((uint32_t)data[i * 4 + 1] << 16)
             | ((uint32_t)data[i * 4 + 2] << 8)
             | ((uint32_t)data[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rc_rotr32(m[i - 15], 7) ^ rc_rotr32(m[i - 15], 18)
                    ^ (m[i - 15] >> 3);
        uint32_t s1 = rc_rotr32(m[i - 2], 17) ^ rc_rotr32(m[i - 2], 19)
                    ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }

    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2];
    uint32_t d = ctx->state[3], e = ctx->state[4], f = ctx->state[5];
    uint32_t g = ctx->state[6], h = ctx->state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t s1 = rc_rotr32(e, 6) ^ rc_rotr32(e, 11) ^ rc_rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + k[i] + m[i];
        uint32_t s0 = rc_rotr32(a, 2) ^ rc_rotr32(a, 13) ^ rc_rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c;
    ctx->state[3] += d; ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

static void SHA256_Init(SHA256_CTX *ctx) {
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
}

static void SHA256_Update(SHA256_CTX *ctx, const void *data, size_t len) {
    const unsigned char *bytes = (const unsigned char *)data;
    for (size_t i = 0; i < len; i++) {
        ctx->data[ctx->datalen++] = bytes[i];
        if (ctx->datalen == 64) {
            SHA256_Transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void SHA256_Final(unsigned char hash[32], SHA256_CTX *ctx) {
    size_t i = ctx->datalen;
    ctx->data[i++] = 0x80;
    if (i > 56) {
        while (i < 64) ctx->data[i++] = 0;
        SHA256_Transform(ctx, ctx->data);
        i = 0;
    }
    while (i < 56) ctx->data[i++] = 0;
    ctx->bitlen += ctx->datalen * 8;
    for (int j = 0; j < 8; j++)
        ctx->data[63 - j] = (unsigned char)(ctx->bitlen >> (j * 8));
    SHA256_Transform(ctx, ctx->data);
    for (i = 0; i < 4; i++) {
        for (int j = 0; j < 8; j++)
            hash[j * 4 + i] = (unsigned char)(ctx->state[j] >> (24 - i * 8));
    }
}

static char *rc_asset_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *out = (char *)malloc(n);
    if (out) memcpy(out, s, n);
    return out;
}

static int rc_has_prefix(const char *s, const char *prefix) {
    size_t n = strlen(prefix);
    return strncmp(s, prefix, n) == 0;
}

const char *rc_asset_logical_path(const char *path) {
    if (!path) return "";
    while (rc_has_prefix(path, "./")) path += 2;
    if (rc_has_prefix(path, "data/")) path += 5;
    const char *data_part = strstr(path, "/data/");
    if (data_part) path = data_part + 6;
    return path;
}

static int rc_path_is_absolute(const char *path) {
    return path && path[0] == '/';
}

static int rc_file_exists_path(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int rc_join_path(char *out, size_t cap, const char *a, const char *b) {
    if (!out || cap == 0 || !a || !b) return 0;
    int n = snprintf(out, cap, "%s/%s", a, b);
    return n > 0 && (size_t)n < cap;
}

static int rc_loose_path(char *out, size_t cap, const char *path) {
    if (!path || !path[0]) return 0;
    if (rc_path_is_absolute(path)) {
        int n = snprintf(out, cap, "%s", path);
        return n > 0 && (size_t)n < cap;
    }
    return rc_join_path(out, cap, g_data_root, rc_asset_logical_path(path));
}

static uint16_t rc_read_le16(const unsigned char *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint64_t rc_read_le64(const unsigned char *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

static int rc_index_take(RcIndexReader *r, void *dst, size_t n) {
    if (!r || r->pos + n > r->size) return 0;
    if (dst) memcpy(dst, r->data + r->pos, n);
    r->pos += n;
    return 1;
}

static int rc_index_u16(RcIndexReader *r, uint16_t *out) {
    unsigned char b[2];
    if (!rc_index_take(r, b, sizeof(b))) return 0;
    *out = rc_read_le16(b);
    return 1;
}

static int rc_index_u64(RcIndexReader *r, uint64_t *out) {
    unsigned char b[8];
    if (!rc_index_take(r, b, sizeof(b))) return 0;
    *out = rc_read_le64(b);
    return 1;
}

static int rc_entry_cmp(const void *a, const void *b) {
    const RcPackEntry *ea = (const RcPackEntry *)a;
    const RcPackEntry *eb = (const RcPackEntry *)b;
    return strcmp(ea->path, eb->path);
}

static int rc_string_cmp(const void *a, const void *b) {
    const char *const *sa = (const char *const *)a;
    const char *const *sb = (const char *const *)b;
    return strcmp(*sa, *sb);
}

static void rc_entries_clear(void) {
    for (size_t i = 0; i < g_entry_count; i++) {
        free(g_entries[i].path);
        free(g_entries[i].pack_path);
    }
    free(g_entries);
    g_entries = NULL;
    g_entry_count = 0;
    g_entry_cap = 0;
}

void rc_asset_reset(void) {
    rc_entries_clear();
    g_initialized = 0;
}

void rc_asset_set_backend(RcAssetBackend backend) {
    g_backend = backend;
    rc_asset_reset();
}

void rc_asset_set_data_root(const char *root) {
    if (root && root[0]) {
        snprintf(g_data_root, sizeof(g_data_root), "%s", root);
        if (strcmp(g_pack_dir, "data/packs") == 0)
            snprintf(g_pack_dir, sizeof(g_pack_dir), "%s/packs", root);
    }
    rc_asset_reset();
}

void rc_asset_set_pack_dir(const char *dir) {
    if (dir && dir[0]) snprintf(g_pack_dir, sizeof(g_pack_dir), "%s", dir);
    rc_asset_reset();
}

static void rc_asset_apply_env(void) {
    const char *root = getenv("RUNEC_DATA_ROOT");
    if (root && root[0]) snprintf(g_data_root, sizeof(g_data_root), "%s", root);
    const char *pack_dir = getenv("RUNEC_PACK_DIR");
    if (pack_dir && pack_dir[0]) {
        snprintf(g_pack_dir, sizeof(g_pack_dir), "%s", pack_dir);
    } else if (root && root[0]) {
        snprintf(g_pack_dir, sizeof(g_pack_dir), "%s/packs", root);
    }
    const char *backend = getenv("RUNEC_ASSET_BACKEND");
    if (backend && strcmp(backend, "loose") == 0) {
        g_backend = RC_ASSET_BACKEND_LOOSE;
    } else if (backend && strcmp(backend, "pack") == 0) {
        g_backend = RC_ASSET_BACKEND_PACK;
    }
}

static int rc_entries_add(const RcPackEntry *entry) {
    if (g_entry_count == g_entry_cap) {
        size_t next = g_entry_cap ? g_entry_cap * 2 : 256;
        RcPackEntry *new_entries =
            (RcPackEntry *)realloc(g_entries, next * sizeof(*g_entries));
        if (!new_entries) return 0;
        g_entries = new_entries;
        g_entry_cap = next;
    }
    g_entries[g_entry_count++] = *entry;
    return 1;
}

static int rc_read_file_range(FILE *f, uint64_t offset, unsigned char *dst,
                              size_t size) {
    if (fseeko(f, (off_t)offset, SEEK_SET) != 0) return 0;
    return fread(dst, 1, size, f) == size;
}

static int rc_scan_one_pack(const char *pack_path) {
    FILE *f = fopen(pack_path, "rb");
    if (!f) return 0;

    unsigned char header[RC_PACK_HEADER_SIZE];
    if (fread(header, 1, sizeof(header), f) != sizeof(header)
            || memcmp(header, RC_PACK_MAGIC, 8) != 0) {
        fclose(f);
        return 0;
    }

    uint64_t index_offset = rc_read_le64(header + 8);
    uint64_t index_size = rc_read_le64(header + 16);
    uint64_t asset_count = rc_read_le64(header + 24);
    unsigned char expected_index_sha[32];
    memcpy(expected_index_sha, header + 32, sizeof(expected_index_sha));

    if (index_size == 0 || index_size > (uint64_t)SIZE_MAX) {
        fclose(f);
        return 0;
    }

    unsigned char *index_data = (unsigned char *)malloc((size_t)index_size);
    if (!index_data
            || !rc_read_file_range(f, index_offset, index_data,
                                   (size_t)index_size)) {
        free(index_data);
        fclose(f);
        return 0;
    }
    fclose(f);

    unsigned char index_sha[32];
    SHA256_CTX sha_ctx;
    SHA256_Init(&sha_ctx);
    SHA256_Update(&sha_ctx, index_data, (size_t)index_size);
    SHA256_Final(index_sha, &sha_ctx);
    if (memcmp(index_sha, expected_index_sha, sizeof(index_sha)) != 0) {
        fprintf(stderr, "asset pack index hash mismatch: %s\n", pack_path);
        free(index_data);
        return 0;
    }

    RcIndexReader r = {index_data, (size_t)index_size, 0};
    unsigned char magic[8];
    uint64_t parsed_count = 0;
    if (!rc_index_take(&r, magic, sizeof(magic))
            || memcmp(magic, RC_INDEX_MAGIC, 8) != 0
            || !rc_index_u64(&r, &parsed_count)
            || parsed_count != asset_count) {
        free(index_data);
        return 0;
    }

    int ok = 1;
    for (uint64_t i = 0; i < parsed_count; i++) {
        uint16_t path_len = 0;
        if (!rc_index_u16(&r, &path_len) || path_len == 0) {
            ok = 0;
            break;
        }
        char *path = (char *)calloc((size_t)path_len + 1, 1);
        if (!path || !rc_index_take(&r, path, path_len)) {
            free(path);
            ok = 0;
            break;
        }

        unsigned char compression = 0;
        uint64_t offset = 0, size = 0, packed_size = 0;
        unsigned char sha[32];
        if (!rc_index_take(&r, &compression, 1)
                || !rc_index_u64(&r, &offset)
                || !rc_index_u64(&r, &size)
                || !rc_index_u64(&r, &packed_size)
                || !rc_index_take(&r, sha, sizeof(sha))) {
            free(path);
            ok = 0;
            break;
        }

        RcPackEntry entry = {
            .path = path,
            .pack_path = rc_asset_strdup(pack_path),
            .offset = offset,
            .size = size,
            .packed_size = packed_size,
            .compression = compression,
        };
        memcpy(entry.sha256, sha, sizeof(entry.sha256));
        if (!entry.pack_path || !rc_entries_add(&entry)) {
            free(entry.path);
            free(entry.pack_path);
            ok = 0;
            break;
        }
    }

    free(index_data);
    return ok;
}

static int rc_name_ends_with(const char *name, const char *suffix) {
    size_t name_len = strlen(name);
    size_t suffix_len = strlen(suffix);
    return name_len >= suffix_len
        && strcmp(name + name_len - suffix_len, suffix) == 0;
}

static void rc_scan_packs(void) {
    DIR *dir = opendir(g_pack_dir);
    if (!dir) return;

    char **names = NULL;
    size_t count = 0, cap = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (!rc_name_ends_with(de->d_name, ".pak")) continue;
        if (count == cap) {
            size_t next = cap ? cap * 2 : 32;
            char **new_names = (char **)realloc(names, next * sizeof(*names));
            if (!new_names) break;
            names = new_names;
            cap = next;
        }
        names[count++] = rc_asset_strdup(de->d_name);
    }
    closedir(dir);
    qsort(names, count, sizeof(*names), rc_string_cmp);

    for (size_t i = 0; i < count; i++) {
        char path[2048];
        if (rc_join_path(path, sizeof(path), g_pack_dir, names[i]))
            rc_scan_one_pack(path);
        free(names[i]);
    }
    free(names);

    if (g_entry_count > 1)
        qsort(g_entries, g_entry_count, sizeof(*g_entries), rc_entry_cmp);
}

static void rc_asset_init(void) {
    if (g_initialized) return;
    rc_asset_apply_env();
    if (g_backend != RC_ASSET_BACKEND_LOOSE)
        rc_scan_packs();
    g_initialized = 1;
}

static const RcPackEntry *rc_find_pack_entry(const char *logical) {
    if (!logical || !logical[0] || g_entry_count == 0) return NULL;
    size_t lo = 0, hi = g_entry_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = strcmp(logical, g_entries[mid].path);
        if (cmp == 0) return &g_entries[mid];
        if (cmp < 0) hi = mid;
        else lo = mid + 1;
    }
    return NULL;
}

static int rc_asset_try_loose_first(void) {
    return g_backend == RC_ASSET_BACKEND_AUTO
        || g_backend == RC_ASSET_BACKEND_LOOSE;
}

static int rc_asset_try_pack(void) {
    return g_backend == RC_ASSET_BACKEND_AUTO
        || g_backend == RC_ASSET_BACKEND_PACK;
}

static int rc_sha_file_matches(FILE *f, const unsigned char expected[32]) {
    unsigned char digest[32];
    unsigned char buf[RC_ASSET_IO_CHUNK];
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    rewind(f);
    for (;;) {
        size_t n = fread(buf, 1, sizeof(buf), f);
        if (n > 0) SHA256_Update(&ctx, buf, n);
        if (n < sizeof(buf)) {
            if (ferror(f)) return 0;
            break;
        }
    }
    SHA256_Final(digest, &ctx);
    rewind(f);
    return memcmp(digest, expected, 32) == 0;
}

static FILE *rc_asset_extract_to_tmp(const RcPackEntry *entry) {
    if (!entry) return NULL;
    FILE *pack = fopen(entry->pack_path, "rb");
    if (!pack) return NULL;
    FILE *tmp = tmpfile();
    if (!tmp) {
        fclose(pack);
        return NULL;
    }
    if (fseeko(pack, (off_t)entry->offset, SEEK_SET) != 0) {
        fclose(pack);
        fclose(tmp);
        return NULL;
    }

    int ok = 1;
    uint64_t remaining = entry->packed_size;
    unsigned char in[RC_ASSET_IO_CHUNK];
    unsigned char out[RC_ASSET_IO_CHUNK];
    if (entry->compression == 0) {
        while (remaining > 0 && ok) {
            size_t want = remaining < sizeof(in) ? (size_t)remaining : sizeof(in);
            size_t n = fread(in, 1, want, pack);
            if (n != want) {
                ok = 0;
                break;
            }
            if (fwrite(in, 1, n, tmp) != n) ok = 0;
            remaining -= n;
        }
    } else if (entry->compression == 1) {
        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        if (inflateInit(&zs) != Z_OK) {
            ok = 0;
        } else {
            int zret = Z_OK;
            while (remaining > 0 && ok && zret != Z_STREAM_END) {
                size_t want = remaining < sizeof(in) ? (size_t)remaining : sizeof(in);
                size_t n = fread(in, 1, want, pack);
                if (n != want) {
                    ok = 0;
                    break;
                }
                remaining -= n;
                zs.next_in = in;
                zs.avail_in = (uInt)n;
                while (zs.avail_in > 0 && ok) {
                    zs.next_out = out;
                    zs.avail_out = sizeof(out);
                    zret = inflate(&zs, Z_NO_FLUSH);
                    if (zret != Z_OK && zret != Z_STREAM_END) {
                        ok = 0;
                        break;
                    }
                    size_t have = sizeof(out) - zs.avail_out;
                    if (have > 0 && fwrite(out, 1, have, tmp) != have) {
                        ok = 0;
                        break;
                    }
                    if (zret == Z_STREAM_END) break;
                }
            }
            inflateEnd(&zs);
            if (zret != Z_STREAM_END) ok = 0;
        }
    } else {
        ok = 0;
    }

    fclose(pack);
    if (!ok || fflush(tmp) != 0 || !rc_sha_file_matches(tmp, entry->sha256)) {
        fclose(tmp);
        return NULL;
    }
    return tmp;
}

static RcAssetBytes rc_asset_read_loose(const char *path) {
    RcAssetBytes bytes = {0};
    FILE *f = fopen(path, "rb");
    if (!f) return bytes;
    if (fseeko(f, 0, SEEK_END) != 0) {
        fclose(f);
        return bytes;
    }
    off_t size = ftello(f);
    if (size < 0 || fseeko(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return bytes;
    }
    bytes.data = (unsigned char *)malloc((size_t)size);
    if (size > 0 && !bytes.data) {
        fclose(f);
        return (RcAssetBytes){0};
    }
    if (size > 0 && fread(bytes.data, 1, (size_t)size, f) != (size_t)size) {
        rc_asset_bytes_free(&bytes);
        fclose(f);
        return (RcAssetBytes){0};
    }
    bytes.size = (size_t)size;
    fclose(f);
    return bytes;
}

static RcAssetBytes rc_asset_read_pack(const RcPackEntry *entry) {
    RcAssetBytes bytes = {0};
    FILE *tmp = rc_asset_extract_to_tmp(entry);
    if (!tmp) return bytes;
    if (entry->size > (uint64_t)SIZE_MAX) {
        fclose(tmp);
        return bytes;
    }
    bytes.data = (unsigned char *)malloc((size_t)entry->size);
    if (entry->size > 0 && !bytes.data) {
        fclose(tmp);
        return bytes;
    }
    if (entry->size > 0
            && fread(bytes.data, 1, (size_t)entry->size, tmp)
                   != (size_t)entry->size) {
        rc_asset_bytes_free(&bytes);
        fclose(tmp);
        return (RcAssetBytes){0};
    }
    bytes.size = (size_t)entry->size;
    fclose(tmp);
    return bytes;
}

int rc_asset_exists(const char *path) {
    rc_asset_init();
    char loose[2048];
    if (rc_asset_try_loose_first()
            && rc_loose_path(loose, sizeof(loose), path)
            && rc_file_exists_path(loose)) {
        return 1;
    }
    const char *logical = rc_asset_logical_path(path);
    if (rc_asset_try_pack() && rc_find_pack_entry(logical))
        return 1;
    return 0;
}

int rc_asset_size(const char *path, uint64_t *out_size) {
    if (out_size) *out_size = 0;
    rc_asset_init();
    char loose[2048];
    if (rc_asset_try_loose_first()
            && rc_loose_path(loose, sizeof(loose), path)) {
        struct stat st;
        if (stat(loose, &st) == 0 && S_ISREG(st.st_mode)) {
            if (out_size) *out_size = (uint64_t)st.st_size;
            return 1;
        }
        if (rc_path_is_absolute(path)
                && rc_asset_logical_path(path) == path) {
            return 0;
        }
    }

    if (rc_asset_try_pack()) {
        const RcPackEntry *entry = rc_find_pack_entry(rc_asset_logical_path(path));
        if (entry) {
            if (out_size) *out_size = entry->size;
            return 1;
        }
    }

    if (g_backend == RC_ASSET_BACKEND_PACK)
        return 0;

    struct stat st;
    if (path && stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
        if (out_size) *out_size = (uint64_t)st.st_size;
        return 1;
    }
    return 0;
}

FILE *rc_asset_fopen(const char *path, const char *mode) {
    rc_asset_init();
    if (!mode || strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+'))
        return fopen(path, mode);

    const char *logical = rc_asset_logical_path(path);
    char loose[2048];
    if (rc_asset_try_loose_first() && rc_loose_path(loose, sizeof(loose), path)) {
        FILE *f = fopen(loose, mode);
        if (f) return f;
        if (rc_path_is_absolute(path) && logical == path) return NULL;
    }

    if (rc_asset_try_pack()) {
        const RcPackEntry *entry = rc_find_pack_entry(logical);
        if (entry) return rc_asset_extract_to_tmp(entry);
    }

    if (g_backend == RC_ASSET_BACKEND_PACK)
        return NULL;
    return fopen(path, mode);
}

int rc_asset_close(FILE *f) {
    return f ? fclose(f) : 0;
}

RcAssetBytes rc_asset_read_all(const char *path) {
    rc_asset_init();
    char loose[2048];
    if (rc_asset_try_loose_first() && rc_loose_path(loose, sizeof(loose), path)) {
        RcAssetBytes bytes = rc_asset_read_loose(loose);
        if (bytes.data || bytes.size == 0) {
            if (bytes.data || rc_file_exists_path(loose))
                return bytes;
        }
    }

    if (rc_asset_try_pack()) {
        const RcPackEntry *entry = rc_find_pack_entry(rc_asset_logical_path(path));
        if (entry) return rc_asset_read_pack(entry);
    }

    if (g_backend == RC_ASSET_BACKEND_PACK)
        return (RcAssetBytes){0};
    return rc_asset_read_loose(path);
}

void rc_asset_bytes_free(RcAssetBytes *bytes) {
    if (!bytes) return;
    free(bytes->data);
    bytes->data = NULL;
    bytes->size = 0;
}
