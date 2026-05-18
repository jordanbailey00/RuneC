#ifndef RUNEC_VIEWER_UI_H
#define RUNEC_VIEWER_UI_H

#include "raylib.h"
#include "ui_assets.h"
#include "ui_interfaces.h"
#include <stddef.h>
#include <stdint.h>

#define RUNEC_UI_INV_SLOT_COUNT 28
#define RUNEC_UI_EQUIP_SLOT_COUNT 14
#define RUNEC_UI_BANK_SLOT_COUNT 800
#define RUNEC_UI_BANK_TAB_COUNT 5
#define RUNEC_UI_BANK_TAB_LABEL_LEN 16
#define RUNEC_UI_CHAT_LINES 7
#define RUNEC_UI_CHAT_TAB_COUNT 7
#define RUNEC_UI_CHAT_INPUT_MAX 128
#define RUNEC_UI_CONTEXT_ACTIONS 5
#define RUNEC_UI_MINIMAP_DOTS 256
#define RUNEC_UI_ITEM_ICON_CACHE 1024
#define RUNEC_UI_SKILL_COUNT 24
#define RUNEC_UI_OPEN_INTERFACES 32
#define RUNEC_UI_COMPONENT_OVERRIDES 256
#define RUNEC_UI_ITEM_CONTAINER_OVERRIDES 8
#define RUNEC_UI_EVENT_MASKS 512
#define RUNEC_UI_COMBAT_STYLE_COUNT 4
#define RUNEC_UI_DEV_TRANSPORT_MAX 8
#define RUNEC_UI_SCENE_PLANE_COUNT 4

typedef enum RuneCUiTab {
    RUNEC_UI_TAB_NONE = -1,
    RUNEC_UI_TAB_COMBAT = 0,
    RUNEC_UI_TAB_SKILLS,
    RUNEC_UI_TAB_QUESTS,
    RUNEC_UI_TAB_INVENTORY,
    RUNEC_UI_TAB_EQUIPMENT,
    RUNEC_UI_TAB_PRAYER,
    RUNEC_UI_TAB_SPELLBOOK,
    RUNEC_UI_TAB_SETTINGS,
    RUNEC_UI_TAB_CLAN,
    RUNEC_UI_TAB_COUNT
} RuneCUiTab;

typedef enum RuneCUiIntentKind {
    RUNEC_UI_INTENT_NONE = 0,
    RUNEC_UI_INTENT_TAB,
    RUNEC_UI_INTENT_INVENTORY_SLOT,
    RUNEC_UI_INTENT_EQUIPMENT_SLOT,
    RUNEC_UI_INTENT_PRAYER_SLOT,
    RUNEC_UI_INTENT_QUICK_PRAYER_SLOT,
    RUNEC_UI_INTENT_QUICK_PRAYER_TOGGLE,
    RUNEC_UI_INTENT_SPELL_SLOT,
    RUNEC_UI_INTENT_AUTOCAST_SPELL,
    RUNEC_UI_INTENT_SKILL_SLOT,
    RUNEC_UI_INTENT_CHAT_FOCUS,
    RUNEC_UI_INTENT_CHAT_SEND,
    RUNEC_UI_INTENT_MINIMAP_CLICK,
    RUNEC_UI_INTENT_RUN_TOGGLE,
    RUNEC_UI_INTENT_COMBAT_STYLE,
    RUNEC_UI_INTENT_AUTO_RETALIATE,
    RUNEC_UI_INTENT_SPECIAL_ATTACK,
    RUNEC_UI_INTENT_CONTEXT_ACTION,
    RUNEC_UI_INTENT_INVENTORY_ACTION,
    RUNEC_UI_INTENT_EQUIPMENT_ACTION,
    RUNEC_UI_INTENT_INVENTORY_DRAG,
    RUNEC_UI_INTENT_SELECTED_ITEM,
    RUNEC_UI_INTENT_SELECTED_SPELL,
    RUNEC_UI_INTENT_SELECTED_ITEM_ON_ITEM,
    RUNEC_UI_INTENT_SELECTED_SPELL_ON_ITEM,
    RUNEC_UI_INTENT_SELECTED_ITEM_ON_COMPONENT,
    RUNEC_UI_INTENT_SELECTED_SPELL_ON_COMPONENT,
    RUNEC_UI_INTENT_COMPONENT_ACTION,
    RUNEC_UI_INTENT_SELECTED_TARGET_CANCEL,
    RUNEC_UI_INTENT_BANK_WITHDRAW,
    RUNEC_UI_INTENT_BANK_DEPOSIT,
    RUNEC_UI_INTENT_BANK_CLOSE,
    RUNEC_UI_INTENT_SCENE_PLANE,
    RUNEC_UI_INTENT_DEV_TRANSPORT
} RuneCUiIntentKind;

typedef struct RuneCUiIntent {
    RuneCUiIntentKind kind;
    int primary;
    int secondary;
    Vector2 position;
    char text[RUNEC_UI_CHAT_INPUT_MAX];
} RuneCUiIntent;

