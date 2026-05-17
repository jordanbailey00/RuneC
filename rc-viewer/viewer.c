// RuneC Raylib viewer.

#include "../rc-core/api.h"
#include "../rc-core/assets.h"
#include "../rc-core/combat.h"
#include "../rc-core/config.h"
#include "../rc-core/items.h"
#include "../rc-core/objects.h"
#include "../rc-core/pathfinding.h"
#include "../rc-core/npc.h"
#include "../rc-core/spells.h"
#include "../rc-content/content.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "asset_raylib.h"
#include "terrain.h"
#include "objects.h"
#include "models.h"
#include "anims.h"
#include "collision.h"
#include "ui.h"
#include "equipment_render.h"
#include "spotanims.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define DEFAULT_WORLD_ORIGIN_X 3072
#define DEFAULT_WORLD_ORIGIN_Y 3264
#define DEFAULT_PLAYER_START_X 3213
#define DEFAULT_PLAYER_START_Y 3428
#define DEFAULT_WORLD_W 320
#define DEFAULT_WORLD_H 320
#define WINDOW_W 1280
#define WINDOW_H 720
#define TPS 1.667f
#define NPC_RENDER_QUEUE_MAX 8
#define NPC_RENDER_MAX_DT 0.05f
#define NPC_MODEL_SCALE 1.0f
#define VIEWER_CONTEXT_NONE 0
#define VIEWER_CONTEXT_NPC 1
#define VIEWER_CONTEXT_OBJECT 2
#define VIEWER_CONTEXT_WALK_HERE -10
#define VIEWER_CONTEXT_EXAMINE -11
#define VIEWER_CONTEXT_CANCEL -12
#define RUNEC_ITEM_STACK_VARIANT_MAX 4096
#define MODEL_ID_SPOTANIM_BASE 0xA2000000u
#define OBJECT_PICK_TILE_RADIUS 1

typedef struct {
    int obj_id;
    int x, y, plane;
    int width, length;
} ViewerPickedObject;

typedef struct {
    const char *key;
    const char *label;
    int target_x, target_y, plane;
    int npc_id;
    int npc_size;
} ViewerDevTransport;

// Player animation sequence IDs (from FC — xbows_human variants)
#define ANIM_IDLE 808
#define ANIM_WALK 819
#define ANIM_RUN  824
#define ANIM_ATTACK_UNARMED 422
#define ANIM_ATTACK_BOW 426
#define ANIM_ATTACK_STAFF 393
#define ANIM_ATTACK_CAST 1162
#define ANIM_ATTACK_WHIP 1658
#define ANIM_ATTACK_GODSWORD 7045

typedef struct {
    int base_id;
    int threshold;
    int variant_id;
} ItemStackVariant;

typedef struct {
    RcWorld *world;
    TerrainMesh *terrain;
    TerrainMesh *terrain_planes[RC_MAX_PLANES];
    ObjectMesh *objects;
    ObjectMesh *object_planes[RC_MAX_PLANES];
    ModelSet *object_anim_model_planes[RC_MAX_PLANES];
    ModelSet *player_model;
    ModelSet *npc_models;
    ModelSet *item_models;
    ModelSet *projectile_models;
    SpotAnimSet *spotanims;
    RuneCItemRenderMap item_render_map;
    ItemStackVariant item_stack_variants[RUNEC_ITEM_STACK_VARIANT_MAX];
    int item_stack_variant_count;
    AnimCache *anims;           // player animations
    AnimCache *npc_anims;       // NPC animations (separate cache; IDs don't overlap)
    AnimCache *npc_fallback_anims;
    AnimModelState *anim_state; // player
    AnimModelState **item_anim_states;
    int item_anim_state_count;
    AnimModelState **projectile_anim_states;
    int projectile_anim_state_count;
    AnimModelState **object_anim_states[RC_MAX_PLANES];
    int object_anim_state_count[RC_MAX_PLANES];
    ModelEntry composed_player_model;
    AnimModelState *composed_anim_state;
    uint64_t composed_player_signature;
    int composed_player_loaded;
    // Per-NPC-def animation scratch state (one per def index into g_npc_defs).
    // We share across all instances of the same NPC type — each instance
    // re-applies its own frame from base_verts before its draw call, so
    // cross-instance clobber is fine.
    AnimModelState *npc_anim_state[RC_MAX_NPC_DEFS];

    // Per-NPC-instance animation progress (parallel to world->npcs[]).
    struct {
        int cur_anim_id;       // 0 = none, otherwise the seq id currently playing
        int frame_idx;
        float frame_timer;
        int initialized;
        int server_x, server_y, server_plane;
        int last_seen_tick;
        float render_x, render_y;
        float waypoint_x[NPC_RENDER_QUEUE_MAX];
        float waypoint_y[NPC_RENDER_QUEUE_MAX];
        int waypoint_count;
        int moving;
        float move_anim_timer;
        int last_dx, last_dy;
    } npc_render[RC_MAX_NPCS];

    // Camera
    Camera3D camera;
    float cam_yaw, cam_pitch, cam_dist;
    int camera_locked;

    // Tick
    float tick_acc, tick_frac;
    int paused;
    float prev_player_x, prev_player_y;

    // Animation
    int cur_anim_id;
    int anim_frame_idx;
    float anim_frame_timer;
    int player_moving;
    float player_facing_angle;   // viewer-side; rc-core doesn't store

    int show_grid;
    int show_collision;
    RuneCUiState ui;
    RcCombatViewState combat_view;
    Shader alpha_cutout_shader_static;
    Shader alpha_cutout_shader_dynamic;
    int alpha_cutout_shader_static_loaded;
    int alpha_cutout_shader_dynamic_loaded;
    int context_kind;
    int context_npc_uid;
    ViewerPickedObject context_object;
    int context_action_option[RUNEC_UI_CONTEXT_ACTIONS];
    Color *minimap_tiles;
    int minimap_tiles_w;
    int minimap_tiles_h;
    int minimap_tiles_plane;
    Color *world_map_pixels;
    int world_map_w;
    int world_map_h;
    int world_map_min_x;
    int world_map_max_y;
    int use_world_map_minimap;
    int scene_plane_override;
    int scene_auto_export;
    int scene_radius_regions;
    int initial_scene_origin_x;
    int initial_scene_origin_y;
    int initial_scene_w;
    int initial_scene_h;
    int initial_scene_ready;
    char initial_terrain_path[1024];
    char initial_objects_path[1024];
    char active_scene_prefix[1024];
} ViewerState;

static void create_object_anim_plane_states(ViewerState *v, int plane);
static void free_object_anim_plane(ViewerState *v, int plane);
static void reset_model_entry_to_base_pose(ModelEntry *entry);
static void ensure_active_scene_plane(ViewerState *v, int plane);
static void reset_viewer_context(ViewerState *v);
static void handle_player_scene_transition(ViewerState *v, int old_x,
                                           int old_y, int old_plane);

static int g_world_origin_x = DEFAULT_WORLD_ORIGIN_X;
static int g_world_origin_y = DEFAULT_WORLD_ORIGIN_Y;
static int g_player_start_x = DEFAULT_PLAYER_START_X;
static int g_player_start_y = DEFAULT_PLAYER_START_Y;
static int g_world_w = DEFAULT_WORLD_W;
static int g_world_h = DEFAULT_WORLD_H;

// Convert world tile to local rendering coordinate
#define LOCAL_X(wx) ((wx) - g_world_origin_x)
#define LOCAL_Y(wy) ((wy) - g_world_origin_y)

static const char *env_path(const char *key, const char *fallback) {
    const char *value = getenv(key);
    return value && value[0] ? value : fallback;
}

static int runtime_data_available(void) {
    const char *required[] = {
        "defs/items.bin",
        "defs/npc_defs.bin",
        "defs/object_defs.bin",
        "regions/varrock.terrain",
        "regions/varrock.objects",
        "models/player.models",
        "sprites/ui/side_icon_inventory.png",
        "fonts/runescape.ttf",
    };
    int missing = 0;
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
        if (!rc_asset_exists(required[i])) {
            fprintf(stderr, "missing runtime data asset: %s\n", required[i]);
            missing = 1;
        }
    }
    if (missing) {
        fprintf(stderr,
                "RuneC runtime data is missing. Run ./scripts/setup-data.sh "
                "from the repository root, or set RUNEC_DATA_ROOT/RUNEC_PACK_DIR.\n");
    }
    return !missing;
}

static int env_int(const char *key, int fallback) {
    const char *value = getenv(key);
    return value && value[0] ? atoi(value) : fallback;
}

static int env_bool(const char *key, int fallback) {
    const char *value = getenv(key);
    if (!value || !value[0])
        return fallback;
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0
            || strcmp(value, "FALSE") == 0 || strcmp(value, "off") == 0
            || strcmp(value, "OFF") == 0 || strcmp(value, "no") == 0
            || strcmp(value, "NO") == 0) {
        return 0;
    }
    return 1;
}

static int clamp_plane(int plane) {
    if (plane < 0) return 0;
    if (plane >= RC_MAX_PLANES) return RC_MAX_PLANES - 1;
    return plane;
}

static int viewer_scene_plane(const ViewerState *v) {
    if (!v || !v->world) return 0;
    if (v->scene_plane_override >= 0)
        return clamp_plane(v->scene_plane_override);
    return clamp_plane(v->world->player.plane);
}

static void viewer_set_scene_plane_delta(ViewerState *v, int delta) {
    if (!v || !v->world) return;
    int plane = v->scene_plane_override >= 0
              ? v->scene_plane_override : v->world->player.plane;
    v->scene_plane_override = clamp_plane(plane + delta);
    ensure_active_scene_plane(v, v->scene_plane_override);
}

static TerrainMesh *viewer_terrain_for_plane(ViewerState *v, int plane) {
    if (!v) return NULL;
    plane = clamp_plane(plane);
    return v->terrain_planes[plane];
}

static ObjectMesh *viewer_objects_for_plane(ViewerState *v, int plane) {
    if (!v) return NULL;
    plane = clamp_plane(plane);
    return v->object_planes[plane];
}

static int plane_path_variant(char *out, size_t cap, const char *base,
                              int plane, const char *ext) {
    if (!out || cap == 0 || !base || !ext) return 0;
    const char *dot = strrchr(base, '.');
    size_t stem_len = dot ? (size_t)(dot - base) : strlen(base);
    int n = snprintf(out, cap, "%.*s.p%d%s", (int)stem_len, base, plane, ext);
    return n > 0 && (size_t)n < cap;
}

static int companion_path(char *out, size_t cap, const char *base,
                          const char *suffix) {
    if (!out || cap == 0 || !base || !suffix) return 0;
    const char *dot = strrchr(base, '.');
    size_t stem_len = dot ? (size_t)(dot - base) : strlen(base);
    int n = snprintf(out, cap, "%.*s%s", (int)stem_len, base, suffix);
    return n > 0 && (size_t)n < cap;
}

static Rectangle scene_plane_up_button_rect(void) {
    return (Rectangle){14.0f, 88.0f, 32.0f, 26.0f};
}

static Rectangle scene_plane_down_button_rect(void) {
    return (Rectangle){14.0f, 146.0f, 32.0f, 26.0f};
}

static Rectangle scene_plane_reset_button_rect(void) {
    return (Rectangle){14.0f, 116.0f, 32.0f, 28.0f};
}

static void draw_triangle_button_icon(Rectangle r, int up, Color color) {
    float cx = r.x + r.width * 0.5f;
    float cy = r.y + r.height * 0.5f;
    if (up) {
        DrawTriangle((Vector2){cx, cy - 6.0f},
                     (Vector2){cx - 7.0f, cy + 5.0f},
                     (Vector2){cx + 7.0f, cy + 5.0f}, color);
    } else {
        DrawTriangle((Vector2){cx, cy + 6.0f},
                     (Vector2){cx + 7.0f, cy - 5.0f},
                     (Vector2){cx - 7.0f, cy - 5.0f}, color);
    }
}

static void draw_scene_plane_controls(ViewerState *v) {
    if (!v || !v->world) return;
    Rectangle up = scene_plane_up_button_rect();
    Rectangle reset = scene_plane_reset_button_rect();
    Rectangle down = scene_plane_down_button_rect();
    Vector2 mouse = GetMousePosition();
    Color bg = (Color){20, 22, 24, 210};
    Color hover = (Color){64, 70, 76, 235};
    Color border = (Color){170, 145, 82, 245};
    Color icon = (Color){240, 225, 180, 255};

    DrawRectangleRounded((Rectangle){10.0f, 84.0f, 40.0f, 92.0f}, 0.12f, 6,
                         (Color){12, 14, 16, 175});
    Rectangle buttons[3] = {up, reset, down};
    for (int i = 0; i < 3; i++) {
        Color fill = CheckCollisionPointRec(mouse, buttons[i]) ? hover : bg;
        DrawRectangleRounded(buttons[i], 0.12f, 6, fill);
        DrawRectangleRoundedLines(buttons[i], 0.12f, 6, border);
    }
    draw_triangle_button_icon(up, 1, icon);
    draw_triangle_button_icon(down, 0, icon);

    char label[8];
    snprintf(label, sizeof(label), "P%d", viewer_scene_plane(v));
    int text_w = MeasureText(label, 14);
    DrawText(label, (int)(reset.x + (reset.width - (float)text_w) * 0.5f),
             (int)(reset.y + 7.0f), 14, icon);
}

static int handle_scene_plane_buttons(ViewerState *v) {
    if (!v || !v->world || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        return 0;
    Vector2 mouse = GetMousePosition();
    if (CheckCollisionPointRec(mouse, scene_plane_up_button_rect())) {
        viewer_set_scene_plane_delta(v, 1);
        return 1;
    }
    if (CheckCollisionPointRec(mouse, scene_plane_down_button_rect())) {
        viewer_set_scene_plane_delta(v, -1);
        return 1;
    }
    if (CheckCollisionPointRec(mouse, scene_plane_reset_button_rect())) {
        v->scene_plane_override = -1;
        return 1;
    }
    return 0;
}

static void load_terrain_plane_assets(ViewerState *v, const char *base_path) {
    if (!v || !base_path) return;
    for (int plane = 0; plane < RC_MAX_PLANES; plane++) {
        char env_key[32];
        snprintf(env_key, sizeof(env_key), "RUNEC_TERRAIN_P%d", plane);
        const char *path = getenv(env_key);
        char variant[1024];
        if (!path || !path[0]) {
            if (plane == 0) {
                path = base_path;
            } else if (plane_path_variant(variant, sizeof(variant), base_path,
                                          plane, ".terrain")
                    && rc_asset_exists(variant)) {
                path = variant;
            } else {
                continue;
            }
        }
        TerrainMesh *tm = terrain_load(path);
        if (!tm) continue;
        terrain_offset(tm, g_world_origin_x, g_world_origin_y);
        v->terrain_planes[plane] = tm;
        if (plane == 0)
            v->terrain = tm;
    }
    if (!v->terrain) {
        for (int plane = 0; plane < RC_MAX_PLANES; plane++) {
            if (v->terrain_planes[plane]) {
                v->terrain = v->terrain_planes[plane];
                break;
            }
        }
    }
}

static void load_object_plane_assets(ViewerState *v, const char *base_path) {
    if (!v || !base_path) return;
    for (int plane = 0; plane < RC_MAX_PLANES; plane++) {
        char env_key[32];
        snprintf(env_key, sizeof(env_key), "RUNEC_OBJECTS_P%d", plane);
        const char *path = getenv(env_key);
        char variant[1024];
        if (!path || !path[0]) {
            if (plane == 0) {
                path = base_path;
            } else if (plane_path_variant(variant, sizeof(variant), base_path,
                                          plane, ".objects")
                    && rc_asset_exists(variant)) {
                path = variant;
            } else {
                continue;
            }
        }
        ObjectMesh *om = objects_load(path);
        if (!om) continue;
        objects_offset(om, g_world_origin_x, g_world_origin_y);
        if (v->alpha_cutout_shader_static_loaded)
            objects_set_shader(om, v->alpha_cutout_shader_static);
        v->object_planes[plane] = om;
        if (om->object_anim_count > 0) {
            char anim_models_path[1024];
            if (companion_path(anim_models_path, sizeof(anim_models_path), path,
                               ".object_anim.models")
                    && rc_asset_exists(anim_models_path)) {
                v->object_anim_model_planes[plane] =
                    models_load(anim_models_path);
                if (v->object_anim_model_planes[plane]) {
                    if (v->alpha_cutout_shader_static_loaded) {
                        models_set_shader(v->object_anim_model_planes[plane],
                                          v->alpha_cutout_shader_static);
                    }
                    create_object_anim_plane_states(v, plane);
                }
            }
        }
        if (plane == 0)
            v->objects = om;
    }
    if (!v->objects) {
        for (int plane = 0; plane < RC_MAX_PLANES; plane++) {
            if (v->object_planes[plane]) {
                v->objects = v->object_planes[plane];
                break;
            }
        }
    }
}

static void load_item_stack_variants(ViewerState *v, const char *path) {
    if (!v || !path || !path[0])
        return;
    FILE *f = rc_asset_fopen(path, "r");
    if (!f) {
        fprintf(stderr, "item_stack_variants: unavailable at %s\n", path);
        return;
    }

    char line[128];
    int loaded = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;
        ItemStackVariant variant = {0};
        if (sscanf(line, "%d %d %d", &variant.base_id, &variant.threshold,
                   &variant.variant_id) != 3)
            continue;
        if (variant.base_id <= 0 || variant.threshold <= 1 || variant.variant_id <= 0)
            continue;
        if (v->item_stack_variant_count >= RUNEC_ITEM_STACK_VARIANT_MAX)
            break;
        v->item_stack_variants[v->item_stack_variant_count++] = variant;
        loaded++;
    }
    rc_asset_close(f);
    fprintf(stderr, "item_stack_variants: loaded %d display variants from %s\n",
            loaded, path);
}

static Shader load_alpha_cutout_shader(float brightness, float lift) {
    const char *vs =
        "#version 330\n"
        "in vec3 vertexPosition;\n"
        "in vec2 vertexTexCoord;\n"
        "in vec4 vertexColor;\n"
        "uniform mat4 mvp;\n"
        "out vec2 fragTexCoord;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "    fragTexCoord = vertexTexCoord;\n"
        "    fragColor = vertexColor;\n"
        "    gl_Position = mvp*vec4(vertexPosition, 1.0);\n"
        "}\n";
    char fs[1024];
    snprintf(fs, sizeof(fs),
        "#version 330\n"
        "in vec2 fragTexCoord;\n"
        "in vec4 fragColor;\n"
        "uniform sampler2D texture0;\n"
        "uniform vec4 colDiffuse;\n"
        "out vec4 finalColor;\n"
        "void main() {\n"
        "    vec4 texel = texture(texture0, fragTexCoord);\n"
        "    vec4 color = texel*fragColor*colDiffuse;\n"
        "    if (color.a < 0.5) discard;\n"
        "    color.rgb = min(color.rgb * %.3ff + vec3(%.3ff), vec3(1.0));\n"
        "    finalColor = color;\n"
        "}\n",
        brightness, lift);

    Shader shader = LoadShaderFromMemory(vs, fs);
    if (shader.id <= 0)
        return shader;
    shader.locs[SHADER_LOC_VERTEX_POSITION] =
        GetShaderLocationAttrib(shader, "vertexPosition");
    shader.locs[SHADER_LOC_VERTEX_TEXCOORD01] =
        GetShaderLocationAttrib(shader, "vertexTexCoord");
    shader.locs[SHADER_LOC_VERTEX_COLOR] =
        GetShaderLocationAttrib(shader, "vertexColor");
    shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(shader, "mvp");
    shader.locs[SHADER_LOC_COLOR_DIFFUSE] =
        GetShaderLocation(shader, "colDiffuse");
    shader.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(shader, "texture0");
    return shader;
}

static float ground_y_plane(ViewerState *v, int plane, int world_x,
                            int world_y) {
    TerrainMesh *terrain = viewer_terrain_for_plane(v, plane);
    if (terrain && terrain->loaded)
        return terrain_height_avg(terrain, LOCAL_X(world_x), LOCAL_Y(world_y)) + 0.05f;
    return 0.0f;
}

static float ground_y(ViewerState *v, int world_x, int world_y) {
    return ground_y_plane(v, viewer_scene_plane(v), world_x, world_y);
}

static float ground_yf_plane(ViewerState *v, int plane, float world_x,
                             float world_y) {
    TerrainMesh *terrain = viewer_terrain_for_plane(v, plane);
    if (!terrain || !terrain->loaded || !terrain->heightmap)
        return 0.0f;
    float lx = world_x - (float)g_world_origin_x;
    float ly = world_y - (float)g_world_origin_y;
    int x0 = (int)floorf(lx);
    int y0 = (int)floorf(ly);
    float fx = lx - (float)x0;
    float fy = ly - (float)y0;
    float h00 = terrain_height_avg(terrain, x0, y0);
    float h10 = terrain_height_avg(terrain, x0 + 1, y0);
    float h01 = terrain_height_avg(terrain, x0, y0 + 1);
    float h11 = terrain_height_avg(terrain, x0 + 1, y0 + 1);
    float hx0 = h00 + (h10 - h00) * fx;
    float hx1 = h01 + (h11 - h01) * fx;
    return hx0 + (hx1 - hx0) * fy + 0.05f;
}

static float ground_yf(ViewerState *v, float world_x, float world_y) {
    return ground_yf_plane(v, viewer_scene_plane(v), world_x, world_y);
}

static int append_unique_model_id(uint32_t *ids, int count, uint32_t id) {
    for (int i = 0; i < count; i++)
        if (ids[i] == id) return count;
    ids[count++] = id;
    return count;
}

static int collect_spawned_npc_model_ids(RcWorld *world, uint32_t *ids,
                                         int max_ids,
                                         int min_plane, int max_plane) {
    int count = 0;
    for (int i = 0; i < world->npc_count && count < max_ids; i++) {
        const RcNpc *npc = &world->npcs[i];
        if (!npc->active || npc->plane < min_plane || npc->plane > max_plane
                || npc->def_id >= (uint32_t)g_npc_def_count) continue;
        count = append_unique_model_id(ids, count, (uint32_t)g_npc_defs[npc->def_id].id);
    }
    return count;
}

static void free_npc_anim_states(ViewerState *v) {
    if (!v) return;
    for (int i = 0; i < RC_MAX_NPC_DEFS; i++) {
        anim_model_state_free(v->npc_anim_state[i]);
        v->npc_anim_state[i] = NULL;
    }
}

static void create_npc_anim_states(ViewerState *v) {
    if (!v || (!v->npc_anims && !v->npc_fallback_anims)
            || !v->npc_models || !v->npc_models->loaded) {
        return;
    }
    int created = 0;
    for (int i = 0; i < g_npc_def_count; i++) {
        ModelEntry *me = model_find(v->npc_models, (uint32_t)g_npc_defs[i].id);
        if (me && me->loaded && me->vertex_skins && me->base_vert_count > 0) {
            v->npc_anim_state[i] = anim_model_state_create(
                me->vertex_skins, me->base_vert_count);
            created++;
        }
    }
    fprintf(stderr, "npc_anim: created %d per-def anim states\n", created);
}

static void reload_npc_models_for_scene(ViewerState *v) {
    if (!v || !v->world)
        return;
    free_npc_anim_states(v);
    models_free(v->npc_models);
    v->npc_models = NULL;

    uint32_t *npc_model_ids =
        calloc((size_t)v->world->npc_count, sizeof(uint32_t));
    int npc_model_id_count = 0;
    if (npc_model_ids) {
        npc_model_id_count = collect_spawned_npc_model_ids(
            v->world, npc_model_ids, v->world->npc_count, 0,
            RC_MAX_PLANES - 1);
    }
    uint32_t empty_model_ids[1] = {0};
    const uint32_t *model_filter = npc_model_ids ? npc_model_ids
                                                 : empty_model_ids;
    v->npc_models = models_load_filtered(
        env_path("RUNEC_NPC_MODELS", "data/models/npcs.models"),
        model_filter, npc_model_id_count);
    free(npc_model_ids);
    if (v->npc_models && v->alpha_cutout_shader_dynamic_loaded)
        models_set_shader(v->npc_models, v->alpha_cutout_shader_dynamic);
    create_npc_anim_states(v);
}

static int tile_from_ray_sample(const Ray *ray, float t, float *out_lx,
                                float *out_ly, int *out_wx, int *out_wy) {
    float lx = ray->position.x + ray->direction.x * t;
    float ly = -(ray->position.z + ray->direction.z * t);
    int wx = (int)floorf(lx) + g_world_origin_x;
    int wy = (int)floorf(ly) + g_world_origin_y;
    if (out_lx) *out_lx = lx;
    if (out_ly) *out_ly = ly;
    if (out_wx) *out_wx = wx;
    if (out_wy) *out_wy = wy;
    return lx >= 0.0f && lx < (float)g_world_w
        && ly >= 0.0f && ly < (float)g_world_h;
}

