#include "../../rc-viewer/ui.h"
#include <stdio.h>
#include <string.h>

// Context hit tests need dimensions and text metrics, not a graphics context.
static int test_screen_width(void) { return 1280; }
static int test_screen_height(void) { return 720; }
static Vector2 input_mouse;
static int input_button = -1, input_key = -1, input_shift;
static const char *input_text = "";
static Vector2 test_mouse(void) { return input_mouse; }
static bool test_button(int button) { return button == input_button; }
static bool test_key(int key) { return key == input_key; }
static bool test_key_down(int key) { return input_shift && key == KEY_LEFT_SHIFT; }
static int test_character(void) { return *input_text ? *input_text++ : 0; }
static float test_zero(void) { return 0; }
static bool test_no_button(int button) { (void)button; return false; }
static int hover_draw_count;
static char hover_draw_text[6][180];
static Color hover_draw_colors[6];
static void test_draw_text(Font font, const char *text, Vector2 pos, float size, float spacing, Color color) {
    (void)font; (void)pos; (void)size; (void)spacing;
    if (hover_draw_count < 6) {
        snprintf(hover_draw_text[hover_draw_count], 180, "%s", text);
        hover_draw_colors[hover_draw_count++] = color;
    }
}
static int drawn_x, drawn_y, drawn_icon, unloaded_icon;
static void test_draw_texture(Texture2D texture, int x, int y, Color tint) {
    (void)tint;
    drawn_x = x; drawn_y = y; drawn_icon = texture.id;
}
static void test_unload_texture(Texture2D texture) { unloaded_icon = texture.id; }
static int missing_icon_drawn;
static void test_text_shadow(const RuneCUiAssets *assets, const char *text,
                              float x, float y, float size, Color color) {
    (void)assets; (void)x; (void)y; (void)size; (void)color;
    missing_icon_drawn = strcmp(text, "?") == 0;
}
static Vector2 test_measure_text(Font font, const char *text, float size, float spacing) {
    (void)font; (void)spacing;
    return (Vector2){strlen(text) * size / 2, size};
}
#define GetScreenWidth test_screen_width
#define GetScreenHeight test_screen_height
#define GetMousePosition test_mouse
#define IsMouseButtonPressed test_button
#define IsMouseButtonReleased test_no_button
#define IsMouseButtonDown test_no_button
#define IsKeyPressed test_key
#define IsKeyDown test_key_down
#define GetCharPressed test_character
#define GetMouseWheelMove test_zero
#define GetFrameTime test_zero
#define DrawTextEx test_draw_text
#define MeasureTextEx test_measure_text
#define DrawTexture test_draw_texture
#define UnloadTexture test_unload_texture
#define runec_ui_draw_text_shadow test_text_shadow
#include "../../rc-viewer/ui.c"
#include "../../rc-viewer/dev_validation.h"
#include "api.h"
#include "combat.h"
#include "spells.h"
#include <assert.h>
#include <time.h>

#include "../../rc-content/content.h"
#include "items.h"
#include "storage.h"
#include "../storage_fixture.h"

