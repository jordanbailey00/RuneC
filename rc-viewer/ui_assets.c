#include "ui_assets.h"
#include "asset_raylib.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct RuneCUiAssetSpec {
    const char *name;
    const char *file;
    int required;
} RuneCUiAssetSpec;

#define UI_ASSET(name) { name, "data/sprites/ui/" name ".png", 1 }
#define UI_OPTIONAL_ASSET(name, file) \
    { name, "data/sprites/items/" file, 0 }

static const RuneCUiAssetSpec g_ui_asset_specs[] = {
    UI_ASSET("tradebacking_dark"),
    UI_ASSET("chatbox_bg"),
    UI_ASSET("main_stones_bottom"),
    UI_ASSET("side_stone_highlights_0"),
    UI_ASSET("side_stone_highlights_1"),
    UI_ASSET("side_stone_highlights_2"),
    UI_ASSET("side_stone_highlights_4"),
    UI_ASSET("osrs_stretch_side_topbottom_0"),
    UI_ASSET("osrs_stretch_side_topbottom_1"),
    UI_ASSET("osrs_stretch_side_columns_0"),
    UI_ASSET("osrs_stretch_side_columns_1"),
    UI_ASSET("compass"),
    UI_ASSET("osrs_stretch_mapsurround"),
    UI_ASSET("resize_compass_mask"),
    UI_ASSET("clickcross_0"),
    UI_ASSET("clickcross_1"),
    UI_ASSET("clickcross_2"),
    UI_ASSET("clickcross_3"),
    UI_ASSET("clickcross_4"),
    UI_ASSET("clickcross_5"),
    UI_ASSET("clickcross_6"),
    UI_ASSET("clickcross_7"),
    UI_ASSET("side_icon_combat"),
    UI_ASSET("side_icon_stats"),
    UI_ASSET("side_icon_inventory"),
    UI_ASSET("side_icon_equipment"),
    UI_ASSET("side_icon_prayer"),
    UI_ASSET("side_icon_magic"),
    UI_ASSET("side_icon_clan"),
    UI_ASSET("wornicons_0"),
    UI_ASSET("wornicons_1"),
    UI_ASSET("wornicons_2"),
    UI_ASSET("wornicons_3"),
    UI_ASSET("wornicons_4"),
    UI_ASSET("wornicons_5"),
    UI_ASSET("wornicons_6"),
    UI_ASSET("wornicons_7"),
    UI_ASSET("wornicons_8"),
    UI_ASSET("wornicons_9"),
    UI_ASSET("wornicons_10"),
    UI_ASSET("combaticons_0"),
    UI_ASSET("combaticons_1"),
    UI_ASSET("combaticons_2"),
    UI_ASSET("combaticons_3"),
    UI_ASSET("combaticons_4"),
    UI_ASSET("combaticons_5"),
    UI_ASSET("combaticons_6"),
    UI_ASSET("combaticons_7"),
    UI_ASSET("combaticons_8"),
    UI_ASSET("combaticons_9"),
    UI_ASSET("combaticons_10"),
    UI_ASSET("combaticons_11"),
    UI_ASSET("combaticons_12"),
    UI_ASSET("combaticons_13"),
    UI_ASSET("combaticons_14"),
    UI_ASSET("combaticons_15"),
    UI_ASSET("combaticons_16"),
    UI_ASSET("combaticons_17"),
    UI_ASSET("combaticons_18"),
    UI_ASSET("combaticons_19"),
    UI_ASSET("combaticons2_0"),
    UI_ASSET("combaticons2_1"),
    UI_ASSET("combaticons2_2"),
    UI_ASSET("combaticons2_3"),
    UI_ASSET("combaticons2_5"),
    UI_ASSET("combaticons2_6"),
    UI_ASSET("combaticons2_7"),
    UI_ASSET("combaticons2_8"),
    UI_ASSET("combaticons2_9"),
    UI_ASSET("combaticons2_10"),
    UI_ASSET("combaticons2_11"),
    UI_ASSET("combaticons2_12"),
    UI_ASSET("combaticons2_13"),
    UI_ASSET("combaticons2_14"),
    UI_ASSET("combaticons2_15"),
    UI_ASSET("combaticons2_16"),
    UI_ASSET("combaticons2_17"),
    UI_ASSET("combaticons2_18"),
    UI_ASSET("combaticons2_19"),
    UI_ASSET("combaticons3_0"),
    UI_ASSET("combaticons3_1"),
    UI_ASSET("combaticons3_2"),
    UI_ASSET("combaticons3_3"),
    UI_ASSET("combaticons3_4"),
    UI_ASSET("combaticons3_5"),
    UI_ASSET("combaticons3_6"),
    UI_ASSET("combaticons3_7"),
    UI_ASSET("combaticons3_8"),
    UI_ASSET("combaticons3_9"),
    UI_ASSET("combaticons3_10"),
    UI_ASSET("combaticons3_11"),
    UI_ASSET("combaticons3_12"),
    UI_ASSET("combaticons3_13"),
    UI_ASSET("combaticons3_14"),
    UI_ASSET("combaticons3_15"),
    UI_ASSET("combaticons3_16"),
    UI_ASSET("combaticons3_17"),
    UI_ASSET("combaticons3_18"),
    UI_OPTIONAL_ASSET("item_995", "item_995.png"),
    UI_OPTIONAL_ASSET("item_995_1", "item_995_1.png"),
    UI_OPTIONAL_ASSET("item_995_2", "item_995_2.png"),
    UI_OPTIONAL_ASSET("item_995_3", "item_995_3.png"),
    UI_OPTIONAL_ASSET("item_995_4", "item_995_4.png"),
    UI_OPTIONAL_ASSET("item_995_5", "item_995_5.png"),
    UI_OPTIONAL_ASSET("item_995_25", "item_995_25.png"),
    UI_OPTIONAL_ASSET("item_995_100", "item_995_100.png"),
    UI_OPTIONAL_ASSET("item_995_250", "item_995_250.png"),
    UI_OPTIONAL_ASSET("item_995_1000", "item_995_1000.png"),
    UI_OPTIONAL_ASSET("item_995_10000", "item_995_10000.png"),
    UI_OPTIONAL_ASSET("item_1038", "item_1038.png"),
    UI_OPTIONAL_ASSET("item_1040", "item_1040.png"),
    UI_OPTIONAL_ASSET("item_1042", "item_1042.png"),
    UI_OPTIONAL_ASSET("item_1044", "item_1044.png"),
    UI_OPTIONAL_ASSET("item_1046", "item_1046.png"),
    UI_OPTIONAL_ASSET("item_1048", "item_1048.png"),
    UI_OPTIONAL_ASSET("item_4151", "item_4151.png"),
    UI_OPTIONAL_ASSET("item_554", "item_554.png"),
    UI_OPTIONAL_ASSET("item_555", "item_555.png"),
    UI_OPTIONAL_ASSET("item_556", "item_556.png"),
    UI_OPTIONAL_ASSET("item_557", "item_557.png"),
    UI_OPTIONAL_ASSET("item_558", "item_558.png"),
    UI_OPTIONAL_ASSET("item_560", "item_560.png"),
    UI_OPTIONAL_ASSET("item_562", "item_562.png"),
    UI_OPTIONAL_ASSET("item_861", "item_861.png"),
    UI_OPTIONAL_ASSET("item_892", "item_892.png"),
    UI_OPTIONAL_ASSET("item_1381", "item_1381.png"),
    UI_OPTIONAL_ASSET("item_10346", "item_10346.png"),
    UI_OPTIONAL_ASSET("item_10348", "item_10348.png"),
    UI_OPTIONAL_ASSET("item_10350", "item_10350.png"),
    UI_OPTIONAL_ASSET("item_10352", "item_10352.png"),
    UI_OPTIONAL_ASSET("item_11802", "item_11802.png"),
    UI_OPTIONAL_ASSET("item_11832", "item_11832.png"),
    UI_OPTIONAL_ASSET("item_11834", "item_11834.png"),
    UI_OPTIONAL_ASSET("item_26382", "item_26382.png"),
    UI_OPTIONAL_ASSET("item_26384", "item_26384.png"),
    UI_OPTIONAL_ASSET("item_26386", "item_26386.png"),
#define PRAYER_ICON(id, name, on, off, overhead) UI_ASSET(#on), UI_ASSET(#off),
#include "prayer_icons.inc"
#undef PRAYER_ICON
    // Sprites the client's scripts place by id.
    UI_ASSET("170"), UI_ASSET("172"), UI_ASSET("173"), UI_ASSET("187"), UI_ASSET("188"), UI_ASSET("189"),
    UI_ASSET("190"), UI_ASSET("191"), UI_ASSET("197"), UI_ASSET("198"), UI_ASSET("199"), UI_ASSET("200"),
    UI_ASSET("201"), UI_ASSET("202"), UI_ASSET("203"), UI_ASSET("204"), UI_ASSET("205"), UI_ASSET("206"),
    UI_ASSET("207"), UI_ASSET("208"), UI_ASSET("209"), UI_ASSET("210"), UI_ASSET("211"), UI_ASSET("212"),
    UI_ASSET("213"), UI_ASSET("214"), UI_ASSET("215"), UI_ASSET("216"), UI_ASSET("217"), UI_ASSET("220"),
    UI_ASSET("221"), UI_ASSET("228"), UI_ASSET("675"), UI_ASSET("760"), UI_ASSET("773"), UI_ASSET("780"),
    UI_ASSET("788"), UI_ASSET("789"), UI_ASSET("790"), UI_ASSET("791"), UI_ASSET("792"), UI_ASSET("912"),
    UI_ASSET("913"), UI_ASSET("914"), UI_ASSET("915"), UI_ASSET("916"), UI_ASSET("917"), UI_ASSET("918"),
    UI_ASSET("919"), UI_ASSET("920"), UI_ASSET("921"), UI_ASSET("922"), UI_ASSET("923"), UI_ASSET("924"),
    UI_ASSET("925"), UI_ASSET("926"), UI_ASSET("927"), UI_ASSET("928"), UI_ASSET("1059"), UI_ASSET("1060"),
    UI_ASSET("1063"), UI_ASSET("1064"), UI_ASSET("1065"), UI_ASSET("1066"), UI_ASSET("1067"), UI_ASSET("1068"),
    UI_ASSET("1069"), UI_ASSET("1070"), UI_ASSET("1071"), UI_ASSET("1072"), UI_ASSET("1090"), 
    UI_ASSET("1141"), UI_ASSET("1142"), UI_ASSET("1143"), UI_ASSET("1144"), UI_ASSET("1145"), UI_ASSET("1146"),
    UI_ASSET("1147"), UI_ASSET("1148"), UI_ASSET("1149"), UI_ASSET("1150"), UI_ASSET("1151"), UI_ASSET("1152"),
    UI_ASSET("1153"), UI_ASSET("1154"), UI_ASSET("1155"), UI_ASSET("1156"), UI_ASSET("1157"), UI_ASSET("1158"),
    UI_ASSET("1343"), UI_ASSET("1438"), UI_ASSET("1439"), UI_ASSET("1607"), UI_ASSET("1608"), UI_ASSET("1610"),
    UI_ASSET("1748"), UI_ASSET("1749"), UI_ASSET("3051"), UI_ASSET("3052"), UI_ASSET("3053"), UI_ASSET("3054"),
    UI_ASSET("3055"), UI_ASSET("3057"), UI_ASSET("3058"),
    UI_ASSET("3575"), UI_ASSET("8603"), UI_ASSET("8605"),
    UI_ASSET("prayer_glow"),
    UI_ASSET("headicons_prayer_0"),
    UI_ASSET("headicons_prayer_1"),
    UI_ASSET("headicons_prayer_2"),
    UI_ASSET("headicons_prayer_3"),
    UI_ASSET("headicons_prayer_4"),
    UI_ASSET("headicons_prayer_5"),
#define SPELL_ICON(book, name, sprite) UI_ASSET(#sprite),
#include "spell_icons.inc"
#undef SPELL_ICON
};

