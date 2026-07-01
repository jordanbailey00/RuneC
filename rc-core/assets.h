#ifndef RC_ASSETS_H
#define RC_ASSETS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    RC_ASSET_BACKEND_AUTO = 0,
    RC_ASSET_BACKEND_LOOSE,
    RC_ASSET_BACKEND_PACK,
} RcAssetBackend;

typedef struct {
    unsigned char *data;
    size_t size;
} RcAssetBytes;

void rc_asset_set_backend(RcAssetBackend backend);
void rc_asset_set_data_root(const char *root);
void rc_asset_set_pack_dir(const char *dir);
void rc_asset_reset(void);

int rc_asset_exists(const char *path);
int rc_asset_size(const char *path, uint64_t *out_size);
FILE *rc_asset_fopen(const char *path, const char *mode);
int rc_asset_close(FILE *f);
RcAssetBytes rc_asset_read_all(const char *path);
void rc_asset_bytes_free(RcAssetBytes *bytes);

const char *rc_asset_logical_path(const char *path);

#endif
