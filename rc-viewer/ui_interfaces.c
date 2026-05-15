#include "ui_interfaces.h"

#include "../rc-core/assets.h"
#include "asset_raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RCUI_BIN_MAGIC "RCUIBIN2"
#define RCUI_BIN_VERSION_MIN 1u
#define RCUI_BIN_VERSION_MAX 2u
#define RUNEC_UI_COMPONENT_HIT_LISTENERS \
    ((1u << RUNEC_UI_LISTENER_ON_OP) \
     | (1u << RUNEC_UI_LISTENER_ON_CLICK) \
     | (1u << RUNEC_UI_LISTENER_ON_RELEASE) \
     | (1u << RUNEC_UI_LISTENER_ON_DRAG) \
     | (1u << RUNEC_UI_LISTENER_ON_DRAG_COMPLETE) \
     | (1u << RUNEC_UI_LISTENER_ON_TARGET_ENTER) \
     | (1u << RUNEC_UI_LISTENER_ON_TARGET_LEAVE))

typedef struct RuneCUiReader {
    const unsigned char *data;
    size_t size;
    size_t pos;
    int failed;
} RuneCUiReader;

typedef struct RuneCUiClipState {
    Rectangle current;
    int active;
} RuneCUiClipState;

static uint8_t read_u8(RuneCUiReader *r) {
    if (r->pos + 1 > r->size) {
        r->failed = 1;
        return 0;
    }
    return r->data[r->pos++];
}

static uint16_t read_u16(RuneCUiReader *r) {
    if (r->pos + 2 > r->size) {
        r->failed = 1;
        return 0;
    }
    uint16_t value = (uint16_t)r->data[r->pos]
        | ((uint16_t)r->data[r->pos + 1] << 8);
    r->pos += 2;
    return value;
}

static uint32_t read_u32(RuneCUiReader *r) {
    if (r->pos + 4 > r->size) {
        r->failed = 1;
        return 0;
    }
    uint32_t value = (uint32_t)r->data[r->pos]
        | ((uint32_t)r->data[r->pos + 1] << 8)
        | ((uint32_t)r->data[r->pos + 2] << 16)
        | ((uint32_t)r->data[r->pos + 3] << 24);
    r->pos += 4;
    return value;
}

static int32_t read_i32(RuneCUiReader *r) {
    return (int32_t)read_u32(r);
}

static char *read_string(RuneCUiReader *r) {
    uint16_t len = read_u16(r);
    if (r->failed || r->pos + len > r->size) {
        r->failed = 1;
        return NULL;
    }
    char *out = (char *)calloc((size_t)len + 1, 1);
    if (!out) {
        r->failed = 1;
        return NULL;
    }
    memcpy(out, r->data + r->pos, len);
    r->pos += len;
    return out;
}

static unsigned char *read_file_bytes(const char *path, size_t *out_size) {
    FILE *fp = rc_asset_fopen(path, "rb");
    if (!fp)
        return NULL;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0) {
        rc_asset_close(fp);
        return NULL;
    }
    unsigned char *data = (unsigned char *)malloc((size_t)size);
    if (!data) {
        rc_asset_close(fp);
        return NULL;
    }
    if (fread(data, 1, (size_t)size, fp) != (size_t)size) {
        free(data);
        rc_asset_close(fp);
        return NULL;
    }
    rc_asset_close(fp);
    *out_size = (size_t)size;
    return data;
}

static void free_component(RuneCUiComponent *component) {
    free(component->name);
    free(component->text);
    free(component->target_verb);
    for (int i = 0; i < component->action_count; i++)
        free(component->actions[i]);
    for (int i = 0; i < component->listener_count; i++) {
        RuneCUiComponentListener *listener = &component->listeners[i];
        for (int v = 0; v < listener->value_count; v++) {
            if (listener->values[v].kind == RUNEC_UI_LISTENER_VALUE_STRING)
                free(listener->values[v].string_value);
        }
    }
}

void runec_ui_interfaces_unload(RuneCUiInterfaceStore *store) {
    if (!store)
        return;
    for (int g = 0; g < store->group_count; g++) {
        RuneCUiInterfaceGroup *group = &store->groups[g];
        free(group->name);
        for (int c = 0; c < group->component_count; c++)
            free_component(&group->components[c]);
        free(group->components);
    }
    free(store->groups);
    store->groups = NULL;
    store->group_count = 0;
    store->loaded = 0;

    for (int i = 0; i < RUNEC_UI_INTERFACE_SPRITE_CACHE; i++) {
        if (store->sprites[i].texture.id != 0)
            UnloadTexture(store->sprites[i].texture);
        store->sprites[i] = (RuneCUiSpriteCacheEntry){0};
    }
}