typedef struct RuneCUiSlot {
    uint32_t item_id;
    uint32_t icon_item_id;
    int quantity;
    char label[24];
    int enabled;
    int category;
} RuneCUiSlot;

typedef enum RuneCUiMinimapDotKind {
    RUNEC_UI_MINIMAP_DOT_NPC = 0,
    RUNEC_UI_MINIMAP_DOT_PLAYER,
    RUNEC_UI_MINIMAP_DOT_DESTINATION
} RuneCUiMinimapDotKind;

typedef struct RuneCUiMinimapDot {
    float dx;
    float dy;
    RuneCUiMinimapDotKind kind;
} RuneCUiMinimapDot;

typedef struct RuneCUiItemIcon {
    uint32_t item_id;
    Texture2D texture;
    int ready;
} RuneCUiItemIcon;

typedef struct RuneCUiCombatStyleOption {
    int visible;
    int style_index;
    char label[24];
    char mode[32];
    char icon_asset[32];
} RuneCUiCombatStyleOption;

typedef enum RuneCUiContextSourceKind {
    RUNEC_UI_CONTEXT_NONE = 0,
    RUNEC_UI_CONTEXT_INVENTORY,
    RUNEC_UI_CONTEXT_EQUIPMENT,
    RUNEC_UI_CONTEXT_PRAYER,
    RUNEC_UI_CONTEXT_SPELL,
    RUNEC_UI_CONTEXT_COMPONENT
} RuneCUiContextSourceKind;

typedef enum RuneCUiSelectedTargetKind {
    RUNEC_UI_SELECTED_NONE = 0,
    RUNEC_UI_SELECTED_ITEM,
    RUNEC_UI_SELECTED_SPELL
} RuneCUiSelectedTargetKind;

typedef struct RuneCUiSelectedTarget {
    RuneCUiSelectedTargetKind kind;
    int source_slot;
    uint32_t source_item_id;
    uint32_t source_component_id;
    char label[48];
    char verb[24];
} RuneCUiSelectedTarget;

typedef struct RuneCUiDragState {
    int active;
    RuneCUiContextSourceKind source_kind;
    int source_slot;
    Vector2 start;
} RuneCUiDragState;

typedef enum RuneCUiOpenMount {
    RUNEC_UI_MOUNT_SCREEN = 0,
    RUNEC_UI_MOUNT_CHAT,
    RUNEC_UI_MOUNT_MAP,
    RUNEC_UI_MOUNT_SIDE,
    RUNEC_UI_MOUNT_SIDE_CONTENT,
    RUNEC_UI_MOUNT_OVERLAY,
    RUNEC_UI_MOUNT_MODAL
} RuneCUiOpenMount;

typedef struct RuneCUiOpenInterface {
    int active;
    RuneCUiOpenMount mount;
    RuneCUiTab tab;
    uint32_t target_component_id;
    int z_order;
    char group[48];
} RuneCUiOpenInterface;

typedef struct RuneCUiComponentEventMask {
    uint32_t component_id;
    uint32_t mask;
} RuneCUiComponentEventMask;

typedef struct RuneCUiState {
    RuneCUiTab active_tab;
    RuneCUiIntent last_intent;
    float tab_press_timer[RUNEC_UI_TAB_COUNT];

    RuneCUiSlot inventory[RUNEC_UI_INV_SLOT_COUNT];
    RuneCUiSlot equipment[RUNEC_UI_EQUIP_SLOT_COUNT];
    RuneCUiSlot bank[RUNEC_UI_BANK_SLOT_COUNT];
    int bank_open;
    int bank_kind;
    int bank_scroll;
    int bank_active_tab;
    char bank_tab_labels[RUNEC_UI_BANK_TAB_COUNT][RUNEC_UI_BANK_TAB_LABEL_LEN];
    int selected_inventory_slot;
    int selected_equipment_slot;
    int selected_combat_style;
    int auto_retaliate;
    int special_attack_enabled;
    int special_attack_energy;
    int combat_weapon_category;
    char combat_weapon_name[64];
    RuneCUiCombatStyleOption combat_styles[RUNEC_UI_COMBAT_STYLE_COUNT];

    int hitpoints;
    int hitpoints_max;
    int prayer_points;
    int prayer_points_max;
    uint32_t active_prayers;
    int run_energy;
    int combat_level;
    int skill_current[RUNEC_UI_SKILL_COUNT];
    int skill_base[RUNEC_UI_SKILL_COUNT];
    int skill_total;

    int world_x;
    int world_y;
    int local_x;
    int local_y;
    uint32_t tick;
    int running;
    int paused;

    int chat_focused;
    int active_chat_tab;
    char chat_input[RUNEC_UI_CHAT_INPUT_MAX];
    char chat_lines[RUNEC_UI_CHAT_LINES][96];
    int chat_line_count;
    int magic_filter_open;

    int context_open;
    Vector2 context_pos;
    char context_title[48];
    char context_actions[RUNEC_UI_CONTEXT_ACTIONS][32];
    int context_action_op[RUNEC_UI_CONTEXT_ACTIONS];
    int context_action_count;
    RuneCUiContextSourceKind context_source_kind;
    int context_source_slot;
    uint32_t context_source_item_id;
    uint32_t context_source_component_id;

    RuneCUiSelectedTarget selected_target;
    RuneCUiDragState drag;

    RuneCUiMinimapDot minimap_dots[RUNEC_UI_MINIMAP_DOTS];
    int minimap_dot_count;
    Texture2D minimap_texture;
    int minimap_texture_ready;
    RuneCUiItemIcon item_icons[RUNEC_UI_ITEM_ICON_CACHE];
    int item_icon_count;

    RuneCUiAssets assets;
    RuneCUiInterfaceStore interfaces;
    int decoded_ui_enabled;
    int decoded_ui_ready;
    char dev_transport_labels[RUNEC_UI_DEV_TRANSPORT_MAX][24];
    int dev_transport_count;
    int scene_plane;
    int player_plane;
    int scene_plane_override_active;
    RuneCUiOpenInterface open_interfaces[RUNEC_UI_OPEN_INTERFACES];
    int open_interface_count;
    RuneCUiComponentOverride component_overrides[RUNEC_UI_COMPONENT_OVERRIDES];
    int component_override_count;
    RuneCUiItemContainerOverride item_container_overrides[RUNEC_UI_ITEM_CONTAINER_OVERRIDES];
    int item_container_override_count;
    RuneCUiComponentEventMask event_masks[RUNEC_UI_EVENT_MASKS];
    int event_mask_count;
} RuneCUiState;

