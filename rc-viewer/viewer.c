// RuneC Raylib viewer.

#include "../rc-core/api.h"
#include "../rc-core/assets.h"
#include "../rc-core/combat.h"
#include "../rc-core/config.h"
#include "../rc-core/consumables.h"
#include "../rc-core/encounter.h"
#include "../rc-core/items.h"
#include "../rc-core/objects.h"
#include "../rc-core/pathfinding.h"
#include "../rc-core/player_command.h"
#include "../rc-core/npc.h"
#include "../rc-core/spawn_index.h"
#include "../rc-core/spells.h"
#include "../rc-core/storage.h"
#include "../rc-content/content.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "asset_raylib.h"
#include "terrain.h"
#include "objects.h"
#include "models.h"
#include "anims.h"
#include "ui.h"
#include "equipment_render.h"
#include "equipment_slots.h"
#include "item_render_defs.h"
#include "npc_render_defs.h"
#include "object_action_visuals.h"
#include "spotanims.h"
#include "combat_visuals.h"
#include "dev_validation.h"
#include "minimap.h"
#include "hitsplats.h"
#include "health_bars.h"
#include "actor_projection.h"
#include "input_feedback.h"
#include "route_overlay.h"
#include "streaming.h"
#include "tick_pacing.h"
#include "streaming_async.h"
#include "world_transform.h"
#include <math.h>
#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_WORLD_ORIGIN_X 3072
#define DEFAULT_WORLD_ORIGIN_Y 3264
#define DEFAULT_PLAYER_START_X RUNEC_DEV_VARROCK_BANK_X
#define DEFAULT_PLAYER_START_Y RUNEC_DEV_VARROCK_BANK_Y
#define DEFAULT_WORLD_W \
    ((RC_WORLD_STREAMING_DEFAULT_ACTIVE_RADIUS * 2 + 1) * RC_REGION_SIZE)
#define DEFAULT_WORLD_H DEFAULT_WORLD_W
#define WINDOW_W 1280
#define WINDOW_H 720
#define TPS 1.667f
#define NPC_RENDER_QUEUE_MAX 8
#define NPC_MODEL_SCALE 1.0f
#define VIEWER_MAX_COMBAT_PROJECTILES 64
#define VIEWER_PROJECTILE_MODEL_FILTER_MAX 256
#define VIEWER_NPC_TZTOK_JAD 3127
#define VIEWER_JAD_MELEE_ANIM_TICKS 3
#define VIEWER_JAD_WARNING_ANIM_TICKS 5
#define VIEWER_CONTEXT_NPC 1
#define VIEWER_CONTEXT_OBJECT 2
#define VIEWER_CONTEXT_GROUND_ITEM 3
#define VIEWER_CONTEXT_TILE 4
#define VIEWER_CONTEXT_WALK_HERE -10
#define VIEWER_CONTEXT_EXAMINE -11
#define VIEWER_CONTEXT_CANCEL -12
#define VIEWER_CONTEXT_TAKE -13
#define VIEWER_CONTEXT_PREVIOUS -14
#define VIEWER_CONTEXT_MORE -15
#define VIEWER_CONTEXT_PAGE_ROWS (RUNEC_UI_CONTEXT_ACTIONS - 4)
#define VIEWER_HOVER_NONE 0
#define VIEWER_HOVER_NPC 1
#define VIEWER_HOVER_OBJECT 2
#define VIEWER_HOVER_GROUND_ITEM 3
#define VIEWER_HOVER_TILE 4
#define RUNEC_ITEM_STACK_VARIANT_MAX 4096
#define MODEL_ID_SPOTANIM_BASE 0xA2000000u
#define OBJECT_PICK_TILE_RADIUS 1
#define DEFAULT_B237_CACHE_ROOT "data"
#define DEFAULT_B237_CACHE_PATH DEFAULT_B237_CACHE_ROOT "/source/b237-openrs2-2528/cache"
#define VIEWER_NPC_SPAWN_INDEX_MAGIC 0x4950534Eu
#define VIEWER_NPC_SPAWN_RECORD_SIZE 20u
#define VIEWER_MOVEMENT_PREFETCH_EDGE_TILES 16

typedef struct {
    ObjectMesh *mesh;
    int origin_x;
    int origin_y;
    int size;
    char path[1024];
} ViewerObjectChunk;

typedef struct {
    int region_x;
    int region_y;
    int plane;
    int render_origin_x;
    int render_origin_y;
    TerrainMesh *terrain;
    ObjectMesh *objects;
    ModelSet *object_anim_models;
    AnimModelState **object_anim_states;
    int object_anim_state_count;
    Color *minimap_pixels;
    int minimap_width;
    int minimap_height;
} ViewerMapsquareChunk;

typedef enum {
    VIEWER_ASYNC_MAPSQUARE_CHUNK = 1,
    VIEWER_ASYNC_MATERIAL_PAGE,
    VIEWER_ASYNC_NPC_MODELS,
} ViewerAsyncMapsquareKind;

// Encounter forms and helpers appear mid-fight (a transform, a wake, an
// owned spawn), so their models load with every scene.
#define VIEWER_ENCOUNTER_ROLE_MODELS \
    (RC_ENC_REGISTRY_CAP * (RC_ENC_MAX_FORMS + RC_ENC_MAX_HELPERS))

typedef struct {
    ViewerAsyncMapsquareKind kind;
    int region_x;
    int region_y;
    int plane;
    char terrain_path[1024];
    char objects_path[1024];
    char models_path[1024];
    char minimap_path[1024];
    char material_path[1024];
    char npc_spawns_path[1024];
    int min_x, min_y, max_x, max_y;
    uint32_t extra_model_ids[RUNEC_UI_DEV_TRANSPORT_MAX + 1
                             + VIEWER_ENCOUNTER_ROLE_MODELS];
    int extra_model_id_count;
} ViewerAsyncMapsquareRequest;

typedef struct {
    ViewerAsyncMapsquareKind kind;
    ViewerMapsquareChunk chunk;
    ObjectMesh *materials;
    ModelSet *npc_models;
    size_t terrain_upload_bytes;
    size_t object_upload_bytes;
    size_t model_upload_bytes;
} ViewerAsyncMapsquarePayload;

typedef struct {
    ViewerMapsquareChunk chunk;
    size_t terrain_upload_bytes;
    size_t object_upload_bytes;
    size_t model_upload_bytes;
    int model_upload_cursor;
    int upload_stage;
} ViewerPendingMapsquareChunk;

typedef struct {
    int active;
    int failed;
    int prefetch_ready;
    int telemetry_finished;
    uint64_t generation;
    int center_x;
    int center_y;
    int plane;
    ViewerStreamingRequestKind request_kind;
    int plan_count;
    int visible_count;
    int loaded_count;
    int expected_results;
    int received_results;
    int staged_count;
    double started_ms;
    char reason[32];
    ViewerMapsquareCoord plan[VIEWER_STREAMING_CHUNK_CAPACITY];
    ViewerMapsquareCoord visible_plan[VIEWER_STREAMING_CHUNK_CAPACITY];
    ViewerPendingMapsquareChunk
        staged[VIEWER_STREAMING_CHUNK_CAPACITY];
    ObjectMesh *materials;
    ModelSet *npc_models;
    size_t npc_model_upload_bytes;
    int npc_model_upload_cursor;
    int needs_npc_models;
    int npc_models_ready;
    uint64_t process_frames;
    double main_thread_work_max_ms;
    ViewerStreamingPlayerTransition player_transition;
} ViewerPendingMapsquareLoad;

typedef struct {
    int active;
    int plan_count;
    int plane;
    ViewerMapsquareCoord plan[VIEWER_STREAMING_CHUNK_CAPACITY];
} ViewerDeferredMapsquareEviction;

typedef enum {
    VIEWER_SCENE_MODE_AUTO = 0,
    VIEWER_SCENE_MODE_GENERATED,
    VIEWER_SCENE_MODE_FIXED,
} ViewerSceneMode;

typedef struct {
    int obj_id;
    int x, y, plane;
    int width, length;
    uint64_t placement_key;
} ViewerPickedObject;

typedef struct {
    int kind;
    int option;
    int npc_uid;
    int ground_index, ground_uid, ground_version;
} ViewerContextEntry;

typedef struct {
    int kind;
    int npc_uid;
    ViewerPickedObject object;
    int ground_item_idx;
    int tile_x;
    int tile_y;
    int option;
    float score;
    char action_text[128];
} ViewerHoverTarget;

typedef struct {
    bool active;
    uint8_t source_kind;
    uint8_t target_kind;
    uint8_t style;
    uint8_t primitive_type;
    uint8_t source_attachment;
    uint8_t target_attachment;
    uint8_t launch_attachment;
    uint8_t impact_attachment;
    int source_uid;
    int target_uid;
    int source_x, source_y;     // tile holding the origin
    float source_fx, source_fy; // origin centre, world tiles (actor centre)
    int target_x, target_y;
    int plane;
    int weapon_item_id;
    int ammo_item_id;
    int spell_idx;
    int attack_anim_id;
    int launch_spotanim_id;
    int travel_spotanim_id;
    int impact_spotanim_id;
    int projectile_model_id;
    int projectile_anim_id;
    int start_tick;
    int duration_ticks;
    int impact_duration_ticks;
    int age_ticks;
    int hit_delay;
    int client_delay;
    int launch_spotanim_height;
    int impact_spotanim_height;
    int impact_spotanim_delay;
    int impact_spotanim_rotation;
    int projectile_start_height;
    int projectile_end_height;
    int projectile_start_time;
    int projectile_end_time;
    int projectile_angle;
    int projectile_progress;
    int sequence_index;
    int sequence_count;
    // Flight as the client steps it: position (world tiles, scene height),
    // vertical velocity (tiles per client cycle) and the client time of the
    // last step. Each step re-aims at the target's current position over
    // the remaining time.
    bool flying;
    float flight_x, flight_y, flight_h, flight_vh;
    float flight_time;
} ViewerCombatProjectile;

typedef struct {
    const char *key;
    int center_x;
    int center_y;
    int plane;
    int radius_regions;
} ViewerValidationScene;

typedef struct {
    RuneCHitsplatState hitsplats;
    RuneCHealthBarState health_bar;
} ViewerActorOverlayState;

static const ViewerValidationScene VIEWER_VALIDATION_SCENES[] = {
    {"graardor", 2872, 5358, 2, 1},
    {"kbd", 2269, 4697, 0, 1},
    {"vorkath", 2269, 4062, 0, 1},
    {"jad", 2400, 5088, 0, 1},
    {"edgeville_dungeon", 3117, 9852, 0, 1},
    {"varrock_rat_pits", 2894, 5097, 0, 1},
    {"varrock_sewer", 3237, 9858, 0, 1},
    {"wilderness_lever", 3154, 3924, 0, 1},
    {"observatory_ladder", 2465, 3495, 1, 1},
    {"yanille_railing", 2519, 3163, 0, 1},
};

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
    struct {
        ModelSet *models;
        int item_id;
        uint64_t last_use;
    } ground_models[RC_MAX_GROUND_ITEMS];
    uint64_t ground_model_clock;
    ModelSet *npc_models;
    ModelSet *item_models;
    ModelSet *projectile_models;
    SpotAnimSet *spotanims;
    RuneCItemRenderMap item_render_map;
    RuneCItemDefRenderMap item_def_render_map;
    RuneCNpcRenderDefs npc_render_defs;
    RuneCObjectActionVisualMap object_action_visuals;
    RuneCHitsplatCatalog hitsplat_catalog;
    Texture2D hitsplat_textures[RUNEC_HITSPLAT_MAX_DEFS];
    unsigned char hitsplat_texture_loaded[RUNEC_HITSPLAT_MAX_DEFS];
    ViewerActorOverlayState player_overlay;
    ViewerActorOverlayState *npc_overlays;
    ItemStackVariant item_stack_variants[RUNEC_ITEM_STACK_VARIANT_MAX];
    int item_stack_variant_count;
    AnimCache *anims;           // shared actor and effect sequences
    AnimCache *object_anim_cache;
    AnimModelState *anim_state; // player
    AnimModelState **item_anim_states;
    int item_anim_state_count;
    AnimModelState **projectile_anim_states;
    int projectile_anim_state_count;
    AnimModelState **object_anim_states[RC_MAX_PLANES];
    int object_anim_state_count[RC_MAX_PLANES];
    // Locs the core spawns at runtime: templates by loc id, type and
    // rotation (tools/cache_pipeline/export_spawn_objects.py).
    ObjectMesh *spawn_object_templates;
    ModelSet *spawn_object_models;
    AnimModelState **spawn_object_states;
    int spawn_object_state_count;
    // Gaps reported once: locs without a template, encounter actions
    // without a combat visual row.
    int missing_spawn_objects[16];
    int missing_spawn_object_count;
    uint32_t missing_npc_visuals[64];
    int missing_npc_visual_count;
    ModelEntry composed_player_model;
    AnimModelState *composed_anim_state;
    uint64_t composed_player_signature;
    int composed_player_loaded;
    // Per-NPC-def animation scratch state keyed by RcNpc.def_id.
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
        int uid;
        int server_x, server_y, server_plane;
        RcTick last_seen_tick;
        float render_x, render_y;
        float waypoint_x[NPC_RENDER_QUEUE_MAX];
        float waypoint_y[NPC_RENDER_QUEUE_MAX];
        int waypoint_count;
        int moving;
        float move_anim_timer;
        int last_dx, last_dy;
        int attack_anim_timer;
        int attack_anim_id;
    } npc_render[RC_MAX_NPCS];

    // Camera
    Camera3D camera;
    float cam_yaw, cam_pitch, cam_dist;
    int camera_locked;

    // Tick
    RcViewerTickPacing tick_pacing;
    float tick_frac;
    int paused;
    float prev_player_x, prev_player_y;

    // Animation
    int cur_anim_id;
    int anim_frame_idx;
    float anim_frame_timer;
    int player_one_shot_finished;
    int player_attack_anim_suppressed;
    int player_attack_anim_timer;
    int player_attack_anim_id;
    int player_action_anim_timer;
    int player_action_anim_id;
    int player_moving;
    float player_facing_angle;   // viewer-side; rc-core doesn't store

    int show_grid;
    int show_collision;
    int show_npc_footprints;
    int god_mode;
    RuneCUiState ui;
    RuneCInputFeedback input_feedback;
    RcRoute route_preview;
    int route_preview_active;
    int route_preview_start_x;
    int route_preview_start_y;
    int route_preview_plane;
    RcCombatViewState combat_view;
    const char *reported_combat_failure;
    ViewerCombatProjectile combat_projectiles[VIEWER_MAX_COMBAT_PROJECTILES];
    int combat_projectile_count;
    uint32_t projectile_model_request_ids[VIEWER_PROJECTILE_MODEL_FILTER_MAX];
    int projectile_model_request_id_count;
    Shader alpha_cutout_shader_static;
    Shader alpha_cutout_shader_dynamic;
    Shader projectile_effect_shader;
    int alpha_cutout_shader_static_loaded;
    int alpha_cutout_shader_dynamic_loaded;
    int projectile_effect_shader_loaded;
    ViewerContextEntry context_entries[RUNEC_UI_CONTEXT_ACTIONS];
    ViewerPickedObject context_object;
    Ray context_ray;
    int context_page;
    int context_plane;
    int context_tile_x;
    int context_tile_y;
    uint64_t interaction_outcome_seen;
    uint64_t consume_outcome_seen;
    uint64_t bank_outcome_seen;
    uint64_t consume_sequence_seen;
    uint64_t traversal_event_seen;
    uint64_t traversal_outcome_seen;
    uint64_t prayer_outcome_seen;
    uint64_t traversal_generation;
    int scene_right_tracking;
    int scene_right_dragged;
    Vector2 scene_right_start;
    Color *minimap_tiles;
    int minimap_tiles_w;
    int minimap_tiles_h;
    int minimap_tiles_plane;
    ViewerStreamingConfig streaming;
    ViewerStreamingTelemetry telemetry;
    double pending_cpu_decode_ms;
    double pending_gpu_upload_ms;
    size_t frame_gpu_upload_bytes;
    size_t peak_gpu_upload_bytes;
    int telemetry_overlay;
    int scene_plane_override;
    int scene_auto_export;
    int preload_scene_planes;
    int object_chunk_size;
    float object_chunk_draw_radius;
    float actor_draw_radius;
    ViewerObjectChunk
        object_chunks[RC_MAX_PLANES][VIEWER_STREAMING_CHUNK_CAPACITY];
    int object_chunk_count[RC_MAX_PLANES];
    Texture2D object_chunk_atlas[RC_MAX_PLANES];
    int object_chunk_atlas_loaded[RC_MAX_PLANES];
    ViewerMapsquareChunk
        mapsquare_chunks[VIEWER_STREAMING_CHUNK_CAPACITY];
    int mapsquare_chunk_count;
    ViewerMapsquareCatalog mapsquare_catalog;
    ObjectMesh *mapsquare_materials;
    ViewerAsyncLoader mapsquare_loader;
    ViewerPendingMapsquareLoad mapsquare_load;
    ViewerDeferredMapsquareEviction mapsquare_eviction;
    uint64_t mapsquare_load_generation;
    const RuneCDevTransport *pending_dev_transport;
    int mapsquare_streaming_active;
    int mapsquare_center_region_x;
    int mapsquare_center_region_y;
    char mapsquare_directory[512];
    int initial_scene_origin_x;
    int initial_scene_origin_y;
    int initial_scene_w;
    int initial_scene_h;
    int initial_scene_ready;
    char initial_terrain_path[1024];
    char initial_objects_path[1024];
    char active_scene_prefix[1024];
} ViewerState;

static const char *object_action_label(const ViewerState *v,
                                       const ViewerPickedObject *object,
                                       const RcObjectDef *def, int opt);

static int viewer_load_hitsplat_textures(ViewerState *v,
                                         const char *sprite_directory) {
    if (!v || !sprite_directory || !runec_hitsplat_catalog_validate_assets(
            &v->hitsplat_catalog, sprite_directory)) {
        return 0;
    }
    for (int id = 0; id < RUNEC_HITSPLAT_MAX_DEFS; id++) {
        if (!v->hitsplat_catalog.defs[id].loaded) continue;
        char path[256];
        int n = snprintf(path, sizeof(path), "%s/hitsplat_%d.png",
                         sprite_directory, id);
        if (n < 0 || n >= (int)sizeof(path)) return 0;
        Texture2D texture = runec_load_texture_asset(path);
        if (texture.id == 0) {
            fprintf(stderr, "hitsplats: failed to load %s\n", path);
            return 0;
        }
        SetTextureFilter(texture, TEXTURE_FILTER_POINT);
        v->hitsplat_textures[id] = texture;
        v->hitsplat_texture_loaded[id] = 1;
    }
    return 1;
}

static void viewer_unload_hitsplat_textures(ViewerState *v) {
    if (!v) return;
    for (int id = 0; id < RUNEC_HITSPLAT_MAX_DEFS; id++) {
        if (!v->hitsplat_texture_loaded[id]) continue;
        UnloadTexture(v->hitsplat_textures[id]);
        v->hitsplat_texture_loaded[id] = 0;
    }
}

static void viewer_update_combat_overlays(ViewerState *v,
                                          float frame_seconds) {
    if (!v || !v->world || v->hitsplat_catalog.loaded_count <= 0) return;
    float client_cycles = frame_seconds > 0.0f
        ? frame_seconds * RUNEC_CLIENT_CYCLES_PER_SECOND : 0.0f;
    runec_hitsplat_state_advance(&v->player_overlay.hitsplats, client_cycles);
    runec_health_bar_advance(&v->player_overlay.health_bar, client_cycles);
    runec_hitsplat_state_sync(
        &v->player_overlay.hitsplats, &v->hitsplat_catalog,
        v->world->player.combat.recent_hits,
        v->world->player.combat.recent_hit_count, 1);
    runec_health_bar_sync(
        &v->player_overlay.health_bar, v->world->player.combat.recent_hits,
        v->world->player.combat.recent_hit_count,
        v->world->player.current_hp, v->world->player.max_hp);

    for (int i = 0; i < v->world->npc_count; i++) {
        const RcNpc *npc = &v->world->npcs[i];
        RcNpcLifePhase life = rc_npc_life_phase(npc);
        if (life == RC_NPC_LIFE_REMOVED || life == RC_NPC_LIFE_HIDDEN) {
            runec_hitsplat_state_reset(&v->npc_overlays[i].hitsplats);
            runec_health_bar_reset(&v->npc_overlays[i].health_bar);
            continue;
        }
        runec_hitsplat_state_advance(&v->npc_overlays[i].hitsplats,
                                     client_cycles);
        runec_health_bar_advance(&v->npc_overlays[i].health_bar,
                                 client_cycles);
        runec_hitsplat_state_sync(
            &v->npc_overlays[i].hitsplats, &v->hitsplat_catalog,
            npc->combat.recent_hits, npc->combat.recent_hit_count, 0);
        const RcNpcDef *def = rc_npc_def_for_npc(v->world, npc);
        int maximum_hp = npc->spawn_hp > 0 ? npc->spawn_hp
                         : def ? def->hitpoints : 0;
        runec_health_bar_sync(
            &v->npc_overlays[i].health_bar, npc->combat.recent_hits,
            npc->combat.recent_hit_count, npc->current_hp, maximum_hp);
    }
}

static void create_object_anim_plane_states(ViewerState *v, int plane);
static void free_object_anim_plane(ViewerState *v, int plane);
static void load_object_anim_models_for_plane(ViewerState *v,
                                              const char *objects_path,
                                              int plane);
static void reset_model_entry_to_base_pose(ModelEntry *entry);
static void ensure_active_scene_plane(ViewerState *v, int plane);
static void reset_viewer_context(ViewerState *v);
static void viewer_finish_dev_transport(ViewerState *v,
                                        const RuneCDevTransport *d);
static void handle_player_scene_transition(ViewerState *v, int old_x,
                                           int old_y, int old_plane);
static int viewer_mapsquare_cache_allowed(void);
static int viewer_load_mapsquare_catalog(ViewerState *v);
static int viewer_mapsquare_center_available(const ViewerState *v, int x,
                                             int y, int plane);
static int viewer_mapsquare_chunk_available(const ViewerState *v,
                                             int region_x, int region_y,
                                             int plane);
static int viewer_mapsquare_missing_asset_path(
    const ViewerState *v, int region_x, int region_y, int plane,
    char *out, size_t capacity);
static void viewer_report_missing_mapsquare_assets(const ViewerState *v,
                                                    int center_x,
                                                    int center_y, int plane,
                                                    const char *operation);
static int viewer_mapsquare_visible_plan(
    const ViewerState *v, int center_x, int center_y,
    ViewerMapsquareCoord *plan, int capacity);
static int viewer_mapsquare_window_assets_available(const ViewerState *v,
                                                     int center_x,
                                                     int center_y, int plane);
static int viewer_prepare_mapsquare_window(ViewerState *v, int center_x,
                                           int center_y, int plane);
static int viewer_mapsquare_asset_path(const ViewerState *v, char *out,
                                       size_t capacity, int region_x,
                                       int region_y, int plane,
                                       const char *suffix);
static int viewer_mapsquare_material_path(const ViewerState *v, char *out,
                                          size_t capacity);
static int viewer_request_mapsquare_window(ViewerState *v, int center_x,
                                           int center_y, int plane,
                                           ViewerStreamingRequestKind kind,
                                           const char *reason);
static int viewer_process_mapsquare_loading(ViewerState *v);
static void viewer_process_deferred_mapsquare_eviction(ViewerState *v);
static void viewer_cancel_mapsquare_loading(ViewerState *v);
static int viewer_mapsquare_plane_loaded(const ViewerState *v, int plane);
static TerrainMesh *viewer_mapsquare_terrain_at(const ViewerState *v,
                                                int plane, int world_x,
                                                int world_y);
static void free_mapsquare_cache(ViewerState *v);
static void draw_mapsquare_chunks(ViewerState *v, int plane, float frame_dt);
static int mapsquare_chunk_in_active_scene(
    const ViewerMapsquareChunk *chunk);
static int viewer_actor_in_draw_range(const ViewerState *v, float x, float y,
                                      float padding);
static void viewer_update_movement_prefetch(ViewerState *v);

static ViewerWorldTransform g_world_transform = {
    DEFAULT_WORLD_ORIGIN_X,
    DEFAULT_WORLD_ORIGIN_Y,
};
#define g_world_origin_x (g_world_transform.origin_x)
#define g_world_origin_y (g_world_transform.origin_y)
static int g_player_start_x = DEFAULT_PLAYER_START_X;
static int g_player_start_y = DEFAULT_PLAYER_START_Y;
static int g_player_start_plane = 0;
static int g_world_w = DEFAULT_WORLD_W;
static int g_world_h = DEFAULT_WORLD_H;

// Convert world tile to local rendering coordinate
#define LOCAL_X(wx) viewer_world_local_x(&g_world_transform, (wx))
#define LOCAL_Y(wy) viewer_world_local_y(&g_world_transform, (wy))

static const char *env_path(const char *key, const char *fallback) {
    const char *value = getenv(key);
    return value && value[0] ? value : fallback;
}

static int env_has_value(const char *key) {
    const char *value = getenv(key);
    return value && value[0];
}

static int local_dir_exists(const char *path) {
    struct stat st;
    return path && path[0] && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static const char *viewer_scene_export_cache_path(void) {
    const char *cache = getenv("RUNEC_CACHE");
    if (cache && cache[0])
        return cache;
    cache = getenv("RUNEC_B237_CACHE");
    if (cache && cache[0])
        return cache;
    if (local_dir_exists(DEFAULT_B237_CACHE_PATH))
        return DEFAULT_B237_CACHE_PATH;
    return NULL;
}

static ViewerSceneMode viewer_scene_mode_from_env(void) {
    const char *mode = getenv("RUNEC_SCENE_MODE");
    if (mode && mode[0]) {
        if (strcmp(mode, "generated") == 0)
            return VIEWER_SCENE_MODE_GENERATED;
        if (strcmp(mode, "fixed") == 0)
            return VIEWER_SCENE_MODE_FIXED;
        if (strcmp(mode, "auto") == 0)
            return VIEWER_SCENE_MODE_AUTO;
        fprintf(stderr,
                "viewer scene: unknown RUNEC_SCENE_MODE=%s; using auto\n",
                mode);
        return VIEWER_SCENE_MODE_AUTO;
    }
    if (env_has_value("RUNEC_TERRAIN") || env_has_value("RUNEC_OBJECTS"))
        return VIEWER_SCENE_MODE_FIXED;
    return VIEWER_SCENE_MODE_FIXED;
}

static const char *viewer_scene_mode_name(ViewerSceneMode mode) {
    switch (mode) {
    case VIEWER_SCENE_MODE_GENERATED: return "generated";
    case VIEWER_SCENE_MODE_FIXED: return "fixed";
    case VIEWER_SCENE_MODE_AUTO:
    default: return "auto";
    }
}

static int runtime_data_available(const ViewerState *v) {
    const char *required[] = {
        "defs/items.bin",
        "defs/npc_defs.bin",
        "defs/object_defs.bin",
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

    ViewerSceneMode scene_mode = viewer_scene_mode_from_env();
    int mapsquare_mode = viewer_mapsquare_cache_allowed();
    if (mapsquare_mode && !v->scene_auto_export
            && (!v->mapsquare_catalog.loaded
                || !viewer_mapsquare_window_assets_available(
                    v, g_player_start_x, g_player_start_y,
                    g_player_start_plane))) {
        viewer_report_missing_mapsquare_assets(
            v, g_player_start_x, g_player_start_y, g_player_start_plane,
            "startup");
        missing = 1;
    } else if (!mapsquare_mode && (scene_mode == VIEWER_SCENE_MODE_FIXED
            || env_has_value("RUNEC_TERRAIN")
            || env_has_value("RUNEC_OBJECTS"))) {
        const char *fixed_required[] = {
            env_path("RUNEC_TERRAIN", "regions/varrock.terrain"),
            env_path("RUNEC_OBJECTS", "regions/varrock.objects"),
        };
        for (size_t i = 0; i < sizeof(fixed_required) / sizeof(fixed_required[0]); i++) {
            if (!rc_asset_exists(fixed_required[i])) {
                fprintf(stderr, "missing runtime data asset: %s\n",
                        fixed_required[i]);
                missing = 1;
            }
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

static int env_int_compat(const char *key, const char *legacy_key,
                          const char *older_key, int fallback) {
    if (env_has_value(key))
        return env_int(key, fallback);
    if (legacy_key && env_has_value(legacy_key))
        return env_int(legacy_key, fallback);
    if (older_key && env_has_value(older_key))
        return env_int(older_key, fallback);
    return fallback;
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

static int string_equal_fold_ascii(const char *a, const char *b) {
    if (!a || !b)
        return 0;
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
    }
    return *a == '\0' && *b == '\0';
}

static int viewer_trace_log_level_from_env(void) {
    const char *value = getenv("RC_VIEWER_LOG_LEVEL");
    if (!value || !value[0])
        return LOG_WARNING;
    if (string_equal_fold_ascii(value, "all"))
        return LOG_ALL;
    if (string_equal_fold_ascii(value, "trace"))
        return LOG_TRACE;
    if (string_equal_fold_ascii(value, "debug"))
        return LOG_DEBUG;
    if (string_equal_fold_ascii(value, "info"))
        return LOG_INFO;
    if (string_equal_fold_ascii(value, "warning")
            || string_equal_fold_ascii(value, "warn"))
        return LOG_WARNING;
    if (string_equal_fold_ascii(value, "error"))
        return LOG_ERROR;
    if (string_equal_fold_ascii(value, "fatal"))
        return LOG_FATAL;
    if (string_equal_fold_ascii(value, "none")
            || string_equal_fold_ascii(value, "off"))
        return LOG_NONE;
    fprintf(stderr,
            "unknown RC_VIEWER_LOG_LEVEL '%s'; using warning\n", value);
    return LOG_WARNING;
}

static float env_float(const char *key, float fallback) {
    const char *value = getenv(key);
    if (!value || !value[0])
        return fallback;
    char *end = NULL;
    float parsed = strtof(value, &end);
    return end && *end == '\0' ? parsed : fallback;
}

typedef enum {
    RUNEC_RENDER_PROFILE_LEGACY = 0,
    RUNEC_RENDER_PROFILE_OSRS,
    RUNEC_RENDER_PROFILE_DEBUG
} RuneCRenderProfile;

typedef struct {
    const char *profile_name;
    int color_lift_enabled;
    int msaa_enabled;
    float camera_pitch;
    float camera_dist;
    float camera_fovy;
} RuneCRenderSettings;

typedef struct GLFWwindow GLFWwindow;
typedef void (*GLFWerrorfun)(int, const char *);

extern int glfwInit(void);
extern void glfwTerminate(void);
extern void glfwDefaultWindowHints(void);
extern void glfwWindowHint(int hint, int value);
extern GLFWwindow *glfwCreateWindow(int width, int height, const char *title,
                                    void *monitor, void *share);
extern void glfwDestroyWindow(GLFWwindow *window);
extern GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun callback);

enum {
    RUNEC_GLFW_FALSE = 0,
    RUNEC_GLFW_VISIBLE = 0x00020004,
    RUNEC_GLFW_SAMPLES = 0x0002100D,
    RUNEC_GLFW_CONTEXT_VERSION_MAJOR = 0x00022002,
    RUNEC_GLFW_CONTEXT_VERSION_MINOR = 0x00022003,
    RUNEC_GLFW_OPENGL_FORWARD_COMPAT = 0x00022006,
    RUNEC_GLFW_OPENGL_PROFILE = 0x00022008,
    RUNEC_GLFW_OPENGL_CORE_PROFILE = 0x00032001,
};

static int g_glfw_preflight_error_code = 0;
static char g_glfw_preflight_error_desc[512];

static void viewer_glfw_preflight_error(int code, const char *description) {
    g_glfw_preflight_error_code = code;
    snprintf(g_glfw_preflight_error_desc,
             sizeof(g_glfw_preflight_error_desc),
             "%s", description ? description : "<no description>");
}

static RuneCRenderProfile render_profile_from_env(const char **out_name) {
    const char *profile = env_path("RUNEC_RENDER_PROFILE", "osrs");
    if (strcmp(profile, "osrs") == 0 || strcmp(profile, "OSRS") == 0) {
        if (out_name) *out_name = "osrs";
        return RUNEC_RENDER_PROFILE_OSRS;
    }
    if (strcmp(profile, "debug") == 0 || strcmp(profile, "DEBUG") == 0) {
        if (out_name) *out_name = "debug";
        return RUNEC_RENDER_PROFILE_DEBUG;
    }
    if (strcmp(profile, "legacy") == 0 || strcmp(profile, "LEGACY") == 0) {
        if (out_name) *out_name = "legacy";
        return RUNEC_RENDER_PROFILE_LEGACY;
    }
    fprintf(stderr,
            "unknown RUNEC_RENDER_PROFILE=%s; using osrs\n",
            profile);
    if (out_name) *out_name = "osrs";
    return RUNEC_RENDER_PROFILE_OSRS;
}

static float clamp_float(float value, float min_value, float max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static RuneCRenderSettings render_settings_from_env(void) {
    const char *profile_name = "legacy";
    RuneCRenderProfile profile = render_profile_from_env(&profile_name);
    RuneCRenderSettings settings = {
        .profile_name = profile_name,
        .color_lift_enabled = profile == RUNEC_RENDER_PROFILE_OSRS ? 0 : 1,
        .msaa_enabled = 0,
        .camera_pitch = 0.6f,
        .camera_dist = 50.0f,
        .camera_fovy = 45.0f,
    };
    settings.color_lift_enabled =
        env_bool("RUNEC_COLOR_LIFT", settings.color_lift_enabled);
    settings.msaa_enabled = env_bool("RUNEC_MSAA", settings.msaa_enabled);
    settings.camera_pitch =
        env_float("RUNEC_CAMERA_PITCH", settings.camera_pitch);
    settings.camera_dist =
        env_float("RUNEC_CAMERA_DIST", settings.camera_dist);
    settings.camera_fovy =
        env_float("RUNEC_CAMERA_FOV", settings.camera_fovy);
    settings.camera_pitch = clamp_float(settings.camera_pitch, 0.1f, 1.4f);
    settings.camera_dist = clamp_float(settings.camera_dist, 5.0f, 300.0f);
    settings.camera_fovy = clamp_float(settings.camera_fovy, 10.0f, 120.0f);
    return settings;
}

static void print_gl_context_help(const char *phase) {
    fprintf(stderr,
            "rc-viewer: failed to create an OpenGL/GLX context during %s.\n",
            phase);
    if (g_glfw_preflight_error_desc[0]) {
        fprintf(stderr, "rc-viewer: GLFW error %d: %s\n",
                g_glfw_preflight_error_code, g_glfw_preflight_error_desc);
    }
    fprintf(stderr,
            "rc-viewer: runtime data loaded, but this display/driver cannot "
            "open the requested GL context.\n"
            "rc-viewer: try `LIBGL_ALWAYS_SOFTWARE=1 ./build/rc-viewer`, "
            "`RUNEC_MSAA=0 ./build/rc-viewer`, or "
            "`RUNEC_VIEWER_SMOKE=1 ./build/rc-viewer` for a no-window data "
            "check.\n");
}

static int preflight_gl_context(const RuneCRenderSettings *settings) {
    if (!env_bool("RUNEC_VIEWER_GL_PREFLIGHT", 1))
        return 1;

    g_glfw_preflight_error_code = 0;
    g_glfw_preflight_error_desc[0] = '\0';
    GLFWerrorfun previous = glfwSetErrorCallback(viewer_glfw_preflight_error);
    if (!glfwInit()) {
        glfwSetErrorCallback(previous);
        return 0;
    }

    glfwDefaultWindowHints();
    glfwWindowHint(RUNEC_GLFW_VISIBLE, RUNEC_GLFW_FALSE);
    glfwWindowHint(RUNEC_GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(RUNEC_GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(RUNEC_GLFW_OPENGL_PROFILE,
                   RUNEC_GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(RUNEC_GLFW_OPENGL_FORWARD_COMPAT, 1);
    if (settings && settings->msaa_enabled)
        glfwWindowHint(RUNEC_GLFW_SAMPLES, 4);

    GLFWwindow *probe = glfwCreateWindow(64, 64, "RuneC GL preflight",
                                         NULL, NULL);
    if (!probe) {
        glfwTerminate();
        glfwSetErrorCallback(previous);
        return 0;
    }

    glfwDestroyWindow(probe);
    glfwTerminate();
    glfwSetErrorCallback(previous);
    return 1;
}

static int gl_context_error_is_glx(void) {
    return strstr(g_glfw_preflight_error_desc, "GLX") != NULL;
}

static int maybe_reexec_with_mesa_glx(char **argv) {
    if (!argv || !argv[0])
        return 0;
    if (!env_bool("RUNEC_GLX_MESA_FALLBACK", 0))
        return 0;
    if (env_bool("RUNEC_GLX_MESA_FALLBACK_ATTEMPTED", 0))
        return 0;
    if (!gl_context_error_is_glx())
        return 0;
    if (env_has_value("__GLX_VENDOR_LIBRARY_NAME"))
        return 0;

    fprintf(stderr,
            "rc-viewer: retrying with Mesa software GLX fallback "
            "(__GLX_VENDOR_LIBRARY_NAME=mesa LIBGL_ALWAYS_SOFTWARE=1)\n");
    setenv("RUNEC_GLX_MESA_FALLBACK_ATTEMPTED", "1", 1);
    setenv("__GLX_VENDOR_LIBRARY_NAME", "mesa", 1);
    setenv("LIBGL_ALWAYS_SOFTWARE", "1", 0);
    execvp(argv[0], argv);
    fprintf(stderr, "rc-viewer: Mesa GLX fallback exec failed: %s\n",
            strerror(errno));
    return 0;
}

static int clamp_plane(int plane) {
    if (plane < 0) return 0;
    if (plane >= RC_MAX_PLANES) return RC_MAX_PLANES - 1;
    return plane;
}

static int viewer_presented_player_tile(const ViewerState *v, int *x, int *y,
                                        int *plane) {
    if (!v || !v->world || !x || !y || !plane)
        return 0;
    if (viewer_streaming_player_transition_source(
            &v->mapsquare_load.player_transition, x, y, plane)) {
        return 1;
    }
    *x = v->world->player.x;
    *y = v->world->player.y;
    *plane = v->world->player.plane;
    return 1;
}

static int viewer_scene_plane(const ViewerState *v) {
    if (!v || !v->world) return 0;
    if (v->scene_plane_override >= 0)
        return clamp_plane(v->scene_plane_override);
    int x = 0;
    int y = 0;
    int plane = 0;
    return viewer_presented_player_tile(v, &x, &y, &plane)
         ? clamp_plane(plane) : 0;
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

static TerrainMesh *viewer_mapsquare_terrain_at(const ViewerState *v,
                                                int plane, int world_x,
                                                int world_y) {
    if (!v || !v->mapsquare_streaming_active)
        return NULL;
    int region_x = world_x / VIEWER_STREAMING_MAPSQUARE_SIZE;
    int region_y = world_y / VIEWER_STREAMING_MAPSQUARE_SIZE;
    plane = clamp_plane(plane);
    for (int i = 0; i < v->mapsquare_chunk_count; i++) {
        const ViewerMapsquareChunk *chunk = &v->mapsquare_chunks[i];
        if (chunk->region_x == region_x && chunk->region_y == region_y
                && chunk->plane == plane) {
            return chunk->terrain;
        }
    }
    return NULL;
}

static int viewer_mapsquare_plane_loaded(const ViewerState *v, int plane) {
    if (!v || !v->mapsquare_streaming_active)
        return 0;
    plane = clamp_plane(plane);
    for (int i = 0; i < v->mapsquare_chunk_count; i++) {
        const ViewerMapsquareChunk *chunk = &v->mapsquare_chunks[i];
        if (chunk->region_x == v->mapsquare_center_region_x
                && chunk->region_y == v->mapsquare_center_region_y
                && chunk->plane == plane && chunk->terrain
                && chunk->objects) {
            return 1;
        }
    }
    return 0;
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

static void free_object_chunk_plane(ViewerState *v, int plane) {
    if (!v || plane < 0 || plane >= RC_MAX_PLANES) return;
    for (int i = 0; i < v->object_chunk_count[plane]; i++) {
        objects_free(v->object_chunks[plane][i].mesh);
        v->object_chunks[plane][i].mesh = NULL;
    }
    v->object_chunk_count[plane] = 0;
    if (v->object_chunk_atlas_loaded[plane]
            && v->object_chunk_atlas[plane].id > 0) {
        UnloadTexture(v->object_chunk_atlas[plane]);
    }
    v->object_chunk_atlas[plane] = (Texture2D){0};
    v->object_chunk_atlas_loaded[plane] = 0;
}

static int object_chunk_path(char *out, size_t cap, const char *dir,
                             const char *prefix, int ox, int oy, int plane) {
    if (!out || cap == 0 || !dir || !prefix)
        return 0;
    int n = 0;
    if (plane == 0) {
        n = snprintf(out, cap, "%s/%s_%d_%d.objects", dir, prefix, ox, oy);
    } else {
        n = snprintf(out, cap, "%s/%s_%d_%d.p%d.objects", dir, prefix, ox, oy,
                     plane);
    }
    return n > 0 && (size_t)n < cap;
}

static int load_object_chunk_index_plane(ViewerState *v,
                                         const char *base_objects_path,
                                         int plane) {
    if (!v || !base_objects_path || !env_bool("RUNEC_OBJECT_CHUNKS", 1))
        return 0;
    plane = clamp_plane(plane);
    free_object_chunk_plane(v, plane);

    const char *dir = env_path("RUNEC_OBJECT_CHUNKS_DIR",
                               "data/regions/varrock_chunks");
    const char *prefix = env_path("RUNEC_OBJECT_CHUNK_PREFIX", "varrock");
    int chunk_size = v->object_chunk_size > 0 ? v->object_chunk_size : 64;
    int count = 0;
    for (int ox = g_world_origin_x; ox < g_world_origin_x + g_world_w;
            ox += chunk_size) {
        for (int oy = g_world_origin_y; oy < g_world_origin_y + g_world_h;
                oy += chunk_size) {
            if (count >= v->streaming.max_cpu_chunks)
                break;
            char path[1024];
            if (!object_chunk_path(path, sizeof(path), dir, prefix, ox, oy,
                                   plane)
                    || !rc_asset_exists(path)) {
                continue;
            }
            ViewerObjectChunk *chunk = &v->object_chunks[plane][count++];
            chunk->origin_x = ox;
            chunk->origin_y = oy;
            chunk->size = chunk_size;
            snprintf(chunk->path, sizeof(chunk->path), "%s", path);
        }
    }
    v->object_chunk_count[plane] = count;
    if (count <= 0)
        return 0;

    char atlas_path[1024];
    if (companion_path(atlas_path, sizeof(atlas_path), base_objects_path,
                       ".atlas")
            && rc_asset_exists(atlas_path)) {
        v->object_chunk_atlas[plane] = objects_load_atlas(NULL, atlas_path);
        v->object_chunk_atlas_loaded[plane] =
            v->object_chunk_atlas[plane].id > 0;
    }
    fprintf(stderr,
            "object chunks: indexed %d chunks for plane %d from %s\n",
            count, plane, dir);
    return 1;
}

static int object_chunk_should_draw(const ViewerState *v,
                                    const ViewerObjectChunk *chunk) {
    if (!v || !chunk)
        return 0;
    if (v->object_chunk_draw_radius <= 0.0f)
        return 1;
    float cx = ((float)chunk->origin_x + (float)chunk->size * 0.5f)
             - (float)g_world_origin_x;
    float cz = -(((float)chunk->origin_y + (float)chunk->size * 0.5f)
               - (float)g_world_origin_y);
    float dx = cx - v->camera.target.x;
    float dz = cz - v->camera.target.z;
    float r = v->object_chunk_draw_radius;
    return dx * dx + dz * dz <= r * r;
}

typedef struct {
    unsigned int ids[32];
    int count;
    uint64_t bytes;
} ViewerTextureResidency;

static void add_texture_residency(ViewerTextureResidency *residency,
                                  Texture2D texture) {
    if (!residency || texture.id == 0 || texture.width <= 0
            || texture.height <= 0)
        return;
    for (int i = 0; i < residency->count; i++)
        if (residency->ids[i] == texture.id)
            return;
    if (residency->count < (int)(sizeof(residency->ids)
            / sizeof(residency->ids[0]))) {
        residency->ids[residency->count++] = texture.id;
    }
    residency->bytes += (uint64_t)texture.width
                      * (uint64_t)texture.height * 4u;
}

static uint64_t model_entry_resident_bytes(const ModelEntry *entry) {
    if (!entry || !entry->loaded)
        return 0;
    uint64_t bytes = 0;
    for (int i = 0; i < entry->model.meshCount; i++) {
        const Mesh *mesh = &entry->model.meshes[i];
        bytes += (uint64_t)mesh->vertexCount
               * (3u * sizeof(float) + 3u * sizeof(float)
                  + 2u * sizeof(float) + 4u);
        bytes += (uint64_t)mesh->triangleCount * 3u * sizeof(uint16_t);
    }
    bytes += (uint64_t)entry->base_vert_count
           * (3u * sizeof(int16_t) + sizeof(uint8_t));
    bytes += (uint64_t)entry->face_count
           * (3u * sizeof(uint16_t) + 3u * sizeof(uint8_t));
    return bytes;
}

static uint64_t model_set_resident_bytes(const ModelSet *set) {
    uint64_t bytes = 0;
    for (int i = 0; set && i < set->count; i++)
        bytes += model_entry_resident_bytes(&set->entries[i]);
    return bytes;
}

static int loaded_object_chunk_count(const ViewerState *v, int plane) {
    int count = 0;
    if (!v || plane < 0 || plane >= RC_MAX_PLANES)
        return 0;
    for (int i = 0; i < v->object_chunk_count[plane]; i++)
        if (v->object_chunks[plane][i].mesh)
            count++;
    return count;
}

static void viewer_refresh_streaming_telemetry(ViewerState *v,
                                               int refresh_model_cache) {
    if (!v) return;
    ViewerStreamingTelemetry *telemetry = &v->telemetry;
    telemetry->terrain_chunks_cpu = 0;
    telemetry->terrain_chunks_gpu = 0;
    telemetry->object_chunks_cpu = 0;
    telemetry->object_chunks_gpu = 0;
    telemetry->async_jobs_pending = v->mapsquare_load.active
        ? viewer_async_loader_pending(
            &v->mapsquare_loader, v->mapsquare_load.generation) : 0;
    telemetry->cpu_chunks_staged = 0;
    for (int i = 0; i < v->mapsquare_load.staged_count; i++) {
        telemetry->cpu_chunks_staged +=
            v->mapsquare_load.staged[i].upload_stage < 3;
    }
    telemetry->gpu_upload_bytes_frame = v->frame_gpu_upload_bytes;
    telemetry->gpu_upload_bytes_peak = v->peak_gpu_upload_bytes;
    telemetry->gpu_upload_budget_bytes =
        viewer_streaming_upload_budget_bytes(&v->streaming);
    if (v->mapsquare_load.active) {
        telemetry->async_frames = v->mapsquare_load.process_frames;
        telemetry->main_thread_streaming_work_max_ms =
            v->mapsquare_load.main_thread_work_max_ms;
    }
    telemetry->terrain_vertices_resident = 0;
    telemetry->object_vertices_resident = 0;

    ViewerTextureResidency textures = {0};
    for (int plane = 0; plane < RC_MAX_PLANES; plane++) {
        TerrainMesh *terrain = v->terrain_planes[plane];
        if (terrain && terrain->loaded) {
            telemetry->terrain_chunks_cpu++;
            telemetry->terrain_chunks_gpu++;
            telemetry->terrain_vertices_resident +=
                (uint64_t)terrain->vertex_count;
        }
        ObjectMesh *objects = v->object_planes[plane];
        if (objects && objects->loaded) {
            telemetry->object_chunks_cpu++;
            telemetry->object_chunks_gpu++;
            telemetry->object_vertices_resident +=
                (uint64_t)objects->total_vertex_count;
            add_texture_residency(&textures, objects->atlas_texture);
        }
        add_texture_residency(&textures, v->object_chunk_atlas[plane]);
        for (int i = 0; i < v->object_chunk_count[plane]; i++) {
            ObjectMesh *chunk = v->object_chunks[plane][i].mesh;
            if (!chunk || !chunk->loaded)
                continue;
            telemetry->object_chunks_cpu++;
            telemetry->object_chunks_gpu++;
            telemetry->object_vertices_resident +=
                (uint64_t)chunk->total_vertex_count;
            add_texture_residency(&textures, chunk->atlas_texture);
        }
    }
    add_texture_residency(
        &textures,
        v->mapsquare_materials
            ? v->mapsquare_materials->atlas_texture : (Texture2D){0});
    for (int i = 0; i < v->mapsquare_chunk_count; i++) {
        ViewerMapsquareChunk *chunk = &v->mapsquare_chunks[i];
        if (chunk->terrain && chunk->terrain->loaded) {
            telemetry->terrain_chunks_cpu++;
            telemetry->terrain_chunks_gpu++;
            telemetry->terrain_vertices_resident +=
                (uint64_t)chunk->terrain->vertex_count;
        }
        if (chunk->objects && chunk->objects->loaded) {
            telemetry->object_chunks_cpu++;
            telemetry->object_chunks_gpu++;
            telemetry->object_vertices_resident +=
                (uint64_t)chunk->objects->total_vertex_count;
        }
    }

    ModelSet *sets[] = {
        v->player_model, v->npc_models, v->item_models,
        v->projectile_models,
    };
    uint64_t model_bytes = 0;
    for (size_t i = 0; i < sizeof(sets) / sizeof(sets[0]); i++) {
        add_texture_residency(&textures,
                              sets[i] ? sets[i]->atlas_texture
                                      : (Texture2D){0});
        if (refresh_model_cache)
            model_bytes += model_set_resident_bytes(sets[i]);
    }
    if (refresh_model_cache) {
        for (int i = 0; i < RC_MAX_GROUND_ITEMS; i++)
            model_bytes += model_set_resident_bytes(v->ground_models[i].models);
    }
    for (int plane = 0; plane < RC_MAX_PLANES; plane++) {
        ModelSet *set = v->object_anim_model_planes[plane];
        add_texture_residency(&textures,
                              set ? set->atlas_texture : (Texture2D){0});
        if (refresh_model_cache)
            model_bytes += model_set_resident_bytes(set);
    }
    for (int i = 0; i < v->mapsquare_chunk_count; i++) {
        ModelSet *set = v->mapsquare_chunks[i].object_anim_models;
        add_texture_residency(
            &textures, set ? set->atlas_texture : (Texture2D){0});
        if (refresh_model_cache)
            model_bytes += model_set_resident_bytes(set);
    }
    if (refresh_model_cache && v->composed_player_loaded)
        model_bytes += model_entry_resident_bytes(&v->composed_player_model);
    if (refresh_model_cache)
        telemetry->model_cache_mb = (double)model_bytes / (1024.0 * 1024.0);
    telemetry->texture_cache_mb =
        (double)textures.bytes / (1024.0 * 1024.0);

    RcWorldStreamingTelemetry backend = {0};
    if (rc_world_get_streaming_telemetry(v->world, &backend) > 0) {
        telemetry->active_npcs = backend.active_npcs;
        telemetry->active_ground_items = backend.active_ground_items;
        telemetry->backend_pages_loaded = backend.backend_pages_loaded;
        telemetry->backend_page_load_ms = backend.backend_page_load_ms;
        telemetry->backend_active_area_load_ms = backend.active_area_load_ms;
    }

    int draw_calls = 0;
    int scene_plane = viewer_scene_plane(v);
    if (viewer_terrain_for_plane(v, scene_plane)) draw_calls++;
    ObjectMesh *scene_objects = viewer_objects_for_plane(v, scene_plane);
    if (scene_objects && scene_objects->loaded) {
        draw_calls++;
        draw_calls += scene_objects->object_anim_count;
    }
    for (int i = 0; i < v->object_chunk_count[scene_plane]; i++) {
        ViewerObjectChunk *chunk = &v->object_chunks[scene_plane][i];
        if (chunk->mesh && object_chunk_should_draw(v, chunk))
            draw_calls++;
    }
    for (int i = 0; i < v->mapsquare_chunk_count; i++) {
        ViewerMapsquareChunk *chunk = &v->mapsquare_chunks[i];
        if (chunk->plane != scene_plane
                || !mapsquare_chunk_in_active_scene(chunk))
            continue;
        if (chunk->terrain && chunk->terrain->loaded)
            draw_calls++;
        if (chunk->objects && chunk->objects->loaded) {
            draw_calls++;
            draw_calls += chunk->objects->object_anim_count;
        }
    }
    if (v->world) {
        if (v->world->player.plane == scene_plane)
            draw_calls++;
        for (int i = 0; i < v->world->npc_count; i++)
            if (v->world->npcs[i].active && !v->world->npcs[i].is_dead
                    && v->world->npcs[i].plane == scene_plane
                    && v->world->npcs[i].x >= g_world_origin_x
                    && v->world->npcs[i].x < g_world_origin_x + g_world_w
                    && v->world->npcs[i].y >= g_world_origin_y
                    && v->world->npcs[i].y < g_world_origin_y + g_world_h
                    && viewer_actor_in_draw_range(
                        v, (float)v->world->npcs[i].x,
                        (float)v->world->npcs[i].y, 0.0f))
                draw_calls++;
        for (int i = 0; i < v->world->ground_item_count; i++)
            if (v->world->ground_items[i].active
                    && v->world->ground_items[i].plane == scene_plane
                    && v->world->ground_items[i].x >= g_world_origin_x
                    && v->world->ground_items[i].x
                        < g_world_origin_x + g_world_w
                    && v->world->ground_items[i].y >= g_world_origin_y
                    && v->world->ground_items[i].y
                        < g_world_origin_y + g_world_h
                    && viewer_actor_in_draw_range(
                        v, (float)v->world->ground_items[i].x,
                        (float)v->world->ground_items[i].y, 0.0f))
                draw_calls++;
    }
    draw_calls += v->combat_projectile_count;
    telemetry->draw_calls_estimate = draw_calls;
}

static void viewer_log_streaming_telemetry(ViewerState *v,
                                           const char *reason) {
    if (!v) return;
    viewer_refresh_streaming_telemetry(v, 1);
    const ViewerStreamingTelemetry *t = &v->telemetry;
    fprintf(stderr,
            "streaming telemetry [%s]: startup_ms=%.2f load_ms=%.2f "
            "cpu_decode_ms=%.2f gpu_upload_ms=%.2f terrain_chunks=%d/%d "
            "object_chunks=%d/%d async_pending=%d cpu_staged=%d "
            "upload_frame_mb=%.2f peak_mb=%.2f budget_mb=%.2f "
            "async_frames=%llu main_work_max_ms=%.2f "
            "vertices=%llu/%llu texture_mb=%.2f "
            "model_mb=%.2f active_npcs=%d active_ground_items=%d "
            "backend_pages=%d backend_page_ms=%.2f "
            "backend_area_ms=%.2f draw_calls_est=%d\n",
            reason ? reason : "snapshot", t->startup_ms,
            t->scene_or_chunk_load_ms, t->cpu_decode_ms, t->gpu_upload_ms,
            t->terrain_chunks_cpu, t->terrain_chunks_gpu,
            t->object_chunks_cpu, t->object_chunks_gpu,
            t->async_jobs_pending, t->cpu_chunks_staged,
            (double)t->gpu_upload_bytes_frame / (1024.0 * 1024.0),
            (double)t->gpu_upload_bytes_peak / (1024.0 * 1024.0),
            (double)t->gpu_upload_budget_bytes / (1024.0 * 1024.0),
            (unsigned long long)t->async_frames,
            t->main_thread_streaming_work_max_ms,
            (unsigned long long)t->terrain_vertices_resident,
            (unsigned long long)t->object_vertices_resident,
            t->texture_cache_mb, t->model_cache_mb, t->active_npcs,
            t->active_ground_items, t->backend_pages_loaded,
            t->backend_page_load_ms, t->backend_active_area_load_ms,
            t->draw_calls_estimate);
}

static double viewer_begin_streaming_load(ViewerState *v) {
    if (v) {
        v->pending_cpu_decode_ms = 0.0;
        v->pending_gpu_upload_ms = 0.0;
        v->frame_gpu_upload_bytes = 0;
        v->peak_gpu_upload_bytes = 0;
    }
    return viewer_streaming_now_ms();
}

static void viewer_finish_streaming_load(ViewerState *v, double started_ms,
                                         const char *reason, int log_snapshot) {
    if (!v) return;
    viewer_streaming_telemetry_record_load(
        &v->telemetry, viewer_streaming_now_ms() - started_ms,
        v->pending_cpu_decode_ms, v->pending_gpu_upload_ms);
    if (log_snapshot)
        viewer_log_streaming_telemetry(v, reason);
}

static void load_object_chunk_mesh(ViewerState *v, int plane,
                                   ViewerObjectChunk *chunk) {
    if (!v || !chunk || chunk->mesh)
        return;
    if (loaded_object_chunk_count(v, plane) >= v->streaming.max_gpu_chunks)
        return;
    double load_started_ms = viewer_begin_streaming_load(v);
    Texture2D atlas = v->object_chunk_atlas_loaded[plane]
                    ? v->object_chunk_atlas[plane] : (Texture2D){0};
    chunk->mesh = objects_load_with_shared_atlas(chunk->path, atlas);
    if (!chunk->mesh)
        return;
    v->pending_cpu_decode_ms += chunk->mesh->cpu_decode_ms;
    v->pending_gpu_upload_ms += chunk->mesh->gpu_upload_ms;
    objects_offset(chunk->mesh, g_world_origin_x, g_world_origin_y);
    if (v->alpha_cutout_shader_static_loaded)
        objects_set_shader(chunk->mesh, v->alpha_cutout_shader_static);
    viewer_finish_streaming_load(v, load_started_ms, "object-chunk", 0);
}

static void draw_object_chunks(ViewerState *v, int plane) {
    if (!v) return;
    plane = clamp_plane(plane);
    for (int i = 0; i < v->object_chunk_count[plane]; i++) {
        ViewerObjectChunk *chunk = &v->object_chunks[plane][i];
        if (!object_chunk_should_draw(v, chunk))
            continue;
        load_object_chunk_mesh(v, plane, chunk);
        if (chunk->mesh && chunk->mesh->loaded)
            DrawModel(chunk->mesh->model, (Vector3){0, 0, 0}, 1.0f, WHITE);
    }
}

static int resolve_plane_asset_path(char *out, size_t cap,
                                    const char *env_prefix,
                                    const char *base_path, int plane,
                                    const char *ext,
                                    int allow_implicit_variant) {
    if (!out || cap == 0 || !env_prefix || !base_path || !ext)
        return 0;
    char env_key[32];
    snprintf(env_key, sizeof(env_key), "%s%d", env_prefix, plane);
    const char *path = getenv(env_key);
    if (path && path[0]) {
        int n = snprintf(out, cap, "%s", path);
        return n > 0 && (size_t)n < cap;
    }
    if (plane == 0) {
        int n = snprintf(out, cap, "%s", base_path);
        return n > 0 && (size_t)n < cap;
    }
    return allow_implicit_variant
        && plane_path_variant(out, cap, base_path, plane, ext)
        && rc_asset_exists(out);
}

static int load_terrain_plane_asset(ViewerState *v, const char *path,
                                    int plane) {
    if (!v || !path) return 0;
    plane = clamp_plane(plane);
    TerrainMesh *tm = terrain_load(path);
    if (!tm) return 0;
    v->pending_cpu_decode_ms += tm->cpu_decode_ms;
    v->pending_gpu_upload_ms += tm->gpu_upload_ms;
    terrain_offset(tm, g_world_origin_x, g_world_origin_y);
    terrain_free(v->terrain_planes[plane]);
    v->terrain_planes[plane] = tm;
    if (plane == 0 || !v->terrain)
        v->terrain = tm;
    return 1;
}

static int load_object_plane_asset(ViewerState *v, const char *path,
                                   int plane) {
    if (!v || !path) return 0;
    plane = clamp_plane(plane);
    ObjectMesh *om = objects_load(path);
    objects_free(v->object_planes[plane]);
    free_object_anim_plane(v, plane);
    v->object_planes[plane] = NULL;
    if (plane == 0)
        v->objects = NULL;
    if (!om) return 0;
    v->pending_cpu_decode_ms += om->cpu_decode_ms;
    v->pending_gpu_upload_ms += om->gpu_upload_ms;

    objects_offset(om, g_world_origin_x, g_world_origin_y);
    if (v->alpha_cutout_shader_static_loaded)
        objects_set_shader(om, v->alpha_cutout_shader_static);
    v->object_planes[plane] = om;
    load_object_anim_models_for_plane(v, path, plane);
    if (plane == 0)
        v->objects = om;
    return 1;
}

static void load_object_anim_models_for_plane(ViewerState *v,
                                              const char *objects_path,
                                              int plane) {
    if (!v || !objects_path)
        return;
    plane = clamp_plane(plane);
    ObjectMesh *om = v->object_planes[plane];
    if (!om || om->object_anim_count <= 0)
        return;

    char anim_models_path[1024];
    if (companion_path(anim_models_path, sizeof(anim_models_path),
                       objects_path, ".object_anim.models")
            && rc_asset_exists(anim_models_path)) {
        v->object_anim_model_planes[plane] = models_load(anim_models_path);
        if (v->object_anim_model_planes[plane]) {
            if (v->alpha_cutout_shader_static_loaded) {
                models_set_shader(v->object_anim_model_planes[plane],
                                  v->alpha_cutout_shader_static);
            }
            create_object_anim_plane_states(v, plane);
        }
    }
}

static int load_object_anim_sidecar_plane(ViewerState *v,
                                          const char *objects_path,
                                          int plane) {
    if (!v || !objects_path)
        return 0;
    plane = clamp_plane(plane);

    ObjectMesh *om = objects_load_anim_sidecar(objects_path);
    if (!om)
        return 0;

    objects_free(v->object_planes[plane]);
    free_object_anim_plane(v, plane);
    v->object_planes[plane] = om;
    if (plane == 0)
        v->objects = om;
    load_object_anim_models_for_plane(v, objects_path, plane);
    return om->object_anim_count > 0;
}

static void load_terrain_plane_assets(ViewerState *v, const char *base_path) {
    if (!v || !base_path) return;
    for (int plane = 0; plane < RC_MAX_PLANES; plane++) {
        char path[1024];
        if (!resolve_plane_asset_path(path, sizeof(path), "RUNEC_TERRAIN_P",
                                      base_path, plane, ".terrain",
                                      v->preload_scene_planes))
            continue;
        load_terrain_plane_asset(v, path, plane);
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
        char path[1024];
        if (!resolve_plane_asset_path(path, sizeof(path), "RUNEC_OBJECTS_P",
                                      base_path, plane, ".objects",
                                      v->preload_scene_planes))
            continue;
        load_object_plane_asset(v, path, plane);
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

static Shader load_projectile_effect_shader(float brightness, float lift) {
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
    TerrainMesh *terrain = viewer_mapsquare_terrain_at(
        v, plane, world_x, world_y);
    if (!terrain)
        terrain = viewer_terrain_for_plane(v, plane);
    if (terrain && terrain->loaded)
        return terrain_height_avg(terrain, LOCAL_X(world_x), LOCAL_Y(world_y)) + 0.05f;
    return 0.0f;
}

static float ground_y(ViewerState *v, int world_x, int world_y) {
    return ground_y_plane(v, viewer_scene_plane(v), world_x, world_y);
}

static float ground_yf_plane(ViewerState *v, int plane, float world_x,
                             float world_y) {
    TerrainMesh *terrain = viewer_mapsquare_terrain_at(
        v, plane, (int)floorf(world_x), (int)floorf(world_y));
    if (!terrain)
        terrain = viewer_terrain_for_plane(v, plane);
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

static float ground_corner_y_plane(ViewerState *v, int plane,
                                   int tile_x, int tile_y,
                                   int corner_x, int corner_y) {
    TerrainMesh *terrain = viewer_mapsquare_terrain_at(
        v, plane, tile_x, tile_y);
    if (!terrain)
        terrain = viewer_terrain_for_plane(v, plane);
    if (terrain && terrain->loaded && terrain->heightmap) {
        int local_x = tile_x - g_world_origin_x + corner_x;
        int local_y = tile_y - g_world_origin_y + corner_y;
        int hx = local_x - terrain->hm_min_x;
        int hy = local_y - terrain->hm_min_y;
        if (hx >= 0 && hx < terrain->hm_width
                && hy >= 0 && hy < terrain->hm_height) {
            return terrain->heightmap[hx + hy * terrain->hm_width] + 0.08f;
        }
    }
    return ground_yf_plane(v, plane,
                           (float)tile_x + (float)corner_x,
                           (float)tile_y + (float)corner_y) + 0.03f;
}

static void tile_overlay_corners(ViewerState *v, int plane,
                                 int tile_x, int tile_y,
                                 Vector3 corners[4]) {
    static const int offset_x[4] = {0, 1, 1, 0};
    static const int offset_y[4] = {0, 0, 1, 1};
    for (int i = 0; i < 4; i++) {
        int world_x = tile_x + offset_x[i];
        int world_y = tile_y + offset_y[i];
        corners[i] = (Vector3){
            (float)(world_x - g_world_origin_x),
            ground_corner_y_plane(v, plane, tile_x, tile_y,
                                  offset_x[i], offset_y[i]),
            -(float)(world_y - g_world_origin_y),
        };
    }
}

static Vector3 vector3_toward(Vector3 from, Vector3 to, float amount) {
    return (Vector3){
        from.x + (to.x - from.x) * amount,
        from.y + (to.y - from.y) * amount,
        from.z + (to.z - from.z) * amount,
    };
}

static void emit_route_tile_fill(ViewerState *v, int plane,
                                 int tile_x, int tile_y) {
    Vector3 c[4];
    tile_overlay_corners(v, plane, tile_x, tile_y, c);
    for (int i = 0; i < 3; i++)
        rlVertex3f(c[i].x, c[i].y, c[i].z);
    rlVertex3f(c[0].x, c[0].y, c[0].z);
    rlVertex3f(c[2].x, c[2].y, c[2].z);
    rlVertex3f(c[3].x, c[3].y, c[3].z);
}

static void emit_route_tile_edges(ViewerState *v, int plane,
                                  int tile_x, int tile_y) {
    Vector3 c[4];
    tile_overlay_corners(v, plane, tile_x, tile_y, c);
    for (int i = 0; i < 4; i++) {
        Vector3 next = c[(i + 1) % 4];
        rlVertex3f(c[i].x, c[i].y, c[i].z);
        rlVertex3f(next.x, next.y, next.z);
    }
}

// The tiles each NPC occupies in the core (its tile plus its size east and
// north): what pathing, melee reach and line of sight are measured against.
static void draw_npc_footprints(ViewerState *v, int plane,
                                const RcNpc *npcs, int npc_count) {
    for (int pass = 0; pass < 2; pass++) {
        rlSetTexture(0);
        rlBegin(pass == 0 ? RL_TRIANGLES : RL_LINES);
        if (pass == 0) rlColor4ub(0, 200, 255, 56);
        else rlColor4ub(0, 220, 255, 200);
        for (int i = 0; i < npc_count; i++) {
            const RcNpc *n = &npcs[i];
            RcNpcLifePhase life = rc_npc_life_phase(n);
            if (life == RC_NPC_LIFE_REMOVED || life == RC_NPC_LIFE_HIDDEN
                    || n->plane != plane
                    || !viewer_actor_in_draw_range(v, (float)n->x,
                                                   (float)n->y, 0.0f))
                continue;
            const RcNpcDef *def = rc_npc_def_for_npc(v->world, n);
            int size = def && def->size > 0 ? def->size : 1;
            for (int dx = 0; dx < size; dx++) {
                for (int dy = 0; dy < size; dy++) {
                    if (pass == 0)
                        emit_route_tile_fill(v, plane, n->x + dx, n->y + dy);
                    else
                        emit_route_tile_edges(v, plane, n->x + dx, n->y + dy);
                }
            }
        }
        rlEnd();
    }
}

static void draw_hovered_tile(ViewerState *v, int plane,
                              int tile_x, int tile_y) {
    Vector3 c[4];
    tile_overlay_corners(v, plane, tile_x, tile_y, c);
    Color color = (Color){255, 255, 0, 255};
    for (int i = 0; i < 4; i++) {
        DrawLine3D(c[i], vector3_toward(c[i], c[(i + 1) % 4], 0.26f),
                   color);
        DrawLine3D(c[i], vector3_toward(c[i], c[(i + 3) % 4], 0.26f),
                   color);
    }
}

static void draw_route_waypoints(ViewerState *v, int scene_plane,
                                 int start_x, int start_y,
                                 const int *waypoint_x,
                                 const int *waypoint_y,
                                 int waypoint_index, int waypoint_count) {
    if (!v || !waypoint_x || !waypoint_y
            || waypoint_index >= waypoint_count)
        return;
    RuneCRouteOverlayCursor cursor;
    runec_route_overlay_begin(
        &cursor, start_x, start_y, waypoint_x, waypoint_y,
        waypoint_index, waypoint_count);
    int tile_x = 0;
    int tile_y = 0;
    rlSetTexture(0);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(255, 255, 0, 48);
    while (runec_route_overlay_next(&cursor, &tile_x, &tile_y))
        emit_route_tile_fill(v, scene_plane, tile_x, tile_y);
    rlEnd();

    runec_route_overlay_begin(
        &cursor, start_x, start_y, waypoint_x, waypoint_y,
        waypoint_index, waypoint_count);
    rlBegin(RL_LINES);
    rlColor4ub(255, 255, 0, 180);
    while (runec_route_overlay_next(&cursor, &tile_x, &tile_y))
        emit_route_tile_edges(v, scene_plane, tile_x, tile_y);
    rlEnd();
}

static void draw_player_route(ViewerState *v, const RcPlayer *player,
                              int scene_plane) {
    if (!player || player->plane != scene_plane)
        return;
    draw_route_waypoints(
        v, scene_plane, player->x, player->y,
        player->route_x, player->route_y,
        player->route_idx, player->route_len);
}

static int append_unique_model_id(uint32_t *ids, int count, uint32_t id) {
    for (int i = 0; i < count; i++)
        if (ids[i] == id) return count;
    ids[count++] = id;
    return count;
}

static int append_encounter_role_model_ids(const RcWorld *world, uint32_t *ids,
                                           int count, int max_ids) {
    for (int s = 0; count >= 0; s++) {
        const RcEncounterSpec *spec = rc_encounter_spec(world, s);
        if (!spec) break;
        for (int i = 0; i < spec->form_count + spec->helper_count; i++) {
            if (count >= max_ids) return -1;
            uint32_t id = i < spec->form_count
                        ? spec->forms[i].npc_id
                        : spec->helpers[i - spec->form_count].npc_id;
            count = append_unique_model_id(ids, count, id);
        }
    }
    return count;
}

static int viewer_actor_in_draw_range(const ViewerState *v, float x, float y,
                                      float padding) {
    if (!v || !v->world)
        return 0;
    int player_x = 0;
    int player_y = 0;
    int player_plane = 0;
    if (!viewer_presented_player_tile(
            v, &player_x, &player_y, &player_plane)) {
        return 0;
    }
    float radius = v->actor_draw_radius + padding;
    float dx = x - (float)player_x;
    float dy = y - (float)player_y;
    return radius <= 0.0f || dx * dx + dy * dy <= radius * radius;
}

static int collect_spawned_npc_model_ids(ViewerState *v, uint32_t *ids,
                                         int max_ids,
                                         int min_plane, int max_plane) {
    RcWorld *world = v ? v->world : NULL;
    if (!world)
        return 0;
    int count = 0;
    for (int i = 0; i < world->npc_count && count < max_ids; i++) {
        const RcNpc *npc = &world->npcs[i];
        const RcNpcDef *base = rc_npc_base_def_for_npc(npc);
        if (!npc->active || npc->plane < min_plane || npc->plane > max_plane
                || npc->x < g_world_origin_x - 8
                || npc->x >= g_world_origin_x + g_world_w + 8
                || npc->y < g_world_origin_y - 8
                || npc->y >= g_world_origin_y + g_world_h + 8
                || !base) continue;
        int form_ids[RC_MAX_NPC_DEFS];
        int form_count = rc_npc_def_collect_form_ids(
            base, form_ids, RC_MAX_NPC_DEFS);
        if (form_count < 0) {
            fprintf(stderr,
                    "npc models: invalid transform closure for NPC %d\n",
                    base->id);
            return -1;
        }
        for (int j = 0; j < form_count; j++) {
            if (count >= max_ids) return -1;
            count = append_unique_model_id(
                ids, count, (uint32_t)form_ids[j]);
        }
    }
    return append_encounter_role_model_ids(world, ids, count, max_ids);
}

static void free_npc_anim_states(ViewerState *v) {
    if (!v) return;
    for (int i = 0; i < RC_MAX_NPC_DEFS; i++) {
        anim_model_state_free(v->npc_anim_state[i]);
        v->npc_anim_state[i] = NULL;
    }
}

static void create_npc_anim_states(ViewerState *v) {
    if (!v || !v->anims
            || !v->npc_models || !v->npc_models->loaded) {
        return;
    }
    int created = 0;
    int npc_def_count = 0;
    const RcNpcDef *npc_defs = rc_npc_defs_all(&npc_def_count);
    for (int i = 0; npc_defs && i < npc_def_count; i++) {
        ModelEntry *me = model_find(v->npc_models, (uint32_t)npc_defs[i].id);
        if (me && me->loaded && me->vertex_skins && me->base_vert_count > 0) {
            v->npc_anim_state[i] = anim_model_state_create_with_faces(
                me->vertex_skins, me->base_vert_count,
                me->face_skins, me->face_count, me->face_alphas);
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
        calloc(RC_MAX_NPC_DEFS, sizeof(uint32_t));
    int npc_model_id_count = 0;
    if (npc_model_ids) {
        npc_model_id_count = collect_spawned_npc_model_ids(
            v, npc_model_ids, RC_MAX_NPC_DEFS, 0,
            RC_MAX_PLANES - 1);
        if (npc_model_id_count < 0) {
            free(npc_model_ids);
            fprintf(stderr, "npc models: failed to collect scene forms\n");
            return;
        }
    }
    uint32_t empty_model_ids[1] = {0};
    const uint32_t *model_filter = npc_model_ids ? npc_model_ids
                                                 : empty_model_ids;
    v->npc_models = models_load_filtered(
        env_path("RUNEC_NPC_MODELS", "data/models/npcs.models"),
        model_filter, npc_model_id_count);
    free(npc_model_ids);
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

static void minimap_accumulate_terrain(const TerrainMesh *terrain, int w, int h,
                                       unsigned int *sum_r,
                                       unsigned int *sum_g,
                                       unsigned int *sum_b,
                                       unsigned int *count) {
    if (!terrain || !terrain->loaded || terrain->model.meshCount <= 0
            || !sum_r || !sum_g || !sum_b || !count)
        return;
    const Mesh *mesh = &terrain->model.meshes[0];
    if (!mesh->vertices || !mesh->colors)
        return;
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

    size_t tile_count = (size_t)w * (size_t)h;
    unsigned int *sum_r = calloc(tile_count, sizeof(unsigned int));
    unsigned int *sum_g = calloc(tile_count, sizeof(unsigned int));
    unsigned int *sum_b = calloc(tile_count, sizeof(unsigned int));
    unsigned int *count = calloc(tile_count, sizeof(unsigned int));
    if (sum_r && sum_g && sum_b && count) {
        TerrainMesh *terrain = viewer_terrain_for_plane(v, scene_plane);
        minimap_accumulate_terrain(terrain, w, h, sum_r, sum_g, sum_b,
                                   count);
        for (int i = 0; i < v->mapsquare_chunk_count; i++) {
            ViewerMapsquareChunk *chunk = &v->mapsquare_chunks[i];
            if (chunk->plane == scene_plane) {
                minimap_accumulate_terrain(chunk->terrain, w, h, sum_r, sum_g,
                                           sum_b, count);
            }
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

static void clear_aggregate_visual_assets(ViewerState *v) {
    if (!v) return;
    for (int plane = 0; plane < RC_MAX_PLANES; plane++) {
        terrain_free(v->terrain_planes[plane]);
        objects_free(v->object_planes[plane]);
        free_object_anim_plane(v, plane);
        free_object_chunk_plane(v, plane);
        v->terrain_planes[plane] = NULL;
        v->object_planes[plane] = NULL;
    }
    v->terrain = NULL;
    v->objects = NULL;
}

static void unload_scene_visual_assets(ViewerState *v) {
    if (!v) return;
    clear_aggregate_visual_assets(v);
    free_mapsquare_cache(v);
    free(v->minimap_tiles);
    v->minimap_tiles = NULL;
    v->minimap_tiles_w = 0;
    v->minimap_tiles_h = 0;
    v->minimap_tiles_plane = -1;
}

static int scene_bounds_for_tile(int x, int y, int radius_regions,
                                 int *origin_x, int *origin_y,
                                 int *world_w, int *world_h) {
    if (!origin_x || !origin_y || !world_w || !world_h
            || !rc_world_coord_valid(x) || !rc_world_coord_valid(y)
            || radius_regions < 0) {
        return 0;
    }
    int64_t side_wide = (int64_t)radius_regions * 2 + 1;
    if (side_wide <= 0 || side_wide > RC_MAPSQUARE_AXIS) return 0;
    int side = (int)side_wide;
    int center_rx = x / RC_MAPSQUARE_SIZE;
    int center_ry = y / RC_MAPSQUARE_SIZE;
    int min_rx = center_rx - radius_regions;
    int min_ry = center_ry - radius_regions;
    int max_origin_region = RC_MAPSQUARE_AXIS - side;
    if (min_rx < 0) min_rx = 0;
    if (min_ry < 0) min_ry = 0;
    if (min_rx > max_origin_region) min_rx = max_origin_region;
    if (min_ry > max_origin_region) min_ry = max_origin_region;
    *origin_x = min_rx * RC_MAPSQUARE_SIZE;
    *origin_y = min_ry * RC_MAPSQUARE_SIZE;
    *world_w = side * RC_MAPSQUARE_SIZE;
    *world_h = side * RC_MAPSQUARE_SIZE;
    return 1;
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

static int scene_objects_file_complete(const char *objects_path) {
    uint64_t asset_size = 0;
    if (!objects_path || !rc_asset_size(objects_path, &asset_size))
        return 0;

    FILE *f = rc_asset_fopen(objects_path, "rb");
    if (!f)
        return 0;

    uint32_t magic = 0;
    uint32_t placement_count = 0;
    int32_t min_wx = 0;
    int32_t min_wy = 0;
    uint32_t total_verts = 0;
    int ok = rc_read_exact(f, &magic, sizeof(magic), 1, objects_path,
                           "object magic")
          && rc_read_exact(f, &placement_count, sizeof(placement_count), 1,
                           objects_path, "object placement count")
          && rc_read_exact(f, &min_wx, sizeof(min_wx), 1, objects_path,
                           "object min world x")
          && rc_read_exact(f, &min_wy, sizeof(min_wy), 1, objects_path,
                           "object min world y")
          && rc_read_exact(f, &total_verts, sizeof(total_verts), 1,
                           objects_path, "object vertex count");
    rc_asset_close(f);
    (void)placement_count;
    (void)min_wx;
    (void)min_wy;

    if (!ok || (magic != OBJS_MAGIC && magic != OBJ2_MAGIC))
        return 0;

    uint64_t expected = 20ull + (uint64_t)total_verts * 3ull * sizeof(float)
                      + (uint64_t)total_verts * 4ull;
    if (magic == OBJ2_MAGIC)
        expected += (uint64_t)total_verts * 2ull * sizeof(float);
    if (asset_size < expected) {
        fprintf(stderr,
                "viewer scene: incomplete object cache %s (%llu < %llu)\n",
                objects_path, (unsigned long long)asset_size,
                (unsigned long long)expected);
        return 0;
    }
    return 1;
}

static int scene_oanim_file_complete(const char *oanim_path,
                                     int *has_placements) {
    if (has_placements)
        *has_placements = 0;
    uint64_t asset_size = 0;
    if (!oanim_path || !rc_asset_size(oanim_path, &asset_size))
        return 0;

    FILE *f = rc_asset_fopen(oanim_path, "rb");
    if (!f)
        return 0;
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t count = 0;
    int ok = rc_read_exact(f, &magic, sizeof(magic), 1, oanim_path,
                           "object anim magic")
          && rc_read_exact(f, &version, sizeof(version), 1, oanim_path,
                           "object anim version")
          && rc_read_exact(f, &count, sizeof(count), 1, oanim_path,
                           "object anim count");
    rc_asset_close(f);
    uint64_t expected = 12ull + (uint64_t)count * sizeof(ObjectAnimRow);
    if (!ok || magic != OANM_MAGIC || version != OANM_VERSION
            || asset_size < expected) {
        return 0;
    }
    if (has_placements)
        *has_placements = count > 0;
    return 1;
}

static int scene_objects_current(const char *objects_path) {
    time_t objects_mtime;
    if (!path_mtime(objects_path, &objects_mtime))
        return rc_asset_exists(objects_path);
    if (!scene_objects_file_complete(objects_path))
        return 0;
    const char *deps[] = {
        "tools/cache_pipeline/export_scene_slice.py",
        "tools/cache_pipeline/export_terrain.py",
        "tools/cache_pipeline/export_objects.py",
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

static int shell_quote(char *out, size_t cap, const char *s) {
    if (!out || cap == 0 || !s)
        return 0;
    size_t used = 0;
    if (used + 1 >= cap)
        return 0;
    out[used++] = '\'';
    for (const char *p = s; *p; p++) {
        if (*p == '\'') {
            const char *esc = "'\\''";
            size_t n = strlen(esc);
            if (used + n >= cap)
                return 0;
            memcpy(out + used, esc, n);
            used += n;
        } else {
            if (used + 1 >= cap)
                return 0;
            out[used++] = *p;
        }
    }
    if (used + 2 > cap)
        return 0;
    out[used++] = '\'';
    out[used] = '\0';
    return 1;
}

static int validate_viewer_scene_assets(ViewerState *v) {
    if (!v)
        return 0;
    const char *dir = env_path("RUNEC_SCENE_CACHE_DIR",
                               "data/regions/scene_cache");
    int missing = 0;
    int checked = 0;
    for (size_t i = 0;
            i < sizeof(VIEWER_VALIDATION_SCENES)
                    / sizeof(VIEWER_VALIDATION_SCENES[0]);
            i++) {
        const ViewerValidationScene *scene = &VIEWER_VALIDATION_SCENES[i];
        int origin_x = 0;
        int origin_y = 0;
        int world_w = 0;
        int world_h = 0;
        if (!scene_bounds_for_tile(scene->center_x, scene->center_y,
                                   scene->radius_regions,
                                   &origin_x, &origin_y,
                                   &world_w, &world_h)) {
            missing++;
            continue;
        }
        (void)world_w;
        (void)world_h;
        char prefix[1024];
        int n = snprintf(prefix, sizeof(prefix), "%s/scene_%d_%d_r%d",
                         dir, origin_x, origin_y, scene->radius_regions);
        if (n <= 0 || (size_t)n >= sizeof(prefix)
                || !scene_plane_files_exist(prefix, scene->plane)) {
            fprintf(stderr,
                    "viewer smoke scenes: missing %s plane %d at %s\n",
                    scene->key, scene->plane, n > 0 ? prefix : "(overflow)");
            missing++;
            continue;
        }
        checked++;
    }
    if (missing > 0)
        return -1;
    fprintf(stderr, "viewer smoke scenes: PASS checked=%d\n", checked);
    return checked;
}

static int validate_mapsquare_window_assets(ViewerState *v, int center_x,
                                             int center_y, int plane) {
    if (!v)
        return -1;
    ViewerMapsquareCoord plan[VIEWER_STREAMING_CHUNK_CAPACITY];
    int count = viewer_mapsquare_visible_plan(
        v, center_x, center_y, plan, VIEWER_STREAMING_CHUNK_CAPACITY);
    if (count <= 0) {
        fprintf(stderr,
                "viewer smoke mapsquares: invalid window for %d,%d plane "
                "%d in %s\n",
                center_x, center_y, plane, v->mapsquare_directory);
        return -1;
    }

    char atlas_path[1024];
    if (!viewer_mapsquare_material_path(v, atlas_path, sizeof(atlas_path)))
        return -1;
    char tanim_path[1024];
    int n = snprintf(tanim_path, sizeof(tanim_path),
                     "%s/mapsquare.materials.tanim",
                     v->mapsquare_directory);
    if (n <= 0 || (size_t)n >= sizeof(tanim_path)
            || !rc_asset_exists(atlas_path) || !rc_asset_exists(tanim_path)) {
        fprintf(stderr,
                "viewer smoke mapsquares: shared material page is incomplete "
                "in %s\n",
                v->mapsquare_directory);
        return -1;
    }

    if (!viewer_mapsquare_window_assets_available(
            v, center_x, center_y, plane)) {
        for (int i = 0; i < count; i++) {
            if (!viewer_mapsquare_chunk_available(
                    v, plan[i].region_x, plan[i].region_y, plane)) {
                fprintf(stderr,
                        "viewer smoke mapsquares: incomplete chunk %d,%d "
                        "plane %d\n",
                        plan[i].region_x, plan[i].region_y, plane);
                break;
            }
        }
        return -1;
    }
    fprintf(stderr,
            "viewer smoke mapsquares: PASS available=%d planned=%d plane=%d\n",
            count, count, plane);
    return count;
}

static int ensure_scene_slice_plane_assets(ViewerState *v, int center_x,
                                           int center_y, const char *prefix,
                                           int plane) {
    plane = clamp_plane(plane);
    if (scene_plane_files_exist(prefix, plane))
        return 1;
    if (!v->scene_auto_export)
        return 0;

    const char *cache = viewer_scene_export_cache_path();
    if (!cache || !cache[0]) {
        fprintf(stderr,
                "viewer scene: RUNEC_CACHE, RUNEC_B237_CACHE, or %s is "
                "required for scene auto-export\n",
                DEFAULT_B237_CACHE_PATH);
        return 0;
    }
    if (!local_dir_exists(cache)) {
        fprintf(stderr, "viewer scene: cache directory not found: %s\n",
                cache);
        return 0;
    }
    char cache_arg[1200];
    char prefix_arg[1400];
    if (!shell_quote(cache_arg, sizeof(cache_arg), cache)
            || !shell_quote(prefix_arg, sizeof(prefix_arg), prefix)) {
        fprintf(stderr, "viewer scene: scene export path is too long\n");
        return 0;
    }
    char cmd[4096];
    int timeout_seconds = env_int("RUNEC_SCENE_EXPORT_TIMEOUT_SECONDS", 120);
    if (timeout_seconds < 1)
        timeout_seconds = 1;
    int n = snprintf(cmd, sizeof(cmd),
        "timeout %d python3 tools/cache_pipeline/export_scene_slice.py "
        "--center-x %d --center-y %d --radius-regions %d "
        "--cache %s --output-prefix %s --planes %d",
        timeout_seconds, center_x, center_y,
        v->streaming.scene_radius_regions,
        cache_arg, prefix_arg, plane);
    if (n <= 0 || (size_t)n >= sizeof(cmd))
        return 0;
    fprintf(stderr,
            "viewer scene: generating b237 slice around %d,%d plane %d "
            "from %s\n",
            center_x, center_y, plane, cache);
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
    if (!scene_objects_file_complete(objects_path))
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
        v->pending_cpu_decode_ms += tm->cpu_decode_ms;
        v->pending_gpu_upload_ms += tm->gpu_upload_ms;
        terrain_offset(tm, g_world_origin_x, g_world_origin_y);
        v->terrain_planes[plane] = tm;
    }

    ObjectMesh *om = objects_load(objects_path);
    if (om) {
        v->pending_cpu_decode_ms += om->cpu_decode_ms;
        v->pending_gpu_upload_ms += om->gpu_upload_ms;
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
    return v->terrain_planes[plane] && v->object_planes[plane];
}

static int activate_core_area_bounds(ViewerState *v, int origin_x,
                                     int origin_y, int width, int height) {
    if (!v || !v->world)
        return 0;

    RcActiveAreaRequest request = {
        .origin_x = origin_x,
        .origin_y = origin_y,
        .width = width,
        .height = height,
        .min_plane = 0,
        .max_plane = RC_MAX_PLANES - 1,
        .options = RC_ACTIVE_AREA_INCLUDE_INSTANCE_NPCS,
    };
    RcActiveAreaStats stats;
    int ok = rc_world_activate_area(v->world, &request, &stats);
    if (ok > 0) {
        memset(v->npc_render, 0, sizeof(v->npc_render));
        if (v->npc_overlays)
            memset(v->npc_overlays, 0,
                   RC_MAX_NPCS * sizeof(*v->npc_overlays));
        fprintf(stderr,
                "core active area: origin=%d,%d size=%dx%d collision=%d"
                " collision_pages=%u/%u collision_rows=%u/%u"
                " collision_resident=%u/%u"
                " npc_pages=%d npc_rows=%d/%d matched=%d spawned=%d"
                " planes=[%d,%d,%d,%d]"
                " ground_item_pages=%d ground_item_rows=%d/%d"
                " matched=%d spawned=%d"
                " object_pages=%u/%u object_rows=%u/%u"
                " object_resident=%u/%u"
                " dormant_npcs=%d saved/restored=%d/%d"
                " dormant_ground_items=%d saved/restored=%d/%d"
                " backend_pages=%d page_load_ms=%.2f area_load_ms=%.2f\n",
                stats.active_area.origin_x, stats.active_area.origin_y,
                stats.active_area.width, stats.active_area.height,
                stats.collision_regions,
                stats.collision_stats.pages_loaded,
                stats.collision_stats.pages_requested,
                stats.collision_stats.rows_loaded,
                stats.collision_stats.total_rows,
                stats.collision_stats.pages_resident,
                stats.collision_stats.rows_resident,
                stats.npc_stats.pages_loaded,
                stats.npc_stats.rows_loaded, stats.npc_stats.total_rows,
                stats.npc_stats.matched_filter, stats.npc_stats.spawned,
                stats.npc_stats.spawned_plane_counts[0],
                stats.npc_stats.spawned_plane_counts[1],
                stats.npc_stats.spawned_plane_counts[2],
                stats.npc_stats.spawned_plane_counts[3],
                stats.ground_item_stats.pages_loaded,
                stats.ground_item_stats.rows_loaded,
                stats.ground_item_stats.total_rows,
                stats.ground_item_stats.matched_filter,
                stats.ground_item_stats.spawned,
                stats.object_placement_stats.pages_loaded,
                stats.object_placement_stats.pages_requested,
                stats.object_placement_stats.rows_loaded,
                stats.object_placement_stats.total_rows,
                stats.object_placement_stats.pages_resident,
                stats.object_placement_stats.rows_resident,
                stats.streaming.dormant_npc_states,
                stats.streaming.saved_npc_states,
                stats.streaming.restored_npc_states,
                stats.streaming.dormant_ground_items,
                stats.streaming.saved_ground_items,
                stats.streaming.restored_ground_items,
                stats.streaming.backend_pages_loaded,
                stats.streaming.backend_page_load_ms,
                stats.streaming.active_area_load_ms);
        return 1;
    }
    fprintf(stderr, "core active area: activation failed for %d,%d %dx%d\n",
            origin_x, origin_y, width, height);
    return 0;
}

static int activate_core_area_for_scene_bounds(ViewerState *v) {
    int activated = activate_core_area_bounds(
        v, g_world_origin_x, g_world_origin_y, g_world_w, g_world_h);
    if (activated
            && (v->npc_models || v->anims)) {
        reload_npc_models_for_scene(v);
    }
    return activated;
}

static int activate_core_area_around_tile(ViewerState *v, int x, int y) {
    if (!v || !v->world)
        return 0;
    const RcWorldStreamingConfig *config =
        rc_world_get_streaming_config(v->world);
    int radius = config ? config->active_radius_regions
                        : RC_WORLD_STREAMING_DEFAULT_ACTIVE_RADIUS;
    int origin_x = 0;
    int origin_y = 0;
    int width = 0;
    int height = 0;
    if (!scene_bounds_for_tile(x, y, radius, &origin_x, &origin_y,
                               &width, &height)) {
        return 0;
    }
    return activate_core_area_bounds(v, origin_x, origin_y, width, height);
}

static int scene_tile_in_initial_scene(const ViewerState *v, int x, int y) {
    return v && v->initial_scene_ready
        && x >= v->initial_scene_origin_x
        && x < v->initial_scene_origin_x + v->initial_scene_w
        && y >= v->initial_scene_origin_y
        && y < v->initial_scene_origin_y + v->initial_scene_h;
}

static void load_fixed_object_visuals(ViewerState *v, const char *objects_path) {
    if (!v || !objects_path)
        return;
    if (load_object_chunk_index_plane(v, objects_path, 0)) {
        load_object_anim_sidecar_plane(v, objects_path, 0);
        return;
    }
    load_object_plane_assets(v, objects_path);
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
    load_fixed_object_visuals(v, v->initial_objects_path);
    if (!activate_core_area_for_scene_bounds(v))
        return 0;
    build_minimap_tiles(v);
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
    double load_started_ms = viewer_begin_streaming_load(v);
    int required_plane = clamp_plane(v->world->player.plane);
    int mapsquare_ready = viewer_prepare_mapsquare_window(
        v, center_x, center_y, required_plane);
    if (mapsquare_ready > 0) {
        int requested = viewer_request_mapsquare_window(
            v, center_x, center_y, required_plane,
            VIEWER_STREAMING_REQUEST_ACTIVATE,
            "mapsquare-window");
        if (requested > 0)
            return requested;
        fprintf(stderr,
                "viewer mapsquare: complete destination %d,%d plane %d "
                "failed to load; keeping the current scene\n",
                center_x, center_y, required_plane);
        return 0;
    }
    if (mapsquare_ready < 0)
        return 0;
    if (scene_tile_in_initial_scene(v, center_x, center_y)) {
        int loaded = reload_initial_scene(v);
        if (loaded)
            viewer_finish_streaming_load(v, load_started_ms,
                                         "scene-reload", 1);
        return loaded;
    }

    int origin_x, origin_y, world_w, world_h;
    if (!scene_bounds_for_tile(center_x, center_y,
                               v->streaming.scene_radius_regions,
                               &origin_x, &origin_y,
                               &world_w, &world_h)) {
        return 0;
    }

    const char *dir = env_path("RUNEC_SCENE_CACHE_DIR",
                               "data/regions/scene_cache");
    char prefix[1024];
    int n = snprintf(prefix, sizeof(prefix), "%s/scene_%d_%d_r%d",
                     dir, origin_x, origin_y,
                     v->streaming.scene_radius_regions);
    if (n <= 0 || (size_t)n >= sizeof(prefix))
        return 0;
    int scene_available = ensure_scene_slice_plane_assets(
        v, center_x, center_y, prefix, required_plane);
    if (!scene_available)
        return 0;

    unload_scene_visual_assets(v);
    g_world_origin_x = origin_x;
    g_world_origin_y = origin_y;
    g_world_w = world_w;
    g_world_h = world_h;
    int visual_loaded = 0;
    visual_loaded = load_generated_scene_plane_assets(v, prefix,
                                                      required_plane);
    if (!visual_loaded)
        return 0;
    if (required_plane != 0 && scene_plane_files_exist(prefix, 0))
        load_generated_scene_plane_assets(v, prefix, 0);
    if (!activate_core_area_for_scene_bounds(v))
        return 0;
    build_minimap_tiles(v);
    strncpy(v->active_scene_prefix, prefix, sizeof(v->active_scene_prefix) - 1);
    v->active_scene_prefix[sizeof(v->active_scene_prefix) - 1] = '\0';
    fprintf(stderr,
            "viewer scene: loaded generated slice origin %d,%d size %dx%d\n",
            g_world_origin_x, g_world_origin_y, g_world_w, g_world_h);
    viewer_finish_streaming_load(v, load_started_ms, "scene-reload", 1);
    return 1;
}

static int loaded_scene_contains_tile(int x, int y) {
    return x >= g_world_origin_x && x < g_world_origin_x + g_world_w
        && y >= g_world_origin_y && y < g_world_origin_y + g_world_h;
}

static void remember_initial_scene(ViewerState *v, const char *terrain_path,
                                   const char *objects_path) {
    if (!v) return;
    snprintf(v->initial_terrain_path, sizeof(v->initial_terrain_path), "%s",
             terrain_path ? terrain_path : "");
    snprintf(v->initial_objects_path, sizeof(v->initial_objects_path), "%s",
             objects_path ? objects_path : "");
    v->initial_scene_origin_x = g_world_origin_x;
    v->initial_scene_origin_y = g_world_origin_y;
    v->initial_scene_w = g_world_w;
    v->initial_scene_h = g_world_h;
    v->initial_scene_ready = 1;
}

static int load_fixed_startup_scene(ViewerState *v, const char *terrain_path,
                                    const char *objects_path) {
    if (!v || !terrain_path || !objects_path)
        return 0;
    unload_scene_visual_assets(v);
    remember_initial_scene(v, terrain_path, objects_path);
    load_terrain_plane_assets(v, terrain_path);
    load_fixed_object_visuals(v, objects_path);
    if (!v->terrain) {
        fprintf(stderr, "viewer scene: fixed startup terrain failed to load\n");
        return 0;
    }
    if (!activate_core_area_for_scene_bounds(v))
        return 0;
    build_minimap_tiles(v);
    v->active_scene_prefix[0] = '\0';
    fprintf(stderr,
            "viewer scene: loaded fixed scene origin %d,%d size %dx%d\n",
            g_world_origin_x, g_world_origin_y, g_world_w, g_world_h);
    return 1;
}

static int generated_scene_prefix_for_start(ViewerState *v, char *out,
                                            size_t cap, int *origin_x,
                                            int *origin_y, int *world_w,
                                            int *world_h) {
    if (!v || !out || cap == 0)
        return 0;
    int ox = 0, oy = 0, ww = 0, wh = 0;
    if (!scene_bounds_for_tile(g_player_start_x, g_player_start_y,
                               v->streaming.preload_radius_regions,
                               &ox, &oy, &ww, &wh)) {
        return 0;
    }
    const char *dir = env_path("RUNEC_SCENE_CACHE_DIR",
                               "data/regions/scene_cache");
    int n = snprintf(out, cap, "%s/scene_%d_%d_r%d", dir, ox, oy,
                     v->streaming.preload_radius_regions);
    if (n <= 0 || (size_t)n >= cap)
        return 0;
    if (origin_x) *origin_x = ox;
    if (origin_y) *origin_y = oy;
    if (world_w) *world_w = ww;
    if (world_h) *world_h = wh;
    return 1;
}

static void print_generated_scene_hint(ViewerState *v, const char *prefix,
                                       int plane) {
    if (!v || !prefix)
        return;
    fprintf(stderr,
            "viewer scene: generated startup scene is missing or stale: %s "
            "(plane %d)\n",
            prefix, plane);
    fprintf(stderr,
            "viewer scene: prewarm with: python3 "
            "tools/cache_pipeline/export_scene_slice.py --center-x %d "
            "--center-y %d --radius-regions %d --output-prefix %s "
            "--planes 0,1,2,3\n",
            g_player_start_x, g_player_start_y,
            v->streaming.preload_radius_regions,
            prefix);
}

static int load_generated_startup_scene(ViewerState *v, int required) {
    if (!v || !v->world)
        return 0;

    char prefix[1024];
    int origin_x = 0, origin_y = 0, world_w = 0, world_h = 0;
    if (!generated_scene_prefix_for_start(v, prefix, sizeof(prefix),
                                          &origin_x, &origin_y,
                                          &world_w, &world_h))
        return 0;

    int required_plane = clamp_plane(v->world->player.plane);
    if (!ensure_scene_slice_plane_assets(v, g_player_start_x, g_player_start_y,
                                         prefix, 0)) {
        if (required)
            print_generated_scene_hint(v, prefix, 0);
        return 0;
    }
    if (required_plane != 0
            && !ensure_scene_slice_plane_assets(v, g_player_start_x,
                                                g_player_start_y, prefix,
                                                required_plane)) {
        if (required)
            print_generated_scene_hint(v, prefix, required_plane);
        return 0;
    }

    unload_scene_visual_assets(v);
    g_world_origin_x = origin_x;
    g_world_origin_y = origin_y;
    g_world_w = world_w;
    g_world_h = world_h;
    char terrain_path[1024];
    char objects_path[1024];
    scene_plane_file(terrain_path, sizeof(terrain_path), prefix, 0,
                     ".terrain");
    scene_plane_file(objects_path, sizeof(objects_path), prefix, 0,
                     ".objects");
    remember_initial_scene(v, terrain_path, objects_path);
    if (!load_generated_scene_plane_assets(v, prefix, 0))
        return 0;
    if (required_plane != 0)
        load_generated_scene_plane_assets(v, prefix, required_plane);
    if (!v->terrain) {
        if (required)
            fprintf(stderr,
                    "viewer scene: generated startup terrain failed to load\n");
        return 0;
    }
    if (!activate_core_area_for_scene_bounds(v))
        return 0;
    build_minimap_tiles(v);
    strncpy(v->active_scene_prefix, prefix, sizeof(v->active_scene_prefix) - 1);
    v->active_scene_prefix[sizeof(v->active_scene_prefix) - 1] = '\0';
    fprintf(stderr,
            "viewer scene: loaded generated startup slice origin %d,%d "
            "size %dx%d\n",
            g_world_origin_x, g_world_origin_y, g_world_w, g_world_h);
    return 1;
}

static int load_startup_scene(ViewerState *v, ViewerSceneMode mode,
                              const char *fixed_terrain,
                              const char *fixed_objects) {
    if (!v)
        return 0;
    double load_started_ms = viewer_begin_streaming_load(v);
    int loaded = 0;
    int mapsquare_ready = viewer_prepare_mapsquare_window(
        v, g_player_start_x, g_player_start_y, v->world->player.plane);
    if (mapsquare_ready > 0) {
        loaded = viewer_request_mapsquare_window(
            v, g_player_start_x, g_player_start_y,
            v->world->player.plane, VIEWER_STREAMING_REQUEST_ACTIVATE,
            "startup-scene");
        while (loaded == 2 && !WindowShouldClose()) {
            int processed = viewer_process_mapsquare_loading(v);
            if (processed != 0)
                loaded = processed > 0;
            if (loaded == 2) {
                BeginDrawing();
                ClearBackground((Color){40, 45, 55, 255});
                EndDrawing();
            }
        }
        if (loaded == 2)
            loaded = 0;
        if (!loaded) {
            fprintf(stderr,
                    "viewer mapsquare: complete startup window failed to "
                    "load\n");
            return 0;
        }
    }
    if (mapsquare_ready < 0)
        return 0;
    if (!loaded && mode == VIEWER_SCENE_MODE_GENERATED) {
        loaded = load_generated_startup_scene(v, 1);
    } else if (!loaded && mode == VIEWER_SCENE_MODE_AUTO
            && load_generated_startup_scene(v, 0)) {
        loaded = 1;
    } else if (!loaded) {
        loaded = load_fixed_startup_scene(v, fixed_terrain, fixed_objects);
    }
    if (loaded && mapsquare_ready <= 0)
        viewer_finish_streaming_load(v, load_started_ms, "startup-scene", 1);
    return loaded;
}

static const char *startup_npc_models_path(ViewerState *v) {
    if (env_has_value("RUNEC_NPC_MODELS"))
        return env_path("RUNEC_NPC_MODELS", "data/models/npcs.models");

    const char *override = getenv("RUNEC_STARTUP_NPC_MODELS");
    if (override && override[0])
        return override;

    const char *varrock_candidate = "data/models/npcs_varrock.models";
    if (v && !v->active_scene_prefix[0]
            && !env_has_value("RUNEC_TERRAIN")
            && !env_has_value("RUNEC_OBJECTS")
            && !env_has_value("RUNEC_WORLD_ORIGIN_X")
            && !env_has_value("RUNEC_WORLD_ORIGIN_Y")
            && !env_has_value("RUNEC_WORLD_W")
            && !env_has_value("RUNEC_WORLD_H")
            && !env_has_value("RUNEC_NPC_SPAWNS")
            && g_world_origin_x == DEFAULT_WORLD_ORIGIN_X
            && g_world_origin_y == DEFAULT_WORLD_ORIGIN_Y
            && g_world_w == DEFAULT_WORLD_W
            && g_world_h == DEFAULT_WORLD_H
            && rc_asset_exists(varrock_candidate)) {
        return varrock_candidate;
    }

    const char *candidate = "data/models/npcs_varrock_bank.models";
    if (v && v->active_scene_prefix[0]
            && v->streaming.preload_radius_regions == 0
            && g_player_start_x == DEFAULT_PLAYER_START_X
            && g_player_start_y == DEFAULT_PLAYER_START_Y
            && rc_asset_exists(candidate)) {
        return candidate;
    }
    return "data/models/npcs.models";
}

static void ensure_active_scene_plane(ViewerState *v, int plane) {
    if (!v || !v->world)
        return;
    plane = clamp_plane(plane);
    if (v->terrain_planes[plane] || v->object_planes[plane])
        return;
    if (v->mapsquare_streaming_active) {
        if (viewer_mapsquare_plane_loaded(v, plane))
            return;
        RcPlayer *p = &v->world->player;
        int mapsquare_ready = viewer_prepare_mapsquare_window(
            v, p->x, p->y, plane);
        int requested = mapsquare_ready > 0
            ? viewer_request_mapsquare_window(
                v, p->x, p->y, plane, VIEWER_STREAMING_REQUEST_PLANE,
                "mapsquare-plane")
            : 0;
        if (requested <= 0 && v->scene_plane_override == plane
                   && plane != p->plane) {
            v->scene_plane_override = -1;
        }
        return;
    }
    double load_started_ms = viewer_begin_streaming_load(v);

    if (!v->active_scene_prefix[0] && v->initial_scene_ready) {
        char terrain_path[1024];
        char objects_path[1024];
        int loaded = 0;
        if (resolve_plane_asset_path(terrain_path, sizeof(terrain_path),
                                     "RUNEC_TERRAIN_P",
                                     v->initial_terrain_path, plane,
                                     ".terrain", 1)) {
            loaded |= load_terrain_plane_asset(v, terrain_path, plane);
        }
        if (resolve_plane_asset_path(objects_path, sizeof(objects_path),
                                     "RUNEC_OBJECTS_P",
                                     v->initial_objects_path, plane,
                                     ".objects", 1)) {
            if (load_object_chunk_index_plane(v, objects_path, plane))
                loaded = 1;
            else
                loaded |= load_object_plane_asset(v, objects_path, plane);
        }
        if (loaded) {
            build_minimap_tiles(v);
            viewer_finish_streaming_load(v, load_started_ms,
                                         "scene-plane", 1);
        }
        return;
    }

    if (!v->active_scene_prefix[0])
        return;

    RcPlayer *p = &v->world->player;
    if (!ensure_scene_slice_plane_assets(v, p->x, p->y,
                                         v->active_scene_prefix, plane)) {
        return;
    }
    if (load_generated_scene_plane_assets(v, v->active_scene_prefix, plane)) {
        build_minimap_tiles(v);
        viewer_finish_streaming_load(v, load_started_ms, "scene-plane", 1);
    }
}

static void viewer_sync_dev_transport_labels(RuneCUiState *ui) {
    ui->dev_spellbooks_enabled = runec_dev_validation_enabled();
    const char *labels[RUNEC_UI_DEV_TRANSPORT_MAX] = {0};
    int count = 0;
    const RuneCDevTransport *transports =
        runec_dev_validation_transports(&count);
    if (count > RUNEC_UI_DEV_TRANSPORT_MAX)
        count = RUNEC_UI_DEV_TRANSPORT_MAX;
    for (int i = 0; i < count; i++)
        labels[i] = transports[i].label;
    runec_ui_set_dev_transport_options(ui, labels, count);
}

static void viewer_sync_scene_plane_ui(ViewerState *v) {
    if (!v || !v->world)
        return;
    runec_ui_set_scene_plane_state(&v->ui, viewer_scene_plane(v),
                                   v->world->player.plane,
                                   v->scene_plane_override >= 0);
}

static int viewer_focus_npc_idx(ViewerState *v,
                                const RuneCDevTransport *d) {
    if (!v || !v->world || !d || d->npc_id < 0)
        return -1;
    return rc_world_find_npc_near(v->world, d->npc_id, d->target_x,
                                  d->target_y, d->plane, 8);
}

static int viewer_ensure_focus_npc(ViewerState *v,
                                   const RuneCDevTransport *d) {
    if (!v || !v->world || !d || d->npc_id < 0)
        return 0;
    RcNpcEnsureResult result;
    int idx = rc_world_ensure_npc_near(v->world, d->npc_id, d->target_x,
                                       d->target_y, d->plane, 8, &result);
    if (idx < 0) {
        fprintf(stderr, "dev transport: missing or unspawnable NPC %d for %s\n",
                d->npc_id, d->label);
        return 0;
    }
    if (!result.spawned)
        return 0;
    if (result.index >= 0 && result.index < RC_MAX_NPCS) {
        memset(&v->npc_render[result.index], 0,
               sizeof(v->npc_render[result.index]));
        memset(&v->npc_overlays[result.index], 0,
               sizeof(v->npc_overlays[result.index]));
    }
    fprintf(stderr, "dev transport: core spawned %s NPC %d at %d,%d,%d\n",
            d->label, d->npc_id, d->target_x, d->target_y, d->plane);
    return 1;
}

static void viewer_start_dev_boss_combat(ViewerState *v,
                                         const RuneCDevTransport *d) {
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
    }
}

static void viewer_clear_player_activity(ViewerState *v) {
    if (!v || !v->world)
        return;
    RcWorld *world = v->world;
    rc_player_cancel_action(world, RC_ACTION_CANCEL_FRONTEND);
    v->player_action_anim_timer = 0;
    v->player_action_anim_id = -1;
    v->player_attack_anim_timer = 0;
    v->player_attack_anim_id = -1;
    v->combat_projectile_count = 0;
    runec_ui_clear_selected_target(&v->ui);
    v->ui.context_open = 0;
    reset_viewer_context(v);
}

static int viewer_dev_tile_safe(const ViewerState *v, int x, int y,
                                int plane, const RuneCDevTransport *d) {
    if (!v || !v->world || !d || rc_tile_blocked(&v->world->map, x, y, plane))
        return 0;
    if (!rc_can_move(&v->world->map, x, y, 1, 0, plane)
            && !rc_can_move(&v->world->map, x, y, -1, 0, plane)
            && !rc_can_move(&v->world->map, x, y, 0, 1, plane)
            && !rc_can_move(&v->world->map, x, y, 0, -1, plane))
        return 0;
    if (d->npc_id < 0)
        return 1;
    int size = d->npc_size > 0 ? d->npc_size : 1;
    int max_x = d->target_x + size - 1;
    int max_y = d->target_y + size - 1;
    return x < d->target_x || x > max_x || y < d->target_y || y > max_y;
}

typedef struct {
    int x, y, score;
} ViewerDevLandingTile;

static int compare_dev_landing_tiles(const void *a, const void *b) {
    const ViewerDevLandingTile *lhs = a, *rhs = b;
    if (lhs->score != rhs->score) return lhs->score - rhs->score;
    if (lhs->x != rhs->x) return lhs->x - rhs->x;
    return lhs->y - rhs->y;
}

static int viewer_dev_player_tile(ViewerState *v,
                                   const RuneCDevTransport *d,
                                   int *out_x, int *out_y) {
    // Visual streaming does not activate core collision. Load it before searching.
    if (rc_world_activate_area_around(v->world, d->target_x, d->target_y,
                                      d->plane, NULL) < 0)
        return 0;
    int size = d->npc_size > 0 ? d->npc_size : 1;
    int desired_x = d->target_x;
    int desired_y = d->npc_id >= 0 ? d->target_y - (size + 4) : d->target_y;
    int plane = clamp_plane(d->plane);
    ViewerDevLandingTile candidates[41 * 41];
    int count = 0;
    RcRouteTarget target = rc_route_target_rectangle(
        d->target_x, d->target_y, size, size, 1, 1, false, false);

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
            candidates[count++] = (ViewerDevLandingTile){x, y, score};
        }
    }
    qsort(candidates, (size_t)count, sizeof(candidates[0]), compare_dev_landing_tiles);
    for (int i = 0; i < count; i++) {
        if (d->npc_id >= 0) {
            RcRoute route = rc_find_route(&v->world->map, candidates[i].x, candidates[i].y,
                                          1, 1, plane, &target, false);
            if (route.status != RC_ROUTE_EXACT && route.status != RC_ROUTE_ALREADY_ARRIVED)
                continue;
        }
        *out_x = candidates[i].x;
        *out_y = candidates[i].y;
        return 1;
    }
    return 0;
}

static void viewer_dev_transport_to(ViewerState *v,
                                    const RuneCDevTransport *d) {
    if (!v || !v->world || !d)
        return;
    RcPlayer *p = &v->world->player;
    int old_plane = p->plane;

    viewer_clear_player_activity(v);
    p->plane = clamp_plane(d->plane);
    int reload_status = reload_scene_around_player(
        v, d->target_x, d->target_y);
    p->plane = old_plane;
    if (reload_status <= 0) {
        fprintf(stderr,
                "dev transport: blocked %s because destination visuals for "
                "%d,%d,%d are unavailable\n",
                d->label, d->target_x, d->target_y, d->plane);
        return;
    }
    if (reload_status == 2) {
        v->pending_dev_transport = d;
        fprintf(stderr,
                "dev transport: loading %s destination %d,%d,%d\n",
                d->label, d->target_x, d->target_y, d->plane);
        return;
    }
    v->pending_dev_transport = NULL;

    viewer_finish_dev_transport(v, d);
}

static void viewer_finish_dev_transport(ViewerState *v,
                                        const RuneCDevTransport *d) {
    if (!v || !v->world || !d)
        return;
    RcPlayer *p = &v->world->player;

    int player_x = d->target_x;
    int player_y = d->target_y;
    if (!viewer_dev_player_tile(v, d, &player_x, &player_y)
            || !rc_world_relocate_player(v->world, player_x, player_y,
                                         clamp_plane(d->plane))) {
        fprintf(stderr, "dev transport: %s rejected: cannot activate a reachable landing tile\n", d->label);
        runec_ui_add_chat_message(&v->ui, "Teleport rejected: cannot activate a reachable landing tile.");
        if (rc_world_activate_area_around(v->world, p->x, p->y, p->plane, NULL) < 0)
            fprintf(stderr, "dev transport: failed to restore source collision\n");
        reload_scene_around_player(v, p->x, p->y);
        return;
    }
    p->facing_entity = -1;
    p->facing_x = d->target_x;
    p->facing_y = d->target_y;
    v->prev_player_x = (float)player_x;
    v->prev_player_y = (float)player_y;
    rc_viewer_tick_pacing_reset(&v->tick_pacing);
    v->tick_frac = 0.0f;
    v->player_moving = 0;
    v->scene_plane_override = -1;
    runec_input_feedback_reset(&v->input_feedback);
    v->route_preview_active = 0;

    ensure_active_scene_plane(v, p->plane);
    if (strcmp(d->key, "varrock") == 0)
        runec_dev_validation_spawn_varrock_bank_dummy(v->world);
    int prepared = runec_dev_validation_prepare_encounter(v->world, d);
    if (prepared <= 0)
        viewer_ensure_focus_npc(v, d);
    if (prepared <= 0)
        viewer_start_dev_boss_combat(v, d);
    fprintf(stderr, "dev transport: %s -> player %d,%d,%d target %d,%d,%d\n",
            d->label, p->x, p->y, p->plane,
            d->target_x, d->target_y, d->plane);
}

static Color minimap_tile_color(const ViewerState *v, int wx, int wy) {
    int lx = wx - g_world_origin_x;
    int ly = wy - g_world_origin_y;
    if (!v->minimap_tiles || lx < 0 || ly < 0
            || lx >= v->minimap_tiles_w || ly >= v->minimap_tiles_h)
        return (Color){42, 62, 38, 255};
    return v->minimap_tiles[lx + ly * v->minimap_tiles_w];
}

static int route_player_to(ViewerState *v, int tx, int ty) {
    RcPlayer *player = &v->world->player;
    int accepted = v->world->player.running
        ? rc_player_run_to(v->world, tx, ty)
        : rc_player_walk_to(v->world, tx, ty);
    v->route_preview_active = 0;
    if (accepted) {
        runec_input_feedback_start_destination(
            &v->input_feedback, tx, ty, player->plane,
            v->world->tick);
        RcRouteTarget target = rc_route_target_point(tx, ty);
        RcRoute preview = rc_find_route(
            &v->world->map, player->x, player->y,
            1, 1, player->plane, &target, false);
        if (rc_route_status_has_path(preview.status)) {
            v->route_preview = preview;
            v->route_preview_active = 1;
            v->route_preview_start_x = player->x;
            v->route_preview_start_y = player->y;
            v->route_preview_plane = player->plane;
        }
    }
    return accepted;
}

static void viewer_start_click_feedback(
    ViewerState *v, RuneCClickFeedbackStyle style) {
    Vector2 mouse = GetMousePosition();
    runec_input_feedback_start_click(
        &v->input_feedback, style, (int)lroundf(mouse.x),
        (int)lroundf(mouse.y));
}

static void viewer_draw_click_feedback(const ViewerState *v) {
    int frame = runec_input_feedback_click_frame(&v->input_feedback);
    if (frame < 0)
        return;
    int asset_frame = frame;
    if (v->input_feedback.click_style == RUNEC_CLICK_FEEDBACK_RED)
        asset_frame += RUNEC_CLICK_FEEDBACK_FRAME_COUNT;
    char asset_name[32];
    snprintf(asset_name, sizeof(asset_name), "clickcross_%d", asset_frame);
    const Texture2D *texture = runec_ui_asset(&v->ui.assets, asset_name);
    if (!texture)
        return;
    DrawTexture(*texture,
                v->input_feedback.click_screen_x - texture->width / 2,
                v->input_feedback.click_screen_y - texture->height / 2,
                WHITE);
}

static int viewer_tile_in_loaded_scene(int x, int y) {
    return x >= g_world_origin_x && x < g_world_origin_x + g_world_w
        && y >= g_world_origin_y && y < g_world_origin_y + g_world_h;
}

static void viewer_defer_player_scene_transition(ViewerState *v, int old_x,
                                                 int old_y, int old_plane) {
    RcPlayer *p = &v->world->player;
    viewer_streaming_player_transition_begin(
        &v->mapsquare_load.player_transition,
        v->traversal_generation,
        old_x, old_y, old_plane, p->x, p->y, p->plane);
    const ViewerStreamingPlayerTransition *transition =
        &v->mapsquare_load.player_transition;
    fprintf(stderr,
            "viewer scene: retaining source visuals %d,%d,%d -> %d,%d,%d\n",
            transition->source_x, transition->source_y,
            transition->source_plane, transition->destination_x,
            transition->destination_y, transition->destination_plane);
}

static void viewer_commit_player_scene_transition(ViewerState *v) {
    if (!v || !v->world)
        return;
    int x = 0;
    int y = 0;
    int plane = 0;
    if (!viewer_streaming_player_transition_commit(
            &v->mapsquare_load.player_transition, v->traversal_generation,
            v->world->player.x, v->world->player.y,
            v->world->player.plane, &x, &y, &plane)) {
        memset(&v->mapsquare_load.player_transition, 0,
               sizeof(v->mapsquare_load.player_transition));
        return;
    }
    RcPlayer *p = &v->world->player;
    v->prev_player_x = (float)p->x;
    v->prev_player_y = (float)p->y;
    v->tick_frac = 0.0f;
    v->player_moving = 0;
    fprintf(stderr,
            "viewer scene: committed visual transition %d,%d,%d "
            "(player now %d,%d,%d)\n",
            x, y, plane, p->x, p->y, p->plane);
}

static void handle_player_scene_transition(ViewerState *v, int old_x,
                                           int old_y, int old_plane) {
    if (!v || !v->world)
        return;
    if (v->mapsquare_load.active
            && v->mapsquare_load.player_transition.active) {
        return;
    }
    RcPlayer *p = &v->world->player;
    int large_step = abs(p->x - old_x) > 2 || abs(p->y - old_y) > 2;
    if (old_plane != p->plane || large_step) {
        v->scene_plane_override = -1;
        v->prev_player_x = (float)p->x;
        v->prev_player_y = (float)p->y;
        v->tick_frac = 0.0f;
        v->player_moving = 0;
    }

    if (old_plane != p->plane && !v->mapsquare_streaming_active
            && viewer_tile_in_loaded_scene(p->x, p->y)) {
        ensure_active_scene_plane(v, p->plane);
        build_minimap_tiles(v);
    }

    int mapsquare_window_changed = v->mapsquare_streaming_active
        && ((p->x / VIEWER_STREAMING_MAPSQUARE_SIZE)
                != v->mapsquare_center_region_x
            || (p->y / VIEWER_STREAMING_MAPSQUARE_SIZE)
                != v->mapsquare_center_region_y
            || (v->scene_plane_override < 0
                && !viewer_mapsquare_plane_loaded(v, p->plane)));
    if (mapsquare_window_changed
            || !viewer_tile_in_loaded_scene(p->x, p->y)) {
        int reload_status = reload_scene_around_player(v, p->x, p->y);
        if (reload_status == 1) {
            int display_plane = viewer_scene_plane(v);
            if (v->mapsquare_streaming_active
                    && display_plane != p->plane) {
                double load_started_ms = viewer_begin_streaming_load(v);
                int mapsquare_ready = viewer_prepare_mapsquare_window(
                    v, p->x, p->y, display_plane);
                int requested = mapsquare_ready > 0
                    ? viewer_request_mapsquare_window(
                        v, p->x, p->y, display_plane,
                        VIEWER_STREAMING_REQUEST_PLANE,
                        "mapsquare-plane")
                    : 0;
                if (requested <= 0) {
                    v->scene_plane_override = -1;
                }
            }
            return;
        }
        if (reload_status == 2) {
            viewer_defer_player_scene_transition(v, old_x, old_y, old_plane);
            return;
        }
        fprintf(stderr,
                "viewer scene: visual destination %d,%d,%d is not ready; "
                "retaining the current scene without moving the player\n",
                p->x, p->y, p->plane);
    }
}

static void viewer_update_movement_prefetch(ViewerState *v) {
    if (!v || !v->world || !v->mapsquare_streaming_active)
        return;
    ViewerPendingMapsquareLoad *load = &v->mapsquare_load;
    RcPlayer *p = &v->world->player;
    if (load->active
            && load->request_kind
                == VIEWER_STREAMING_REQUEST_TRANSPORT_PREFETCH) {
        if (p->interaction.active || p->traversal.active)
            return;
        viewer_cancel_mapsquare_loading(v);
        load = &v->mapsquare_load;
    }
    if (load->active
            && load->request_kind
                != VIEWER_STREAMING_REQUEST_MOVEMENT_PREFETCH) {
        return;
    }
    if (load->player_transition.active)
        return;

    int center_x = 0;
    int center_y = 0;
    int have_route = p->route_idx < p->route_len;
    int predicted = have_route && viewer_streaming_predict_prefetch_center(
        v->mapsquare_center_region_x, v->mapsquare_center_region_y,
        p->x, p->y, p->route_x[p->route_len - 1],
        p->route_y[p->route_len - 1], VIEWER_MOVEMENT_PREFETCH_EDGE_TILES,
        &center_x, &center_y);
    if (!predicted)
        return;
    if (load->active && viewer_streaming_same_window(
            load->center_x, load->center_y, load->plane,
            center_x, center_y, p->plane)) {
        return;
    }
    if (viewer_prepare_mapsquare_window(
            v, center_x, center_y, p->plane) <= 0) {
        return;
    }
    viewer_request_mapsquare_window(
        v, center_x, center_y, p->plane,
        VIEWER_STREAMING_REQUEST_MOVEMENT_PREFETCH,
        "movement-prefetch");
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
    if (v->ui.selected_target.kind == RUNEC_UI_SELECTED_SPELL)
        return runec_ui_spell_runtime_id(&v->ui, v->ui.selected_target.source_slot);
    return v->world->player.selected_spell;
}

static int item_display_id_for_quantity(const ViewerState *v, int item_id, int quantity);

enum {
    UI_ITEM_CONTAINER_INVENTORY = 0,
    UI_ITEM_CONTAINER_EQUIPMENT,
    UI_ITEM_CONTAINER_BANK,
};

static int item_action_is_equip(const char *action) {
    return action && (strcmp(action, "Wear") == 0
        || strcmp(action, "Wield") == 0
        || strcmp(action, "Hold") == 0
        || strcmp(action, "Equip") == 0);
}

static void ui_slot_add_action(RuneCUiSlot *slot, const char *action, int op) {
    if (!slot || !action || !action[0]
            || slot->action_count >= RUNEC_UI_ITEM_ACTIONS) {
        return;
    }
    for (int i = 0; i < slot->action_count; i++) {
        if (strcmp(slot->actions[i], action) == 0) return;
    }
    int idx = slot->action_count++;
    snprintf(slot->actions[idx], sizeof(slot->actions[idx]), "%s", action);
    slot->action_ops[idx] = op;
}

static void sync_ui_slot(const ViewerState *v, RuneCUiSlot *dst,
                         const RcInvSlot *src, int container_kind) {
    if (!dst || !src || src->item_id < 0 || src->quantity <= 0) {
        if (dst) memset(dst, 0, sizeof(*dst));
        return;
    }
    const RcItemDef *def = rc_item_def_get(src->item_id);
    memset(dst, 0, sizeof(*dst));
    dst->item_id = (uint32_t)src->item_id;
    dst->icon_item_id = (uint32_t)item_display_id_for_quantity(v, src->item_id,
                                                               src->quantity);
    dst->quantity = src->quantity;
    dst->generation = src->generation;
    snprintf(dst->label, sizeof(dst->label), "%s",
             def ? def->name : TextFormat("Item %d", src->item_id));
    dst->enabled = 1;
    if (container_kind == UI_ITEM_CONTAINER_INVENTORY) {
        for (int i = 0; def && i < RC_ITEM_ACTION_COUNT; i++) {
            if (rc_consumable_action(src->item_id, i))
                ui_slot_add_action(dst, def->inventory_actions[i], i);
        }
        if (def && def->equippable && def->equipable_by_player) {
            for (int i = 0; i < RC_ITEM_ACTION_COUNT; i++) {
                if (item_action_is_equip(def->inventory_actions[i])) {
                    ui_slot_add_action(dst, def->inventory_actions[i],
                                       RUNEC_UI_ITEM_OP_EQUIP);
                    break;
                }
            }
        }
        ui_slot_add_action(dst, "Use", RUNEC_UI_ITEM_OP_USE);
        if (def) {
            for (int i = 0; i < RC_ITEM_ACTION_COUNT; i++) {
                if (strcmp(def->inventory_actions[i], "Drop") == 0) {
                    ui_slot_add_action(dst, def->inventory_actions[i],
                                       RUNEC_UI_ITEM_OP_DROP);
                    break;
                }
            }
        }
        ui_slot_add_action(dst, "Examine", RUNEC_UI_ITEM_OP_EXAMINE);
    } else if (container_kind == UI_ITEM_CONTAINER_EQUIPMENT) {
        ui_slot_add_action(dst, "Remove", RUNEC_UI_ITEM_OP_REMOVE);
        ui_slot_add_action(dst, "Examine", RUNEC_UI_ITEM_OP_EXAMINE);
    }
}

static void sync_ui_items(ViewerState *v) {
    const RcPlayer *p = &v->world->player;
    for (int i = 0; i < RUNEC_UI_INV_SLOT_COUNT; i++)
        sync_ui_slot(v, &v->ui.inventory[i], &p->inventory[i],
                     UI_ITEM_CONTAINER_INVENTORY);
    if (v->ui.selected_inventory_slot >= 0
            && !v->ui.inventory[v->ui.selected_inventory_slot].enabled) {
        v->ui.selected_inventory_slot = -1;
    }

    for (int i = 0; i < RUNEC_UI_EQUIP_SLOT_COUNT; i++) {
        memset(&v->ui.equipment[i], 0, sizeof(v->ui.equipment[i]));
    }
    for (int core = 0; core < RC_EQUIP_COUNT; core++) {
        int ui_slot = core_equip_slot_to_ui(core);
        if (ui_slot >= 0)
            sync_ui_slot(v, &v->ui.equipment[ui_slot], &p->equipment[core],
                         UI_ITEM_CONTAINER_EQUIPMENT);
    }
    if (v->ui.selected_equipment_slot >= 0
            && !v->ui.equipment[v->ui.selected_equipment_slot].enabled) {
        v->ui.selected_equipment_slot = -1;
    }

    v->ui.bank_open = p->storage_kind == RC_STORAGE_BANK ||
                      p->storage_kind == RC_STORAGE_DEPOSIT_BOX;
    if (v->ui.bank_session != p->storage_session) {
        v->ui.bank_session = p->storage_session;
        v->ui.bank_amount_source = RUNEC_UI_CONTEXT_NONE;
        if (v->ui.context_source_kind == RUNEC_UI_CONTEXT_BANK
                || v->ui.context_source_kind == RUNEC_UI_CONTEXT_BANK_INVENTORY)
            v->ui.context_open = 0;
    }
    v->ui.bank_kind = p->storage_kind;
    if (v->ui.bank_open && v->ui.active_tab != RUNEC_UI_TAB_INVENTORY)
        runec_ui_set_active_tab(&v->ui, RUNEC_UI_TAB_INVENTORY);
    if (!v->ui.bank_open)
        v->ui.bank_scroll = 0;
    for (int i = 0; i < RUNEC_UI_BANK_SLOT_COUNT; i++) {
        sync_ui_slot(v, &v->ui.bank[i], &p->bank[i], UI_ITEM_CONTAINER_BANK);
        v->ui.bank[i].category = p->bank_tab[i];
    }
}

static void sync_ui_player_status(ViewerState *v) {
    const RcPlayer *p = &v->world->player;
    if (v->bank_outcome_seen != p->bank_result.sequence) {
        v->bank_outcome_seen = p->bank_result.sequence;
        const char *message = rc_bank_result_message(&p->bank_result);
        if (message) runec_ui_add_chat_message(&v->ui, message);
    }
    if (!runec_ui_sync_spellbook(&v->ui, v->world)) {
        fprintf(stderr, "spellbook: invalid book or icon catalog capacity exceeded\n");
        exit(EXIT_FAILURE);
    }
    if (p->combat.failure_reason != v->reported_combat_failure) {
        if (p->combat.failure_reason)
            runec_ui_add_chat_message(&v->ui, p->combat.failure_reason);
        v->reported_combat_failure = p->combat.failure_reason;
    }
    RcCombatViewState combat_view;
    memset(&combat_view, 0, sizeof(combat_view));
    rc_combat_get_player_view(v->world, &combat_view);
    v->combat_view = combat_view;
    v->ui.hitpoints = p->current_hp > 0 ? (p->current_hp + 9) / 10 : 0;
    v->ui.hitpoints_max = p->max_hp > 0 ? (p->max_hp + 9) / 10 : 0;
    v->ui.prayer_points = p->current_prayer_points > 0
                         ? (p->current_prayer_points + 9) / 10 : 0;
    v->ui.prayer_points_max = p->skills.base_level[SKILL_PRAYER];
    if (!runec_prayer_ui_sync(&v->ui.prayers, v->world)) {
        fprintf(stderr, "prayer UI: invalid prayer catalog\n");
        exit(EXIT_FAILURE);
    }
    if (p->prayer_outcome.sequence != v->prayer_outcome_seen) {
        v->prayer_outcome_seen = p->prayer_outcome.sequence;
        const char *message = rc_prayer_result_message(p->prayer_outcome.code);
        if (message[0]) runec_ui_add_chat_message(&v->ui, message);
    }
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

static void viewer_sync_interaction_outcome(ViewerState *v) {
    if (!v || !v->world)
        return;
    const RcInteractionOutcome *outcome =
        rc_interaction_last_outcome(&v->world->player);
    if (!outcome || outcome->sequence == 0
            || outcome->sequence == v->interaction_outcome_seen) {
        return;
    }
    v->interaction_outcome_seen = outcome->sequence;
    if (outcome->message[0])
        runec_ui_add_chat_message(&v->ui, outcome->message);
}

static void viewer_sync_traversal_outcome(ViewerState *v) {
    if (!v || !v->world) return;
    const RcTraversalOutcome *outcome =
        rc_traversal_last_outcome(&v->world->player);
    if (!outcome || outcome->sequence == 0
            || outcome->sequence == v->traversal_outcome_seen) {
        return;
    }
    v->traversal_outcome_seen = outcome->sequence;
    if (v->traversal_generation != 0
            && outcome->generation != v->traversal_generation) {
        return;
    }
    if (outcome->message[0])
        runec_ui_add_chat_message(&v->ui, outcome->message);
    if (outcome->code != RC_TRAVERSAL_RESULT_SUCCESS
            && outcome->generation == v->traversal_generation) {
        if (v->mapsquare_load.active
                && (v->mapsquare_load.request_kind
                        == VIEWER_STREAMING_REQUEST_TRANSPORT_PREFETCH
                    || v->mapsquare_load.player_transition.active)) {
            viewer_cancel_mapsquare_loading(v);
        }
        v->traversal_generation = 0;
    }
}

static void set_viewer_demo_stats(RcPlayer *p) {
    for (int i = 0; i < SKILL_COUNT; i++) {
        p->skills.base_level[i] = 99;
        p->skills.boosted_level[i] = 99;
        p->skills.xp[i] = 13034431;
    }
    p->current_hp = 990;
    p->max_hp = 990;
    rc_prayer_recharge(p);
}

static int ground_item_at_tile_plane(const ViewerState *v, int x, int y,
                                     int plane) {
    for (int i = 0; i < v->world->ground_item_count; i++) {
        const RcGroundItem *g = &v->world->ground_items[i];
        if (rc_ground_item_visible(g, RC_GROUND_OWNER_LOCAL_PLAYER)
                && g->x == x && g->y == y && g->plane == plane) {
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

static int npc_default_left_click_option(const RcWorld *world, const RcNpcDef *def) {
    if (!def)
        return -1;
    int attack_opt = -1;
    int first_opt = -1;
    for (int opt = 0; opt < RC_NPC_OPTION_COUNT; opt++) {
        const char *option = rc_npc_def_option(def, opt);
        if (!option || !option[0])
            continue;
        if (first_opt < 0)
            first_opt = opt;
        if (rc_storage_kind_for_npc(world, def, opt) != RC_STORAGE_NONE)
            return opt;
        if (attack_opt < 0 && rc_npc_def_option_is_attack(def, opt))
            attack_opt = opt;
    }
    return attack_opt >= 0 ? attack_opt : first_opt;
}

static float npc_pick_padding(const RcNpcDef *def) {
    (void)def;
    return 0.05f;
}

static float npc_pick_box_height(const RcNpcDef *def, int size) {
    (void)def;
    float height = npc_pick_height(size);
    return height;
}

static RayCollision pick_npc_ray(ViewerState *v, int i, Ray ray) {
    const RcNpc *n = &v->world->npcs[i];
    int plane = viewer_scene_plane(v);
    if (!n->active || n->is_dead || n->plane != plane)
        return (RayCollision){0};
    const RcNpcDef *def = rc_npc_def_for_npc(v->world, n);
    if (!def) return (RayCollision){0};
    int size = def->size > 0 ? def->size : 1;
    float x = v->npc_render[i].initialized ? v->npc_render[i].render_x : (float)n->x;
    float y = v->npc_render[i].initialized ? v->npc_render[i].render_y : (float)n->y;
    float pad = npc_pick_padding(def);
    BoundingBox box = viewer_tile_box(x - pad, y - pad, size + 2 * pad, size + 2 * pad,
        ground_yf_plane(v, plane, x, y), npc_pick_box_height(def, size));
    return GetRayCollisionBox(ray, box);
}

static int pick_npc_at_mouse_score(ViewerState *v, int *out_uid,
                                   float *out_score) {
    int best_uid = -1;
    float best_distance = 1000000000.0f;
    Ray ray = GetScreenToWorldRay(GetMousePosition(), v->camera);
    for (int i = 0; i < v->world->npc_count; i++) {
        const RcNpc *n = &v->world->npcs[i];
        RayCollision hit = pick_npc_ray(v, i, ray);
        if (hit.hit && hit.distance < best_distance) {
            best_distance = hit.distance;
            best_uid = n->uid;
        }
    }
    if (best_uid < 0)
        return 0;
    if (out_uid)
        *out_uid = best_uid;
    if (out_score)
        *out_score = best_distance;
    return 1;
}

static int pick_npc_at_mouse(ViewerState *v) {
    int uid = -1;
    if (!pick_npc_at_mouse_score(v, &uid, NULL))
        return -1;
    return uid;
}

static const RcNpc *viewer_find_npc_const_by_uid(const ViewerState *v,
                                                 int npc_uid) {
    if (!v || !v->world)
        return NULL;
    for (int i = 0; i < v->world->npc_count; i++) {
        const RcNpc *npc = &v->world->npcs[i];
        if (npc->active && npc->uid == npc_uid)
            return npc;
    }
    return NULL;
}

static const char *viewer_npc_name_by_uid(const ViewerState *v, int npc_uid) {
    const RcNpc *npc = viewer_find_npc_const_by_uid(v, npc_uid);
    const RcNpcDef *def = rc_npc_def_for_npc(v ? v->world : NULL, npc);
    if (!def)
        return "NPC";
    return def->name[0] ? def->name : "NPC";
}

static int viewer_npc_default_option_by_uid(const ViewerState *v,
                                            int npc_uid) {
    const RcNpc *npc = viewer_find_npc_const_by_uid(v, npc_uid);
    const RcNpcDef *def = rc_npc_def_for_npc(v ? v->world : NULL, npc);
    if (!def)
        return -1;
    return npc_default_left_click_option(v->world, def);
}

static const char *viewer_npc_option_label_by_uid(const ViewerState *v,
                                                  int npc_uid, int option) {
    const RcNpc *npc = viewer_find_npc_const_by_uid(v, npc_uid);
    const RcNpcDef *def = rc_npc_def_for_npc(v ? v->world : NULL, npc);
    if (!def)
        return "";
    return rc_npc_def_option(def, option);
}

static const char *viewer_ground_item_name(const ViewerState *v, int idx) {
    if (!v || !v->world || idx < 0 || idx >= v->world->ground_item_count)
        return "item";
    const RcGroundItem *g = &v->world->ground_items[idx];
    const RcItemDef *def = rc_item_def_get(g->item_id);
    return def && def->name[0] ? def->name : "item";
}

static void viewer_selected_target_text(const ViewerState *v, const char *name,
                                        char *dst, size_t dst_cap) {
    if (!v || !dst || dst_cap == 0)
        return;
    const RuneCUiSelectedTarget *selected = &v->ui.selected_target;
    snprintf(dst, dst_cap, "%s %.40s -> %.54s",
             selected->verb[0] ? selected->verb : "Use",
             selected->label, name && name[0] ? name : "target");
}

static void viewer_default_npc_text(const ViewerState *v, int npc_uid,
                                    int option, char *dst, size_t dst_cap) {
    const char *label = viewer_npc_option_label_by_uid(v, npc_uid, option);
    const char *name = viewer_npc_name_by_uid(v, npc_uid);
    if (!label || !label[0])
        label = "Walk here";
    snprintf(dst, dst_cap, "%s %.64s", label, name);
}

static void viewer_default_object_text(const ViewerState *v,
                                       const ViewerPickedObject *object,
                                       int option, char *dst, size_t dst_cap) {
    const RcObjectDef *def = rc_object_def_get(object ? object->obj_id : -1);
    const char *name = def && def->name[0] ? def->name : "object";
    const char *label = object_action_label(v, object, def, option);
    if (!label || !label[0])
        label = "Walk here";
    snprintf(dst, dst_cap, "%s %.64s", label, name);
}

static int pick_npc_candidate(ViewerState *v, ViewerHoverTarget *out) {
    int uid = -1;
    float score = 0.0f;
    if (!pick_npc_at_mouse_score(v, &uid, &score))
        return 0;
    out->kind = VIEWER_HOVER_NPC;
    out->npc_uid = uid;
    out->ground_item_idx = -1;
    out->option = viewer_npc_default_option_by_uid(v, uid);
    out->score = score;
    if (v->ui.selected_target.kind != RUNEC_UI_SELECTED_NONE) {
        viewer_selected_target_text(v, viewer_npc_name_by_uid(v, uid),
                                    out->action_text,
                                    sizeof(out->action_text));
    } else {
        viewer_default_npc_text(v, uid, out->option, out->action_text,
                                sizeof(out->action_text));
    }
    return 1;
}

static int object_action_option_available(const ViewerState *v,
                                          const ViewerPickedObject *object,
                                          int opt) {
    if (!v || !v->world || !object || opt < 0 || opt >= RC_OBJECT_ACTIONS)
        return 0;
    return rc_world_object_option_supported(
        v->world, object->obj_id, object->x, object->y, object->plane,
        object->placement_key, opt);
}

static const char *object_action_label(const ViewerState *v,
                                       const ViewerPickedObject *object,
                                       const RcObjectDef *def, int opt) {
    if (def && opt >= 0 && opt < RC_OBJECT_ACTIONS && def->actions[opt][0])
        return def->actions[opt];
    return v && v->world && object ? rc_world_object_option_label(
        v->world, object->obj_id, object->x, object->y, object->plane,
        object->placement_key, opt) : "";
}

static int object_first_action_option(const ViewerState *v,
                                      ViewerPickedObject object) {
    const RcObjectDef *def = rc_object_def_get(object.obj_id);
    if (!def)
        return -1;
    for (int i = 0; i < RC_OBJECT_ACTIONS; i++) {
        if (object_action_option_available(v, &object, i)
                && object_action_label(v, &object, def, i)[0])
            return i;
    }
    return -1;
}

static int object_first_pick_option(const ViewerState *v,
                                    ViewerPickedObject object) {
    const RcObjectDef *def = rc_object_def_get(object.obj_id);
    if (!def)
        return -1;
    for (int i = 0; i < RC_OBJECT_ACTIONS; i++) {
        if (object_action_option_available(v, &object, i)) {
            return i;
        }
    }
    return -1;
}

static void viewer_start_player_action_anim(ViewerState *v, int anim_id,
                                            int ticks) {
    if (!v || anim_id < 0 || ticks <= 0)
        return;
    v->player_action_anim_id = anim_id;
    v->player_action_anim_timer = ticks;
    v->player_one_shot_finished = 0;
}

static void viewer_sync_consumption(ViewerState *v) {
    const RcPlayer *p = &v->world->player;
    if (v->consume_outcome_seen != p->consume_outcome_sequence) {
        v->consume_outcome_seen = p->consume_outcome_sequence;
        if (p->consume_result != RC_CONSUME_OK)
            runec_ui_add_chat_message(&v->ui, rc_consume_result_message(p->consume_result));
    }
    if (v->consume_sequence_seen != p->consume_sequence) {
        v->consume_sequence_seen = p->consume_sequence;
        viewer_start_player_action_anim(v, p->consume_kind == RC_CONSUME_DRINK ? 830 : 829, 3);
    }
}

static void viewer_start_object_action_visual(ViewerState *v,
                                              ViewerPickedObject object,
                                              int option) {
    (void)option;
    if (!v)
        return;
    const RcObjectBehavior *behavior = rc_object_behavior_get(object.obj_id);
    if (!behavior || !(behavior->flags & (RC_OBJ_BEHAVIOR_LADDER |
                                          RC_OBJ_BEHAVIOR_STAIR))) {
        return;
    }
    const RuneCObjectActionVisualRecord *visual =
        runec_object_action_visual_find(&v->object_action_visuals,
                                        object.obj_id);
    if (visual && visual->climb_anim >= 0)
        viewer_start_player_action_anim(v, visual->climb_anim, 2);
}

static void viewer_prefetch_traversal_destination(
    ViewerState *v, const RcTraversalEvent *event) {
    if (!v || !v->world || !event || !viewer_mapsquare_cache_allowed())
        return;
    int plane = clamp_plane(event->destination_plane);
    int current_x = v->mapsquare_center_region_x
                  * VIEWER_STREAMING_MAPSQUARE_SIZE;
    int current_y = v->mapsquare_center_region_y
                  * VIEWER_STREAMING_MAPSQUARE_SIZE;
    if (v->mapsquare_streaming_active && viewer_streaming_same_window(
            current_x, current_y, viewer_scene_plane(v),
            event->destination_x, event->destination_y, plane)) {
        return;
    }
    if (viewer_prepare_mapsquare_window(
            v, event->destination_x, event->destination_y, plane) <= 0) {
        return;
    }
    viewer_request_mapsquare_window(
        v, event->destination_x, event->destination_y, plane,
        VIEWER_STREAMING_REQUEST_TRANSPORT_PREFETCH,
        "transport-prefetch");
}

static int viewer_interact_object(ViewerState *v, ViewerPickedObject object,
                                  int option) {
    if (!v || !v->world || option < 0)
        return 0;
    if (!rc_player_interact_object_placement(
            v->world, object.obj_id, object.x, object.y, object.plane,
            object.placement_key, option)) {
        return 0;
    }
    return 1;
}

static void viewer_sync_traversal_event(ViewerState *v) {
    if (!v || !v->world) return;
    const RcTraversalEvent *event =
        rc_traversal_last_event(&v->world->player);
    if (!event || event->sequence == 0
            || event->sequence == v->traversal_event_seen) {
        return;
    }
    v->traversal_event_seen = event->sequence;
    if (v->traversal_generation != 0
            && v->traversal_generation != event->generation
            && v->mapsquare_load.active
            && (v->mapsquare_load.request_kind
                    == VIEWER_STREAMING_REQUEST_TRANSPORT_PREFETCH
                || v->mapsquare_load.player_transition.active)) {
        viewer_cancel_mapsquare_loading(v);
    }
    v->traversal_generation = event->generation;
    if (event->phase == RC_TRAVERSAL_PHASE_APPROACH
            || event->phase == RC_TRAVERSAL_PHASE_TAKEOFF) {
        viewer_prefetch_traversal_destination(v, event);
    }
    if (event->phase == RC_TRAVERSAL_PHASE_TAKEOFF
            && event->presentation == RC_TRAVERSAL_PRESENTATION_CLIMB) {
        ViewerPickedObject object = {
            .obj_id = event->source_id,
            .x = event->source_x,
            .y = event->source_y,
            .plane = event->source_plane,
            .placement_key = event->source_key,
        };
        viewer_start_object_action_visual(v, object, event->source_option);
    }
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
    (void)def;
    int max_dim = w > l ? w : l;
    if (max_dim < 1) max_dim = 1;
    if (placement->type == 22)
        return 0.75f;
    if (placement->type >= 0 && placement->type <= 3)
        return 2.25f;
    if (placement->type >= 4 && placement->type <= 8)
        return 2.75f;
    return 2.4f + 0.35f * (float)(max_dim - 1);
}

static float object_pick_padding(int obj_id, int option,
                                 const RcObjectBehavior *behavior) {
    (void)obj_id;
    (void)option;
    (void)behavior;
    return 0.05f;
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

    RcObjectPlacement pick_row;
    if (v->world) {
        if (!rc_world_object_resolve_placement(v->world, row, &pick_row))
            return 0;
    } else {
        pick_row = *row;
    }
    int obj_id = (int)pick_row.obj_id;
    uint64_t placement_key = pick_row.key;
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
        .placement_key = placement_key,
    };
    int option = object_first_pick_option(v, candidate);
    if (!def || (option < 0 && !def->name[0]))
        return 0;
    const RcObjectBehavior *behavior = rc_object_behavior_get(obj_id);
    float pad = object_pick_padding(obj_id, option, behavior);

    float base_y = ground_yf_plane(v, scene_plane, (float)pick_row.x,
                                   (float)pick_row.y);
    BoundingBox box = viewer_tile_box((float)pick_row.x - pad,
                                      (float)pick_row.y - pad,
                                      (float)w + pad * 2.0f,
                                      (float)l + pad * 2.0f, base_y,
                                      object_pick_height(&pick_row, def, w, l));
    RayCollision hit = GetRayCollisionBox(ray, box);
    if (!contains && !hit.hit)
        return 0;

    float score = hit.hit ? hit.distance : 100000.0f;
    if (contains)
        score -= 0.75f;
    if (option >= 0)
        score -= 0.25f;
    if (behavior && (behavior->flags & RC_OBJ_BEHAVIOR_TRANSPORT))
        score -= 0.25f;
    if (option >= 0 && rc_storage_kind_for_object(v->world, obj_id, option) !=
            RC_STORAGE_NONE) {
        score -= 0.35f;
    }
    *out = candidate;
    *out_score = score;
    return 1;
}

static int pick_object_from_regions(ViewerState *v, ViewerPickedObject *out,
                                    float *out_score,
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
            uint16_t ms;
            if (!rc_mapsquare_key(rx, ry, &ms))
                continue;
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
    if (out_score)
        *out_score = best_score;
    return 1;
}

static int pick_object_near_tile(ViewerState *v, ViewerPickedObject *out,
                                 float *out_score,
                                 int tile_x, int tile_y) {
    RcTileRect bounds;
    if (!rc_tile_rect_around(tile_x, tile_y, OBJECT_PICK_TILE_RADIUS,
                             &bounds)) {
        return 0;
    }
    int min_rx = bounds.min_x / RC_MAPSQUARE_SIZE;
    int min_ry = bounds.min_y / RC_MAPSQUARE_SIZE;
    int max_rx = bounds.max_x / RC_MAPSQUARE_SIZE;
    int max_ry = bounds.max_y / RC_MAPSQUARE_SIZE;
    return pick_object_from_regions(v, out, out_score, 1, tile_x, tile_y,
                                    min_rx, min_ry, max_rx, max_ry,
                                    bounds.min_x, bounds.min_y,
                                    bounds.max_x, bounds.max_y);
}

static int pick_object_at_mouse_score(ViewerState *v, ViewerPickedObject *out,
                                      float *out_score) {
    if (!v || !out)
        return 0;
    int tile_x = -1, tile_y = -1;
    int has_tile = raycast_tile(v, &tile_x, &tile_y);
    if (has_tile)
        return pick_object_near_tile(v, out, out_score, tile_x, tile_y);
    RcTileRect bounds;
    if (!rc_tile_rect_from_origin_size(g_world_origin_x, g_world_origin_y,
                                       g_world_w, g_world_h, &bounds)) {
        return 0;
    }
    int min_rx = bounds.min_x / RC_MAPSQUARE_SIZE;
    int min_ry = bounds.min_y / RC_MAPSQUARE_SIZE;
    int max_rx = bounds.max_x / RC_MAPSQUARE_SIZE;
    int max_ry = bounds.max_y / RC_MAPSQUARE_SIZE;
    return pick_object_from_regions(v, out, out_score, has_tile, tile_x, tile_y,
                                    min_rx, min_ry, max_rx, max_ry,
                                    bounds.min_x, bounds.min_y,
                                    bounds.max_x, bounds.max_y);
}

static int pick_object_at_mouse(ViewerState *v, ViewerPickedObject *out) {
    return pick_object_at_mouse_score(v, out, NULL);
}

static int pick_object_candidate(ViewerState *v, ViewerHoverTarget *out) {
    ViewerPickedObject object;
    float score = 0.0f;
    if (!pick_object_at_mouse_score(v, &object, &score))
        return 0;
    out->kind = VIEWER_HOVER_OBJECT;
    out->object = object;
    out->npc_uid = -1;
    out->ground_item_idx = -1;
    out->option = object_first_action_option(v, object);
    out->score = score;
    const RcObjectDef *def = rc_object_def_get(object.obj_id);
    const char *name = def && def->name[0] ? def->name : "object";
    if (v->ui.selected_target.kind != RUNEC_UI_SELECTED_NONE) {
        viewer_selected_target_text(v, name, out->action_text,
                                    sizeof(out->action_text));
    } else {
        viewer_default_object_text(v, &object, out->option, out->action_text,
                                   sizeof(out->action_text));
    }
    return 1;
}

static int resolve_scene_hover_target(ViewerState *v, ViewerHoverTarget *out) {
    if (!v || !out)
        return 0;
    memset(out, 0, sizeof(*out));
    out->kind = VIEWER_HOVER_NONE;
    out->npc_uid = -1;
    out->ground_item_idx = -1;
    out->option = -1;
    out->score = 1000000000.0f;

    ViewerHoverTarget npc = {0};
    ViewerHoverTarget object = {0};
    int have_npc = pick_npc_candidate(v, &npc);
    int have_object = pick_object_candidate(v, &object);
    if (have_npc && (!have_object || npc.score <= object.score)) {
        *out = npc;
        return 1;
    }
    if (have_object) {
        *out = object;
        return 1;
    }

    int tx = -1, ty = -1;
    if (!raycast_tile(v, &tx, &ty))
        return 0;
    int ground_idx = ground_item_at_tile(v, tx, ty);
    if (ground_idx >= 0) {
        out->kind = VIEWER_HOVER_GROUND_ITEM;
        out->ground_item_idx = ground_idx;
        out->tile_x = tx;
        out->tile_y = ty;
        if (v->ui.selected_target.kind != RUNEC_UI_SELECTED_NONE) {
            viewer_selected_target_text(v, viewer_ground_item_name(v, ground_idx),
                                        out->action_text,
                                        sizeof(out->action_text));
        } else if (v->world->player.x == tx && v->world->player.y == ty) {
            snprintf(out->action_text, sizeof(out->action_text), "Take %.64s",
                     viewer_ground_item_name(v, ground_idx));
        } else {
            snprintf(out->action_text, sizeof(out->action_text), "Walk here");
        }
        return 1;
    }
    out->kind = VIEWER_HOVER_TILE;
    out->tile_x = tx;
    out->tile_y = ty;
    snprintf(out->action_text, sizeof(out->action_text), "Walk here");
    return 1;
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

static int viewer_find_npc_index_by_uid(ViewerState *v, int npc_uid) {
    if (!v || !v->world)
        return -1;
    for (int i = 0; i < v->world->npc_count; i++) {
        RcNpc *npc = &v->world->npcs[i];
        if (npc->active && npc->uid == npc_uid)
            return i;
    }
    return -1;
}

static int viewer_left_click_npc(ViewerState *v, int npc_uid) {
    RcNpc *npc = viewer_find_npc_by_uid(v, npc_uid);
    const RcNpcDef *def = rc_npc_def_for_npc(v ? v->world : NULL, npc);
    if (!def)
        return 0;
    int opt = npc_default_left_click_option(v->world, def);
    if (opt < 0)
        return 0;
    if (rc_npc_def_option_is_attack(def, opt))
        return rc_player_attack_npc(v->world, npc_uid);
    else {
        rc_player_interact_npc(v->world, npc_uid, opt);
        return 1;
    }
}

static void reset_viewer_context(ViewerState *v) {
    memset(v->context_entries, 0, sizeof(v->context_entries));
    v->context_object = (ViewerPickedObject){.obj_id = -1};
    v->context_page = 0;
    v->context_plane = -1;
    v->context_tile_x = v->context_tile_y = -1;
}

static void scene_context_row(ViewerState *v, int row, ViewerContextEntry entry,
                               const char *action, const char *target, Color color) {
    v->context_entries[row] = entry;
    snprintf(v->ui.context_actions[row], sizeof(v->ui.context_actions[row]), "%s", action);
    snprintf(v->ui.context_targets[row], sizeof(v->ui.context_targets[row]), "%s", target);
    v->ui.context_target_colors[row] = color;
    v->ui.context_action_op[row] = row;
    v->ui.context_action_count = row + 1;
}

static void scene_context_candidate(ViewerState *v, int *seen, ViewerContextEntry entry,
                                     const char *action, const char *target, Color color) {
    int row = (*seen)++ - v->context_page * VIEWER_CONTEXT_PAGE_ROWS;
    if (row >= 0 && row < VIEWER_CONTEXT_PAGE_ROWS)
        scene_context_row(v, row, entry, action, target, color);
}

static void build_scene_context_page(ViewerState *v, int page) {
    if (!v || !v->world || page < 0 || v->context_plane != viewer_scene_plane(v))
        return;
    v->context_page = page;
    Vector2 position = v->ui.context_pos;
    const char *cancel[] = {"Cancel"};
    runec_ui_open_context_targeted(&v->ui, position, "", WHITE, cancel, 1);
    v->ui.context_action_count = 0;
    memset(v->context_entries, 0, sizeof(v->context_entries));
    int seen = 0;
    // A right-click lists overlapping targets; the left-click winner must not hide loot.
    for (int i = 0; i < v->world->npc_count; i++) {
        if (!pick_npc_ray(v, i, v->context_ray).hit) continue;
        const RcNpc *npc = &v->world->npcs[i];
        const RcNpcDef *def = rc_npc_def_for_npc(v->world, npc);
        ViewerContextEntry entry = {.kind = VIEWER_CONTEXT_NPC, .npc_uid = npc->uid};
        for (int opt = 0; opt < RC_NPC_OPTION_COUNT; opt++) {
            const char *label = rc_npc_def_option(def, opt);
            if (!label || !label[0]) continue;
            entry.option = opt;
            scene_context_candidate(v, &seen, entry, label, def->name, YELLOW);
        }
        entry.option = VIEWER_CONTEXT_EXAMINE;
        scene_context_candidate(v, &seen, entry, "Examine", def->name, YELLOW);
    }
    const ViewerPickedObject *object = &v->context_object;
    const RcObjectDef *def = object->obj_id >= 0 ? rc_object_def_get(object->obj_id) : NULL;
    if (def) {
        ViewerContextEntry entry = {.kind = VIEWER_CONTEXT_OBJECT};
        for (int opt = 0; opt < RC_OBJECT_ACTIONS; opt++) {
            const char *label = object_action_label(v, object, def, opt);
            if (!object_action_option_available(v, object, opt) || !label[0]) continue;
            entry.option = opt;
            scene_context_candidate(v, &seen, entry, label, def->name, (Color){0,255,255,255});
        }
        entry.option = VIEWER_CONTEXT_EXAMINE;
        scene_context_candidate(v, &seen, entry, "Examine", def->name, (Color){0,255,255,255});
    }
    for (int i = 0; i < v->world->ground_item_count; i++) {
        const RcGroundItem *g = &v->world->ground_items[i];
        if (!rc_ground_item_visible(g, RC_GROUND_OWNER_LOCAL_PLAYER)
                || g->x != v->context_tile_x || g->y != v->context_tile_y
                || g->plane != v->context_plane) continue;
        ViewerContextEntry entry = {.kind = VIEWER_CONTEXT_GROUND_ITEM,
            .ground_index = i, .ground_uid = g->uid, .ground_version = g->version,
            .option = VIEWER_CONTEXT_TAKE};
        scene_context_candidate(v, &seen, entry, "Take",
            viewer_ground_item_name(v, i), (Color){255,144,64,255});
        entry.option = VIEWER_CONTEXT_EXAMINE;
        scene_context_candidate(v, &seen, entry, "Examine",
            viewer_ground_item_name(v, i), (Color){255,144,64,255});
    }
    ViewerContextEntry entry = {.kind = VIEWER_CONTEXT_TILE};
    if (page > 0) {
        entry.option = VIEWER_CONTEXT_PREVIOUS;
        scene_context_row(v, v->ui.context_action_count, entry, "Previous options", "", WHITE);
    }
    if (seen > (page + 1) * VIEWER_CONTEXT_PAGE_ROWS) {
        entry.option = VIEWER_CONTEXT_MORE;
        scene_context_row(v, v->ui.context_action_count, entry, "More options", "", WHITE);
    }
    if (v->context_tile_x >= 0 && v->context_tile_y >= 0) {
        entry.option = VIEWER_CONTEXT_WALK_HERE;
        scene_context_row(v, v->ui.context_action_count, entry, "Walk here", "", WHITE);
    }
    entry.option = VIEWER_CONTEXT_CANCEL;
    scene_context_row(v, v->ui.context_action_count, entry, "Cancel", "", WHITE);
}

static void open_scene_context_menu(ViewerState *v) {
    reset_viewer_context(v);
    v->context_plane = viewer_scene_plane(v);
    v->ui.context_pos = GetMousePosition();
    v->context_ray = GetScreenToWorldRay(v->ui.context_pos, v->camera);
    raycast_tile(v, &v->context_tile_x, &v->context_tile_y);
    pick_object_at_mouse(v, &v->context_object);
    build_scene_context_page(v, 0);
}

static void handle_context_intent(ViewerState *v) {
    if (v->ui.last_intent.kind != RUNEC_UI_INTENT_CONTEXT_ACTION) return;
    int row = v->ui.last_intent.primary;
    if (row < 0 || row >= v->ui.context_action_count || row >= RUNEC_UI_CONTEXT_ACTIONS
            || v->context_plane != viewer_scene_plane(v)) {
        reset_viewer_context(v);
        return;
    }
    ViewerContextEntry entry = v->context_entries[row];
    int option = entry.option;
    if (entry.kind == VIEWER_CONTEXT_NPC) {
        RcNpc *npc = viewer_find_npc_by_uid(v, entry.npc_uid);
        if (npc && option >= 0)
            rc_player_interact_npc(v->world, entry.npc_uid, option);
        else if (npc && option == VIEWER_CONTEXT_EXAMINE)
            rc_player_examine_npc(v->world, entry.npc_uid);
    } else if (entry.kind == VIEWER_CONTEXT_OBJECT) {
        ViewerPickedObject object = v->context_object;
        if (option >= 0)
            viewer_interact_object(v, object, option);
        else if (option == VIEWER_CONTEXT_EXAMINE)
            rc_player_examine_object_placement(v->world, object.obj_id,
                object.x, object.y, object.plane, object.placement_key);
    } else if (entry.kind == VIEWER_CONTEXT_GROUND_ITEM) {
        if (option == VIEWER_CONTEXT_TAKE) {
            rc_player_pickup_item_expected(v->world, entry.ground_index,
                entry.ground_uid, entry.ground_version);
        } else if (option == VIEWER_CONTEXT_EXAMINE) {
            const RcGroundItem *g = entry.ground_index >= 0
                && entry.ground_index < v->world->ground_item_count
                ? &v->world->ground_items[entry.ground_index] : NULL;
            if (g && g->uid == entry.ground_uid && g->version == entry.ground_version
                    && g->plane == v->context_plane
                    && rc_ground_item_visible(g, RC_GROUND_OWNER_LOCAL_PLAYER))
                rc_player_examine_ground_item(v->world, entry.ground_index);
        }
    } else if (entry.kind == VIEWER_CONTEXT_TILE) {
        if (option == VIEWER_CONTEXT_MORE || option == VIEWER_CONTEXT_PREVIOUS) {
            build_scene_context_page(v, v->context_page + (option == VIEWER_CONTEXT_MORE ? 1 : -1));
            return;
        }
        if (option == VIEWER_CONTEXT_WALK_HERE)
            route_player_to(v, v->context_tile_x, v->context_tile_y);
    }
    reset_viewer_context(v);
}

static void draw_hover_action_label(ViewerState *v, int ui_capture) {
    if (!v || ui_capture || v->ui.context_open)
        return;
    ViewerHoverTarget hover;
    if (!resolve_scene_hover_target(v, &hover) || !hover.action_text[0])
        return;
    Color shadow = (Color){0, 0, 0, 220};
    Color text = (Color){255, 255, 0, 255};
    DrawText(hover.action_text, 5, 6, 16, shadow);
    DrawText(hover.action_text, 4, 5, 16, text);
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

static void begin_one_sided_model_draw(void) {
    rlSetCullFace(RL_CULL_FACE_BACK);
    rlEnableBackfaceCulling();
}

static void end_one_sided_model_draw(void) {
    rlDisableBackfaceCulling();
}

static void draw_model_entry_one_sided(ModelEntry *entry, Vector3 pos,
                                       float facing_angle, float scale,
                                       Color tint, Shader shader,
                                       int use_shader) {
    if (!entry || !entry->loaded)
        return;
    Shader previous_shader = {0};
    int restore_shader = 0;
    if (use_shader && shader.id > 0 && entry->model.materialCount > 0) {
        previous_shader = entry->model.materials[0].shader;
        for (int i = 0; i < entry->model.materialCount; i++)
            entry->model.materials[i].shader = shader;
        restore_shader = 1;
    }
    begin_one_sided_model_draw();
    DrawModelEx(entry->model, pos, (Vector3){0, 1, 0}, facing_angle,
                (Vector3){scale, scale, scale}, tint);
    end_one_sided_model_draw();
    if (restore_shader) {
        for (int i = 0; i < entry->model.materialCount; i++)
            entry->model.materials[i].shader = previous_shader;
    }
}

static int draw_item_model(ViewerState *v, uint32_t model_id, Vector3 pos,
                           float facing_angle, float scale, Color tint) {
    if (!v->item_models || !v->item_models->loaded
            || model_id == RUNEC_RENDER_MODEL_MISSING)
        return 0;
    ModelEntry *entry = model_find(v->item_models, model_id);
    if (!entry || !entry->loaded)
        return 0;
    draw_model_entry_one_sided(
        entry, pos, facing_angle, scale, tint,
        v->alpha_cutout_shader_dynamic, 0);
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

static void build_ui_icon_for_item(ViewerState *v, int item_id, int quantity) {
    int icon_item_id = item_display_id_for_quantity(v, item_id, quantity);
    if (icon_item_id <= 0 || ui_item_icon_cached(&v->ui, (uint32_t)icon_item_id))
        return;
    Texture2D icon = load_item_icon_texture_from_file(icon_item_id);
    if (!icon.id)
        fprintf(stderr, "ui_icons: missing item_%d.png; rebuild item sprites\n", icon_item_id);
    // Cache failed loads too: report once, not file I/O on every frame.
    runec_ui_set_item_icon(&v->ui, (uint32_t)icon_item_id, icon);
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
    if (p->storage_kind == RC_STORAGE_BANK ||
            p->storage_kind == RC_STORAGE_DEPOSIT_BOX) {
        for (int i = 0; i < RUNEC_UI_BANK_SLOT_COUNT; i++) {
            if (p->bank[i].item_id >= 0 && p->bank[i].quantity > 0
                    && p->bank_tab[i] == v->ui.bank_active_tab) {
                build_ui_icon_for_item(v, p->bank[i].item_id,
                                       p->bank[i].quantity);
            }
        }
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
        (*states_out)[i] = anim_model_state_create_with_faces(
            entry->vertex_skins, entry->base_vert_count,
            entry->face_skins, entry->face_count, entry->face_alphas);
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

static void create_object_anim_states(ObjectMesh *objects, ModelSet *models,
                                      AnimModelState ***states,
                                      int *state_count) {
    if (!states || !state_count)
        return;
    free_model_anim_states(states, state_count);
    if (!objects || !models || !models->loaded
            || objects->object_anim_count <= 0)
        return;
    *state_count = objects->object_anim_count;
    *states = calloc((size_t)*state_count, sizeof(AnimModelState *));
    if (!*states) {
        *state_count = 0;
        return;
    }
    for (int i = 0; i < objects->object_anim_count; i++) {
        ModelEntry *entry = model_find(models, objects->object_anims[i].model_id);
        if (!entry || !entry->loaded || !entry->vertex_skins
                || entry->base_vert_count <= 0)
            continue;
        (*states)[i] = anim_model_state_create_with_faces(
            entry->vertex_skins, entry->base_vert_count,
            entry->face_skins, entry->face_count, entry->face_alphas);
    }
}

static void create_object_anim_plane_states(ViewerState *v, int plane) {
    if (!v || plane < 0 || plane >= RC_MAX_PLANES) return;
    create_object_anim_states(v->object_planes[plane],
                              v->object_anim_model_planes[plane],
                              &v->object_anim_states[plane],
                              &v->object_anim_state_count[plane]);
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

static void update_model_entry_colors_from_anim(ModelEntry *entry,
                                                AnimModelState *state) {
    if (!entry || !entry->loaded || !state || !entry->rest_colors)
        return;
    Mesh *mesh = &entry->model.meshes[0];
    if (!mesh->colors || mesh->vertexCount <= 0)
        return;
    anim_update_mesh_colors(mesh->colors, entry->rest_colors, state,
                            entry->face_count, mesh->vertexCount);
    UpdateMeshBuffer(*mesh, 3, mesh->colors, mesh->vertexCount * 4, 0);
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
    update_model_entry_colors_from_anim(entry, state);
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
    update_model_entry_colors_from_anim(entry, state);
    models_recompute_texture_uvs_from_vertices(entry, state->verts);
    return 1;
}

static int animated_object_should_draw(const ViewerState *v, int scene_plane,
                                       const ObjectAnimRow *row) {
    if (!v || !row)
        return 0;
    if (v->mapsquare_streaming_active
            && !viewer_actor_in_draw_range(
                v, (float)row->world_x, (float)row->world_y, 8.0f))
        return 0;
    scene_plane = clamp_plane(scene_plane);
    if (v->object_chunk_count[scene_plane] <= 0
            || v->object_chunk_draw_radius <= 0.0f)
        return 1;

    float lx = row->pos_x - (float)g_world_origin_x;
    float lz = row->pos_z + (float)g_world_origin_y;
    float dx = lx - v->camera.target.x;
    float dz = lz - v->camera.target.z;
    float r = v->object_chunk_draw_radius
            + (float)(v->object_chunk_size > 0 ? v->object_chunk_size : 64);
    return dx * dx + dz * dz <= r * r;
}

static void draw_animated_object_resources(ViewerState *v, int scene_plane,
                                           ObjectMesh *objects,
                                           ModelSet *models,
                                           AnimModelState **states,
                                           int state_count) {
    if (!v || !objects || objects->object_anim_count <= 0)
        return;
    scene_plane = clamp_plane(scene_plane);
    if (!models || !models->loaded || !states)
        return;

    float client_ticks = ((float)(v->world ? v->world->tick : 0)
                       + v->tick_frac) * 30.0f;
    for (int i = 0; i < objects->object_anim_count; i++) {
        ObjectAnimRow *row = &objects->object_anims[i];
        if (!animated_object_should_draw(v, scene_plane, row))
            continue;
        if (row->pad & (OANM_FLAG_DYNAMIC_BASE | OANM_FLAG_DYNAMIC_REPLACEMENT)) {
            RcObjectState active;
            int have_state = v->world && rc_world_object_active_state_layer(
                v->world, (int)row->obj_id, row->world_x, row->world_y,
                row->plane, rc_object_layer_for_type(row->obj_type), &active);
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
        AnimCache *cache = v->object_anim_cache ? v->object_anim_cache
                                                : v->anims;
        if (states[i]) {
            if (!animate_model_entry_sequence(entry, states[i], cache,
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

static void draw_animated_objects(ViewerState *v, int scene_plane,
                                  ObjectMesh *objects) {
    scene_plane = clamp_plane(scene_plane);
    draw_animated_object_resources(
        v, scene_plane, objects, v->object_anim_model_planes[scene_plane],
        v->object_anim_states[scene_plane],
        v->object_anim_state_count[scene_plane]);
}

static void load_spawn_objects(ViewerState *v) {
    const char *models_path = env_path("RUNEC_SPAWN_OBJECT_MODELS",
        "data/models/spawn_objects.models");
    const char *templates_path = env_path("RUNEC_SPAWN_OBJECT_TEMPLATES",
        "data/models/spawn_objects.oanim");
    ObjectMesh *templates = calloc(1, sizeof(*templates));
    if (!templates || !objects_load_object_anims_path(templates, templates_path)) {
        fprintf(stderr, "spawn objects: no templates at %s; spawned locs are "
                "not drawn\n", templates_path);
        objects_free(templates);
        return;
    }
    v->spawn_object_templates = templates;
    v->spawn_object_models = models_load(models_path);
    if (v->spawn_object_models && v->alpha_cutout_shader_static_loaded)
        models_set_shader(v->spawn_object_models, v->alpha_cutout_shader_static);
    create_object_anim_states(templates, v->spawn_object_models,
                              &v->spawn_object_states,
                              &v->spawn_object_state_count);
}

static void free_spawn_objects(ViewerState *v) {
    free_model_anim_states(&v->spawn_object_states,
                           &v->spawn_object_state_count);
    models_free(v->spawn_object_models);
    objects_free(v->spawn_object_templates);
    v->spawn_object_models = NULL;
    v->spawn_object_templates = NULL;
}

static int spawn_object_template(const ViewerState *v, const RcObjectState *s) {
    const ObjectMesh *t = v->spawn_object_templates;
    for (int i = 0; t && i < t->object_anim_count; i++) {
        const ObjectAnimRow *row = &t->object_anims[i];
        if ((int)row->obj_id == s->active_obj_id
                && row->obj_type == s->active_type
                && row->rotation == (s->active_rotation & 3))
            return i;
    }
    return -1;
}

static void report_missing_spawn_object(ViewerState *v, int obj_id) {
    for (int i = 0; i < v->missing_spawn_object_count; i++) {
        if (v->missing_spawn_objects[i] == obj_id) return;
    }
    if (v->missing_spawn_object_count < (int)(sizeof(v->missing_spawn_objects)
                                              / sizeof(v->missing_spawn_objects[0])))
        v->missing_spawn_objects[v->missing_spawn_object_count++] = obj_id;
    fprintf(stderr, "spawn objects: no template for loc %d "
            "(tools/cache_pipeline/export_spawn_objects.py)\n", obj_id);
}

// Locs the core spawned at runtime have no map-square model; each is drawn
// from its template at the state's tile, standing on the ground there.
static void draw_spawned_objects(ViewerState *v, int scene_plane) {
    const RcWorld *world = v ? v->world : NULL;
    if (!world || !world->object_state_count) return;
    AnimCache *cache = v->object_anim_cache ? v->object_anim_cache : v->anims;
    float client_ticks = ((float)world->tick + v->tick_frac) * 30.0f;
    for (int i = 0; i < world->object_state_count; i++) {
        const RcObjectState *s = &world->object_states[i];
        if (!(s->flags & RC_OBJECT_STATE_SPAWNED) || s->active_obj_id < 0
                || s->active_plane != scene_plane
                || !viewer_actor_in_draw_range(v, (float)s->active_x,
                                               (float)s->active_y, 2.0f))
            continue;
        int t = spawn_object_template(v, s);
        const ObjectAnimRow *row = t >= 0
            ? &v->spawn_object_templates->object_anims[t] : NULL;
        ModelEntry *entry = row && v->spawn_object_models
            ? model_find(v->spawn_object_models, row->model_id) : NULL;
        if (!entry || !entry->loaded) {
            report_missing_spawn_object(v, s->active_obj_id);
            continue;
        }
        AnimModelState *state = t < v->spawn_object_state_count
                              ? v->spawn_object_states[t] : NULL;
        if (state && row->animation_id >= 0) {
            float phase = (float)((s->active_x * 13 + s->active_y * 17
                                   + s->active_obj_id * 3) % 97);
            if (!animate_model_entry_sequence(entry, state, cache,
                                              row->animation_id,
                                              client_ticks + phase))
                reset_model_entry_to_base_pose(entry);
        }
        float wx = (float)s->active_x + row->pos_x;
        float wy = (float)s->active_y - row->pos_z;
        Vector3 pos = {
            wx - (float)g_world_origin_x,
            ground_yf_plane(v, scene_plane, wx, wy) + row->pos_y,
            row->pos_z - (float)(s->active_y - g_world_origin_y),
        };
        DrawModel(entry->model, pos, 1.0f, WHITE);
    }
}

static int mapsquare_chunk_in_active_scene(
    const ViewerMapsquareChunk *chunk) {
    if (!chunk)
        return 0;
    int min_x = chunk->region_x * VIEWER_STREAMING_MAPSQUARE_SIZE;
    int min_y = chunk->region_y * VIEWER_STREAMING_MAPSQUARE_SIZE;
    int max_x = min_x + VIEWER_STREAMING_MAPSQUARE_SIZE;
    int max_y = min_y + VIEWER_STREAMING_MAPSQUARE_SIZE;
    return max_x > g_world_origin_x && min_x < g_world_origin_x + g_world_w
        && max_y > g_world_origin_y && min_y < g_world_origin_y + g_world_h;
}

static void draw_mapsquare_chunks(ViewerState *v, int plane, float frame_dt) {
    if (!v || !v->mapsquare_streaming_active)
        return;
    objects_update_texture_anims(v->mapsquare_materials, frame_dt);
    plane = clamp_plane(plane);
    for (int i = 0; i < v->mapsquare_chunk_count; i++) {
        ViewerMapsquareChunk *chunk = &v->mapsquare_chunks[i];
        if (chunk->plane != plane || !mapsquare_chunk_in_active_scene(chunk))
            continue;
        if (chunk->terrain && chunk->terrain->loaded) {
            DrawModel(chunk->terrain->model, (Vector3){0, 0, 0}, 1.0f,
                      WHITE);
        }
        if (chunk->objects && chunk->objects->loaded) {
            DrawModel(chunk->objects->model, (Vector3){0, 0, 0}, 1.0f,
                      WHITE);
        }
        draw_animated_object_resources(
            v, plane, chunk->objects, chunk->object_anim_models,
            chunk->object_anim_states, chunk->object_anim_state_count);
    }
}

static int viewer_mapsquare_cache_allowed(void) {
    if (!env_bool("RUNEC_MAPSQUARE_STREAMING", 1)
            || env_has_value("RUNEC_TERRAIN")
            || env_has_value("RUNEC_OBJECTS"))
        return 0;
    const char *mode = getenv("RUNEC_SCENE_MODE");
    return !mode || !mode[0] || strcmp(mode, "auto") == 0;
}

static int viewer_mapsquare_asset_path(const ViewerState *v, char *out,
                                       size_t capacity, int region_x,
                                       int region_y, int plane,
                                       const char *suffix) {
    if (!v)
        return 0;
    return viewer_streaming_mapsquare_path(
        out, capacity, v->mapsquare_directory, region_x, region_y, plane,
        suffix);
}

static int viewer_mapsquare_material_path(const ViewerState *v, char *out,
                                          size_t capacity) {
    if (!v || !out || capacity == 0 || !v->mapsquare_directory[0])
        return 0;
    int n = snprintf(out, capacity, "%s/mapsquare.materials.atlas",
                     v->mapsquare_directory);
    return n > 0 && (size_t)n < capacity;
}

static int viewer_mapsquare_catalog_path(const ViewerState *v, char *out,
                                         size_t capacity) {
    if (!v || !out || capacity == 0 || !v->mapsquare_directory[0])
        return 0;
    int n = snprintf(out, capacity, "%s/mapsquare.catalog",
                     v->mapsquare_directory);
    return n > 0 && (size_t)n < capacity;
}

static int viewer_load_mapsquare_catalog(ViewerState *v) {
    if (!v)
        return 0;
    if (v->mapsquare_catalog.loaded)
        return 1;
    char path[1024];
    if (!viewer_mapsquare_catalog_path(v, path, sizeof(path))
            || !rc_asset_exists(path)
            || !viewer_streaming_catalog_load(&v->mapsquare_catalog, path)) {
        return 0;
    }
    fprintf(stderr, "viewer mapsquare: catalog loaded %u regions from %s\n",
            v->mapsquare_catalog.count, path);
    return 1;
}

static int viewer_mapsquare_region_present(const ViewerState *v,
                                           int region_x, int region_y) {
    return !v || !v->mapsquare_catalog.loaded
        || viewer_streaming_catalog_contains(
            &v->mapsquare_catalog, region_x, region_y);
}

static int viewer_filter_mapsquare_plan(const ViewerState *v,
                                        ViewerMapsquareCoord *plan,
                                        int count) {
    return viewer_streaming_filter_mapsquares(
        v ? &v->mapsquare_catalog : NULL, plan, count);
}

static int viewer_mapsquare_materials_available(const ViewerState *v) {
    char atlas_path[1024];
    char tanim_path[1024];
    if (!viewer_mapsquare_material_path(v, atlas_path, sizeof(atlas_path)))
        return 0;
    int n = snprintf(tanim_path, sizeof(tanim_path),
                     "%s/mapsquare.materials.tanim",
                     v->mapsquare_directory);
    return n > 0 && (size_t)n < sizeof(tanim_path)
        && rc_asset_exists(atlas_path) && rc_asset_exists(tanim_path);
}

static int viewer_mapsquare_chunk_available(const ViewerState *v,
                                            int region_x, int region_y,
                                            int plane) {
    char missing_path[1024];
    return !viewer_mapsquare_missing_asset_path(
        v, region_x, region_y, plane, missing_path, sizeof(missing_path));
}

static int viewer_mapsquare_missing_asset_path(
    const ViewerState *v, int region_x, int region_y, int plane,
    char *out, size_t capacity) {
    static const char *const suffixes[] = {
        ".terrain", ".objects", ".oanim",
    };
    if (!v || !out || capacity == 0)
        return 1;
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
        if (!viewer_mapsquare_asset_path(
                v, out, capacity, region_x, region_y, plane, suffixes[i])
                || !rc_asset_exists(out)) {
            return 1;
        }
    }

    if (!v->scene_auto_export)
        return 0;
    char objects_path[1024];
    if (!viewer_mapsquare_asset_path(
            v, objects_path, sizeof(objects_path), region_x, region_y, plane,
            ".objects")
            || !scene_objects_file_complete(objects_path)) {
        snprintf(out, capacity, "%s", objects_path);
        return 1;
    }
    int has_animated_objects = 0;
    if (!scene_oanim_file_complete(out, &has_animated_objects))
        return 1;
    if (!has_animated_objects)
        return 0;
    if (!viewer_mapsquare_asset_path(
            v, out, capacity, region_x, region_y, plane,
            ".object_anim.models")
            || !rc_asset_exists(out)) {
        return 1;
    }
    return 0;
}

static int viewer_mapsquare_visible_plan(
    const ViewerState *v, int center_x, int center_y,
    ViewerMapsquareCoord *plan, int capacity) {
    if (!v)
        return -1;
    ViewerStreamingConfig visible = v->streaming;
    visible.preload_radius_regions = 0;
    int count = viewer_streaming_plan_mapsquares(
        &visible, center_x, center_y, plan, capacity);
    return count < 0 ? count : viewer_filter_mapsquare_plan(v, plan, count);
}

static void viewer_report_missing_mapsquare_assets(const ViewerState *v,
                                                    int center_x,
                                                    int center_y, int plane,
                                                    const char *operation) {
    const char *label = operation && operation[0] ? operation : "transition";
    char path[1024];
    fprintf(stderr,
            "viewer mapsquare: %s requires a complete prebuilt visual "
            "window at %d,%d plane %d\n",
            label, center_x, center_y, clamp_plane(plane));
    if (!v || !v->mapsquare_catalog.loaded) {
        if (!viewer_mapsquare_catalog_path(v, path, sizeof(path)))
            snprintf(path, sizeof(path), "data/regions/mapsquare.catalog");
        fprintf(stderr, "viewer mapsquare: missing catalog: %s\n", path);
    } else {
        int region_x = center_x / VIEWER_STREAMING_MAPSQUARE_SIZE;
        int region_y = center_y / VIEWER_STREAMING_MAPSQUARE_SIZE;
        if (!viewer_mapsquare_region_present(v, region_x, region_y)) {
            fprintf(stderr,
                    "viewer mapsquare: center mapsquare %d,%d is not present "
                    "in the installed b237 catalog\n",
                    region_x, region_y);
        }
    }
    if (v && !viewer_mapsquare_materials_available(v)) {
        if (!viewer_mapsquare_material_path(v, path, sizeof(path)))
            snprintf(path, sizeof(path),
                     "data/regions/mapsquare.materials.atlas");
        fprintf(stderr,
                "viewer mapsquare: missing shared material assets under %s\n",
                path);
    }
    if (v && v->mapsquare_catalog.loaded) {
        ViewerMapsquareCoord plan[VIEWER_STREAMING_CHUNK_CAPACITY];
        int count = viewer_mapsquare_visible_plan(
            v, center_x, center_y, plan, VIEWER_STREAMING_CHUNK_CAPACITY);
        for (int i = 0; i < count; i++) {
            if (viewer_mapsquare_missing_asset_path(
                    v, plan[i].region_x, plan[i].region_y,
                    clamp_plane(plane), path, sizeof(path))) {
                fprintf(stderr,
                        "viewer mapsquare: first missing or incomplete asset: "
                        "%s\n",
                        path);
                break;
            }
        }
    }
    fprintf(stderr,
            "viewer mapsquare: run ./scripts/setup-data.sh to install the "
            "complete runtime-data release; normal gameplay does not generate "
            "assets\n");
    fprintf(stderr,
            "viewer mapsquare: maintainers may opt in with "
            "RUNEC_SCENE_AUTO_EXPORT=1 and a local b237 cache\n");
}

static int viewer_mapsquare_window_assets_available(const ViewerState *v,
                                                     int center_x,
                                                     int center_y, int plane) {
    if (!v || !viewer_mapsquare_materials_available(v))
        return 0;
    int center_region_x = center_x / VIEWER_STREAMING_MAPSQUARE_SIZE;
    int center_region_y = center_y / VIEWER_STREAMING_MAPSQUARE_SIZE;
    if (!viewer_mapsquare_region_present(
            v, center_region_x, center_region_y)) {
        return 0;
    }
    ViewerMapsquareCoord plan[VIEWER_STREAMING_CHUNK_CAPACITY];
    int count = viewer_mapsquare_visible_plan(
        v, center_x, center_y, plan, VIEWER_STREAMING_CHUNK_CAPACITY);
    if (count <= 0)
        return 0;
    plane = clamp_plane(plane);
    for (int i = 0; i < count; i++) {
        if (!viewer_mapsquare_chunk_available(
                v, plan[i].region_x, plan[i].region_y, plane)) {
            return 0;
        }
    }
    return 1;
}

static int viewer_prepare_mapsquare_window(ViewerState *v, int center_x,
                                           int center_y, int plane) {
    if (!v || !viewer_mapsquare_cache_allowed())
        return 0;
    int catalog_loaded = viewer_load_mapsquare_catalog(v);
    plane = clamp_plane(plane);
    if (!catalog_loaded && !v->scene_auto_export) {
        viewer_report_missing_mapsquare_assets(
            v, center_x, center_y, plane, "transition");
        return -1;
    }
    int center_region_x = center_x / VIEWER_STREAMING_MAPSQUARE_SIZE;
    int center_region_y = center_y / VIEWER_STREAMING_MAPSQUARE_SIZE;
    if (!viewer_mapsquare_region_present(
            v, center_region_x, center_region_y)) {
        viewer_report_missing_mapsquare_assets(
            v, center_x, center_y, plane, "transition");
        return -1;
    }
    if (viewer_mapsquare_window_assets_available(
            v, center_x, center_y, plane)) {
        return 1;
    }
    if (!v->scene_auto_export) {
        viewer_report_missing_mapsquare_assets(
            v, center_x, center_y, plane, "transition");
        return -1;
    }

    const char *cache = viewer_scene_export_cache_path();
    if (!cache || !cache[0] || !local_dir_exists(cache)) {
        fprintf(stderr,
                "viewer mapsquare: development auto-export requires "
                "RUNEC_CACHE, RUNEC_B237_CACHE, or %s\n",
                DEFAULT_B237_CACHE_PATH);
        return -1;
    }

    ViewerMapsquareCoord plan[VIEWER_STREAMING_CHUNK_CAPACITY];
    int count = viewer_mapsquare_visible_plan(
        v, center_x, center_y, plan, VIEWER_STREAMING_CHUNK_CAPACITY);
    if (count <= 0)
        return -1;

    char regions[VIEWER_STREAMING_CHUNK_CAPACITY * 9] = {0};
    size_t used = 0;
    int missing_count = 0;
    for (int i = 0; i < count; i++) {
        if (viewer_mapsquare_chunk_available(
                v, plan[i].region_x, plan[i].region_y, plane)) {
            continue;
        }
        int n = snprintf(regions + used, sizeof(regions) - used,
                         "%s%d,%d", missing_count ? " " : "",
                         plan[i].region_x, plan[i].region_y);
        if (n <= 0 || (size_t)n >= sizeof(regions) - used)
            return -1;
        used += (size_t)n;
        missing_count++;
    }
    if (missing_count == 0) {
        int region_x = center_x / VIEWER_STREAMING_MAPSQUARE_SIZE;
        int region_y = center_y / VIEWER_STREAMING_MAPSQUARE_SIZE;
        int n = snprintf(regions, sizeof(regions), "%d,%d", region_x,
                         region_y);
        if (n <= 0 || (size_t)n >= sizeof(regions))
            return -1;
        missing_count = 1;
    }

    char cache_arg[1200];
    char regions_arg[1400];
    char output_arg[1200];
    if (!shell_quote(cache_arg, sizeof(cache_arg), cache)
            || !shell_quote(regions_arg, sizeof(regions_arg), regions)
            || !shell_quote(output_arg, sizeof(output_arg),
                            v->mapsquare_directory)) {
        return -1;
    }
    int timeout_seconds = env_int("RUNEC_SCENE_EXPORT_TIMEOUT_SECONDS", 120);
    if (timeout_seconds < 1)
        timeout_seconds = 1;
    char cmd[4096];
    int n = snprintf(
        cmd, sizeof(cmd),
        "timeout %d python3 tools/cache_pipeline/export_scene_slice.py "
        "--regions %s --cache %s --split-by-mapsquare --output-dir %s "
        "--planes %d",
        timeout_seconds, regions_arg, cache_arg, output_arg, plane);
    if (n <= 0 || (size_t)n >= sizeof(cmd))
        return -1;

    fprintf(stderr,
            "viewer mapsquare: generating %d missing visible chunk%s for "
            "%d,%d plane %d\n",
            missing_count, missing_count == 1 ? "" : "s", center_x,
            center_y, plane);
    int status = system(cmd);
    if (status != 0) {
        fprintf(stderr,
                "viewer mapsquare: split export failed with status %d; "
                "keeping the current scene\n",
                status);
        return -1;
    }
    viewer_load_mapsquare_catalog(v);
    if (!viewer_mapsquare_window_assets_available(
            v, center_x, center_y, plane)) {
        fprintf(stderr,
                "viewer mapsquare: split export completed without a "
                "complete visible window; keeping the current scene\n");
        return -1;
    }
    return 1;
}

static int viewer_mapsquare_center_available(const ViewerState *v, int x,
                                             int y, int plane) {
    if (!v || x < 0 || y < 0)
        return 0;
    int region_x = x / VIEWER_STREAMING_MAPSQUARE_SIZE;
    int region_y = y / VIEWER_STREAMING_MAPSQUARE_SIZE;
    return viewer_mapsquare_region_present(v, region_x, region_y)
        && viewer_mapsquare_materials_available(v)
        && viewer_mapsquare_chunk_available(
            v, region_x, region_y, clamp_plane(plane));
}

static int find_mapsquare_chunk(const ViewerState *v, int region_x,
                                int region_y, int plane) {
    if (!v)
        return -1;
    for (int i = 0; i < v->mapsquare_chunk_count; i++) {
        const ViewerMapsquareChunk *chunk = &v->mapsquare_chunks[i];
        if (chunk->region_x == region_x && chunk->region_y == region_y
                && chunk->plane == plane)
            return i;
    }
    return -1;
}

static void free_mapsquare_chunk(ViewerMapsquareChunk *chunk) {
    if (!chunk)
        return;
    free_model_anim_states(&chunk->object_anim_states,
                           &chunk->object_anim_state_count);
    models_free(chunk->object_anim_models);
    chunk->object_anim_models = NULL;
    objects_free(chunk->objects);
    chunk->objects = NULL;
    terrain_free(chunk->terrain);
    if (chunk->minimap_pixels)
        UnloadImageColors(chunk->minimap_pixels);
    memset(chunk, 0, sizeof(*chunk));
}

static void remove_mapsquare_chunk(ViewerState *v, int index) {
    if (!v || index < 0 || index >= v->mapsquare_chunk_count)
        return;
    free_mapsquare_chunk(&v->mapsquare_chunks[index]);
    v->mapsquare_chunk_count--;
    if (index < v->mapsquare_chunk_count) {
        memmove(&v->mapsquare_chunks[index],
                &v->mapsquare_chunks[index + 1],
                (size_t)(v->mapsquare_chunk_count - index)
                    * sizeof(v->mapsquare_chunks[0]));
    }
    memset(&v->mapsquare_chunks[v->mapsquare_chunk_count], 0,
           sizeof(v->mapsquare_chunks[0]));
}

static void free_mapsquare_cache(ViewerState *v) {
    if (!v)
        return;
    while (v->mapsquare_chunk_count > 0)
        remove_mapsquare_chunk(v, v->mapsquare_chunk_count - 1);
    objects_free(v->mapsquare_materials);
    v->mapsquare_materials = NULL;
    v->mapsquare_streaming_active = 0;
    v->mapsquare_center_region_x = 0;
    v->mapsquare_center_region_y = 0;
    memset(&v->mapsquare_eviction, 0, sizeof(v->mapsquare_eviction));
}

static int mapsquare_cache_limit(const ViewerState *v) {
    int limit = VIEWER_STREAMING_CHUNK_CAPACITY;
    if (!v)
        return limit;
    if (v->streaming.max_cpu_chunks < limit)
        limit = v->streaming.max_cpu_chunks;
    if (v->streaming.max_gpu_chunks < limit)
        limit = v->streaming.max_gpu_chunks;
    return limit;
}

static void viewer_async_mapsquare_payload_free(
    ViewerAsyncMapsquarePayload *payload) {
    if (!payload)
        return;
    objects_free(payload->materials);
    models_free(payload->npc_models);
    free_mapsquare_chunk(&payload->chunk);
    free(payload);
}

static void viewer_async_request_destroy(void *opaque, void *user) {
    (void)user;
    free(opaque);
}

static void viewer_async_payload_destroy(void *opaque, void *user) {
    (void)user;
    viewer_async_mapsquare_payload_free(opaque);
}

static ModelSet *viewer_decode_npc_models(
    const ViewerAsyncMapsquareRequest *request) {
    RcSpawnIndexSlice slice = {0};
    if (!request || !rc_spawn_index_read(
            request->npc_spawns_path, VIEWER_NPC_SPAWN_INDEX_MAGIC,
            VIEWER_NPC_SPAWN_RECORD_SIZE, 1,
            request->min_x, request->min_y,
            request->max_x, request->max_y, &slice)) {
        rc_spawn_index_slice_free(&slice);
        return NULL;
    }

    size_t capacity = (size_t)slice.record_count
                    + (size_t)request->extra_model_id_count + 1u;
    uint32_t *ids = calloc(capacity, sizeof(*ids));
    if (!ids) {
        rc_spawn_index_slice_free(&slice);
        return NULL;
    }
    int count = 0;
    for (uint32_t i = 0; i < slice.record_count; i++) {
        const unsigned char *record = slice.records
            + (size_t)i * slice.record_size;
        uint32_t npc_id = 0;
        int32_t x = 0;
        int32_t y = 0;
        memcpy(&npc_id, record + 4, sizeof(npc_id));
        memcpy(&x, record + 8, sizeof(x));
        memcpy(&y, record + 12, sizeof(y));
        if (x < request->min_x || x > request->max_x
                || y < request->min_y || y > request->max_y) {
            continue;
        }
        count = append_unique_model_id(ids, count, npc_id);
    }
    for (int i = 0; i < request->extra_model_id_count; i++) {
        count = append_unique_model_id(
            ids, count, request->extra_model_ids[i]);
    }
    rc_spawn_index_slice_free(&slice);
    ModelSet *models = models_load_filtered_cpu(
        request->models_path, ids, count);
    free(ids);
    return models;
}

static void viewer_decode_minimap(ViewerMapsquareChunk *chunk,
                                  const char *path) {
    if (!chunk || !path || !path[0] || !rc_asset_exists(path))
        return;
    Image image = runec_load_image_asset(path);
    if (!image.data)
        return;
    if (image.width == RUNEC_MINIMAP_SOURCE_SIZE
            && image.height == RUNEC_MINIMAP_SOURCE_SIZE) {
        ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        chunk->minimap_pixels = LoadImageColors(image);
        if (chunk->minimap_pixels) {
            chunk->minimap_width = image.width;
            chunk->minimap_height = image.height;
        }
    } else {
        fprintf(stderr,
                "minimap: ignored %s with invalid size %dx%d (expected %dx%d)\n",
                path, image.width, image.height,
                RUNEC_MINIMAP_SOURCE_SIZE, RUNEC_MINIMAP_SOURCE_SIZE);
    }
    UnloadImage(image);
}

static void *viewer_async_mapsquare_decode(void *opaque,
                                           size_t *upload_bytes,
                                           double *decode_ms, void *user) {
    (void)user;
    ViewerAsyncMapsquareRequest *request = opaque;
    double started_ms = viewer_streaming_now_ms();
    ViewerAsyncMapsquarePayload *payload = calloc(1, sizeof(*payload));
    if (!request || !payload)
        goto failed;
    payload->kind = request->kind;
    if (request->kind == VIEWER_ASYNC_MATERIAL_PAGE) {
        payload->materials = objects_load_material_page_cpu(
            request->material_path);
        if (!payload->materials)
            goto failed;
        *upload_bytes = objects_material_page_upload_bytes(
            payload->materials);
    } else if (request->kind == VIEWER_ASYNC_MAPSQUARE_CHUNK) {
        ViewerMapsquareChunk *chunk = &payload->chunk;
        chunk->region_x = request->region_x;
        chunk->region_y = request->region_y;
        chunk->plane = request->plane;
        chunk->terrain = terrain_load_cpu(request->terrain_path);
        chunk->objects = objects_load_cpu_with_shared_atlas(
            request->objects_path);
        if (!chunk->terrain || !chunk->objects)
            goto failed;
        if (chunk->objects->object_anim_count > 0) {
            if (!rc_asset_exists(request->models_path)) {
                fprintf(stderr,
                        "viewer mapsquare: missing animated-object models "
                        "for %d,%d plane %d\n",
                        chunk->region_x, chunk->region_y, chunk->plane);
                goto failed;
            }
            chunk->object_anim_models =
                models_load_cpu_with_shared_atlas(request->models_path);
            if (!chunk->object_anim_models)
                goto failed;
        }
        viewer_decode_minimap(chunk, request->minimap_path);
        payload->terrain_upload_bytes = terrain_upload_bytes(chunk->terrain);
        payload->object_upload_bytes = objects_upload_bytes(chunk->objects);
        payload->model_upload_bytes = models_upload_bytes(
            chunk->object_anim_models);
        *upload_bytes = payload->terrain_upload_bytes;
        if (SIZE_MAX - *upload_bytes < payload->object_upload_bytes)
            *upload_bytes = SIZE_MAX;
        else
            *upload_bytes += payload->object_upload_bytes;
        if (SIZE_MAX - *upload_bytes < payload->model_upload_bytes)
            *upload_bytes = SIZE_MAX;
        else
            *upload_bytes += payload->model_upload_bytes;
    } else if (request->kind == VIEWER_ASYNC_NPC_MODELS) {
        payload->npc_models = viewer_decode_npc_models(request);
        if (!payload->npc_models)
            goto failed;
        payload->model_upload_bytes = models_upload_bytes(
            payload->npc_models);
        *upload_bytes = payload->model_upload_bytes;
    } else {
        goto failed;
    }
    *decode_ms = viewer_streaming_now_ms() - started_ms;
    return payload;

failed:
    viewer_async_mapsquare_payload_free(payload);
    *decode_ms = viewer_streaming_now_ms() - started_ms;
    return NULL;
}

static void discard_pending_mapsquare_load(ViewerState *v) {
    if (!v)
        return;
    ViewerPendingMapsquareLoad *load = &v->mapsquare_load;
    for (int i = 0; i < load->staged_count; i++)
        free_mapsquare_chunk(&load->staged[i].chunk);
    objects_free(load->materials);
    models_free(load->npc_models);
    memset(load, 0, sizeof(*load));
}

static void viewer_cancel_mapsquare_loading(ViewerState *v) {
    if (!v || !v->mapsquare_loader.initialized)
        return;
    uint64_t generation = ++v->mapsquare_load_generation;
    if (generation == 0)
        generation = ++v->mapsquare_load_generation;
    viewer_async_loader_set_generation(&v->mapsquare_loader, generation);
    discard_pending_mapsquare_load(v);
    v->pending_dev_transport = NULL;
}

static uint64_t mapsquare_job_key(int region_x, int region_y, int plane) {
    uint16_t mapsquare;
    if (!rc_mapsquare_key(region_x, region_y, &mapsquare)
            || !rc_plane_valid(plane)) {
        return 0;
    }
    return 1u + mapsquare + ((uint64_t)(uint32_t)plane << 16);
}

static int pending_chunk_index(const ViewerPendingMapsquareLoad *load,
                               int region_x, int region_y, int plane) {
    for (int i = 0; load && i < load->staged_count; i++) {
        const ViewerMapsquareChunk *chunk = &load->staged[i].chunk;
        if (chunk->region_x == region_x && chunk->region_y == region_y
                && chunk->plane == plane)
            return i;
    }
    return -1;
}

static ViewerAsyncMapsquareRequest *mapsquare_chunk_request(
    const ViewerState *v, int region_x, int region_y, int plane) {
    ViewerAsyncMapsquareRequest *request = calloc(1, sizeof(*request));
    if (!request)
        return NULL;
    request->kind = VIEWER_ASYNC_MAPSQUARE_CHUNK;
    request->region_x = region_x;
    request->region_y = region_y;
    request->plane = plane;
    if (!viewer_mapsquare_asset_path(
            v, request->terrain_path, sizeof(request->terrain_path),
            region_x, region_y, plane, ".terrain")
            || !viewer_mapsquare_asset_path(
                v, request->objects_path, sizeof(request->objects_path),
                region_x, region_y, plane, ".objects")
            || !viewer_mapsquare_asset_path(
                v, request->models_path, sizeof(request->models_path),
                region_x, region_y, plane, ".object_anim.models")
            || !viewer_mapsquare_asset_path(
                v, request->minimap_path, sizeof(request->minimap_path),
                region_x, region_y, plane, ".minimap.png")) {
        free(request);
        return NULL;
    }
    return request;
}

static ViewerAsyncMapsquareRequest *npc_models_request(
    const ViewerState *v, int center_x, int center_y) {
    if (!v || !v->world)
        return NULL;
    ViewerAsyncMapsquareRequest *request = calloc(1, sizeof(*request));
    if (!request)
        return NULL;
    request->kind = VIEWER_ASYNC_NPC_MODELS;
    snprintf(request->models_path, sizeof(request->models_path), "%s",
             env_path("RUNEC_NPC_MODELS", "data/models/npcs.models"));
    snprintf(request->npc_spawns_path, sizeof(request->npc_spawns_path), "%s",
             v->world->npc_spawns_path);
    int origin_x = 0;
    int origin_y = 0;
    int width = 0;
    int height = 0;
    if (!scene_bounds_for_tile(center_x, center_y,
                               v->streaming.scene_radius_regions,
                               &origin_x, &origin_y, &width, &height)) {
        free(request);
        return NULL;
    }
    request->min_x = origin_x > 8 ? origin_x - 8 : 0;
    request->min_y = origin_y > 8 ? origin_y - 8 : 0;
    request->max_x = origin_x + width + 7;
    request->max_y = origin_y + height + 7;
    if (request->max_x > RC_WORLD_MAX) request->max_x = RC_WORLD_MAX;
    if (request->max_y > RC_WORLD_MAX) request->max_y = RC_WORLD_MAX;
    request->extra_model_ids[request->extra_model_id_count++] = 2668;
    int transport_count = 0;
    const RuneCDevTransport *transports =
        runec_dev_validation_transports(&transport_count);
    for (int i = 0; transports && i < transport_count
            && request->extra_model_id_count
                < RUNEC_UI_DEV_TRANSPORT_MAX + 1; i++) {
        if (transports[i].npc_id >= 0) {
            request->extra_model_ids[request->extra_model_id_count++] =
                (uint32_t)transports[i].npc_id;
        }
    }
    int roles = append_encounter_role_model_ids(
        v->world, request->extra_model_ids, request->extra_model_id_count,
        (int)(sizeof(request->extra_model_ids)
              / sizeof(request->extra_model_ids[0])));
    if (roles < 0) {
        fprintf(stderr, "npc models: encounter role models exceed the request\n");
        free(request);
        return NULL;
    }
    request->extra_model_id_count = roles;
    return request;
}

static int stale_mapsquare_chunk_index(
    const ViewerState *v, const ViewerMapsquareCoord *plan,
    int plan_count, int plane) {
    if (!v)
        return -1;
    int player_plane = v->world ? clamp_plane(v->world->player.plane) : plane;
    for (int i = v->mapsquare_chunk_count - 1; i >= 0; i--) {
        const ViewerMapsquareChunk *chunk = &v->mapsquare_chunks[i];
        if (!viewer_streaming_chunk_retained(
                plan, plan_count, chunk->region_x, chunk->region_y,
                chunk->plane, plane, player_plane)) {
            return i;
        }
    }
    return -1;
}

static int make_mapsquare_cache_room(
    ViewerState *v, const ViewerMapsquareCoord *plan,
    int plan_count, int plane, int incoming_count) {
    int limit = mapsquare_cache_limit(v);
    while (v && v->mapsquare_chunk_count + incoming_count > limit) {
        int stale = stale_mapsquare_chunk_index(
            v, plan, plan_count, plane);
        if (stale < 0)
            return 0;
        remove_mapsquare_chunk(v, stale);
    }
    return v != NULL;
}

static void schedule_mapsquare_eviction(
    ViewerState *v, const ViewerMapsquareCoord *plan,
    int plan_count, int plane) {
    if (!v || !plan || plan_count <= 0
            || plan_count > VIEWER_STREAMING_CHUNK_CAPACITY) {
        if (v)
            memset(&v->mapsquare_eviction, 0,
                   sizeof(v->mapsquare_eviction));
        return;
    }
    ViewerDeferredMapsquareEviction *eviction = &v->mapsquare_eviction;
    eviction->active = 1;
    eviction->plan_count = plan_count;
    eviction->plane = plane;
    memcpy(eviction->plan, plan,
           (size_t)plan_count * sizeof(eviction->plan[0]));
}

static void viewer_process_deferred_mapsquare_eviction(ViewerState *v) {
    if (!v || !v->mapsquare_eviction.active)
        return;
    ViewerDeferredMapsquareEviction *eviction = &v->mapsquare_eviction;
    int stale = stale_mapsquare_chunk_index(
        v, eviction->plan, eviction->plan_count, eviction->plane);
    if (stale >= 0) {
        remove_mapsquare_chunk(v, stale);
        return;
    }
    memset(eviction, 0, sizeof(*eviction));
    viewer_log_streaming_telemetry(v, "cache-retired");
}

static int rebase_mapsquare_cache(ViewerState *v,
                                  const ViewerWorldTransform *target) {
    if (!v || !target)
        return 0;
    for (int i = 0; i < v->mapsquare_chunk_count; i++) {
        ViewerMapsquareChunk *chunk = &v->mapsquare_chunks[i];
        ViewerWorldTransform current = {
            chunk->render_origin_x,
            chunk->render_origin_y,
        };
        int delta_x = 0;
        int delta_y = 0;
        if (!viewer_world_transform_rebase_delta(
                &current, target, &delta_x, &delta_y)) {
            return 0;
        }
        if (delta_x != 0 || delta_y != 0) {
            terrain_offset(chunk->terrain, delta_x, delta_y);
            objects_offset(chunk->objects, delta_x, delta_y);
        }
        chunk->render_origin_x = target->origin_x;
        chunk->render_origin_y = target->origin_y;
    }
    return 1;
}

static int submit_mapsquare_request(ViewerState *v,
                                    ViewerAsyncMapsquareRequest *request,
                                    uint64_t key) {
    if (!v || !request)
        return 0;
    if (viewer_async_loader_submit(&v->mapsquare_loader,
                                   v->mapsquare_load.generation, key,
                                   request)) {
        v->mapsquare_load.expected_results++;
        return 1;
    }
    free(request);
    return 0;
}

static int viewer_request_mapsquare_window(ViewerState *v, int center_x,
                                           int center_y, int plane,
                                           ViewerStreamingRequestKind kind,
                                           const char *reason) {
    if (!v || !v->world || !v->mapsquare_loader.initialized
            || !viewer_mapsquare_cache_allowed()) {
        return 0;
    }
    plane = clamp_plane(plane);
    ViewerPendingMapsquareLoad *load = &v->mapsquare_load;
    if (load->active && viewer_streaming_same_window(
            load->center_x, load->center_y, load->plane,
            center_x, center_y, plane)
            && !(kind == VIEWER_STREAMING_REQUEST_ACTIVATE
                 && load->request_kind == VIEWER_STREAMING_REQUEST_PLANE)) {
        if (kind == VIEWER_STREAMING_REQUEST_ACTIVATE
                && (load->request_kind
                        == VIEWER_STREAMING_REQUEST_MOVEMENT_PREFETCH
                    || load->request_kind
                        == VIEWER_STREAMING_REQUEST_TRANSPORT_PREFETCH)) {
            load->request_kind = VIEWER_STREAMING_REQUEST_ACTIVATE;
            snprintf(load->reason, sizeof(load->reason), "%s",
                     reason ? reason : "mapsquare-window");
            fprintf(stderr,
                    "viewer mapsquare: promoted prefetched window %d,%d "
                    "plane %d\n",
                    center_x / VIEWER_STREAMING_MAPSQUARE_SIZE,
                    center_y / VIEWER_STREAMING_MAPSQUARE_SIZE, plane);
            return viewer_process_mapsquare_loading(v) > 0 ? 1 : 2;
        }
        if (kind == VIEWER_STREAMING_REQUEST_TRANSPORT_PREFETCH
                && load->request_kind
                    == VIEWER_STREAMING_REQUEST_MOVEMENT_PREFETCH) {
            load->request_kind = kind;
            snprintf(load->reason, sizeof(load->reason), "%s",
                     reason ? reason : "transport-prefetch");
        }
        return 2;
    }

    viewer_cancel_mapsquare_loading(v);
    load = &v->mapsquare_load;
    load->active = 1;
    load->generation = v->mapsquare_load_generation;
    load->center_x = center_x;
    load->center_y = center_y;
    load->plane = plane;
    load->request_kind = kind;
    load->started_ms = viewer_begin_streaming_load(v);
    snprintf(load->reason, sizeof(load->reason), "%s",
             reason ? reason : "mapsquare-window");

    load->plan_count = viewer_streaming_plan_mapsquares(
        &v->streaming, center_x, center_y, load->plan,
        VIEWER_STREAMING_CHUNK_CAPACITY);
    if (load->plan_count >= 0) {
        load->plan_count = viewer_filter_mapsquare_plan(
            v, load->plan, load->plan_count);
    }
    load->visible_count = viewer_mapsquare_visible_plan(
        v, center_x, center_y, load->visible_plan,
        VIEWER_STREAMING_CHUNK_CAPACITY);
    if (load->plan_count <= 0 || load->visible_count <= 0
            || !viewer_mapsquare_window_assets_available(
                v, center_x, center_y, plane)) {
        goto failed;
    }

    if (!v->mapsquare_materials) {
        ViewerAsyncMapsquareRequest *request = calloc(1, sizeof(*request));
        if (!request)
            goto failed;
        request->kind = VIEWER_ASYNC_MATERIAL_PAGE;
        if (!viewer_mapsquare_material_path(
                v, request->material_path, sizeof(request->material_path))
                || !submit_mapsquare_request(v, request, UINT64_MAX)) {
            goto failed;
        }
    }

    int player_plane = clamp_plane(v->world->player.plane);
    int retained_count = 0;
    for (int i = 0; i < v->mapsquare_chunk_count; i++) {
        ViewerMapsquareChunk *chunk = &v->mapsquare_chunks[i];
        if ((chunk->plane == plane || chunk->plane == player_plane)
                && viewer_streaming_mapsquare_in_plan(
                    load->plan, load->plan_count,
                    chunk->region_x, chunk->region_y)) {
            retained_count++;
        }
    }
    int stage_limit = mapsquare_cache_limit(v) - retained_count;
    int submitted_chunks = 0;
    for (int i = 0; i < load->plan_count; i++) {
        int region_x = load->plan[i].region_x;
        int region_y = load->plan[i].region_y;
        if (find_mapsquare_chunk(v, region_x, region_y, plane) >= 0) {
            load->loaded_count++;
            continue;
        }
        int required = viewer_streaming_mapsquare_in_plan(
            load->visible_plan, load->visible_count, region_x, region_y);
        if (!viewer_mapsquare_chunk_available(
                v, region_x, region_y, plane)) {
            if (required)
                goto failed;
            continue;
        }
        if (submitted_chunks >= stage_limit) {
            if (required)
                goto failed;
            continue;
        }
        ViewerAsyncMapsquareRequest *request = mapsquare_chunk_request(
            v, region_x, region_y, plane);
        if (!request || !submit_mapsquare_request(
                v, request, mapsquare_job_key(region_x, region_y, plane))) {
            if (required)
                goto failed;
            continue;
        }
        submitted_chunks++;
    }

    if (kind != VIEWER_STREAMING_REQUEST_PLANE) {
        ViewerAsyncMapsquareRequest *request = npc_models_request(
            v, center_x, center_y);
        load->needs_npc_models = 1;
        if (!request || !submit_mapsquare_request(
                v, request, UINT64_MAX - 1u)) {
            goto failed;
        }
    }

    if (load->expected_results == 0)
        return viewer_process_mapsquare_loading(v) > 0 ? 1 : 0;
    return 2;

failed:
    fprintf(stderr,
            "viewer mapsquare: could not queue complete destination "
            "%d,%d plane %d\n", center_x, center_y, plane);
    viewer_cancel_mapsquare_loading(v);
    return 0;
}

static int pending_window_complete(const ViewerState *v) {
    const ViewerPendingMapsquareLoad *load = &v->mapsquare_load;
    if (load->received_results != load->expected_results
            || (!v->mapsquare_materials && !load->materials)) {
        return 0;
    }
    if (load->needs_npc_models && !load->npc_models_ready)
        return 0;
    for (int i = 0; i < load->staged_count; i++) {
        if (load->staged[i].upload_stage < 3)
            return 0;
    }
    for (int i = 0; i < load->visible_count; i++) {
        int region_x = load->visible_plan[i].region_x;
        int region_y = load->visible_plan[i].region_y;
        if (find_mapsquare_chunk(v, region_x, region_y, load->plane) >= 0)
            continue;
        int staged = pending_chunk_index(
            load, region_x, region_y, load->plane);
        if (staged < 0 || load->staged[staged].upload_stage < 3)
            return 0;
    }
    return 1;
}

static int upload_pending_mapsquare_resources(ViewerState *v) {
    ViewerPendingMapsquareLoad *load = &v->mapsquare_load;
    size_t budget = viewer_streaming_upload_budget_bytes(&v->streaming);
    size_t used = 0;
    v->frame_gpu_upload_bytes = 0;

    if (!v->mapsquare_materials && load->materials) {
        size_t bytes = objects_material_page_upload_bytes(load->materials);
        if (!viewer_streaming_upload_budget_admit(used, bytes, budget))
            return 1;
        if (!objects_upload_material_page(load->materials))
            return 0;
        used += bytes;
        v->frame_gpu_upload_bytes = used;
        if (used > v->peak_gpu_upload_bytes)
            v->peak_gpu_upload_bytes = used;
        v->pending_gpu_upload_ms += load->materials->gpu_upload_ms;
        v->mapsquare_materials = load->materials;
        load->materials = NULL;
    }

    for (int i = 0; i < load->staged_count; i++) {
        ViewerPendingMapsquareChunk *pending = &load->staged[i];
        ViewerMapsquareChunk *chunk = &pending->chunk;
        if (pending->upload_stage == 0) {
            size_t bytes = pending->terrain_upload_bytes;
            if (!viewer_streaming_upload_budget_admit(used, bytes, budget))
                break;
            terrain_offset(chunk->terrain, g_world_origin_x,
                           g_world_origin_y);
            chunk->render_origin_x = g_world_origin_x;
            chunk->render_origin_y = g_world_origin_y;
            if (!terrain_upload(chunk->terrain))
                return 0;
            used += bytes;
            v->frame_gpu_upload_bytes = used;
            if (used > v->peak_gpu_upload_bytes)
                v->peak_gpu_upload_bytes = used;
            v->pending_gpu_upload_ms += chunk->terrain->gpu_upload_ms;
            pending->upload_stage = 1;
        }
        if (pending->upload_stage == 1) {
            size_t bytes = pending->object_upload_bytes;
            if (!viewer_streaming_upload_budget_admit(used, bytes, budget))
                break;
            objects_offset(chunk->objects, g_world_origin_x,
                           g_world_origin_y);
            if (!objects_upload_with_shared_atlas(
                    chunk->objects,
                    v->mapsquare_materials->atlas_texture)) {
                return 0;
            }
            used += bytes;
            v->frame_gpu_upload_bytes = used;
            if (used > v->peak_gpu_upload_bytes)
                v->peak_gpu_upload_bytes = used;
            v->pending_gpu_upload_ms += chunk->objects->gpu_upload_ms;
            objects_release_cpu_geometry(chunk->objects);
            if (v->alpha_cutout_shader_static_loaded) {
                objects_set_shader(chunk->objects,
                                   v->alpha_cutout_shader_static);
            }
            pending->upload_stage = 2;
        }
        if (pending->upload_stage == 2) {
            if (chunk->object_anim_models) {
                size_t uploaded = 0;
                double gpu_before =
                    chunk->object_anim_models->gpu_upload_ms;
                if (!models_upload_budgeted(
                        chunk->object_anim_models,
                        v->mapsquare_materials->atlas_texture,
                        &pending->model_upload_cursor, used, budget,
                        &uploaded)) {
                    return 0;
                }
                used += uploaded;
                v->frame_gpu_upload_bytes = used;
                if (used > v->peak_gpu_upload_bytes)
                    v->peak_gpu_upload_bytes = used;
                v->pending_gpu_upload_ms +=
                    chunk->object_anim_models->gpu_upload_ms - gpu_before;
                if (pending->model_upload_cursor
                        < chunk->object_anim_models->count) {
                    break;
                }
                if (v->alpha_cutout_shader_static_loaded) {
                    models_set_shader(chunk->object_anim_models,
                                      v->alpha_cutout_shader_static);
                }
                create_object_anim_states(
                    chunk->objects, chunk->object_anim_models,
                    &chunk->object_anim_states,
                    &chunk->object_anim_state_count);
            }
            pending->upload_stage = 3;
        }
    }

    if (load->needs_npc_models && load->npc_models
            && !load->npc_models_ready) {
        size_t uploaded = 0;
        double gpu_before = load->npc_models->gpu_upload_ms;
        if (!models_upload_budgeted(
                load->npc_models, (Texture2D){0},
                &load->npc_model_upload_cursor, used, budget, &uploaded)) {
            return 0;
        }
        used += uploaded;
        v->frame_gpu_upload_bytes = used;
        if (used > v->peak_gpu_upload_bytes)
            v->peak_gpu_upload_bytes = used;
        v->pending_gpu_upload_ms +=
            load->npc_models->gpu_upload_ms - gpu_before;
        if (load->npc_model_upload_cursor >= load->npc_models->count) {
            load->npc_models_ready = 1;
            if (v->alpha_cutout_shader_dynamic_loaded) {
                models_set_shader(load->npc_models,
                                  v->alpha_cutout_shader_dynamic);
            }
        }
    }
    return 1;
}

static int commit_pending_mapsquare_window(ViewerState *v) {
    ViewerPendingMapsquareLoad *load = &v->mapsquare_load;

    int previous_chunk_count = v->mapsquare_chunk_count;
    if (!make_mapsquare_cache_room(
            v, load->plan, load->plan_count, load->plane,
            load->staged_count)) {
        return 0;
    }
    for (int i = 0; i < load->staged_count; i++) {
        v->mapsquare_chunks[v->mapsquare_chunk_count++] =
            load->staged[i].chunk;
        memset(&load->staged[i].chunk, 0,
               sizeof(load->staged[i].chunk));
    }
    if (previous_chunk_count > 0) {
        schedule_mapsquare_eviction(
            v, load->plan, load->plan_count, load->plane);
    } else {
        memset(&v->mapsquare_eviction, 0, sizeof(v->mapsquare_eviction));
    }

    int origin_x = 0, origin_y = 0, world_w = 0, world_h = 0;
    if (!scene_bounds_for_tile(load->center_x, load->center_y,
                               v->streaming.scene_radius_regions,
                               &origin_x, &origin_y,
                               &world_w, &world_h)) {
        return 0;
    }
    ViewerWorldTransform target_transform;
    if (!viewer_world_transform_set(&target_transform, origin_x, origin_y)
            || !rebase_mapsquare_cache(v, &target_transform)) {
        return 0;
    }
    g_world_transform = target_transform;
    g_world_w = world_w;
    g_world_h = world_h;
    clear_aggregate_visual_assets(v);

    v->mapsquare_streaming_active = 1;
    int center_region_x = load->center_x
                        / VIEWER_STREAMING_MAPSQUARE_SIZE;
    int center_region_y = load->center_y
                        / VIEWER_STREAMING_MAPSQUARE_SIZE;
    v->mapsquare_center_region_x = center_region_x;
    v->mapsquare_center_region_y = center_region_y;
    if (load->npc_models) {
        free_npc_anim_states(v);
        models_free(v->npc_models);
        v->npc_models = load->npc_models;
        load->npc_models = NULL;
        create_npc_anim_states(v);
    }
    int object_count = 0;
    int minimap_count = 0;
    for (int i = 0; i < v->mapsquare_chunk_count; i++) {
        const ViewerMapsquareChunk *chunk = &v->mapsquare_chunks[i];
        if (chunk->plane != load->plane
                || !viewer_streaming_mapsquare_in_plan(
                    load->plan, load->plan_count,
                    chunk->region_x, chunk->region_y)) {
            continue;
        }
        if (chunk->objects)
            object_count++;
        if (chunk->minimap_pixels)
            minimap_count++;
    }
    v->active_scene_prefix[0] = '\0';
    build_minimap_tiles(v);
    fprintf(stderr,
            "viewer mapsquare: center=%d,%d plane=%d terrain=%d/%d "
            "objects=%d minimaps=%d/%d resident=%d origin=%d,%d size=%dx%d\n",
            center_region_x, center_region_y, load->plane,
            load->loaded_count, load->plan_count, object_count,
            minimap_count, load->plan_count, v->mapsquare_chunk_count,
            g_world_origin_x, g_world_origin_y, g_world_w, g_world_h);
    return 1;
}

static void record_mapsquare_main_thread_work(
    ViewerPendingMapsquareLoad *load, double started_ms) {
    if (!load)
        return;
    double elapsed_ms = viewer_streaming_now_ms() - started_ms;
    load->process_frames++;
    if (elapsed_ms > load->main_thread_work_max_ms)
        load->main_thread_work_max_ms = elapsed_ms;
}

static int viewer_process_mapsquare_loading(ViewerState *v) {
    if (!v || !v->mapsquare_load.active)
        return 0;
    double main_thread_started_ms = viewer_streaming_now_ms();
    ViewerPendingMapsquareLoad *load = &v->mapsquare_load;
    if (load->prefetch_ready
            && (load->request_kind
                    == VIEWER_STREAMING_REQUEST_MOVEMENT_PREFETCH
                || load->request_kind
                    == VIEWER_STREAMING_REQUEST_TRANSPORT_PREFETCH)) {
        return 0;
    }
    ViewerAsyncResult result;
    while (viewer_async_loader_poll(&v->mapsquare_loader, &result)) {
        if (result.generation != load->generation) {
            viewer_async_loader_release_result(&v->mapsquare_loader, &result);
            continue;
        }
        load->received_results++;
        v->pending_cpu_decode_ms += result.decode_ms;
        if (!result.success || !result.payload) {
            load->failed = 1;
            viewer_async_loader_release_result(&v->mapsquare_loader, &result);
            continue;
        }
        ViewerAsyncMapsquarePayload *payload = result.payload;
        result.payload = NULL;
        if (payload->kind == VIEWER_ASYNC_MATERIAL_PAGE) {
            if (load->materials)
                load->failed = 1;
            else {
                load->materials = payload->materials;
                payload->materials = NULL;
            }
        } else if (payload->kind == VIEWER_ASYNC_NPC_MODELS) {
            if (load->npc_models) {
                load->failed = 1;
            } else {
                load->npc_models = payload->npc_models;
                payload->npc_models = NULL;
                load->npc_model_upload_bytes = payload->model_upload_bytes;
            }
        } else if (payload->kind == VIEWER_ASYNC_MAPSQUARE_CHUNK
                   && load->staged_count
                       < VIEWER_STREAMING_CHUNK_CAPACITY) {
            ViewerPendingMapsquareChunk *pending =
                &load->staged[load->staged_count++];
            pending->chunk = payload->chunk;
            memset(&payload->chunk, 0, sizeof(payload->chunk));
            pending->terrain_upload_bytes = payload->terrain_upload_bytes;
            pending->object_upload_bytes = payload->object_upload_bytes;
            pending->model_upload_bytes = payload->model_upload_bytes;
            load->loaded_count++;
        } else {
            load->failed = 1;
        }
        viewer_async_mapsquare_payload_free(payload);
        viewer_async_loader_release_result(&v->mapsquare_loader, &result);
    }

    if (!load->failed && !upload_pending_mapsquare_resources(v))
        load->failed = 1;
    if (load->failed) {
        int center_x = load->center_x;
        int center_y = load->center_y;
        int plane = load->plane;
        fprintf(stderr,
                "viewer mapsquare: asynchronous destination %d,%d plane %d "
                "failed; keeping the current scene\n",
                center_x, center_y, plane);
        record_mapsquare_main_thread_work(load, main_thread_started_ms);
        viewer_cancel_mapsquare_loading(v);
        return -1;
    }
    if (!pending_window_complete(v)) {
        record_mapsquare_main_thread_work(load, main_thread_started_ms);
        return 0;
    }
    if (load->request_kind == VIEWER_STREAMING_REQUEST_MOVEMENT_PREFETCH
            || load->request_kind
                == VIEWER_STREAMING_REQUEST_TRANSPORT_PREFETCH) {
        record_mapsquare_main_thread_work(load, main_thread_started_ms);
        if (!load->prefetch_ready) {
            load->prefetch_ready = 1;
            viewer_finish_streaming_load(v, load->started_ms,
                                         load->reason, 1);
            load->telemetry_finished = 1;
            fprintf(stderr,
                    "viewer mapsquare: prefetched center=%d,%d plane=%d "
                    "chunks=%d/%d\n",
                    load->center_x / VIEWER_STREAMING_MAPSQUARE_SIZE,
                    load->center_y / VIEWER_STREAMING_MAPSQUARE_SIZE,
                    load->plane, load->loaded_count, load->plan_count);
        }
        return 0;
    }
    if (load->player_transition.active
            && !viewer_streaming_player_transition_current(
                &load->player_transition, v->traversal_generation,
                v->world->player.x, v->world->player.y,
                v->world->player.plane)) {
        fprintf(stderr,
                "viewer scene: discarded stale visual transition "
                "generation=%llu\n",
                (unsigned long long)
                    load->player_transition.traversal_generation);
        viewer_cancel_mapsquare_loading(v);
        return -1;
    }
    if (!commit_pending_mapsquare_window(v)) {
        record_mapsquare_main_thread_work(load, main_thread_started_ms);
        viewer_cancel_mapsquare_loading(v);
        return -1;
    }
    record_mapsquare_main_thread_work(load, main_thread_started_ms);
    if (!load->telemetry_finished)
        viewer_finish_streaming_load(v, load->started_ms, load->reason, 1);
    const RuneCDevTransport *pending_transport = v->pending_dev_transport;
    v->pending_dev_transport = NULL;
    viewer_commit_player_scene_transition(v);
    discard_pending_mapsquare_load(v);
    if (pending_transport)
        viewer_finish_dev_transport(v, pending_transport);
    return 1;
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
    free(entry->rest_colors);
    free(entry->base_verts);
    free(entry->vertex_skins);
    free(entry->face_indices);
    free(entry->face_priorities);
    free(entry->face_alphas);
    free(entry->face_skins);
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
    unsigned char *rest_colors = calloc((size_t)total_vc * 4, sizeof(unsigned char));
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
    if (!verts || !rest || !normals || !colors || !rest_colors || !base || !skins
            || !faces || !priorities || (v->item_models->has_textures && (!texcoords || !face_uvs))) {
        free(verts); free(rest); free(normals); free(colors);
        free(rest_colors);
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
        if (src->rest_colors) {
            memcpy(&colors[vc_off * 4], src->rest_colors, (size_t)vc * 4);
            memcpy(&rest_colors[vc_off * 4], src->rest_colors, (size_t)vc * 4);
        } else if (mesh->colors) {
            memcpy(&colors[vc_off * 4], mesh->colors, (size_t)vc * 4);
            memcpy(&rest_colors[vc_off * 4], mesh->colors, (size_t)vc * 4);
        }
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
    v->composed_player_model = (ModelEntry){
        .model_id = 0xC0FFEEu,
        .model = composed_model,
        .loaded = 1,
        .rest_verts = rest,
        .rest_texcoords = texcoords && total_vc > 0
            ? malloc((size_t)total_vc * 2 * sizeof(float)) : NULL,
        .rest_colors = rest_colors,
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
    if (mesh->colors && entry->rest_colors) {
        memcpy(mesh->colors, entry->rest_colors, (size_t)vc * 4);
        UpdateMeshBuffer(mesh[0], 3, mesh->colors, vc * 4, 0);
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
        const RuneCItemDefRenderRecord *render_def =
            runec_item_def_render_find(&v->item_def_render_map, eq->item_id);
        if (!render_def)
            continue;
        for (int i = 0; i < 3; i++) {
            uint32_t model_id = render_def->male_model_ids[i];
            if (model_id == RUNEC_RENDER_MODEL_MISSING)
                continue;
            drawn += draw_item_model(v, model_id, player_pos,
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

static ModelSet *ground_item_models(ViewerState *v, int item_id) {
    uint64_t stamp = ++v->ground_model_clock;
    int oldest = 0;
    for (int i = 0; i < RC_MAX_GROUND_ITEMS; i++) {
        if (v->ground_models[i].last_use && v->ground_models[i].item_id == item_id) {
            v->ground_models[i].last_use = stamp;
            return v->ground_models[i].models;
        }
        if (v->ground_models[i].last_use < v->ground_models[oldest].last_use)
            oldest = i;
    }
    models_free(v->ground_models[oldest].models);
    char path[128];
    snprintf(path, sizeof(path), "data/models/ground/%d.models", item_id);
    ModelSet *models = models_load_cpu_with_shared_atlas(path);
    if (!models || !model_find(models, (uint32_t)item_id)) {
        fprintf(stderr, "ground item %d: missing render model; rebuild ground-item assets (%s)\n",
                item_id, path);
        models_free(models);
        models = NULL;
    }
    v->ground_models[oldest].models = models;
    v->ground_models[oldest].item_id = item_id;
    v->ground_models[oldest].last_use = stamp;
    return models;
}

static void draw_ground_item_model(ViewerState *v, const RcGroundItem *ground,
                                   Vector3 pos) {
    int item_id = item_display_id_for_quantity(v, ground->item_id, ground->quantity);
    ModelSet *models = ground_item_models(v, item_id);
    if (!models || !v->item_models || !v->item_models->atlas_texture.id)
        return;
    ModelEntry *entry = model_find(models, (uint32_t)item_id);
    if (!entry->uploaded && !models_upload_with_shared_atlas(
            models, v->item_models->atlas_texture)) {
        fprintf(stderr, "ground item %d: GPU upload failed\n", item_id);
        return;
    }
    draw_model_entry_one_sided(entry, pos, 0.0f, 1.0f, WHITE,
        v->alpha_cutout_shader_dynamic, v->alpha_cutout_shader_dynamic_loaded);
}

static const char *combat_actor_kind_name(int kind) {
    switch (kind) {
        case RC_COMBAT_ACTOR_PLAYER: return "player";
        case RC_COMBAT_ACTOR_NPC: return "npc";
        default: return "none";
    }
}

static const char *combat_action_kind_name(int kind) {
    switch (kind) {
        case RC_COMBAT_ACTION_ITEM: return "item";
        case RC_COMBAT_ACTION_SPELL: return "spell";
        case RC_COMBAT_ACTION_NPC: return "npc";
        case RC_COMBAT_ACTION_SPECIAL: return "special";
        default: return "none";
    }
}

static const char *combat_style_name(int style) {
    switch (style) {
        case COMBAT_MELEE_STAB: return "stab";
        case COMBAT_MELEE_SLASH: return "slash";
        case COMBAT_MELEE_CRUSH: return "crush";
        case COMBAT_RANGED: return "ranged";
        case COMBAT_MAGIC: return "magic";
        default: return "unknown";
    }
}

static void debug_log_combat_attack_events(ViewerState *v) {
    if (!v || !v->world || !env_bool("RUNEC_DEBUG_COMBAT_VISUAL_EVENTS", 0))
        return;
    int count = 0;
    const RcCombatAttackEvent *events =
        rc_combat_attack_events(v->world, &count);
    for (int i = 0; events && i < count; i++) {
        const RcCombatAttackEvent *e = &events[i];
        if (!e->active) continue;
        fprintf(stderr,
                "combat attack event: tick=%" PRIu64 " source=%s:%d target=%s:%d "
                "style=%s action=%s:%d:%s hit_delay=%d "
                "src=(%d,%d,%d) dst=(%d,%d,%d)\n",
                e->world_tick,
                combat_actor_kind_name(e->source_kind), e->source_uid,
                combat_actor_kind_name(e->target_kind), e->target_uid,
                combat_style_name(e->style),
                combat_action_kind_name(e->action_kind),
                e->action_key_id, e->action_key_name,
                e->hit_delay,
                e->source_x, e->source_y, e->plane,
                e->target_x, e->target_y, e->plane);
    }
}

static int viewer_visual_has_projectile(const RcCombatVisualDef *visual) {
    return visual && (visual->travel_spotanim_id >= 0 ||
                      visual->launch_spotanim_id >= 0 ||
                      visual->aux_travel_spotanim_id >= 0 ||
                      visual->projectile_model_id >= 0 ||
                      visual->projectile_anim_id >= 0 ||
                      (visual->kind != RC_COMBAT_VISUAL_SPECIAL &&
                       visual->impact_spotanim_id >= 0));
}

static int viewer_visual_is_fixed_tile_impact(
    const RcCombatVisualDef *visual
) {
    return visual && visual->primitive_type ==
           RC_COMBAT_VISUAL_PRIMITIVE_FIXED_TILE_IMPACT;
}

static int viewer_visual_has_projectile_profile(
    const RcCombatVisualDef *visual
) {
    return visual && visual->projectile_start_height >= 0 &&
           visual->projectile_end_height >= 0 &&
           visual->projectile_delay >= 0 &&
           visual->projectile_angle >= 0 &&
           visual->projectile_progress >= 0 &&
           visual->projectile_step_multiplier >= 0;
}

static int viewer_visual_has_alt_projectile_profile(
    const RcCombatVisualDef *visual
) {
    return visual && visual->alt_projectile_start_height >= 0 &&
           visual->alt_projectile_end_height >= 0 &&
           visual->alt_projectile_delay >= 0 &&
           visual->alt_projectile_angle >= 0 &&
           visual->alt_projectile_progress >= 0 &&
           visual->alt_projectile_step_multiplier >= 0;
}

static int viewer_visual_projectile_end_time(
    const RcCombatVisualDef *visual,
    int distance
) {
    if (!viewer_visual_has_projectile_profile(visual)) return -1;
    if (distance < 0) distance = 0;
    return visual->projectile_delay +
           visual->projectile_length_adjustment +
           visual->projectile_step_multiplier * distance;
}

static int viewer_projectile_count(const RcCombatVisualDef *visual) {
    if (!visual || visual->projectile_count < 1)
        return 1;
    return visual->projectile_count > 4 ? 4 : visual->projectile_count;
}

static int viewer_should_show_impact_for_index(
    const RcCombatVisualDef *effect,
    int index,
    int count
) {
    if (!effect || !effect->impact_on_last_only)
        return 1;
    return index == count - 1;
}

static int viewer_launch_spotanim_for_sequence(
    const RcCombatVisualDef *visual,
    const RcCombatVisualDef *effect,
    int sequence_index,
    int sequence_count
) {
    if (!visual || sequence_index != 0) return -1;
    if (effect && sequence_count > 1 && visual->double_launch_spotanim_id >= 0)
        return visual->double_launch_spotanim_id;
    return visual->launch_spotanim_id;
}

static ViewerCombatProjectile *viewer_next_projectile_slot(ViewerState *v) {
    if (!v) return NULL;
    for (int i = 0; i < v->combat_projectile_count; i++) {
        if (!v->combat_projectiles[i].active)
            return &v->combat_projectiles[i];
    }
    if (v->combat_projectile_count < VIEWER_MAX_COMBAT_PROJECTILES)
        return &v->combat_projectiles[v->combat_projectile_count++];
    int oldest = 0;
    for (int i = 1; i < v->combat_projectile_count; i++) {
        if (v->combat_projectiles[i].start_tick <
                v->combat_projectiles[oldest].start_tick)
            oldest = i;
    }
    return &v->combat_projectiles[oldest];
}

static void viewer_tick_combat_projectiles(ViewerState *v) {
    if (!v) return;
    int w = 0;
    for (int i = 0; i < v->combat_projectile_count; i++) {
        ViewerCombatProjectile proj = v->combat_projectiles[i];
        if (!proj.active) continue;
        if (proj.duration_ticks <= 0) proj.duration_ticks = 1;
        proj.age_ticks++;
        int retain_ticks = proj.duration_ticks + proj.impact_duration_ticks;
        if (retain_ticks < proj.duration_ticks)
            retain_ticks = proj.duration_ticks;
        if (proj.age_ticks > retain_ticks)
            continue;
        v->combat_projectiles[w++] = proj;
    }
    v->combat_projectile_count = w;
}

static void viewer_tick_attack_anims(ViewerState *v) {
    if (!v) return;
    if (v->player_action_anim_timer > 0)
        v->player_action_anim_timer--;
    if (v->player_action_anim_timer <= 0)
        v->player_action_anim_id = -1;
    if (v->player_attack_anim_timer > 0)
        v->player_attack_anim_timer--;
    if (v->player_attack_anim_timer <= 0)
        v->player_attack_anim_id = -1;
    if (!v->world) return;
    for (int i = 0; i < v->world->npc_count && i < RC_MAX_NPCS; i++) {
        if (v->npc_render[i].attack_anim_timer > 0)
            v->npc_render[i].attack_anim_timer--;
        if (v->npc_render[i].attack_anim_timer <= 0)
            v->npc_render[i].attack_anim_id = -1;
    }
}

static int viewer_npc_attack_anim_ticks(
    const RcCombatAttackEvent *event
) {
    if (!event || event->source_definition_id != VIEWER_NPC_TZTOK_JAD)
        return 2;
    if (event->style == COMBAT_MAGIC || event->style == COMBAT_RANGED)
        return VIEWER_JAD_WARNING_ANIM_TICKS;
    return VIEWER_JAD_MELEE_ANIM_TICKS;
}

static void viewer_start_player_attack_anim(
    ViewerState *v,
    const RcCombatVisualDef *visual
) {
    if (!v) return;
    v->player_attack_anim_timer = 2;
    v->player_attack_anim_id =
        visual && visual->attack_anim_id >= 0 ? visual->attack_anim_id : -1;
    AnimSequence *seq = v->player_attack_anim_id >= 0
        ? anim_get_sequence(v->anims, (uint16_t)v->player_attack_anim_id) : NULL;
    if (seq) v->player_attack_anim_timer = anim_sequence_game_ticks(seq);
    v->player_attack_anim_suppressed = 0;
    v->player_one_shot_finished = 0;
    v->anim_frame_idx = 0;
    v->anim_frame_timer = 0;
}

// The attack sequence plays for its full length (at least the default
// hold, or `hold_ticks` when longer), so long sequences such as a whole
// mechanic's are not cut short and a form change can hold its last frame.
static void viewer_start_npc_attack_anim(
    ViewerState *v,
    const RcCombatAttackEvent *event,
    const RcCombatVisualDef *visual,
    int hold_ticks
) {
    int idx = viewer_find_npc_index_by_uid(v, event ? event->source_uid : -1);
    if (idx < 0) return;
    int anim_id = visual && visual->attack_anim_id >= 0
                ? visual->attack_anim_id : -1;
    int ticks = viewer_npc_attack_anim_ticks(event);
    if (hold_ticks > ticks) ticks = hold_ticks;
    AnimSequence *seq = anim_id >= 0 && anim_id <= 0xFFFF
        ? anim_get_sequence(v->anims, (uint16_t)anim_id) : NULL;
    int seq_ticks = anim_sequence_game_ticks(seq);
    v->npc_render[idx].attack_anim_timer = seq_ticks > ticks ? seq_ticks : ticks;
    v->npc_render[idx].attack_anim_id = anim_id;
}

static int viewer_npc_size(const ViewerState *v, const RcNpc *npc) {
    const RcNpcDef *def = rc_npc_def_for_npc(v ? v->world : NULL, npc);
    return def && def->size > 0 ? def->size : 1;
}

static int viewer_distance_event_target(
    const RcCombatAttackEvent *event
) {
    if (!event) return 0;
    int dx = event->source_x > event->target_x
           ? event->source_x - event->target_x
           : event->target_x - event->source_x;
    int dy = event->source_y > event->target_y
           ? event->source_y - event->target_y
           : event->target_y - event->source_y;
    return dx > dy ? dx : dy;
}

static int viewer_projectile_launch_height(
    const RcCombatVisualDef *visual,
    RcCombatStyle style
) {
    if (visual && visual->launch_spotanim_height >= 0)
        return visual->launch_spotanim_height;
    return style == COMBAT_MAGIC ? 92 : 96;
}

static int viewer_projectile_impact_height(
    const ViewerCombatProjectile *proj,
    const RcCombatVisualDef *visual,
    RcCombatStyle style
) {
    if (visual && visual->impact_spotanim_height >= 0)
        return visual->impact_spotanim_height;
    if (proj && proj->projectile_end_height >= 0)
        return proj->projectile_end_height;
    return style == COMBAT_MAGIC ? 124 : 0;
}

static void viewer_apply_projectile_profile(
    ViewerCombatProjectile *proj,
    const RcCombatVisualDef *visual,
    int distance,
    int hit_delay
) {
    if (!proj) return;
    proj->hit_delay = hit_delay;
    proj->client_delay = hit_delay > 0 ? hit_delay : 1;
    proj->duration_ticks = proj->client_delay;
    proj->impact_duration_ticks = proj->impact_spotanim_id >= 0 ? 3 : 0;
    proj->projectile_start_height = -1;
    proj->projectile_end_height = -1;
    proj->projectile_start_time = 0;
    proj->projectile_end_time = proj->duration_ticks * 30;
    proj->projectile_angle = -1;
    proj->projectile_progress = -1;
    if (viewer_visual_has_projectile_profile(visual)) {
        proj->projectile_start_height = visual->projectile_start_height;
        proj->projectile_end_height = visual->projectile_end_height;
        proj->projectile_start_time = visual->projectile_delay;
        proj->projectile_end_time =
            viewer_visual_projectile_end_time(visual, distance);
        proj->projectile_angle = visual->projectile_angle;
        proj->projectile_progress = visual->projectile_progress;
        if (proj->projectile_end_time <= proj->projectile_start_time)
            proj->projectile_end_time = proj->projectile_start_time + 1;
        proj->duration_ticks = 1 + proj->projectile_end_time / 30;
        proj->client_delay = proj->duration_ticks;
    }
    if (proj->duration_ticks < 1)
        proj->duration_ticks = 1;
}

static void viewer_apply_projectile_profile_for_index(
    ViewerCombatProjectile *proj,
    const RcCombatVisualDef *base_visual,
    const RcCombatVisualDef *effect_visual,
    int sequence_index,
    int distance,
    int hit_delay
) {
    const RcCombatVisualDef *profile = base_visual;
    RcCombatVisualDef alt;
    if (sequence_index > 0 &&
            viewer_visual_has_alt_projectile_profile(effect_visual)) {
        alt = *effect_visual;
        alt.projectile_start_height =
            effect_visual->alt_projectile_start_height;
        alt.projectile_end_height =
            effect_visual->alt_projectile_end_height;
        alt.projectile_delay = effect_visual->alt_projectile_delay;
        alt.projectile_angle = effect_visual->alt_projectile_angle;
        alt.projectile_length_adjustment =
            effect_visual->alt_projectile_length_adjustment;
        alt.projectile_progress = effect_visual->alt_projectile_progress;
        alt.projectile_step_multiplier =
            effect_visual->alt_projectile_step_multiplier;
        profile = &alt;
    }
    viewer_apply_projectile_profile(proj, profile, distance, hit_delay);
}

// An NPC's projectiles and launch graphics are locked to the NPC, as in the
// client: they start at its centre (its rendered position plus half its
// size) and the row's progress moves the start toward the target, so a large
// NPC fires from the same body point whatever the angle. Tile rows start at
// the target tile.
static void viewer_npc_visual_source(
    ViewerState *v,
    const RcCombatAttackEvent *event,
    const RcCombatVisualDef *visual,
    ViewerCombatProjectile *proj
) {
    if (!v || !event || !proj) return;
    int idx = viewer_find_npc_index_by_uid(v, event->source_uid);
    if (idx < 0 || (visual && (visual->source_attachment ==
                               RC_COMBAT_VISUAL_ATTACH_TARGET_TILE
                               || visual->source_attachment ==
                               RC_COMBAT_VISUAL_ATTACH_FIXED_TILE))) {
        bool at_target = idx >= 0;
        proj->source_x = at_target ? event->target_x : event->source_x;
        proj->source_y = at_target ? event->target_y : event->source_y;
        proj->source_fx = (float)proj->source_x + 0.5f;
        proj->source_fy = (float)proj->source_y + 0.5f;
        return;
    }
    const RcNpc *npc = &v->world->npcs[idx];
    float half = 0.5f * (float)viewer_npc_size(v, npc);
    float x = v->npc_render[idx].initialized ? v->npc_render[idx].render_x
                                             : (float)npc->x;
    float y = v->npc_render[idx].initialized ? v->npc_render[idx].render_y
                                             : (float)npc->y;
    proj->source_fx = x + half;
    proj->source_fy = y + half;
    proj->source_x = (int)floorf(proj->source_fx);
    proj->source_y = (int)floorf(proj->source_fy);
}

static void viewer_spawn_projectile_instance(
    ViewerState *v,
    const RcCombatAttackEvent *event,
    const RcCombatVisualDef *visual,
    const RcCombatVisualDef *profile_visual,
    const RcCombatVisualDef *effect_visual,
    int sequence_index,
    int sequence_count,
    int launch_spotanim_id,
    int travel_spotanim_id,
    int impact_spotanim_id,
    int projectile_model_id,
    int projectile_anim_id
) {
    if (!v || !event ||
            (launch_spotanim_id < 0 && travel_spotanim_id < 0 &&
             impact_spotanim_id < 0 && projectile_model_id < 0 &&
             projectile_anim_id < 0)) {
        return;
    }
    ViewerCombatProjectile *proj = viewer_next_projectile_slot(v);
    if (!proj) return;
    memset(proj, 0, sizeof(*proj));
    proj->active = true;
    proj->source_kind = event->source_kind;
    proj->target_kind = event->target_kind;
    proj->style = event->style;
    proj->primitive_type = visual ? visual->primitive_type
                                  : RC_COMBAT_VISUAL_PRIMITIVE_NONE;
    proj->source_attachment = visual ? visual->source_attachment
                                     : RC_COMBAT_VISUAL_ATTACH_NONE;
    proj->target_attachment = visual ? visual->target_attachment
                                     : RC_COMBAT_VISUAL_ATTACH_NONE;
    proj->launch_attachment = visual ? visual->launch_attachment
                                     : RC_COMBAT_VISUAL_ATTACH_NONE;
    proj->impact_attachment = visual ? visual->impact_attachment
                                     : RC_COMBAT_VISUAL_ATTACH_NONE;
    proj->source_uid = event->source_uid;
    proj->target_uid = event->target_uid;
    proj->source_x = event->source_x;
    proj->source_y = event->source_y;
    proj->source_fx = (float)event->source_x + 0.5f;
    proj->source_fy = (float)event->source_y + 0.5f;
    if (event->source_kind == RC_COMBAT_ACTOR_NPC)
        viewer_npc_visual_source(v, event, visual, proj);
    proj->target_x = event->target_x;
    proj->target_y = event->target_y;
    proj->plane = event->plane;
    proj->weapon_item_id = event->weapon_item_id;
    proj->ammo_item_id = event->ammo_item_id;
    proj->spell_idx = event->spell_idx;
    proj->attack_anim_id = visual ? visual->attack_anim_id : -1;
    proj->launch_spotanim_id = launch_spotanim_id;
    proj->travel_spotanim_id = travel_spotanim_id;
    proj->impact_spotanim_id = impact_spotanim_id;
    proj->launch_spotanim_height =
        viewer_projectile_launch_height(visual, (RcCombatStyle)event->style);
    proj->projectile_model_id = projectile_model_id;
    proj->projectile_anim_id = projectile_anim_id;
    proj->sequence_index = sequence_index;
    proj->sequence_count = sequence_count;
    viewer_apply_projectile_profile_for_index(
        proj, profile_visual ? profile_visual : visual, effect_visual,
        sequence_index, viewer_distance_event_target(event), event->hit_delay);
    if (viewer_visual_is_fixed_tile_impact(visual)) {
        int impact_time = viewer_visual_projectile_end_time(
            visual, viewer_distance_event_target(event));
        if (impact_time <= 0)
            impact_time = (event->hit_delay > 0 ? event->hit_delay : 1) * 30;
        proj->launch_spotanim_id = -1;
        proj->travel_spotanim_id = -1;
        proj->impact_spotanim_id = impact_spotanim_id;
        proj->projectile_model_id = -1;
        proj->projectile_anim_id = -1;
        proj->projectile_start_height =
            visual && visual->projectile_start_height >= 0
            ? visual->projectile_start_height : proj->projectile_start_height;
        proj->projectile_end_height =
            visual && visual->projectile_end_height >= 0
            ? visual->projectile_end_height : proj->projectile_end_height;
        proj->projectile_start_time = 0;
        proj->projectile_end_time = impact_time;
        proj->projectile_angle = 0;
        proj->projectile_progress = 0;
        proj->client_delay = event->hit_delay > 0 ? event->hit_delay
                                                  : proj->client_delay;
        proj->duration_ticks = proj->client_delay > 0 ? proj->client_delay : 1;
        int min_duration = 1 + impact_time / 30;
        if (proj->duration_ticks < min_duration)
            proj->duration_ticks = min_duration;
        proj->impact_duration_ticks = 3;
    }
    // Tile-aimed encounter projectiles land on the core's impact tick.
    if (event->action_kind == RC_COMBAT_ACTION_ENCOUNTER
            && event->target_kind == RC_COMBAT_ACTOR_NONE
            && event->hit_delay > 0
            && !viewer_visual_is_fixed_tile_impact(visual)) {
        proj->duration_ticks = proj->client_delay = event->hit_delay;
        proj->projectile_end_time = event->hit_delay * 30;
        if (proj->projectile_start_time >= proj->projectile_end_time)
            proj->projectile_start_time = proj->projectile_end_time - 1;
    }
    proj->impact_spotanim_height = viewer_projectile_impact_height(
        proj, visual, (RcCombatStyle)event->style);
    bool accurate = event->accurate;
    if (event->source_kind == RC_COMBAT_ACTOR_PLAYER && sequence_index < 4) {
        int delay = event->hit_delays[sequence_index];
        proj->hit_delay = delay;
        proj->duration_ticks = proj->client_delay = delay > 0 ? delay : 1;
        proj->projectile_end_time = proj->duration_ticks * 30;
        if (proj->projectile_start_time >= proj->projectile_end_time)
            proj->projectile_start_time = proj->projectile_end_time - 1;
        accurate = event->hit_accurate[sequence_index];
    }
    if (event->source_kind == RC_COMBAT_ACTOR_PLAYER &&
            event->style == COMBAT_MAGIC && !accurate) {
        proj->impact_spotanim_id = 85;
        proj->impact_spotanim_height = 124;
    }
    proj->impact_spotanim_delay =
        visual ? visual->impact_spotanim_delay : -1;
    proj->impact_spotanim_rotation =
        visual ? visual->impact_spotanim_rotation : -1;
    proj->start_tick = event->world_tick;
    proj->age_ticks = 0;
}

static void viewer_spawn_player_attack_projectiles(
    ViewerState *v,
    const RcCombatAttackEvent *event,
    const RcCombatVisualDef *visual,
    const RcCombatVisualDef *effect_visual,
    const RcCombatVisualDef *profile_visual
) {
    if (!v || !event) return;
    const RcCombatVisualDef *event_visual = effect_visual ? effect_visual
                                                          : visual;
    if (!viewer_visual_has_projectile(visual) &&
            !viewer_visual_has_projectile(event_visual)) {
        return;
    }
    int count = viewer_projectile_count(event_visual);
    if (event->hit_count > 0 && count > event->hit_count)
        count = event->hit_count;
    for (int i = 0; i < count; i++) {
        int show_impact = viewer_should_show_impact_for_index(
            event_visual, i, count);
        if (event_visual && event_visual->aux_travel_spotanim_id >= 0) {
            viewer_spawn_projectile_instance(
                v, event, event_visual, profile_visual, event_visual,
                i, count, i == 0 ? event_visual->launch_spotanim_id : -1,
                event_visual->aux_travel_spotanim_id,
                show_impact ? event_visual->aux_impact_spotanim_id : -1,
                event_visual->aux_projectile_model_id,
                event_visual->aux_projectile_anim_id);
        }
        int launch_id = -1;
        if (event_visual && i == 0 && event_visual->launch_spotanim_id >= 0 &&
                event_visual->aux_travel_spotanim_id < 0) {
            launch_id = event_visual->launch_spotanim_id;
        } else if (visual && i == 0 &&
                (!event_visual || event_visual->launch_spotanim_id < 0)) {
            launch_id = viewer_launch_spotanim_for_sequence(
                visual, event_visual, i, count);
        } else if (visual && count == 1) {
            launch_id = visual->launch_spotanim_id;
        }
        int travel_id = visual ? visual->travel_spotanim_id : -1;
        int impact_id = show_impact && visual ? visual->impact_spotanim_id : -1;
        int model_id = visual ? visual->projectile_model_id : -1;
        int anim_id = visual ? visual->projectile_anim_id : -1;
        if (travel_id < 0 && impact_id < 0 && model_id < 0 && anim_id < 0 &&
                launch_id < 0) {
            continue;
        }
        viewer_spawn_projectile_instance(
            v, event, visual ? visual : event_visual, profile_visual,
            event_visual, i, count, launch_id, travel_id, impact_id, model_id,
            anim_id);
    }
}

static void viewer_spawn_npc_attack_projectiles(
    ViewerState *v,
    const RcCombatAttackEvent *event,
    const RcCombatVisualDef *visual
) {
    if (!v || !event || !viewer_visual_has_projectile(visual)) return;
    int count = viewer_projectile_count(visual);
    for (int i = 0; i < count; i++) {
        int impact_id = viewer_should_show_impact_for_index(visual, i, count)
                      ? visual->impact_spotanim_id : -1;
        viewer_spawn_projectile_instance(
            v, event, visual, visual, visual, i, count,
            i == 0 ? visual->launch_spotanim_id : -1,
            visual->travel_spotanim_id, impact_id,
            visual->projectile_model_id, visual->projectile_anim_id);
    }
}

static void viewer_handle_player_attack_event(
    ViewerState *v,
    const RcCombatAttackEvent *event
) {
    if (!v || !event) return;
    RcPlayerAttackVisuals visuals = rc_combat_visual_resolve_player(event);
    viewer_start_player_attack_anim(v, visuals.animation);
    viewer_spawn_player_attack_projectiles(
        v, event, visuals.projectile, visuals.effect, visuals.timing);
}

static uint32_t viewer_npc_action_hash(int npc_id, const char *key) {
    uint32_t h = 2166136261u ^ (uint32_t)npc_id;
    for (const char *c = key; c && *c; c++) h = (h ^ (uint8_t)*c) * 16777619u;
    return h ? h : 1u;
}

static void viewer_report_missing_npc_visual(ViewerState *v,
                                             const RcCombatAttackEvent *event) {
    uint32_t h = viewer_npc_action_hash(event->source_definition_id,
                                        event->action_key_name);
    for (int i = 0; i < v->missing_npc_visual_count; i++) {
        if (v->missing_npc_visuals[i] == h) return;
    }
    if (v->missing_npc_visual_count < (int)(sizeof(v->missing_npc_visuals)
                                            / sizeof(v->missing_npc_visuals[0])))
        v->missing_npc_visuals[v->missing_npc_visual_count++] = h;
    fprintf(stderr, "combat visuals: no row for npc %d action '%s' "
            "(content/combat_visuals/visuals.tsv)\n",
            event->source_definition_id, event->action_key_name);
}

// Encounter actions are drawn from the row for their attack or mechanic
// part. A missing attack row falls back to the NPC's style row; a missing
// part row draws nothing. Either gap is reported once.
static void viewer_handle_npc_attack_event(ViewerState *v,
                                           const RcCombatAttackEvent *event) {
    const RcCombatVisualDef *visual = NULL;
    if (event->action_kind == RC_COMBAT_ACTION_ENCOUNTER) {
        visual = rc_combat_visual_for_npc_action(event->source_definition_id,
                                                 event->action_key_name);
        if (!visual) {
            viewer_report_missing_npc_visual(v, event);
            if (strchr(event->action_key_name, ':')) return;
        } else {
            // Parts without a sequence leave the boss's running one alone;
            // a leaving form holds its sequence for the whole transition.
            int hold = strcmp(event->action_key_name,
                              RC_ENC_FORM_LEAVE_KEY) == 0 ? event->hit_delay : 0;
            if (visual->attack_anim_id >= 0)
                viewer_start_npc_attack_anim(v, event, visual, hold);
            viewer_spawn_npc_attack_projectiles(v, event, visual);
            return;
        }
    }
    visual = rc_combat_visual_for_npc(event->source_definition_id,
                                      (RcCombatStyle)event->style);
    viewer_start_npc_attack_anim(v, event, visual, 0);
    viewer_spawn_npc_attack_projectiles(v, event, visual);
}

static void viewer_capture_combat_attack_events(ViewerState *v) {
    if (!v || !v->world) return;
    int count = 0;
    const RcCombatAttackEvent *events =
        rc_combat_attack_events(v->world, &count);
    for (int i = 0; events && i < count; i++) {
        const RcCombatAttackEvent *event = &events[i];
        if (!event->active) continue;
        if (event->source_kind == RC_COMBAT_ACTOR_PLAYER)
            viewer_handle_player_attack_event(v, event);
        else if (event->source_kind == RC_COMBAT_ACTOR_NPC)
            viewer_handle_npc_attack_event(v, event);
    }
}

static Color projectile_color(const ViewerCombatProjectile *proj) {
    if (!proj) return WHITE;
    if (proj->style == COMBAT_MAGIC) return (Color){255, 104, 36, 235};
    if (proj->style == COMBAT_RANGED) return (Color){218, 178, 92, 235};
    return (Color){220, 220, 220, 235};
}

static float projectile_model_scale(const ViewerCombatProjectile *proj) {
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

static int projectile_model_id_add(uint32_t *ids, int *count, int cap,
                                   uint32_t id) {
    if (!ids || !count || cap <= 0)
        return 0;
    for (int i = 0; i < *count; i++) {
        if (ids[i] == id)
            return 1;
    }
    if (*count >= cap)
        return 0;
    ids[(*count)++] = id;
    return 1;
}

static int projectile_model_id_list_contains(const uint32_t *ids, int count,
                                             uint32_t id) {
    for (int i = 0; ids && i < count; i++) {
        if (ids[i] == id)
            return 1;
    }
    return 0;
}

static int projectile_model_requests_same(ViewerState *v,
                                          const uint32_t *ids,
                                          int count) {
    if (!v || count != v->projectile_model_request_id_count)
        return 0;
    for (int i = 0; i < count; i++) {
        if (!projectile_model_id_list_contains(v->projectile_model_request_ids,
                                               count, ids[i])) {
            return 0;
        }
    }
    return 1;
}

static void projectile_model_remember_request(ViewerState *v,
                                              const uint32_t *ids,
                                              int count) {
    if (!v)
        return;
    if (count > VIEWER_PROJECTILE_MODEL_FILTER_MAX)
        count = VIEWER_PROJECTILE_MODEL_FILTER_MAX;
    for (int i = 0; i < count; i++)
        v->projectile_model_request_ids[i] = ids[i];
    v->projectile_model_request_id_count = count;
}

static int projectile_model_set_covers_ids(ModelSet *models,
                                           const uint32_t *ids,
                                           int count) {
    if (!models || !models->loaded)
        return 0;
    for (int i = 0; i < count; i++) {
        ModelEntry *entry = model_find(models, ids[i]);
        if (!entry || !entry->loaded)
            return 0;
    }
    return 1;
}

static const SpotAnimDef *projectile_travel_spotanim(ViewerState *v,
                                                     const ViewerCombatProjectile *proj) {
    if (!v || !proj || proj->travel_spotanim_id < 0)
        return NULL;
    return spotanim_find(v->spotanims, proj->travel_spotanim_id);
}

static const SpotAnimDef *projectile_launch_spotanim(ViewerState *v,
                                                     const ViewerCombatProjectile *proj) {
    if (!v || !proj || proj->launch_spotanim_id < 0)
        return NULL;
    return spotanim_find(v->spotanims, proj->launch_spotanim_id);
}

static const SpotAnimDef *projectile_impact_spotanim(ViewerState *v,
                                                     const ViewerCombatProjectile *proj) {
    if (!v || !proj || proj->impact_spotanim_id < 0)
        return NULL;
    return spotanim_find(v->spotanims, proj->impact_spotanim_id);
}

static void projectile_collect_spotanim_model_ids(
    ViewerState *v,
    int spotanim_id,
    uint32_t *ids,
    int *count,
    int *truncated
) {
    if (!v || spotanim_id < 0)
        return;
    const SpotAnimDef *spot = spotanim_find(v->spotanims, spotanim_id);
    if (!spot)
        return;
    if (spot->id <= 0x0FFFFFFFu
            && !projectile_model_id_add(ids, count,
                                        VIEWER_PROJECTILE_MODEL_FILTER_MAX,
                                        MODEL_ID_SPOTANIM_BASE + spot->id)) {
        if (truncated) *truncated = 1;
    }
    if (spot->model_id >= 0
            && !projectile_model_id_add(ids, count,
                                        VIEWER_PROJECTILE_MODEL_FILTER_MAX,
                                        (uint32_t)spot->model_id)) {
        if (truncated) *truncated = 1;
    }
}

static void ensure_projectile_models_for_active_projectiles(
    ViewerState *v,
    int scene_plane
) {
    if (!v)
        return;
    uint32_t ids[VIEWER_PROJECTILE_MODEL_FILTER_MAX];
    int id_count = 0;
    int truncated = 0;
    scene_plane = clamp_plane(scene_plane);

    for (int i = 0; i < v->combat_projectile_count; i++) {
        const ViewerCombatProjectile *proj = &v->combat_projectiles[i];
        if (!proj->active || proj->plane != scene_plane)
            continue;
        if (proj->projectile_model_id >= 0
                && !projectile_model_id_add(ids, &id_count,
                                            VIEWER_PROJECTILE_MODEL_FILTER_MAX,
                                            (uint32_t)proj->projectile_model_id)) {
            truncated = 1;
        }
        projectile_collect_spotanim_model_ids(
            v, proj->launch_spotanim_id, ids, &id_count, &truncated);
        projectile_collect_spotanim_model_ids(
            v, proj->travel_spotanim_id, ids, &id_count, &truncated);
        projectile_collect_spotanim_model_ids(
            v, proj->impact_spotanim_id, ids, &id_count, &truncated);
    }

    if (id_count <= 0)
        return;
    if (projectile_model_set_covers_ids(v->projectile_models, ids, id_count))
        return;
    if (projectile_model_requests_same(v, ids, id_count))
        return;

    free_projectile_anim_states(v);
    models_free(v->projectile_models);
    v->projectile_models = models_load_filtered(
        env_path("RUNEC_PROJECTILE_MODELS", "data/models/projectiles.models"),
        ids, id_count);
    if (v->projectile_models && v->projectile_effect_shader_loaded)
        models_set_shader(v->projectile_models, v->projectile_effect_shader);
    create_projectile_anim_states(v);
    projectile_model_remember_request(v, ids, id_count);
    fprintf(stderr,
            "projectile models: lazy requested=%d%s active visual ids\n",
            id_count, truncated ? " (truncated)" : "");
}

static int projectile_effect_model_id(const ViewerCombatProjectile *proj,
                                      const SpotAnimDef *spot) {
    if (proj && proj->source_kind == RC_COMBAT_ACTOR_NPC &&
            spot && spot->model_id >= 0)
        return spot->model_id;
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
                                                 const ViewerCombatProjectile *proj,
                                                 const SpotAnimDef *spot) {
    return projectile_spotanim_model_entry(
        v, spot, projectile_effect_model_id(proj, spot));
}

static int projectile_effect_anim_id(const ViewerCombatProjectile *proj,
                                     const SpotAnimDef *spot) {
    if (proj && proj->source_kind == RC_COMBAT_ACTOR_NPC &&
            spot && spot->animation_id >= 0)
        return spot->animation_id;
    if (proj && proj->projectile_anim_id >= 0)
        return proj->projectile_anim_id;
    if (spot && spot->animation_id >= 0)
        return spot->animation_id;
    return -1;
}

static Vector3 projectile_spotanim_scale(const ViewerCombatProjectile *proj,
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

static float projectile_yaw_degrees(float sx, float sz, float tx, float tz) {
    return atan2f(tx - sx, tz - sz) * (180.0f / 3.14159265f);
}

static int projectile_has_travel_visual(const ViewerCombatProjectile *proj) {
    return proj && (proj->travel_spotanim_id >= 0 ||
                    proj->projectile_model_id >= 0 ||
                    proj->projectile_anim_id >= 0);
}

static void draw_oriented_projectile_model(Model model, Vector3 pos,
                                           float yaw, float pitch,
                                           Vector3 scale, Color tint) {
    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);
    rlRotatef(yaw, 0.0f, 1.0f, 0.0f);
    rlRotatef(pitch, 1.0f, 0.0f, 0.0f);
    rlScalef(scale.x, scale.y, scale.z);
    DrawModel(model, (Vector3){0.0f, 0.0f, 0.0f}, 1.0f, tint);
    rlPopMatrix();
}

static int projectile_target_point(ViewerState *v,
                                   const ViewerCombatProjectile *proj,
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
    // A row aimed at a tile lands where it was aimed; anything aimed at an
    // actor ends on that actor's current position.
    bool on_tile = proj->target_attachment == RC_COMBAT_VISUAL_ATTACH_TARGET_TILE
                || proj->target_attachment == RC_COMBAT_VISUAL_ATTACH_FIXED_TILE
                || proj->target_attachment == RC_COMBAT_VISUAL_ATTACH_GROUND_TILE;

    if (!on_tile && proj->target_kind == RC_COMBAT_ACTOR_NPC) {
        for (int i = 0; i < v->world->npc_count; i++) {
            const RcNpc *npc = &v->world->npcs[i];
            if (!npc->active || npc->uid != proj->target_uid)
                continue;
            if (npc->plane != scene_plane)
                return 0;
            int size = viewer_npc_size(v, npc);
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
    } else if (!on_tile && proj->target_kind == RC_COMBAT_ACTOR_PLAYER) {
        int player_x = 0;
        int player_y = 0;
        int player_plane = 0;
        if (!viewer_presented_player_tile(
                v, &player_x, &player_y, &player_plane)
                || player_plane != scene_plane) {
            return 0;
        }
        target_plane = player_plane;
        int held = v->mapsquare_load.player_transition.active;
        wx = held ? (float)player_x
            : v->prev_player_x
                + ((float)player_x - v->prev_player_x) * v->tick_frac;
        wy = held ? (float)player_y
            : v->prev_player_y
                + ((float)player_y - v->prev_player_y) * v->tick_frac;
        tile_x = player_x;
        tile_y = player_y;
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

// The client's projectile step: before each move it aims at the target's
// current position over the remaining flight time (so a homing projectile
// follows a moving target smoothly), sets its climb from the launch slope
// once, and solves the vertical acceleration that lands it at the end
// height. The first visible step starts `progress` toward the target.
static Vector3 combat_projectile_position(ViewerState *v,
                                          ViewerCombatProjectile *proj,
                                          int scene_plane,
                                          float *out_angle,
                                          float *out_pitch,
                                          int *out_visible) {
    *out_visible = 0;
    float sx = proj->source_fx - (float)g_world_origin_x;
    float sz = -(proj->source_fy - (float)g_world_origin_y);
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
    float end_h = proj->projectile_end_height >= 0
                ? (float)proj->projectile_end_height / 128.0f : 1.05f;
    float ty = target_ground + end_h;

    float start_time = proj->projectile_start_time >= 0
                     ? (float)proj->projectile_start_time : 0.0f;
    float end_time = proj->projectile_end_time > proj->projectile_start_time
                   ? (float)proj->projectile_end_time
                   : (float)(proj->duration_ticks > 0
                             ? proj->duration_ticks * 30 : 30);
    float client_time = ((float)proj->age_ticks + v->tick_frac) * 30.0f;
    float sy = ground_yf_plane(v, scene_plane, proj->source_fx,
                               proj->source_fy) + start_h;
    if (client_time < start_time || client_time > end_time + 1.0f) {
        if (client_time < start_time) proj->flying = false;
        return (Vector3){sx, sy, sz};
    }

    // World coordinates, so a scene re-centred mid-flight does not move it.
    float target_x = tx + (float)g_world_origin_x;
    float target_y = (float)g_world_origin_y - tz;
    if (!proj->flying) {
        float dx = target_x - proj->source_fx, dy = target_y - proj->source_fy;
        float horizontal = sqrtf(dx * dx + dy * dy);
        float start_pos = proj->projectile_progress >= 0
                        ? (float)proj->projectile_progress / 128.0f : 0.0f;
        proj->flight_x = proj->source_fx;
        proj->flight_y = proj->source_fy;
        if (horizontal > 1e-5f) {
            proj->flight_x += dx * start_pos / horizontal;
            proj->flight_y += dy * start_pos / horizontal;
        }
        proj->flight_h = sy;
        proj->flight_time = start_time;
    }
    float remaining = end_time + 1.0f - proj->flight_time;
    if (remaining < 1.0f) remaining = 1.0f;
    float vx = (target_x - proj->flight_x) / remaining;
    float vy = (target_y - proj->flight_y) / remaining;
    float speed = sqrtf(vx * vx + vy * vy);
    if (!proj->flying) {
        float slope = proj->projectile_angle >= 0
                    ? (float)proj->projectile_angle : 15.0f;
        proj->flight_vh = speed * tanf(slope * (3.14159265f / 128.0f));
        proj->flying = true;
    }
    float accel = 2.0f * (ty - proj->flight_h - proj->flight_vh * remaining)
                / (remaining * remaining);
    float delta = client_time - proj->flight_time;
    if (delta > 0.0f) {
        proj->flight_x += vx * delta;
        proj->flight_y += vy * delta;
        proj->flight_h += proj->flight_vh * delta + 0.5f * accel * delta * delta;
        proj->flight_vh += accel * delta;
        proj->flight_time = client_time;
    }

    if (out_angle) {
        *out_angle = speed > 1e-6f
                   ? atan2f(vx, -vy) * (180.0f / 3.14159265f)
                   : projectile_yaw_degrees(sx, sz, tx, tz);
    }
    if (out_pitch) {
        *out_pitch = speed > 1e-6f
                   ? -atan2f(proj->flight_vh, speed) * (180.0f / 3.14159265f)
                   : 0.0f;
    }
    *out_visible = 1;
    return (Vector3){proj->flight_x - (float)g_world_origin_x, proj->flight_h,
                     -(proj->flight_y - (float)g_world_origin_y)};
}

static int combat_projectile_impact_position(ViewerState *v,
                                             const ViewerCombatProjectile *proj,
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
    float end_h = proj->impact_spotanim_height >= 0
                ? (float)proj->impact_spotanim_height / 128.0f
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
                                  const ViewerCombatProjectile *proj,
                                  int scene_plane,
                                  float client_time,
                                  float impact_start) {
    if (proj->impact_spotanim_delay >= 0)
        impact_start = (float)proj->impact_spotanim_delay;
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
        float angle = proj->impact_spotanim_rotation >= 0
                    ? (float)proj->impact_spotanim_rotation
                    : (spot ? (float)spot->rotation : 0.0f);
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
                                  const ViewerCombatProjectile *proj,
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
        proj->source_fx - (float)g_world_origin_x,
        ground_yf_plane(v, scene_plane, proj->source_fx, proj->source_fy)
            + height,
        -(proj->source_fy - (float)g_world_origin_y),
    };
    ModelEntry *entry = projectile_spotanim_model_entry(v, spot, -1);
    if (entry) {
        float angle = spot ? (float)spot->rotation : 0.0f;
        float tx = 0.0f;
        float tz = 0.0f;
        float target_ground = 0.0f;
        if (projectile_target_point(v, proj, scene_plane, &tx, &tz,
                                    &target_ground, NULL, NULL)) {
            angle += projectile_yaw_degrees(pos.x, pos.z, tx, tz);
        }
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
    int count = v ? v->combat_projectile_count : 0;
    ViewerCombatProjectile *projectiles = v ? v->combat_projectiles : NULL;
    if (!projectiles || count <= 0) return;
    int scene_plane = viewer_scene_plane(v);
    ensure_projectile_models_for_active_projectiles(v, scene_plane);
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    for (int i = 0; i < count; i++) {
        ViewerCombatProjectile *proj = &projectiles[i];
        if (!proj->active || proj->plane != scene_plane)
            continue;
        float end_time = proj->projectile_end_time > proj->projectile_start_time
                       ? (float)proj->projectile_end_time
                       : (float)(proj->duration_ticks > 0
                                 ? proj->duration_ticks * 30 : 30);
        float client_time = ((float)proj->age_ticks + v->tick_frac) * 30.0f;
        draw_projectile_launch(v, proj, scene_plane, client_time);
        if (!projectile_has_travel_visual(proj)) {
            draw_projectile_impact(v, proj, scene_plane, client_time,
                                   end_time);
            continue;
        }
        float angle = 0.0f;
        float pitch = 0.0f;
        int visible = 0;
        Vector3 pos = combat_projectile_position(v, proj, scene_plane,
                                                 &angle, &pitch, &visible);
        if (!visible) {
            draw_projectile_impact(v, proj, scene_plane, client_time,
                                   end_time);
            continue;
        }
        float sx = proj->source_fx - (float)g_world_origin_x;
        float sz = -(proj->source_fy - (float)g_world_origin_y);
        float sy = ground_yf_plane(v, scene_plane, proj->source_fx,
                                   proj->source_fy) +
                   (proj->projectile_start_height >= 0
                    ? (float)proj->projectile_start_height / 128.0f : 1.45f);
        Vector3 start = {sx, sy, sz};
        Color c = projectile_color(proj);
        const SpotAnimDef *spot = projectile_travel_spotanim(v, proj);
        int anim_id = projectile_effect_anim_id(proj, spot);
        ModelEntry *entry = projectile_effect_model_entry(v, proj, spot);
        if (entry) {
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
            float model_pitch = spot ? 0.0f : pitch;
            draw_oriented_projectile_model(entry->model, pos, angle, model_pitch,
                                           scale, WHITE);
            continue;
        }
        float radius = proj->style == COMBAT_MAGIC ? 0.16f : 0.07f;
        DrawSphere(pos, radius, c);
        DrawLine3D(start, pos, (Color){c.r, c.g, c.b, 80});
    }
    rlEnableDepthMask();
    EndBlendMode();
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

static float npc_core_facing_angle(const RcNpc *n, int size, float fallback) {
    if (!n) return fallback;
    if (n->facing_entity >= 0) {
        return runec_npc_target_angle(n->x, n->y, size, n->facing_x,
                                      n->facing_y, fallback);
    }
    if (n->facing_x >= 0 || n->facing_y >= 0) {
        return face_angle_between_tiles(n->x, n->y, n->facing_x,
                                        n->facing_y, fallback);
    }
    return fallback;
}

static int player_base_anim_id(ViewerState *v) {
    const RcPlayer *p = &v->world->player;
    const RuneCItemRenderRecord *weapon = equipped_weapon_render_record(v, p);
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

enum {
    PLAYER_ONE_SHOT_NONE = 0,
    PLAYER_ONE_SHOT_ACTION = 1,
    PLAYER_ONE_SHOT_ATTACK = 2
};

static int player_target_anim_id(ViewerState *v, int *one_shot_kind) {
    const RcPlayer *p = &v->world->player;
    if (one_shot_kind) *one_shot_kind = PLAYER_ONE_SHOT_NONE;
    if (v->player_action_anim_timer > 0 && v->player_action_anim_id > 0)
    {
        if (one_shot_kind) *one_shot_kind = PLAYER_ONE_SHOT_ACTION;
        return player_anim_or_fallback(v, (uint32_t)v->player_action_anim_id,
                                       ANIM_IDLE);
    }
    if (v->player_attack_anim_timer > 0 && !v->player_attack_anim_suppressed) {
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
        if (one_shot_kind) *one_shot_kind = PLAYER_ONE_SHOT_ATTACK;
        int fallback = player_anim_or_fallback(v, attack_anim, ANIM_IDLE);
        return v->player_attack_anim_id > 0
             ? player_anim_or_fallback(v, (uint32_t)v->player_attack_anim_id,
                                       fallback)
             : fallback;
    }
    return player_base_anim_id(v);
}

static void snap_npc_render_state(ViewerState *v, int idx, const RcNpc *npc) {
    v->npc_render[idx].initialized = 1;
    v->npc_render[idx].uid = npc->uid;
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
        fprintf(stderr, "npc movement: presentation queue full for uid=%d\n",
                v->npc_render[idx].uid);
        return 0;
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

    v->npc_render[idx].move_anim_timer = 1.0f / TPS;
    return 1;
}

static void update_npc_render_motion(ViewerState *v, float dt) {
    if (!v || !v->world) return;
    float step = dt;
    if (step < 0.0f) step = 0.0f;
    float max_axis_delta = step * TPS;
    for (int i = 0; i < v->world->npc_count; i++) {
        const RcNpc *npc = &v->world->npcs[i];
        RcNpcLifePhase life = rc_npc_life_phase(npc);
        if (life == RC_NPC_LIFE_REMOVED || life == RC_NPC_LIFE_HIDDEN) {
            v->npc_render[i].initialized = 0;
            v->npc_render[i].moving = 0;
            v->npc_render[i].move_anim_timer = 0.0f;
            memset(&v->npc_overlays[i], 0, sizeof(v->npc_overlays[i]));
            continue;
        }

        if (!v->npc_render[i].initialized
                || v->npc_render[i].uid != npc->uid) {
            memset(&v->npc_overlays[i], 0, sizeof(v->npc_overlays[i]));
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
        float remaining = max_axis_delta;
        while (v->npc_render[i].waypoint_count > 0 && remaining > 0.0f) {
            float target_x = v->npc_render[i].waypoint_x[0];
            float target_y = v->npc_render[i].waypoint_y[0];
            float dx = target_x - v->npc_render[i].render_x;
            float dy = target_y - v->npc_render[i].render_y;
            if (fabsf(dx) > 0.001f || fabsf(dy) > 0.001f) {
                v->npc_render[i].last_dx = (dx > 0.001f) - (dx < -0.001f);
                v->npc_render[i].last_dy = (dy > 0.001f) - (dy < -0.001f);
            }
            if (runec_actor_advance_tile(&v->npc_render[i].render_x,
                    &v->npc_render[i].render_y, target_x, target_y, &remaining)) {
                npc_render_pop_waypoint(v, i);
                v->npc_render[i].moving =
                    v->npc_render[i].waypoint_count > 0;
                moved_this_frame = 1;
            } else {
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
    Color pixels[RUNEC_MINIMAP_DISPLAY_SIZE * RUNEC_MINIMAP_DISPLAY_SIZE];
    int presented_x = p->x;
    int presented_y = p->y;
    int presented_plane = p->plane;
    int held = viewer_streaming_player_transition_source(
        &v->mapsquare_load.player_transition,
        &presented_x, &presented_y, &presented_plane);
    float player_x = (held ? (float)presented_x
        : v->prev_player_x
            + ((float)p->x - v->prev_player_x) * v->tick_frac) + 0.5f;
    float player_y = (held ? (float)presented_y
        : v->prev_player_y
            + ((float)p->y - v->prev_player_y) * v->tick_frac) + 0.5f;
    float sine = sinf(v->cam_yaw);
    float cosine = cosf(v->cam_yaw);

    RuneCMinimapChunk sources[VIEWER_STREAMING_CHUNK_CAPACITY];
    int source_count = 0;
    for (int i = 0; i < v->mapsquare_chunk_count; i++) {
        const ViewerMapsquareChunk *chunk = &v->mapsquare_chunks[i];
        if (chunk->plane != scene_plane || !chunk->minimap_pixels)
            continue;
        sources[source_count++] = (RuneCMinimapChunk){
            chunk->region_x,
            chunk->region_y,
            chunk->plane,
            chunk->minimap_pixels,
            chunk->minimap_width,
            chunk->minimap_height,
        };
    }

    if (source_count > 0) {
        RuneCMinimapScene scene = {sources, source_count, scene_plane};
        runec_minimap_render(&scene, player_x, player_y, v->cam_yaw, pixels);
    }
    for (int y = 0; y < RUNEC_MINIMAP_DISPLAY_SIZE; y++) {
        for (int x = 0; x < RUNEC_MINIMAP_DISPLAY_SIZE; x++) {
            int index = x + y * RUNEC_MINIMAP_DISPLAY_SIZE;
            if (source_count > 0 && pixels[index].a != 0)
                continue;
            float sx = (float)x - RUNEC_MINIMAP_DISPLAY_CENTER;
            float sy = (float)y - RUNEC_MINIMAP_DISPLAY_CENTER;
            if (sx * sx + sy * sy > RUNEC_MINIMAP_DISPLAY_RADIUS
                    * RUNEC_MINIMAP_DISPLAY_RADIUS) {
                pixels[index] = BLANK;
                continue;
            }
            float dx = (sx * cosine + sy * sine)
                     / RUNEC_MINIMAP_DISPLAY_PIXELS_PER_TILE;
            float dy = (sx * sine - sy * cosine)
                     / RUNEC_MINIMAP_DISPLAY_PIXELS_PER_TILE;
            pixels[index] = minimap_tile_color(
                v, (int)floorf(player_x + dx),
                (int)floorf(player_y + dy));
        }
    }
    runec_ui_update_minimap(&v->ui, pixels, RUNEC_MINIMAP_DISPLAY_SIZE,
                            RUNEC_MINIMAP_DISPLAY_SIZE);
    runec_ui_set_minimap_rotation(&v->ui, v->cam_yaw);

    runec_ui_clear_minimap(&v->ui);
    runec_ui_add_minimap_dot(&v->ui, 0, 0, RUNEC_UI_MINIMAP_DOT_PLAYER);

    int destination_active = 0;
    int destination_x = 0;
    int destination_y = 0;
    if (v->input_feedback.pending_destination_active
            && v->input_feedback.pending_destination_plane == p->plane) {
        destination_active = 1;
        destination_x = v->input_feedback.pending_destination_x;
        destination_y = v->input_feedback.pending_destination_y;
    } else if (p->route_idx < p->route_len && p->route_len > 0) {
        destination_active = 1;
        destination_x = p->route_x[p->route_len - 1];
        destination_y = p->route_y[p->route_len - 1];
    }
    if (destination_active) {
        Vector2 offset = runec_minimap_rotate_offset(
            (float)destination_x + 0.5f - player_x,
            (float)destination_y + 0.5f - player_y, v->cam_yaw);
        runec_ui_add_minimap_dot(&v->ui, offset.x, offset.y,
                                 RUNEC_UI_MINIMAP_DOT_DESTINATION);
    }

    for (int i = 0; i < v->world->npc_count; i++) {
        const RcNpc *npc = &v->world->npcs[i];
        RcNpcLifePhase life = rc_npc_life_phase(npc);
        if (life == RC_NPC_LIFE_REMOVED || life == RC_NPC_LIFE_HIDDEN
                || npc->plane != scene_plane)
            continue;
        float npc_x = v->npc_render[i].initialized
                    ? v->npc_render[i].render_x : (float)npc->x;
        float npc_y = v->npc_render[i].initialized
                    ? v->npc_render[i].render_y : (float)npc->y;
        int size = viewer_npc_size(v, npc);
        float dx = npc_x + 0.5f * (float)size - player_x;
        float dy = npc_y + 0.5f * (float)size - player_y;
        if (dx < -40 || dx > 40 || dy < -40 || dy > 40)
            continue;
        Vector2 offset = runec_minimap_rotate_offset(dx, dy, v->cam_yaw);
        runec_ui_add_minimap_dot(&v->ui, offset.x, offset.y,
                                 RUNEC_UI_MINIMAP_DOT_NPC);
    }
}

static void handle_input(ViewerState *v, int ui_capture) {
    RcPlayer *p = &v->world->player;
    const ViewerStreamingPlayerTransition *transition =
        &v->mapsquare_load.player_transition;
    int remote_transition = v->pending_dev_transport != NULL
        || (transition->active
            && (transition->source_plane != transition->destination_plane
                || abs(transition->destination_x - transition->source_x) > 2
                || abs(transition->destination_y - transition->source_y) > 2));

    if (remote_transition) {
        v->scene_right_tracking = 0;
        v->scene_right_dragged = 0;
    } else if (!ui_capture && !v->ui.context_open
            && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        v->scene_right_tracking = 1;
        v->scene_right_dragged = 0;
        v->scene_right_start = GetMousePosition();
        open_scene_context_menu(v);
    }

    // Camera orbit
    if (v->scene_right_tracking
            && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 mouse = GetMousePosition();
        float total_x = mouse.x - v->scene_right_start.x;
        float total_y = mouse.y - v->scene_right_start.y;
        if (!v->scene_right_dragged
                && total_x * total_x + total_y * total_y > 9.0f) {
            v->scene_right_dragged = 1;
            v->ui.context_open = 0;
            reset_viewer_context(v);
        }
        Vector2 d = GetMouseDelta();
        if (v->scene_right_dragged) {
            v->cam_yaw += d.x * 0.005f;
            v->cam_pitch -= d.y * 0.005f;
            if (v->cam_pitch < 0.1f) v->cam_pitch = 0.1f;
            if (v->cam_pitch > 1.4f) v->cam_pitch = 1.4f;
        }
    }
    if (v->scene_right_tracking
            && IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) {
        v->scene_right_tracking = 0;
        v->scene_right_dragged = 0;
    }
    float wh = ui_capture ? 0.0f : GetMouseWheelMove();
    if (wh != 0.0f) {
        v->cam_dist *= (wh > 0) ? (1.0f / 1.15f) : 1.15f;
        if (v->cam_dist < 5) v->cam_dist = 5;
        if (v->cam_dist > 300) v->cam_dist = 300;
    }

    if (!v->ui.chat_focused && !v->ui.bank_amount_source) {
        if (IsKeyPressed(KEY_FOUR)) { v->cam_yaw = 0; v->cam_pitch = 1.35f; v->cam_dist = 120; }
        if (IsKeyPressed(KEY_FIVE)) { v->cam_yaw = 0; v->cam_pitch = 0.6f; v->cam_dist = 50; }
        if (IsKeyPressed(KEY_L)) v->camera_locked = !v->camera_locked;
        if (IsKeyPressed(KEY_G)) {
            if (runec_dev_validation_set_god_mode(v->world, !v->god_mode))
                v->god_mode = !v->god_mode;
            else
                fprintf(stderr, "viewer god mode: unable to change damage protection\n");
            fprintf(stderr, "viewer god mode: %s\n",
                    v->god_mode ? "on" : "off");
        }
        if (IsKeyPressed(KEY_F3)) v->show_grid = !v->show_grid;
        if (IsKeyPressed(KEY_F4))
            v->show_npc_footprints = !v->show_npc_footprints;
        if (IsKeyPressed(KEY_C)) v->show_collision = !v->show_collision;
        if (IsKeyPressed(KEY_SPACE)) v->paused = !v->paused;
        if (IsKeyPressed(KEY_R))
            (void)rc_player_set_running(v->world, !p->running);
        if (IsKeyPressed(KEY_P)) pickup_current_tile(v);
        if (IsKeyPressed(KEY_PAGE_UP))
            viewer_set_scene_plane_delta(v, 1);
        if (IsKeyPressed(KEY_PAGE_DOWN))
            viewer_set_scene_plane_delta(v, -1);
        if (IsKeyPressed(KEY_HOME))
            v->scene_plane_override = -1;
    }

    if (remote_transition)
        return;

    // Click-to-move
    if (!ui_capture && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
            && !IsMouseButtonDown(MOUSE_BUTTON_RIGHT)
            && !IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        ViewerHoverTarget hover;
        int have_hover = resolve_scene_hover_target(v, &hover);
        if (v->ui.selected_target.kind != RUNEC_UI_SELECTED_NONE) {
            if (have_hover && hover.kind == VIEWER_HOVER_NPC) {
                int accepted = 0;
                if (v->ui.selected_target.kind == RUNEC_UI_SELECTED_ITEM) {
                    accepted = rc_player_use_inventory_item_on_npc(
                        v->world, v->ui.selected_target.source_slot,
                        hover.npc_uid);
                } else {
                    int spell_id = selected_spell_id_for_viewer(v);
                    if (spell_id >= 0)
                        accepted = rc_player_cast_spell_on_npc(
                            v->world, spell_id, hover.npc_uid);
                }
                if (accepted) {
                    viewer_start_click_feedback(
                        v, RUNEC_CLICK_FEEDBACK_RED);
                    runec_ui_clear_selected_target(&v->ui);
                }
                return;
            }
            if (have_hover && hover.kind == VIEWER_HOVER_OBJECT) {
                int accepted = 0;
                if (v->ui.selected_target.kind == RUNEC_UI_SELECTED_ITEM) {
                    accepted = rc_player_use_inventory_item_on_object_placement(
                        v->world, v->ui.selected_target.source_slot,
                        hover.object.obj_id, hover.object.x, hover.object.y,
                        hover.object.plane, hover.object.placement_key);
                } else {
                    int spell_id = selected_spell_id_for_viewer(v);
                    if (spell_id >= 0) {
                        accepted = rc_player_cast_spell_on_object_placement(
                            v->world, spell_id, hover.object.obj_id,
                            hover.object.x, hover.object.y, hover.object.plane,
                            hover.object.placement_key);
                    }
                }
                if (accepted) {
                    viewer_start_click_feedback(
                        v, RUNEC_CLICK_FEEDBACK_RED);
                    runec_ui_clear_selected_target(&v->ui);
                }
                return;
            }
            if (have_hover && hover.kind == VIEWER_HOVER_GROUND_ITEM) {
                int accepted = 0;
                if (v->ui.selected_target.kind == RUNEC_UI_SELECTED_ITEM) {
                    accepted = rc_player_use_inventory_item_on_ground_item(
                        v->world, v->ui.selected_target.source_slot,
                        hover.ground_item_idx);
                } else {
                    int spell_id = selected_spell_id_for_viewer(v);
                    if (spell_id >= 0) {
                        accepted = rc_player_cast_spell_on_ground_item(
                            v->world, spell_id, hover.ground_item_idx);
                    }
                }
                if (accepted) {
                    viewer_start_click_feedback(
                        v, RUNEC_CLICK_FEEDBACK_RED);
                    runec_ui_clear_selected_target(&v->ui);
                }
                return;
            }
        }

        if (have_hover && hover.kind == VIEWER_HOVER_NPC) {
            if (viewer_left_click_npc(v, hover.npc_uid))
                viewer_start_click_feedback(v, RUNEC_CLICK_FEEDBACK_RED);
            return;
        }
        if (have_hover && hover.kind == VIEWER_HOVER_OBJECT) {
            ViewerPickedObject object = hover.object;
            int option = hover.option >= 0 ? hover.option
                                           : object_first_action_option(v, object);
            if (option >= 0) {
                if (viewer_interact_object(v, object, option))
                    viewer_start_click_feedback(
                        v, RUNEC_CLICK_FEEDBACK_RED);
            } else {
                viewer_start_click_feedback(
                    v, RUNEC_CLICK_FEEDBACK_YELLOW);
                (void)route_player_to(v, object.x, object.y);
            }
            return;
        }
        if (have_hover && (hover.kind == VIEWER_HOVER_GROUND_ITEM
                    || hover.kind == VIEWER_HOVER_TILE)) {
            int tx = hover.tile_x;
            int ty = hover.tile_y;
            int ground_idx = hover.ground_item_idx;
            if (ground_idx >= 0) {
                if (rc_item_result_accepted(
                        rc_player_pickup_item(v->world, ground_idx))) {
                    viewer_start_click_feedback(
                        v, RUNEC_CLICK_FEEDBACK_RED);
                }
            } else {
                viewer_start_click_feedback(
                    v, RUNEC_CLICK_FEEDBACK_YELLOW);
                (void)route_player_to(v, tx, ty);
            }
        }
    }

    // WASD
    int dx = 0, dy = 0;
    if (!v->ui.chat_focused && !v->ui.bank_amount_source) {
        if (IsKeyDown(KEY_W)) dy = 1;
        if (IsKeyDown(KEY_S)) dy = -1;
        if (IsKeyDown(KEY_A)) dx = -1;
        if (IsKeyDown(KEY_D)) dx = 1;
    }
    if ((dx || dy) && p->route_idx >= p->route_len) {
        (void)rc_player_step(v->world, dx, dy);
    }
}

// Apply animation frame to player model
static void update_player_anim(ViewerState *v) {
    if (!v->anims) return;

    int attack_timer = v->player_attack_anim_timer;
    if (attack_timer <= 0) {
        v->player_attack_anim_suppressed = 0;
    }

    // Pick animation based on movement plus weapon BAS metadata when present.
    int one_shot_kind = PLAYER_ONE_SHOT_NONE;
    int target_anim = player_target_anim_id(v, &one_shot_kind);
    if (one_shot_kind != PLAYER_ONE_SHOT_NONE && v->player_one_shot_finished &&
            target_anim == v->cur_anim_id) {
        if (one_shot_kind == PLAYER_ONE_SHOT_ATTACK)
            v->player_attack_anim_suppressed = 1;
        target_anim = player_base_anim_id(v);
        one_shot_kind = PLAYER_ONE_SHOT_NONE;
    }

    // Switch animation
    if (target_anim != v->cur_anim_id) {
        v->cur_anim_id = target_anim;
        v->anim_frame_idx = 0;
        v->anim_frame_timer = 0;
        v->player_one_shot_finished = 0;
    }

    AnimSequence *seq = anim_get_sequence(v->anims, (uint16_t)v->cur_anim_id);
    if (!seq || seq->frame_count == 0) return;

    // Advance frame timer
    v->anim_frame_timer += GetFrameTime() * 50.0f; // ~20ms per client tick
    AnimSequenceFrame *sf = &seq->frames[v->anim_frame_idx % seq->frame_count];
    float delay = (float)(sf->delay > 0 ? sf->delay : 1);
    while (v->anim_frame_timer >= delay) {
        v->anim_frame_timer -= delay;
        if (one_shot_kind != PLAYER_ONE_SHOT_NONE &&
                v->anim_frame_idx + 1 >= seq->frame_count) {
            v->anim_frame_idx = seq->frame_count - 1;
            v->anim_frame_timer = 0.0f;
            v->player_one_shot_finished = 1;
            if (one_shot_kind == PLAYER_ONE_SHOT_ATTACK)
                v->player_attack_anim_suppressed = 1;
            break;
        }
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
        update_model_entry_colors_from_anim(pe, v->anim_state);
    }
}

// Skeleton (framebase) a sequence animates, or -1 when unknown.
static int viewer_sequence_skeleton(AnimCache *cache, int seq_id) {
    AnimSequence *seq = seq_id >= 0 && seq_id <= 0xFFFF
                      ? anim_get_sequence(cache, (uint16_t)seq_id) : NULL;
    return seq && seq->frame_count ? (int)seq->frames[0].frame.framebase_id : -1;
}

// Apply the correct animation frame to one NPC's shared mesh just before it
// draws. NPCs of the same def share the mesh buffer + AnimModelState, so the
// caller must animate and draw each instance sequentially (no batching across
// instances of the same type).
//
// Returns 1 if the mesh was updated (so caller knows it should draw), 0 if the
// NPC has no animation data and the caller should draw the rest pose.
static int update_npc_anim(ViewerState *v, int npc_idx, ModelEntry *me) {
    if (!v->anims || !me || !me->loaded)
        return 0;
    const RcNpc *n = &v->world->npcs[npc_idx];
    const RcNpcDef *def = rc_npc_def_for_npc(v->world, n);
    if (!def)
        return 0;
    const RuneCNpcRenderDef *render_def =
        runec_npc_render_find(&v->npc_render_defs, def->id);
    if (!render_def)
        return 0;
    int active_def_idx = rc_npc_def_find(def->id);
    AnimModelState *state = active_def_idx >= 0
                          ? v->npc_anim_state[active_def_idx] : NULL;
    if (!state) return 0;

    // Pick target anim from NPC state. stand/walk are always present; run,
    // attack, death are -1 on most non-combat NPCs, so fall back to walk/stand.
    int moved_last_tick = v->npc_render[npc_idx].moving
                        || v->npc_render[npc_idx].move_anim_timer > 0.0f;
    int target = render_def->stand_anim;
    int attack_anim_active = 0;
    if (n->is_dead && render_def->death_anim >= 0) {
        target = render_def->death_anim;
    }
    else if (v->npc_render[npc_idx].attack_anim_timer > 0
            && !v->npc_render[npc_idx].moving) {
        attack_anim_active = 1;
        int attack = v->npc_render[npc_idx].attack_anim_id;
        int bone = viewer_sequence_skeleton(v->anims, attack);
        int body = viewer_sequence_skeleton(v->anims, render_def->stand_anim);
        if (attack > 0 && bone >= 0 && body >= 0 && bone != body) {
            // A sequence made for another skeleton (a previous form's)
            // never deforms this definition's model.
            attack_anim_active = 0;
        } else if (attack > 0) {
            target = attack;
        } else {
            target = render_def->attack_anim >= 0
                ? render_def->attack_anim : target;
        }
    }
    else if (moved_last_tick && render_def->walk_anim >= 0) {
        target = render_def->walk_anim;
    }
    if (target < 0) return 0;

    // Detect anim change → reset frame / timer.
    if (v->npc_render[npc_idx].cur_anim_id != target) {
        v->npc_render[npc_idx].cur_anim_id = target;
        v->npc_render[npc_idx].frame_idx   = 0;
        v->npc_render[npc_idx].frame_timer = 0.0f;
    }

    AnimCache *cache = v->anims;
    AnimSequence *seq = anim_get_sequence(cache, (uint16_t)target);
    if (!seq || seq->frame_count == 0) return 0;

    // Advance frame timer (20ms per client tick = GetFrameTime() * 50).
    v->npc_render[npc_idx].frame_timer += GetFrameTime() * 50.0f;
    if (v->npc_render[npc_idx].frame_idx >= seq->frame_count)
        v->npc_render[npc_idx].frame_idx = seq->frame_count - 1;
    AnimSequenceFrame *sf = &seq->frames[v->npc_render[npc_idx].frame_idx];
    float delay = (float)(sf->delay > 0 ? sf->delay : 1);
    while (v->npc_render[npc_idx].frame_timer >= delay) {
        v->npc_render[npc_idx].frame_timer -= delay;
        if (attack_anim_active &&
                v->npc_render[npc_idx].frame_idx + 1 >= seq->frame_count) {
            v->npc_render[npc_idx].frame_idx = seq->frame_count - 1;
            v->npc_render[npc_idx].frame_timer = 0.0f;
            break;
        }
        v->npc_render[npc_idx].frame_idx =
            (v->npc_render[npc_idx].frame_idx + 1) % seq->frame_count;
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
    update_model_entry_colors_from_anim(me, state);
    return 1;
}

static void draw_hitsplat_state(ViewerState *v,
                                const RuneCHitsplatState *state,
                                Vector2 anchor) {
    if (!v || !state) return;
    Font font = runec_ui_font_for_size(&v->ui.assets, 12.0f);
    for (int slot_idx = 0; slot_idx < RUNEC_HITSPLAT_SLOT_COUNT;
            slot_idx++) {
        const RuneCHitsplatSlot *slot = &state->slots[slot_idx];
        if (!slot->active || slot->remaining_cycles <= 0.0f) continue;
        const RuneCHitsplatDef *definition = runec_hitsplat_def(
            &v->hitsplat_catalog, slot->definition_id);
        if (!definition || !v->hitsplat_texture_loaded[definition->id])
            continue;

        int offset_x = 0;
        int offset_y = 0;
        runec_hitsplat_slot_offset(slot_idx, &offset_x, &offset_y);
        float visual_x = 0.0f;
        float visual_y = 0.0f;
        unsigned char alpha = 255;
        runec_hitsplat_visual_offset(definition, slot, &visual_x, &visual_y,
                                     &alpha);
        float center_x = anchor.x + (float)offset_x
                       + visual_x;
        float center_y = anchor.y + (float)offset_y
                       + visual_y;
        DrawTexture(v->hitsplat_textures[definition->id],
                    (int)(center_x - 12.0f), (int)(center_y - 12.0f),
                    (Color){255, 255, 255, alpha});
        if (!definition->shows_amount) continue;

        char amount[16];
        snprintf(amount, sizeof(amount), "%d", slot->amount);
        Vector2 size = MeasureTextEx(font, amount, 12.0f, 0.0f);
        Vector2 text_position = {
            center_x - size.x * 0.5f,
            center_y - size.y * 0.5f + (float)definition->text_offset_y,
        };
        DrawTextEx(font, amount,
                   (Vector2){text_position.x + 1.0f,
                             text_position.y + 1.0f},
                   12.0f, 0.0f, (Color){0, 0, 0, alpha});
        Color text_color = {
            (unsigned char)(definition->text_color >> 16),
            (unsigned char)(definition->text_color >> 8),
            (unsigned char)definition->text_color,
            alpha,
        };
        DrawTextEx(font, amount, text_position, 12.0f, 0.0f, text_color);
    }
}

static int viewer_model_vertical_bounds(const ModelEntry *entry,
                                        float *min_y, float *max_y) {
    if (!entry || !entry->loaded || !entry->rest_verts)
        return 0;
    return runec_actor_model_vertical_bounds(
        entry->rest_verts, entry->model.meshes[0].vertexCount,
        min_y, max_y);
}

static void draw_health_bar(const RuneCHealthBarState *state,
                            Vector2 anchor) {
    if (!runec_health_bar_visible(state)) return;
    int fill = runec_health_bar_fill_width(state);
    int x = (int)anchor.x - RUNEC_HEALTH_BAR_WIDTH / 2;
    int y = (int)anchor.y - 3;
    if (fill > 0) {
        DrawRectangle(x, y, fill, RUNEC_HEALTH_BAR_HEIGHT,
                      (Color){0, 255, 0, 255});
    }
    if (fill < RUNEC_HEALTH_BAR_WIDTH) {
        DrawRectangle(x + fill, y, RUNEC_HEALTH_BAR_WIDTH - fill,
                      RUNEC_HEALTH_BAR_HEIGHT,
                      (Color){255, 0, 0, 255});
    }
}

static void draw_scene(ViewerState *v, int ui_capture) {
    RcPlayer *p = &v->world->player;
    int scene_plane = viewer_scene_plane(v);
    float t = v->tick_frac;
    int presented_x = p->x;
    int presented_y = p->y;
    int presented_plane = p->plane;
    int held = viewer_streaming_player_transition_source(
        &v->mapsquare_load.player_transition,
        &presented_x, &presented_y, &presented_plane);
    // Interpolate ordinary movement; distant scene activation holds a visual
    // source snapshot without changing authoritative player coordinates.
    float wx = held ? (float)presented_x
        : v->prev_player_x + ((float)p->x - v->prev_player_x) * t;
    float wy = held ? (float)presented_y
        : v->prev_player_y + ((float)p->y - v->prev_player_y) * t;
    float px = (wx - g_world_origin_x) + 0.5f;
    float pz = -((wy - g_world_origin_y) + 0.5f);
    float py = ground_yf_plane(v, scene_plane, wx, wy);

    if (v->camera_locked)
        v->camera.target = (Vector3){px, py, pz};
    v->camera.position = (Vector3){
        v->camera.target.x + v->cam_dist * cosf(v->cam_pitch) * sinf(v->cam_yaw),
        v->camera.target.y + v->cam_dist * sinf(v->cam_pitch),
        v->camera.target.z + v->cam_dist * cosf(v->cam_pitch) * cosf(v->cam_yaw)
    };

    int hover_tile_x = -1;
    int hover_tile_y = -1;
    int show_hover_tile = !ui_capture && !v->ui.context_open && !held
        && raycast_tile(v, &hover_tile_x, &hover_tile_y);

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
    }
    draw_mapsquare_chunks(v, scene_plane, frame_dt);
    draw_object_chunks(v, scene_plane);
    models_update_texture_anims(v->object_anim_model_planes[scene_plane],
                                frame_dt);
    draw_animated_objects(v, scene_plane, scene_objects);
    draw_spawned_objects(v, scene_plane);

    // Player model. If equipment needs body-part replacement, use the generated
    // client-style identity-kit/equipment composition path so torso/arms/legs/
    // hands/feet/head/jaw visibility follows cache wearpos metadata. If no
    // body part is hidden, keep the original exported player base and layer the
    // equipped render model on top; this preserves the known-good default body
    // for partial hats and weapons.
    if (presented_plane == scene_plane) {
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

    // NPC rendering — each live NPC's def_id indexes the active NPC definition
    // view, and the model set is keyed by NPC cache ID.
    int npc_count = 0;
    const RcNpc *npcs = rc_get_npcs(v->world, &npc_count);
    for (int i = 0; i < npc_count; i++) {
        const RcNpc *n = &npcs[i];
        RcNpcLifePhase life = rc_npc_life_phase(n);
        if (life == RC_NPC_LIFE_REMOVED || life == RC_NPC_LIFE_HIDDEN)
            continue;
        if (n->plane != scene_plane) continue;
        if (n->x < g_world_origin_x || n->x >= g_world_origin_x + g_world_w
                || n->y < g_world_origin_y
                || n->y >= g_world_origin_y + g_world_h)
            continue;
        if (!viewer_actor_in_draw_range(v, (float)n->x, (float)n->y, 0.0f))
            continue;

        const RcNpcDef *def = rc_npc_def_for_npc(v->world, n);
        if (!def) continue;
        int size = def->size > 0 ? def->size : 1;

        float nwx = v->npc_render[i].initialized
                  ? v->npc_render[i].render_x : (float)n->x;
        float nwy = v->npc_render[i].initialized
                  ? v->npc_render[i].render_y : (float)n->y;
        float nx_r = (nwx - g_world_origin_x) + 0.5f * (float)size;
        float nz_r = -((nwy - g_world_origin_y) + 0.5f * (float)size);
        float ny_r = ground_yf_plane(v, scene_plane, nwx, nwy);

        // Find the NPC's model by its cache ID
        ModelEntry *ne = NULL;
        if (v->npc_models && v->npc_models->loaded) {
            ne = model_find(v->npc_models, (uint32_t)def->id);
        }

        if (ne && ne->loaded) {
            // Forward walking follows the displayed step; stopped actors face
            // their interaction target. Do not use a later queued step's angle.
            float face_angle = 0.0f;
            int dx = n->x - n->prev_x;
            int dy = n->y - n->prev_y;
            if (v->npc_render[i].moving
                    && (v->npc_render[i].last_dx || v->npc_render[i].last_dy)) {
                face_angle = face_angle_between_tiles(0, 0,
                                                      v->npc_render[i].last_dx,
                                                      v->npc_render[i].last_dy,
                                                      face_angle);
            } else if (n->facing_entity >= 0) {
                face_angle = npc_core_facing_angle(n, size, face_angle);
            } else if (dx || dy) {
                face_angle = face_angle_between_tiles(n->prev_x, n->prev_y,
                                                      n->x, n->y, face_angle);
            } else {
                face_angle = npc_core_facing_angle(n, size, face_angle);
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
        if (!rc_ground_item_visible(g, RC_GROUND_OWNER_LOCAL_PLAYER)
                || g->plane != scene_plane) continue;
        if (g->x < g_world_origin_x || g->x >= g_world_origin_x + g_world_w
                || g->y < g_world_origin_y
                || g->y >= g_world_origin_y + g_world_h)
            continue;
        if (!viewer_actor_in_draw_range(v, (float)g->x, (float)g->y, 0.0f))
            continue;
        float gx = (float)LOCAL_X(g->x) + 0.5f;
        float gz = -((float)LOCAL_Y(g->y) + 0.5f);
        float gy = ground_y_plane(v, scene_plane, g->x, g->y) + 0.08f;
        draw_ground_item_model(v, g, (Vector3){gx, gy, gz});
    }

    draw_combat_projectiles(v);

    if (!held && v->input_feedback.pending_destination_active
            && v->route_preview_active
            && v->route_preview_plane == scene_plane) {
        draw_route_waypoints(
            v, scene_plane,
            v->route_preview_start_x, v->route_preview_start_y,
            v->route_preview.waypoints_x, v->route_preview.waypoints_y,
            0, v->route_preview.length);
    } else if (!held && !v->input_feedback.pending_destination_active) {
        draw_player_route(v, p, scene_plane);
    }
    if (show_hover_tile)
        draw_hovered_tile(v, scene_plane, hover_tile_x, hover_tile_y);

    rlEnableBackfaceCulling();

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

    if (v->show_npc_footprints) {
        rlDisableBackfaceCulling();
        draw_npc_footprints(v, scene_plane, npcs, npc_count);
        rlEnableBackfaceCulling();
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
        RcNpcLifePhase life = rc_npc_life_phase(n);
        if (life == RC_NPC_LIFE_REMOVED || life == RC_NPC_LIFE_HIDDEN
                || n->plane != scene_plane) continue;
        const RcNpcDef *def = rc_npc_def_for_npc(v->world, n);
        if (!def) continue;
        int size = def->size > 0 ? def->size : 1;
        float nwx = v->npc_render[i].initialized
                  ? v->npc_render[i].render_x : (float)n->x;
        float nwy = v->npc_render[i].initialized
                  ? v->npc_render[i].render_y : (float)n->y;
        float nx_r = (nwx - g_world_origin_x) + 0.5f * (float)size;
        float nz_r = -((nwy - g_world_origin_y) + 0.5f * (float)size);
        float ground_y = ground_yf_plane(v, scene_plane, nwx, nwy);
        float model_min_y = 0.0f;
        float model_max_y = 2.15f;
        float model_mid_y = 1.0f;
        ModelEntry *model = v->npc_models && v->npc_models->loaded
                          ? model_find(v->npc_models, (uint32_t)def->id) : NULL;
        if (viewer_model_vertical_bounds(model, &model_min_y, &model_max_y)) {
            model_mid_y = runec_actor_model_midpoint(
                model_min_y, model_max_y, NPC_MODEL_SCALE);
        }
        float health_bar_y = runec_health_bar_model_anchor(
            model_min_y, model_max_y, NPC_MODEL_SCALE);
        Vector2 head = GetWorldToScreen(
            (Vector3){nx_r, ground_y + health_bar_y, nz_r}, v->camera);
        Vector2 middle = GetWorldToScreen(
            (Vector3){nx_r, ground_y + model_mid_y, nz_r}, v->camera);
        draw_health_bar(&v->npc_overlays[i].health_bar, head);
        draw_hitsplat_state(v, &v->npc_overlays[i].hitsplats, middle);
    }

    float player_min_y = 0.0f;
    float player_max_y = 2.25f;
    float player_mid_y = 1.0f;
    ModelEntry *player_anchor_model = v->composed_player_model.loaded
                                    ? &v->composed_player_model
                                    : (v->player_model
                                       && v->player_model->loaded
                                       ? &v->player_model->entries[0] : NULL);
    if (viewer_model_vertical_bounds(player_anchor_model,
                                     &player_min_y, &player_max_y)) {
        player_mid_y = runec_actor_model_midpoint(
            player_min_y, player_max_y, 1.0f);
    }
    float player_health_bar_y = runec_health_bar_model_anchor(
        player_min_y, player_max_y, 1.0f);
    Vector2 player_head = GetWorldToScreen(
        (Vector3){px, py + player_health_bar_y, pz}, v->camera);
    Vector2 ps = GetWorldToScreen(
        (Vector3){px, py + player_mid_y, pz}, v->camera);
    draw_health_bar(&v->player_overlay.health_bar, player_head);
    const char *overhead = runec_prayer_overhead_asset(v->world->player.active_prayers);
    if (overhead && !v->world->player.is_dead) {
        const Texture2D *icon = runec_ui_asset(&v->ui.assets, overhead);
        if (icon) {
            int bar_gap = runec_health_bar_visible(&v->player_overlay.health_bar) ? 9 : 0;
            Vector2 pos = {roundf(player_head.x - icon->width * 0.5f),
                           roundf(player_head.y - icon->height - bar_gap - 4)};
            DrawTextureV(*icon, pos, WHITE);
        }
    }
    draw_hitsplat_state(v, &v->player_overlay.hitsplats, ps);
}

static void draw_streaming_telemetry_overlay(ViewerState *v) {
    if (!v || !v->telemetry_overlay)
        return;
    viewer_refresh_streaming_telemetry(v, 0);
    const ViewerStreamingTelemetry *t = &v->telemetry;
    char lines[8][160];
    snprintf(lines[0], sizeof(lines[0]),
             "Streaming  startup %.1f ms  last load %.1f ms",
             t->startup_ms, t->scene_or_chunk_load_ms);
    snprintf(lines[1], sizeof(lines[1]),
             "Decode %.1f  upload %.1f  main max %.1f ms",
             t->cpu_decode_ms, t->gpu_upload_ms,
             t->main_thread_streaming_work_max_ms);
    snprintf(lines[2], sizeof(lines[2]),
             "Async q%d f%llu cpu%d  up %.1f peak %.1f/%.1f MB",
             t->async_jobs_pending, (unsigned long long)t->async_frames,
             t->cpu_chunks_staged,
             (double)t->gpu_upload_bytes_frame / (1024.0 * 1024.0),
             (double)t->gpu_upload_bytes_peak / (1024.0 * 1024.0),
             (double)t->gpu_upload_budget_bytes / (1024.0 * 1024.0));
    snprintf(lines[3], sizeof(lines[3]),
             "Terrain %d/%d  vertices %llu",
             t->terrain_chunks_cpu, t->terrain_chunks_gpu,
             (unsigned long long)t->terrain_vertices_resident);
    snprintf(lines[4], sizeof(lines[4]),
             "Objects %d/%d  vertices %llu",
             t->object_chunks_cpu, t->object_chunks_gpu,
             (unsigned long long)t->object_vertices_resident);
    snprintf(lines[5], sizeof(lines[5]),
             "Texture %.1f MB  model %.1f MB",
             t->texture_cache_mb, t->model_cache_mb);
    snprintf(lines[6], sizeof(lines[6]),
             "NPCs %d  ground items %d  draws ~%d",
             t->active_npcs, t->active_ground_items,
             t->draw_calls_estimate);
    snprintf(lines[7], sizeof(lines[7]),
             "Backend pages %d  page %.1f ms  area %.1f ms",
             t->backend_pages_loaded, t->backend_page_load_ms,
             t->backend_active_area_load_ms);

    const int x = 8;
    const int y = 8;
    const int width = 390;
    const int line_height = 15;
    const int height = 8 + 8 * line_height;
    DrawRectangle(x, y, width, height, (Color){18, 20, 24, 220});
    DrawRectangleLines(x, y, width, height, (Color){120, 126, 136, 230});
    for (int i = 0; i < 8; i++)
        DrawText(lines[i], x + 7, y + 5 + i * line_height, 12, RAYWHITE);
}

static RcWorldConfig viewer_world_config(
    RcWorldStreamingConfig backend_streaming, const char *combat_visuals_path) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.streaming = backend_streaming;
    cfg.npc_capacity = RC_WORLD_NPC_CAPACITY_FULL;
    cfg.subsystems = RC_SUB_INVENTORY | RC_SUB_EQUIPMENT | RC_SUB_LOOT |
                     RC_SUB_COMBAT | RC_SUB_PRAYER | RC_SUB_OBJECTS |
                     RC_SUB_REGIONS | RC_SUB_TRAVERSAL | RC_SUB_STORAGE |
                     RC_SUB_ENCOUNTER | RC_SUB_CONSUMABLES;
    cfg.npc_defs_path = env_path("RUNEC_NPC_DEFS", "data/defs/npc_defs.bin");
    cfg.items_path = env_path("RUNEC_ITEMS", "data/defs/items.bin");
    cfg.drops_path = env_path("RUNEC_DROPS", "data/defs/drops.bin");
    cfg.rdt_path = env_path("RUNEC_RDT", "data/defs/rdt.bin");
    cfg.gdt_path = env_path("RUNEC_GDT", "data/defs/gdt.bin");
    cfg.mrdt_path = env_path("RUNEC_MRDT", "data/defs/mrdt.bin");
    cfg.prayers_path = env_path("RUNEC_PRAYERS", "data/defs/prayers.bin");
    cfg.varbits_path = env_path("RUNEC_VARBITS", "data/defs/varbits.bin");
    cfg.varps_path = env_path("RUNEC_VARPS", "data/defs/varps.bin");
    cfg.spells_path = env_path("RUNEC_SPELLS", "data/defs/spells.bin");
    cfg.combat_profiles_path = env_path("RUNEC_COMBAT_PROFILES",
        combat_visuals_path);
    cfg.monster_mechanics_path = env_path("RUNEC_MONSTER_MECHANICS",
        "data/defs/regular_npc_mechanics.bin");
    cfg.activity_schemas_path = env_path("RUNEC_ACTIVITY_SCHEMAS",
        "data/defs/activity_schemas.bin");
    cfg.activity_spawns_path = env_path("RUNEC_ACTIVITY_SPAWNS",
        "data/defs/activity_spawns.bin");
    cfg.activity_mechanics_path = env_path("RUNEC_ACTIVITY_MECHANICS",
        "data/defs/activity_mechanics.bin");
    cfg.activity_states_path = env_path("RUNEC_ACTIVITY_STATES",
        "data/defs/activity_states.bin");
    cfg.encounters_path = env_path("RUNEC_ENCOUNTERS",
        "data/defs/encounters.bin");
    cfg.player_actions_path = env_path("RUNEC_PLAYER_ACTIONS",
        "data/defs/player_actions.bin");
    cfg.object_defs_path = env_path("RUNEC_OBJECT_DEFS",
        "data/defs/object_defs.bin");
    cfg.object_placements_path = env_path("RUNEC_OBJECT_PLACEMENTS",
        "data/regions/world.object-placements.indexed.bin");
    cfg.object_behaviors_path = env_path("RUNEC_OBJECT_BEHAVIORS",
        "data/defs/object_behaviors.bin");
    cfg.collision_tiles_path = env_path("RUNEC_COLLISION_TILES",
        "data/regions/world.collision-tiles.indexed.bin");
    cfg.spawns_path = env_path("RUNEC_NPC_SPAWNS",
        "data/spawns/world.npc-spawns.indexed.bin");
    cfg.ground_item_spawns_path = env_path("RUNEC_GROUND_ITEM_SPAWNS",
        "data/spawns/world.ground-items.indexed.bin");
    cfg.area_flags_path = env_path("RUNEC_AREA_FLAGS",
        "data/defs/area_flags.bin");
    cfg.traversal_edges_path = env_path("RUNEC_TRAVERSAL_EDGES",
        "data/defs/traversal_edges.bin");
    cfg.seed = 12345;
    return cfg;
}

int main(int argc, char **argv) {
    (void)argc;
    double startup_started_ms = viewer_streaming_now_ms();
    ViewerState v = {0};
    int exit_status = 0;
    v.streaming = viewer_streaming_config_default();
    v.streaming.scene_radius_regions = env_int_compat(
        "RUNEC_VIEWER_SCENE_RADIUS_REGIONS", "RUNEC_VIEWER_DRAW_RADIUS",
        "RUNEC_SCENE_RADIUS_REGIONS", v.streaming.scene_radius_regions);
    v.streaming.preload_radius_regions = env_int_compat(
        "RUNEC_VIEWER_PRELOAD_RADIUS", "RUNEC_VIEWER_PRELOAD_RADIUS_REGIONS",
        "RUNEC_STARTUP_SCENE_RADIUS_REGIONS",
        v.streaming.preload_radius_regions);
    v.streaming.max_gpu_chunks = env_int(
        "RUNEC_VIEWER_MAX_GPU_CHUNKS", v.streaming.max_gpu_chunks);
    v.streaming.max_cpu_chunks = env_int(
        "RUNEC_VIEWER_MAX_CPU_CHUNKS", v.streaming.max_cpu_chunks);
    v.streaming.upload_budget_mb_per_frame = env_int(
        "RUNEC_VIEWER_UPLOAD_BUDGET_MB_PER_FRAME",
        v.streaming.upload_budget_mb_per_frame);
    viewer_streaming_config_sanitize(&v.streaming);
    v.telemetry_overlay = env_bool("RUNEC_VIEWER_TELEMETRY_OVERLAY", 0);

    RcWorldStreamingConfig backend_streaming =
        rc_world_streaming_config_default();
    backend_streaming.active_radius_regions = env_int(
        "RUNEC_WORLD_ACTIVE_RADIUS_REGIONS",
        backend_streaming.active_radius_regions);
    backend_streaming.max_cached_regions = env_int(
        "RUNEC_WORLD_MAX_CACHED_REGIONS",
        backend_streaming.max_cached_regions);
    rc_world_streaming_config_sanitize(&backend_streaming);

    v.scene_plane_override = env_int("RUNEC_SCENE_PLANE", -1);
    v.minimap_tiles_plane = -1;
    g_world_origin_x = env_int("RUNEC_WORLD_ORIGIN_X", DEFAULT_WORLD_ORIGIN_X);
    g_world_origin_y = env_int("RUNEC_WORLD_ORIGIN_Y", DEFAULT_WORLD_ORIGIN_Y);
    g_player_start_x = env_int("RUNEC_PLAYER_START_X", DEFAULT_PLAYER_START_X);
    g_player_start_y = env_int("RUNEC_PLAYER_START_Y", DEFAULT_PLAYER_START_Y);
    g_player_start_plane = clamp_plane(
        env_int("RUNEC_PLAYER_START_PLANE", 0));
    int default_world_side =
        (backend_streaming.active_radius_regions * 2 + 1) * RC_REGION_SIZE;
    g_world_w = env_int("RUNEC_WORLD_W", default_world_side);
    g_world_h = env_int("RUNEC_WORLD_H", default_world_side);
    v.scene_auto_export = env_bool("RUNEC_SCENE_AUTO_EXPORT", 0);
    v.preload_scene_planes = env_bool("RUNEC_PRELOAD_SCENE_PLANES", 0);
    v.object_chunk_size = env_int("RUNEC_OBJECT_CHUNK_SIZE", 64);
    v.object_chunk_draw_radius =
        env_float("RUNEC_OBJECT_CHUNK_DRAW_RADIUS", 180.0f);
    v.actor_draw_radius = env_float("RUNEC_ACTOR_DRAW_RADIUS", 64.0f);
    if (v.object_chunk_size <= 0)
        v.object_chunk_size = 64;
    snprintf(v.mapsquare_directory, sizeof(v.mapsquare_directory), "%s",
             env_path("RUNEC_MAPSQUARE_DIR", "data/regions"));
    viewer_load_mapsquare_catalog(&v);
    if (v.scene_auto_export) {
        fprintf(stderr,
                "viewer scene: development-only runtime export enabled by "
                "RUNEC_SCENE_AUTO_EXPORT=1\n");
    }
    if (!runtime_data_available(&v))
        return 1;
    int viewer_smoke = env_bool("RUNEC_VIEWER_SMOKE", 0);
    RuneCRenderSettings render_settings = render_settings_from_env();
    if (!viewer_smoke) {
        SetTraceLogLevel(viewer_trace_log_level_from_env());
        unsigned int window_flags = FLAG_WINDOW_RESIZABLE;
        if (render_settings.msaa_enabled)
            window_flags |= FLAG_MSAA_4X_HINT;
        SetConfigFlags(window_flags);
        if (!preflight_gl_context(&render_settings)) {
            maybe_reexec_with_mesa_glx(argv);
            print_gl_context_help("GLFW context preflight");
            return 1;
        }
    }
    const char *combat_visuals_path = env_path("RUNEC_COMBAT_VISUALS",
        "data/defs/combat_visuals.tsv");
    const char *npc_attack_anims_path = env_path("RUNEC_NPC_ATTACK_ANIMS",
        "data/defs/npc_attack_anims.tsv");
    const char *hitsplats_path = env_path("RUNEC_HITSPLATS",
        "data/defs/hitsplats.bin");
    const char *hitsplat_sprite_directory = env_path(
        "RUNEC_HITSPLAT_SPRITES", "data/sprites/ui");
    if (runec_hitsplat_catalog_load(&v.hitsplat_catalog, hitsplats_path) <= 0
            || !runec_hitsplat_catalog_validate_assets(
                &v.hitsplat_catalog, hitsplat_sprite_directory)) {
        fprintf(stderr, "viewer: required B237 hitsplats are unavailable\n");
        return 1;
    }

    RcWorldConfig cfg = viewer_world_config(backend_streaming, combat_visuals_path);
    fprintf(stderr,
            "backend streaming config: active_radius=%d "
            "max_cached_regions=%d\n",
            cfg.streaming.active_radius_regions,
            cfg.streaming.max_cached_regions);
    fprintf(stderr,
            "viewer streaming config: scene_radius=%d preload_radius=%d "
            "max_cpu_chunks=%d max_gpu_chunks=%d upload_budget_mb=%d\n",
            v.streaming.scene_radius_regions,
            v.streaming.preload_radius_regions,
            v.streaming.max_cpu_chunks, v.streaming.max_gpu_chunks,
            v.streaming.upload_budget_mb_per_frame);
    v.world = rc_world_create_config(&cfg);
    if (!v.world) { fprintf(stderr, "Failed to create world\n"); return 1; }
    v.npc_overlays = calloc(RC_MAX_NPCS, sizeof(*v.npc_overlays));
    if (!v.npc_overlays) {
        fprintf(stderr, "viewer: failed to allocate NPC overlay state\n");
        rc_world_destroy(v.world);
        return 1;
    }
    if (runec_npc_render_defs_load(&v.npc_render_defs, cfg.npc_defs_path) <= 0
            || runec_npc_render_defs_apply_attack_anims(
                &v.npc_render_defs, npc_attack_anims_path) <= 0) {
        fprintf(stderr,
                "viewer: required NPC render definitions are unavailable\n");
        free(v.npc_overlays);
        rc_world_destroy(v.world);
        return 1;
    }
    runec_object_action_visuals_load(&v.object_action_visuals,
                                     cfg.object_behaviors_path);
    // Register all OSRS content modules (boss scripts, etc.). See
    // rc-content/README.md for the engine/content split.
    if (!rc_world_install_content(v.world, rc_content_register_all))
        fprintf(stderr, "content: registration failed; see messages above\n");

    (void)rc_world_relocate_player(v.world, g_player_start_x,
                                   g_player_start_y, g_player_start_plane);
    v.prev_player_x = (float)g_player_start_x;
    v.prev_player_y = (float)g_player_start_y;
    set_viewer_demo_stats(&v.world->player);
    if (!runec_dev_validation_seed_prayers(v.world)
            || !runec_prayer_ui_sync(&v.ui.prayers, v.world)) {
        fprintf(stderr, "viewer: prayer definitions/unlocks/UI mapping are incomplete\n");
        free(v.npc_overlays);
        rc_world_destroy(v.world);
        return 1;
    }
    runec_dev_validation_seed_bank(v.world);

    if (viewer_smoke) {
        int combat_visual_count = rc_load_combat_visuals(combat_visuals_path);
        if (combat_visual_count < 0) {
            fprintf(stderr, "viewer smoke: failed to load combat visuals\n");
            free(v.npc_overlays);
            rc_world_destroy(v.world);
            return 1;
        }
        if (!activate_core_area_for_scene_bounds(&v)) {
            free(v.npc_overlays);
            rc_world_destroy(v.world);
            return 1;
        }
        if (env_bool("RUNEC_VIEWER_SMOKE_SCENES", 0)
                && validate_viewer_scene_assets(&v) < 0) {
            free(v.npc_overlays);
            rc_world_destroy(v.world);
            return 1;
        }
        if (env_bool("RUNEC_VIEWER_SMOKE_MAPSQUARES", 0)) {
            int prepared = viewer_prepare_mapsquare_window(
                &v, g_player_start_x, g_player_start_y,
                v.world->player.plane);
            if (prepared <= 0 || validate_mapsquare_window_assets(
                    &v, g_player_start_x, g_player_start_y,
                    v.world->player.plane) < 0) {
                free(v.npc_overlays);
                rc_world_destroy(v.world);
                return 1;
            }
        }
        v.telemetry.startup_ms = viewer_streaming_now_ms() - startup_started_ms;
        viewer_log_streaming_telemetry(&v, "startup-smoke");
        fprintf(stderr,
                "viewer smoke: PASS backend=%s combat_visuals=%d npcs=%d\n",
                env_path("RUNEC_ASSET_BACKEND", "auto"),
                combat_visual_count, v.world->npc_count);
        free(v.npc_overlays);
        rc_world_destroy(v.world);
        return 0;
    }

    InitWindow(WINDOW_W, WINDOW_H,
               env_path("RUNEC_VIEWER_TITLE", "RuneC Viewer"));
    if (!IsWindowReady()) {
        print_gl_context_help("raylib InitWindow");
        free(v.npc_overlays);
        rc_world_destroy(v.world);
        return 1;
    }
    SetTargetFPS(60);
    runec_ui_init(&v.ui);
    if (!viewer_load_hitsplat_textures(&v, hitsplat_sprite_directory)) {
        fprintf(stderr, "viewer: failed to load required hitsplat textures\n");
        exit_status = 1;
        goto cleanup;
    }
    viewer_sync_dev_transport_labels(&v.ui);
    rc_load_combat_visuals(combat_visuals_path);
    fprintf(stderr,
            "render profile: %s color_lift=%s msaa=%s camera_pitch=%.3f "
            "camera_dist=%.1f camera_fov=%.1f\n",
            render_settings.profile_name,
            render_settings.color_lift_enabled ? "on" : "off",
            render_settings.msaa_enabled ? "on" : "off",
            render_settings.camera_pitch, render_settings.camera_dist,
            render_settings.camera_fovy);

    int dynamic_model_shader_enabled =
        env_bool("RUNEC_DYNAMIC_MODEL_SHADER", 1);
    float static_brightness = render_settings.color_lift_enabled ? 1.16f : 1.0f;
    float static_lift = render_settings.color_lift_enabled ? 0.04f : 0.0f;
    float dynamic_brightness = render_settings.color_lift_enabled ? 1.10f : 1.0f;
    float dynamic_lift = render_settings.color_lift_enabled ? 0.09f : 0.0f;

    v.alpha_cutout_shader_static =
        load_alpha_cutout_shader(static_brightness, static_lift);
    v.alpha_cutout_shader_static_loaded = v.alpha_cutout_shader_static.id > 0;
    v.projectile_effect_shader =
        load_projectile_effect_shader(static_brightness, static_lift);
    v.projectile_effect_shader_loaded = v.projectile_effect_shader.id > 0;
    if (dynamic_model_shader_enabled) {
        v.alpha_cutout_shader_dynamic =
            load_alpha_cutout_shader(dynamic_brightness, dynamic_lift);
        v.alpha_cutout_shader_dynamic_loaded = v.alpha_cutout_shader_dynamic.id > 0;
    } else {
        fprintf(stderr,
                "dynamic model shader disabled by RUNEC_DYNAMIC_MODEL_SHADER=0\n");
    }
    v.mapsquare_load_generation = 1;
    if (!viewer_async_loader_init(
            &v.mapsquare_loader, viewer_async_mapsquare_decode,
            viewer_async_request_destroy, viewer_async_payload_destroy, &v)) {
        fprintf(stderr, "viewer mapsquare: failed to start async loader\n");
        exit_status = 1;
        goto cleanup;
    }

    v.cam_yaw = 0;
    v.cam_pitch = render_settings.camera_pitch;
    v.cam_dist = render_settings.camera_dist;
    v.camera_locked = 1;
    v.camera.up = (Vector3){0, 1, 0};
    v.camera.fovy = render_settings.camera_fovy;
    v.camera.projection = CAMERA_PERSPECTIVE;

    // No custom lighting shader — the export scripts already bake directional
    // lighting into vertex colors. Adding another pass just darkens everything.

    // Load world. Plane-specific files are optional: if
    // data/regions/varrock.p1.terrain or RUNEC_TERRAIN_P1 exists, scene
    // rendering can follow the selected plane while legacy single-plane assets
    // continue to load as plane 0 fallback.
    ViewerSceneMode scene_mode = viewer_scene_mode_from_env();
    const char *initial_terrain = env_path("RUNEC_TERRAIN",
        "data/regions/varrock.terrain");
    const char *initial_objects = env_path("RUNEC_OBJECTS",
        "data/regions/varrock.objects");
    const char *startup_scene_mode =
        viewer_mapsquare_cache_allowed()
            && viewer_mapsquare_center_available(
                &v, g_player_start_x, g_player_start_y,
                v.world->player.plane)
        ? "mapsquare" : viewer_scene_mode_name(scene_mode);
    fprintf(stderr,
            "viewer scene mode: %s auto_export=%s startup_radius=%d "
            "stream_radius=%d object_chunk_radius=%.1f "
            "actor_radius=%.1f\n",
            startup_scene_mode,
            v.scene_auto_export ? "on" : "off",
            v.streaming.preload_radius_regions,
            v.streaming.scene_radius_regions,
            v.object_chunk_draw_radius, v.actor_draw_radius);
    if (!load_startup_scene(&v, scene_mode, initial_terrain,
                            initial_objects)) {
        fprintf(stderr, "Failed to load initial visual scene\n");
        exit_status = 1;
        goto cleanup;
    }
    if (v.scene_plane_override >= 0)
        ensure_active_scene_plane(&v, v.scene_plane_override);
    if (loaded_scene_contains_tile(RUNEC_DEV_VARROCK_BANK_X,
                                   RUNEC_DEV_VARROCK_BANK_Y))
        runec_dev_validation_spawn_varrock_bank_dummy(v.world);
    build_minimap_tiles(&v);

    // Aggregate development scenes retain the synchronous compatibility path.
    // Mapsquare scenes arrive through the worker and are already GPU-resident.
    if (!v.npc_models) {
        uint32_t *npc_model_ids = calloc(
            RC_MAX_NPC_DEFS, sizeof(uint32_t));
        int npc_model_id_count = 0;
        if (npc_model_ids) {
            npc_model_id_count = collect_spawned_npc_model_ids(
                &v, npc_model_ids, RC_MAX_NPC_DEFS,
                0, RC_MAX_PLANES - 1);
            if (npc_model_id_count < 0) {
                free(npc_model_ids);
                fprintf(stderr,
                        "npc models: failed to collect startup forms\n");
                exit_status = 1;
                goto cleanup;
            }
        }
        uint32_t empty_model_ids[1] = {0};
        const uint32_t *model_filter = npc_model_ids
            ? npc_model_ids : empty_model_ids;
        v.npc_models = models_load_filtered(
            startup_npc_models_path(&v), model_filter, npc_model_id_count);
        free(npc_model_ids);
    }

    v.anims = anim_cache_load(env_path("RUNEC_ANIMS", "data/anims/all.anims"));
    if (!v.anims) {
        fprintf(stderr, "viewer: shared animation cache is missing or invalid\n");
        return 1;
    }
    if (v.npc_models && v.npc_models->loaded) {
        int created = 0;
        int npc_def_count = 0;
        const RcNpcDef *npc_defs = rc_npc_defs_all(&npc_def_count);
        for (int i = 0; npc_defs && i < npc_def_count; i++) {
            ModelEntry *me = model_find(v.npc_models, (uint32_t)npc_defs[i].id);
            if (me && me->loaded && me->vertex_skins && me->base_vert_count > 0) {
                v.npc_anim_state[i] = anim_model_state_create_with_faces(
                    me->vertex_skins, me->base_vert_count,
                    me->face_skins, me->face_count, me->face_alphas);
                created++;
            }
        }
        fprintf(stderr, "npc_anim: created %d per-def anim states\n", created);
    }

    // Load player model + animations
    v.player_model = models_load(env_path("RUNEC_PLAYER_MODELS",
        "data/models/player.models"));
    v.item_models = models_load(env_path("RUNEC_ITEM_MODELS",
        "data/models/items.models"));
    if (env_bool("RUNEC_LOAD_PROJECTILE_MODELS", 0)) {
        v.projectile_models = models_load(env_path("RUNEC_PROJECTILE_MODELS",
            "data/models/projectiles.models"));
        if (v.projectile_models && v.projectile_effect_shader_loaded)
            models_set_shader(v.projectile_models, v.projectile_effect_shader);
        create_projectile_anim_states(&v);
    } else {
        fprintf(stderr,
                "projectile models: lazy active-projectile loading enabled; "
                "set RUNEC_LOAD_PROJECTILE_MODELS=1 for eager full-pack load\n");
    }
    v.spotanims = spotanims_load(env_path("RUNEC_SPOTANIMS",
        "data/defs/spotanims.bin"));
    load_spawn_objects(&v);
    runec_item_render_map_load(&v.item_render_map, env_path("RUNEC_ITEM_RENDER_MAP",
        "data/models/item_render.map"));
    runec_item_def_render_map_load(&v.item_def_render_map, cfg.items_path);
    load_item_stack_variants(&v, env_path("RUNEC_ITEM_STACK_VARIANTS",
        "data/sprites/items/item_stack_variants.tsv"));
    create_item_anim_states(&v);
    build_ui_item_icons(&v);
    {
        const char *object_anim_path = env_path("RUNEC_OBJECT_ANIMS",
            "data/anims/object.anims");
        if (rc_asset_exists(object_anim_path))
            v.object_anim_cache = anim_cache_load(object_anim_path);
    }

    if (v.player_model && v.player_model->loaded && v.player_model->entries[0].loaded) {
        ModelEntry *pe = &v.player_model->entries[0];
        // Don't apply lighting shader to player — the animation system rewrites
        // mesh vertices each frame in OSRS units, then the shader's mvp transforms
        // them. The default shader handles this correctly.
        v.anim_state = anim_model_state_create_with_faces(
            pe->vertex_skins, pe->base_vert_count,
            pe->face_skins, pe->face_count, pe->face_alphas);
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
    v.telemetry.startup_ms = viewer_streaming_now_ms() - startup_started_ms;
    viewer_log_streaming_telemetry(&v, "startup-ready");

    const char *dev_transport_dest = getenv("RUNEC_DEV_TRANSPORT_DEST");
    const RuneCDevTransport *dev_transport =
        runec_dev_validation_find_transport(dev_transport_dest);
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
    double frame_benchmark_started_ms = max_frames > 0
        ? viewer_streaming_now_ms() : 0.0;

    while (!WindowShouldClose()) {
        runec_input_feedback_advance(&v.input_feedback, GetFrameTime());
        viewer_process_deferred_mapsquare_eviction(&v);
        viewer_process_mapsquare_loading(&v);
        RcPlayer *p = &v.world->player;
        runec_ui_sync_status(&v.ui, p->x, p->y, LOCAL_X(p->x), LOCAL_Y(p->y),
                             (uint32_t)v.world->tick, p->running, v.paused);
        sync_ui_items(&v);
        sync_ui_player_status(&v);
        viewer_sync_scene_plane_ui(&v);
        int ui_capture = runec_ui_handle_input(&v.ui, GetScreenWidth(), GetScreenHeight());
        if (v.ui.last_intent.kind == RUNEC_UI_INTENT_RUN_TOGGLE) {
            (void)rc_player_set_running(v.world, !p->running);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_BANK_WITHDRAW
                || v.ui.last_intent.kind == RUNEC_UI_INTENT_BANK_WITHDRAW_ALL_BUT_ONE) {
            rc_bank_withdraw_slot_mode(v.world, v.ui.last_intent.primary,
                v.ui.last_intent.secondary, v.ui.bank_noted != 0,
                v.ui.last_intent.kind == RUNEC_UI_INTENT_BANK_WITHDRAW_ALL_BUT_ONE);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_BANK_DEPOSIT) {
            rc_bank_deposit_slot(v.world, v.ui.last_intent.primary,
                                  v.ui.last_intent.secondary);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_BANK_DEPOSIT_ALL) {
            rc_bank_deposit_all(v.world, v.ui.last_intent.primary != 0);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_BANK_CLOSE) {
            rc_player_close_storage(v.world);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_BANK_EXAMINE) {
            const RcItemDef *def = rc_item_def_get(v.ui.last_intent.primary);
            if (def) runec_ui_add_chat_message(&v.ui, def->examine[0]
                ? def->examine : "Examine text is unavailable for this item.");
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_SCENE_PLANE) {
            if (v.ui.last_intent.primary < 0) {
                v.scene_plane_override = -1;
            } else {
                v.scene_plane_override = clamp_plane(v.ui.last_intent.primary);
            }
            ensure_active_scene_plane(&v, viewer_scene_plane(&v));
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_DEV_TRANSPORT) {
            int idx = v.ui.last_intent.primary;
            int count = 0;
            const RuneCDevTransport *transports =
                runec_dev_validation_transports(&count);
            if (transports && idx >= 0 && idx < count)
                viewer_dev_transport_to(&v, &transports[idx]);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_DEV_SPELLBOOK) {
            if (runec_dev_validation_set_spellbook(v.world, v.ui.last_intent.primary))
                runec_ui_set_active_tab(&v.ui, RUNEC_UI_TAB_SPELLBOOK);
            else
                runec_ui_add_chat_message(&v.ui, "Spellbook switch rejected: testing disabled or player busy.");
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_MINIMAP_CLICK) {
            int tile_x = 0;
            int tile_y = 0;
            if (runec_minimap_click_to_world(
                    (float)v.ui.last_intent.primary,
                    (float)v.ui.last_intent.secondary,
                    (float)p->x + 0.5f, (float)p->y + 0.5f,
                    v.cam_yaw, &tile_x, &tile_y)) {
                route_player_to(&v, tile_x, tile_y);
            }
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_INVENTORY_SLOT) {
            int slot = v.ui.last_intent.primary;
            int previous = v.ui.last_intent.secondary;
            if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
                rc_player_drop_item(v.world, slot);
            } else if (slot >= 0 && slot < RC_INVENTORY_SIZE
                    && p->inventory[slot].item_id >= 0) {
                const RcItemDef *def = rc_item_def_get(p->inventory[slot].item_id);
                int consume_op = -1;
                for (int i = 0; i < RC_ITEM_ACTION_COUNT; i++) {
                    if (rc_consumable_action(p->inventory[slot].item_id, i)) {
                        consume_op = i;
                        break;
                    }
                }
                if (consume_op >= 0)
                    rc_player_interact_inventory_item(v.world, slot, consume_op);
                else if (def && def->equippable && def->equipable_by_player)
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
            int op = v.ui.last_intent.secondary;
            if (op == RUNEC_UI_ITEM_OP_EQUIP) {
                rc_player_equip(v.world, slot);
            } else if (op == RUNEC_UI_ITEM_OP_DROP) {
                rc_player_drop_item(v.world, slot);
            } else if (op == RUNEC_UI_ITEM_OP_EXAMINE
                    && slot >= 0 && slot < RC_INVENTORY_SIZE
                    && p->inventory[slot].item_id >= 0) {
                rc_player_examine_inventory_item(v.world, slot);
            } else if (op >= 0) {
                rc_player_interact_inventory_item(v.world, slot, op);
            }
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_EQUIPMENT_SLOT) {
            int core_slot = ui_equip_slot_to_core(v.ui.last_intent.primary);
            if (core_slot >= 0)
                rc_player_unequip(v.world, core_slot);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_EQUIPMENT_ACTION) {
            int core_slot = ui_equip_slot_to_core(v.ui.last_intent.primary);
            int op = v.ui.last_intent.secondary;
            if (core_slot >= 0 && op == RUNEC_UI_ITEM_OP_REMOVE) {
                rc_player_unequip(v.world, core_slot);
            } else if (core_slot >= 0
                    && op == RUNEC_UI_ITEM_OP_EXAMINE) {
                rc_player_examine_equipment_item(v.world, core_slot);
            }
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_COMBAT_STYLE) {
            if (p->autocast_spell >= 0) rc_player_set_autocast_spell(v.world, -1, 0);
            rc_combat_set_player_style(v.world, v.ui.last_intent.primary);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_AUTO_RETALIATE) {
            if (!rc_combat_set_auto_retaliate(v.world,
                                              v.ui.last_intent.primary != 0))
                fprintf(stderr, "auto-retaliate: command rejected (%d)\n",
                        v.world->player_commands.last_result);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_SPECIAL_ATTACK) {
            if ((p->combat.special_pending ? 1 : 0) != v.ui.last_intent.primary)
                rc_combat_toggle_special(v.world);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_PRAYER_SLOT) {
            rc_player_set_prayer(v.world, v.ui.last_intent.primary);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_QUICK_PRAYER_SLOT) {
            rc_player_select_quick_prayer(v.world, v.ui.last_intent.primary);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_QUICK_PRAYER_TOGGLE) {
            rc_player_toggle_quick_prayers(v.world);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_SELECTED_SPELL) {
            int spell_idx = runec_ui_spell_runtime_id(&v.ui, v.ui.last_intent.primary);
            if (spell_idx >= 0)
                rc_player_select_spell(v.world, spell_idx);
            else {
                runec_ui_clear_selected_target(&v.ui);
                runec_ui_add_chat_message(&v.ui, "This spell is not supported by the current casting system.");
            }
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_AUTOCAST_SPELL) {
            int spell_idx = runec_ui_spell_runtime_id(&v.ui, v.ui.last_intent.primary);
            if (spell_idx >= 0 || v.ui.last_intent.primary == -1)
                rc_player_set_autocast_spell(v.world, spell_idx, v.ui.last_intent.secondary);
            else
                runec_ui_add_chat_message(&v.ui, "This spell is not supported by the current casting system.");
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_SELECTED_ITEM_ON_ITEM) {
            if (rc_player_use_inventory_item_on_inventory_item(
                    v.world, v.ui.last_intent.primary,
                    v.ui.last_intent.secondary)) {
                runec_ui_clear_selected_target(&v.ui);
            }
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_SELECTED_SPELL_ON_ITEM) {
            int spell_idx = selected_spell_id_for_viewer(&v);
            if (spell_idx >= 0 && rc_player_cast_spell_on_inventory_item(
                    v.world, spell_idx, v.ui.last_intent.secondary))
                runec_ui_clear_selected_target(&v.ui);
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_SELECTED_ITEM_ON_COMPONENT) {
            uint32_t component_id = (uint32_t)v.ui.last_intent.secondary;
            if (rc_player_use_inventory_item_on_widget(
                    v.world, v.ui.last_intent.primary,
                    ui_component_group(component_id),
                    ui_component_child(component_id))) {
                runec_ui_clear_selected_target(&v.ui);
            }
        } else if (v.ui.last_intent.kind == RUNEC_UI_INTENT_SELECTED_SPELL_ON_COMPONENT) {
            int spell_idx = selected_spell_id_for_viewer(&v);
            if (spell_idx >= 0) {
                uint32_t component_id = (uint32_t)v.ui.last_intent.secondary;
                if (rc_player_cast_spell_on_widget(
                        v.world, spell_idx, ui_component_group(component_id),
                        ui_component_child(component_id))) {
                    runec_ui_clear_selected_target(&v.ui);
                }
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
        if (v.ui.context_dismissed)
            reset_viewer_context(&v);
        handle_input(&v, ui_capture);
        viewer_update_movement_prefetch(&v);

        // Animation (runs every frame, independent of ticks)
        update_player_anim(&v);

        // Tick
        if (!v.paused) {
            int ticks_due = rc_viewer_tick_pacing_advance(
                &v.tick_pacing, GetFrameTime(), 1.0 / TPS);
            for (int tick_idx = 0; tick_idx < ticks_due; tick_idx++) {
                v.prev_player_x = (float)v.world->player.x;
                v.prev_player_y = (float)v.world->player.y;

                int old_x = v.world->player.x;
                int old_y = v.world->player.y;
                int old_plane = v.world->player.plane;
                rc_world_tick(v.world);
                // Capture every authoritative step, including catch-up ticks.
                update_npc_render_motion(&v, 0.0f);
                viewer_sync_traversal_event(&v);
                viewer_sync_traversal_outcome(&v);
                runec_input_feedback_reconcile_tick(
                    &v.input_feedback, v.world->tick);
                viewer_tick_combat_projectiles(&v);
                viewer_tick_attack_anims(&v);
                viewer_capture_combat_attack_events(&v);
                debug_log_combat_attack_events(&v);
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
            v.tick_frac = (float)rc_viewer_tick_pacing_fraction(
                &v.tick_pacing, 1.0 / TPS);
        }
        viewer_sync_interaction_outcome(&v);
        viewer_sync_consumption(&v);
        viewer_sync_traversal_event(&v);
        viewer_sync_traversal_outcome(&v);
        float presentation_dt = v.paused ? 0.0f : GetFrameTime();
        update_npc_render_motion(&v, presentation_dt);
        viewer_update_combat_overlays(&v, presentation_dt);

        BeginDrawing();
        ClearBackground((Color){40, 45, 55, 255});
        draw_scene(&v, ui_capture);
        runec_ui_sync_status(&v.ui, p->x, p->y, LOCAL_X(p->x), LOCAL_Y(p->y),
                             (uint32_t)v.world->tick, p->running, v.paused);
        sync_ui_items(&v);
        build_ui_item_icons(&v);
        sync_ui_player_status(&v);
        viewer_sync_scene_plane_ui(&v);
        sync_ui_minimap(&v);
        runec_ui_draw(&v.ui, GetScreenWidth(), GetScreenHeight());
        viewer_draw_click_feedback(&v);
        draw_hover_action_label(&v, ui_capture);
        draw_streaming_telemetry_overlay(&v);
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
    if (max_frames > 0) {
        double elapsed_ms = viewer_streaming_now_ms()
                          - frame_benchmark_started_ms;
        double fps = elapsed_ms > 0.0
                   ? (double)frame_count * 1000.0 / elapsed_ms : 0.0;
        fprintf(stderr,
                "viewer frame benchmark: frames=%d elapsed_ms=%.2f fps=%.2f\n",
                frame_count, elapsed_ms, fps);
    }

cleanup:
    viewer_async_loader_destroy(&v.mapsquare_loader);
    discard_pending_mapsquare_load(&v);
    for (int plane = 0; plane < RC_MAX_PLANES; plane++) {
        terrain_free(v.terrain_planes[plane]);
        objects_free(v.object_planes[plane]);
        free_object_anim_plane(&v, plane);
        free_object_chunk_plane(&v, plane);
    }
    free_mapsquare_cache(&v);
    free_item_anim_states(&v);
    free_projectile_anim_states(&v);
    free_spawn_objects(&v);
    clear_composed_player_model(&v);
    models_free(v.player_model);
    for (int i = 0; i < RC_MAX_GROUND_ITEMS; i++)
        models_free(v.ground_models[i].models);
    models_free(v.npc_models);
    models_free(v.item_models);
    models_free(v.projectile_models);
    spotanims_free(v.spotanims);
    runec_item_render_map_free(&v.item_render_map);
    runec_item_def_render_map_free(&v.item_def_render_map);
    anim_model_state_free(v.anim_state);
    for (int i = 0; i < RC_MAX_NPC_DEFS; i++)
        anim_model_state_free(v.npc_anim_state[i]);
    anim_cache_free(v.anims);
    anim_cache_free(v.object_anim_cache);
    rc_world_destroy(v.world);
    free(v.npc_overlays);
    viewer_unload_hitsplat_textures(&v);
    runec_ui_shutdown(&v.ui);
    free(v.minimap_tiles);
    if (v.alpha_cutout_shader_static_loaded)
        UnloadShader(v.alpha_cutout_shader_static);
    if (v.alpha_cutout_shader_dynamic_loaded)
        UnloadShader(v.alpha_cutout_shader_dynamic);
    if (v.projectile_effect_shader_loaded)
        UnloadShader(v.projectile_effect_shader);
    CloseWindow();
    return exit_status;
}
