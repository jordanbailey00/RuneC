#ifndef RUNEC_VIEWER_UI_ASSETS_H
#define RUNEC_VIEWER_UI_ASSETS_H

#include "raylib.h"

#define RUNEC_UI_ASSET_MAX 576

// The client's bitmap fonts (FontID 494-497), exported by export_fonts.py.
typedef enum RuneCUiFontId {
    RUNEC_FONT_P11 = 0,
    RUNEC_FONT_P12,
    RUNEC_FONT_B12,
    RUNEC_FONT_Q8,
    RUNEC_FONT_COUNT
} RuneCUiFontId;

typedef struct RuneCUiFont {
    Texture2D atlas;
    int cell_w;
    int cell_h;
    int ascent;
    int max_ascent;
    int max_descent;
    unsigned char advance[256];
    unsigned char width[256];
    unsigned char height[256];
    unsigned char left[256];
    unsigned char top[256];
} RuneCUiFont;

typedef struct RuneCUiAssets {
    Texture2D textures[RUNEC_UI_ASSET_MAX];
    unsigned char loaded[RUNEC_UI_ASSET_MAX];
    RuneCUiFont fonts[RUNEC_FONT_COUNT];
    int loaded_count;
    int missing_count;
    int required_count;
    int missing_required_count;
    int missing_optional_count;
} RuneCUiAssets;

void runec_ui_assets_load(RuneCUiAssets *assets);
void runec_ui_assets_unload(RuneCUiAssets *assets);

const Texture2D *runec_ui_asset(const RuneCUiAssets *assets, const char *name);
int runec_ui_asset_ready(const RuneCUiAssets *assets, const char *name);
// The sprite's pixels, for the widgets the client draws by hand. The caller
// unloads the image.
Image runec_ui_asset_image(const char *name);
void runec_ui_draw_asset(const RuneCUiAssets *assets, const char *name,
                         Rectangle dst, Color tint);
int runec_ui_text_width(const RuneCUiAssets *assets, RuneCUiFontId font,
                        const char *text);
// One line with its baseline at y, laid out as the client's AbstractFont:
// <col=rrggbb>, </col>, <lt> and <gt> are honoured, shadow is 1px down-right.
void runec_ui_draw_text(const RuneCUiAssets *assets, RuneCUiFontId font,
                        const char *text, int x, int baseline, Color color,
                        int shadow);
// Text in a box as a widget draws it: <br> breaks lines; x_align and y_align
// are 0 left/top, 1 centre, 2 right/bottom.
void runec_ui_draw_text_box(const RuneCUiAssets *assets, RuneCUiFontId font,
                            const char *text, Rectangle box, int x_align,
                            int y_align, Color color, int shadow);
// Rows text takes at this width, broken as the client's AbstractFont does.
int runec_ui_text_rows(const RuneCUiAssets *assets, RuneCUiFontId font,
                       const char *text, int width);
// Text wrapped to width from the top of (x, y); colours carry across rows.
void runec_ui_draw_text_wrapped(const RuneCUiAssets *assets, RuneCUiFontId font,
                                const char *text, int x, int y, int width,
                                int line_height, Color color, int shadow);

#endif