int runec_ui_interfaces_load(RuneCUiInterfaceStore *store, const char *path) {
    if (!store || !path)
        return 0;
    memset(store, 0, sizeof(*store));

    size_t size = 0;
    unsigned char *bytes = read_file_bytes(path, &size);
    if (!bytes)
        return 0;

    RuneCUiReader r = {.data = bytes, .size = size};
    if (size < 16 || memcmp(bytes, RCUI_BIN_MAGIC, 8) != 0) {
        free(bytes);
        return 0;
    }
    r.pos = 8;
    uint32_t version = read_u32(&r);
    uint32_t group_count = read_u32(&r);
    if (version < RCUI_BIN_VERSION_MIN || version > RCUI_BIN_VERSION_MAX
            || group_count > 4096) {
        free(bytes);
        return 0;
    }

    store->groups = (RuneCUiInterfaceGroup *)calloc(group_count, sizeof(*store->groups));
    if (!store->groups) {
        free(bytes);
        return 0;
    }
    store->group_count = (int)group_count;

    for (uint32_t g = 0; g < group_count && !r.failed; g++) {
        RuneCUiInterfaceGroup *group = &store->groups[g];
        group->id = read_u32(&r);
        group->name = read_string(&r);
        uint32_t component_count = read_u32(&r);
        if (component_count > 65535) {
            r.failed = 1;
            break;
        }
        group->component_count = (int)component_count;
        group->components = (RuneCUiComponent *)calloc(component_count, sizeof(*group->components));
        if (!group->components) {
            r.failed = 1;
            break;
        }

        for (uint32_t c = 0; c < component_count && !r.failed; c++) {
            RuneCUiComponent *component = &group->components[c];
            component->id = read_u32(&r);
            component->parent_id = read_i32(&r);
            component->group_id = read_u32(&r);
            component->file_id = read_u32(&r);
            component->is_if3 = read_u8(&r);
            component->type = read_u8(&r);
            component->hidden = read_u8(&r);
            component->sprite_tiling = read_u8(&r);
            component->filled = read_u8(&r);
            component->line_direction = read_u8(&r);
            component->text_shadowed = read_u8(&r);
            component->flipped_vertically = read_u8(&r);
            component->flipped_horizontally = read_u8(&r);
            component->no_click_through = read_u8(&r);
            component->opacity = read_u8(&r);
            component->border_type = read_u8(&r);
            component->line_width = read_u8(&r);
            component->line_height = read_u8(&r);
            component->content_type = read_i32(&r);
            component->x = read_i32(&r);
            component->y = read_i32(&r);
            component->width = read_i32(&r);
            component->height = read_i32(&r);
            component->width_mode = read_i32(&r);
            component->height_mode = read_i32(&r);
            component->x_position_mode = read_i32(&r);
            component->y_position_mode = read_i32(&r);
            component->scroll_width = read_i32(&r);
            component->scroll_height = read_i32(&r);
            component->sprite_id = read_i32(&r);
            component->texture_id = read_i32(&r);
            component->shadow_color = read_i32(&r);
            component->model_id = read_i32(&r);
            component->model_type = read_i32(&r);
            component->font_id = read_i32(&r);
            component->text_color = read_i32(&r);
            component->click_mask = read_u32(&r);
            component->x_text_alignment = read_i32(&r);
            component->y_text_alignment = read_i32(&r);
            component->name = read_string(&r);
            component->text = read_string(&r);
            component->target_verb = read_string(&r);
            int encoded_action_count = (int)read_u8(&r);
            int action_count = encoded_action_count;
            if (action_count > RUNEC_UI_INTERFACE_MAX_ACTIONS)
                action_count = RUNEC_UI_INTERFACE_MAX_ACTIONS;
            component->action_count = action_count;
            for (int a = 0; a < encoded_action_count; a++) {
                char *action = read_string(&r);
                if (a < RUNEC_UI_INTERFACE_MAX_ACTIONS)
                    component->actions[a] = action;
                else
                    free(action);
            }
            if (version >= 2) {
                int encoded_listener_count = (int)read_u8(&r);
                int listener_count = encoded_listener_count;
                if (listener_count > RUNEC_UI_INTERFACE_MAX_LISTENERS)
                    listener_count = RUNEC_UI_INTERFACE_MAX_LISTENERS;
                component->listener_count = listener_count;
                for (int l = 0; l < encoded_listener_count; l++) {
                    int raw_kind = (int)read_u8(&r);
                    int encoded_value_count = (int)read_u8(&r);
                    RuneCUiComponentListener *listener =
                        l < RUNEC_UI_INTERFACE_MAX_LISTENERS
                            ? &component->listeners[l] : NULL;
                    if (listener) {
                        listener->kind = raw_kind >= 0
                            && raw_kind < RUNEC_UI_LISTENER_COUNT
                            ? (RuneCUiListenerKind)raw_kind
                            : RUNEC_UI_LISTENER_ON_LOAD;
                        if (raw_kind >= 0 && raw_kind < RUNEC_UI_LISTENER_COUNT)
                            component->listener_mask |= 1u << (unsigned)raw_kind;
                    }
                    int value_count = encoded_value_count;
                    if (value_count > RUNEC_UI_INTERFACE_MAX_LISTENER_VALUES)
                        value_count = RUNEC_UI_INTERFACE_MAX_LISTENER_VALUES;
                    if (listener)
                        listener->value_count = value_count;
                    for (int v = 0; v < encoded_value_count; v++) {
                        int value_type = (int)read_u8(&r);
                        if (value_type == RUNEC_UI_LISTENER_VALUE_STRING) {
                            char *value = read_string(&r);
                            if (listener && v < RUNEC_UI_INTERFACE_MAX_LISTENER_VALUES) {
                                listener->values[v].kind = RUNEC_UI_LISTENER_VALUE_STRING;
                                listener->values[v].string_value = value;
                            } else {
                                free(value);
                            }
                        } else {
                            int32_t value = read_i32(&r);
                            if (listener && v < RUNEC_UI_INTERFACE_MAX_LISTENER_VALUES) {
                                listener->values[v].kind = RUNEC_UI_LISTENER_VALUE_INT;
                                listener->values[v].int_value = value;
                            }
                        }
                    }
                }

                int encoded_trigger_count = (int)read_u8(&r);
                int trigger_count = encoded_trigger_count;
                if (trigger_count > RUNEC_UI_INTERFACE_MAX_TRIGGERS)
                    trigger_count = RUNEC_UI_INTERFACE_MAX_TRIGGERS;
                component->trigger_count = trigger_count;
                for (int t = 0; t < encoded_trigger_count; t++) {
                    int raw_kind = (int)read_u8(&r);
                    int encoded_value_count = (int)read_u8(&r);
                    RuneCUiComponentTrigger *trigger =
                        t < RUNEC_UI_INTERFACE_MAX_TRIGGERS
                            ? &component->triggers[t] : NULL;
                    if (trigger) {
                        trigger->kind = raw_kind >= 0
                            && raw_kind < RUNEC_UI_TRIGGER_COUNT
                            ? (RuneCUiTriggerKind)raw_kind
                            : RUNEC_UI_TRIGGER_VAR_TRANSMIT;
                        if (raw_kind >= 0 && raw_kind < RUNEC_UI_TRIGGER_COUNT)
                            component->trigger_mask |= 1u << (unsigned)raw_kind;
                    }
                    int value_count = encoded_value_count;
                    if (value_count > RUNEC_UI_INTERFACE_MAX_TRIGGER_VALUES)
                        value_count = RUNEC_UI_INTERFACE_MAX_TRIGGER_VALUES;
                    if (trigger)
                        trigger->value_count = value_count;
                    for (int v = 0; v < encoded_value_count; v++) {
                        int32_t value = read_i32(&r);
                        if (trigger && v < RUNEC_UI_INTERFACE_MAX_TRIGGER_VALUES)
                            trigger->values[v] = value;
                    }
                }
            }
        }
    }

    free(bytes);
    if (r.failed) {
        runec_ui_interfaces_unload(store);
        return 0;
    }
    store->loaded = 1;
    fprintf(stderr, "ui_interfaces: loaded %d interface groups from %s\n",
            store->group_count, path);
    return 1;
}