// Returns world tile coordinates
static int raycast_tile(ViewerState *v, int *out_x, int *out_y) {
    Ray ray = GetScreenToWorldRay(GetMousePosition(), v->camera);
    if (fabsf(ray.direction.y) < 0.001f)
        return 0;

    float prev_t = 0.0f;
    float prev_delta = 0.0f;
    int have_prev = 0;
    const float max_t = 2048.0f;
    for (int i = 1; i <= 256; i++) {
        float t = max_t * (float)i / 256.0f;
        int wx = 0, wy = 0;
        if (!tile_from_ray_sample(&ray, t, NULL, NULL, &wx, &wy))
            continue;
        float y = ray.position.y + ray.direction.y * t;
        float ground = ground_yf(v, (float)wx, (float)wy);
        float delta = y - ground;
        if (have_prev && prev_delta >= 0.0f && delta <= 0.0f) {
            float lo = prev_t;
            float hi = t;
            int hit_wx = wx;
            int hit_wy = wy;
            for (int j = 0; j < 16; j++) {
                float mid = (lo + hi) * 0.5f;
                int mid_wx = 0, mid_wy = 0;
                if (!tile_from_ray_sample(&ray, mid, NULL, NULL,
                                          &mid_wx, &mid_wy)) {
                    hi = mid;
                    continue;
                }
                float mid_y = ray.position.y + ray.direction.y * mid;
                float mid_ground = ground_yf(v, (float)mid_wx,
                                             (float)mid_wy);
                if (mid_y > mid_ground) {
                    lo = mid;
                } else {
                    hi = mid;
                    hit_wx = mid_wx;
                    hit_wy = mid_wy;
                }
            }
            *out_x = hit_wx;
            *out_y = hit_wy;
            return hit_wx >= g_world_origin_x
                && hit_wx < g_world_origin_x + g_world_w
                && hit_wy >= g_world_origin_y
                && hit_wy < g_world_origin_y + g_world_h;
        }
        prev_t = t;
        prev_delta = delta;
        have_prev = 1;
    }

    float gy = ground_y(v, g_world_origin_x + g_world_w / 2,
                        g_world_origin_y + g_world_h / 2);
    float t = (gy - ray.position.y) / ray.direction.y;
    if (t < 0.0f) return 0;
    int wx = 0, wy = 0;
    if (!tile_from_ray_sample(&ray, t, NULL, NULL, &wx, &wy))
        return 0;
    *out_x = wx;
    *out_y = wy;
    return 1;
}

static Color darken_color(Color c, unsigned char amount) {
    c.r = c.r > amount ? (unsigned char)(c.r - amount) : 0;
    c.g = c.g > amount ? (unsigned char)(c.g - amount) : 0;
    c.b = c.b > amount ? (unsigned char)(c.b - amount) : 0;
    c.a = 255;
    return c;
}

static Color minimap_quantize_color(Color c) {
    int max = c.r;
    if (c.g > max) max = c.g;
    if (c.b > max) max = c.b;

    if (c.b > c.g + 18 && c.b > c.r + 18)
        return (Color){56, 78, 112, 255};  // water
    if (c.g > c.r + 10 && c.g > c.b + 8)
        return (Color){62, 104, 43, 255};  // grass
    if (c.r > 105 && c.g > 85 && c.b < 95)
        return (Color){111, 96, 63, 255};  // paths / roofs
    if (max < 55)
        return (Color){45, 39, 31, 255};
    if (c.r > c.g && c.r > c.b)
        return (Color){88, 70, 48, 255};
    return (Color){78, 83, 58, 255};
}

static void minimap_put_pixel(Color *pixels, int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= 152 || y >= 152)
        return;
    float dx = (float)x - 76.0f;
    float dy = (float)y - 76.0f;
    if (dx * dx + dy * dy > 75.0f * 75.0f)
        return;
    pixels[x + y * 152] = c;
}

static void minimap_fill_rect(Color *pixels, int x, int y, int w, int h, Color c) {
    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++)
            minimap_put_pixel(pixels, x + xx, y + yy, c);
    }
}

static void minimap_draw_tile_features(ViewerState *v, Color *pixels,
                                       int wx, int wy, int sx, int sy) {
    uint32_t f = rc_get_flags(&v->world->map, wx, wy,
                              viewer_scene_plane(v));
    if (!f)
        return;

    if (f & COL_LOC)
        minimap_fill_rect(pixels, sx - 1, sy - 1, 3, 3, (Color){73, 62, 45, 255});

    Color wall = (Color){224, 221, 198, 255};
    if (f & COL_WALL_N)
        minimap_fill_rect(pixels, sx - 2, sy - 2, 5, 1, wall);
    if (f & COL_WALL_S)
        minimap_fill_rect(pixels, sx - 2, sy + 2, 5, 1, wall);
    if (f & COL_WALL_W)
        minimap_fill_rect(pixels, sx - 2, sy - 2, 1, 5, wall);
    if (f & COL_WALL_E)
        minimap_fill_rect(pixels, sx + 2, sy - 2, 1, 5, wall);
}

static void build_minimap_tiles(ViewerState *v) {
    int w = g_world_w;
    int h = g_world_h;
    int scene_plane = viewer_scene_plane(v);
    free(v->minimap_tiles);
    v->minimap_tiles = calloc((size_t)w * (size_t)h, sizeof(Color));
    v->minimap_tiles_w = w;
    v->minimap_tiles_h = h;
    v->minimap_tiles_plane = scene_plane;
    if (!v->minimap_tiles)
        return;

    for (int i = 0; i < w * h; i++)
        v->minimap_tiles[i] = (Color){64, 96, 48, 255};

    TerrainMesh *terrain = viewer_terrain_for_plane(v, scene_plane);
    if (terrain && terrain->loaded && terrain->model.meshCount > 0) {
        Mesh *mesh = &terrain->model.meshes[0];
        if (mesh->vertices && mesh->colors) {
            unsigned int *sum_r = calloc((size_t)w * (size_t)h, sizeof(unsigned int));
            unsigned int *sum_g = calloc((size_t)w * (size_t)h, sizeof(unsigned int));
            unsigned int *sum_b = calloc((size_t)w * (size_t)h, sizeof(unsigned int));
            unsigned int *count = calloc((size_t)w * (size_t)h, sizeof(unsigned int));
            if (sum_r && sum_g && sum_b && count) {
                for (int i = 0; i < mesh->vertexCount; i++) {
                    int lx = (int)floorf(mesh->vertices[i * 3]);
                    int ly = (int)floorf(-mesh->vertices[i * 3 + 2]);
                    if (lx < 0 || lx >= w || ly < 0 || ly >= h)
                        continue;
                    int idx = lx + ly * w;
                    sum_r[idx] += mesh->colors[i * 4 + 0];
                    sum_g[idx] += mesh->colors[i * 4 + 1];
                    sum_b[idx] += mesh->colors[i * 4 + 2];
                    count[idx]++;
                }
                for (int i = 0; i < w * h; i++) {
                    if (count[i] == 0)
                        continue;
                    Color avg = (Color){
                        (unsigned char)(sum_r[i] / count[i]),
                        (unsigned char)(sum_g[i] / count[i]),
                        (unsigned char)(sum_b[i] / count[i]),
                        255
                    };
                    v->minimap_tiles[i] = minimap_quantize_color(avg);
                }
            }
            free(sum_r);
            free(sum_g);
            free(sum_b);
            free(count);
        }
    }

    for (int lx = 0; lx < w; lx++) {
        for (int ly = 0; ly < h; ly++) {
            uint32_t f = rc_get_flags(&v->world->map,
                                      g_world_origin_x + lx,
                                      g_world_origin_y + ly,
                                      scene_plane);
            if (!f)
                continue;
            int idx = lx + ly * w;
            if (f & COL_LOC)
                v->minimap_tiles[idx] = darken_color(v->minimap_tiles[idx], 35);
            else if (f & COL_BLOCK_WALK)
                v->minimap_tiles[idx] = darken_color(v->minimap_tiles[idx], 18);
            if (f & (COL_WALL_N | COL_WALL_S | COL_WALL_E | COL_WALL_W))
                v->minimap_tiles[idx] = darken_color(v->minimap_tiles[idx], 28);
        }
    }
}

static void unload_scene_visual_assets(ViewerState *v) {
    if (!v) return;
    for (int plane = 0; plane < RC_MAX_PLANES; plane++) {
        terrain_free(v->terrain_planes[plane]);
        objects_free(v->object_planes[plane]);
        free_object_anim_plane(v, plane);
        v->terrain_planes[plane] = NULL;
        v->object_planes[plane] = NULL;
    }
    v->terrain = NULL;
    v->objects = NULL;
    free(v->minimap_tiles);
    v->minimap_tiles = NULL;
    v->minimap_tiles_w = 0;
    v->minimap_tiles_h = 0;
    v->minimap_tiles_plane = -1;
}

static void scene_bounds_for_tile(int x, int y, int radius_regions,
                                  int *origin_x, int *origin_y,
                                  int *world_w, int *world_h) {
    int center_rx = x >> 6;
    int center_ry = y >> 6;
    int min_rx = center_rx - radius_regions;
    int min_ry = center_ry - radius_regions;
    int side = radius_regions * 2 + 1;
    *origin_x = min_rx * 64;
    *origin_y = min_ry * 64;
    *world_w = side * 64;
    *world_h = side * 64;
}

static void scene_plane_file(char *out, size_t cap, const char *prefix,
                             int plane, const char *suffix) {
    if (plane == 0)
        snprintf(out, cap, "%s%s", prefix, suffix);
    else
        snprintf(out, cap, "%s.p%d%s", prefix, plane, suffix);
}

static int path_mtime(const char *path, time_t *out) {
    struct stat st;
    if (!path || stat(path, &st) != 0)
        return 0;
    if (out) *out = st.st_mtime;
    return 1;
}

static int scene_objects_current(const char *objects_path) {
    time_t objects_mtime;
    if (!path_mtime(objects_path, &objects_mtime))
        return rc_asset_exists(objects_path);
    const char *deps[] = {
        "tools/cache_pipeline/export_scene_slice.py",
        "tools/cache_pipeline/export_terrain.py",
        "tools/cache_pipeline/export_objects.py",
        "data/defs/object_defs.bin",
        "data/defs/object_behaviors.bin",
        "data/defs/object_placements.bin",
    };
    for (size_t i = 0; i < sizeof(deps) / sizeof(deps[0]); i++) {
        time_t dep_mtime;
        if (path_mtime(deps[i], &dep_mtime) && dep_mtime > objects_mtime)
            return 0;
    }
    return 1;
}

static int scene_plane_files_exist(const char *prefix, int plane) {
    if (!prefix || !prefix[0])
        return 0;
    char path[1024];
    plane = clamp_plane(plane);
    scene_plane_file(path, sizeof(path), prefix, plane, ".terrain");
    if (!rc_asset_exists(path))
        return 0;
    char objects_path[1024];
    scene_plane_file(objects_path, sizeof(objects_path), prefix, plane,
                     ".objects");
    if (!rc_asset_exists(objects_path) || !scene_objects_current(objects_path))
        return 0;
    scene_plane_file(path, sizeof(path), prefix, plane, ".atlas");
    return rc_asset_exists(path);
}

static int ensure_scene_slice_plane_assets(ViewerState *v, int center_x,
                                           int center_y, const char *prefix,
                                           int plane) {
    plane = clamp_plane(plane);
    if (scene_plane_files_exist(prefix, plane))
        return 1;
    if (!v->scene_auto_export)
        return 0;

    const char *cache = env_path("RUNEC_CACHE",
        "tools/cache_pipeline/source/current_fightcaves_demo/data/cache");
    char cmd[4096];
    int timeout_seconds = env_int("RUNEC_SCENE_EXPORT_TIMEOUT_SECONDS", 20);
    if (timeout_seconds < 1)
        timeout_seconds = 1;
    int n = snprintf(cmd, sizeof(cmd),
        "timeout %d python3 tools/cache_pipeline/export_scene_slice.py "
        "--center-x %d --center-y %d --radius-regions %d "
        "--cache %s --output-prefix %s --planes %d",
        timeout_seconds, center_x, center_y, v->scene_radius_regions, cache,
        prefix, plane);
    if (n <= 0 || (size_t)n >= sizeof(cmd))
        return 0;
    fprintf(stderr,
            "viewer scene: generating b237 slice around %d,%d plane %d\n",
            center_x, center_y, plane);
    int status = system(cmd);
    return status == 0 && scene_plane_files_exist(prefix, plane);
}

static int load_generated_scene_plane_assets(ViewerState *v,
                                             const char *prefix, int plane) {
    if (!v || !prefix || !prefix[0])
        return 0;
    plane = clamp_plane(plane);
    char terrain_path[1024];
    char objects_path[1024];
    scene_plane_file(terrain_path, sizeof(terrain_path), prefix, plane,
                     ".terrain");
    scene_plane_file(objects_path, sizeof(objects_path), prefix, plane,
                     ".objects");
    if (!rc_asset_exists(terrain_path) || !rc_asset_exists(objects_path))
        return 0;

    terrain_free(v->terrain_planes[plane]);
    objects_free(v->object_planes[plane]);
    free_object_anim_plane(v, plane);
    v->terrain_planes[plane] = NULL;
    v->object_planes[plane] = NULL;
    v->terrain = NULL;
    v->objects = NULL;

    TerrainMesh *tm = terrain_load(terrain_path);
    if (tm) {
        terrain_offset(tm, g_world_origin_x, g_world_origin_y);
        v->terrain_planes[plane] = tm;
    }

    ObjectMesh *om = objects_load(objects_path);
    if (om) {
        objects_offset(om, g_world_origin_x, g_world_origin_y);
        if (v->alpha_cutout_shader_static_loaded)
            objects_set_shader(om, v->alpha_cutout_shader_static);
        v->object_planes[plane] = om;
        if (om->object_anim_count > 0) {
            char anim_models_path[1024];
            if (companion_path(anim_models_path, sizeof(anim_models_path),
                               objects_path, ".object_anim.models")
                    && rc_asset_exists(anim_models_path)) {
                v->object_anim_model_planes[plane] =
                    models_load(anim_models_path);
                if (v->object_anim_model_planes[plane]) {
                    if (v->alpha_cutout_shader_static_loaded) {
                        models_set_shader(v->object_anim_model_planes[plane],
                                          v->alpha_cutout_shader_static);
                    }
                    create_object_anim_plane_states(v, plane);
                }
            }
        }
    }

    for (int i = 0; i < RC_MAX_PLANES; i++) {
        if (!v->terrain && v->terrain_planes[i])
            v->terrain = v->terrain_planes[i];
        if (!v->objects && v->object_planes[i])
            v->objects = v->object_planes[i];
    }
    return v->terrain_planes[plane] || v->object_planes[plane];
}

static void reload_npc_spawns_for_scene(ViewerState *v) {
    if (!v || !v->world)
        return;
    const char *path = env_path("RUNEC_NPC_SPAWNS",
        "data/spawns/world.npc-spawns.bin");
    memset(v->world->npcs, 0, sizeof(v->world->npcs));
    v->world->npc_count = 0;
    memset(v->npc_render, 0, sizeof(v->npc_render));

    RcNpcSpawnLoadStats stats;
    int spawned = rc_load_npc_spawns_rect_stats(
        v->world, path, g_world_origin_x, g_world_origin_y,
        g_world_origin_x + g_world_w - 1,
        g_world_origin_y + g_world_h - 1,
        0, RC_MAX_PLANES - 1, &stats);
    if (spawned >= 0) {
        fprintf(stderr,
                "viewer npc slice reload: rows=%d matched=%d spawned=%d"
                " planes=[%d,%d,%d,%d]\n",
                stats.total_rows, stats.matched_filter, stats.spawned,
                stats.spawned_plane_counts[0], stats.spawned_plane_counts[1],
                stats.spawned_plane_counts[2], stats.spawned_plane_counts[3]);
    }
    reload_npc_models_for_scene(v);
}

static int scene_tile_in_initial_scene(const ViewerState *v, int x, int y) {
    return v && v->initial_scene_ready
        && x >= v->initial_scene_origin_x
        && x < v->initial_scene_origin_x + v->initial_scene_w
        && y >= v->initial_scene_origin_y
        && y < v->initial_scene_origin_y + v->initial_scene_h;
}

static int reload_initial_scene(ViewerState *v) {
    if (!v || !v->initial_scene_ready)
        return 0;
    unload_scene_visual_assets(v);
    g_world_origin_x = v->initial_scene_origin_x;
    g_world_origin_y = v->initial_scene_origin_y;
    g_world_w = v->initial_scene_w;
    g_world_h = v->initial_scene_h;
    load_terrain_plane_assets(v, v->initial_terrain_path);
    load_object_plane_assets(v, v->initial_objects_path);
    build_minimap_tiles(v);
    reload_npc_spawns_for_scene(v);
    v->active_scene_prefix[0] = '\0';
    fprintf(stderr,
            "viewer scene: reloaded initial scene origin %d,%d size %dx%d\n",
            g_world_origin_x, g_world_origin_y, g_world_w, g_world_h);
    return 1;
}

static int reload_scene_around_player(ViewerState *v, int center_x,
                                      int center_y) {
    if (!v || !v->world)
        return 0;
    if (scene_tile_in_initial_scene(v, center_x, center_y))
        return reload_initial_scene(v);

    int origin_x, origin_y, world_w, world_h;
    scene_bounds_for_tile(center_x, center_y, v->scene_radius_regions,
                          &origin_x, &origin_y, &world_w, &world_h);

    const char *dir = env_path("RUNEC_SCENE_CACHE_DIR",
                               "data/regions/scene_cache");
    char prefix[1024];
    int n = snprintf(prefix, sizeof(prefix), "%s/scene_%d_%d_r%d",
                     dir, origin_x, origin_y, v->scene_radius_regions);
    if (n <= 0 || (size_t)n >= sizeof(prefix))
        return 0;
    int required_plane = clamp_plane(v->world->player.plane);
    if (!ensure_scene_slice_plane_assets(v, center_x, center_y, prefix,
                                         required_plane))
        return 0;

    char terrain_path[1024];
    char objects_path[1024];
    scene_plane_file(terrain_path, sizeof(terrain_path), prefix, 0,
                     ".terrain");
    scene_plane_file(objects_path, sizeof(objects_path), prefix, 0,
                     ".objects");

    unload_scene_visual_assets(v);
    g_world_origin_x = origin_x;
    g_world_origin_y = origin_y;
    g_world_w = world_w;
    g_world_h = world_h;
    load_terrain_plane_assets(v, terrain_path);
    load_object_plane_assets(v, objects_path);
    build_minimap_tiles(v);
    reload_npc_spawns_for_scene(v);
    strncpy(v->active_scene_prefix, prefix, sizeof(v->active_scene_prefix) - 1);
    v->active_scene_prefix[sizeof(v->active_scene_prefix) - 1] = '\0';
    fprintf(stderr,
            "viewer scene: loaded generated slice origin %d,%d size %dx%d\n",
            g_world_origin_x, g_world_origin_y, g_world_w, g_world_h);
    return 1;
}

static void ensure_active_scene_plane(ViewerState *v, int plane) {
    if (!v || !v->world || !v->active_scene_prefix[0])
        return;
    plane = clamp_plane(plane);
    if (v->terrain_planes[plane] || v->object_planes[plane])
        return;
    RcPlayer *p = &v->world->player;
    if (!ensure_scene_slice_plane_assets(v, p->x, p->y,
                                         v->active_scene_prefix, plane)) {
        return;
    }
    if (load_generated_scene_plane_assets(v, v->active_scene_prefix, plane))
        build_minimap_tiles(v);
}

// Temporary combat-validation transports. Coordinates come from local
// OSRS/VoidPS spawn-area data; boss NPC ids come from the b237 symbol dump.
static const ViewerDevTransport g_dev_transports[] = {
    {"varrock",  "Varrock",   DEFAULT_PLAYER_START_X, DEFAULT_PLAYER_START_Y, 0,   -1, 1},
    {"graardor", "Graardor",  2872, 5358, 2, 2215, 4},
    {"kbd",      "KBD",       2269, 4697, 0,  239, 5},
    {"vorkath",  "Vorkath",   2269, 4062, 0, 8061, 7},
    {"jad",      "Jad",       2400, 5088, 0, 3127, 5},
};

static int dev_transport_enabled(void) {
    return env_bool("RUNEC_DEV_TRANSPORT_PANEL", 1);
}

static int dev_transport_count(void) {
    return (int)(sizeof(g_dev_transports) / sizeof(g_dev_transports[0]));
}

static Rectangle dev_transport_button_rect(int idx) {
    return (Rectangle){56.0f, 108.0f + (float)idx * 25.0f, 118.0f, 22.0f};
}

static const ViewerDevTransport *find_dev_transport(const char *key) {
    if (!key || !key[0])
        return NULL;
    for (int i = 0; i < dev_transport_count(); i++) {
        const ViewerDevTransport *d = &g_dev_transports[i];
        if (strcmp(key, d->key) == 0 || strcmp(key, d->label) == 0)
            return d;
    }
    return NULL;
}

static int viewer_has_npc_near(ViewerState *v, int npc_id, int x, int y,
                               int plane, int radius) {
    if (!v || !v->world || npc_id < 0)
        return 0;
    for (int i = 0; i < v->world->npc_count; i++) {
        RcNpc *npc = &v->world->npcs[i];
        if (!npc->active || npc->def_id < 0 || npc->def_id >= g_npc_def_count)
            continue;
        if (g_npc_defs[npc->def_id].id != npc_id || npc->plane != plane)
            continue;
        if (abs(npc->x - x) <= radius && abs(npc->y - y) <= radius)
            return 1;
    }
    return 0;
}

static int viewer_focus_npc_idx(ViewerState *v,
                                const ViewerDevTransport *d) {
    if (!v || !v->world || !d || d->npc_id < 0)
        return -1;
    for (int i = 0; i < v->world->npc_count; i++) {
        RcNpc *npc = &v->world->npcs[i];
        if (!npc->active || npc->def_id < 0 || npc->def_id >= g_npc_def_count)
            continue;
        if (g_npc_defs[npc->def_id].id != d->npc_id || npc->plane != d->plane)
            continue;
        if (abs(npc->x - d->target_x) <= 8 &&
                abs(npc->y - d->target_y) <= 8) {
            return i;
        }
    }
    return -1;
}

static int viewer_spawn_focus_npc(ViewerState *v,
                                  const ViewerDevTransport *d) {
    if (!v || !v->world || !d || d->npc_id < 0)
        return 0;
    if (viewer_has_npc_near(v, d->npc_id, d->target_x, d->target_y,
                            d->plane, 8)) {
        return 0;
    }
    int def_idx = rc_npc_def_find(d->npc_id);
    if (def_idx < 0) {
        fprintf(stderr, "dev transport: missing NPC def %d for %s\n",
                d->npc_id, d->label);
        return 0;
    }
    int idx = rc_npc_spawn(v->world, def_idx, d->target_x, d->target_y,
                           d->plane);
    if (idx < 0)
        return 0;
    memset(&v->npc_render[idx], 0, sizeof(v->npc_render[idx]));
    fprintf(stderr, "dev transport: spawned %s NPC %d at %d,%d,%d\n",
            d->label, d->npc_id, d->target_x, d->target_y, d->plane);
    return 1;
}

static void viewer_start_dev_boss_combat(ViewerState *v,
                                         const ViewerDevTransport *d) {
    if (!v || !v->world || !d || d->npc_id < 0 ||
            !env_bool("RUNEC_DEV_BOSS_ATTACKS", 1)) {
        return;
    }
    int idx = viewer_focus_npc_idx(v, d);
    if (idx < 0)
        return;
    RcNpc *npc = &v->world->npcs[idx];
    if (rc_combat_start_npc_vs_player(v->world, npc->uid, 0)) {
        npc->attack_timer = 0;
        npc->combat.attack_animation_id =
            npc->def_id >= 0 && npc->def_id < g_npc_def_count
            ? g_npc_defs[npc->def_id].attack_anim : 0;
    }
}

static void viewer_clear_player_activity(ViewerState *v) {
    if (!v || !v->world)
        return;
    RcWorld *world = v->world;
    RcPlayer *p = &world->player;
    p->route_len = 0;
    p->route_idx = 0;
    p->interaction.active = false;
    p->pending_traversal_active = 0;
    p->pending_traversal_x = -1;
    p->pending_traversal_y = -1;
    p->pending_traversal_plane = -1;
    p->action_lock_timer = 0;
    p->action_anim_timer = 0;
    p->attack_anim_timer = 0;
    world->combat_projectile_count = 0;
    rc_combat_stop_actor(world, (RcCombatActorRef){RC_COMBAT_ACTOR_PLAYER, 0},
                         RC_COMBAT_STATE_CANCELLED);
    for (int i = 0; i < world->npc_count; i++) {
        if (world->npcs[i].active) {
            rc_combat_stop_actor(
                world, (RcCombatActorRef){RC_COMBAT_ACTOR_NPC,
                                          world->npcs[i].uid},
                RC_COMBAT_STATE_CANCELLED);
        }
    }
    runec_ui_clear_selected_target(&v->ui);
    v->ui.context_open = 0;
    reset_viewer_context(v);
}

static int viewer_dev_tile_safe(const ViewerState *v, int x, int y,
                                int plane, const ViewerDevTransport *d) {
    if (!v || !v->world || !d || rc_tile_blocked(&v->world->map, x, y, plane))
        return 0;
    if (d->npc_id < 0)
        return 1;
    int size = d->npc_size > 0 ? d->npc_size : 1;
    int max_x = d->target_x + size - 1;
    int max_y = d->target_y + size - 1;
    return x < d->target_x || x > max_x || y < d->target_y || y > max_y;
}

static void viewer_dev_player_tile(const ViewerState *v,
                                   const ViewerDevTransport *d,
                                   int *out_x, int *out_y) {
    int size = d->npc_size > 0 ? d->npc_size : 1;
    int desired_x = d->target_x;
    int desired_y = d->npc_id >= 0 ? d->target_y - (size + 4) : d->target_y;
    int plane = clamp_plane(d->plane);
    int best_x = desired_x;
    int best_y = desired_y;
    int best_score = 0x3fffffff;

    for (int dx = -20; dx <= 20; dx++) {
        for (int dy = -20; dy <= 20; dy++) {
            int x = d->target_x + dx;
            int y = d->target_y + dy;
            if (!viewer_dev_tile_safe(v, x, y, plane, d))
                continue;
            int dist = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
            if (d->npc_id >= 0 && dist < size + 1)
                continue;
            int score = abs(x - desired_x) * 4 + abs(y - desired_y) * 4;
            if (d->npc_id >= 0) {
                score += abs(dist - (size + 4));
                if (!rc_has_los(&v->world->map, x, y,
                                d->target_x, d->target_y, plane))
                    score += 16;
            }
            if (score < best_score) {
                best_score = score;
                best_x = x;
                best_y = y;
            }
        }
    }

    *out_x = best_x;
    *out_y = best_y;
}

