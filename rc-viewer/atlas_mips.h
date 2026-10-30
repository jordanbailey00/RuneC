#ifndef RUNEC_ATLAS_MIPS_H
#define RUNEC_ATLAS_MIPS_H

// Mip chains for texture atlases (DSA-001).
//
// Point-sampled atlases alias at distance: at the default zoom a tile shows
// about five texels per pixel, so textures sparkle as the camera moves. The
// chain is built per 128x128 atlas cell (export_textures.build_atlas packs one
// texture per 128-wide column, slot heights are multiples of 128), so every
// level down to one texel per cell averages inside one texture. Colours are
// averaged by coverage so transparent texels do not darken edges. Alpha stays
// binary like the cache textures: each cell keeps its level-0 share of opaque
// texels at every level (the texels straddling the threshold are dithered),
// so foliage, nets and fences do not thin out or vanish at distance, and
// shaders without a cutout test never blend fringes. Levels below one texel
// per cell average across cells and only serve sub-pixel faces.

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ATLAS_MIP_CELL 128
#define ATLAS_MIP_CELL_LEVELS 7  // ATLAS_MIP_CELL >> 7 == 1
#define ATLAS_MIP_COVERAGE_BINS 1024

static int atlas_mip_level_count(int width, int height) {
    int levels = 1;
    while (width > 1 || height > 1) {
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
        levels++;
    }
    return levels;
}

static void atlas_mip_level_size(int width, int height, int level,
                                 int *out_w, int *out_h) {
    for (int i = 0; i < level; i++) {
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    *out_w = width;
    *out_h = height;
}

// Bytes before `level` in a chain stored level 0 first, as raylib uploads it.
static size_t atlas_mip_level_offset(int width, int height, int level) {
    size_t offset = 0;
    for (int i = 0; i < level; i++) {
        int w, h;
        atlas_mip_level_size(width, height, i, &w, &h);
        offset += (size_t)w * (size_t)h * 4u;
    }
    return offset;
}

static size_t atlas_mip_chain_bytes(int width, int height) {
    return atlas_mip_level_offset(width, height,
                                  atlas_mip_level_count(width, height));
}

static int atlas_mips_supported(int width, int height) {
    return width >= ATLAS_MIP_CELL && height >= ATLAS_MIP_CELL
        && width % ATLAS_MIP_CELL == 0 && height % ATLAS_MIP_CELL == 0;
}

// Level `level` (>= 1) inside the lower-level buffer from atlas_mips_build.
static uint8_t *atlas_mip_level(uint8_t *mips, int width, int height,
                                int level) {
    return mips + atlas_mip_level_offset(width, height, level)
                - (size_t)width * (size_t)height * 4u;
}

static const uint8_t ATLAS_MIP_BAYER[4][4] = {
    {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5},
};

// Rebuild levels 1..ATLAS_MIP_CELL_LEVELS of one cell from level 0.
// `coverage` holds 2 * ATLAS_MIP_CELL^2 floats of scratch.
static void atlas_mips_build_cell(uint8_t *mips, const uint8_t *pixels,
                                  int width, int height, int cx, int cy,
                                  float *coverage) {
    enum { N = ATLAS_MIP_CELL * ATLAS_MIP_CELL };
    int histogram[ATLAS_MIP_COVERAGE_BINS + 1];
    int opaque = 0;
    for (int y = 0; y < ATLAS_MIP_CELL; y++) {
        for (int x = 0; x < ATLAS_MIP_CELL; x++) {
            uint8_t a = pixels[((size_t)(cy + y) * width + cx + x) * 4u + 3];
            coverage[y * ATLAS_MIP_CELL + x] = (float)a / 255.0f;
            opaque += a >= 128;
        }
    }
    float share = (float)opaque / (float)N;

    const uint8_t *src = pixels;
    int src_w = width, src_x = cx, src_y = cy;
    for (int level = 1; level <= ATLAS_MIP_CELL_LEVELS; level++) {
        int size = ATLAS_MIP_CELL >> level;
        int level_w, level_h;
        atlas_mip_level_size(width, height, level, &level_w, &level_h);
        uint8_t *dst = atlas_mip_level(mips, width, height, level);
        int dst_x = cx >> level, dst_y = cy >> level;
        const float *above = coverage + ((level - 1) & 1) * N;
        float *here = coverage + (level & 1) * N;
        memset(histogram, 0, sizeof(histogram));
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                float sum[3] = {0}, plain[3] = {0}, weight = 0.0f;
                for (int k = 0; k < 4; k++) {
                    int sx = x * 2 + (k & 1), sy = y * 2 + (k >> 1);
                    const uint8_t *p = &src[((size_t)(src_y + sy) * src_w
                                             + src_x + sx) * 4u];
                    float w = above[sy * size * 2 + sx];
                    for (int c = 0; c < 3; c++) {
                        sum[c] += p[c] * w;
                        plain[c] += p[c];
                    }
                    weight += w;
                }
                uint8_t *q = &dst[((size_t)(dst_y + y) * level_w + dst_x + x)
                                  * 4u];
                for (int c = 0; c < 3; c++) {
                    float v = weight > 0.0f ? sum[c] / weight : plain[c] / 4.0f;
                    q[c] = (uint8_t)(v + 0.5f);
                }
                here[y * size + x] = weight / 4.0f;
                histogram[(int)(weight / 4.0f * ATLAS_MIP_COVERAGE_BINS
                                + 0.5f)]++;
            }
        }
        // Texels above the boundary bin are opaque; the boundary bin is
        // dithered so the cell keeps `target` opaque texels.
        int target = (int)(share * (float)(size * size) + 0.5f);
        int boundary = ATLAS_MIP_COVERAGE_BINS + 1, kept = 0;
        float fraction = 0.0f;
        if (target > 0) {
            boundary = 0;
            for (int bin = ATLAS_MIP_COVERAGE_BINS; bin > 0; bin--) {
                if (kept + histogram[bin] >= target) {
                    boundary = bin;
                    fraction = (float)(target - kept) / (float)histogram[bin];
                    break;
                }
                kept += histogram[bin];
            }
        }
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                int bin = (int)(here[y * size + x] * ATLAS_MIP_COVERAGE_BINS
                                + 0.5f);
                int on = bin > boundary
                    || (bin == boundary
                        && (ATLAS_MIP_BAYER[y & 3][x & 3] + 0.5f) / 16.0f
                               < fraction);
                dst[((size_t)(dst_y + y) * level_w + dst_x + x) * 4u + 3] =
                    on && bin > 0 ? 255 : 0;
            }
        }
        src = dst;
        src_w = level_w;
        src_x = dst_x;
        src_y = dst_y;
        (void)level_h;
    }
}

