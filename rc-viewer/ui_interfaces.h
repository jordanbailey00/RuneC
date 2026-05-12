#ifndef RUNEC_VIEWER_UI_INTERFACES_H
#define RUNEC_VIEWER_UI_INTERFACES_H

#include "raylib.h"
#include "ui_assets.h"

#include <stdint.h>

#define RUNEC_UI_INTERFACE_MAX_ACTIONS 10
#define RUNEC_UI_INTERFACE_MAX_LISTENERS 18
#define RUNEC_UI_INTERFACE_MAX_LISTENER_VALUES 16
#define RUNEC_UI_INTERFACE_MAX_TRIGGERS 3
#define RUNEC_UI_INTERFACE_MAX_TRIGGER_VALUES 32
#define RUNEC_UI_INTERFACE_SPRITE_CACHE 256
#define RUNEC_UI_COMPONENT_OVERRIDE_TEXT   0x00000001u
#define RUNEC_UI_COMPONENT_OVERRIDE_HIDDEN 0x00000002u
#define RUNEC_UI_COMPONENT_OVERRIDE_MODEL  0x00000004u
#define RUNEC_UI_COMPONENT_OVERRIDE_COLOR  0x00000008u
#define RUNEC_UI_COMPONENT_OVERRIDE_SCROLL 0x00000010u
#define RUNEC_UI_COMPONENT_OVERRIDE_ITEM   0x00000020u
#define RUNEC_UI_COMPONENT_OVERRIDE_ANIM   0x00000040u
#define RUNEC_UI_ITEM_CONTAINER_MAX_SLOTS 64

typedef enum RuneCUiListenerKind {
    RUNEC_UI_LISTENER_ON_LOAD = 0,
    RUNEC_UI_LISTENER_ON_MOUSE_OVER,
    RUNEC_UI_LISTENER_ON_MOUSE_LEAVE,
    RUNEC_UI_LISTENER_ON_TARGET_LEAVE,
    RUNEC_UI_LISTENER_ON_TARGET_ENTER,
    RUNEC_UI_LISTENER_ON_VAR_TRANSMIT,
    RUNEC_UI_LISTENER_ON_INV_TRANSMIT,
    RUNEC_UI_LISTENER_ON_STAT_TRANSMIT,
    RUNEC_UI_LISTENER_ON_TIMER,
    RUNEC_UI_LISTENER_ON_OP,
    RUNEC_UI_LISTENER_ON_MOUSE_REPEAT,
    RUNEC_UI_LISTENER_ON_CLICK,
    RUNEC_UI_LISTENER_ON_CLICK_REPEAT,
    RUNEC_UI_LISTENER_ON_RELEASE,
    RUNEC_UI_LISTENER_ON_HOLD,
    RUNEC_UI_LISTENER_ON_DRAG,
    RUNEC_UI_LISTENER_ON_DRAG_COMPLETE,
    RUNEC_UI_LISTENER_ON_SCROLL_WHEEL,
    RUNEC_UI_LISTENER_COUNT
} RuneCUiListenerKind;

typedef enum RuneCUiTriggerKind {
    RUNEC_UI_TRIGGER_VAR_TRANSMIT = 0,
    RUNEC_UI_TRIGGER_INV_TRANSMIT,
    RUNEC_UI_TRIGGER_STAT_TRANSMIT,
    RUNEC_UI_TRIGGER_COUNT
} RuneCUiTriggerKind;

typedef enum RuneCUiListenerValueKind {
    RUNEC_UI_LISTENER_VALUE_INT = 0,
    RUNEC_UI_LISTENER_VALUE_STRING
} RuneCUiListenerValueKind;

typedef struct RuneCUiListenerValue {
    RuneCUiListenerValueKind kind;
    int32_t int_value;
    char *string_value;
} RuneCUiListenerValue;

typedef struct RuneCUiComponentListener {
    RuneCUiListenerKind kind;
    int value_count;
    RuneCUiListenerValue values[RUNEC_UI_INTERFACE_MAX_LISTENER_VALUES];
} RuneCUiComponentListener;

typedef struct RuneCUiComponentTrigger {
    RuneCUiTriggerKind kind;
    int value_count;
    int32_t values[RUNEC_UI_INTERFACE_MAX_TRIGGER_VALUES];
} RuneCUiComponentTrigger;