static void viewer_dev_transport_to(ViewerState *v,
                                    const ViewerDevTransport *d) {
    if (!v || !v->world || !d)
        return;
    RcPlayer *p = &v->world->player;
    int old_x = p->x;
    int old_y = p->y;
    int old_plane = p->plane;
    int player_x = d->target_x;
    int player_y = d->target_y;
    viewer_dev_player_tile(v, d, &player_x, &player_y);

    viewer_clear_player_activity(v);
    p->x = player_x;
    p->y = player_y;
    p->prev_x = player_x;
    p->prev_y = player_y;
    p->plane = clamp_plane(d->plane);
    p->facing_entity = -1;
    p->facing_x = d->target_x;
    p->facing_y = d->target_y;
    v->prev_player_x = (float)player_x;
    v->prev_player_y = (float)player_y;
    v->tick_acc = 0.0f;
    v->tick_frac = 0.0f;
    v->player_moving = 0;
    v->scene_plane_override = -1;

    if (!reload_scene_around_player(v, p->x, p->y))
        handle_player_scene_transition(v, old_x, old_y, old_plane);
    ensure_active_scene_plane(v, p->plane);
    if (viewer_spawn_focus_npc(v, d))
        reload_npc_models_for_scene(v);
    viewer_start_dev_boss_combat(v, d);
    fprintf(stderr, "dev transport: %s -> player %d,%d,%d target %d,%d,%d\n",
            d->label, p->x, p->y, p->plane,
            d->target_x, d->target_y, d->plane);
}

static void draw_dev_transport_controls(ViewerState *v) {
    if (!v || !v->world || !dev_transport_enabled())
        return;
    Vector2 mouse = GetMousePosition();
    Color panel = (Color){12, 14, 16, 175};
    Color bg = (Color){20, 22, 24, 220};
    Color hover = (Color){64, 70, 76, 240};
    Color border = (Color){170, 145, 82, 245};
    Color text = (Color){240, 225, 180, 255};
    int count = dev_transport_count();
    DrawRectangleRounded((Rectangle){52.0f, 84.0f, 126.0f,
                                     30.0f + (float)count * 25.0f},
                         0.08f, 6, panel);
    DrawText("DEV BOSS", 62, 91, 12, text);
    for (int i = 0; i < count; i++) {
        Rectangle r = dev_transport_button_rect(i);
        Color fill = CheckCollisionPointRec(mouse, r) ? hover : bg;
        DrawRectangleRounded(r, 0.10f, 6, fill);
        DrawRectangleRoundedLines(r, 0.10f, 6, border);
        DrawText(g_dev_transports[i].label, (int)r.x + 8, (int)r.y + 5,
                 12, text);
    }
}

static int handle_dev_transport_buttons(ViewerState *v) {
    if (!v || !v->world || !dev_transport_enabled()
            || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        return 0;
    }
    Vector2 mouse = GetMousePosition();
    for (int i = 0; i < dev_transport_count(); i++) {
        if (CheckCollisionPointRec(mouse, dev_transport_button_rect(i))) {
            viewer_dev_transport_to(v, &g_dev_transports[i]);
            return 1;
        }
    }
    return 0;
}

static Color minimap_tile_color(const ViewerState *v, int wx, int wy) {
    int lx = wx - g_world_origin_x;
    int ly = wy - g_world_origin_y;
    if (!v->minimap_tiles || lx < 0 || ly < 0
            || lx >= v->minimap_tiles_w || ly >= v->minimap_tiles_h)
        return (Color){42, 62, 38, 255};
    return v->minimap_tiles[lx + ly * v->minimap_tiles_w];
}

static void load_world_map_minimap(ViewerState *v) {
    const char *enabled = getenv("RUNEC_MINIMAP_WORLD_MAP");
    if (!enabled || !enabled[0] || strcmp(enabled, "0") == 0)
        return;

    const char *path = env_path("RUNEC_MINIMAP_MAP",
        "tools/cache_pipeline/source/current_fightcaves_demo/data/"
        "map-oldschool-live-en-b236-2026-03-18-11-45-07-openrs2#2499.png");
    if (!rc_asset_exists(path))
        return;

    Image img = runec_load_image_asset(path);
    if (!img.data)
        return;
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    v->world_map_pixels = LoadImageColors(img);
    v->world_map_w = img.width;
    v->world_map_h = img.height;
    UnloadImage(img);
    if (!v->world_map_pixels) {
        v->world_map_w = 0;
        v->world_map_h = 0;
        return;
    }

    v->world_map_min_x = env_int("RUNEC_MINIMAP_MAP_MIN_X", 16 * 64);
    v->world_map_max_y = env_int("RUNEC_MINIMAP_MAP_MAX_Y", (194 + 1) * 64 - 1);
    v->use_world_map_minimap = 1;
    fprintf(stderr, "minimap: loaded world map %s (%dx%d, min_x=%d max_y=%d)\n",
            path, v->world_map_w, v->world_map_h,
            v->world_map_min_x, v->world_map_max_y);
}

static Color world_map_minimap_color(const ViewerState *v, int wx, int wy) {
    if (!v->use_world_map_minimap || !v->world_map_pixels
            || viewer_scene_plane(v) != 0)
        return minimap_tile_color(v, wx, wy);

    int ix = wx - v->world_map_min_x;
    int iy = v->world_map_max_y - wy;
    if (ix < 0 || iy < 0 || ix >= v->world_map_w || iy >= v->world_map_h)
        return minimap_tile_color(v, wx, wy);

    Color c = v->world_map_pixels[ix + iy * v->world_map_w];
    if (c.a == 0)
        return minimap_tile_color(v, wx, wy);
    if ((c.r > 215 && c.g > 215 && c.b > 215)
            || (c.g > 165 && c.r < 95 && c.b < 95)
            || (c.r < 18 && c.g < 24 && c.b < 18)) {
        return minimap_tile_color(v, wx, wy);
    }
    c.a = 255;
    return c;
}

static void route_player_to(ViewerState *v, int tx, int ty) {
    if (v->world->player.running)
        rc_player_run_to(v->world, tx, ty);
    else
        rc_player_walk_to(v->world, tx, ty);
}

static int viewer_tile_in_loaded_scene(int x, int y) {
    return x >= g_world_origin_x && x < g_world_origin_x + g_world_w
        && y >= g_world_origin_y && y < g_world_origin_y + g_world_h;
}

static void handle_player_scene_transition(ViewerState *v, int old_x,
                                           int old_y, int old_plane) {
    if (!v || !v->world)
        return;
    RcPlayer *p = &v->world->player;
    int large_step = abs(p->x - old_x) > 2 || abs(p->y - old_y) > 2;
    if (old_plane != p->plane || large_step) {
        v->scene_plane_override = -1;
        v->prev_player_x = (float)p->x;
        v->prev_player_y = (float)p->y;
        v->tick_frac = 0.0f;
        v->player_moving = 0;
    }

    static int last_warn_x = -1;
    static int last_warn_y = -1;
    if (!viewer_tile_in_loaded_scene(p->x, p->y)
            && (p->x != last_warn_x || p->y != last_warn_y)) {
        if (reload_scene_around_player(v, p->x, p->y)) {
            last_warn_x = -1;
            last_warn_y = -1;
            return;
        }
        last_warn_x = p->x;
        last_warn_y = p->y;
        fprintf(stderr,
                "viewer scene: player at %d,%d,%d outside loaded visual "
                "window %d,%d %dx%d; core traversal/collision state is "
                "valid, visual scene assets need a generated slice for that "
                "destination\n",
                p->x, p->y, p->plane, g_world_origin_x, g_world_origin_y,
                g_world_w, g_world_h);
    }
}

static int ui_equip_slot_to_core(int ui_slot) {
    static const int map[RUNEC_UI_EQUIP_SLOT_COUNT] = {
        EQUIP_HEAD, EQUIP_CAPE, EQUIP_AMULET, EQUIP_WEAPON,
        EQUIP_BODY, EQUIP_SHIELD, EQUIP_AMMO, EQUIP_LEGS,
        -1, EQUIP_GLOVES, EQUIP_BOOTS, -1, EQUIP_RING, EQUIP_AMMO,
    };
    return ui_slot >= 0 && ui_slot < RUNEC_UI_EQUIP_SLOT_COUNT
         ? map[ui_slot] : -1;
}

static int ui_component_group(uint32_t component_id) {
    return (int)((component_id >> 16) & 0xffffu);
}

static int ui_component_child(uint32_t component_id) {
    return (int)(component_id & 0xffffu);
}

static int selected_spell_id_for_viewer(const ViewerState *v) {
    if (!v || !v->world)
        return -1;
    return v->world->player.selected_spell;
}

static int core_equip_slot_to_ui(int core_slot) {
    static const int map[RC_EQUIP_COUNT] = {
        0, 1, 2, 3, 4, 5, 7, 9, 10, 12, 6,
    };
    return core_slot >= 0 && core_slot < RC_EQUIP_COUNT
         ? map[core_slot] : -1;
}

static int item_display_id_for_quantity(const ViewerState *v, int item_id, int quantity);

static void sync_ui_slot(const ViewerState *v, RuneCUiSlot *dst, const RcInvSlot *src) {
    if (!dst || !src || src->item_id < 0 || src->quantity <= 0) {
        if (dst) {
            dst->item_id = 0;
            dst->icon_item_id = 0;
            dst->quantity = 0;
            dst->label[0] = '\0';
            dst->enabled = 0;
        }
        return;
    }
    const RcItemDef *def = rc_item_def_get(src->item_id);
    dst->item_id = (uint32_t)src->item_id;
    dst->icon_item_id = (uint32_t)item_display_id_for_quantity(v, src->item_id,
                                                               src->quantity);
    dst->quantity = src->quantity;
    snprintf(dst->label, sizeof(dst->label), "%.23s",
             def ? def->name : TextFormat("Item %d", src->item_id));
    dst->enabled = 1;
}

static void sync_ui_items(ViewerState *v) {
    const RcPlayer *p = &v->world->player;
    for (int i = 0; i < RUNEC_UI_INV_SLOT_COUNT; i++)
        sync_ui_slot(v, &v->ui.inventory[i], &p->inventory[i]);
    if (v->ui.selected_inventory_slot >= 0
            && !v->ui.inventory[v->ui.selected_inventory_slot].enabled) {
        v->ui.selected_inventory_slot = -1;
    }

    for (int i = 0; i < RUNEC_UI_EQUIP_SLOT_COUNT; i++) {
        v->ui.equipment[i].item_id = 0;
        v->ui.equipment[i].icon_item_id = 0;
        v->ui.equipment[i].quantity = 0;
        v->ui.equipment[i].label[0] = '\0';
        v->ui.equipment[i].enabled = 0;
    }
    for (int core = 0; core < RC_EQUIP_COUNT; core++) {
        int ui_slot = core_equip_slot_to_ui(core);
        if (ui_slot >= 0)
            sync_ui_slot(v, &v->ui.equipment[ui_slot], &p->equipment[core]);
    }
    if (v->ui.selected_equipment_slot >= 0
            && !v->ui.equipment[v->ui.selected_equipment_slot].enabled) {
        v->ui.selected_equipment_slot = -1;
    }
}

static void sync_ui_player_status(ViewerState *v) {
    const RcPlayer *p = &v->world->player;
    RcCombatViewState combat_view;
    memset(&combat_view, 0, sizeof(combat_view));
    rc_combat_get_player_view(v->world, &combat_view);
    v->combat_view = combat_view;
    v->ui.hitpoints = p->current_hp > 0 ? (p->current_hp + 9) / 10 : 0;
    v->ui.hitpoints_max = p->max_hp > 0 ? (p->max_hp + 9) / 10 : 0;
    v->ui.prayer_points = p->current_prayer_points > 0
                         ? (p->current_prayer_points + 9) / 10 : 0;
    v->ui.prayer_points_max = p->skills.base_level[SKILL_PRAYER];
    v->ui.active_prayers = p->active_prayers;
    v->ui.run_energy = p->run_energy / 100;
    v->ui.selected_combat_style = combat_view.selected_style_idx;
    v->ui.auto_retaliate = combat_view.auto_retaliate;
    v->ui.special_attack_enabled = combat_view.special_pending;
    v->ui.special_attack_energy = combat_view.special_energy / 100;
    v->ui.combat_level = rc_combat_level(&p->skills);
    const RcInvSlot *weapon_slot = &p->equipment[EQUIP_WEAPON];
    const RcItemDef *weapon_def = rc_item_def_get(weapon_slot->item_id);
    runec_ui_set_combat_weapon_name(&v->ui,
                                    weapon_def && weapon_def->name[0]
                                        ? weapon_def->name : "Unarmed");
    runec_ui_set_combat_style_profile(&v->ui, combat_view.weapon_category);

    static const int display_to_core_skill[RUNEC_UI_SKILL_COUNT] = {
        SKILL_ATTACK, SKILL_STRENGTH, SKILL_DEFENCE, SKILL_RANGED,
        SKILL_PRAYER, SKILL_MAGIC, SKILL_RUNECRAFT, SKILL_CONSTRUCTION,
        SKILL_HITPOINTS, SKILL_AGILITY, SKILL_HERBLORE, SKILL_THIEVING,
        SKILL_CRAFTING, SKILL_FLETCHING, SKILL_SLAYER, SKILL_HUNTER,
        SKILL_MINING, SKILL_SMITHING, SKILL_FISHING, SKILL_COOKING,
        SKILL_FIREMAKING, SKILL_WOODCUTTING, SKILL_FARMING, -1,
    };
    int total_level = 0;
    for (int i = 0; i < RUNEC_UI_SKILL_COUNT; i++) {
        int base = 1;
        int current = 1;
        int core_skill = display_to_core_skill[i];
        if (core_skill >= 0 && core_skill < SKILL_COUNT) {
            base = p->skills.base_level[core_skill];
            current = p->skills.boosted_level[core_skill];
            if (base <= 0)
                base = 1;
            if (current <= 0)
                current = base;
        }
        v->ui.skill_base[i] = base;
        v->ui.skill_current[i] = current;
        total_level += base;
    }
    v->ui.skill_total = total_level;
}

static void seed_viewer_inventory(RcWorld *world) {
    typedef struct {
        int item_id;
        int quantity;
    } SeedItem;
    static const SeedItem items[] = {
        {6570, 1},     // Fire cape
        {21295, 1},    // Infernal cape
        {4151, 1},     // Abyssal whip
        {11802, 1},    // Armadyl godsword
        {11832, 1},    // Saradomin godsword
        {11834, 1},    // Bandos godsword
        {861, 1},      // Magic shortbow
        {892, 10000},  // Rune arrows
        {1381, 1},     // Staff of fire
        {995, 10000000},
        {556, 100000}, // Air rune
        {555, 100000}, // Water rune
        {557, 100000}, // Earth rune
        {554, 100000}, // Fire rune
        {558, 100000}, // Mind rune
        {559, 100000}, // Body rune
        {564, 100000}, // Cosmic rune
        {562, 100000}, // Chaos rune
        {561, 100000}, // Nature rune
        {563, 100000}, // Law rune
        {560, 100000}, // Death rune
        {565, 100000}, // Blood rune
        {566, 100000}, // Soul rune
        {21880, 100000}, // Wrath rune
        {10350, 1},    // 3rd age full helmet
        {10348, 1},    // 3rd age platebody
        {10346, 1},    // 3rd age platelegs
        {10352, 1},    // 3rd age kiteshield
    };
    for (int i = 0; i < (int)(sizeof(items) / sizeof(items[0])); i++)
        rc_inv_add(world->player.inventory, items[i].item_id, items[i].quantity);
    int fire_blast = rc_spell_find("Fire Blast");
    if (fire_blast >= 0)
        world->player.selected_spell = fire_blast;
    rc_recalc_bonuses(&world->player);
    rc_refresh_player_combat_style(&world->player);
}

static void set_viewer_demo_stats(RcPlayer *p) {
    for (int i = 0; i < SKILL_COUNT; i++) {
        p->skills.base_level[i] = 99;
        p->skills.boosted_level[i] = 99;
        p->skills.xp[i] = 13034431;
    }
    p->current_hp = 990;
    p->max_hp = 990;
    p->current_prayer_points = 990;
}

static int ground_item_at_tile_plane(const ViewerState *v, int x, int y,
                                     int plane) {
    for (int i = 0; i < v->world->ground_item_count; i++) {
        const RcGroundItem *g = &v->world->ground_items[i];
        if (g->active && g->x == x && g->y == y && g->plane == plane) {
            return i;
        }
    }
    return -1;
}

static int ground_item_at_tile(const ViewerState *v, int x, int y) {
    return ground_item_at_tile_plane(v, x, y, viewer_scene_plane(v));
}

static BoundingBox viewer_tile_box(float world_x, float world_y, float width,
                                   float length, float base_y,
                                   float height) {
    float lx0 = world_x - (float)g_world_origin_x;
    float lx1 = lx0 + width;
    float lz0 = -((world_y - (float)g_world_origin_y) + length);
    float lz1 = -(world_y - (float)g_world_origin_y);
    return (BoundingBox){
        .min = {lx0, base_y, lz0},
        .max = {lx1, base_y + height, lz1},
    };
}

static float npc_pick_height(int size) {
    if (size < 1) size = 1;
    return 1.9f + 0.45f * (float)(size - 1);
}

static int pick_npc_at_mouse(ViewerState *v) {
    int best_uid = -1;
    float best_distance = 1000000000.0f;
    int scene_plane = viewer_scene_plane(v);
    Ray ray = GetScreenToWorldRay(GetMousePosition(), v->camera);
    for (int i = 0; i < v->world->npc_count; i++) {
        const RcNpc *n = &v->world->npcs[i];
        if (!n->active || n->is_dead || n->plane != scene_plane)
            continue;
        const RcNpcDef *def = &g_npc_defs[n->def_id];
        int size = def->size > 0 ? def->size : 1;
        float npc_x = v->npc_render[i].initialized
                    ? v->npc_render[i].render_x : (float)n->x;
        float npc_y = v->npc_render[i].initialized
                    ? v->npc_render[i].render_y : (float)n->y;
        float base_y = ground_yf_plane(v, scene_plane, npc_x, npc_y);
        BoundingBox box = viewer_tile_box(npc_x, npc_y, (float)size,
                                          (float)size, base_y,
                                          npc_pick_height(size));
        RayCollision hit = GetRayCollisionBox(ray, box);
        if (hit.hit && hit.distance < best_distance) {
            best_distance = hit.distance;
            best_uid = n->uid;
        }
    }
    return best_uid;
}

static const RcTraversalEdge *viewer_object_traversal_edge(
    int obj_id, int x, int y, int plane, int opt) {
    if (obj_id < 0 || x < 0 || y < 0 || plane < 0 || opt < 0)
        return NULL;
    RcObjectPlacement placement = {0};
    int have_placement = 0;
    RcObjectPlacement rows_at_anchor[32];
    int anchor_count = rc_object_placements_at(x, y, plane, rows_at_anchor,
                                               32);
    for (int i = 0; i < anchor_count; i++) {
        if ((int)rows_at_anchor[i].obj_id != obj_id)
            continue;
        placement = rows_at_anchor[i];
        have_placement = 1;
        break;
    }
    if (g_rc_object_placement_count > 0 && !have_placement)
        return NULL;
    const RcTraversalEdge *exact =
        rc_traversal_find(RC_TRAVERSAL_OBJECT, obj_id, x, y, plane, opt);
    if (exact)
        return exact;
    if (!have_placement)
        return NULL;

    int count = 0;
    const RcTraversalEdge *rows =
        rc_traversal_edges_for(RC_TRAVERSAL_OBJECT, obj_id, &count);
    const RcObjectDef *def = rc_object_def_get(obj_id);
    int w = def && def->width > 0 ? def->width : 1;
    int l = def && def->length > 0 ? def->length : 1;
    if (placement.rotation & 1u) {
        int tmp = w;
        w = l;
        l = tmp;
    }
    int min_x = placement.x;
    int min_y = placement.y;
    int max_x = placement.x + w - 1;
    int max_y = placement.y + l - 1;
    for (int i = 0; rows && i < count; i++) {
        const RcTraversalEdge *row = &rows[i];
        if (row->kind != RC_TRAVERSAL_OBJECT
                || row->source_id != (uint32_t)obj_id)
            continue;
        if ((int)row->option != opt || (int)row->start_plane != plane)
            continue;
        int dx = 0;
        int dy = 0;
        if ((int)row->start_x < min_x) dx = min_x - (int)row->start_x;
        else if ((int)row->start_x > max_x) dx = (int)row->start_x - max_x;
        if ((int)row->start_y < min_y) dy = min_y - (int)row->start_y;
        else if ((int)row->start_y > max_y) dy = (int)row->start_y - max_y;
        if (dx + dy <= 1)
            return row;
    }
    return NULL;
}

static int object_action_option_available(const ViewerPickedObject *object,
                                          int opt) {
    if (!object || opt < 0 || opt >= RC_OBJECT_ACTIONS)
        return 0;
    const RcObjectDef *def = rc_object_def_get(object->obj_id);
    if (def && def->actions[opt][0])
        return 1;
    const RcObjectBehavior *behavior = rc_object_behavior_get(object->obj_id);
    if (behavior && (behavior->action_mask & (1u << opt)))
        return 1;
    return viewer_object_traversal_edge(object->obj_id, object->x, object->y,
                                        object->plane, opt) != NULL;
}

static const char *object_action_label(const ViewerPickedObject *object,
                                       const RcObjectDef *def, int opt) {
    if (def && opt >= 0 && opt < RC_OBJECT_ACTIONS && def->actions[opt][0])
        return def->actions[opt];
    const RcTraversalEdge *edge = object ? viewer_object_traversal_edge(
        object->obj_id, object->x, object->y, object->plane, opt) : NULL;
    return edge && edge->action[0] ? edge->action : "";
}

static int object_first_action_option(ViewerPickedObject object) {
    const RcObjectDef *def = rc_object_def_get(object.obj_id);
    if (!def)
        return -1;
    for (int i = 0; i < RC_OBJECT_ACTIONS; i++) {
        if (object_action_option_available(&object, i)
                && object_action_label(&object, def, i)[0])
            return i;
    }
    return -1;
}

static int object_first_pick_option(ViewerPickedObject object) {
    const RcObjectDef *def = rc_object_def_get(object.obj_id);
    if (!def)
        return -1;
    const RcObjectBehavior *behavior = rc_object_behavior_get(object.obj_id);
    for (int i = 0; i < RC_OBJECT_ACTIONS; i++) {
        if (def->actions[i][0]
                || (behavior && (behavior->action_mask & (1u << i)))) {
            return i;
        }
    }
    return -1;
}

static int object_footprint_contains(const RcObjectPlacement *placement,
                                     int obj_id, int tile_x, int tile_y,
                                     int *out_w, int *out_l) {
    const RcObjectDef *def = rc_object_def_get(obj_id);
    int w = def && def->width > 0 ? def->width : 1;
    int l = def && def->length > 0 ? def->length : 1;
    if (placement->rotation & 1u) {
        int tmp = w;
        w = l;
        l = tmp;
    }
    if (out_w) *out_w = w;
    if (out_l) *out_l = l;
    return tile_x >= placement->x && tile_x < placement->x + w
        && tile_y >= placement->y && tile_y < placement->y + l;
}

static float object_pick_height(const RcObjectPlacement *placement,
                                const RcObjectDef *def, int w, int l) {
    int max_dim = w > l ? w : l;
    if (max_dim < 1) max_dim = 1;
    if (placement->type == 22)
        return 0.75f;
    if (placement->type >= 0 && placement->type <= 3)
        return 2.25f;
    if (placement->type >= 4 && placement->type <= 8)
        return 2.75f;
    if (def && def->animation_id >= 0)
        return 3.25f + 0.25f * (float)(max_dim - 1);
    return 2.4f + 0.35f * (float)(max_dim - 1);
}

static int object_pick_candidate(ViewerState *v, const RcObjectPlacement *row,
                                 int tile_x, int tile_y, int has_tile,
                                 Ray ray, ViewerPickedObject *out,
                                 float *out_score) {
    if (!v || !row || !out || !out_score)
        return 0;
    int scene_plane = viewer_scene_plane(v);
    if (row->plane != scene_plane || !viewer_tile_in_loaded_scene(row->x, row->y))
        return 0;

    RcObjectPlacement pick_row = *row;
    int obj_id = (int)row->obj_id;
    RcObjectState active_state;
    if (v->world && v->world->object_state_count > 0
            && rc_world_object_active_state(v->world, obj_id, row->x, row->y,
                                            row->plane, &active_state)) {
        int active_id = active_state.active_obj_id;
        if (active_id != obj_id && rc_object_def_get(active_id))
            obj_id = active_id;
        pick_row.obj_id = (uint32_t)obj_id;
        pick_row.x = (uint16_t)active_state.active_x;
        pick_row.y = (uint16_t)active_state.active_y;
        pick_row.plane = (uint8_t)active_state.active_plane;
        pick_row.type = active_state.active_type;
        pick_row.rotation = active_state.active_rotation;
    }
    if (pick_row.plane != scene_plane
            || !viewer_tile_in_loaded_scene(pick_row.x, pick_row.y))
        return 0;

    int w = 1, l = 1;
    int contains = has_tile
        ? object_footprint_contains(&pick_row, obj_id, tile_x, tile_y, &w, &l)
        : object_footprint_contains(&pick_row, obj_id, pick_row.x, pick_row.y,
                                    &w, &l);
    const RcObjectDef *def = rc_object_def_get(obj_id);
    ViewerPickedObject candidate = {
        .obj_id = obj_id,
        .x = pick_row.x,
        .y = pick_row.y,
        .plane = pick_row.plane,
        .width = w,
        .length = l,
    };
    int option = object_first_pick_option(candidate);
    if (!def || (option < 0 && !def->name[0]))
        return 0;

    float base_y = ground_yf_plane(v, scene_plane, (float)pick_row.x,
                                   (float)pick_row.y);
    BoundingBox box = viewer_tile_box((float)pick_row.x, (float)pick_row.y,
                                      (float)w, (float)l, base_y,
                                      object_pick_height(&pick_row, def, w, l));
    RayCollision hit = GetRayCollisionBox(ray, box);
    if (!contains && !hit.hit)
        return 0;

    float score = hit.hit ? hit.distance : 100000.0f;
    if (contains)
        score -= 0.75f;
    if (option >= 0)
        score -= 0.25f;
    const RcObjectBehavior *behavior = rc_object_behavior_get(obj_id);
    if (behavior && (behavior->flags & RC_OBJ_BEHAVIOR_TRANSPORT))
        score -= 0.25f;
    *out = candidate;
    *out_score = score;
    return 1;
}