// Rebuild the levels below one texel per cell, which mix cells.
static void atlas_mips_build_tail(uint8_t *mips, int width, int height) {
    int levels = atlas_mip_level_count(width, height);
    for (int level = ATLAS_MIP_CELL_LEVELS + 1; level < levels; level++) {
        int sw, sh, dw, dh;
        atlas_mip_level_size(width, height, level - 1, &sw, &sh);
        atlas_mip_level_size(width, height, level, &dw, &dh);
        const uint8_t *src = atlas_mip_level(mips, width, height, level - 1);
        uint8_t *dst = atlas_mip_level(mips, width, height, level);
        for (int y = 0; y < dh; y++) {
            for (int x = 0; x < dw; x++) {
                unsigned sum[3] = {0}, plain[3] = {0}, opaque = 0;
                for (int k = 0; k < 4; k++) {
                    int sx = x * 2 + (k & 1), sy = y * 2 + (k >> 1);
                    if (sx >= sw) sx = sw - 1;
                    if (sy >= sh) sy = sh - 1;
                    const uint8_t *p = &src[((size_t)sy * sw + sx) * 4u];
                    for (int c = 0; c < 3; c++) {
                        plain[c] += p[c];
                        if (p[3]) sum[c] += p[c];
                    }
                    opaque += p[3] != 0;
                }
                uint8_t *q = &dst[((size_t)y * dw + x) * 4u];
                for (int c = 0; c < 3; c++)
                    q[c] = (uint8_t)(opaque ? (sum[c] + opaque / 2) / opaque
                                            : (plain[c] + 2) / 4);
                q[3] = opaque >= 2 ? 255 : 0;
            }
        }
    }
}

