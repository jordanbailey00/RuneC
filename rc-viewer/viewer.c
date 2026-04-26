// RuneC Viewer — walk around Varrock rendered in Raylib.
// Ported from runescape-rl/claude demo-env/src/viewer.c

#include "../rc-core/api.h"
#include "../rc-core/pathfinding.h"
#include "../rc-core/npc.h"
#include "../rc-content/content.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "terrain.h"
#include "objects.h"
#include "models.h"
#include "anims.h"
#include "collision.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// World origin: region (48,51) = tile (3072, 3264)
#define WORLD_ORIGIN_X 3072
#define WORLD_ORIGIN_Y 3264
// Player position is in WORLD coordinates (for pathfinding).
// Subtract WORLD_ORIGIN for rendering (local = world - origin).
#define PLAYER_START_X 3213
#define PLAYER_START_Y 3428
#define WORLD_W 320
#define WORLD_H 320
#define WINDOW_W 1280
#define WINDOW_H 720
#define TPS 1.667f
#define NPC_MODEL_SCALE 1.0f

// Player animation sequence IDs (from FC — xbows_human variants)
#define ANIM_IDLE 4591
#define ANIM_WALK 4226
#define ANIM_RUN  4228

typedef struct {
    RcWorld *world;
    TerrainMesh *terrain;
    ObjectMesh *objects;
    ModelSet *player_model;
    ModelSet *npc_models;
    AnimCache *anims;           // player animations
    AnimCache *npc_anims;       // NPC animations (separate cache; IDs don't overlap)
    AnimModelState *anim_state; // player
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
} ViewerState;

// Convert world tile to local rendering coordinate
#define LOCAL_X(wx) ((wx) - WORLD_ORIGIN_X)
#define LOCAL_Y(wy) ((wy) - WORLD_ORIGIN_Y)

static float ground_y(ViewerState *v, int world_x, int world_y) {
    if (v->terrain && v->terrain->loaded)
        return terrain_height_avg(v->terrain, LOCAL_X(world_x), LOCAL_Y(world_y)) + 0.05f;
    return 0.0f;
}

static int append_unique_model_id(uint32_t *ids, int count, uint32_t id) {
    for (int i = 0; i < count; i++)
        if (ids[i] == id) return count;
    ids[count++] = id;
    return count;
}

static int collect_spawned_npc_model_ids(RcWorld *world, uint32_t *ids,
                                         int max_ids, int plane) {
    int count = 0;
    for (int i = 0; i < world->npc_count && count < max_ids; i++) {
        const RcNpc *npc = &world->npcs[i];
        if (!npc->active || npc->plane != plane
                || npc->def_id >= (uint32_t)g_npc_def_count) continue;
        count = append_unique_model_id(ids, count, (uint32_t)g_npc_defs[npc->def_id].id);
    }
    return count;
}

// Returns world tile coordinates
static int raycast_tile(ViewerState *v, int *out_x, int *out_y) {
    Ray ray = GetScreenToWorldRay(GetMousePosition(), v->camera);
    float gy = ground_y(v, WORLD_ORIGIN_X + WORLD_W/2, WORLD_ORIGIN_Y + WORLD_H/2);
    if (fabsf(ray.direction.y) < 0.001f) return 0;
    float t = (gy - ray.position.y) / ray.direction.y;
    if (t < 0) return 0;
    // Local rendering coords → world coords
    int lx = (int)floorf(ray.position.x + ray.direction.x * t);
    int ly = (int)floorf(-(ray.position.z + ray.direction.z * t));
    *out_x = lx + WORLD_ORIGIN_X;
    *out_y = ly + WORLD_ORIGIN_Y;
    return (lx >= 0 && lx < WORLD_W && ly >= 0 && ly < WORLD_H);
}