const RuneCUiInterfaceGroup *runec_ui_interface_group(
    const RuneCUiInterfaceStore *store, const char *name) {
    if (!store || !store->loaded || !name)
        return NULL;
    for (int i = 0; i < store->group_count; i++) {
        if (store->groups[i].name && strcmp(store->groups[i].name, name) == 0)
            return &store->groups[i];
    }
    return NULL;
}

static const RuneCUiComponent *find_component(
    const RuneCUiInterfaceGroup *group, uint32_t id) {
    for (int i = 0; i < group->component_count; i++) {
        if (group->components[i].id == id)
            return &group->components[i];
    }
    return NULL;
}

static Color color_from_rgb(int32_t rgb, unsigned char opacity) {
    Color c = {
        (unsigned char)((rgb >> 16) & 0xFF),
        (unsigned char)((rgb >> 8) & 0xFF),
        (unsigned char)(rgb & 0xFF),
        (unsigned char)(255 - opacity),
    };
    return c;
}

static void clean_interface_text(const char *src, char *dst, size_t cap) {
    if (!dst || cap == 0)
        return;
    size_t out = 0;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (size_t i = 0; src[i] && out + 1 < cap; i++) {
        if (src[i] == '<') {
            if (strncmp(&src[i], "<br>", 4) == 0) {
                dst[out++] = '\n';
                i += 3;
                continue;
            }
            const char *end = strchr(&src[i], '>');
            if (end) {
                i = (size_t)(end - src);
                continue;
            }
        }
        dst[out++] = src[i];
    }
    dst[out] = '\0';
}

static int rect_has_area(Rectangle r) {
    return r.width > 0.0f && r.height > 0.0f;
}

static Rectangle rect_intersect(Rectangle a, Rectangle b) {
    float x1 = a.x > b.x ? a.x : b.x;
    float y1 = a.y > b.y ? a.y : b.y;
    float x2 = a.x + a.width < b.x + b.width
        ? a.x + a.width : b.x + b.width;
    float y2 = a.y + a.height < b.y + b.height
        ? a.y + a.height : b.y + b.height;
    if (x2 <= x1 || y2 <= y1)
        return (Rectangle){0, 0, 0, 0};
    return (Rectangle){x1, y1, x2 - x1, y2 - y1};
}

static Rectangle rect_expand_to_scroll(Rectangle rect,
                                       const RuneCUiComponent *component) {
    Rectangle out = rect;
    if (component->scroll_width > 0 && (float)component->scroll_width > out.width)
        out.width = (float)component->scroll_width;
    if (component->scroll_height > 0 && (float)component->scroll_height > out.height)
        out.height = (float)component->scroll_height;
    return out;
}

static const RuneCUiComponentOverride *find_override(
    const RuneCUiComponentOverrides *overrides,
    uint32_t component_id) {
    if (!overrides || !overrides->items || overrides->count <= 0)
        return NULL;
    for (int i = 0; i < overrides->count; i++) {
        const RuneCUiComponentOverride *override = &overrides->items[i];
        if (override->component_id == component_id)
            return override;
    }
    return NULL;
}