// Rebuild the lower levels for the cells covering a level-0 rect.
static int atlas_mips_update_rect(uint8_t *mips, const uint8_t *pixels,
                                  int width, int height,
                                  int x, int y, int w, int h) {
    if (!mips || !pixels || !atlas_mips_supported(width, height))
        return 0;
    float *coverage = malloc(2u * ATLAS_MIP_CELL * ATLAS_MIP_CELL
                             * sizeof(float));
    if (!coverage)
        return 0;
    int x0 = (x < 0 ? 0 : x) / ATLAS_MIP_CELL * ATLAS_MIP_CELL;
    int y0 = (y < 0 ? 0 : y) / ATLAS_MIP_CELL * ATLAS_MIP_CELL;
    int x1 = x + w > width ? width : x + w;
    int y1 = y + h > height ? height : y + h;
    for (int cy = y0; cy < y1; cy += ATLAS_MIP_CELL)
        for (int cx = x0; cx < x1; cx += ATLAS_MIP_CELL)
            atlas_mips_build_cell(mips, pixels, width, height, cx, cy,
                                  coverage);
    free(coverage);
    atlas_mips_build_tail(mips, width, height);
    return 1;
}

// Lower levels (1..count-1, contiguous) for an atlas whose level 0 is
// `pixels`, or NULL when the atlas is not cell-aligned.
static uint8_t *atlas_mips_build(const uint8_t *pixels, int width,
                                 int height) {
    if (!pixels || !atlas_mips_supported(width, height))
        return NULL;
    size_t level0 = (size_t)width * (size_t)height * 4u;
    uint8_t *mips = malloc(atlas_mip_chain_bytes(width, height) - level0);
    if (mips && !atlas_mips_update_rect(mips, pixels, width, height,
                                        0, 0, width, height)) {
        free(mips);
        mips = NULL;
    }
    return mips;
}

#ifdef RAYLIB_H
#include "rlgl.h"

// Texture atlases sample their nearest texel and blend between levels when
// mipmaps are on (RUNEC_TEXTURE_MIPMAPS, set by the viewer).
static int g_atlas_mipmaps = 1;

typedef void (*AtlasGlProc)(void);
extern AtlasGlProc glfwGetProcAddress(const char *procname);
typedef void (*AtlasGlTexSubImage2D)(unsigned int target, int level,
                                     int xoffset, int yoffset, int width,
                                     int height, unsigned int format,
                                     unsigned int type, const void *pixels);
typedef void (*AtlasGlPixelStorei)(unsigned int pname, int param);

// Upload one level-0 rect straight from the atlas's pixel buffer, so a
// texture animation sends only the cells it changed. Returns 0 when the GL
// entry points are unavailable and the caller must upload the whole atlas.
static int atlas_upload_rect(Texture2D texture, const uint8_t *pixels,
                             int width, int x, int y, int w, int h) {
    static AtlasGlTexSubImage2D tex_sub_image = NULL;
    static AtlasGlPixelStorei pixel_store = NULL;
    if (!tex_sub_image || !pixel_store) {
        tex_sub_image = (AtlasGlTexSubImage2D)glfwGetProcAddress(
            "glTexSubImage2D");
        pixel_store = (AtlasGlPixelStorei)glfwGetProcAddress("glPixelStorei");
        if (!tex_sub_image || !pixel_store)
            return 0;
    }
    rlEnableTexture(texture.id);
    pixel_store(0x0CF2 /* GL_UNPACK_ROW_LENGTH */, width);
    tex_sub_image(0x0DE1 /* GL_TEXTURE_2D */, 0, x, y, w, h,
                  0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */,
                  pixels + ((size_t)y * width + x) * 4u);
    pixel_store(0x0CF2 /* GL_UNPACK_ROW_LENGTH */, 0);
    rlDisableTexture();
    return 1;
}

static size_t atlas_texture_bytes(int width, int height) {
    if (g_atlas_mipmaps && atlas_mips_supported(width, height))
        return atlas_mip_chain_bytes(width, height);
    return (size_t)width * (size_t)height * 4u;
}