static int pick_object_from_regions(ViewerState *v, ViewerPickedObject *out,
                                    int has_tile, int tile_x, int tile_y,
                                    int min_rx, int min_ry,
                                    int max_rx, int max_ry,
                                    int anchor_min_x, int anchor_min_y,
                                    int anchor_max_x, int anchor_max_y) {
    if (!v || !out)
        return 0;
    ViewerPickedObject best = {0};
    float best_score = 1000000000.0f;
    Ray ray = GetScreenToWorldRay(GetMousePosition(), v->camera);
    for (int rx = min_rx; rx <= max_rx; rx++) {
        for (int ry = min_ry; ry <= max_ry; ry++) {
            int count = 0;
            uint16_t ms = (uint16_t)((rx << 8) | ry);
            const RcObjectPlacement *rows =
                rc_object_region_placements(ms, &count);
            for (int i = 0; rows && i < count; i++) {
                if ((int)rows[i].x < anchor_min_x
                        || (int)rows[i].x > anchor_max_x
                        || (int)rows[i].y < anchor_min_y
                        || (int)rows[i].y > anchor_max_y) {
                    continue;
                }
                ViewerPickedObject candidate;
                float score = 0.0f;
                if (!object_pick_candidate(v, &rows[i], tile_x, tile_y,
                                           has_tile, ray, &candidate, &score))
                    continue;
                if (score < best_score) {
                    best_score = score;
                    best = candidate;
                }
            }
        }
    }

    if (best_score >= 1000000000.0f)
        return 0;
    *out = best;
    return 1;
}

static int pick_object_near_tile(ViewerState *v, ViewerPickedObject *out,
                                 int tile_x, int tile_y) {
    int min_x = tile_x - OBJECT_PICK_TILE_RADIUS;
    int min_y = tile_y - OBJECT_PICK_TILE_RADIUS;
    int max_x = tile_x + OBJECT_PICK_TILE_RADIUS;
    int max_y = tile_y + OBJECT_PICK_TILE_RADIUS;
    int min_rx = min_x >> 6;
    int min_ry = min_y >> 6;
    int max_rx = max_x >> 6;
    int max_ry = max_y >> 6;
    return pick_object_from_regions(v, out, 1, tile_x, tile_y,
                                    min_rx, min_ry, max_rx, max_ry,
                                    min_x, min_y, max_x, max_y);
}

static int pick_object_at_mouse(ViewerState *v, ViewerPickedObject *out) {
    if (!v || !out)
        return 0;
    int tile_x = -1, tile_y = -1;
    int has_tile = raycast_tile(v, &tile_x, &tile_y);
    if (has_tile)
        return pick_object_near_tile(v, out, tile_x, tile_y);
    int min_rx = g_world_origin_x >> 6;
    int min_ry = g_world_origin_y >> 6;
    int max_rx = (g_world_origin_x + g_world_w - 1) >> 6;
    int max_ry = (g_world_origin_y + g_world_h - 1) >> 6;
    return pick_object_from_regions(v, out, has_tile, tile_x, tile_y,
                                    min_rx, min_ry, max_rx, max_ry,
                                    g_world_origin_x, g_world_origin_y,
                                    g_world_origin_x + g_world_w - 1,
                                    g_world_origin_y + g_world_h - 1);
}

static int pick_object_at_mouse_tile(ViewerState *v, ViewerPickedObject *out) {
    if (!v || !out)
        return 0;
    int tile_x = -1, tile_y = -1;
    if (!raycast_tile(v, &tile_x, &tile_y))
        return 0;
    return pick_object_near_tile(v, out, tile_x, tile_y);
}

static RcNpc *viewer_find_npc_by_uid(ViewerState *v, int npc_uid) {
    if (!v || !v->world)
        return NULL;
    for (int i = 0; i < v->world->npc_count; i++) {
        RcNpc *npc = &v->world->npcs[i];
        if (npc->active && npc->uid == npc_uid)
            return npc;
    }
    return NULL;
}

static void reset_viewer_context(ViewerState *v) {
    v->context_kind = VIEWER_CONTEXT_NONE;
    v->context_npc_uid = -1;
    v->context_object = (ViewerPickedObject){0};
    for (int i = 0; i < RUNEC_UI_CONTEXT_ACTIONS; i++)
        v->context_action_option[i] = VIEWER_CONTEXT_CANCEL;
}

static void open_npc_context_menu(ViewerState *v, int npc_uid) {
    RcNpc *npc = viewer_find_npc_by_uid(v, npc_uid);
    if (!npc || npc->def_id < 0 || npc->def_id >= g_npc_def_count)
        return;
    const RcNpcDef *def = &g_npc_defs[npc->def_id];
    const char *actions[RUNEC_UI_CONTEXT_ACTIONS];
    char action_text[RUNEC_UI_CONTEXT_ACTIONS][32];
    int action_options[RUNEC_UI_CONTEXT_ACTIONS];
    int count = 0;

    for (int i = 0; i < RC_NPC_OPTION_COUNT && count < RUNEC_UI_CONTEXT_ACTIONS - 2; i++) {
        const char *option = rc_npc_def_option(def, i);
        if (!option || !option[0])
            continue;
        snprintf(action_text[count], sizeof(action_text[count]), "%s %.22s",
                 option, def->name);
        actions[count] = action_text[count];
        action_options[count] = i;
        count++;
    }
    if (count < RUNEC_UI_CONTEXT_ACTIONS - 1) {
        snprintf(action_text[count], sizeof(action_text[count]), "Walk here");
        actions[count] = action_text[count];
        action_options[count] = VIEWER_CONTEXT_WALK_HERE;
        count++;
    }
    if (count < RUNEC_UI_CONTEXT_ACTIONS - 1) {
        snprintf(action_text[count], sizeof(action_text[count]), "Examine %.22s",
                 def->name);
        actions[count] = action_text[count];
        action_options[count] = VIEWER_CONTEXT_EXAMINE;
        count++;
    }
    snprintf(action_text[count], sizeof(action_text[count]), "Cancel");
    actions[count] = action_text[count];
    action_options[count] = VIEWER_CONTEXT_CANCEL;
    count++;

    reset_viewer_context(v);
    v->context_kind = VIEWER_CONTEXT_NPC;
    v->context_npc_uid = npc_uid;
    for (int i = 0; i < count; i++)
        v->context_action_option[i] = action_options[i];
    runec_ui_open_context(&v->ui, GetMousePosition(), "Choose Option",
                          actions, count);
}

static void open_object_context_menu(ViewerState *v,
                                     ViewerPickedObject object) {
    const RcObjectDef *def = rc_object_def_get(object.obj_id);
    if (!def)
        return;

    const char *actions[RUNEC_UI_CONTEXT_ACTIONS];
    char action_text[RUNEC_UI_CONTEXT_ACTIONS][80];
    int action_options[RUNEC_UI_CONTEXT_ACTIONS];
    int count = 0;

    for (int i = 0; i < RC_OBJECT_ACTIONS
            && count < RUNEC_UI_CONTEXT_ACTIONS - 2; i++) {
        const char *label = object_action_label(&object, def, i);
        if (!object_action_option_available(&object, i) || !label[0])
            continue;
        snprintf(action_text[count], sizeof(action_text[count]), "%s %.24s",
                 label, def->name);
        actions[count] = action_text[count];
        action_options[count] = i;
        count++;
    }
    if (count < RUNEC_UI_CONTEXT_ACTIONS - 1) {
        snprintf(action_text[count], sizeof(action_text[count]), "Walk here");
        actions[count] = action_text[count];
        action_options[count] = VIEWER_CONTEXT_WALK_HERE;
        count++;
    }
    if (count < RUNEC_UI_CONTEXT_ACTIONS - 1) {
        snprintf(action_text[count], sizeof(action_text[count]), "Examine %.24s",
                 def->name);
        actions[count] = action_text[count];
        action_options[count] = VIEWER_CONTEXT_EXAMINE;
        count++;
    }
    snprintf(action_text[count], sizeof(action_text[count]), "Cancel");
    actions[count] = action_text[count];
    action_options[count] = VIEWER_CONTEXT_CANCEL;
    count++;

    reset_viewer_context(v);
    v->context_kind = VIEWER_CONTEXT_OBJECT;
    v->context_object = object;
    for (int i = 0; i < count; i++)
        v->context_action_option[i] = action_options[i];
    runec_ui_open_context(&v->ui, GetMousePosition(), "Choose Option",
                          actions, count);
}

static void handle_context_intent(ViewerState *v) {
    if (v->ui.last_intent.kind != RUNEC_UI_INTENT_CONTEXT_ACTION)
        return;
    int action_idx = v->ui.last_intent.primary;
    if (action_idx < 0 || action_idx >= RUNEC_UI_CONTEXT_ACTIONS) {
        reset_viewer_context(v);
        return;
    }
    if (v->context_kind == VIEWER_CONTEXT_NPC) {
        RcNpc *npc = viewer_find_npc_by_uid(v, v->context_npc_uid);
        int option = v->context_action_option[action_idx];
        if (npc && option >= 0) {
            rc_player_interact_npc(v->world, v->context_npc_uid, option);
        } else if (npc && option == VIEWER_CONTEXT_WALK_HERE) {
            route_player_to(v, npc->x, npc->y);
        }
    } else if (v->context_kind == VIEWER_CONTEXT_OBJECT) {
        ViewerPickedObject object = v->context_object;
        int option = v->context_action_option[action_idx];
        if (option >= 0) {
            rc_player_interact_object_at(v->world, object.obj_id, object.x,
                                         object.y, object.plane, option);
        } else if (option == VIEWER_CONTEXT_WALK_HERE) {
            route_player_to(v, object.x, object.y);
        }
    }
    reset_viewer_context(v);
}

static void pickup_current_tile(ViewerState *v) {
    RcPlayer *p = &v->world->player;
    int idx = ground_item_at_tile_plane(v, p->x, p->y, p->plane);
    if (idx >= 0)
        rc_player_pickup_item(v->world, idx);
}

static int coin_stack_model_item_id(int quantity) {
    if (quantity <= 1) return 995;
    if (quantity == 2) return 996;
    if (quantity == 3) return 997;
    if (quantity == 4) return 998;
    if (quantity < 25) return 999;
    if (quantity < 100) return 1000;
    if (quantity < 250) return 1001;
    if (quantity < 1000) return 1002;
    if (quantity < 10000) return 1003;
    return 1004;
}

static int item_display_id_for_quantity(const ViewerState *v, int item_id, int quantity) {
    if (item_id <= 0)
        return 0;
    if (quantity <= 1)
        return item_id;

    int best_threshold = 0;
    int best_variant_id = item_id;
    if (v) {
        for (int i = 0; i < v->item_stack_variant_count; i++) {
            const ItemStackVariant *variant = &v->item_stack_variants[i];
            if (variant->base_id == item_id && quantity >= variant->threshold
                    && variant->threshold > best_threshold) {
                best_threshold = variant->threshold;
                best_variant_id = variant->variant_id;
            }
        }
    }

    if (best_variant_id != item_id)
        return best_variant_id;
    if (item_id == 995)
        return coin_stack_model_item_id(quantity);
    return item_id;
}

static float ground_item_scale(int item_id, int quantity) {
    (void)item_id;
    (void)quantity;
    return 0.65f;
}

static void begin_one_sided_model_draw(void) {
    rlSetCullFace(RL_CULL_FACE_BACK);
    rlEnableBackfaceCulling();
}

static void end_one_sided_model_draw(void) {
    rlDisableBackfaceCulling();
}

static int draw_item_model(ViewerState *v, uint32_t model_id, Vector3 pos,
                           float facing_angle, float scale, Color tint) {
    if (!v->item_models || !v->item_models->loaded
            || model_id == RUNEC_RENDER_MODEL_MISSING)
        return 0;
    ModelEntry *entry = model_find(v->item_models, model_id);
    if (!entry || !entry->loaded)
        return 0;
    begin_one_sided_model_draw();
    DrawModelEx(entry->model, pos, (Vector3){0, 1, 0}, facing_angle,
                (Vector3){scale, scale, scale}, tint);
    end_one_sided_model_draw();
    return 1;
}

static int ui_item_icon_cached(const RuneCUiState *ui, uint32_t icon_item_id) {
    for (int i = 0; i < ui->item_icon_count; i++) {
        if (ui->item_icons[i].ready && ui->item_icons[i].item_id == icon_item_id)
            return 1;
    }
    return 0;
}

static Texture2D load_item_icon_texture_from_file(int icon_item_id) {
    Texture2D empty = {0};
    char path[256];
    snprintf(path, sizeof(path), "data/sprites/items/item_%d.png", icon_item_id);
    if (!rc_asset_exists(path))
        return empty;

    Texture2D tex = runec_load_texture_asset(path);
    if (tex.id != 0)
        SetTextureFilter(tex, TEXTURE_FILTER_POINT);
    return tex;
}

static Texture2D render_item_icon_texture(ModelEntry *entry) {
    Texture2D empty = {0};
    if (!entry || !entry->loaded)
        return empty;

    const int icon_size = 40;
    RenderTexture2D target = LoadRenderTexture(icon_size, icon_size);
    if (target.id == 0)
        return empty;

    BoundingBox bb = GetModelBoundingBox(entry->model);
    Vector3 center = {
        (bb.min.x + bb.max.x) * 0.5f,
        (bb.min.y + bb.max.y) * 0.5f,
        (bb.min.z + bb.max.z) * 0.5f
    };
    float extent_x = bb.max.x - bb.min.x;
    float extent_y = bb.max.y - bb.min.y;
    float extent_z = bb.max.z - bb.min.z;
    float max_extent = fmaxf(extent_x, fmaxf(extent_y, extent_z));
    if (max_extent < 0.01f)
        max_extent = 1.0f;

    Camera3D cam = {0};
    cam.target = center;
    cam.position = (Vector3){
        center.x + max_extent * 1.5f,
        center.y + max_extent * 1.0f,
        center.z + max_extent * 2.2f
    };
    cam.up = (Vector3){0, 1, 0};
    cam.projection = CAMERA_ORTHOGRAPHIC;
    cam.fovy = max_extent * 1.85f;

    BeginTextureMode(target);
    ClearBackground(BLANK);
    BeginMode3D(cam);
    DrawModel(entry->model, (Vector3){0, 0, 0}, 1.0f, WHITE);
    EndMode3D();
    EndTextureMode();

    Image img = LoadImageFromTexture(target.texture);
    Texture2D tex = {0};
    if (img.data) {
        ImageFlipVertical(&img);
        tex = LoadTextureFromImage(img);
        if (tex.id != 0)
            SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
        UnloadImage(img);
    }
    UnloadRenderTexture(target);
    return tex;
}

static void build_ui_icon_for_item(ViewerState *v, int item_id, int quantity) {
    int icon_item_id = item_display_id_for_quantity(v, item_id, quantity);
    if (icon_item_id <= 0 || ui_item_icon_cached(&v->ui, (uint32_t)icon_item_id))
        return;

    Texture2D icon = load_item_icon_texture_from_file(icon_item_id);
    if (icon.id != 0) {
        runec_ui_set_item_icon(&v->ui, (uint32_t)icon_item_id, icon);
        return;
    }

    const char *model_icon_env = getenv("RUNEC_UI_MODEL_ITEM_ICONS");
    int allow_model_fallback = model_icon_env && model_icon_env[0]
        && strcmp(model_icon_env, "0") != 0;
    if (!allow_model_fallback)
        return;

    if (!v->item_models || !v->item_models->loaded || !v->item_render_map.loaded)
        return;

    uint32_t render_item_id = (uint32_t)icon_item_id;

    const RuneCItemRenderRecord *rec =
        runec_item_render_find(&v->item_render_map, render_item_id);
    if (!rec)
        rec = runec_item_render_find(&v->item_render_map, (uint32_t)item_id);

    uint32_t model_id = RUNEC_RENDER_MODEL_MISSING;
    if (rec && rec->ground_model_id != RUNEC_RENDER_MODEL_MISSING) {
        model_id = rec->ground_model_id;
    } else {
        const RcItemDef *def = rc_item_def_get(item_id);
        if (def && def->ground_model_id >= 0)
            model_id = (uint32_t)def->ground_model_id;
    }
    if (model_id == RUNEC_RENDER_MODEL_MISSING)
        return;

    ModelEntry *entry = model_find(v->item_models, model_id);
    if (!entry || !entry->loaded)
        return;

    Texture2D tex = render_item_icon_texture(entry);
    if (tex.id != 0)
        runec_ui_set_item_icon(&v->ui, (uint32_t)icon_item_id, tex);
}

static void build_ui_item_icons(ViewerState *v) {
    int before_count = v->ui.item_icon_count;
    RcPlayer *p = &v->world->player;
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        if (p->inventory[i].item_id >= 0 && p->inventory[i].quantity > 0)
            build_ui_icon_for_item(v, p->inventory[i].item_id,
                                   p->inventory[i].quantity);
    }
    for (int i = 0; i < RC_EQUIP_COUNT; i++) {
        if (p->equipment[i].item_id >= 0 && p->equipment[i].quantity > 0)
            build_ui_icon_for_item(v, p->equipment[i].item_id,
                                   p->equipment[i].quantity);
    }
    if (v->ui.item_icon_count != before_count)
        fprintf(stderr, "ui_icons: cached %d item icons\n", v->ui.item_icon_count);
}

static void create_model_anim_states(ModelSet *models,
                                     AnimModelState ***states_out,
                                     int *state_count_out) {
    if (!models || !models->loaded || models->count <= 0
            || !states_out || !state_count_out)
        return;
    *state_count_out = models->count;
    *states_out = calloc((size_t)*state_count_out,
                         sizeof(AnimModelState *));
    if (!*states_out) {
        *state_count_out = 0;
        return;
    }
    for (int i = 0; i < models->count; i++) {
        ModelEntry *entry = &models->entries[i];
        if (!entry->loaded || !entry->vertex_skins || entry->base_vert_count <= 0)
            continue;
        (*states_out)[i] = anim_model_state_create(
            entry->vertex_skins, entry->base_vert_count);
    }
}

static void free_model_anim_states(AnimModelState ***states,
                                   int *state_count) {
    if (!states || !*states)
        return;
    for (int i = 0; i < *state_count; i++)
        anim_model_state_free((*states)[i]);
    free(*states);
    *states = NULL;
    *state_count = 0;
}

static void create_item_anim_states(ViewerState *v) {
    create_model_anim_states(v->item_models, &v->item_anim_states,
                             &v->item_anim_state_count);
}

static void free_item_anim_states(ViewerState *v) {
    free_model_anim_states(&v->item_anim_states, &v->item_anim_state_count);
}

static void create_projectile_anim_states(ViewerState *v) {
    create_model_anim_states(v->projectile_models, &v->projectile_anim_states,
                             &v->projectile_anim_state_count);
}

static void free_projectile_anim_states(ViewerState *v) {
    free_model_anim_states(&v->projectile_anim_states,
                           &v->projectile_anim_state_count);
}

static void free_object_anim_plane(ViewerState *v, int plane) {
    if (!v || plane < 0 || plane >= RC_MAX_PLANES) return;
    free_model_anim_states(&v->object_anim_states[plane],
                           &v->object_anim_state_count[plane]);
    models_free(v->object_anim_model_planes[plane]);
    v->object_anim_model_planes[plane] = NULL;
}

static void create_object_anim_plane_states(ViewerState *v, int plane) {
    if (!v || plane < 0 || plane >= RC_MAX_PLANES) return;
    ObjectMesh *om = v->object_planes[plane];
    ModelSet *models = v->object_anim_model_planes[plane];
    if (!om || !models || !models->loaded || om->object_anim_count <= 0)
        return;
    v->object_anim_state_count[plane] = om->object_anim_count;
    v->object_anim_states[plane] = calloc(
        (size_t)om->object_anim_count, sizeof(AnimModelState *));
    if (!v->object_anim_states[plane]) {
        v->object_anim_state_count[plane] = 0;
        return;
    }
    for (int i = 0; i < om->object_anim_count; i++) {
        ModelEntry *entry = model_find(models, om->object_anims[i].model_id);
        if (!entry || !entry->loaded || !entry->vertex_skins
                || entry->base_vert_count <= 0)
            continue;
        v->object_anim_states[plane][i] = anim_model_state_create(
            entry->vertex_skins, entry->base_vert_count);
    }
}

static AnimModelState *model_anim_state_for_entry(ModelSet *models,
                                                  AnimModelState **states,
                                                  int state_count,
                                                  ModelEntry *entry) {
    if (!models || !entry || !states)
        return NULL;
    ptrdiff_t idx = entry - models->entries;
    if (idx < 0 || idx >= state_count)
        return NULL;
    return states[idx];
}

static AnimModelState *item_anim_state_for_entry(ViewerState *v,
                                                 ModelEntry *entry) {
    return model_anim_state_for_entry(v->item_models, v->item_anim_states,
                                      v->item_anim_state_count, entry);
}

static AnimModelState *projectile_anim_state_for_entry(ViewerState *v,
                                                       ModelEntry *entry) {
    return model_anim_state_for_entry(v->projectile_models,
                                      v->projectile_anim_states,
                                      v->projectile_anim_state_count, entry);
}

static void animate_model_entry_to_player_frame(ViewerState *v,
                                                ModelEntry *entry,
                                                AnimModelState *state) {
    if (!v->anims || !entry || !entry->loaded)
        return;
    if (!state)
        return;
    AnimSequence *seq = anim_get_sequence(v->anims, (uint16_t)v->cur_anim_id);
    if (!seq || seq->frame_count == 0)
        return;
    AnimSequenceFrame *sf = &seq->frames[v->anim_frame_idx % seq->frame_count];
    AnimFrameBase *fb = anim_get_framebase(v->anims, sf->frame.framebase_id);
    if (!fb)
        return;
    anim_apply_frame(state, entry->base_verts, &sf->frame, fb);
    anim_update_mesh(entry->model.meshes[0].vertices, state,
                     entry->face_indices, entry->face_priorities,
                     entry->face_count);
    float *mv = entry->model.meshes[0].vertices;
    int vc = entry->model.meshes[0].vertexCount;
    for (int i = 0; i < vc; i++) {
        mv[i*3]   /=  128.0f;
        mv[i*3+1] /=  128.0f;
        mv[i*3+2] /= -128.0f;
    }
    UpdateMeshBuffer(entry->model.meshes[0], 0, mv, vc * 3 * sizeof(float), 0);
    models_recompute_texture_uvs_from_vertices(entry, state->verts);
}

static int animate_model_entry_sequence(ModelEntry *entry,
                                        AnimModelState *state,
                                        AnimCache *cache,
                                        int anim_id,
                                        float client_ticks) {
    if (!entry || !entry->loaded || !state || !cache || anim_id < 0
            || anim_id > 0xFFFF)
        return 0;
    AnimSequence *seq = anim_get_sequence(cache, (uint16_t)anim_id);
    if (!seq || seq->frame_count == 0)
        return 0;

    int frame_idx = 0;
    float remaining = client_ticks;
    int total_delay = 0;
    for (int i = 0; i < seq->frame_count; i++)
        total_delay += seq->frames[i].delay > 0 ? seq->frames[i].delay : 1;
    if (total_delay > 0) {
        remaining = fmodf(remaining, (float)total_delay);
        for (int i = 0; i < seq->frame_count; i++) {
            int delay = seq->frames[i].delay > 0 ? seq->frames[i].delay : 1;
            if (remaining < (float)delay) {
                frame_idx = i;
                break;
            }
            remaining -= (float)delay;
        }
    }

    AnimSequenceFrame *sf = &seq->frames[frame_idx];
    AnimFrameBase *fb = anim_get_framebase(cache, sf->frame.framebase_id);
    if (!fb)
        return 0;

    anim_apply_frame(state, entry->base_verts, &sf->frame, fb);
    anim_update_mesh(entry->model.meshes[0].vertices, state,
                     entry->face_indices, entry->face_priorities,
                     entry->face_count);
    float *mv = entry->model.meshes[0].vertices;
    int vc = entry->model.meshes[0].vertexCount;
    for (int i = 0; i < vc; i++) {
        mv[i*3]   /=  128.0f;
        mv[i*3+1] /=  128.0f;
        mv[i*3+2] /= -128.0f;
    }
    UpdateMeshBuffer(entry->model.meshes[0], 0, mv,
                     vc * 3 * sizeof(float), 0);
    models_recompute_texture_uvs_from_vertices(entry, state->verts);
    return 1;
}