static void test_bank_ui(RcWorld *world) {
    RuneCUiState *ui = calloc(1, sizeof(*ui));
    assert(ui);
    ui->active_tab = RUNEC_UI_TAB_INVENTORY;
    ui->bank_open = 1;
    ui->bank[4] = (RuneCUiSlot){.item_id=23685,.quantity=10000,.enabled=1,.generation=7};
    strcpy(ui->bank[4].label, "Divine super combat potion(4)");
    ui->inventory[0] = (RuneCUiSlot){.item_id=385,.quantity=1,.enabled=1,.generation=3,.action_count=4};
    strcpy(ui->inventory[0].label,"Shark");
    strcpy(ui->inventory[0].actions[0],"Eat");
    RuneCUiLayout layout;
    ui_layout(1280,720,&layout);
    Rectangle panel = bank_panel_rect(1280,720,&layout);
    Rectangle cell = bank_slot_rect(panel,0);
    Vector2 bank_mouse = {cell.x+10,cell.y+10};
    RuneCUiItemHover hover;
    assert(item_hover_text(ui,&layout,1280,720,bank_mouse,0,&hover));
    assert(!strcmp(hover.action,"Withdraw-1") && hover.more_options == 6);
    assert(!strcmp(hover.target,"Divine super combat potion(4)"));
    assert(item_hover_text(ui,&layout,1280,720,bank_mouse,1,&hover));
    assert(!strcmp(hover.action,"Withdraw-All"));
    input_mouse = bank_mouse;
    draw_item_hover(ui,&layout,1280,720);
    assert(hover_draw_count == 6 && !strcmp(hover_draw_text[1],"Withdraw-1"));
    assert(!strcmp(hover_draw_text[3]," Divine super combat potion(4)"));
    assert(!strcmp(hover_draw_text[5]," / 6 more options"));
    assert(hover_draw_colors[3].r == 255 && hover_draw_colors[3].g == 144 && hover_draw_colors[3].b == 64);
    assert(hover_draw_colors[1].g == 255 && hover_draw_colors[5].g == 255);
    assert(!item_hover_text(ui,&layout,1280,720,(Vector2){panel.x+2,panel.y+2},0,&hover));
    assert(!bank_source_slot(ui,RUNEC_UI_CONTEXT_BANK,-1));
    assert(!bank_source_slot(ui,RUNEC_UI_CONTEXT_NONE,0));
    set_bank_context(ui,input_mouse,RUNEC_UI_CONTEXT_BANK,3);
    assert(!ui->context_open);

    // Native input translation executes headlessly, including menu priority over the bank below it.
    input_button = MOUSE_BUTTON_RIGHT;
    assert(runec_ui_handle_input(ui,1280,720));
    assert(ui->context_open && ui->context_action_count == 8 && ui->context_source_slot == 4);
    assert(!item_hover_text(ui,&layout,1280,720,bank_mouse,0,&hover));
    RuneCContextMenuLayout menu = context_menu_layout_for_ui(ui);
    input_mouse = (Vector2){menu.x+8, menu.y+RUNEC_CONTEXT_MENU_HEADER_HEIGHT+RUNEC_CONTEXT_MENU_ROW_HEIGHT+5};
    input_button = MOUSE_BUTTON_LEFT;
    assert(runec_ui_handle_input(ui,1280,720));
    assert(!ui->context_open && ui->last_intent.kind == RUNEC_UI_INTENT_BANK_WITHDRAW);
    assert(ui->last_intent.primary == 4 && ui->last_intent.secondary == 5);
    world->enabled |= RC_SUB_STORAGE;
    test_storage_open(world);
    world->player.bank[4] = (RcInvSlot){.item_id=23685,.quantity=10000};
    assert(rc_bank_withdraw_slot(world,ui->last_intent.primary,ui->last_intent.secondary));
    rc_world_tick(world);
    assert(world->player.bank[4].quantity == 9995);
    int withdrawn = 0;
    for (int i = 0; i < RC_INVENTORY_SIZE; i++)
        if (world->player.inventory[i].item_id == 23685) withdrawn += world->player.inventory[i].quantity;
    assert(withdrawn == 5);

    clear_intent(ui);
    ui->bank[4].quantity = 1;
    submit_bank_action(ui,RUNEC_UI_CONTEXT_BANK,4,23685,7,BANK_ALL_BUT_ONE);
    assert(ui->last_intent.kind == RUNEC_UI_INTENT_NONE);
    ui->bank[4].quantity = 10000;
    submit_bank_action(ui,RUNEC_UI_CONTEXT_BANK,4,23685,7,BANK_ALL_BUT_ONE);
    assert(ui->last_intent.kind == RUNEC_UI_INTENT_BANK_WITHDRAW_ALL_BUT_ONE
           && ui->last_intent.secondary == 0);
    clear_intent(ui);
    submit_bank_action(ui,RUNEC_UI_CONTEXT_BANK,4,23685,6,1);
    assert(ui->last_intent.kind == RUNEC_UI_INTENT_NONE);
    submit_bank_action(ui,RUNEC_UI_CONTEXT_BANK,4,23685,7,RUNEC_UI_ITEM_OP_EXAMINE);
    assert(ui->last_intent.kind == RUNEC_UI_INTENT_BANK_EXAMINE && ui->last_intent.primary == 23685);
    submit_bank_action(ui,RUNEC_UI_CONTEXT_BANK,4,23685,7,BANK_AMOUNT_X);
    assert(!item_hover_text(ui,&layout,1280,720,bank_mouse,0,&hover));
    input_button = -1;
    input_key = KEY_ENTER;
    input_text = "0";
    assert(runec_ui_handle_input(ui,1280,720) && ui->bank_amount_source);
    input_key = KEY_BACKSPACE;
    assert(runec_ui_handle_input(ui,1280,720) && !ui->bank_amount_text[0]);
    input_key = KEY_ENTER;
    input_text = "9999999999";
    assert(runec_ui_handle_input(ui,1280,720) && ui->bank_amount_source);
    ui->bank_amount_text[0] = 0;
    input_text = "7";
    assert(runec_ui_handle_input(ui,1280,720) && !ui->bank_amount_source);
    assert(ui->last_intent.kind == RUNEC_UI_INTENT_BANK_WITHDRAW && ui->last_intent.secondary == 7);
    input_key = -1;

    cell = inv_slot_rect(&layout,0);
    input_mouse = (Vector2){cell.x+8,cell.y+8};
    input_button = MOUSE_BUTTON_RIGHT;
    assert(runec_ui_handle_input(ui,1280,720));
    assert(ui->context_source_kind == RUNEC_UI_CONTEXT_BANK_INVENTORY);
    assert(ui->context_action_count == 7 && !strcmp(ui->context_actions[0],"Deposit-1"));
    // Deposit through the same menu/input path, using the item just withdrawn.
    ui->inventory[0].item_id = world->player.inventory[0].item_id;
    ui->inventory[0].generation = world->player.inventory[0].generation;
    set_bank_context(ui,input_mouse,RUNEC_UI_CONTEXT_BANK_INVENTORY,0);
    menu = context_menu_layout_for_ui(ui);
    Vector2 inventory_mouse = input_mouse;
    input_mouse = (Vector2){menu.x+8,menu.y+RUNEC_CONTEXT_MENU_HEADER_HEIGHT+5};
    input_button = MOUSE_BUTTON_LEFT;
    assert(runec_ui_handle_input(ui,1280,720));
    assert(ui->last_intent.kind == RUNEC_UI_INTENT_BANK_DEPOSIT && ui->last_intent.secondary == 1);
    assert(rc_bank_deposit_slot(world,ui->last_intent.primary,ui->last_intent.secondary));
    rc_world_tick(world);
    assert(world->player.bank[4].quantity == 9996 && world->player.inventory[0].item_id == -1);
    ui->inventory[0].item_id = 385;
    ui->inventory[0].generation = 3;
    input_mouse = inventory_mouse;
    close_context(ui);
    assert(item_hover_text(ui,&layout,1280,720,input_mouse,0,&hover));
    assert(!strcmp(hover.action,"Deposit-1") && hover.more_options == 5);
    submit_bank_action(ui,RUNEC_UI_CONTEXT_BANK_INVENTORY,0,385,3,0);
    assert(ui->last_intent.kind == RUNEC_UI_INTENT_BANK_DEPOSIT && !ui->last_intent.secondary);
    submit_bank_action(ui,RUNEC_UI_CONTEXT_BANK_INVENTORY,0,385,3,BANK_AMOUNT_X);
    input_key = KEY_ESCAPE;
    input_button = -1;
    assert(runec_ui_handle_input(ui,1280,720) && !ui->bank_amount_source);
    set_bank_context(ui,input_mouse,RUNEC_UI_CONTEXT_BANK_INVENTORY,0);
    menu = context_menu_layout_for_ui(ui);
    input_mouse = (Vector2){menu.x+8,menu.y+RUNEC_CONTEXT_MENU_HEADER_HEIGHT+6*RUNEC_CONTEXT_MENU_ROW_HEIGHT+5};
    input_key = -1;
    input_button = MOUSE_BUTTON_LEFT;
    assert(runec_ui_handle_input(ui,1280,720));
    assert(!ui->context_open && ui->last_intent.kind == RUNEC_UI_INTENT_NONE);
    input_mouse = inventory_mouse;
    input_button = -1;
    input_key = -1;
    ui->bank_open = 0;
    assert(item_hover_text(ui,&layout,1280,720,input_mouse,0,&hover));
    assert(!strcmp(hover.action,"Eat") && hover.more_options == 3);
    set_selected_item_target(ui,0);
    assert(item_hover_text(ui,&layout,1280,720,input_mouse,0,&hover));
    assert(!strcmp(hover.action,"Use") && !strcmp(hover.target,"Shark -> Shark") && !hover.more_options);
    hover_draw_count = 0;
    draw_item_hover(ui,&layout,1280,720);
    assert(hover_draw_count == 6 && !hover_draw_text[5][0]);
    runec_ui_clear_selected_target(ui);
    ui->inventory[0].action_count = 0;
    assert(!item_hover_text(ui,&layout,1280,720,input_mouse,0,&hover));
    ui->active_tab = RUNEC_UI_TAB_SKILLS;
    assert(!item_hover_text(ui,&layout,1280,720,input_mouse,0,&hover));
    ui->active_tab = RUNEC_UI_TAB_INVENTORY;
    assert(!item_hover_text(ui,&layout,1280,720,(Vector2){0,0},0,&hover));
    ui->drag.active = 1;
    assert(!item_hover_text(ui,&layout,1280,720,input_mouse,0,&hover));
    hover_draw_count = 0;
    draw_item_hover(ui,&layout,1280,720);
    assert(!hover_draw_count);
    ui->drag.active = 0;
    ui->inventory[0].enabled = 0;
    assert(!item_hover_text(ui,&layout,1280,720,input_mouse,0,&hover));
    ui->bank_open = 1;
    for (int i = 0; i < RUNEC_UI_BANK_SLOT_COUNT; i++) {
        ui->bank[i] = (RuneCUiSlot){.item_id=385,.quantity=10000,.enabled=1};
        strcpy(ui->bank[i].label,"Shark");
    }
    ui->bank_scroll = RUNEC_UI_BANK_SLOT_COUNT - RUNEC_UI_BANK_VISIBLE_SLOTS;
    cell = bank_slot_rect(panel,RUNEC_UI_BANK_VISIBLE_SLOTS-1);
    input_mouse = (Vector2){cell.x+8,cell.y+8};
    assert(bank_slot_at(ui,panel,input_mouse) == RUNEC_UI_BANK_SLOT_COUNT-1);
    clock_t start = clock();
    for (int i = 0; i < 100000; i++)
        assert(item_hover_text(ui,&layout,1280,720,input_mouse,0,&hover));
    printf("Full-bank hover: %.3f us/frame (100000 queries)\n",
        1000000.0 * (clock()-start) / CLOCKS_PER_SEC / 100000);
    ui->bank_kind = RC_STORAGE_BANK;
    Rectangle mode = bank_mode_rect(panel,1);
    input_mouse = (Vector2){mode.x+4,mode.y+4};
    input_button = MOUSE_BUTTON_LEFT;
    assert(runec_ui_handle_input(ui,1280,720) && ui->bank_noted);
    mode = bank_mode_rect(panel,0);
    input_mouse = (Vector2){mode.x+4,mode.y+4};
    assert(runec_ui_handle_input(ui,1280,720) && !ui->bank_noted);
    ui->bank_scroll = 0;
    cell = bank_slot_rect(panel,0);
    input_mouse = (Vector2){cell.x+8,cell.y+8};
    assert(runec_ui_handle_input(ui,1280,720));
    assert(ui->last_intent.kind == RUNEC_UI_INTENT_BANK_WITHDRAW && ui->last_intent.secondary == 1);
    input_shift = 1;
    assert(runec_ui_handle_input(ui,1280,720));
    assert(ui->last_intent.kind == RUNEC_UI_INTENT_BANK_WITHDRAW && ui->last_intent.secondary == 0);
    input_shift = 0;
    Rectangle tab = bank_tab_rect(panel,1);
    input_mouse = (Vector2){tab.x+4,tab.y+4};
    assert(runec_ui_handle_input(ui,1280,720) && ui->bank_active_tab == 1);
    ui->bank_active_tab = 0;
    ui->bank_kind = RC_STORAGE_DEPOSIT_BOX;
    ui->bank_scroll = 0;
    ui->inventory[0].enabled = 1;
    cell = bank_slot_rect(panel,0);
    input_mouse = (Vector2){cell.x+8,cell.y+8};
    assert(bank_slot_at(ui,panel,input_mouse) == 0);
    assert(item_hover_text(ui,&layout,1280,720,input_mouse,0,&hover));
    assert(!strcmp(hover.action,"Deposit-1"));
    assert(runec_ui_handle_input(ui,1280,720));
    assert(ui->last_intent.kind == RUNEC_UI_INTENT_BANK_DEPOSIT && ui->last_intent.primary == 0);
    input_button = MOUSE_BUTTON_RIGHT;
    assert(runec_ui_handle_input(ui,1280,720));
    assert(ui->context_source_kind == RUNEC_UI_CONTEXT_BANK_INVENTORY);
    assert(!strcmp(ui->context_actions[0],"Deposit-1"));
    ui->context_open = 0;
    input_button = MOUSE_BUTTON_LEFT;
    for (int kind = RC_STORAGE_BANK; kind <= RC_STORAGE_DEPOSIT_BOX; kind++) {
        ui->bank_kind = kind;
        for (int worn = 0; worn < 2; worn++) {
            Rectangle button = bank_deposit_all_rect(panel,worn);
            input_mouse = (Vector2){button.x+4,button.y+4};
            assert(runec_ui_handle_input(ui,1280,720));
            assert(ui->last_intent.kind == RUNEC_UI_INTENT_BANK_DEPOSIT_ALL);
            assert(ui->last_intent.primary == worn);
            assert(item_hover_text(ui,&layout,1280,720,input_mouse,0,&hover));
            assert(!strcmp(hover.action,worn ? "Deposit worn items" : "Deposit inventory"));
            assert(!hover.more_options && !hover.target[0]);
        }
    }
    input_button = -1;
    free(ui);
    world->player.storage_kind = RC_STORAGE_NONE;
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) world->player.inventory[i] = (RcInvSlot){.item_id=-1};
}