_Static_assert(sizeof(g_ui_asset_specs) / sizeof(g_ui_asset_specs[0]) <= RUNEC_UI_ASSET_MAX,
               "UI asset table exceeds texture capacity");

static int ui_asset_count(void) {
    return (int)(sizeof(g_ui_asset_specs) / sizeof(g_ui_asset_specs[0]));
}

static int find_asset_index(const char *name) {
    int count = ui_asset_count();
    for (int i = 0; i < count; i++) {
        if (strcmp(g_ui_asset_specs[i].name, name) == 0)
            return i;
    }
    return -1;
}

#define UI_FONT_FILE "data/fonts/osrs_fonts.bin"
#define UI_FONT_GLYPHS 256
#define UI_FONT_GRID 16

// Reads one font (see export_fonts.py) into a 16x16 glyph atlas; returns the
// bytes read, 0 on a malformed record.
static size_t load_font(RuneCUiFont *font, const unsigned char *data,
                        size_t size) {
    const size_t header = 8 + UI_FONT_GLYPHS * 5;
    if (size < header) return 0;
    font->ascent = (int)(data[4] | data[5] << 8 | data[6] << 16 | data[7] << 24);
    const unsigned char *records = data + 8;
    size_t coverage = 0;
    int min_top = INT32_MAX, max_bottom = INT32_MIN;
    for (int c = 0; c < UI_FONT_GLYPHS; c++) {
        const unsigned char *r = records + c * 5;
        font->advance[c] = r[0];
        font->width[c] = r[1];
        font->height[c] = r[2];
        font->left[c] = r[3];
        font->top[c] = r[4];
        coverage += (size_t)r[1] * r[2];
        if (r[1] + 1 > font->cell_w) font->cell_w = r[1] + 1;
        if (r[2] + 1 > font->cell_h) font->cell_h = r[2] + 1;
        if (r[2] > 0 && r[4] < min_top) min_top = r[4];
        if (r[2] + r[4] > max_bottom) max_bottom = r[2] + r[4];
    }
    if (size < header + coverage) return 0;
    font->max_ascent = font->ascent - min_top;
    font->max_descent = max_bottom - font->ascent;

    int width = font->cell_w * UI_FONT_GRID;
    int height = font->cell_h * UI_FONT_GRID;
    Color *pixels = calloc((size_t)width * height, sizeof(Color));
    if (!pixels) return 0;
    const unsigned char *src = data + header;
    for (int c = 0; c < UI_FONT_GLYPHS; c++) {
        int cell_x = (c % UI_FONT_GRID) * font->cell_w;
        int cell_y = (c / UI_FONT_GRID) * font->cell_h;
        for (int y = 0; y < font->height[c]; y++) {
            for (int x = 0; x < font->width[c]; x++) {
                pixels[(cell_y + y) * width + cell_x + x] =
                    (Color){255, 255, 255, *src++};
            }
        }
    }
    Image image = {pixels, width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    font->atlas = LoadTextureFromImage(image);
    free(pixels);
    if (font->atlas.id == 0) return 0;
    SetTextureFilter(font->atlas, TEXTURE_FILTER_POINT);
    return header + coverage;
}

static int load_fonts(RuneCUiAssets *assets) {
    RcAssetBytes bytes = rc_asset_read_all(UI_FONT_FILE);
    if (!bytes.data) return 0;
    const unsigned char *data = bytes.data;
    size_t size = bytes.size;
    int loaded = 0;
    if (size >= 8 && memcmp(data, "RCFT", 4) == 0
            && (data[4] | data[5] << 8) == RUNEC_FONT_COUNT) {
        size_t pos = 8;
        while (loaded < RUNEC_FONT_COUNT) {
            size_t used = load_font(&assets->fonts[loaded], data + pos,
                                    size - pos);
            if (!used) break;
            pos += used;
            loaded++;
        }
    }
    rc_asset_bytes_free(&bytes);
    return loaded == RUNEC_FONT_COUNT;
}

void runec_ui_assets_load(RuneCUiAssets *assets) {
    memset(assets, 0, sizeof(*assets));

    int count = ui_asset_count();

    for (int i = 0; i < count; i++) {
        char path[256];
        snprintf(path, sizeof(path), "%s", g_ui_asset_specs[i].file);
        if (g_ui_asset_specs[i].required)
            assets->required_count++;
        if (!rc_asset_exists(path)) {
            assets->missing_count++;
            if (g_ui_asset_specs[i].required) {
                assets->missing_required_count++;
                fprintf(stderr, "ui_assets: missing required sprite %s (%s)\n",
                        g_ui_asset_specs[i].name, path);
            } else {
                assets->missing_optional_count++;
            }
            continue;
        }
        Texture2D tex = runec_load_texture_asset(path);
        if (tex.id == 0) {
            assets->missing_count++;
            if (g_ui_asset_specs[i].required) {
                assets->missing_required_count++;
                fprintf(stderr, "ui_assets: failed to load required sprite %s (%s)\n",
                        g_ui_asset_specs[i].name, path);
            } else {
                assets->missing_optional_count++;
            }
            continue;
        }
        SetTextureFilter(tex, TEXTURE_FILTER_POINT);
        assets->textures[i] = tex;
        assets->loaded[i] = 1;
        assets->loaded_count++;
    }
    fprintf(stderr,
            "ui_assets: loaded %d/%d required sprites, %d optional sprites missing\n",
            assets->required_count - assets->missing_required_count,
            assets->required_count, assets->missing_optional_count);

    if (!load_fonts(assets))
        fprintf(stderr, "ui_assets: failed to load %s\n", UI_FONT_FILE);
}

void runec_ui_assets_unload(RuneCUiAssets *assets) {
    int count = ui_asset_count();

    for (int i = 0; i < count; i++) {
        if (assets->loaded[i]) {
            UnloadTexture(assets->textures[i]);
            assets->loaded[i] = 0;
        }
    }
    for (int i = 0; i < RUNEC_FONT_COUNT; i++) {
        if (assets->fonts[i].atlas.id != 0) {
            UnloadTexture(assets->fonts[i].atlas);
            assets->fonts[i].atlas.id = 0;
        }
    }
    assets->loaded_count = 0;
    assets->missing_count = 0;
    assets->required_count = 0;
    assets->missing_required_count = 0;
    assets->missing_optional_count = 0;
}

const Texture2D *runec_ui_asset(const RuneCUiAssets *assets, const char *name) {
    int index = find_asset_index(name);
    if (index < 0 || index >= RUNEC_UI_ASSET_MAX || !assets->loaded[index])
        return NULL;
    return &assets->textures[index];
}

int runec_ui_asset_ready(const RuneCUiAssets *assets, const char *name) {
    return runec_ui_asset(assets, name) != NULL;
}

Image runec_ui_asset_image(const char *name) {
    int index = find_asset_index(name);
    if (index < 0) return (Image){0};
    return runec_load_image_asset(g_ui_asset_specs[index].file);
}

void runec_ui_draw_asset(const RuneCUiAssets *assets, const char *name,
                         Rectangle dst, Color tint) {
    const Texture2D *tex = runec_ui_asset(assets, name);
    if (!tex)
        return;
    Rectangle src = {0, 0, (float)tex->width, (float)tex->height};
    DrawTexturePro(*tex, src, dst, (Vector2){0, 0}, 0.0f, tint);
}

// Advances past tags at *text and returns the next glyph, or -1 at end. color
// follows <col> tags when given.
static int next_glyph(const char **text, const char *end, Color *color,
                      Color base) {
    const char *p = *text;
    while (p < end && *p == '<') {
        const char *close = memchr(p, '>', (size_t)(end - p));
        if (!close) break;
        size_t len = (size_t)(close - p) + 1;
        if (len == 12 && strncmp(p, "<col=", 5) == 0) {
            if (color) {
                unsigned long rgb = strtoul(p + 5, NULL, 16);
                *color = (Color){(unsigned char)(rgb >> 16),
                                 (unsigned char)(rgb >> 8),
                                 (unsigned char)rgb, base.a};
            }
        } else if (len == 6 && strncmp(p, "</col>", 6) == 0) {
            if (color) *color = base;
        } else if (len == 4 && strncmp(p, "<lt>", 4) == 0) {
            *text = close + 1;
            return '<';
        } else if (len == 4 && strncmp(p, "<gt>", 4) == 0) {
            *text = close + 1;
            return '>';
        } else {
            break;
        }
        p = close + 1;
    }
    if (p >= end) {
        *text = p;
        return -1;
    }
    *text = p + 1;
    return (unsigned char)*p;
}

static int span_width(const RuneCUiFont *font, const char *text,
                      const char *end) {
    int width = 0;
    for (int c; (c = next_glyph(&text, end, NULL, BLANK)) >= 0;)
        width += font->advance[c];
    return width;
}

static void draw_glyph(const RuneCUiFont *font, int c, int x, int y,
                       Color color) {
    float w = (float)font->width[c];
    float h = (float)font->height[c];
    if (w <= 0.0f || h <= 0.0f) return;
    Rectangle src = {(float)((c % 16) * font->cell_w),
                     (float)((c / 16) * font->cell_h), w, h};
    Rectangle dst = {(float)(x + font->left[c]), (float)(y + font->top[c]), w, h};
    DrawTexturePro(font->atlas, src, dst, (Vector2){0, 0}, 0.0f, color);
}

static void draw_span(const RuneCUiFont *font, const char *text,
                      const char *end, int x, int baseline, Color *color,
                      Color base, int shadow) {
    int y = baseline - font->ascent;
    Color black = {0, 0, 0, base.a};
    for (int c; (c = next_glyph(&text, end, color, base)) >= 0;) {
        if (shadow) draw_glyph(font, c, x + 1, y + 1, black);
        draw_glyph(font, c, x, y, *color);
        x += font->advance[c];
    }
}

int runec_ui_text_width(const RuneCUiAssets *assets, RuneCUiFontId font,
                        const char *text) {
    return span_width(&assets->fonts[font], text, text + strlen(text));
}

void runec_ui_draw_text(const RuneCUiAssets *assets, RuneCUiFontId font,
                        const char *text, int x, int baseline, Color color,
                        int shadow) {
    const RuneCUiFont *f = &assets->fonts[font];
    if (!text || f->atlas.id == 0) return;
    Color current = color;
    draw_span(f, text, text + strlen(text), x, baseline, &current, color,
              shadow);
}

void runec_ui_draw_text_box(const RuneCUiAssets *assets, RuneCUiFontId font,
                            const char *text, Rectangle box, int x_align,
                            int y_align, Color color, int shadow) {
    const RuneCUiFont *f = &assets->fonts[font];
    if (!text || f->atlas.id == 0) return;
    int lines = 1;
    for (const char *p = text; (p = strstr(p, "<br>")); p += 4) lines++;
    int x = (int)box.x, y = (int)box.y, w = (int)box.width, h = (int)box.height;
    int baseline = y + f->max_ascent;
    if (y_align == 1)
        baseline += (h - f->max_ascent - f->max_descent
                     - f->ascent * (lines - 1)) / 2;
    else if (y_align == 2)
        baseline = y + h - f->max_descent - f->ascent * (lines - 1);
    Color current = color;
    const char *line = text;
    for (int i = 0; i < lines; i++) {
        const char *end = strstr(line, "<br>");
        if (!end) end = line + strlen(line);
        int width = span_width(f, line, end);
        int line_x = x_align == 1 ? x + (w - width) / 2
                   : x_align == 2 ? x + w - width : x;
        draw_span(f, line, end, line_x, baseline, &current, color, shadow);
        baseline += f->ascent;
        line = end + 4;
    }
}

// End of the row starting at text: after the last space or hyphen before the
// width runs out, as AbstractFont.breakLines. A space at the break is dropped,
// so *next says where the following row starts.
static const char *wrap_row(const RuneCUiFont *font, const char *text,
                            const char *end, int width, const char **next) {
    const char *p = text, *row_end = NULL, *row_next = NULL;
    int x = 0;
    for (int c; (c = next_glyph(&p, end, NULL, BLANK)) >= 0;) {
        x += font->advance[c];
        if (c == ' ') {
            row_end = p - 1;
            row_next = p;
        }
        if (x > width && row_end) {
            *next = row_next;
            return row_end;
        }
        if (c == '-') row_end = row_next = p;
    }
    *next = end;
    return end;
}

int runec_ui_text_rows(const RuneCUiAssets *assets, RuneCUiFontId font,
                       const char *text, int width) {
    const char *end = text + strlen(text), *next = text;
    int rows = 0;
    do {
        wrap_row(&assets->fonts[font], next, end, width, &next);
        rows++;
    } while (next < end);
    return rows;
}

void runec_ui_draw_text_wrapped(const RuneCUiAssets *assets, RuneCUiFontId font,
                                const char *text, int x, int y, int width,
                                int line_height, Color color, int shadow) {
    const RuneCUiFont *f = &assets->fonts[font];
    if (!text || f->atlas.id == 0) return;
    const char *end = text + strlen(text), *next;
    Color current = color;
    int baseline = y + f->max_ascent;
    for (;;) {
        const char *row_end = wrap_row(f, text, end, width, &next);
        draw_span(f, text, row_end, x, baseline, &current, color, shadow);
        if (next >= end) break;
        text = next;
        baseline += line_height;
    }
}