static void draw_animated_objects(ViewerState *v, int scene_plane,
                                  ObjectMesh *objects) {
    if (!v || !objects || objects->object_anim_count <= 0)
        return;
    scene_plane = clamp_plane(scene_plane);
    ModelSet *models = v->object_anim_model_planes[scene_plane];
    AnimModelState **states = v->object_anim_states[scene_plane];
    int state_count = v->object_anim_state_count[scene_plane];
    if (!models || !models->loaded || !states)
        return;

    float client_ticks = ((float)(v->world ? v->world->tick : 0)
                       + v->tick_frac) * 30.0f;
    for (int i = 0; i < objects->object_anim_count; i++) {
        ObjectAnimRow *row = &objects->object_anims[i];
        if (row->pad & (OANM_FLAG_DYNAMIC_BASE | OANM_FLAG_DYNAMIC_REPLACEMENT)) {
            RcObjectState active;
            int have_state = v->world && rc_world_object_active_state(
                v->world, (int)row->obj_id, row->world_x, row->world_y,
                row->plane, &active);
            if (row->pad & OANM_FLAG_DYNAMIC_REPLACEMENT) {
                if (!have_state || active.active_obj_id != (int)row->obj_id
                        || active.active_x != row->world_x
                        || active.active_y != row->world_y
                        || active.active_plane != row->plane) {
                    continue;
                }
            } else if (have_state
                    && (active.active_obj_id != (int)row->obj_id
                        || active.active_x != row->world_x
                        || active.active_y != row->world_y
                        || active.active_plane != row->plane)) {
                continue;
            }
        }
        ModelEntry *entry = model_find(models, row->model_id);
        if (!entry || !entry->loaded || i >= state_count)
            continue;
        int anim_id = row->animation_id;
        if (v->world) {
            RcObjectState active;
            if (rc_world_object_active_state(v->world, (int)row->obj_id,
                                             row->world_x, row->world_y,
                                             row->plane, &active)
                    && active.animation_timer > 0
                    && active.animation_id >= 0) {
                anim_id = active.animation_id;
            }
        }
        if (states[i]) {
            if (!animate_model_entry_sequence(entry, states[i], v->anims,
                                              anim_id,
                                              client_ticks + row->phase_ticks)) {
                reset_model_entry_to_base_pose(entry);
            }
        } else {
            reset_model_entry_to_base_pose(entry);
        }
        Vector3 pos = {
            row->pos_x - (float)g_world_origin_x,
            row->pos_y,
            row->pos_z + (float)g_world_origin_y,
        };
        DrawModel(entry->model, pos, 1.0f, WHITE);
    }
}

static void animate_item_entry_to_player_frame(ViewerState *v,
                                               ModelEntry *entry) {
    animate_model_entry_to_player_frame(v, entry,
                                        item_anim_state_for_entry(v, entry));
}

static uint32_t composed_player_hide_mask(ViewerState *v, const RcPlayer *p);

static void free_generated_model_entry(ModelEntry *entry) {
    if (!entry || !entry->loaded)
        return;
    UnloadModel(entry->model);
    free(entry->rest_verts);
    free(entry->rest_texcoords);
    free(entry->base_verts);
    free(entry->vertex_skins);
    free(entry->face_indices);
    free(entry->face_priorities);
    free(entry->face_uvs);
    memset(entry, 0, sizeof(*entry));
}

static void clear_composed_player_model(ViewerState *v) {
    free_generated_model_entry(&v->composed_player_model);
    anim_model_state_free(v->composed_anim_state);
    v->composed_anim_state = NULL;
    v->composed_player_loaded = 0;
    v->composed_player_signature = 0;
}

static uint64_t mix_composed_signature(uint64_t sig, uint64_t value) {
    sig ^= value + 0x9e3779b97f4a7c15ULL + (sig << 6) + (sig >> 2);
    return sig;
}

static uint64_t composed_player_signature(ViewerState *v, const RcPlayer *p) {
    uint64_t sig = 0xcbf29ce484222325ULL;
    uint32_t hide = composed_player_hide_mask(v, p);
    sig = mix_composed_signature(sig, hide);
    for (int part = 0; part < RUNEC_RENDER_BODY_PART_COUNT; part++) {
        uint32_t body_bit = 1u << part;
        uint32_t model_id = v->item_render_map.body_model_ids[part];
        if ((hide & body_bit) || model_id == RUNEC_RENDER_MODEL_MISSING)
            continue;
        sig = mix_composed_signature(sig, ((uint64_t)part << 32) | model_id);
    }
    for (int slot = 0; slot < RC_EQUIP_COUNT; slot++) {
        const RcInvSlot *eq = &p->equipment[slot];
        if (eq->item_id < 0 || eq->quantity <= 0)
            continue;
        const RuneCItemRenderRecord *rec = runec_item_render_find(
            &v->item_render_map, (uint32_t)eq->item_id);
        if (!rec || rec->male_model_id == RUNEC_RENDER_MODEL_MISSING)
            continue;
        sig = mix_composed_signature(sig,
            ((uint64_t)(slot + 64) << 32) | (uint64_t)rec->male_model_id);
    }
    return sig;
}

static int append_composed_source(ModelEntry **sources, int count, int max_count,
                                  ModelSet *set, uint32_t model_id) {
    if (!set || !set->loaded || model_id == RUNEC_RENDER_MODEL_MISSING)
        return count;
    ModelEntry *entry = model_find(set, model_id);
    if (!entry || !entry->loaded)
        return count;
    if (count >= max_count)
        return count;
    sources[count++] = entry;
    return count;
}

static void recalc_expanded_normals(float *verts, float *normals, int face_count) {
    if (!verts || !normals)
        return;
    for (int i = 0; i < face_count; i++) {
        int i0 = i * 3, i1 = i * 3 + 1, i2 = i * 3 + 2;
        float ax = verts[i1*3] - verts[i0*3];
        float ay = verts[i1*3+1] - verts[i0*3+1];
        float az = verts[i1*3+2] - verts[i0*3+2];
        float bx = verts[i2*3] - verts[i0*3];
        float by = verts[i2*3+1] - verts[i0*3+1];
        float bz = verts[i2*3+2] - verts[i0*3+2];
        float nx = ay*bz - az*by;
        float ny = az*bx - ax*bz;
        float nz = ax*by - ay*bx;
        float len = sqrtf(nx*nx + ny*ny + nz*nz);
        if (len > 1e-4f) {
            nx /= len;
            ny /= len;
            nz /= len;
        }
        for (int j = 0; j < 3; j++) {
            normals[(i*3+j)*3] = nx;
            normals[(i*3+j)*3+1] = ny;
            normals[(i*3+j)*3+2] = nz;
        }
    }
}

static int rebuild_composed_player_model(ViewerState *v, const RcPlayer *p) {
    if (!v->item_render_map.loaded || !v->item_models || !v->item_models->loaded)
        return 0;

    enum { MAX_COMPOSED_PARTS = 32 };
    ModelEntry *sources[MAX_COMPOSED_PARTS];
    int source_count = 0;
    uint32_t hide = composed_player_hide_mask(v, p);

    for (int part = 0; part < RUNEC_RENDER_BODY_PART_COUNT; part++) {
        uint32_t body_bit = 1u << part;
        uint32_t model_id = v->item_render_map.body_model_ids[part];
        if ((hide & body_bit) || model_id == RUNEC_RENDER_MODEL_MISSING)
            continue;
        source_count = append_composed_source(sources, source_count,
                                             MAX_COMPOSED_PARTS,
                                             v->item_models, model_id);
    }

    for (int slot = 0; slot < RC_EQUIP_COUNT; slot++) {
        const RcInvSlot *eq = &p->equipment[slot];
        if (eq->item_id < 0 || eq->quantity <= 0)
            continue;
        const RuneCItemRenderRecord *rec = runec_item_render_find(
            &v->item_render_map, (uint32_t)eq->item_id);
        if (!rec || rec->male_model_id == RUNEC_RENDER_MODEL_MISSING)
            continue;
        source_count = append_composed_source(sources, source_count,
                                             MAX_COMPOSED_PARTS,
                                             v->item_models,
                                             rec->male_model_id);
    }

    if (source_count <= 0)
        return 0;

    int total_vc = 0;
    int total_fc = 0;
    int total_bvc = 0;
    for (int i = 0; i < source_count; i++) {
        Mesh *mesh = &sources[i]->model.meshes[0];
        total_vc += mesh->vertexCount;
        total_fc += sources[i]->face_count;
        total_bvc += sources[i]->base_vert_count;
    }

    float *verts = calloc((size_t)total_vc * 3, sizeof(float));
    float *rest = calloc((size_t)total_vc * 3, sizeof(float));
    float *normals = calloc((size_t)total_vc * 3, sizeof(float));
    unsigned char *colors = calloc((size_t)total_vc * 4, sizeof(unsigned char));
    float *texcoords = v->item_models->has_textures
        ? calloc((size_t)total_vc * 2, sizeof(float))
        : NULL;
    int16_t *base = calloc((size_t)total_bvc * 3, sizeof(int16_t));
    uint8_t *skins = calloc((size_t)total_bvc, sizeof(uint8_t));
    uint16_t *faces = calloc((size_t)total_fc * 3, sizeof(uint16_t));
    uint8_t *priorities = calloc((size_t)total_fc, sizeof(uint8_t));
    ModelFaceUvInfo *face_uvs = v->item_models->has_textures
        ? calloc((size_t)total_fc, sizeof(ModelFaceUvInfo))
        : NULL;
    if (!verts || !rest || !normals || !colors || !base || !skins
            || !faces || !priorities || (v->item_models->has_textures && (!texcoords || !face_uvs))) {
        free(verts); free(rest); free(normals); free(colors);
        free(texcoords); free(base); free(skins); free(faces); free(priorities);
        free(face_uvs);
        return 0;
    }

    int vc_off = 0;
    int fc_off = 0;
    int bvc_off = 0;
    for (int i = 0; i < source_count; i++) {
        ModelEntry *src = sources[i];
        Mesh *mesh = &src->model.meshes[0];
        int vc = mesh->vertexCount;
        int fc = src->face_count;
        int bvc = src->base_vert_count;

        memcpy(&verts[vc_off * 3], src->rest_verts, (size_t)vc * 3 * sizeof(float));
        memcpy(&rest[vc_off * 3], src->rest_verts, (size_t)vc * 3 * sizeof(float));
        if (mesh->colors)
            memcpy(&colors[vc_off * 4], mesh->colors, (size_t)vc * 4);
        if (texcoords && mesh->texcoords)
            memcpy(&texcoords[vc_off * 2], mesh->texcoords, (size_t)vc * 2 * sizeof(float));
        memcpy(&base[bvc_off * 3], src->base_verts, (size_t)bvc * 3 * sizeof(int16_t));
        memcpy(&skins[bvc_off], src->vertex_skins, (size_t)bvc);

        for (int f = 0; f < fc * 3; f++)
            faces[fc_off * 3 + f] = (uint16_t)(src->face_indices[f] + bvc_off);
        memcpy(&priorities[fc_off], src->face_priorities, (size_t)fc);
        if (face_uvs && src->face_uvs) {
            for (int f = 0; f < fc; f++) {
                ModelFaceUvInfo info = src->face_uvs[f];
                if (info.textured) {
                    info.tex_a = (uint16_t)(info.tex_a + bvc_off);
                    info.tex_b = (uint16_t)(info.tex_b + bvc_off);
                    info.tex_c = (uint16_t)(info.tex_c + bvc_off);
                }
                face_uvs[fc_off + f] = info;
            }
        }

        vc_off += vc;
        fc_off += fc;
        bvc_off += bvc;
    }

    recalc_expanded_normals(verts, normals, total_fc);

    clear_composed_player_model(v);

    Mesh mesh = {0};
    mesh.vertexCount = total_vc;
    mesh.triangleCount = total_fc;
    mesh.vertices = verts;
    mesh.colors = colors;
    mesh.texcoords = texcoords;
    mesh.normals = normals;
    UploadMesh(&mesh, false);

    Model composed_model = LoadModelFromMesh(mesh);
    if (v->item_models->has_textures && v->item_models->atlas_texture.id > 0)
        composed_model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture =
            v->item_models->atlas_texture;
    if (v->alpha_cutout_shader_dynamic_loaded)
        composed_model.materials[0].shader = v->alpha_cutout_shader_dynamic;

    v->composed_player_model = (ModelEntry){
        .model_id = 0xC0FFEEu,
        .model = composed_model,
        .loaded = 1,
        .rest_verts = rest,
        .rest_texcoords = texcoords && total_vc > 0
            ? malloc((size_t)total_vc * 2 * sizeof(float)) : NULL,
        .base_verts = base,
        .vertex_skins = skins,
        .face_indices = faces,
        .face_priorities = priorities,
        .face_uvs = face_uvs,
        .base_vert_count = total_bvc,
        .face_count = total_fc,
    };
    if (texcoords && v->composed_player_model.rest_texcoords)
        memcpy(v->composed_player_model.rest_texcoords, texcoords,
               (size_t)total_vc * 2 * sizeof(float));
    v->composed_anim_state = anim_model_state_create(skins, total_bvc);
    v->composed_player_loaded = 1;
    v->composed_player_signature = composed_player_signature(v, p);
    return 1;
}

static int draw_animated_item_model(ViewerState *v, uint32_t model_id,
                                    Vector3 pos, float facing_angle,
                                    float scale, Color tint) {
    if (!v->item_models || !v->item_models->loaded
            || model_id == RUNEC_RENDER_MODEL_MISSING)
        return 0;
    ModelEntry *entry = model_find(v->item_models, model_id);
    if (!entry || !entry->loaded)
        return 0;
    animate_item_entry_to_player_frame(v, entry);
    begin_one_sided_model_draw();
    DrawModelEx(entry->model, pos, (Vector3){0, 1, 0}, facing_angle,
                (Vector3){scale, scale, scale}, tint);
    end_one_sided_model_draw();
    return 1;
}

static int render_record_has_loaded_bas(ViewerState *v,
                                        const RuneCItemRenderRecord *rec) {
    if (!v->anims || !rec)
        return 0;
    if (rec->ready_anim_id != RUNEC_RENDER_MODEL_MISSING
            && rec->ready_anim_id <= 0xFFFFu
            && anim_get_sequence(v->anims, (uint16_t)rec->ready_anim_id))
        return 1;
    if (rec->walk_anim_id != RUNEC_RENDER_MODEL_MISSING
            && rec->walk_anim_id <= 0xFFFFu
            && anim_get_sequence(v->anims, (uint16_t)rec->walk_anim_id))
        return 1;
    if (rec->run_anim_id != RUNEC_RENDER_MODEL_MISSING
            && rec->run_anim_id <= 0xFFFFu
            && anim_get_sequence(v->anims, (uint16_t)rec->run_anim_id))
        return 1;
    return 0;
}

static int held_equipment_lacks_loaded_bas(ViewerState *v, const RcPlayer *p) {
    const int slots[] = {EQUIP_WEAPON, EQUIP_SHIELD};
    for (int i = 0; i < (int)(sizeof(slots) / sizeof(slots[0])); i++) {
        int slot = slots[i];
        const RcInvSlot *eq = &p->equipment[slot];
        if (eq->item_id < 0 || eq->quantity <= 0)
            continue;
        const RuneCItemRenderRecord *rec = runec_item_render_find(
            &v->item_render_map, (uint32_t)eq->item_id);
        if (rec && !render_record_has_loaded_bas(v, rec))
            return 1;
    }
    return 0;
}

static void reset_model_entry_to_base_pose(ModelEntry *entry) {
    if (!entry || !entry->loaded || !entry->rest_verts)
        return;
    Mesh *mesh = &entry->model.meshes[0];
    float *mv = mesh->vertices;
    if (!mv)
        return;
    int vc = mesh->vertexCount;
    for (int i = 0; i < vc * 3; i++)
        mv[i] = entry->rest_verts[i];
    UpdateMeshBuffer(mesh[0], 0, mv, vc * 3 * sizeof(float), 0);
    if (mesh->texcoords && entry->rest_texcoords) {
        memcpy(mesh->texcoords, entry->rest_texcoords,
               (size_t)vc * 2 * sizeof(float));
        UpdateMeshBuffer(mesh[0], 1, mesh->texcoords,
                         vc * 2 * sizeof(float), 0);
    }
}

static int draw_static_item_model(ViewerState *v, uint32_t model_id,
                                  Vector3 pos, float facing_angle,
                                  float scale, Color tint) {
    if (!v->item_models || !v->item_models->loaded
            || model_id == RUNEC_RENDER_MODEL_MISSING)
        return 0;
    ModelEntry *entry = model_find(v->item_models, model_id);
    if (!entry || !entry->loaded)
        return 0;
    reset_model_entry_to_base_pose(entry);
    begin_one_sided_model_draw();
    DrawModelEx(entry->model, pos, (Vector3){0, 1, 0}, facing_angle,
                (Vector3){scale, scale, scale}, tint);
    end_one_sided_model_draw();
    return 1;
}

static int draw_raw_equipped_item_models(ViewerState *v, const RcPlayer *p,
                                         Vector3 player_pos, float facing_angle) {
    int drawn = 0;
    for (int slot = 0; slot < RC_EQUIP_COUNT; slot++) {
        const RcInvSlot *eq = &p->equipment[slot];
        if (eq->item_id < 0 || eq->quantity <= 0)
            continue;
        const RcItemDef *def = rc_item_def_get(eq->item_id);
        if (!def)
            continue;
        for (int i = 0; i < 3; i++) {
            int model_id = def->male_model_ids[i];
            if (model_id < 0)
                continue;
            drawn += draw_item_model(v, (uint32_t)model_id, player_pos,
                                     facing_angle, 1.0f, WHITE);
        }
    }
    return drawn;
}

static uint32_t composed_player_hide_mask(ViewerState *v, const RcPlayer *p) {
    uint32_t hide = 0;
    if (!v->item_render_map.loaded)
        return hide;
    for (int slot = 0; slot < RC_EQUIP_COUNT; slot++) {
        const RcInvSlot *eq = &p->equipment[slot];
        if (eq->item_id < 0 || eq->quantity <= 0)
            continue;
        const RuneCItemRenderRecord *rec = runec_item_render_find(
            &v->item_render_map, (uint32_t)eq->item_id);
        if (rec)
            hide |= rec->hide_body_mask;
    }
    return hide;
}

static int draw_composed_player_model(ViewerState *v, const RcPlayer *p,
                                      Vector3 player_pos, float facing_angle) {
    if (!v->item_render_map.loaded || !v->item_models || !v->item_models->loaded)
        return 0;

    uint64_t sig = composed_player_signature(v, p);
    if (!v->composed_player_loaded || sig != v->composed_player_signature) {
        if (!rebuild_composed_player_model(v, p))
            return 0;
    }

    if (v->anims && v->composed_anim_state) {
        animate_model_entry_to_player_frame(v, &v->composed_player_model,
                                            v->composed_anim_state);
    } else {
        reset_model_entry_to_base_pose(&v->composed_player_model);
    }

    begin_one_sided_model_draw();
    DrawModelEx(v->composed_player_model.model, player_pos,
                (Vector3){0, 1, 0}, facing_angle, (Vector3){1, 1, 1}, WHITE);
    end_one_sided_model_draw();
    return 1;
}

static int draw_generated_equipped_item_models(ViewerState *v, const RcPlayer *p,
                                               Vector3 player_pos,
                                               float facing_angle) {
    if (!v->item_render_map.loaded || !v->item_models || !v->item_models->loaded)
        return 0;

    int drawn = 0;
    for (int slot = 0; slot < RC_EQUIP_COUNT; slot++) {
        const RcInvSlot *eq = &p->equipment[slot];
        if (eq->item_id < 0 || eq->quantity <= 0)
            continue;
        const RuneCItemRenderRecord *rec = runec_item_render_find(
            &v->item_render_map, (uint32_t)eq->item_id);
        if (!rec || rec->male_model_id == RUNEC_RENDER_MODEL_MISSING)
            continue;
        if (!v->anims) {
            drawn += draw_static_item_model(v, rec->male_model_id, player_pos,
                                            facing_angle, 1.0f, WHITE);
        } else {
            drawn += draw_animated_item_model(v, rec->male_model_id, player_pos,
                                              facing_angle, 1.0f, WHITE);
        }
    }
    return drawn;
}

static int draw_ground_item_model(ViewerState *v, const RcGroundItem *ground,
                                  Vector3 pos) {
    int render_item_id = ground->item_id == 995
        ? coin_stack_model_item_id(ground->quantity)
        : ground->item_id;
    const RuneCItemRenderRecord *rec = runec_item_render_find(
        &v->item_render_map, (uint32_t)render_item_id);
    if (rec && rec->ground_model_id != RUNEC_RENDER_MODEL_MISSING) {
        return draw_item_model(v, rec->ground_model_id, pos, 0.0f,
                               ground_item_scale(render_item_id, ground->quantity),
                               WHITE);
    }

    const RcItemDef *def = rc_item_def_get(render_item_id);
    if (!def)
        def = rc_item_def_get(ground->item_id);
    if (!def || def->ground_model_id < 0)
        return 0;
    if (!v->item_models || !v->item_models->loaded)
        return 0;
    ModelEntry *entry = model_find(v->item_models, (uint32_t)def->ground_model_id);
    if (!entry || !entry->loaded)
        return 0;
    float scale = ground_item_scale(render_item_id, ground->quantity);
    DrawModelEx(entry->model, pos, (Vector3){0, 1, 0}, 0.0f,
                (Vector3){scale, scale, scale}, WHITE);
    return 1;
}

static Color projectile_color(const RcCombatProjectile *proj) {
    if (!proj) return WHITE;
    if (proj->style == COMBAT_MAGIC) return (Color){255, 104, 36, 235};
    if (proj->style == COMBAT_RANGED) return (Color){218, 178, 92, 235};
    return (Color){220, 220, 220, 235};
}

static float projectile_model_scale(const RcCombatProjectile *proj) {
    (void)proj;
    return 1.0f;
}

static ModelEntry *projectile_model_find(ViewerState *v, uint32_t model_id) {
    if (!v) return NULL;
    ModelEntry *entry = NULL;
    if (v->projectile_models && v->projectile_models->loaded) {
        entry = model_find(v->projectile_models, model_id);
        if (entry && entry->loaded) return entry;
    }
    if (v->item_models && v->item_models->loaded) {
        entry = model_find(v->item_models, model_id);
        if (entry && entry->loaded) return entry;
    }
    if (v->npc_models && v->npc_models->loaded) {
        entry = model_find(v->npc_models, model_id);
        if (entry && entry->loaded) return entry;
    }
    return NULL;
}

static const SpotAnimDef *projectile_travel_spotanim(ViewerState *v,
                                                     const RcCombatProjectile *proj) {
    if (!v || !proj || proj->travel_spotanim_id < 0)
        return NULL;
    return spotanim_find(v->spotanims, proj->travel_spotanim_id);
}

static const SpotAnimDef *projectile_launch_spotanim(ViewerState *v,
                                                     const RcCombatProjectile *proj) {
    if (!v || !proj || proj->launch_spotanim_id < 0)
        return NULL;
    return spotanim_find(v->spotanims, proj->launch_spotanim_id);
}

static const SpotAnimDef *projectile_impact_spotanim(ViewerState *v,
                                                     const RcCombatProjectile *proj) {
    if (!v || !proj || proj->impact_spotanim_id < 0)
        return NULL;
    return spotanim_find(v->spotanims, proj->impact_spotanim_id);
}

static int projectile_effect_model_id(const RcCombatProjectile *proj,
                                      const SpotAnimDef *spot) {
    if (proj && proj->projectile_model_id >= 0)
        return proj->projectile_model_id;
    if (spot && spot->model_id >= 0)
        return spot->model_id;
    return -1;
}

static ModelEntry *projectile_spotanim_model_entry(ViewerState *v,
                                                   const SpotAnimDef *spot,
                                                   int fallback_model_id) {
    if (spot && spot->id <= 0x0FFFFFFFu) {
        ModelEntry *entry = projectile_model_find(
            v, MODEL_ID_SPOTANIM_BASE + spot->id);
        if (entry)
            return entry;
    }
    int model_id = fallback_model_id;
    if (model_id < 0 && spot)
        model_id = spot->model_id;
    if (model_id < 0)
        return NULL;
    return projectile_model_find(v, (uint32_t)model_id);
}

static ModelEntry *projectile_effect_model_entry(ViewerState *v,
                                                 const RcCombatProjectile *proj,
                                                 const SpotAnimDef *spot) {
    return projectile_spotanim_model_entry(
        v, spot, projectile_effect_model_id(proj, spot));
}

static int projectile_effect_anim_id(const RcCombatProjectile *proj,
                                     const SpotAnimDef *spot) {
    if (proj && proj->projectile_anim_id >= 0)
        return proj->projectile_anim_id;
    if (spot && spot->animation_id >= 0)
        return spot->animation_id;
    return -1;
}

static Vector3 projectile_spotanim_scale(const RcCombatProjectile *proj,
                                         const SpotAnimDef *spot) {
    float scale = projectile_model_scale(proj);
    float xy = scale;
    float y = scale;
    if (spot && spot->resize_xy > 0)
        xy *= (float)spot->resize_xy / 128.0f;
    if (spot && spot->resize_z > 0)
        y *= (float)spot->resize_z / 128.0f;
    return (Vector3){xy, y, xy};
}

static int projectile_target_point(ViewerState *v,
                                   const RcCombatProjectile *proj,
                                   int scene_plane,
                                   float *out_x,
                                   float *out_z,
                                   float *out_y,
                                   int *out_tile_x,
                                   int *out_tile_y) {
    if (!v || !proj || !out_x || !out_z || !out_y)
        return 0;
    float wx = (float)proj->target_x;
    float wy = (float)proj->target_y;
    int tile_x = proj->target_x;
    int tile_y = proj->target_y;
    int target_plane = proj->plane;

    if (proj->target_kind == RC_COMBAT_ACTOR_NPC) {
        for (int i = 0; i < v->world->npc_count; i++) {
            const RcNpc *npc = &v->world->npcs[i];
            if (!npc->active || npc->uid != proj->target_uid)
                continue;
            if (npc->plane != scene_plane)
                return 0;
            int size = 1;
            if (npc->def_id >= 0 && npc->def_id < g_npc_def_count &&
                    g_npc_defs[npc->def_id].size > 0) {
                size = g_npc_defs[npc->def_id].size;
            }
            wx = v->npc_render[i].initialized
               ? v->npc_render[i].render_x : (float)npc->x;
            wy = v->npc_render[i].initialized
               ? v->npc_render[i].render_y : (float)npc->y;
            tile_x = npc->x;
            tile_y = npc->y;
            *out_x = (wx - g_world_origin_x) + 0.5f * (float)size;
            *out_z = -((wy - g_world_origin_y) + 0.5f * (float)size);
            *out_y = ground_yf_plane(v, scene_plane, wx, wy);
            if (out_tile_x) *out_tile_x = tile_x;
            if (out_tile_y) *out_tile_y = tile_y;
            return 1;
        }
    } else if (proj->target_kind == RC_COMBAT_ACTOR_PLAYER) {
        const RcPlayer *p = &v->world->player;
        if (p->plane != scene_plane)
            return 0;
        target_plane = p->plane;
        wx = v->prev_player_x + ((float)p->x - v->prev_player_x) * v->tick_frac;
        wy = v->prev_player_y + ((float)p->y - v->prev_player_y) * v->tick_frac;
        tile_x = p->x;
        tile_y = p->y;
    }

    if (target_plane != scene_plane)
        return 0;
    *out_x = (wx - g_world_origin_x) + 0.5f;
    *out_z = -((wy - g_world_origin_y) + 0.5f);
    *out_y = ground_yf_plane(v, scene_plane, wx, wy);
    if (out_tile_x) *out_tile_x = tile_x;
    if (out_tile_y) *out_tile_y = tile_y;
    return 1;
}