static const RuneCUiItemContainerOverride *find_item_container_override(
    const RuneCUiComponentOverrides *overrides,
    uint32_t component_id) {
    if (!overrides || !overrides->containers || overrides->container_count <= 0)
        return NULL;
    for (int i = 0; i < overrides->container_count; i++) {
        const RuneCUiItemContainerOverride *override = &overrides->containers[i];
        if (override->component_id == component_id)
            return override;
    }
    return NULL;
}

static RuneCUiComponent component_with_override(
    const RuneCUiComponent *component,
    const RuneCUiComponentOverride *override) {
    RuneCUiComponent out = *component;
    if (!override)
        return out;
    if (override->flags & RUNEC_UI_COMPONENT_OVERRIDE_HIDDEN)
        out.hidden = override->hidden ? 1 : 0;
    if (override->flags & RUNEC_UI_COMPONENT_OVERRIDE_TEXT)
        out.text = (char *)override->text;
    if (override->flags & RUNEC_UI_COMPONENT_OVERRIDE_MODEL) {
        out.model_id = override->model_id;
        out.model_type = override->model_type;
    }
    if (override->flags & RUNEC_UI_COMPONENT_OVERRIDE_COLOR)
        out.text_color = override->text_color;
    return out;
}

static Rectangle component_child_parent_rect(
    Rectangle rect,
    const RuneCUiComponent *component,
    const RuneCUiComponentOverride *override) {
    Rectangle out = component->type == 0
        ? rect_expand_to_scroll(rect, component)
        : rect;
    if (override && (override->flags & RUNEC_UI_COMPONENT_OVERRIDE_SCROLL)) {
        out.x -= (float)override->scroll_x;
        out.y -= (float)override->scroll_y;
    }
    return out;
}

static void apply_scissor(Rectangle rect) {
    int x = (int)(rect.x + 0.5f);
    int y = (int)(rect.y + 0.5f);
    int w = (int)(rect.width + 0.5f);
    int h = (int)(rect.height + 0.5f);
    if (w < 0)
        w = 0;
    if (h < 0)
        h = 0;
    BeginScissorMode(x, y, w, h);
}

static void push_clip(RuneCUiClipState *clip, Rectangle next,
                      Rectangle *prev, int *prev_active) {
    *prev = clip->current;
    *prev_active = clip->active;
    if (clip->active)
        next = rect_intersect(next, clip->current);
    if (!rect_has_area(next)) {
        next = (Rectangle){0, 0, 0, 0};
    }
    if (clip->active)
        EndScissorMode();
    apply_scissor(next);
    clip->current = next;
    clip->active = 1;
}

static void pop_clip(RuneCUiClipState *clip, Rectangle prev, int prev_active) {
    if (clip->active)
        EndScissorMode();
    clip->current = prev;
    clip->active = prev_active;
    if (clip->active)
        apply_scissor(clip->current);
}

static Rectangle layout_component(const RuneCUiComponent *component,
                                  Rectangle parent,
                                  int root) {
    if (root)
        return parent;

    float w = (float)component->width;
    float h = (float)component->height;
    if (component->width_mode == 1)
        w = parent.width - (float)component->width;
    else if (component->width_mode == 2)
        w = parent.width * (float)component->width / 16384.0f;
    if (component->height_mode == 1)
        h = parent.height - (float)component->height;
    else if (component->height_mode == 2)
        h = parent.height * (float)component->height / 16384.0f;
    if (w < 0)
        w = 0;
    if (h < 0)
        h = 0;

    float x = parent.x + (float)component->x;
    float y = parent.y + (float)component->y;
    if (component->x_position_mode == 1)
        x = parent.x + (parent.width - w) * 0.5f + (float)component->x;
    else if (component->x_position_mode == 2)
        x = parent.x + parent.width - w - (float)component->x;
    else if (component->x_position_mode == 3)
        x = parent.x + parent.width * (float)component->x / 16384.0f;
    else if (component->x_position_mode == 4)
        x = parent.x + (parent.width - w) * 0.5f
            + parent.width * (float)component->x / 16384.0f;
    else if (component->x_position_mode == 5)
        x = parent.x + parent.width - w
            - parent.width * (float)component->x / 16384.0f;

    if (component->y_position_mode == 1)
        y = parent.y + (parent.height - h) * 0.5f + (float)component->y;
    else if (component->y_position_mode == 2)
        y = parent.y + parent.height - h - (float)component->y;
    else if (component->y_position_mode == 3)
        y = parent.y + parent.height * (float)component->y / 16384.0f;
    else if (component->y_position_mode == 4)
        y = parent.y + (parent.height - h) * 0.5f
            + parent.height * (float)component->y / 16384.0f;
    else if (component->y_position_mode == 5)
        y = parent.y + parent.height - h
            - parent.height * (float)component->y / 16384.0f;

    return (Rectangle){x, y, w, h};
}

static int component_uses_mount_rect(const RuneCUiComponent *component) {
    return component->file_id == 0 && component->parent_id == -1;
}

static Texture2D *load_cached_texture(RuneCUiInterfaceStore *store, int key,
                                      const char *path) {
    for (int i = 0; i < RUNEC_UI_INTERFACE_SPRITE_CACHE; i++) {
        RuneCUiSpriteCacheEntry *entry = &store->sprites[i];
        if (entry->texture.id != 0 && entry->sprite_id == key)
            return &entry->texture;
        if (entry->missing && entry->sprite_id == key)
            return NULL;
    }

    RuneCUiSpriteCacheEntry *slot = NULL;
    for (int i = 0; i < RUNEC_UI_INTERFACE_SPRITE_CACHE; i++) {
        if (store->sprites[i].texture.id == 0 && !store->sprites[i].missing) {
            slot = &store->sprites[i];
            break;
        }
    }
    if (!slot)
        return NULL;

    slot->sprite_id = key;
    if (!rc_asset_exists(path)) {
        slot->missing = 1;
        return NULL;
    }
    slot->texture = runec_load_texture_asset(path);
    if (slot->texture.id == 0) {
        slot->missing = 1;
        return NULL;
    }
    SetTextureFilter(slot->texture, TEXTURE_FILTER_POINT);
    return &slot->texture;
}

