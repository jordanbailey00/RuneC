#include "context_menu.h"
#include "assets.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_osrs_dimensions_and_anchor(void) {
    assert(RUNEC_CONTEXT_MENU_DISMISS_MARGIN == 10);
    RuneCContextMenuLayout layout = runec_context_menu_layout(
        400, 200, 800, 600, 92, 4);
    assert(layout.width == 100);
    assert(layout.height == 82);
    assert(layout.x == 350);
    assert(layout.y == 200);
}

static void test_osrs_dismiss_margin(void) {
    RuneCContextMenuLayout layout = runec_context_menu_layout(
        400, 200, 800, 600, 92, 4);
    assert(runec_context_menu_contains_margin(&layout, 340, 190, 10));
    assert(runec_context_menu_contains_margin(&layout, 460, 292, 10));
    assert(!runec_context_menu_contains_margin(&layout, 339, 200, 10));
    assert(!runec_context_menu_contains_margin(&layout, 350, 293, 10));
}

static void test_screen_clamping(void) {
    RuneCContextMenuLayout top_left = runec_context_menu_layout(
        2, -4, 200, 100, 92, 4);
    assert(top_left.x == 0);
    assert(top_left.y == 0);

    RuneCContextMenuLayout bottom_right = runec_context_menu_layout(
        198, 135, 200, 140, 92, 4);
    assert(bottom_right.x == 100);
    assert(bottom_right.y == 58);
}

static void test_action_hit_rows(void) {
    RuneCContextMenuLayout layout = runec_context_menu_layout(
        400, 200, 800, 600, 92, 4);
    assert(runec_context_menu_contains(&layout, 350, 200));
    assert(!runec_context_menu_contains(&layout, 349, 200));
    assert(runec_context_menu_action_at(&layout, 4, 360, 218) == -1);
    assert(runec_context_menu_action_at(&layout, 4, 360, 219) == 0);
    assert(runec_context_menu_action_at(&layout, 4, 360, 233) == 0);
    assert(runec_context_menu_action_at(&layout, 4, 360, 234) == 1);
    assert(runec_context_menu_action_at(&layout, 4, 360, 278) == 3);
    assert(runec_context_menu_action_at(&layout, 4, 360, 279) == -1);
}

static void test_fonts_match_loose_and_packed_data(void) {
    const char *path = "data/fonts/osrs_fonts.bin";
    assert(rc_asset_reset());
    assert(rc_asset_set_backend(RC_ASSET_BACKEND_LOOSE));
    RcAssetBytes loose = rc_asset_read_all(path);
    assert(rc_asset_reset());
    assert(rc_asset_set_backend(RC_ASSET_BACKEND_PACK));
    RcAssetBytes packed = rc_asset_read_all(path);
    if (!loose.data || !packed.data || loose.size != packed.size
            || memcmp(loose.data, packed.data, loose.size) != 0) {
        fprintf(stderr,
                "context menu font mismatch: %s loose=%zu packed=%zu\n",
                path, loose.size, packed.size);
        rc_asset_bytes_free(&loose);
        rc_asset_bytes_free(&packed);
        assert(0);
    }
    rc_asset_bytes_free(&loose);
    rc_asset_bytes_free(&packed);
    assert(rc_asset_reset());
}

int main(void) {
    test_osrs_dimensions_and_anchor();
    test_osrs_dismiss_margin();
    test_screen_clamping();
    test_action_hit_rows();
    test_fonts_match_loose_and_packed_data();
    puts("context menu tests passed");
    return 0;
}