void runec_ui_init(RuneCUiState *ui);
void runec_ui_shutdown(RuneCUiState *ui);
void runec_ui_clear_minimap(RuneCUiState *ui);
void runec_ui_add_minimap_dot(RuneCUiState *ui, float dx, float dy,
                              RuneCUiMinimapDotKind kind);
void runec_ui_update_minimap(RuneCUiState *ui, const Color *pixels,
                             int width, int height);
void runec_ui_set_item_icon(RuneCUiState *ui, uint32_t icon_item_id, Texture2D texture);
void runec_ui_set_combat_weapon_name(RuneCUiState *ui, const char *name);
void runec_ui_set_combat_style_profile(RuneCUiState *ui, int core_weapon_category);
void runec_ui_set_active_tab(RuneCUiState *ui, RuneCUiTab tab);
void runec_ui_set_dev_transport_options(RuneCUiState *ui,
                                        const char *const *labels,
                                        int count);
void runec_ui_set_scene_plane_state(RuneCUiState *ui, int scene_plane,
                                    int player_plane, int override_active);
void runec_ui_sync_status(RuneCUiState *ui, int world_x, int world_y,
                          int local_x, int local_y, uint32_t tick,
                          int running, int paused);
void runec_ui_open_context(RuneCUiState *ui, Vector2 pos, const char *title,
                           const char **actions, int action_count);
void runec_ui_clear_selected_target(RuneCUiState *ui);
void runec_ui_clear_component_overrides(RuneCUiState *ui);
int runec_ui_set_component_text(RuneCUiState *ui, uint32_t component_id,
                                const char *text);
int runec_ui_set_component_hidden(RuneCUiState *ui, uint32_t component_id,
                                  int hidden);
int runec_ui_set_component_model(RuneCUiState *ui, uint32_t component_id,
                                 int model_type, int model_id);
int runec_ui_set_component_item(RuneCUiState *ui, uint32_t component_id,
                                uint32_t item_id, uint32_t icon_item_id,
                                int quantity, int selected);
int runec_ui_set_component_animation(RuneCUiState *ui, uint32_t component_id,
                                     int animation_id);
int runec_ui_set_component_color(RuneCUiState *ui, uint32_t component_id,
                                 int color);
int runec_ui_set_component_scroll(RuneCUiState *ui, uint32_t component_id,
                                  int scroll_x, int scroll_y);
int runec_ui_open_interface(RuneCUiState *ui, RuneCUiOpenMount mount,
                            RuneCUiTab tab, const char *group);
int runec_ui_open_top_interface(RuneCUiState *ui, const char *group);
int runec_ui_open_subinterface(RuneCUiState *ui, RuneCUiOpenMount mount,
                               RuneCUiTab tab, uint32_t target_component_id,
                               const char *group);
int runec_ui_open_overlay(RuneCUiState *ui, const char *group);
int runec_ui_open_modal(RuneCUiState *ui, const char *group);
int runec_ui_move_interface(RuneCUiState *ui, RuneCUiOpenMount from_mount,
                            RuneCUiTab from_tab, RuneCUiOpenMount to_mount,
                            RuneCUiTab to_tab, uint32_t target_component_id);
void runec_ui_close_interface(RuneCUiState *ui, RuneCUiOpenMount mount,
                              RuneCUiTab tab);
int runec_ui_handle_input(RuneCUiState *ui, int screen_w, int screen_h);
void runec_ui_draw(RuneCUiState *ui, int screen_w, int screen_h);
const char *runec_ui_tab_name(RuneCUiTab tab);
int runec_ui_runtime_selftest(RuneCUiState *ui, char *error, size_t error_cap);

#endif