static Texture2D *load_sprite_texture(RuneCUiInterfaceStore *store, int sprite_id) {
    if (sprite_id < 0)
        return NULL;
    char path[128];
    snprintf(path, sizeof(path), "data/sprites/ui/%d.png", sprite_id);
    if (!rc_asset_exists(path))
        snprintf(path, sizeof(path), "data/sprites/ui/%d_0.png", sprite_id);
    return load_cached_texture(store, sprite_id, path);
}

static Texture2D *load_item_icon_texture(RuneCUiInterfaceStore *store, int item_id) {
    if (item_id < 0)
        return NULL;
    char path[128];
    snprintf(path, sizeof(path), "data/sprites/items/item_%d.png", item_id);
    return load_cached_texture(store, -1000000 - item_id, path);
}

static Color stack_text_color(int quantity) {
    if (quantity >= 10000000)
        return (Color){0, 255, 0, 255};
    if (quantity >= 100000)
        return WHITE;
    return (Color){255, 255, 0, 255};
}

static void format_stack_quantity(int quantity, char *dst, size_t cap) {
    if (quantity >= 10000000)
        snprintf(dst, cap, "%dM", quantity / 1000000);
    else if (quantity >= 100000)
        snprintf(dst, cap, "%dK", quantity / 1000);
    else
        snprintf(dst, cap, "%d", quantity);
}

static void draw_small_text(const RuneCUiAssets *assets, const char *text,
                            Rectangle rect, float size, Color color) {
    runec_ui_draw_text_shadow(assets, text, rect.x, rect.y, size, color);
}

static int draw_item_icon(RuneCUiInterfaceStore *store,
                          const RuneCUiAssets *assets,
                          uint32_t icon_item_id,
                          int quantity,
                          Rectangle rect) {
    if (icon_item_id == 0)
        return 0;
    Texture2D *tex = load_item_icon_texture(store, (int)icon_item_id);
    if (tex && tex->id != 0) {
        float sx = rect.width / (float)tex->width;
        float sy = rect.height / (float)tex->height;
        float scale = sx < sy ? sx : sy;
        if (scale > 1.0f)
            scale = 1.0f;
        Rectangle dst = {
            rect.x + (rect.width - (float)tex->width * scale) * 0.5f,
            rect.y + (rect.height - (float)tex->height * scale) * 0.5f,
            (float)tex->width * scale,
            (float)tex->height * scale,
        };
        DrawTexturePro(*tex,
                       (Rectangle){0, 0, (float)tex->width, (float)tex->height},
                       dst, (Vector2){0, 0}, 0.0f, WHITE);
    } else {
        DrawRectangleRec((Rectangle){rect.x + 6, rect.y + 6,
                                     rect.width - 12, rect.height - 12},
                         (Color){255, 0, 255, 210});
    }
    if (quantity > 1) {
        char q[16];
        format_stack_quantity(quantity, q, sizeof(q));
        draw_small_text(assets, q, (Rectangle){rect.x + 1, rect.y - 1,
                                               rect.width, 12},
                        10.0f, stack_text_color(quantity));
    }
    return 1;
}

static void draw_item_container_override(
    RuneCUiInterfaceStore *store,
    const RuneCUiAssets *assets,
    const RuneCUiItemContainerOverride *override,
    Rectangle rect) {
    if (!override || override->slot_count <= 0 || override->columns <= 0)
        return;
    int count = override->slot_count;
    if (count > RUNEC_UI_ITEM_CONTAINER_MAX_SLOTS)
        count = RUNEC_UI_ITEM_CONTAINER_MAX_SLOTS;
    for (int i = 0; i < count; i++) {
        int col = i % override->columns;
        int row = i / override->columns;
        Rectangle slot = {
            rect.x + override->x0 + (float)col * override->step_x,
            rect.y + override->y0 + (float)row * override->step_y,
            override->slot_w,
            override->slot_h,
        };
        if (override->selected_slot == i) {
            DrawRectangleLinesEx((Rectangle){slot.x - 1, slot.y - 1,
                                             slot.width + 2, slot.height + 2},
                                 2.0f, (Color){255, 255, 0, 255});
        }
        const RuneCUiItemContainerSlot *item = &override->slots[i];
        if (!item->enabled)
            continue;
        uint32_t icon = item->icon_item_id ? item->icon_item_id : item->item_id;
        draw_item_icon(store, assets, icon, item->quantity, slot);
    }
}

