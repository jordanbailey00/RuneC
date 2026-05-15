#ifndef RUNEC_ASSET_RAYLIB_H
#define RUNEC_ASSET_RAYLIB_H

#include "../rc-core/assets.h"
#include "raylib.h"

#include <string.h>

static const char *runec_asset_ext(const char *path, const char *fallback) {
    const char *dot = path ? strrchr(path, '.') : NULL;
    return dot && dot[0] ? dot : fallback;
}

static Texture2D runec_load_texture_asset(const char *path) {
    Texture2D empty = {0};
    RcAssetBytes bytes = rc_asset_read_all(path);
    if (!bytes.data && bytes.size == 0)
        return empty;
    Image img = LoadImageFromMemory(runec_asset_ext(path, ".png"),
                                    bytes.data, (int)bytes.size);
    rc_asset_bytes_free(&bytes);
    if (!img.data)
        return empty;
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

static Image runec_load_image_asset(const char *path) {
    Image empty = {0};
    RcAssetBytes bytes = rc_asset_read_all(path);
    if (!bytes.data && bytes.size == 0)
        return empty;
    Image img = LoadImageFromMemory(runec_asset_ext(path, ".png"),
                                    bytes.data, (int)bytes.size);
    rc_asset_bytes_free(&bytes);
    return img;
}

static Font runec_load_font_asset(const char *path, int font_size) {
    Font empty = {0};
    RcAssetBytes bytes = rc_asset_read_all(path);
    if (!bytes.data && bytes.size == 0)
        return empty;
    Font font = LoadFontFromMemory(runec_asset_ext(path, ".ttf"),
                                   bytes.data, (int)bytes.size,
                                   font_size, NULL, 95);
    rc_asset_bytes_free(&bytes);
    return font.texture.id != 0 ? font : empty;
}

#endif