static Vector3 combat_projectile_position(ViewerState *v,
                                          const RcCombatProjectile *proj,
                                          int scene_plane,
                                          float *out_angle,
                                          int *out_visible) {
    *out_visible = 0;
    float sx = (float)LOCAL_X(proj->source_x) + 0.5f;
    float sz = -((float)LOCAL_Y(proj->source_y) + 0.5f);
    float tx = 0.0f;
    float tz = 0.0f;
    float target_ground = 0.0f;
    if (!projectile_target_point(v, proj, scene_plane, &tx, &tz,
                                 &target_ground, NULL, NULL)) {
        return (Vector3){sx, 0.0f, sz};
    }

    float start_h = proj->projectile_start_height >= 0
                  ? (float)proj->projectile_start_height / 128.0f
                  : 1.45f;
    float end_h = proj->impact_spotanim_height >= 0
                ? (float)proj->impact_spotanim_height / 128.0f
                : (proj->projectile_end_height >= 0
                   ? (float)proj->projectile_end_height / 128.0f : 1.05f);
    float sy = ground_y_plane(v, scene_plane, proj->source_x,
                              proj->source_y) + start_h;
    float ty = target_ground + end_h;

    float start_time = proj->projectile_start_time >= 0
                     ? (float)proj->projectile_start_time : 0.0f;
    float end_time = proj->projectile_end_time > proj->projectile_start_time
                   ? (float)proj->projectile_end_time
                   : (float)(proj->duration_ticks > 0
                             ? proj->duration_ticks * 30 : 30);
    float client_time = ((float)proj->age_ticks + v->tick_frac) * 30.0f;
    if (client_time < start_time || client_time > end_time + 1.0f)
        return (Vector3){sx, sy, sz};

    float dx = tx - sx;
    float dz = tz - sz;
    float horizontal = sqrtf(dx * dx + dz * dz);
    float dir_x = 0.0f;
    float dir_z = 1.0f;
    if (horizontal > 1e-5f) {
        dir_x = dx / horizontal;
        dir_z = dz / horizontal;
    }

    float start_pos = proj->projectile_progress >= 0
                    ? (float)proj->projectile_progress / 128.0f
                    : 0.0f;
    sx += dir_x * start_pos;
    sz += dir_z * start_pos;

    float travel_time = end_time + 1.0f - start_time;
    if (travel_time < 1.0f)
        travel_time = 1.0f;
    float t = client_time - start_time;
    if (t < 0.0f) t = 0.0f;
    if (t > travel_time) t = travel_time;

    float speed_x = (tx - sx) / travel_time;
    float speed_z = (tz - sz) / travel_time;
    float horizontal_speed = sqrtf(speed_x * speed_x + speed_z * speed_z);
    float slope = proj->projectile_angle >= 0
                ? (float)proj->projectile_angle : 15.0f;
    float speed_y = horizontal_speed * tanf(slope * (3.14159265f / 128.0f));
    float accel_y = 2.0f * (ty - sy - speed_y * travel_time) /
                    (travel_time * travel_time);

    Vector3 pos = {
        sx + speed_x * t,
        sy + speed_y * t + 0.5f * accel_y * t * t,
        sz + speed_z * t,
    };
    if (out_angle) {
        *out_angle = atan2f(tx - sx, tz - sz) * (180.0f / 3.14159265f)
                   + 180.0f;
    }
    *out_visible = 1;
    return pos;
}

static int combat_projectile_impact_position(ViewerState *v,
                                             const RcCombatProjectile *proj,
                                             int scene_plane,
                                             Vector3 *out_pos) {
    if (!out_pos)
        return 0;
    float tx = 0.0f;
    float tz = 0.0f;
    float target_ground = 0.0f;
    if (!projectile_target_point(v, proj, scene_plane, &tx, &tz,
                                 &target_ground, NULL, NULL)) {
        return 0;
    }
    float end_h = proj->projectile_end_height >= 0
                ? (float)proj->projectile_end_height / 128.0f
                : 1.05f;
    *out_pos = (Vector3){tx, target_ground + end_h, tz};
    return 1;
}

static int projectile_sequence_duration(AnimCache *cache, int anim_id) {
    if (!cache || anim_id < 0 || anim_id > 0xFFFF)
        return 30;
    AnimSequence *seq = anim_get_sequence(cache, (uint16_t)anim_id);
    if (!seq || seq->frame_count <= 0)
        return 30;
    int total = 0;
    for (int i = 0; i < seq->frame_count; i++)
        total += seq->frames[i].delay > 0 ? seq->frames[i].delay : 1;
    return total > 0 ? total : 30;
}

static int draw_projectile_impact(ViewerState *v,
                                  const RcCombatProjectile *proj,
                                  int scene_plane,
                                  float client_time,
                                  float impact_start) {
    if (proj->impact_spotanim_id < 0 || client_time < impact_start)
        return 0;
    const SpotAnimDef *spot = projectile_impact_spotanim(v, proj);
    int anim_id = spot && spot->animation_id >= 0 ? spot->animation_id : -1;
    int anim_duration = projectile_sequence_duration(v->anims, anim_id);
    float elapsed = client_time - impact_start;
    float retain = (float)(proj->impact_duration_ticks > 0
                         ? proj->impact_duration_ticks * 30 : 30);
    float draw_until = fminf((float)anim_duration, retain);
    if (elapsed >= draw_until)
        return 0;

    Vector3 pos;
    if (!combat_projectile_impact_position(v, proj, scene_plane, &pos))
        return 0;

    ModelEntry *entry = projectile_spotanim_model_entry(v, spot, -1);
    if (entry) {
        float angle = spot ? (float)spot->rotation : 0.0f;
        Vector3 scale = projectile_spotanim_scale(proj, spot);
        AnimModelState *anim_state = projectile_anim_state_for_entry(v, entry);
        if (!animate_model_entry_sequence(entry, anim_state, v->anims,
                                          anim_id, elapsed)) {
            reset_model_entry_to_base_pose(entry);
        }
        DrawModelEx(entry->model, pos, (Vector3){0, 1, 0}, angle,
                    scale, WHITE);
        return 1;
    }

    Color c = projectile_color(proj);
    DrawSphere(pos, proj->style == COMBAT_MAGIC ? 0.28f : 0.12f, c);
    return 1;
}

static int draw_projectile_launch(ViewerState *v,
                                  const RcCombatProjectile *proj,
                                  int scene_plane,
                                  float client_time) {
    if (!v || !proj || proj->launch_spotanim_id < 0 ||
            proj->plane != scene_plane) {
        return 0;
    }
    const SpotAnimDef *spot = projectile_launch_spotanim(v, proj);
    int anim_id = spot && spot->animation_id >= 0 ? spot->animation_id : -1;
    int anim_duration = projectile_sequence_duration(v->anims, anim_id);
    float retain = proj->projectile_start_time > 0
                 ? (float)proj->projectile_start_time : 30.0f;
    if (retain < 30.0f) retain = 30.0f;
    if (retain > (float)anim_duration) retain = (float)anim_duration;
    if (client_time >= retain)
        return 0;

    float height = proj->launch_spotanim_height >= 0
                 ? (float)proj->launch_spotanim_height / 128.0f : 0.75f;
    Vector3 pos = {
        (float)LOCAL_X(proj->source_x) + 0.5f,
        ground_y_plane(v, scene_plane, proj->source_x, proj->source_y) + height,
        -((float)LOCAL_Y(proj->source_y) + 0.5f),
    };
    ModelEntry *entry = projectile_spotanim_model_entry(v, spot, -1);
    if (entry) {
        float angle = spot ? (float)spot->rotation : 0.0f;
        Vector3 scale = projectile_spotanim_scale(proj, spot);
        AnimModelState *anim_state = projectile_anim_state_for_entry(v, entry);
        if (!animate_model_entry_sequence(entry, anim_state, v->anims,
                                          anim_id, client_time)) {
            reset_model_entry_to_base_pose(entry);
        }
        DrawModelEx(entry->model, pos, (Vector3){0, 1, 0}, angle,
                    scale, WHITE);
        return 1;
    }
    DrawSphere(pos, proj->style == COMBAT_MAGIC ? 0.18f : 0.08f,
               projectile_color(proj));
    return 1;
}

static void draw_combat_projectiles(ViewerState *v) {
    int count = 0;
    const RcCombatProjectile *projectiles =
        rc_combat_projectiles(v->world, &count);
    if (!projectiles || count <= 0) return;
    int scene_plane = viewer_scene_plane(v);
    for (int i = 0; i < count; i++) {
        const RcCombatProjectile *proj = &projectiles[i];
        if (!proj->active || proj->plane != scene_plane)
            continue;
        float end_time = proj->projectile_end_time > proj->projectile_start_time
                       ? (float)proj->projectile_end_time
                       : (float)(proj->duration_ticks > 0
                                 ? proj->duration_ticks * 30 : 30);
        float client_time = ((float)proj->age_ticks + v->tick_frac) * 30.0f;
        draw_projectile_launch(v, proj, scene_plane, client_time);
        float angle = 0.0f;
        int visible = 0;
        Vector3 pos = combat_projectile_position(v, proj, scene_plane,
                                                 &angle, &visible);
        if (!visible) {
            draw_projectile_impact(v, proj, scene_plane, client_time,
                                   end_time);
            continue;
        }
        float sx = (float)LOCAL_X(proj->source_x) + 0.5f;
        float sz = -((float)LOCAL_Y(proj->source_y) + 0.5f);
        float sy = ground_y_plane(v, scene_plane, proj->source_x,
                                  proj->source_y) +
                   (proj->projectile_start_height >= 0
                    ? (float)proj->projectile_start_height / 128.0f : 1.45f);
        Vector3 start = {sx, sy, sz};
        Color c = projectile_color(proj);
        const SpotAnimDef *spot = projectile_travel_spotanim(v, proj);
        int anim_id = projectile_effect_anim_id(proj, spot);
        ModelEntry *entry = projectile_effect_model_entry(v, proj, spot);
        if (entry) {
            if (spot)
                angle += (float)spot->rotation;
            Vector3 scale = projectile_spotanim_scale(proj, spot);
            AnimModelState *anim_state = projectile_anim_state_for_entry(v, entry);
            float client_ticks = ((float)proj->age_ticks + v->tick_frac) * 30.0f;
            if (proj->projectile_start_time > 0)
                client_ticks -= (float)proj->projectile_start_time;
            if (client_ticks < 0.0f)
                client_ticks = 0.0f;
            if (!animate_model_entry_sequence(entry, anim_state, v->anims,
                                              anim_id, client_ticks)) {
                reset_model_entry_to_base_pose(entry);
            }
            DrawModelEx(entry->model, pos, (Vector3){0, 1, 0}, angle,
                        scale, WHITE);
            continue;
        }
        float radius = proj->style == COMBAT_MAGIC ? 0.16f : 0.07f;
        DrawSphere(pos, radius, c);
        DrawLine3D(start, pos, (Color){c.r, c.g, c.b, 80});
    }
}

static const RuneCItemRenderRecord *equipped_weapon_render_record(
    ViewerState *v,
    const RcPlayer *p
) {
    if (!v->item_render_map.loaded)
        return NULL;
    if (p->equipment[EQUIP_WEAPON].item_id < 0
            || p->equipment[EQUIP_WEAPON].quantity <= 0)
        return NULL;
    return runec_item_render_find(&v->item_render_map,
                                  (uint32_t)p->equipment[EQUIP_WEAPON].item_id);
}

static int player_anim_or_fallback(ViewerState *v, uint32_t anim_id,
                                   int fallback) {
    if (!v->anims || anim_id == RUNEC_RENDER_MODEL_MISSING || anim_id == 0
            || anim_id > 0xFFFFu)
        return fallback;
    return anim_get_sequence(v->anims, (uint16_t)anim_id)
         ? (int)anim_id : fallback;
}

static int player_core_attack_anim_or_fallback(ViewerState *v, int fallback) {
    const RcPlayer *p = &v->world->player;
    int anim = p->combat.attack_animation_id;
    if (anim > 0)
        return player_anim_or_fallback(v, (uint32_t)anim, fallback);
    return fallback;
}

static float face_angle_between_tiles(int from_x, int from_y,
                                      int to_x, int to_y, float fallback) {
    int dx = to_x - from_x;
    int dy = to_y - from_y;
    if (!dx && !dy) return fallback;
    return atan2f((float)dx, -(float)dy) * (180.0f / 3.14159f);
}

static float player_core_facing_angle(const RcPlayer *p, float fallback) {
    if (!p) return fallback;
    if (p->facing_entity >= 0 || p->facing_x >= 0 || p->facing_y >= 0) {
        return face_angle_between_tiles(p->x, p->y, p->facing_x,
                                        p->facing_y, fallback);
    }
    return fallback;
}

static float npc_core_facing_angle(const RcNpc *n, float fallback) {
    if (!n) return fallback;
    if (n->facing_entity >= 0 || n->facing_x >= 0 || n->facing_y >= 0) {
        return face_angle_between_tiles(n->x, n->y, n->facing_x,
                                        n->facing_y, fallback);
    }
    return fallback;
}

static int player_target_anim_id(ViewerState *v) {
    const RcPlayer *p = &v->world->player;
    const RuneCItemRenderRecord *weapon = equipped_weapon_render_record(v, p);
    if (p->action_anim_timer > 0 && p->action_anim_id > 0)
        return player_anim_or_fallback(v, (uint32_t)p->action_anim_id,
                                       ANIM_IDLE);
    if (p->attack_anim_timer > 0 || p->combat.attack_animation_timer > 0) {
        int weapon_id = p->equipment[EQUIP_WEAPON].item_id;
        int attack_anim = ANIM_ATTACK_UNARMED;
        if (p->combat.combat_class == RC_COMBAT_CLASS_MAGIC)
            attack_anim = ANIM_ATTACK_CAST;
        else if (p->combat.combat_class == RC_COMBAT_CLASS_RANGED)
            attack_anim = ANIM_ATTACK_BOW;
        else if (weapon_id == 4151)
            attack_anim = ANIM_ATTACK_WHIP;
        else if (weapon_id == 11802)
            attack_anim = ANIM_ATTACK_GODSWORD;
        else if (weapon_id == 1381)
            attack_anim = ANIM_ATTACK_STAFF;
        return player_core_attack_anim_or_fallback(
            v, player_anim_or_fallback(v, attack_anim, ANIM_IDLE));
    }
    if (v->player_moving) {
        if (p->running) {
            return weapon
                 ? player_anim_or_fallback(v, weapon->run_anim_id, ANIM_RUN)
                 : ANIM_RUN;
        }
        return weapon
             ? player_anim_or_fallback(v, weapon->walk_anim_id, ANIM_WALK)
             : ANIM_WALK;
    }
    return weapon
         ? player_anim_or_fallback(v, weapon->ready_anim_id, ANIM_IDLE)
         : ANIM_IDLE;
}

static void snap_npc_render_state(ViewerState *v, int idx, const RcNpc *npc) {
    v->npc_render[idx].initialized = 1;
    v->npc_render[idx].server_x = npc->x;
    v->npc_render[idx].server_y = npc->y;
    v->npc_render[idx].server_plane = npc->plane;
    v->npc_render[idx].last_seen_tick = v->world ? v->world->tick : 0;
    v->npc_render[idx].render_x = (float)npc->x;
    v->npc_render[idx].render_y = (float)npc->y;
    v->npc_render[idx].waypoint_count = 0;
    v->npc_render[idx].moving = 0;
    v->npc_render[idx].move_anim_timer = 0.0f;
    v->npc_render[idx].last_dx = 0;
    v->npc_render[idx].last_dy = 0;
}

static int sign_i(int value) {
    return (value > 0) - (value < 0);
}

static int npc_render_push_waypoint(ViewerState *v, int idx, float x, float y) {
    int count = v->npc_render[idx].waypoint_count;
    if (count > 0
            && fabsf(v->npc_render[idx].waypoint_x[count - 1] - x) < 0.001f
            && fabsf(v->npc_render[idx].waypoint_y[count - 1] - y) < 0.001f) {
        return 1;
    }
    if (count >= NPC_RENDER_QUEUE_MAX) {
        v->npc_render[idx].waypoint_x[NPC_RENDER_QUEUE_MAX - 1] = x;
        v->npc_render[idx].waypoint_y[NPC_RENDER_QUEUE_MAX - 1] = y;
        return 1;
    }
    v->npc_render[idx].waypoint_x[count] = x;
    v->npc_render[idx].waypoint_y[count] = y;
    v->npc_render[idx].waypoint_count = count + 1;
    return 1;
}

static void npc_render_pop_waypoint(ViewerState *v, int idx) {
    int count = v->npc_render[idx].waypoint_count;
    if (count <= 0) return;
    for (int i = 1; i < count; i++) {
        v->npc_render[idx].waypoint_x[i - 1] = v->npc_render[idx].waypoint_x[i];
        v->npc_render[idx].waypoint_y[i - 1] = v->npc_render[idx].waypoint_y[i];
    }
    v->npc_render[idx].waypoint_count = count - 1;
}

static int enqueue_npc_server_step(ViewerState *v, int idx, const RcNpc *npc) {
    int dx = npc->x - v->npc_render[idx].server_x;
    int dy = npc->y - v->npc_render[idx].server_y;
    if (!dx && !dy) return 1;
    if (abs(dx) > 2 || abs(dy) > 2) {
        snap_npc_render_state(v, idx, npc);
        return 0;
    }

    int sx = sign_i(dx);
    int sy = sign_i(dy);
    int from_x = v->npc_render[idx].server_x;
    int from_y = v->npc_render[idx].server_y;
    while (from_x != npc->x || from_y != npc->y) {
        if (from_x != npc->x) from_x += sx;
        if (from_y != npc->y) from_y += sy;
        if (!npc_render_push_waypoint(v, idx, (float)from_x, (float)from_y)) {
            snap_npc_render_state(v, idx, npc);
            return 0;
        }
    }

    v->npc_render[idx].last_dx = sx;
    v->npc_render[idx].last_dy = sy;
    v->npc_render[idx].move_anim_timer = 1.0f / TPS;
    return 1;
}

static void update_npc_render_motion(ViewerState *v, float dt) {
    if (!v || !v->world) return;
    float step = dt;
    if (step < 0.0f) step = 0.0f;
    if (step > NPC_RENDER_MAX_DT) step = NPC_RENDER_MAX_DT;
    float max_axis_delta = step * TPS;
    for (int i = 0; i < v->world->npc_count; i++) {
        const RcNpc *npc = &v->world->npcs[i];
        if (!npc->active || npc->is_dead) {
            v->npc_render[i].initialized = 0;
            v->npc_render[i].moving = 0;
            v->npc_render[i].move_anim_timer = 0.0f;
            continue;
        }

        if (!v->npc_render[i].initialized) {
            snap_npc_render_state(v, i, npc);
            continue;
        }

        if (npc->plane != v->npc_render[i].server_plane) {
            snap_npc_render_state(v, i, npc);
            continue;
        }

        if (v->npc_render[i].last_seen_tick != v->world->tick) {
            v->npc_render[i].last_seen_tick = v->world->tick;
            if (!enqueue_npc_server_step(v, i, npc)) {
                continue;
            }
            v->npc_render[i].server_x = npc->x;
            v->npc_render[i].server_y = npc->y;
            v->npc_render[i].server_plane = npc->plane;
        }

        if (v->npc_render[i].move_anim_timer > 0.0f) {
            v->npc_render[i].move_anim_timer -= dt;
            if (v->npc_render[i].move_anim_timer < 0.0f)
                v->npc_render[i].move_anim_timer = 0.0f;
        }

        int moved_this_frame = 0;
        v->npc_render[i].moving = v->npc_render[i].waypoint_count > 0;
        if (v->npc_render[i].waypoint_count > 0 && max_axis_delta > 0.0f) {
            float target_x = v->npc_render[i].waypoint_x[0];
            float target_y = v->npc_render[i].waypoint_y[0];
            float dx = target_x - v->npc_render[i].render_x;
            float dy = target_y - v->npc_render[i].render_y;
            float axis = fmaxf(fabsf(dx), fabsf(dy));
            if (axis <= max_axis_delta || axis < 0.001f) {
                v->npc_render[i].render_x = target_x;
                v->npc_render[i].render_y = target_y;
                npc_render_pop_waypoint(v, i);
                v->npc_render[i].moving =
                    v->npc_render[i].waypoint_count > 0;
                moved_this_frame = 1;
            } else {
                float scale = max_axis_delta / axis;
                v->npc_render[i].render_x += dx * scale;
                v->npc_render[i].render_y += dy * scale;
                v->npc_render[i].moving = 1;
                moved_this_frame = 1;
            }
        }
        if (moved_this_frame && v->npc_render[i].move_anim_timer < 0.05f)
            v->npc_render[i].move_anim_timer = 0.05f;
    }
}

static void sync_ui_minimap(ViewerState *v) {
    RcPlayer *p = &v->world->player;
    int scene_plane = viewer_scene_plane(v);
    if (!v->minimap_tiles || v->minimap_tiles_plane != scene_plane)
        build_minimap_tiles(v);
    Color pixels[152 * 152];
    const float scale = 4.0f;
    float player_x = v->prev_player_x + ((float)p->x - v->prev_player_x) * v->tick_frac;
    float player_y = v->prev_player_y + ((float)p->y - v->prev_player_y) * v->tick_frac;
    for (int y = 0; y < 152; y++) {
        for (int x = 0; x < 152; x++) {
            float sx = (float)x - 76.0f;
            float sy = (float)y - 76.0f;
            if (sx * sx + sy * sy > 75.0f * 75.0f) {
                pixels[x + y * 152] = BLANK;
                continue;
            }
            int wx = (int)roundf(player_x + sx / scale);
            int wy = (int)roundf(player_y - sy / scale);
            pixels[x + y * 152] = world_map_minimap_color(v, wx, wy);
        }
    }

    if (!v->use_world_map_minimap || scene_plane != 0) {
        for (int dy = -20; dy <= 20; dy++) {
            for (int dx = -20; dx <= 20; dx++) {
                int sx = (int)roundf(76.0f + dx * scale);
                int sy = (int)roundf(76.0f - dy * scale);
                minimap_draw_tile_features(v, pixels, p->x + dx, p->y + dy, sx, sy);
            }
        }
    }
    runec_ui_update_minimap(&v->ui, pixels, 152, 152);

    runec_ui_clear_minimap(&v->ui);
    runec_ui_add_minimap_dot(&v->ui, 0, 0, RUNEC_UI_MINIMAP_DOT_PLAYER);

    if (p->route_idx < p->route_len && p->route_len > 0) {
        int tx = p->route_x[p->route_len - 1];
        int ty = p->route_y[p->route_len - 1];
        runec_ui_add_minimap_dot(&v->ui, (float)tx - player_x,
                                 (float)ty - player_y,
                                 RUNEC_UI_MINIMAP_DOT_DESTINATION);
    }

    for (int i = 0; i < v->world->npc_count; i++) {
        const RcNpc *npc = &v->world->npcs[i];
        if (!npc->active || npc->plane != scene_plane)
            continue;
        float npc_x = v->npc_render[i].initialized
                    ? v->npc_render[i].render_x : (float)npc->x;
        float npc_y = v->npc_render[i].initialized
                    ? v->npc_render[i].render_y : (float)npc->y;
        float dx = npc_x - player_x;
        float dy = npc_y - player_y;
        if (dx < -40 || dx > 40 || dy < -40 || dy > 40)
            continue;
        runec_ui_add_minimap_dot(&v->ui, dx, dy, RUNEC_UI_MINIMAP_DOT_NPC);
    }
}