static void draw_text_component(const RuneCUiAssets *assets,
                                const RuneCUiComponent *component,
                                Rectangle rect) {
    if (!component->text || !component->text[0])
        return;
    char text[1024];
    clean_interface_text(component->text, text, sizeof(text));
    if (!text[0])
        return;
    float size = component->font_id == 0 ? 11.0f : 12.0f;
    if (component->line_height > 0)
        size = (float)(component->line_height + 9);
    if (size < 12.0f)
        size = 12.0f;
    Font font = runec_ui_font_for_size(assets, size);
    Color color = color_from_rgb(component->text_color, component->opacity);
    Vector2 measure = MeasureTextEx(font, text, size, 0);
    float x = rect.x;
    float y = rect.y;
    if (component->x_text_alignment == 1)
        x = rect.x + (rect.width - measure.x) * 0.5f;
    else if (component->x_text_alignment == 2)
        x = rect.x + rect.width - measure.x;
    if (component->y_text_alignment == 1)
        y = rect.y + (rect.height - measure.y) * 0.5f;
    else if (component->y_text_alignment == 2)
        y = rect.y + rect.height - measure.y;
    x = (float)((int)(x + 0.5f));
    y = (float)((int)(y + 0.5f));
    if (component->text_shadowed)
        DrawTextEx(font, text, (Vector2){x + 1, y + 1}, size, 0, BLACK);
    DrawTextEx(font, text, (Vector2){x, y}, size, 0, color);
}

static void draw_sprite_component(RuneCUiInterfaceStore *store,
                                  const RuneCUiComponent *component,
                                  Rectangle rect) {
    Texture2D *tex = load_sprite_texture(store, component->sprite_id);
    if (!tex)
        return;
    Color tint = WHITE;
    tint.a = (unsigned char)(255 - component->opacity);
    if (rect.width <= 0)
        rect.width = (float)tex->width;
    if (rect.height <= 0)
        rect.height = (float)tex->height;
    Rectangle full_src = {
        component->flipped_horizontally ? (float)tex->width : 0.0f,
        component->flipped_vertically ? (float)tex->height : 0.0f,
        component->flipped_horizontally ? -(float)tex->width : (float)tex->width,
        component->flipped_vertically ? -(float)tex->height : (float)tex->height,
    };
    if (component->shadow_color != 0) {
        Color shadow = color_from_rgb(component->shadow_color, component->opacity);
        DrawTexturePro(*tex, full_src,
                       (Rectangle){rect.x + 1, rect.y + 1, rect.width, rect.height},
                       (Vector2){0, 0}, 0, shadow);
    }
    if (component->border_type > 0) {
        Color border = BLACK;
        border.a = tint.a;
        for (int layer = 0; layer < component->border_type && layer < 3; layer++) {
            float offset = (float)(layer + 1);
            DrawTexturePro(*tex, full_src,
                           (Rectangle){rect.x - offset, rect.y, rect.width, rect.height},
                           (Vector2){0, 0}, 0, border);
            DrawTexturePro(*tex, full_src,
                           (Rectangle){rect.x + offset, rect.y, rect.width, rect.height},
                           (Vector2){0, 0}, 0, border);
            DrawTexturePro(*tex, full_src,
                           (Rectangle){rect.x, rect.y - offset, rect.width, rect.height},
                           (Vector2){0, 0}, 0, border);
            DrawTexturePro(*tex, full_src,
                           (Rectangle){rect.x, rect.y + offset, rect.width, rect.height},
                           (Vector2){0, 0}, 0, border);
        }
    }
    if (component->sprite_tiling) {
        for (float y = rect.y; y < rect.y + rect.height; y += (float)tex->height) {
            for (float x = rect.x; x < rect.x + rect.width; x += (float)tex->width) {
                float w = tex->width;
                float h = tex->height;
                if (x + w > rect.x + rect.width)
                    w = rect.x + rect.width - x;
                if (y + h > rect.y + rect.height)
                    h = rect.y + rect.height - y;
                Rectangle src = {
                    component->flipped_horizontally ? (float)tex->width : 0.0f,
                    component->flipped_vertically ? (float)tex->height : 0.0f,
                    component->flipped_horizontally ? -w : w,
                    component->flipped_vertically ? -h : h,
                };
                DrawTexturePro(*tex, src,
                               (Rectangle){x, y, w, h}, (Vector2){0, 0}, 0, tint);
            }
        }
    } else {
        DrawTexturePro(*tex, full_src, rect, (Vector2){0, 0}, 0, tint);
    }
}

static int draw_model_component(RuneCUiInterfaceStore *store,
                                const RuneCUiAssets *assets,
                                const RuneCUiComponent *component,
                                Rectangle rect) {
    if (component->model_type == 4) {
        if (draw_item_icon(store, assets, (uint32_t)component->model_id, 1, rect))
            return 1;
    }

    DrawRectangleRec(rect, (Color){18, 16, 12, 115});
    DrawRectangleLinesEx(rect, 1, (Color){112, 92, 58, 175});
    char label[32];
    snprintf(label, sizeof(label), "M%d", component->model_id);
    Font font = runec_ui_font_for_size(assets, 10);
    DrawTextEx(font, label, (Vector2){rect.x + 3, rect.y + 3}, 10, 1,
               (Color){220, 190, 110, 190});
    return 0;
}