typedef struct RuneCUiComponent {
    uint32_t id;
    int32_t parent_id;
    uint32_t group_id;
    uint32_t file_id;
    unsigned char is_if3;
    unsigned char type;
    unsigned char hidden;
    unsigned char sprite_tiling;
    unsigned char filled;
    unsigned char line_direction;
    unsigned char text_shadowed;
    unsigned char flipped_vertically;
    unsigned char flipped_horizontally;
    unsigned char no_click_through;
    unsigned char opacity;
    unsigned char border_type;
    unsigned char line_width;
    unsigned char line_height;

    int32_t content_type;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t width_mode;
    int32_t height_mode;
    int32_t x_position_mode;
    int32_t y_position_mode;
    int32_t scroll_width;
    int32_t scroll_height;
    int32_t sprite_id;
    int32_t texture_id;
    int32_t shadow_color;
    int32_t model_id;
    int32_t model_type;
    int32_t font_id;
    int32_t text_color;
    uint32_t click_mask;
    int32_t x_text_alignment;
    int32_t y_text_alignment;

    char *name;
    char *text;
    char *target_verb;
    char *actions[RUNEC_UI_INTERFACE_MAX_ACTIONS];
    int action_count;
    uint32_t listener_mask;
    RuneCUiComponentListener listeners[RUNEC_UI_INTERFACE_MAX_LISTENERS];
    int listener_count;
    uint32_t trigger_mask;
    RuneCUiComponentTrigger triggers[RUNEC_UI_INTERFACE_MAX_TRIGGERS];
    int trigger_count;
} RuneCUiComponent;

typedef struct RuneCUiInterfaceGroup {
    uint32_t id;
    char *name;
    RuneCUiComponent *components;
    int component_count;
} RuneCUiInterfaceGroup;

typedef struct RuneCUiSpriteCacheEntry {
    int sprite_id;
    int missing;
    Texture2D texture;
} RuneCUiSpriteCacheEntry;

typedef struct RuneCUiInterfaceStore {
    RuneCUiInterfaceGroup *groups;
    int group_count;
    int loaded;
    RuneCUiSpriteCacheEntry sprites[RUNEC_UI_INTERFACE_SPRITE_CACHE];
} RuneCUiInterfaceStore;

typedef struct RuneCUiComponentOverride {
    uint32_t component_id;
    uint32_t flags;
    char text[128];
    int hidden;
    int selected;
    int32_t model_id;
    int32_t model_type;
    int32_t text_color;
    int32_t scroll_x;
    int32_t scroll_y;
    int32_t item_id;
    int32_t icon_item_id;
    int32_t item_quantity;
    int32_t animation_id;
} RuneCUiComponentOverride;

typedef struct RuneCUiItemContainerSlot {
    uint32_t item_id;
    uint32_t icon_item_id;
    int quantity;
    int enabled;
} RuneCUiItemContainerSlot;

typedef struct RuneCUiItemContainerOverride {
    uint32_t component_id;
    int slot_count;
    int columns;
    float x0;
    float y0;
    float step_x;
    float step_y;
    float slot_w;
    float slot_h;
    int selected_slot;
    RuneCUiItemContainerSlot slots[RUNEC_UI_ITEM_CONTAINER_MAX_SLOTS];
} RuneCUiItemContainerOverride;

typedef struct RuneCUiComponentOverrides {
    const RuneCUiComponentOverride *items;
    int count;
    const RuneCUiItemContainerOverride *containers;
    int container_count;
} RuneCUiComponentOverrides;

typedef struct RuneCUiHitResult {
    uint32_t component_id;
    uint32_t group_id;
    uint32_t file_id;
    Rectangle rect;
    uint32_t click_mask;
    uint32_t listener_mask;
    uint32_t trigger_mask;
    int action_count;
    char name[64];
    char text[128];
    char target_verb[64];
    char actions[RUNEC_UI_INTERFACE_MAX_ACTIONS][64];
} RuneCUiHitResult;

int runec_ui_interfaces_load(RuneCUiInterfaceStore *store, const char *path);
void runec_ui_interfaces_unload(RuneCUiInterfaceStore *store);
const RuneCUiInterfaceGroup *runec_ui_interface_group(
    const RuneCUiInterfaceStore *store, const char *name);

int runec_ui_interfaces_draw_group(RuneCUiInterfaceStore *store,
                                   const RuneCUiAssets *assets,
                                   const char *group_name,
                                   Rectangle mount);
int runec_ui_interfaces_draw_group_ex(RuneCUiInterfaceStore *store,
                                      const RuneCUiAssets *assets,
                                      const char *group_name,
                                      Rectangle mount,
                                      const RuneCUiComponentOverrides *overrides);

int runec_ui_interfaces_component_rect(const RuneCUiInterfaceStore *store,
                                       const char *group_name,
                                       const char *component_name,
                                       Rectangle mount,
                                       Rectangle *out_rect);
int runec_ui_interfaces_component_rect_by_id(const RuneCUiInterfaceStore *store,
                                             const char *group_name,
                                             uint32_t component_id,
                                             Rectangle mount,
                                             Rectangle *out_rect);

int runec_ui_interfaces_hit_test(const RuneCUiInterfaceStore *store,
                                 const char *group_name,
                                 Rectangle mount,
                                 Vector2 point,
                                 RuneCUiHitResult *out_hit);
int runec_ui_interfaces_hit_test_ex(const RuneCUiInterfaceStore *store,
                                    const char *group_name,
                                    Rectangle mount,
                                    Vector2 point,
                                    RuneCUiHitResult *out_hit,
                                    const RuneCUiComponentOverrides *overrides);

#endif