static void equip(RcWorld *w, int id) {
    w->player.equipment[EQUIP_WEAPON] = (RcInvSlot){.item_id = -1};
    w->player.equipment[EQUIP_SHIELD] = (RcInvSlot){.item_id = -1};
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) w->player.inventory[i] = (RcInvSlot){.item_id = -1};
    assert(rc_inv_add(w->player.inventory, id, 1) == 0);
    assert(rc_item_result_accepted(rc_player_equip(w, 0)));
    rc_world_tick(w);
    assert(w->player.equipment[EQUIP_WEAPON].item_id == id);
}

int main(void) {
    RuneCUiState *ui = calloc(1, sizeof(*ui));
    assert(ui);
    Texture2D icon = {.id = 9001, .width = 36, .height = 32};
    runec_ui_set_item_icon(NULL, 1, icon);
    runec_ui_set_item_icon(ui, 0, icon);
    assert(ui->item_icon_count == 0);
    runec_ui_set_item_icon(ui, 555, icon);
    RuneCUiSlot item = {.item_id = 555, .icon_item_id = 555};
    draw_inventory_item(ui, &item, (Rectangle){100.5f, 200.5f, 32, 32});
    assert(drawn_icon == 9001 && drawn_x == 98 && drawn_y == 200);
    runec_ui_set_item_icon(ui, 555, (Texture2D){0});
    assert(unloaded_icon == 9001 && ui->item_icon_count == 1);
    assert(ui_item_icon_texture(ui, 555)->id == 0 && ui->item_icons[0].ready);
    item.icon_item_id = 0;
    draw_inventory_item(ui, &item, (Rectangle){100, 200, 32, 32});
    assert(missing_icon_drawn);
    runec_ui_set_item_icon(ui, 560, (Texture2D){0});
    assert(ui->item_icon_count == 2 && !ui_item_icon_texture(ui, 999));
    ui->item_icon_count = RUNEC_UI_ITEM_ICON_CACHE;
    runec_ui_set_item_icon(ui, 999, icon);
    assert(ui->item_icon_count == RUNEC_UI_ITEM_ICON_CACHE && unloaded_icon == 9001);
    runec_ui_set_item_icon(ui, 999, (Texture2D){0});
    ui->item_icon_count = 2;
    // Texture metadata tests catalog resolution without creating a GL context.
    for (int i = 0; i < RUNEC_UI_ASSET_MAX; i++) {
        ui->assets.loaded[i] = 1;
        ui->assets.textures[i] = (Texture2D){.id = i + 1, .width = 40, .height = 40};
    }
    ui->current_spellbook = ui->autocast_slot = -1;
    ui->dev_spellbooks_enabled = 1;
    RuneCUiLayout layout;
    ui_layout(1280, 720, &layout);
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT | RC_SUB_INVENTORY | RC_SUB_EQUIPMENT | RC_SUB_PRAYER;
    cfg.items_path = RC_TEST_SOURCE_DIR "/data/defs/items.bin";
    cfg.spells_path = RC_TEST_SOURCE_DIR "/data/defs/spells.bin";
    cfg.player_actions_path = RC_TEST_SOURCE_DIR "/data/defs/player_actions.bin";
    cfg.prayers_path = RC_TEST_SOURCE_DIR "/data/defs/prayers.bin";
    cfg.varbits_path = RC_TEST_SOURCE_DIR "/data/defs/varbits.bin";
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    test_bank_ui(world);
    rc_content_combat_register(world);
    for (int i = 0; i < SKILL_COUNT; i++)
        world->player.skills.base_level[i] = world->player.skills.boosted_level[i] = 99;
    assert(setenv("RUNEC_DEV_VALIDATION", "1", 1) == 0);
    assert(!runec_dev_validation_seed_prayers(NULL));
    assert(runec_dev_validation_seed_prayers(world));
    assert(rc_prayer_available(world, RC_PRAYER_PIETY) == RC_PRAYER_OK);
    assert(runec_prayer_ui_sync(&ui->prayers, world));
    for (int i = 0; i < 31; i++) {
        const RuneCPrayerUiRef *ref = runec_prayer_ui_ref(i);
        assert(runec_ui_asset(&ui->assets, ref->available_asset));
        assert(runec_ui_asset(&ui->assets, ref->locked_asset));
    }
    assert(!runec_dev_validation_set_spellbook(NULL, 0));
    assert(!runec_dev_validation_set_spellbook(world, -1));
    assert(!runec_dev_validation_set_spellbook(world, 4));
    int counts[] = {76, 27, 46, 46};
    unsigned char mapped[RC_MAX_SPELL_DEFS] = {0};
    int icons = 0;
    for (int choice = 1; choice <= 4; choice++) {
        int book = choice % 4;
        equip(world, book == 1 ? 4675 : book == 3 ? 11791 : 1387);
        ui->active_tab = RUNEC_UI_TAB_CLAN;
        Rectangle button = dev_spellbook_button(&layout, book);
        assert(handle_side_clan_input(ui, &layout, (Vector2){button.x + 10, button.y + 10}));
        assert(ui->last_intent.kind == RUNEC_UI_INTENT_DEV_SPELLBOOK && ui->last_intent.primary == book);
        world->player.selected_spell = world->player.manual_spell_cast = world->player.autocast_spell = 0;
        ui->context_open = 1;
        set_selected_spell_target(ui, 0, "Old spell");
        assert(runec_dev_validation_set_spellbook(world, book));
        rc_world_tick(world);
        assert(runec_ui_sync_spellbook(ui, world));
        assert(world->player.current_spellbook == book && ui->current_spellbook == book);
        assert(world->player.selected_spell == -1 && world->player.autocast_spell == -1);
        assert(!ui->context_open && ui->selected_target.kind == RUNEC_UI_SELECTED_NONE);
        assert(ui->spell_count == counts[book] && ui->spell_scroll == 0);
        ui->active_tab = RUNEC_UI_TAB_SPELLBOOK;
        assert(spell_grid_visible(ui));
        for (int slot = 0; slot < ui->spell_count; slot++) {
            assert(ui->spell_icons[slot] && ui->spell_icons[slot]->width == 40);
            Rectangle cell = spell_grid_cell(ui, &layout, slot);
            assert(cell.x >= layout.side_content.x && cell.x + cell.width <= layout.side_content.x + 190);
            assert(cell.y >= layout.side_content.y && cell.y + cell.height <= layout.side_content.y + 240);
            Vector2 mouse = {cell.x + 2, cell.y + 2};
            assert(handle_spell_click(ui, &layout, mouse, 0));
            assert(ui->last_intent.kind == RUNEC_UI_INTENT_SELECTED_SPELL && ui->last_intent.primary == slot);
            assert(!strcmp(ui->last_intent.text, ui->spells[slot]->name));
            int spell_id = runec_ui_spell_runtime_id(ui, slot);
            if (spell_id >= 0) {
                mapped[spell_id] = 1;
                const RcSpellDef *spell = rc_spell_def_get(spell_id);
                for (int i = 0; i < RC_INVENTORY_SIZE; i++)
                    world->player.inventory[i] = (RcInvSlot){.item_id = -1};
                for (int i = 0; i < spell->rune_count; i++)
                    assert(rc_inv_add(world->player.inventory, spell->runes[i].item_id, spell->runes[i].qty * 10) >= 0);
                int before = world->player.selected_spell;
                RcSpellResult available = rc_spell_available(world, spell_id, 0);
                rc_player_select_spell(world, spell_id);
                rc_world_tick(world);
                assert(world->player.selected_spell == (available == RC_SPELL_OK ? spell_id : before));
                if (available != RC_SPELL_OK) assert(world->player.combat.failure_reason);
            }
            for (int action = 0; action < (ui->spell_can_autocast[slot] ? 3 : 1); action++) {
                assert(handle_spell_click(ui, &layout, mouse, 1));
                RuneCContextMenuLayout box = context_menu_layout_for_ui(ui);
                ui->last_intent = (RuneCUiIntent){0};
                assert(handle_context_click(ui, (Vector2){box.x + 5,
                    box.y + RUNEC_CONTEXT_MENU_HEADER_HEIGHT + action * RUNEC_CONTEXT_MENU_ROW_HEIGHT + 5}));
                assert(ui->last_intent.kind == (action ? RUNEC_UI_INTENT_AUTOCAST_SPELL : RUNEC_UI_INTENT_SELECTED_SPELL));
                assert(!strcmp(ui->last_intent.text, ui->spells[slot]->name));
                assert(ui->last_intent.secondary == (action == 2));
                if (action) {
                    assert(ui->selected_target.kind == RUNEC_UI_SELECTED_NONE);
                    rc_player_set_autocast_spell(world, spell_id, ui->last_intent.secondary);
                    rc_world_tick(world);
                    assert(world->player.autocast_spell == spell_id);
                    assert(world->player.defensive_autocast == (action == 2));
                }
            }
            icons++;
        }
        assert(!handle_spell_click(ui, &layout, (Vector2){0}, 0));
        assert(handle_spell_click(ui, &layout,
            (Vector2){layout.side_content.x + 90, layout.side_content.y + 245}, 0));
        assert(runec_ui_sync_spellbook(ui, world));
        ui->active_tab = RUNEC_UI_TAB_COMBAT;
        RcCombatViewState view;
        rc_combat_get_player_view(world, &view);
        runec_ui_set_combat_style_profile(ui, view.weapon_category);
        assert(!handle_autocast_button(ui, &layout, (Vector2){0}));
        for (int defensive = 0; defensive <= 1; defensive++) {
            Rectangle r = autocast_button(&layout, defensive);
            assert(handle_autocast_button(ui, &layout, (Vector2){r.x + 2, r.y + 2}));
            assert(ui->autocast_picker && ui->autocast_picker_defensive == defensive);
            int slots[RUNEC_UI_SPELL_MAX];
            int n = spell_display_slots(ui, slots);
            if (book == 2) assert(n == 0);
            else {
                assert(n > 0);
                Rectangle cell = spell_grid_cell(ui, &layout, 0);
                assert(handle_spell_click(ui, &layout, (Vector2){cell.x + 2, cell.y + 2}, 0));
                assert(ui->last_intent.kind == RUNEC_UI_INTENT_AUTOCAST_SPELL);
                assert(ui->last_intent.primary == slots[0] && ui->last_intent.secondary == defensive);
                assert(!ui->autocast_picker);
            }
            ui->autocast_picker = 1;
            handle_spell_click(ui, &layout, (Vector2){layout.side_content.x + 140, layout.side_content.y + 245}, 0);
            assert(!ui->autocast_picker);
        }
        ui->autocast_picker = 1;
        handle_spell_click(ui, &layout, (Vector2){layout.side_content.x + 10, layout.side_content.y + 245}, 0);
        assert(ui->last_intent.kind == RUNEC_UI_INTENT_AUTOCAST_SPELL && ui->last_intent.primary == -1);
        rc_player_set_autocast_spell(world, -1, 0);
        rc_combat_set_player_style(world, 1);
        rc_world_tick(world);
        assert(world->player.autocast_spell == -1 && world->player.combat_style != COMBAT_MAGIC);
        for (int i = 0; i < 3; i++) {
            Rectangle melee = combat_style_rect(ui, &layout, i);
            assert(combat_style_at(ui, &layout, (Vector2){melee.x + 2, melee.y + 2}));
            for (int d = 0; d < 2; d++) assert(!CheckCollisionRecs(melee, autocast_button(&layout, d)));
        }
    }
    assert(icons == 195);
    int combat_spells = 0;
    for (int i = 0; rc_spell_def_get(i); i++) {
        const RcSpellDef *spell = rc_spell_def_get(i);
        if (spell->type != RC_SPELL_TYPE_COMBAT || !spell->effect_flags) continue;
        if (!mapped[i]) fprintf(stderr, "Missing combat spell button: %s\n", spell->name);
        assert(mapped[i]);
        combat_spells++;
    }
    assert(combat_spells == 57);
    for (int i = 0; i < 2; i++) {
        equip(world, i ? 11907 : 4151);
        assert(runec_ui_sync_spellbook(ui, world) && !ui->autocast_available);
        assert(!ui->autocast_picker);
        Rectangle melee = combat_style_rect(ui, &layout, 0);
        assert(melee.height == RUNEC_OSRS_COMBAT_STYLES[0].rect.height);
        Rectangle r = autocast_button(&layout, 0);
        assert(!handle_autocast_button(ui, &layout, (Vector2){r.x + 2, r.y + 2}));
    }
    assert(!runec_ui_set_spellbook(NULL, 0));
    assert(!runec_ui_set_spellbook(ui, -1));
    assert(!runec_ui_set_spellbook(ui, 4));
    assert(!runec_ui_sync_spellbook(NULL, world) && !runec_ui_sync_spellbook(ui, NULL));
    assert(runec_ui_spell_runtime_id(NULL, 0) == -1 && runec_ui_spell_runtime_id(ui, -1) == -1);
    ui->active_tab = RUNEC_UI_TAB_SPELLBOOK;
    scroll_spell_grid(ui, 100);
    assert(ui->spell_scroll == 0);
    scroll_spell_grid(ui, -100);
    assert(ui->spell_scroll == 0);
    // The NPC tiles toggle sits clear of every other Validation control and
    // asks for the opposite of the current state.
    Rectangle tiles = side_clan_npc_tiles_rect(&layout);
    assert(tiles.x >= layout.side_content.x && tiles.y >= layout.side_content.y
           && tiles.x + tiles.width <= layout.side_content.x + layout.side_content.width
           && tiles.y + tiles.height <= layout.side_content.y + layout.side_content.height);
    assert(!CheckCollisionRecs(tiles, side_clan_plane_follow_rect(&layout)));
    for (int plane = 0; plane < RUNEC_UI_SCENE_PLANE_COUNT; plane++)
        assert(!CheckCollisionRecs(tiles, side_clan_plane_button_rect(&layout, plane)));
    for (int i = 0; i < RUNEC_UI_DEV_TRANSPORT_MAX; i++)
        assert(!CheckCollisionRecs(tiles, side_clan_dev_transport_rect(&layout, i)));
    // Every teleport button fits the panel above the Spellbook row.
    for (int i = 0; i < RUNEC_UI_DEV_TRANSPORT_MAX; i++) {
        Rectangle r = side_clan_dev_transport_rect(&layout, i);
        assert(r.x >= layout.side_content.x
               && r.x + r.width <= layout.side_content.x + layout.side_content.width);
        for (int book = 0; book < 4; book++)
            assert(!CheckCollisionRecs(r, dev_spellbook_button(&layout, book)));
        for (int j = 0; j < i; j++)
            assert(!CheckCollisionRecs(r, side_clan_dev_transport_rect(&layout, j)));
    }
    for (int book = 0; book < 4; book++)
        assert(!CheckCollisionRecs(tiles, dev_spellbook_button(&layout, book)));
    RuneCUiTab tab = ui->active_tab;
    ui->active_tab = RUNEC_UI_TAB_CLAN;
    runec_ui_set_npc_tiles_overlay(ui, 0);
    assert(handle_side_clan_input(ui, &layout, (Vector2){tiles.x + 5, tiles.y + 5}));
    assert(ui->last_intent.kind == RUNEC_UI_INTENT_DEV_NPC_TILES && ui->last_intent.primary == 1);
    runec_ui_set_npc_tiles_overlay(ui, 7);
    assert(ui->npc_tiles_overlay == 1);
    assert(handle_side_clan_input(ui, &layout, (Vector2){tiles.x + 5, tiles.y + 5}));
    assert(ui->last_intent.kind == RUNEC_UI_INTENT_DEV_NPC_TILES && ui->last_intent.primary == 0);
    runec_ui_set_npc_tiles_overlay(NULL, 1);
    ui->active_tab = tab;
    ui->dev_spellbooks_enabled = 0;
    assert(spell_grid_visible(ui));
    ui->active_tab = RUNEC_UI_TAB_CLAN;
    ui->last_intent.kind = RUNEC_UI_INTENT_NONE;
    Rectangle button = dev_spellbook_button(&layout, 0);
    handle_side_clan_input(ui, &layout, (Vector2){button.x + 5, button.y + 5});
    assert(ui->last_intent.kind == RUNEC_UI_INTENT_NONE);
    assert(setenv("RUNEC_DEV_VALIDATION", "0", 1) == 0);
    assert(!runec_dev_validation_set_spellbook(world, 1));
    assert(unsetenv("RUNEC_DEV_VALIDATION") == 0);
    clock_t start = clock();
    for (int i = 0; i < 10000; i++) assert(runec_ui_set_spellbook(ui, i % 4));
    printf("Spellbook icon projection: %.3f us/switch (10000 switches)\n",
        1000000.0 * (clock() - start) / CLOCKS_PER_SEC / 10000);
    assert(runec_ui_sync_spellbook(ui, world));
    start = clock();
    for (int i = 0; i < 1000000; i++) assert(runec_ui_sync_spellbook(ui, world));
    printf("Cached spellbook sync: %.3f us/frame (1000000 frames)\n",
        1000000.0 * (clock() - start) / CLOCKS_PER_SEC / 1000000);
    rc_world_destroy(world);
    free(ui);
    puts("Spellbook icons, casting, staff picker, defensive autocast, melee and disabled gate passed.");
    return 0;
}