static void draw_component(RuneCUiInterfaceStore *store,
                           const RuneCUiAssets *assets,
                           const RuneCUiInterfaceGroup *group,
                           const RuneCUiComponent *component,
                           Rectangle rect,
                           RuneCUiClipState *clip,
                           const RuneCUiComponentOverrides *overrides) {
    const RuneCUiComponentOverride *override =
        find_override(overrides, component->id);
    RuneCUiComponent view = component_with_override(component, override);
    component = &view;
    if (component->hidden)
        return;
    if (clip && clip->active && !rect_has_area(rect_intersect(rect, clip->current)))
        return;

    if (component->type == 3) {
        Color color = color_from_rgb(component->text_color, component->opacity);
        if (component->filled)
            DrawRectangleRec(rect, color);
        else
            DrawRectangleLinesEx(rect, 1, color);
    } else if (component->type == 4) {
        draw_text_component(assets, component, rect);
    } else if (component->type == 5) {
        draw_sprite_component(store, component, rect);
    } else if (component->type == 6 && component->model_id >= 0) {
        draw_model_component(store, assets, component, rect);
    } else if (component->type == 9) {
        Color color = color_from_rgb(component->text_color, component->opacity);
        Vector2 a = {rect.x, component->line_direction ? rect.y + rect.height : rect.y};
        Vector2 b = {rect.x + rect.width,
                     component->line_direction ? rect.y : rect.y + rect.height};
        float line_width = component->line_width > 0 ? (float)component->line_width : 1.0f;
        DrawLineEx(a, b, line_width, color);
    }

    for (int i = 0; i < group->component_count; i++) {
        const RuneCUiComponent *child = &group->components[i];
        if (child->parent_id != (int32_t)component->id)
            continue;
        Rectangle child_parent =
            component_child_parent_rect(rect, component, override);
        Rectangle child_rect = layout_component(child, child_parent, 0);
        if (component->type == 0 && rect_has_area(rect)) {
            Rectangle prev = {0};
            int prev_active = 0;
            push_clip(clip, rect, &prev, &prev_active);
            draw_component(store, assets, group, child, child_rect, clip,
                           overrides);
            pop_clip(clip, prev, prev_active);
        } else {
            draw_component(store, assets, group, child, child_rect, clip,
                           overrides);
        }
    }

    const RuneCUiItemContainerOverride *item_container =
        find_item_container_override(overrides, component->id);
    if (item_container)
        draw_item_container_override(store, assets, item_container, rect);

    if (override && (override->flags & RUNEC_UI_COMPONENT_OVERRIDE_ITEM)) {
        if (override->selected) {
            DrawRectangleLinesEx((Rectangle){rect.x - 1, rect.y - 1,
                                             rect.width + 2, rect.height + 2},
                                 2.0f, (Color){255, 255, 0, 255});
        }
        if (override->item_id > 0) {
            uint32_t icon = override->icon_item_id > 0
                ? (uint32_t)override->icon_item_id
                : (uint32_t)override->item_id;
            draw_item_icon(store, assets, icon, override->item_quantity, rect);
        }
    }
}

int runec_ui_interfaces_draw_group_ex(RuneCUiInterfaceStore *store,
                                      const RuneCUiAssets *assets,
                                      const char *group_name,
                                      Rectangle mount,
                                      const RuneCUiComponentOverrides *overrides) {
    const RuneCUiInterfaceGroup *group = runec_ui_interface_group(store, group_name);
    if (!group)
        return 0;

    RuneCUiClipState clip = {0};
    Rectangle prev = {0};
    int prev_active = 0;
    push_clip(&clip, mount, &prev, &prev_active);

    int drew = 0;
    for (int i = 0; i < group->component_count; i++) {
        const RuneCUiComponent *component = &group->components[i];
        if (component->parent_id != -1)
            continue;
        const RuneCUiComponent *root = find_component(group, component->id);
        if (!root)
            continue;
        Rectangle rect = layout_component(root, mount, component_uses_mount_rect(root));
        draw_component(store, assets, group, root, rect, &clip, overrides);
        drew = 1;
    }
    pop_clip(&clip, prev, prev_active);
    return drew;
}

int runec_ui_interfaces_draw_group(RuneCUiInterfaceStore *store,
                                   const RuneCUiAssets *assets,
                                   const char *group_name,
                                   Rectangle mount) {
    return runec_ui_interfaces_draw_group_ex(store, assets, group_name,
                                            mount, NULL);
}

static int find_component_rect_recursive(const RuneCUiInterfaceGroup *group,
                                         const RuneCUiComponent *component,
                                         const char *component_name,
                                         Rectangle rect,
                                         Rectangle *out_rect) {
    if (component->name && strcmp(component->name, component_name) == 0) {
        *out_rect = rect;
        return 1;
    }

    Rectangle child_parent = component->type == 0
        ? rect_expand_to_scroll(rect, component)
        : rect;
    for (int i = 0; i < group->component_count; i++) {
        const RuneCUiComponent *child = &group->components[i];
        if (child->parent_id != (int32_t)component->id)
            continue;
        Rectangle child_rect = layout_component(child, child_parent, 0);
        if (find_component_rect_recursive(group, child, component_name,
                                          child_rect, out_rect))
            return 1;
    }
    return 0;
}

static int find_component_rect_by_id_recursive(const RuneCUiInterfaceGroup *group,
                                               const RuneCUiComponent *component,
                                               uint32_t component_id,
                                               Rectangle rect,
                                               Rectangle *out_rect) {
    if (component->id == component_id) {
        *out_rect = rect;
        return 1;
    }

    Rectangle child_parent = component->type == 0
        ? rect_expand_to_scroll(rect, component)
        : rect;
    for (int i = 0; i < group->component_count; i++) {
        const RuneCUiComponent *child = &group->components[i];
        if (child->parent_id != (int32_t)component->id)
            continue;
        Rectangle child_rect = layout_component(child, child_parent, 0);
        if (find_component_rect_by_id_recursive(group, child, component_id,
                                                child_rect, out_rect))
            return 1;
    }
    return 0;
}