static void handle_input(ViewerState *v, int ui_capture) {
    RcPlayer *p = &v->world->player;

    if (handle_scene_plane_buttons(v))
        return;
    if (handle_dev_transport_buttons(v))
        return;

    if (!ui_capture && IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
        int npc_uid = pick_npc_at_mouse(v);
        if (npc_uid >= 0) {
            open_npc_context_menu(v, npc_uid);
            return;
        }
        ViewerPickedObject object;
        if (pick_object_at_mouse(v, &object)) {
            open_object_context_menu(v, object);
            return;
        }
        reset_viewer_context(v);
    }

    // Camera orbit
    if (!ui_capture && IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !v->ui.context_open) {
        Vector2 d = GetMouseDelta();
        v->cam_yaw += d.x * 0.005f;
        v->cam_pitch -= d.y * 0.005f;
        if (v->cam_pitch < 0.1f) v->cam_pitch = 0.1f;
        if (v->cam_pitch > 1.4f) v->cam_pitch = 1.4f;
    }
    float wh = ui_capture ? 0.0f : GetMouseWheelMove();
    if (wh != 0.0f) {
        v->cam_dist *= (wh > 0) ? (1.0f / 1.15f) : 1.15f;
        if (v->cam_dist < 5) v->cam_dist = 5;
        if (v->cam_dist > 300) v->cam_dist = 300;
    }

    if (!v->ui.chat_focused) {
        if (IsKeyPressed(KEY_FOUR)) { v->cam_yaw = 0; v->cam_pitch = 1.35f; v->cam_dist = 120; }
        if (IsKeyPressed(KEY_FIVE)) { v->cam_yaw = 0; v->cam_pitch = 0.6f; v->cam_dist = 50; }
        if (IsKeyPressed(KEY_L)) v->camera_locked = !v->camera_locked;
        if (IsKeyPressed(KEY_G)) v->show_grid = !v->show_grid;
        if (IsKeyPressed(KEY_C)) v->show_collision = !v->show_collision;
        if (IsKeyPressed(KEY_SPACE)) v->paused = !v->paused;
        if (IsKeyPressed(KEY_R)) p->running = !p->running;
        if (IsKeyPressed(KEY_P)) pickup_current_tile(v);
        if (IsKeyPressed(KEY_PAGE_UP))
            viewer_set_scene_plane_delta(v, 1);
        if (IsKeyPressed(KEY_PAGE_DOWN))
            viewer_set_scene_plane_delta(v, -1);
        if (IsKeyPressed(KEY_HOME))
            v->scene_plane_override = -1;
    }

    // Click-to-move
    if (!ui_capture && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
            && !IsMouseButtonDown(MOUSE_BUTTON_RIGHT)
            && !IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        if (v->ui.selected_target.kind != RUNEC_UI_SELECTED_NONE) {
            int npc_uid = pick_npc_at_mouse(v);
            if (npc_uid >= 0) {
                if (v->ui.selected_target.kind == RUNEC_UI_SELECTED_ITEM) {
                    rc_player_use_inventory_item_on_npc(
                        v->world, v->ui.selected_target.source_slot, npc_uid);
                } else {
                    int spell_id = selected_spell_id_for_viewer(v);
                    if (spell_id < 0)
                        spell_id = rc_spell_find(v->ui.selected_target.label);
                    if (spell_id >= 0)
                        rc_player_cast_spell_on_npc(v->world, spell_id, npc_uid);
                }
                runec_ui_clear_selected_target(&v->ui);
                return;
            }
            ViewerPickedObject object;
            if (pick_object_at_mouse_tile(v, &object)) {
                if (v->ui.selected_target.kind == RUNEC_UI_SELECTED_ITEM) {
                    rc_player_use_inventory_item_on_object(
                        v->world, v->ui.selected_target.source_slot,
                        object.obj_id, object.x, object.y, object.plane);
                } else {
                    int spell_id = selected_spell_id_for_viewer(v);
                    if (spell_id < 0)
                        spell_id = rc_spell_find(v->ui.selected_target.label);
                    if (spell_id >= 0) {
                        rc_player_cast_spell_on_object(v->world, spell_id,
                                                       object.obj_id,
                                                       object.x, object.y,
                                                       object.plane);
                    }
                }
                runec_ui_clear_selected_target(&v->ui);
                return;
            }
            int tx, ty;
            if (raycast_tile(v, &tx, &ty)) {
                int ground_idx = ground_item_at_tile(v, tx, ty);
                if (ground_idx >= 0) {
                    if (v->ui.selected_target.kind == RUNEC_UI_SELECTED_ITEM) {
                        rc_player_use_inventory_item_on_ground_item(
                            v->world, v->ui.selected_target.source_slot,
                            ground_idx);
                    } else {
                        int spell_id = selected_spell_id_for_viewer(v);
                        if (spell_id < 0)
                            spell_id = rc_spell_find(v->ui.selected_target.label);
                        if (spell_id >= 0) {
                            rc_player_cast_spell_on_ground_item(v->world,
                                                                spell_id,
                                                                ground_idx);
                        }
                    }
                    runec_ui_clear_selected_target(&v->ui);
                    return;
                }
            }
        }

        int npc_uid = pick_npc_at_mouse(v);
        if (npc_uid >= 0) {
            rc_player_attack_npc(v->world, npc_uid);
            return;
        }
        ViewerPickedObject object;
        if (pick_object_at_mouse_tile(v, &object)) {
            int option = object_first_action_option(object);
            if (option >= 0) {
                rc_player_interact_object_at(v->world, object.obj_id,
                                             object.x, object.y,
                                             object.plane, option);
            } else {
                route_player_to(v, object.x, object.y);
            }
            return;
        }
        int tx, ty;
        if (raycast_tile(v, &tx, &ty)) {
            int ground_idx = ground_item_at_tile(v, tx, ty);
            if (ground_idx >= 0 && p->x == tx && p->y == ty)
                rc_player_pickup_item(v->world, ground_idx);
            else
                route_player_to(v, tx, ty);
        }
    }

    // WASD
    int dx = 0, dy = 0;
    if (!v->ui.chat_focused) {
        if (IsKeyDown(KEY_W)) dy = 1;
        if (IsKeyDown(KEY_S)) dy = -1;
        if (IsKeyDown(KEY_A)) dx = -1;
        if (IsKeyDown(KEY_D)) dx = 1;
    }
    if ((dx || dy) && p->route_idx >= p->route_len) {
        p->route_x[0] = p->x + dx;
        p->route_y[0] = p->y + dy;
        p->route_len = 1;
        p->route_idx = 0;
    }
}

static void process_movement(ViewerState *v, int *moved) {
    RcWorld *world = v->world;
    static int logged = 0;
    RcPlayer *p = &world->player;
    *moved = 0;
    if (p->route_idx >= p->route_len) return;

    // One-time sanity check: is collision data actually present?
    if (!logged) {
        logged = 1;
        fprintf(stderr, "DEBUG: map.region_count=%d\n", world->map.region_count);
        // Check known blocked tile
        uint32_t f = rc_get_flags(&world->map, 3210, 3426, p->plane);
        fprintf(stderr, "DEBUG: flags at (3210,3426) = 0x%08X (expect non-zero)\n", f);
    }

    int steps = p->running ? 2 : 1;
    for (int s = 0; s < steps && p->route_idx < p->route_len; s++) {
        int nx = p->route_x[p->route_idx];
        int ny = p->route_y[p->route_idx];
        int dx = nx - p->x, dy = ny - p->y;
        if (dx > 1) dx = 1; if (dx < -1) dx = -1;
        if (dy > 1) dy = 1; if (dy < -1) dy = -1;

        int can = rc_can_move(&world->map, p->x, p->y, dx, dy, p->plane);
        if (can) {
            p->x += dx; p->y += dy;
            // atan2(dx, dy) gives world-space angle. Negate because Z is flipped in rendering.
            v->player_facing_angle = atan2f((float)dx, -(float)dy) * (180.0f / 3.14159f);
            *moved = 1;
        } else {
            uint32_t dest_f = rc_get_flags(&world->map, p->x + dx, p->y + dy,
                                           p->plane);
            fprintf(stderr, "BLOCKED (%d,%d)->(%d,%d) dest=0x%08X\n",
                    p->x, p->y, p->x+dx, p->y+dy, dest_f);
            p->route_len = 0;
            break;
        }
        if (p->x == nx && p->y == ny) p->route_idx++;
    }
}

// Apply animation frame to player model
static void update_player_anim(ViewerState *v) {
    if (!v->anims) return;

    // Pick animation based on movement plus weapon BAS metadata when present.
    int target_anim = player_target_anim_id(v);

    // Switch animation
    if (target_anim != v->cur_anim_id) {
        v->cur_anim_id = target_anim;
        v->anim_frame_idx = 0;
        v->anim_frame_timer = 0;
    }

    AnimSequence *seq = anim_get_sequence(v->anims, (uint16_t)v->cur_anim_id);
    if (!seq || seq->frame_count == 0) return;

    // Advance frame timer
    v->anim_frame_timer += GetFrameTime() * 50.0f; // ~20ms per client tick
    AnimSequenceFrame *sf = &seq->frames[v->anim_frame_idx % seq->frame_count];
    float delay = (float)(sf->delay > 0 ? sf->delay : 1);
    while (v->anim_frame_timer >= delay) {
        v->anim_frame_timer -= delay;
        v->anim_frame_idx = (v->anim_frame_idx + 1) % seq->frame_count;
        sf = &seq->frames[v->anim_frame_idx];
        delay = (float)(sf->delay > 0 ? sf->delay : 1);
    }

    // Apply frame transforms
    AnimFrameBase *fb = anim_get_framebase(v->anims, sf->frame.framebase_id);
    if (!v->anim_state || !v->player_model || !v->player_model->loaded)
        return;
    ModelEntry *pe = &v->player_model->entries[0];
    if (fb && pe->loaded) {
        anim_apply_frame(v->anim_state, pe->base_verts, &sf->frame, fb);
        anim_update_mesh(pe->model.meshes[0].vertices, v->anim_state,
                         pe->face_indices, pe->face_priorities,
                         pe->face_count);
        // anim_update_mesh writes raw OSRS int16 units — scale to tile units
        float *mv = pe->model.meshes[0].vertices;
        int vc = pe->model.meshes[0].vertexCount;
        for (int i = 0; i < vc; i++) {
            mv[i*3]   /=  128.0f;
            mv[i*3+1] /=  128.0f;
            mv[i*3+2] /= -128.0f;
        }
        UpdateMeshBuffer(pe->model.meshes[0], 0, mv, vc * 3 * sizeof(float), 0);
    }
}

// Apply the correct animation frame to one NPC's shared mesh just before it
// draws. NPCs of the same def share the mesh buffer + AnimModelState, so the
// caller must animate and draw each instance sequentially (no batching across
// instances of the same type).
//
// Returns 1 if the mesh was updated (so caller knows it should draw), 0 if the
// NPC has no animation data and the caller should draw the rest pose.
static int update_npc_anim(ViewerState *v, int npc_idx, ModelEntry *me) {
    if ((!v->npc_anims && !v->npc_fallback_anims) || !me || !me->loaded)
        return 0;
    const RcNpc *n = &v->world->npcs[npc_idx];
    const RcNpcDef *def = &g_npc_defs[n->def_id];
    AnimModelState *state = v->npc_anim_state[n->def_id];
    if (!state) return 0;

    // Pick target anim from NPC state. stand/walk are always present; run,
    // attack, death are -1 on most non-combat NPCs, so fall back to walk/stand.
    int moved_last_tick = v->npc_render[npc_idx].moving
                        || v->npc_render[npc_idx].move_anim_timer > 0.0f;
    int target = def->stand_anim;
    if (n->is_dead && def->death_anim >= 0)       target = def->death_anim;
    else if (n->attack_anim_timer > 0) {
        if (n->combat.attack_animation_id > 0)
            target = n->combat.attack_animation_id;
        else
            target = def->attack_anim >= 0 ? def->attack_anim : target;
    }
    else if (moved_last_tick && def->walk_anim >= 0) target = def->walk_anim;
    if (target < 0) return 0;

    // Detect anim change → reset frame / timer.
    if (v->npc_render[npc_idx].cur_anim_id != target) {
        v->npc_render[npc_idx].cur_anim_id = target;
        v->npc_render[npc_idx].frame_idx   = 0;
        v->npc_render[npc_idx].frame_timer = 0.0f;
    }

    AnimCache *cache = v->npc_anims;
    AnimSequence *seq = anim_get_sequence(cache, (uint16_t)target);
    if (!seq && v->npc_fallback_anims) {
        cache = v->npc_fallback_anims;
        seq = anim_get_sequence(cache, (uint16_t)target);
    }
    if (!seq || seq->frame_count == 0) return 0;

    // Advance frame timer (20ms per client tick = GetFrameTime() * 50).
    v->npc_render[npc_idx].frame_timer += GetFrameTime() * 50.0f;
    AnimSequenceFrame *sf = &seq->frames[v->npc_render[npc_idx].frame_idx % seq->frame_count];
    float delay = (float)(sf->delay > 0 ? sf->delay : 1);
    while (v->npc_render[npc_idx].frame_timer >= delay) {
        v->npc_render[npc_idx].frame_timer -= delay;
        v->npc_render[npc_idx].frame_idx = (v->npc_render[npc_idx].frame_idx + 1) % seq->frame_count;
        sf = &seq->frames[v->npc_render[npc_idx].frame_idx];
        delay = (float)(sf->delay > 0 ? sf->delay : 1);
    }

    AnimFrameBase *fb = anim_get_framebase(cache, sf->frame.framebase_id);
    if (!fb) return 0;

    // Apply frame transforms to the shared per-def AnimModelState → base-pose
    // verts in OSRS int16 units. Then expand to face-unrolled float mesh verts
    // (applying raylib Y-flip), scale OSRS→tile units, re-upload.
    anim_apply_frame(state, me->base_verts, &sf->frame, fb);
    anim_update_mesh(me->model.meshes[0].vertices, state,
                     me->face_indices, me->face_priorities,
                     me->face_count);
    float *mv = me->model.meshes[0].vertices;
    int vc   = me->model.meshes[0].vertexCount;
    for (int i = 0; i < vc; i++) {
        mv[i*3]   /=  128.0f;
        mv[i*3+1] /=  128.0f;
        mv[i*3+2] /= -128.0f;
    }
    UpdateMeshBuffer(me->model.meshes[0], 0, mv, vc * 3 * sizeof(float), 0);
    return 1;
}

static void draw_scene(ViewerState *v) {
    RcPlayer *p = &v->world->player;
    const RcCombatViewState *combat_view = &v->combat_view;
    int scene_plane = viewer_scene_plane(v);
    float t = v->tick_frac;
    // Interpolate in world coords, then convert to local for rendering
    float wx = v->prev_player_x + ((float)p->x - v->prev_player_x) * t;
    float wy = v->prev_player_y + ((float)p->y - v->prev_player_y) * t;
    float px = (wx - g_world_origin_x) + 0.5f;
    float pz = -((wy - g_world_origin_y) + 0.5f);
    float py = ground_y_plane(v, scene_plane, p->x, p->y);

    if (v->camera_locked)
        v->camera.target = (Vector3){px, py, pz};
    v->camera.position = (Vector3){
        v->camera.target.x + v->cam_dist * cosf(v->cam_pitch) * sinf(v->cam_yaw),
        v->camera.target.y + v->cam_dist * sinf(v->cam_pitch),
        v->camera.target.z + v->cam_dist * cosf(v->cam_pitch) * cosf(v->cam_yaw)
    };

    BeginMode3D(v->camera);
    rlDisableBackfaceCulling();

    float frame_dt = GetFrameTime();
    models_update_texture_anims(v->item_models, frame_dt);
    models_update_texture_anims(v->npc_models, frame_dt);
    models_update_texture_anims(v->projectile_models, frame_dt);

    TerrainMesh *scene_terrain = viewer_terrain_for_plane(v, scene_plane);
    ObjectMesh *scene_objects = viewer_objects_for_plane(v, scene_plane);
    if (scene_terrain && scene_terrain->loaded)
        DrawModel(scene_terrain->model, (Vector3){0, 0, 0}, 1.0f, WHITE);
    if (scene_objects && scene_objects->loaded) {
        objects_update_texture_anims(scene_objects, frame_dt);
        DrawModel(scene_objects->model, (Vector3){0, 0, 0}, 1.0f, WHITE);
        draw_animated_objects(v, scene_plane, scene_objects);
    }

    // Player model. If equipment needs body-part replacement, use the generated
    // client-style identity-kit/equipment composition path so torso/arms/legs/
    // hands/feet/head/jaw visibility follows cache wearpos metadata. If no
    // body part is hidden, keep the original exported player base and layer the
    // equipped render model on top; this preserves the known-good default body
    // for partial hats and weapons.
    if (p->plane == scene_plane) {
        ModelEntry *pe = (v->player_model && v->player_model->loaded)
                       ? &v->player_model->entries[0] : NULL;
        float player_draw_angle = v->player_moving
                                ? v->player_facing_angle
                                : player_core_facing_angle(
                                      p, v->player_facing_angle);
        if (draw_composed_player_model(v, p, (Vector3){px, py, pz},
                                       player_draw_angle)) {
            // Client-style composed body/equipment appearance was drawn.
        } else if (pe && pe->loaded) {
            DrawModelEx(pe->model, (Vector3){px, py, pz}, (Vector3){0, 1, 0},
                        player_draw_angle, (Vector3){1, 1, 1}, WHITE);
            if (!draw_generated_equipped_item_models(
                    v, p, (Vector3){px, py, pz}, player_draw_angle)) {
                draw_raw_equipped_item_models(v, p, (Vector3){px, py, pz},
                                              player_draw_angle);
            }
        } else {
            int composed = draw_composed_player_model(
                v, p, (Vector3){px, py, pz}, player_draw_angle);
            if (composed) {
                // Fallback body/equipment parts were drawn.
            } else {
                DrawCube((Vector3){px, py + 1.0f, pz}, 0.8f, 2.0f, 0.8f,
                         BLUE);
                draw_raw_equipped_item_models(v, p, (Vector3){px, py, pz},
                                              player_draw_angle);
            }
        }
    }

    // NPC rendering — one entry per def. Each live NPC's def_id indexes into
    // g_npc_defs, and we look up the model by NPC cache ID in the model set.
    int npc_count = 0;
    const RcNpc *npcs = rc_get_npcs(v->world, &npc_count);
    for (int i = 0; i < npc_count; i++) {
        const RcNpc *n = &npcs[i];
        if (!n->active || n->is_dead) continue;
        if (n->plane != scene_plane) continue;

        RcNpcDef *def = &g_npc_defs[n->def_id];

        float nwx = v->npc_render[i].initialized
                  ? v->npc_render[i].render_x : (float)n->x;
        float nwy = v->npc_render[i].initialized
                  ? v->npc_render[i].render_y : (float)n->y;
        float nx_r = (nwx - g_world_origin_x) + 0.5f * (float)def->size;
        float nz_r = -((nwy - g_world_origin_y) + 0.5f * (float)def->size);
        float ny_r = ground_yf_plane(v, scene_plane, nwx, nwy);

        // Find the NPC's model by its cache ID
        ModelEntry *ne = NULL;
        if (v->npc_models && v->npc_models->loaded) {
            ne = model_find(v->npc_models, (uint32_t)def->id);
        }

        if (ne && ne->loaded) {
            // Target-facing wins during interactions; otherwise movement
            // direction becomes the idle-facing direction.
            float face_angle = 0.0f;
            int dx = n->x - n->prev_x;
            int dy = n->y - n->prev_y;
            if (n->facing_entity >= 0) {
                face_angle = npc_core_facing_angle(n, face_angle);
            } else if (v->npc_render[i].moving
                    && (v->npc_render[i].last_dx || v->npc_render[i].last_dy)) {
                face_angle = face_angle_between_tiles(0, 0,
                                                      v->npc_render[i].last_dx,
                                                      v->npc_render[i].last_dy,
                                                      face_angle);
            } else if (dx || dy) {
                face_angle = face_angle_between_tiles(n->prev_x, n->prev_y,
                                                      n->x, n->y, face_angle);
            } else {
                face_angle = npc_core_facing_angle(n, face_angle);
            }
            // Animate into the shared mesh buffer just before drawing this
            // instance — two NPCs with the same model can play different anims
            // or frames because each draw re-applies from base_verts.
            update_npc_anim(v, i, ne);
            DrawModelEx(ne->model, (Vector3){nx_r, ny_r, nz_r},
                        (Vector3){0, 1, 0}, face_angle,
                        (Vector3){NPC_MODEL_SCALE, NPC_MODEL_SCALE, NPC_MODEL_SCALE},
                        WHITE);
        }
    }

    for (int i = 0; i < v->world->ground_item_count; i++) {
        const RcGroundItem *g = &v->world->ground_items[i];
        if (!g->active || g->plane != scene_plane) continue;
        float gx = (float)LOCAL_X(g->x) + 0.5f;
        float gz = -((float)LOCAL_Y(g->y) + 0.5f);
        float gy = ground_y_plane(v, scene_plane, g->x, g->y) + 0.08f;
        if (!draw_ground_item_model(v, g, (Vector3){gx, gy, gz})) {
            DrawCube((Vector3){gx, gy, gz}, 0.35f, 0.08f, 0.35f,
                     (Color){235, 190, 55, 255});
        }
    }

    draw_combat_projectiles(v);

    rlEnableBackfaceCulling();

    // Route markers (convert world→local for rendering)
    if (p->plane == scene_plane && p->route_idx < p->route_len) {
        for (int i = p->route_idx; i < p->route_len; i++) {
            float rx = (float)LOCAL_X(p->route_x[i]) + 0.5f;
            float rz = -((float)LOCAL_Y(p->route_y[i]) + 0.5f);
            float ry = ground_y_plane(v, p->plane, p->route_x[i],
                                      p->route_y[i]) + 0.05f;
            DrawCube((Vector3){rx, ry, rz}, 0.3f, 0.05f, 0.3f, YELLOW);
        }
    }

    // Collision overlay (C key) — shows blocked tiles and wall flags
    if (v->show_collision) {
        // Show all tiles in the world
        for (int wx = g_world_origin_x; wx < g_world_origin_x + g_world_w; wx++) {
            for (int wy = g_world_origin_y; wy < g_world_origin_y + g_world_h; wy++) {
                uint32_t f = rc_get_flags(&v->world->map, wx, wy,
                                          scene_plane);
                if (f == 0) continue;
                float tx = (float)LOCAL_X(wx) + 0.5f;
                float tz = -((float)LOCAL_Y(wy) + 0.5f);
                float ty = ground_y_plane(v, scene_plane, wx, wy) + 0.1f;
                // Blocked tiles = red
                if (f & (COL_BLOCK_WALK | COL_LOC))
                    DrawCube((Vector3){tx, ty, tz}, 0.9f, 0.05f, 0.9f, (Color){255,0,0,120});
                // Wall flags = colored lines on tile edges
                float e = 0.5f;
                if (f & COL_WALL_N) DrawLine3D((Vector3){tx-e,ty,tz-e}, (Vector3){tx+e,ty,tz-e}, YELLOW);
                if (f & COL_WALL_S) DrawLine3D((Vector3){tx-e,ty,tz+e}, (Vector3){tx+e,ty,tz+e}, YELLOW);
                if (f & COL_WALL_E) DrawLine3D((Vector3){tx+e,ty,tz-e}, (Vector3){tx+e,ty,tz+e}, YELLOW);
                if (f & COL_WALL_W) DrawLine3D((Vector3){tx-e,ty,tz-e}, (Vector3){tx-e,ty,tz+e}, YELLOW);
            }
        }
    }

    // Grid
    if (v->show_grid) {
        Color gc = {80, 80, 80, 60};
        for (int x = 0; x <= g_world_w; x += 8)
            DrawLine3D((Vector3){(float)x, 0.02f, 0}, (Vector3){(float)x, 0.02f, -(float)g_world_h}, gc);
        for (int z = 0; z <= g_world_h; z += 8)
            DrawLine3D((Vector3){0, 0.02f, -(float)z}, (Vector3){(float)g_world_w, 0.02f, -(float)z}, gc);
    }

    EndMode3D();

    for (int i = 0; i < npc_count; i++) {
        const RcNpc *n = &npcs[i];
        if (!n->active || n->is_dead || n->plane != scene_plane) continue;
        const RcNpcDef *def = &g_npc_defs[n->def_id];
        float nwx = v->npc_render[i].initialized
                  ? v->npc_render[i].render_x : (float)n->x;
        float nwy = v->npc_render[i].initialized
                  ? v->npc_render[i].render_y : (float)n->y;
        float nx_r = (nwx - g_world_origin_x) + 0.5f * (float)def->size;
        float nz_r = -((nwy - g_world_origin_y) + 0.5f * (float)def->size);
        float ny_r = ground_yf_plane(v, scene_plane, nwx, nwy) + 2.15f;
        Vector2 s = GetWorldToScreen((Vector3){nx_r, ny_r, nz_r}, v->camera);
        int is_target = combat_view->target.kind == RC_COMBAT_ACTOR_NPC &&
                        combat_view->target.uid == n->uid;
        int hp_now = is_target ? combat_view->target_hp_current : n->combat.hp_current;
        int hp_max = is_target ? combat_view->target_hp_max : n->combat.hp_max;
        if (hp_max <= 0) hp_max = def->hitpoints;
        if (hp_max > 0 && (is_target || hp_now < hp_max)) {
            float w = 42.0f;
            float h = 5.0f;
            float pct = (float)hp_now / (float)hp_max;
            if (pct < 0.0f) pct = 0.0f;
            if (pct > 1.0f) pct = 1.0f;
            DrawRectangle((int)(s.x - w * 0.5f), (int)(s.y - 14), (int)w,
                          (int)h, (Color){64, 16, 14, 230});
            DrawRectangle((int)(s.x - w * 0.5f), (int)(s.y - 14),
                          (int)(w * pct), (int)h,
                          (Color){30, 180, 38, 235});
            DrawRectangleLines((int)(s.x - w * 0.5f), (int)(s.y - 14),
                               (int)w, (int)h, is_target ? YELLOW : BLACK);
        }
        const RcCombatActorState *state = &n->combat;
        for (int hit = 0; hit < state->recent_hit_count && hit < 4; hit++) {
            const RcCombatRecentHit *recent = &state->recent_hits[hit];
            if (recent->timer <= 0) continue;
            char text[12];
            snprintf(text, sizeof(text), "%d", recent->damage);
            Color fill = recent->hit_type == RC_HIT_TYPE_MISS
                       ? (Color){40, 44, 58, 235}
                       : (recent->hit_type == RC_HIT_TYPE_MAX
                          ? (Color){210, 38, 24, 245}
                          : (Color){150, 26, 20, 235});
            int x = (int)s.x + (hit - 1) * 18;
            int y = (int)(s.y - 30 - hit * 2);
            DrawCircle(x, y, 13.0f, fill);
            DrawCircleLines(x, y, 13.0f, BLACK);
            DrawText(text, x - 5, y - 8, 16, WHITE);
        }
    }

    Vector2 ps = GetWorldToScreen((Vector3){px, py + 2.25f, pz}, v->camera);
    for (int hit = 0; hit < p->combat.recent_hit_count && hit < 4; hit++) {
        const RcCombatRecentHit *recent = &p->combat.recent_hits[hit];
        if (recent->timer <= 0) continue;
        char text[12];
        snprintf(text, sizeof(text), "%d", recent->damage);
        Color fill = recent->hit_type == RC_HIT_TYPE_MISS
                   ? (Color){40, 44, 58, 235}
                   : (recent->hit_type == RC_HIT_TYPE_MAX
                      ? (Color){210, 38, 24, 245}
                      : (Color){150, 26, 20, 235});
        int x = (int)ps.x + (hit - 1) * 18;
        int y = (int)(ps.y - 30 - hit * 2);
        DrawCircle(x, y, 13.0f, fill);
        DrawCircleLines(x, y, 13.0f, BLACK);
        DrawText(text, x - 5, y - 8, 16, WHITE);
    }

    if (combat_view->target.kind == RC_COMBAT_ACTOR_NPC &&
            combat_view->target.uid >= 0 && combat_view->target_hp_max > 0) {
        char label[64];
        snprintf(label, sizeof(label), "Target %d",
                 combat_view->target.definition_id);
        RcNpc *target = viewer_find_npc_by_uid(v, combat_view->target.uid);
        if (target && target->def_id >= 0 && target->def_id < g_npc_def_count)
            snprintf(label, sizeof(label), "%.63s", g_npc_defs[target->def_id].name);
        float pct = (float)combat_view->target_hp_current /
                    (float)combat_view->target_hp_max;
        if (pct < 0.0f) pct = 0.0f;
        if (pct > 1.0f) pct = 1.0f;
        int x = GetScreenWidth() / 2 - 90;
        int y = 18;
        DrawRectangle(x, y, 180, 30, (Color){24, 18, 13, 210});
        DrawRectangleLines(x, y, 180, 30, (Color){155, 125, 60, 255});
        DrawText(label, x + 6, y + 4, 12, (Color){255, 152, 31, 255});
        DrawRectangle(x + 6, y + 20, 168, 5, (Color){64, 16, 14, 230});
        DrawRectangle(x + 6, y + 20, (int)(168.0f * pct), 5,
                      (Color){30, 180, 38, 235});
    }
}