// Upload an atlas; *mips_out keeps its lower levels for texture animation.
static Texture2D atlas_load_texture(const uint8_t *pixels, int width,
                                    int height, uint8_t **mips_out) {
    if (mips_out)
        *mips_out = NULL;
    uint8_t *mips = g_atlas_mipmaps
        ? atlas_mips_build(pixels, width, height) : NULL;
    size_t level0 = (size_t)width * (size_t)height * 4u;
    size_t chain_bytes = atlas_mip_chain_bytes(width, height);
    uint8_t *chain = mips ? malloc(chain_bytes) : NULL;
    Image image = {
        .data = (void *)pixels,
        .width = width,
        .height = height,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
    };
    if (chain) {
        memcpy(chain, pixels, level0);
        memcpy(chain + level0, mips, chain_bytes - level0);
        image.data = chain;
        image.mipmaps = atlas_mip_level_count(width, height);
    }
    Texture2D texture = LoadTextureFromImage(image);
    free(chain);
    if (texture.id > 0 && image.mipmaps > 1) {
        rlTextureParameters(texture.id, RL_TEXTURE_MIN_FILTER,
                            RL_TEXTURE_FILTER_NEAREST_MIP_LINEAR);
        rlTextureParameters(texture.id, RL_TEXTURE_MAG_FILTER,
                            RL_TEXTURE_FILTER_NEAREST);
    } else if (texture.id > 0) {
        SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    }
    if (mips_out && texture.id > 0 && image.mipmaps > 1)
        *mips_out = mips;
    else
        free(mips);
    return texture;
}

#endif

static int atlas_wrap(int value, int period) {
    value %= period;
    return value < 0 ? value + period : value;
}

// Copy a texture animation rect scrolled by `shift` texels: rows wrap inside
// the band of center_h rows after `pad` guard rows (vertical), or columns
// wrap across the rect. Strides are in texels; both pointers address the
// rect's top-left texel.
static void atlas_scroll_rect(uint8_t *dst, int dst_stride,
                              const uint8_t *src, int src_stride, int w,
                              int h, int pad, int center_h, int vertical,
                              int shift) {
    int from = vertical ? 0 : atlas_wrap(shift, w);
    for (int row = 0; row < h; row++) {
        int sy = vertical ? atlas_wrap(row - pad + shift, center_h) + pad : row;
        const uint8_t *line = src + (size_t)sy * src_stride * 4u;
        uint8_t *out = dst + (size_t)row * dst_stride * 4u;
        memcpy(out, line + (size_t)from * 4u, (size_t)(w - from) * 4u);
        memcpy(out + (size_t)(w - from) * 4u, line, (size_t)from * 4u);
    }
}

// The base chain's rect at `level`, scrolled by the level-0 shift scaled down
// to that level, into a contiguous buffer.
static void atlas_scroll_level(uint8_t *out, const uint8_t *base_mips,
                               int width, int height, int level,
                               int x, int y, int w, int h, int pad,
                               int center_h, int vertical, int shift) {
    int lw, lh;
    atlas_mip_level_size(width, height, level, &lw, &lh);
    const uint8_t *src = atlas_mip_level((uint8_t *)base_mips, width, height,
                                         level);
    int step = 1 << level;
    int lshift = shift >= 0 ? shift / step : -((-shift + step - 1) / step);
    atlas_scroll_rect(out, w >> level,
                      src + ((size_t)(y >> level) * lw + (x >> level)) * 4u,
                      lw, w >> level, h >> level, pad >> level,
                      center_h >> level, vertical, lshift);
}

#ifdef RAYLIB_H
// After a texture animation scrolled its level-0 rows: scroll the levels that
// keep detail. Wrapping keeps each cell's average, so the levels from one
// texel per cell down stay as built.
static void atlas_scroll_mips(Texture2D texture, const uint8_t *base_mips,
                              int width, int height, int x, int y, int w,
                              int h, int pad, int center_h, int vertical,
                              int shift) {
    static AtlasGlTexSubImage2D tex_sub_image = NULL;
    int align = 1 << (ATLAS_MIP_CELL_LEVELS - 1);
    if (!base_mips || texture.id <= 0 || texture.mipmaps <= 1
            || x % align || y % align || w % align || h % align
            || pad % align || center_h % align || center_h <= 0)
        return;
    if (!tex_sub_image)
        tex_sub_image = (AtlasGlTexSubImage2D)glfwGetProcAddress(
            "glTexSubImage2D");
    uint8_t *rows = tex_sub_image ? malloc((size_t)w * h) : NULL;
    if (!rows)
        return;
    rlEnableTexture(texture.id);
    for (int level = 1; level < ATLAS_MIP_CELL_LEVELS; level++) {
        atlas_scroll_level(rows, base_mips, width, height, level, x, y, w, h,
                           pad, center_h, vertical, shift);
        tex_sub_image(0x0DE1 /* GL_TEXTURE_2D */, level, x >> level,
                      y >> level, w >> level, h >> level,
                      0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */,
                      rows);
    }
    rlDisableTexture();
    free(rows);
}
#endif

#endif