static void copy_hit_text(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0)
        return;
    if (!src)
        src = "";
    snprintf(dst, cap, "%s", src);
}

static int component_hit_interactive(const RuneCUiComponent *component) {
    return component->click_mask != 0
        || component->action_count > 0
        || (component->listener_mask & RUNEC_UI_COMPONENT_HIT_LISTENERS) != 0
        || (component->target_verb && component->target_verb[0] != '\0')
        || component->no_click_through;
}

static void fill_hit_result(RuneCUiHitResult *out,
                            const RuneCUiComponent *component,
                            Rectangle rect) {
    memset(out, 0, sizeof(*out));
    out->component_id = component->id;
    out->group_id = component->group_id;
    out->file_id = component->file_id;
    out->rect = rect;
    out->click_mask = component->click_mask;
    out->listener_mask = component->listener_mask;
    out->trigger_mask = component->trigger_mask;
    out->action_count = component->action_count;
    copy_hit_text(out->name, sizeof(out->name), component->name);
    copy_hit_text(out->text, sizeof(out->text), component->text);
    copy_hit_text(out->target_verb, sizeof(out->target_verb),
                  component->target_verb);
    for (int i = 0; i < component->action_count
            && i < RUNEC_UI_INTERFACE_MAX_ACTIONS; i++) {
        copy_hit_text(out->actions[i], sizeof(out->actions[i]),
                      component->actions[i]);
    }
}

static int hit_test_component_recursive(const RuneCUiInterfaceGroup *group,
                                        const RuneCUiComponent *component,
                                        Rectangle rect,
                                        Vector2 point,
                                        RuneCUiHitResult *out_hit,
                                        const RuneCUiComponentOverrides *overrides) {
    const RuneCUiComponentOverride *override =
        find_override(overrides, component->id);
    RuneCUiComponent view = component_with_override(component, override);
    component = &view;
    if (component->hidden || !rect_has_area(rect)
            || !CheckCollisionPointRec(point, rect))
        return 0;

    Rectangle child_parent =
        component_child_parent_rect(rect, component, override);
    for (int i = group->component_count - 1; i >= 0; i--) {
        const RuneCUiComponent *child = &group->components[i];
        if (child->parent_id != (int32_t)component->id)
            continue;
        Rectangle child_rect = layout_component(child, child_parent, 0);
        if (hit_test_component_recursive(group, child, child_rect, point,
                                         out_hit, overrides))
            return 1;
    }

    if (!component_hit_interactive(component))
        return 0;
    fill_hit_result(out_hit, component, rect);
    return 1;
}

int runec_ui_interfaces_hit_test_ex(const RuneCUiInterfaceStore *store,
                                    const char *group_name,
                                    Rectangle mount,
                                    Vector2 point,
                                    RuneCUiHitResult *out_hit,
                                    const RuneCUiComponentOverrides *overrides) {
    if (!out_hit)
        return 0;
    const RuneCUiInterfaceGroup *group = runec_ui_interface_group(store, group_name);
    if (!group)
        return 0;
    for (int i = group->component_count - 1; i >= 0; i--) {
        const RuneCUiComponent *component = &group->components[i];
        if (component->parent_id != -1)
            continue;
        Rectangle rect = layout_component(component, mount,
                                          component_uses_mount_rect(component));
        if (hit_test_component_recursive(group, component, rect, point, out_hit,
                                         overrides))
            return 1;
    }
    return 0;
}

int runec_ui_interfaces_hit_test(const RuneCUiInterfaceStore *store,
                                 const char *group_name,
                                 Rectangle mount,
                                 Vector2 point,
                                 RuneCUiHitResult *out_hit) {
    return runec_ui_interfaces_hit_test_ex(store, group_name, mount, point,
                                          out_hit, NULL);
}

int runec_ui_interfaces_component_rect(const RuneCUiInterfaceStore *store,
                                       const char *group_name,
                                       const char *component_name,
                                       Rectangle mount,
                                       Rectangle *out_rect) {
    if (!out_rect)
        return 0;
    const RuneCUiInterfaceGroup *group = runec_ui_interface_group(store, group_name);
    if (!group || !component_name)
        return 0;

    for (int i = 0; i < group->component_count; i++) {
        const RuneCUiComponent *component = &group->components[i];
        if (component->parent_id != -1)
            continue;
        Rectangle rect = layout_component(component, mount,
                                          component_uses_mount_rect(component));
        if (find_component_rect_recursive(group, component, component_name,
                                          rect, out_rect))
            return 1;
    }
    return 0;
}

int runec_ui_interfaces_component_rect_by_id(const RuneCUiInterfaceStore *store,
                                             const char *group_name,
                                             uint32_t component_id,
                                             Rectangle mount,
                                             Rectangle *out_rect) {
    if (!out_rect || component_id == 0)
        return 0;
    const RuneCUiInterfaceGroup *group = runec_ui_interface_group(store, group_name);
    if (!group)
        return 0;

    for (int i = 0; i < group->component_count; i++) {
        const RuneCUiComponent *component = &group->components[i];
        if (component->parent_id != -1)
            continue;
        Rectangle rect = layout_component(component, mount,
                                          component_uses_mount_rect(component));
        if (find_component_rect_by_id_recursive(group, component, component_id,
                                                rect, out_rect))
            return 1;
    }
    return 0;
}