int main(void) {
    ViewerState v = {0};
    int exit_status = 0;
    v.scene_plane_override = env_int("RUNEC_SCENE_PLANE", -1);
    v.minimap_tiles_plane = -1;
    g_world_origin_x = env_int("RUNEC_WORLD_ORIGIN_X", DEFAULT_WORLD_ORIGIN_X);
    g_world_origin_y = env_int("RUNEC_WORLD_ORIGIN_Y", DEFAULT_WORLD_ORIGIN_Y);
    g_player_start_x = env_int("RUNEC_PLAYER_START_X", DEFAULT_PLAYER_START_X);
    g_player_start_y = env_int("RUNEC_PLAYER_START_Y", DEFAULT_PLAYER_START_Y);
    g_world_w = env_int("RUNEC_WORLD_W", DEFAULT_WORLD_W);
    g_world_h = env_int("RUNEC_WORLD_H", DEFAULT_WORLD_H);
    v.scene_auto_export = env_bool("RUNEC_SCENE_AUTO_EXPORT", 1);
    v.scene_radius_regions = env_int("RUNEC_SCENE_RADIUS_REGIONS", 1);
    if (v.scene_radius_regions < 0)
        v.scene_radius_regions = 0;
    if (!runtime_data_available())
        return 1;

    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_INVENTORY | RC_SUB_EQUIPMENT | RC_SUB_LOOT |
                     RC_SUB_COMBAT | RC_SUB_PRAYER | RC_SUB_OBJECTS |
                     RC_SUB_REGIONS | RC_SUB_TRAVERSAL;
    cfg.items_path = env_path("RUNEC_ITEMS", "data/defs/items.bin");
    cfg.prayers_path = env_path("RUNEC_PRAYERS", "data/defs/prayers.bin");
    cfg.spells_path = env_path("RUNEC_SPELLS", "data/defs/spells.bin");
    cfg.combat_visuals_path = env_path("RUNEC_COMBAT_VISUALS",
        "data/defs/combat_visuals.tsv");
    cfg.player_actions_path = env_path("RUNEC_PLAYER_ACTIONS",
        "data/defs/player_actions.bin");
    cfg.object_defs_path = env_path("RUNEC_OBJECT_DEFS",
        "data/defs/object_defs.bin");
    cfg.object_placements_path = env_path("RUNEC_OBJECT_PLACEMENTS",
        "data/defs/object_placements.bin");
    cfg.object_behaviors_path = env_path("RUNEC_OBJECT_BEHAVIORS",
        "data/defs/object_behaviors.bin");
    cfg.object_transports_path = env_path("RUNEC_OBJECT_TRANSPORTS",
        "data/defs/object_transports.bin");
    cfg.collision_tiles_path = env_path("RUNEC_COLLISION_TILES",
        "data/defs/collision_tiles.bin");
    cfg.area_flags_path = env_path("RUNEC_AREA_FLAGS",
        "data/defs/area_flags.bin");
    cfg.traversal_edges_path = env_path("RUNEC_TRAVERSAL_EDGES",
        "data/defs/traversal_edges.bin");
    cfg.seed = 12345;
    v.world = rc_world_create_config(&cfg);
    if (!v.world) { fprintf(stderr, "Failed to create world\n"); return 1; }
    // Register all OSRS content modules (boss scripts, etc.). See
    // rc-content/README.md for the engine/content split.
    rc_content_register_all(v.world);

    v.world->player.x = g_player_start_x;
    v.world->player.y = g_player_start_y;
    v.world->player.prev_x = g_player_start_x;
    v.world->player.prev_y = g_player_start_y;
    v.prev_player_x = (float)g_player_start_x;
    v.prev_player_y = (float)g_player_start_y;
    set_viewer_demo_stats(&v.world->player);
    seed_viewer_inventory(v.world);

    const char *quiet_log = getenv("RC_VIEWER_QUIET");
    if (quiet_log && quiet_log[0] && strcmp(quiet_log, "0") != 0)
        SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(WINDOW_W, WINDOW_H,
               env_path("RUNEC_VIEWER_TITLE", "RuneC Viewer"));
    SetTargetFPS(60);
    runec_ui_init(&v.ui);

    int dynamic_model_shader_enabled =
        env_bool("RUNEC_DYNAMIC_MODEL_SHADER", 1);

    v.alpha_cutout_shader_static = load_alpha_cutout_shader(1.16f, 0.04f);
    v.alpha_cutout_shader_static_loaded = v.alpha_cutout_shader_static.id > 0;
    if (dynamic_model_shader_enabled) {
        v.alpha_cutout_shader_dynamic = load_alpha_cutout_shader(1.10f, 0.09f);
        v.alpha_cutout_shader_dynamic_loaded = v.alpha_cutout_shader_dynamic.id > 0;
    } else {
        fprintf(stderr,
                "dynamic model shader disabled by RUNEC_DYNAMIC_MODEL_SHADER=0\n");
    }

    v.cam_yaw = 0; v.cam_pitch = 0.6f; v.cam_dist = 50;
    v.camera_locked = 1;
    v.camera.up = (Vector3){0, 1, 0};
    v.camera.fovy = 45;
    v.camera.projection = CAMERA_PERSPECTIVE;

    // No custom lighting shader — the export scripts already bake directional
    // lighting into vertex colors. Adding another pass just darkens everything.

    // Load world. Plane-specific files are optional: if
    // data/regions/varrock.p1.terrain or RUNEC_TERRAIN_P1 exists, scene
    // rendering can follow the selected plane while legacy single-plane assets
    // continue to load as plane 0 fallback.
    const char *initial_terrain = env_path("RUNEC_TERRAIN",
        "data/regions/varrock.terrain");
    const char *initial_objects = env_path("RUNEC_OBJECTS",
        "data/regions/varrock.objects");
    strncpy(v.initial_terrain_path, initial_terrain,
            sizeof(v.initial_terrain_path) - 1);
    strncpy(v.initial_objects_path, initial_objects,
            sizeof(v.initial_objects_path) - 1);
    v.initial_scene_origin_x = g_world_origin_x;
    v.initial_scene_origin_y = g_world_origin_y;
    v.initial_scene_w = g_world_w;
    v.initial_scene_h = g_world_h;
    v.initial_scene_ready = 1;
    load_terrain_plane_assets(&v, initial_terrain);
    load_object_plane_assets(&v, initial_objects);

    // Load collision
    collision_load(&v.world->map, env_path("RUNEC_CMAP",
        "data/regions/varrock.cmap"));
    build_minimap_tiles(&v);
    load_world_map_minimap(&v);

    // Load NPC definitions + spawns (must be before model loading since spawns
    // tell us which NPCs exist in the world)
    rc_load_npc_defs(env_path("RUNEC_NPC_DEFS", "data/defs/npc_defs.bin"));
    const char *npc_spawns_path = env_path("RUNEC_NPC_SPAWNS",
        "data/spawns/world.npc-spawns.bin");
    RcNpcSpawnLoadStats spawn_stats;
    if (env_bool("RUNEC_NPC_SPAWNS_SLICE", 1)) {
        int spawned = rc_load_npc_spawns_rect_stats(
            v.world, npc_spawns_path,
            g_world_origin_x, g_world_origin_y,
            g_world_origin_x + g_world_w - 1,
            g_world_origin_y + g_world_h - 1,
            0, RC_MAX_PLANES - 1, &spawn_stats);
        if (spawned < 0 && !getenv("RUNEC_NPC_SPAWNS")) {
            rc_load_npc_spawns(v.world, "data/regions/varrock.npc-spawns.bin");
        } else if (spawned >= 0) {
            fprintf(stderr,
                    "viewer npc slice: rows=%d matched=%d spawned=%d"
                    " planes=[%d,%d,%d,%d]\n",
                    spawn_stats.total_rows, spawn_stats.matched_filter,
                    spawn_stats.spawned,
                    spawn_stats.spawned_plane_counts[0],
                    spawn_stats.spawned_plane_counts[1],
                    spawn_stats.spawned_plane_counts[2],
                    spawn_stats.spawned_plane_counts[3]);
        }
    } else {
        rc_load_npc_spawns(v.world, npc_spawns_path);
    }

    // Load NPC models (combined body parts per NPC, one model entry per NPC def)
    uint32_t *npc_model_ids = calloc((size_t)v.world->npc_count, sizeof(uint32_t));
    int npc_model_id_count = 0;
    if (npc_model_ids) {
        npc_model_id_count = collect_spawned_npc_model_ids(
            v.world, npc_model_ids, v.world->npc_count, 0, RC_MAX_PLANES - 1);
    }
    uint32_t empty_model_ids[1] = {0};
    const uint32_t *model_filter = npc_model_ids ? npc_model_ids : empty_model_ids;
    v.npc_models = models_load_filtered(
        env_path("RUNEC_NPC_MODELS", "data/models/npcs.models"),
        model_filter, npc_model_id_count);
    free(npc_model_ids);
    if (v.npc_models && v.alpha_cutout_shader_dynamic_loaded)
        models_set_shader(v.npc_models, v.alpha_cutout_shader_dynamic);

    // NPC animations (separate cache — player.anims has combat/player anims,
    // npcs.anims has the subset referenced by our loaded NPC defs). Each
    // unique NPC def gets its own AnimModelState built from its base model's
    // per-vertex skin labels.
    v.npc_anims = anim_cache_load(env_path("RUNEC_NPC_ANIMS",
        "data/anims/npcs.anims"));
    v.npc_fallback_anims = anim_cache_load(env_path("RUNEC_NPC_FALLBACK_ANIMS",
        "data/anims/all.anims"));
    if ((v.npc_anims || v.npc_fallback_anims) &&
            v.npc_models && v.npc_models->loaded) {
        int created = 0;
        for (int i = 0; i < g_npc_def_count; i++) {
            ModelEntry *me = model_find(v.npc_models, (uint32_t)g_npc_defs[i].id);
            if (me && me->loaded && me->vertex_skins && me->base_vert_count > 0) {
                v.npc_anim_state[i] = anim_model_state_create(
                    me->vertex_skins, me->base_vert_count);
                created++;
            }
        }
        fprintf(stderr, "npc_anim: created %d per-def anim states\n", created);
    }

    // Load player model + animations
    v.player_model = models_load(env_path("RUNEC_PLAYER_MODELS",
        "data/models/player.models"));
    if (v.player_model && v.alpha_cutout_shader_dynamic_loaded)
        models_set_shader(v.player_model, v.alpha_cutout_shader_dynamic);
    v.item_models = models_load(env_path("RUNEC_ITEM_MODELS",
        "data/models/items.models"));
    if (v.item_models && v.alpha_cutout_shader_dynamic_loaded)
        models_set_shader(v.item_models, v.alpha_cutout_shader_dynamic);
    v.projectile_models = models_load(env_path("RUNEC_PROJECTILE_MODELS",
        "data/models/projectiles.models"));
    if (v.projectile_models && v.alpha_cutout_shader_static_loaded)
        models_set_shader(v.projectile_models, v.alpha_cutout_shader_static);
    create_projectile_anim_states(&v);
    v.spotanims = spotanims_load(env_path("RUNEC_SPOTANIMS",
        "data/defs/spotanims.bin"));
    runec_item_render_map_load(&v.item_render_map, env_path("RUNEC_ITEM_RENDER_MAP",
        "data/models/item_render.map"));
    load_item_stack_variants(&v, env_path("RUNEC_ITEM_STACK_VARIANTS",
        "data/sprites/items/item_stack_variants.tsv"));
    create_item_anim_states(&v);
    build_ui_item_icons(&v);
    v.anims = anim_cache_load(env_path("RUNEC_PLAYER_ANIMS",
        "data/anims/player.anims"));
    if (!v.anims) {
        v.anims = anim_cache_load(env_path("RUNEC_FALLBACK_ANIMS",
            "data/anims/all.anims"));
    }

    if (v.player_model && v.player_model->loaded && v.player_model->entries[0].loaded) {
        ModelEntry *pe = &v.player_model->entries[0];
        // Don't apply lighting shader to player — the animation system rewrites
        // mesh vertices each frame in OSRS units, then the shader's mvp transforms
        // them. The default shader handles this correctly.
        v.anim_state = anim_model_state_create(pe->vertex_skins, pe->base_vert_count);
        v.cur_anim_id = ANIM_IDLE;
    }

    // Verify collision is working
    {
        int blocked = 0;
        int max_x = g_world_w < 64 ? g_world_w : 64;
        int max_y = g_world_h < 64 ? g_world_h : 64;
        for (int x = 0; x < max_x; x++)
            for (int y = 0; y < max_y; y++)
                if (rc_get_flags(&v.world->map, g_world_origin_x + x,
                                 g_world_origin_y + y,
                                 v.world->player.plane) & 0x200000)
                    blocked++;
        fprintf(stderr, "Collision check: first %dx%d tiles have %d blocked tiles\n",
                max_x, max_y, blocked);
    }

    fprintf(stderr, "Viewer ready. Player at world (%d, %d), local (%d, %d)\n",
            v.world->player.x, v.world->player.y,
            LOCAL_X(v.world->player.x), LOCAL_Y(v.world->player.y));

    const char *dev_transport_dest = getenv("RUNEC_DEV_TRANSPORT_DEST");
    const ViewerDevTransport *dev_transport =
        find_dev_transport(dev_transport_dest);
    if (dev_transport)
        viewer_dev_transport_to(&v, dev_transport);
    else if (dev_transport_dest && dev_transport_dest[0])
        fprintf(stderr, "dev transport: unknown destination '%s'\n",
                dev_transport_dest);

    const char *ui_selftest = getenv("RUNEC_UI_RUNTIME_SELFTEST");
    if (ui_selftest && ui_selftest[0] && strcmp(ui_selftest, "0") != 0) {
        char error[256] = {0};
        sync_ui_items(&v);
        sync_ui_player_status(&v);
        sync_ui_minimap(&v);
        if (!runec_ui_runtime_selftest(&v.ui, error, sizeof(error))) {
            fprintf(stderr, "ui runtime selftest: FAIL: %s\n", error);
            exit_status = 1;
        } else {
            fprintf(stderr, "ui runtime selftest: PASS\n");
        }
        goto cleanup;
    }

    int max_frames = 0;
    const char *exit_frames = getenv("RC_VIEWER_EXIT_FRAMES");
    if (exit_frames) max_frames = atoi(exit_frames);
    const char *screenshot_path = getenv("RC_VIEWER_SCREENSHOT");
    int screenshot_taken = 0;
    int frame_count = 0;

    while (!WindowShouldClose()) {
        RcPlayer *p = &v.world->player;
        runec_ui_sync_status(&v.ui, p->x, p->y, LOCAL_X(p->x), LOCAL_Y(p->y),
                             (uint32_t)v.world->tick, p->running, v.paused);
        sync_ui_items(&v);
        sync_ui_player_status(&v);
        int ui_capture = runec_ui_handle_input(&v.ui, GetScreenWidth(), GetScreenHeight());
        if (v.ui.last_intent.kind == RUNEC_UI_INTENT_RUN_TOGGLE) {
            p->running = !p->running;
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_MINIMAP_CLICK) {
            int dx = (v.ui.last_intent.primary - 72) / 4;
            int dy = (75 - v.ui.last_intent.secondary) / 4;
            route_player_to(&v, p->x + dx, p->y + dy);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_INVENTORY_SLOT) {
            int slot = v.ui.last_intent.primary;
            int previous = v.ui.last_intent.secondary;
            if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
                rc_player_drop_item(v.world, slot);
            } else if (slot >= 0 && slot < RC_INVENTORY_SIZE
                    && p->inventory[slot].item_id >= 0) {
                const RcItemDef *def = rc_item_def_get(p->inventory[slot].item_id);
                if (def && def->equippable)
                    rc_player_equip(v.world, slot);
                else if (previous >= 0 && previous != slot)
                    rc_player_move_inventory_item(v.world, previous, slot);
            } else if (previous >= 0 && previous != slot) {
                rc_player_move_inventory_item(v.world, previous, slot);
            }
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_INVENTORY_DRAG) {
            int from = v.ui.last_intent.primary;
            int to = v.ui.last_intent.secondary;
            if (from >= 0 && to >= 0 && from != to)
                rc_player_move_inventory_item(v.world, from, to);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_INVENTORY_ACTION) {
            int slot = v.ui.last_intent.primary;
            if (strcmp(v.ui.last_intent.text, "Drop") == 0) {
                rc_player_drop_item(v.world, slot);
            } else if (strcmp(v.ui.last_intent.text, "Examine") == 0
                    && slot >= 0 && slot < RC_INVENTORY_SIZE
                    && p->inventory[slot].item_id >= 0) {
                rc_player_interact_inventory_item(v.world, slot, 1);
                const RcItemDef *def = rc_item_def_get(p->inventory[slot].item_id);
                fprintf(stderr, "ui examine item: %s (%d)\n",
                        def ? def->name : "unknown", p->inventory[slot].item_id);
            }
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_EQUIPMENT_SLOT) {
            int core_slot = ui_equip_slot_to_core(v.ui.last_intent.primary);
            if (core_slot >= 0)
                rc_player_unequip(v.world, core_slot);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_EQUIPMENT_ACTION) {
            int core_slot = ui_equip_slot_to_core(v.ui.last_intent.primary);
            if (core_slot >= 0 && strcmp(v.ui.last_intent.text, "Remove") == 0) {
                rc_player_unequip(v.world, core_slot);
            } else if (core_slot >= 0
                    && strcmp(v.ui.last_intent.text, "Examine") == 0) {
                rc_player_interact_equipment_item(v.world, core_slot, 1);
                const RcItemDef *def =
                    rc_item_def_get(p->equipment[core_slot].item_id);
                fprintf(stderr, "ui examine equipment: %s (%d)\n",
                        def ? def->name : "unknown",
                        p->equipment[core_slot].item_id);
            }
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_COMBAT_STYLE) {
            rc_combat_set_player_style(v.world, v.ui.last_intent.primary);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_AUTO_RETALIATE) {
            if ((p->auto_retaliate ? 1 : 0) != v.ui.last_intent.primary)
                rc_combat_toggle_auto_retaliate(v.world);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_SPECIAL_ATTACK) {
            if ((p->combat.special_pending ? 1 : 0) != v.ui.last_intent.primary)
                rc_combat_toggle_special(v.world);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_PRAYER_SLOT) {
            rc_player_set_prayer(v.world, v.ui.last_intent.primary);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_QUICK_PRAYER_SLOT) {
            fprintf(stderr, "ui quick-prayer slot hook: %d (%s)\n",
                    v.ui.last_intent.primary, v.ui.last_intent.text);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_QUICK_PRAYER_TOGGLE) {
            fprintf(stderr, "ui quick-prayer toggle hook\n");
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_SELECTED_SPELL) {
            int spell_idx = rc_spell_find(v.ui.last_intent.text);
            if (spell_idx >= 0)
                rc_player_select_spell(v.world, spell_idx);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_AUTOCAST_SPELL) {
            int spell_idx = rc_spell_find(v.ui.last_intent.text);
            if (spell_idx >= 0)
                rc_player_set_autocast_spell(v.world, spell_idx, 0);
            fprintf(stderr, "ui autocast hook: %s\n", v.ui.last_intent.text);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_SELECTED_ITEM_ON_ITEM) {
            rc_player_use_inventory_item_on_inventory_item(
                v.world, v.ui.last_intent.primary, v.ui.last_intent.secondary);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_SELECTED_SPELL_ON_ITEM) {
            int spell_idx = selected_spell_id_for_viewer(&v);
            if (spell_idx >= 0) {
                rc_player_cast_spell_on_inventory_item(
                    v.world, spell_idx, v.ui.last_intent.secondary);
            }
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_SELECTED_ITEM_ON_COMPONENT) {
            uint32_t component_id = (uint32_t)v.ui.last_intent.secondary;
            rc_player_use_inventory_item_on_widget(
                v.world, v.ui.last_intent.primary,
                ui_component_group(component_id),
                ui_component_child(component_id));
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_SELECTED_SPELL_ON_COMPONENT) {
            int spell_idx = selected_spell_id_for_viewer(&v);
            if (spell_idx >= 0) {
                uint32_t component_id = (uint32_t)v.ui.last_intent.secondary;
                rc_player_cast_spell_on_widget(v.world, spell_idx,
                                               ui_component_group(component_id),
                                               ui_component_child(component_id));
            }
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_COMPONENT_ACTION) {
            uint32_t component_id = (uint32_t)v.ui.last_intent.primary;
            rc_player_widget_action(v.world, ui_component_group(component_id),
                                    ui_component_child(component_id),
                                    v.ui.last_intent.secondary);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_CHAT_SEND) {
            fprintf(stderr, "ui chat send hook: %s\n", v.ui.last_intent.text);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_CONTEXT_ACTION) {
            handle_context_intent(&v);
        }
        handle_input(&v, ui_capture);

        // Animation (runs every frame, independent of ticks)
        update_player_anim(&v);

        // Tick
        if (!v.paused) {
            v.tick_acc += GetFrameTime() * TPS;
            if (v.tick_acc >= 1.0f) {
                v.tick_acc -= 1.0f;
                v.prev_player_x = (float)v.world->player.x;
                v.prev_player_y = (float)v.world->player.y;

                int old_x = v.world->player.x;
                int old_y = v.world->player.y;
                int old_plane = v.world->player.plane;
                rc_world_tick(v.world);
                v.player_moving = old_x != v.world->player.x ||
                                  old_y != v.world->player.y;
                if (v.player_moving) {
                    int dx = v.world->player.x - old_x;
                    int dy = v.world->player.y - old_y;
                    v.player_facing_angle =
                        atan2f((float)dx, -(float)dy) * (180.0f / 3.14159f);
                }
                handle_player_scene_transition(&v, old_x, old_y, old_plane);
                v.tick_frac = 0.0f;
            }
            v.tick_frac = v.tick_acc;
            if (v.tick_frac > 1.0f) v.tick_frac = 1.0f;
        }
        update_npc_render_motion(&v, v.paused ? 0.0f : GetFrameTime());

        BeginDrawing();
        ClearBackground((Color){40, 45, 55, 255});
        draw_scene(&v);
        runec_ui_sync_status(&v.ui, p->x, p->y, LOCAL_X(p->x), LOCAL_Y(p->y),
                             (uint32_t)v.world->tick, p->running, v.paused);
        sync_ui_items(&v);
        build_ui_item_icons(&v);
        sync_ui_player_status(&v);
        sync_ui_minimap(&v);
        runec_ui_draw(&v.ui, GetScreenWidth(), GetScreenHeight());
        draw_scene_plane_controls(&v);
        draw_dev_transport_controls(&v);
        EndDrawing();
        if (screenshot_path && screenshot_path[0] && !screenshot_taken) {
            Image screenshot = LoadImageFromScreen();
            if (screenshot.data) {
                ExportImage(screenshot, screenshot_path);
                UnloadImage(screenshot);
            }
            screenshot_taken = 1;
        }
        if (max_frames > 0 && ++frame_count >= max_frames) break;
    }

cleanup:
    for (int plane = 0; plane < RC_MAX_PLANES; plane++) {
        terrain_free(v.terrain_planes[plane]);
        objects_free(v.object_planes[plane]);
        free_object_anim_plane(&v, plane);
    }
    free_item_anim_states(&v);
    free_projectile_anim_states(&v);
    clear_composed_player_model(&v);
    models_free(v.player_model);
    models_free(v.npc_models);
    models_free(v.item_models);
    models_free(v.projectile_models);
    spotanims_free(v.spotanims);
    runec_item_render_map_free(&v.item_render_map);
    anim_model_state_free(v.anim_state);
    for (int i = 0; i < RC_MAX_NPC_DEFS; i++)
        anim_model_state_free(v.npc_anim_state[i]);
    anim_cache_free(v.anims);
    anim_cache_free(v.npc_anims);
    anim_cache_free(v.npc_fallback_anims);
    rc_world_destroy(v.world);
    runec_ui_shutdown(&v.ui);
    free(v.minimap_tiles);
    if (v.world_map_pixels)
        UnloadImageColors(v.world_map_pixels);
    if (v.alpha_cutout_shader_static_loaded)
        UnloadShader(v.alpha_cutout_shader_static);
    if (v.alpha_cutout_shader_dynamic_loaded)
        UnloadShader(v.alpha_cutout_shader_dynamic);
    CloseWindow();
    return exit_status;
}