static void handle_input(ViewerState *v) {
    RcPlayer *p = &v->world->player;

    // Camera orbit
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 d = GetMouseDelta();
        v->cam_yaw += d.x * 0.005f;
        v->cam_pitch -= d.y * 0.005f;
        if (v->cam_pitch < 0.1f) v->cam_pitch = 0.1f;
        if (v->cam_pitch > 1.4f) v->cam_pitch = 1.4f;
    }
    float wh = GetMouseWheelMove();
    if (wh != 0) {
        v->cam_dist *= (wh > 0) ? (1.0f / 1.15f) : 1.15f;
        if (v->cam_dist < 5) v->cam_dist = 5;
        if (v->cam_dist > 300) v->cam_dist = 300;
    }

    if (IsKeyPressed(KEY_FOUR)) { v->cam_yaw = 0; v->cam_pitch = 1.35f; v->cam_dist = 120; }
    if (IsKeyPressed(KEY_FIVE)) { v->cam_yaw = 0; v->cam_pitch = 0.6f; v->cam_dist = 50; }
    if (IsKeyPressed(KEY_L)) v->camera_locked = !v->camera_locked;
    if (IsKeyPressed(KEY_G)) v->show_grid = !v->show_grid;
    if (IsKeyPressed(KEY_C)) v->show_collision = !v->show_collision;
    if (IsKeyPressed(KEY_SPACE)) v->paused = !v->paused;
    if (IsKeyPressed(KEY_R)) p->running = !p->running;

    // Click-to-move
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        int tx, ty;
        if (raycast_tile(v, &tx, &ty)) {
            RcRoute route = rc_find_path(&v->world->map, p->x, p->y, tx, ty, 1, 0, true);
            if (route.length > 0) {
                for (int i = 0; i < route.length && i < RC_MAX_ROUTE; i++) {
                    p->route_x[i] = route.waypoints_x[i];
                    p->route_y[i] = route.waypoints_y[i];
                }
                p->route_len = route.length;
                p->route_idx = 0;
            }
        }
    }

    // WASD
    int dx = 0, dy = 0;
    if (IsKeyDown(KEY_W)) dy = 1;
    if (IsKeyDown(KEY_S)) dy = -1;
    if (IsKeyDown(KEY_A)) dx = -1;
    if (IsKeyDown(KEY_D)) dx = 1;
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
        uint32_t f = rc_get_flags(&world->map, 3210, 3426, 0);
        fprintf(stderr, "DEBUG: flags at (3210,3426) = 0x%08X (expect non-zero)\n", f);
    }

    int steps = p->running ? 2 : 1;
    for (int s = 0; s < steps && p->route_idx < p->route_len; s++) {
        int nx = p->route_x[p->route_idx];
        int ny = p->route_y[p->route_idx];
        int dx = nx - p->x, dy = ny - p->y;
        if (dx > 1) dx = 1; if (dx < -1) dx = -1;
        if (dy > 1) dy = 1; if (dy < -1) dy = -1;

        int can = rc_can_move(&world->map, p->x, p->y, dx, dy, 0);
        if (can) {
            p->x += dx; p->y += dy;
            // atan2(dx, dy) gives world-space angle. Negate because Z is flipped in rendering.
            v->player_facing_angle = atan2f((float)dx, -(float)dy) * (180.0f / 3.14159f);
            *moved = 1;
        } else {
            uint32_t dest_f = rc_get_flags(&world->map, p->x + dx, p->y + dy, 0);
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
    if (!v->anims || !v->anim_state || !v->player_model || !v->player_model->loaded) return;

    // Pick animation based on movement
    int target_anim = ANIM_IDLE;
    if (v->player_moving) target_anim = v->world->player.running ? ANIM_RUN : ANIM_WALK;

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
    if (!v->npc_anims || !me || !me->loaded) return 0;
    const RcNpc *n = &v->world->npcs[npc_idx];
    const RcNpcDef *def = &g_npc_defs[n->def_id];
    AnimModelState *state = v->npc_anim_state[n->def_id];
    if (!state) return 0;

    // Pick target anim from NPC state. stand/walk are always present; run,
    // attack, death are -1 on most non-combat NPCs, so fall back to walk/stand.
    int moved_last_tick = (n->x != n->prev_x) || (n->y != n->prev_y);
    int target = def->stand_anim;
    if (n->is_dead && def->death_anim >= 0)       target = def->death_anim;
    else if (moved_last_tick && def->walk_anim >= 0) target = def->walk_anim;
    if (target < 0) return 0;

    // Detect anim change → reset frame / timer.
    if (v->npc_render[npc_idx].cur_anim_id != target) {
        v->npc_render[npc_idx].cur_anim_id = target;
        v->npc_render[npc_idx].frame_idx   = 0;
        v->npc_render[npc_idx].frame_timer = 0.0f;
    }

    AnimSequence *seq = anim_get_sequence(v->npc_anims, (uint16_t)target);
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

    AnimFrameBase *fb = anim_get_framebase(v->npc_anims, sf->frame.framebase_id);
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
    float t = v->tick_frac;
    // Interpolate in world coords, then convert to local for rendering
    float wx = v->prev_player_x + ((float)p->x - v->prev_player_x) * t;
    float wy = v->prev_player_y + ((float)p->y - v->prev_player_y) * t;
    float px = (wx - WORLD_ORIGIN_X) + 0.5f;
    float pz = -((wy - WORLD_ORIGIN_Y) + 0.5f);
    float py = ground_y(v, p->x, p->y);

    if (v->camera_locked)
        v->camera.target = (Vector3){px, py, pz};
    v->camera.position = (Vector3){
        v->camera.target.x + v->cam_dist * cosf(v->cam_pitch) * sinf(v->cam_yaw),
        v->camera.target.y + v->cam_dist * sinf(v->cam_pitch),
        v->camera.target.z + v->cam_dist * cosf(v->cam_pitch) * cosf(v->cam_yaw)
    };

    BeginMode3D(v->camera);
    rlDisableBackfaceCulling();

    if (v->terrain && v->terrain->loaded)
        DrawModel(v->terrain->model, (Vector3){0, 0, 0}, 1.0f, WHITE);
    if (v->objects && v->objects->loaded)
        DrawModel(v->objects->model, (Vector3){0, 0, 0}, 1.0f, WHITE);

    // Player model or fallback cube
    ModelEntry *pe = (v->player_model && v->player_model->loaded) ? &v->player_model->entries[0] : NULL;
    if (pe && pe->loaded) {
        DrawModelEx(pe->model, (Vector3){px, py, pz}, (Vector3){0, 1, 0},
                    v->player_facing_angle, (Vector3){1, 1, 1}, WHITE);
    } else {
        DrawCube((Vector3){px, py + 1.0f, pz}, 0.8f, 2.0f, 0.8f, BLUE);
    }

    // NPC rendering — one entry per def. Each live NPC's def_id indexes into
    // g_npc_defs, and we look up the model by NPC cache ID in the model set.
    int npc_count = 0;
    const RcNpc *npcs = rc_get_npcs(v->world, &npc_count);
    for (int i = 0; i < npc_count; i++) {
        const RcNpc *n = &npcs[i];
        if (!n->active || n->is_dead) continue;
        if (n->plane != p->plane) continue;

        RcNpcDef *def = &g_npc_defs[n->def_id];

        // Interpolate position between prev and current
        float nwx = (float)n->prev_x + ((float)n->x - (float)n->prev_x) * v->tick_frac;
        float nwy = (float)n->prev_y + ((float)n->y - (float)n->prev_y) * v->tick_frac;
        float nx_r = (nwx - WORLD_ORIGIN_X) + 0.5f * (float)def->size;
        float nz_r = -((nwy - WORLD_ORIGIN_Y) + 0.5f * (float)def->size);
        float ny_r = ground_y(v, n->x, n->y);

        // Find the NPC's model by its cache ID
        ModelEntry *ne = NULL;
        if (v->npc_models && v->npc_models->loaded) {
            ne = model_find(v->npc_models, (uint32_t)def->id);
        }

        if (ne && ne->loaded) {
            // Face angle: if moving, face direction of movement; else face south
            float face_angle = 0.0f;
            int dx = n->x - n->prev_x;
            int dy = n->y - n->prev_y;
            if (dx || dy) {
                face_angle = atan2f((float)dx, -(float)dy) * (180.0f / 3.14159f);
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

    rlEnableBackfaceCulling();

    // Route markers (convert world→local for rendering)
    if (p->route_idx < p->route_len) {
        for (int i = p->route_idx; i < p->route_len; i++) {
            float rx = (float)LOCAL_X(p->route_x[i]) + 0.5f;
            float rz = -((float)LOCAL_Y(p->route_y[i]) + 0.5f);
            float ry = ground_y(v, p->route_x[i], p->route_y[i]) + 0.05f;
            DrawCube((Vector3){rx, ry, rz}, 0.3f, 0.05f, 0.3f, YELLOW);
        }
    }

    // Collision overlay (C key) — shows blocked tiles and wall flags
    if (v->show_collision) {
        // Show all tiles in the world
        for (int wx = WORLD_ORIGIN_X; wx < WORLD_ORIGIN_X + WORLD_W; wx++) {
            for (int wy = WORLD_ORIGIN_Y; wy < WORLD_ORIGIN_Y + WORLD_H; wy++) {
                uint32_t f = rc_get_flags(&v->world->map, wx, wy, 0);
                if (f == 0) continue;
                float tx = (float)LOCAL_X(wx) + 0.5f;
                float tz = -((float)LOCAL_Y(wy) + 0.5f);
                float ty = ground_y(v, wx, wy) + 0.1f;
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
        for (int x = 0; x <= WORLD_W; x += 8)
            DrawLine3D((Vector3){(float)x, 0.02f, 0}, (Vector3){(float)x, 0.02f, -(float)WORLD_H}, gc);
        for (int z = 0; z <= WORLD_H; z += 8)
            DrawLine3D((Vector3){0, 0.02f, -(float)z}, (Vector3){(float)WORLD_W, 0.02f, -(float)z}, gc);
    }

    EndMode3D();

    // HUD
    DrawRectangle(0, 0, WINDOW_W, 30, (Color){0, 0, 0, 180});
    DrawText(TextFormat("World: (%d, %d)  Local: (%d, %d)  Tick: %d  %s",
             p->x, p->y, LOCAL_X(p->x), LOCAL_Y(p->y),
             v->world->tick, p->running ? "RUN" : "WALK"),
             10, 7, 16, WHITE);
    DrawFPS(WINDOW_W - 80, 7);
    DrawText("Click: move | Right-drag: orbit | Scroll: zoom | R: run | L: lock | C: collision | G: grid | 4/5: presets",
             10, WINDOW_H - 18, 10, GRAY);
}

int main(void) {
    ViewerState v = {0};

    RcWorldConfig cfg = rc_preset_base_only();
    cfg.seed = 12345;
    v.world = rc_world_create_config(&cfg);
    if (!v.world) { fprintf(stderr, "Failed to create world\n"); return 1; }
    // Register all OSRS content modules (boss scripts, etc.). See
    // rc-content/README.md for the engine/content split.
    rc_content_register_all(v.world);

    v.world->player.x = PLAYER_START_X;
    v.world->player.y = PLAYER_START_Y;
    v.world->player.prev_x = PLAYER_START_X;
    v.world->player.prev_y = PLAYER_START_Y;
    v.prev_player_x = (float)PLAYER_START_X;
    v.prev_player_y = (float)PLAYER_START_Y;

    InitWindow(WINDOW_W, WINDOW_H, "RuneC - Varrock");
    SetTargetFPS(60);

    v.cam_yaw = 0; v.cam_pitch = 0.6f; v.cam_dist = 50;
    v.camera_locked = 1;
    v.camera.up = (Vector3){0, 1, 0};
    v.camera.fovy = 45;
    v.camera.projection = CAMERA_PERSPECTIVE;

    // No custom lighting shader — the export scripts already bake directional
    // lighting into vertex colors. Adding another pass just darkens everything.

    // Load world
    v.terrain = terrain_load("data/regions/varrock.terrain");
    if (v.terrain) terrain_offset(v.terrain, WORLD_ORIGIN_X, WORLD_ORIGIN_Y);

    v.objects = objects_load("data/regions/varrock.objects");
    if (v.objects) objects_offset(v.objects, WORLD_ORIGIN_X, WORLD_ORIGIN_Y);

    // Load collision
    collision_load(&v.world->map, "data/regions/varrock.cmap");

    // Load NPC definitions + spawns (must be before model loading since spawns
    // tell us which NPCs exist in the world)
    rc_load_npc_defs("data/defs/npc_defs.bin");
    rc_load_npc_spawns(v.world, "data/regions/varrock.npc-spawns.bin");

    // Load NPC models (combined body parts per NPC, one MDL2 entry per NPC def)
    uint32_t *npc_model_ids = calloc((size_t)v.world->npc_count, sizeof(uint32_t));
    int npc_model_id_count = 0;
    if (npc_model_ids) {
        npc_model_id_count = collect_spawned_npc_model_ids(
            v.world, npc_model_ids, v.world->npc_count, v.world->player.plane);
    }
    uint32_t empty_model_ids[1] = {0};
    const uint32_t *model_filter = npc_model_ids ? npc_model_ids : empty_model_ids;
    v.npc_models = models_load_filtered(
        "data/models/npcs.models", model_filter, npc_model_id_count);
    free(npc_model_ids);

    // NPC animations (separate cache — player.anims has combat/player anims,
    // npcs.anims has the subset referenced by our loaded NPC defs). Each
    // unique NPC def gets its own AnimModelState built from its base model's
    // per-vertex skin labels.
    v.npc_anims = anim_cache_load("data/anims/npcs.anims");
    if (v.npc_anims && v.npc_models && v.npc_models->loaded) {
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
    v.player_model = models_load("data/models/player.models");
    v.anims = anim_cache_load("data/anims/player.anims");
    if (!v.anims) v.anims = anim_cache_load("data/anims/all.anims");

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
        for (int x = 0; x < 64; x++)
            for (int y = 0; y < 64; y++)
                if (rc_get_flags(&v.world->map, 50*64+x, 53*64+y, 0) & 0x200000) blocked++;
        fprintf(stderr, "Collision check: region (50,53) has %d blocked tiles (expect ~578)\n", blocked);

        // Test a known blocked tile near player
        uint32_t f = rc_get_flags(&v.world->map, 3210, 3426, 0);
        int can = rc_can_move(&v.world->map, 3211, 3426, -1, 0, 0); // try move west into blocked
        fprintf(stderr, "Tile (3210,3426): flags=0x%08X, can_move_into=%d (expect 0)\n", f, can);
    }

    fprintf(stderr, "Viewer ready. Player at world (%d, %d), local (%d, %d)\n",
            v.world->player.x, v.world->player.y,
            LOCAL_X(v.world->player.x), LOCAL_Y(v.world->player.y));

    int max_frames = 0;
    const char *exit_frames = getenv("RC_VIEWER_EXIT_FRAMES");
    if (exit_frames) max_frames = atoi(exit_frames);
    int frame_count = 0;

    while (!WindowShouldClose()) {
        handle_input(&v);

        // Animation (runs every frame, independent of ticks)
        update_player_anim(&v);

        // Tick
        if (!v.paused) {
            v.tick_acc += GetFrameTime() * TPS;
            if (v.tick_acc >= 1.0f) {
                v.tick_acc -= 1.0f;
                v.prev_player_x = (float)v.world->player.x;
                v.prev_player_y = (float)v.world->player.y;

                int moved = 0;
                process_movement(&v, &moved);
                v.player_moving = moved;

                // Tick all active NPCs (wander AI, respawn, movement)
                for (int i = 0; i < v.world->npc_count; i++) {
                    if (v.world->npcs[i].active) {
                        rc_npc_tick(v.world, &v.world->npcs[i]);
                    }
                }

                v.world->tick++;
                v.tick_frac = 0.0f;
            }
            v.tick_frac = v.tick_acc;
            if (v.tick_frac > 1.0f) v.tick_frac = 1.0f;
        }

        BeginDrawing();
        ClearBackground((Color){40, 45, 55, 255});
        draw_scene(&v);
        EndDrawing();
        if (max_frames > 0 && ++frame_count >= max_frames) break;
    }

    terrain_free(v.terrain);
    objects_free(v.objects);
    models_free(v.player_model);
    models_free(v.npc_models);
    anim_model_state_free(v.anim_state);
    for (int i = 0; i < RC_MAX_NPC_DEFS; i++)
        anim_model_state_free(v.npc_anim_state[i]);
    anim_cache_free(v.anims);
    anim_cache_free(v.npc_anims);
    rc_world_destroy(v.world);
    CloseWindow();
    return 0;
}
