#include "object_runtime.h"
#include "storage.h"

#include "varbits.h"

#include <limits.h>
#include <string.h>

enum {
    RC_OBJECT_FLAG_HAS_ACTIONS = 1u << 4,
    RC_OBJECT_FLAG_SOLID = 1u << 5,
    RC_OBJECT_FLAG_IMPENETRABLE = 1u << 6,
};

static uint32_t *map_tile_flags(RcWorldMap *map, int x, int y, int plane) {
    if (!map || !rc_world_tile_valid(x, y, plane)) return NULL;
    int region_x = x / RC_REGION_SIZE;
    int region_y = y / RC_REGION_SIZE;
    int local_x = x % RC_REGION_SIZE;
    int local_y = y % RC_REGION_SIZE;
    for (int i = 0; i < map->region_count; i++) {
        RcRegion *region = &map->regions[i];
        if (region->loaded && region->region_x == region_x
                && region->region_y == region_y) {
            return &region->tiles[plane][local_x][local_y].collision_flags;
        }
    }
    return NULL;
}

static void change_tile_flags(RcWorld *world, int x, int y, int plane,
                              uint32_t flags, int add) {
    uint32_t *tile = map_tile_flags(world ? &world->map : NULL, x, y, plane);
    if (!tile) return;
    if (add) *tile |= flags;
    else *tile &= ~flags;
}

static uint32_t wall_flags(uint32_t walk, int projectile) {
    return walk | (projectile ? ((walk & 0xffu) << 9) : 0u);
}

static void change_wall(RcWorld *world, const RcObjectPlacement *object,
                        int add, int projectile) {
#define CHANGE(dx, dy, flags) \
    change_tile_flags(world, object->x + (dx), object->y + (dy), \
                      object->plane, wall_flags((flags), projectile), add)
    int rotation = object->rotation & 3;
    int type = object->type;
    if (type == 0 || type == 3) {
        if (rotation == 0) {
            CHANGE(0, 0, COL_WALL_W); CHANGE(-1, 0, COL_WALL_E);
        } else if (rotation == 1) {
            CHANGE(0, 0, COL_WALL_N); CHANGE(0, 1, COL_WALL_S);
        } else if (rotation == 2) {
            CHANGE(0, 0, COL_WALL_E); CHANGE(1, 0, COL_WALL_W);
        } else {
            CHANGE(0, 0, COL_WALL_S); CHANGE(0, -1, COL_WALL_N);
        }
    } else if (type == 2) {
        if (rotation == 0) {
            CHANGE(0, 0, COL_WALL_W | COL_WALL_N);
            CHANGE(-1, 0, COL_WALL_E); CHANGE(0, 1, COL_WALL_S);
        } else if (rotation == 1) {
            CHANGE(0, 0, COL_WALL_E | COL_WALL_N);
            CHANGE(1, 0, COL_WALL_W); CHANGE(0, 1, COL_WALL_S);
        } else if (rotation == 2) {
            CHANGE(0, 0, COL_WALL_E | COL_WALL_S);
            CHANGE(1, 0, COL_WALL_W); CHANGE(0, -1, COL_WALL_N);
        } else {
            CHANGE(0, 0, COL_WALL_W | COL_WALL_S);
            CHANGE(-1, 0, COL_WALL_E); CHANGE(0, -1, COL_WALL_N);
        }
    } else if (type == 1) {
        if (rotation == 0) {
            CHANGE(0, 0, COL_WALL_NW); CHANGE(-1, 1, COL_WALL_SE);
        } else if (rotation == 1) {
            CHANGE(0, 0, COL_WALL_NE); CHANGE(1, 1, COL_WALL_SW);
        } else if (rotation == 2) {
            CHANGE(0, 0, COL_WALL_SE); CHANGE(1, -1, COL_WALL_NW);
        } else {
            CHANGE(0, 0, COL_WALL_SW); CHANGE(-1, -1, COL_WALL_NE);
        }
    }
#undef CHANGE
}

static void change_footprint(RcWorld *world,
                             const RcObjectPlacement *object,
                             const RcObjectDef *def, int add,
                             int projectile) {
    int width = def->width > 0 ? def->width : 1;
    int length = def->length > 0 ? def->length : 1;
    if (object->rotation & 1u) {
        int swap = width;
        width = length;
        length = swap;
    }
    uint32_t flags = COL_BLOCK_WALK;
    if (projectile) flags |= COL_PROJ_BLOCK_FULL;
    for (int dx = 0; dx < width; dx++) {
        for (int dy = 0; dy < length; dy++) {
            change_tile_flags(world, object->x + dx, object->y + dy,
                              object->plane, flags, add);
        }
    }
}

static void change_object_collision(RcWorld *world,
                                    const RcObjectPlacement *object,
                                    int add) {
    if (!world || !object || object->obj_id >= RC_MAX_OBJECT_ID) return;
    const RcObjectDef *def = rc_object_def_get((int)object->obj_id);
    // Interact type 0 (cache opcode 17) never clips, though the solid flag
    // stays set: the static collision export skips such locs too, as
    // rsmod's toggleLoc skips blockWalk 0. Vorkath's acid pool is one.
    if (!def || !(def->flags & RC_OBJECT_FLAG_SOLID) || def->interact_type == 0)
        return;
    int projectile = (def->flags & RC_OBJECT_FLAG_IMPENETRABLE) != 0
                  || (def->clip_flags & RC_OBJECT_CLIP_BLOCKS_PROJECTILE) != 0;
    if (object->type >= 0 && object->type <= 3) {
        change_wall(world, object, add, projectile);
    } else if (object->type == 22) {
        if (def->flags & RC_OBJECT_FLAG_HAS_ACTIONS)
            change_footprint(world, object, def, add, 0);
    } else if (object->type == 9 || object->type == 10
            || object->type == 11 || object->type >= 12) {
        change_footprint(world, object, def, add, projectile);
    }
}

static int state_to_active_placement(const RcObjectState *state,
                                     RcObjectPlacement *out) {
    if (!state || !out || state->active_obj_id < 0
            || !rc_world_tile_valid(state->active_x, state->active_y,
                                    state->active_plane)) {
        return 0;
    }
    uint16_t mapsquare;
    if (!rc_world_to_mapsquare(state->active_x, state->active_y,
                               &mapsquare, NULL, NULL)) {
        return 0;
    }
    *out = (RcObjectPlacement){
        .obj_id = (uint32_t)state->active_obj_id,
        .key = state->placement_key,
        .x = (uint16_t)state->active_x,
        .y = (uint16_t)state->active_y,
        .mapsquare = mapsquare,
        .plane = (uint8_t)state->active_plane,
        .type = state->active_type,
        .rotation = state->active_rotation,
    };
    return 1;
}

static int state_to_base_placement(const RcObjectState *state,
                                   RcObjectPlacement *out) {
    if (!state || !out || state->base_obj_id < 0
            || !rc_world_tile_valid(state->x, state->y, state->plane)) {
        return 0;
    }
    uint16_t mapsquare;
    if (!rc_world_to_mapsquare(state->x, state->y, &mapsquare, NULL, NULL))
        return 0;
    *out = (RcObjectPlacement){
        .obj_id = (uint32_t)state->base_obj_id,
        .key = state->placement_key,
        .x = (uint16_t)state->x,
        .y = (uint16_t)state->y,
        .mapsquare = mapsquare,
        .plane = (uint8_t)state->plane,
        .type = state->base_type,
        .rotation = state->base_rotation,
    };
    return 1;
}

RcObjectState *rc_world_object_state_find_key(RcWorld *world, uint64_t key) {
    if (!world || !key) return NULL;
    for (int i = 0; i < world->object_state_count; i++) {
        if (world->object_states[i].placement_key == key)
            return &world->object_states[i];
    }
    return NULL;
}

const RcObjectState *rc_world_object_state_find_key_const(
    const RcWorld *world, uint64_t key) {
    return rc_world_object_state_find_key((RcWorld *)world, key);
}

int rc_world_object_state_matches(const RcObjectState *state, int obj_id,
                                  int x, int y, int plane) {
    if (!state) return 0;
    int base = state->base_obj_id == obj_id && state->x == x
            && state->y == y && state->plane == plane;
    int active = state->active_obj_id == obj_id && state->active_x == x
              && state->active_y == y && state->active_plane == plane;
    return base || active;
}

RcObjectState *rc_world_object_state_find(RcWorld *world, int obj_id,
                                          int x, int y, int plane) {
    if (!world || !rc_world_tile_valid(x, y, plane)) return NULL;
    RcObjectState *match = NULL;
    for (int i = 0; i < world->object_state_count; i++) {
        RcObjectState *state = &world->object_states[i];
        if (!rc_world_object_state_matches(state, obj_id, x, y, plane))
            continue;
        if (match) return NULL;
        match = state;
    }
    return match;
}

const RcObjectState *rc_world_object_state_find_const(
    const RcWorld *world, int obj_id, int x, int y, int plane) {
    return rc_world_object_state_find((RcWorld *)world, obj_id, x, y, plane);
}

static int object_transform_selector(const RcWorld *world,
                                     const RcObjectDef *def) {
    if (!world || !def) return -1;
    if (def->varbit >= 0) return (int)rc_varbit_get(world, def->varbit);
    if (def->varp >= 0 && def->varp < RC_MAX_VARPS)
        return world->varps[def->varp];
    return -1;
}

const RcObjectDef *rc_world_object_def_resolve(const RcWorld *world,
                                               int base_obj_id,
                                               int *out_obj_id) {
    int obj_id = base_obj_id;
    for (int depth = 0; depth < 32; depth++) {
        const RcObjectDef *def = rc_object_def_get(obj_id);
        if (!def) return NULL;
        if (def->transform_count == 0) {
            if (out_obj_id) *out_obj_id = obj_id;
            return def;
        }
        int selector = object_transform_selector(world, def);
        int index = selector >= 0 && selector < def->transform_count - 1
                  ? selector : def->transform_count - 1;
        int next_id;
        if (!rc_object_def_transform_at(def, index, &next_id)
                || next_id < 0 || next_id == obj_id) {
            return NULL;
        }
        obj_id = next_id;
    }
    return NULL;
}

int rc_world_object_resolve_placement(const RcWorld *world,
                                      const RcObjectPlacement *base,
                                      RcObjectPlacement *out) {
    if (!base || !out) return 0;
    const RcObjectState *state = base->key
        ? rc_world_object_state_find_key_const(world, base->key) : NULL;
    RcObjectPlacement current = *base;
    if (state) {
        if (!state_to_active_placement(state, &current)) return 0;
    }
    int effective_id;
    if (!rc_world_object_def_resolve(world, (int)current.obj_id,
                                     &effective_id)) {
        return 0;
    }
    current.obj_id = (uint32_t)effective_id;
    *out = current;
    return 1;
}

int rc_world_object_current_placement(const RcWorld *world, int obj_id,
                                      int x, int y, int plane,
                                      uint64_t placement_key,
                                      RcObjectPlacement *out) {
    if (!out || !rc_world_tile_valid(x, y, plane)) return 0;
    if (placement_key) {
        const RcObjectState *state =
            rc_world_object_state_find_key_const(world, placement_key);
        if (state) {
            RcObjectPlacement active;
            if (!state_to_active_placement(state, &active)
                    || !rc_world_object_resolve_placement(world, &active,
                                                          out)) {
                return 0;
            }
            return obj_id < 0 || (int)out->obj_id == obj_id;
        }
        RcObjectPlacement base;
        if (!rc_object_placement_find_key(placement_key, x, y, plane, &base)
                || !rc_world_object_resolve_placement(world, &base, out)) {
            return 0;
        }
        return obj_id < 0 || (int)out->obj_id == obj_id;
    }

    uint16_t mapsquare;
    if (!rc_world_to_mapsquare(x, y, &mapsquare, NULL, NULL)) return 0;
    int count = 0;
    int matches = 0;
    RcObjectPlacement resolved = {0};
    for (int i = 0; world && i < world->object_state_count; i++) {
        RcObjectPlacement candidate;
        int effective_id = -1;
        if (!state_to_active_placement(&world->object_states[i], &candidate)
                || candidate.x != x || candidate.y != y
                || candidate.plane != plane
                || !rc_world_object_def_resolve(
                    world, (int)candidate.obj_id, &effective_id)
                || (obj_id >= 0 && effective_id != obj_id)) {
            continue;
        }
        candidate.obj_id = (uint32_t)effective_id;
        resolved = candidate;
        matches++;
    }
    const RcObjectPlacement *rows = rc_object_region_placements(
        mapsquare, &count);
    for (int i = 0; rows && i < count; i++) {
        if (rows[i].x != x || rows[i].y != y || rows[i].plane != plane)
            continue;
        if (rows[i].key
                && rc_world_object_state_find_key_const(world, rows[i].key)) {
            continue;
        }
        RcObjectPlacement candidate;
        if (!rc_world_object_resolve_placement(world, &rows[i], &candidate)
                || candidate.x != x || candidate.y != y
                || candidate.plane != plane
                || (obj_id >= 0 && (int)candidate.obj_id != obj_id)) {
            continue;
        }
        resolved = candidate;
        matches++;
    }
    if (matches != 1) return 0;
    *out = resolved;
    return 1;
}

static void schedule_next_change(RcWorld *world) {
    world->next_object_change_tick = 0;
    for (int i = 0; i < world->object_state_count; i++) {
        RcTick tick = world->object_states[i].revert_tick;
        if (tick > 0 && (world->next_object_change_tick == 0
                || tick < world->next_object_change_tick)) {
            world->next_object_change_tick = tick;
        }
    }
}

static int valid_placement(const RcObjectPlacement *object) {
    return object && object->obj_id < RC_MAX_OBJECT_ID
        && rc_world_tile_valid(object->x, object->y, object->plane)
        && rc_object_layer_for_type(object->type) >= 0;
}

static RcTick expiry_tick(const RcWorld *world, RcTick duration) {
    if (!duration) return 0;
    if (UINT64_MAX - world->tick < duration) return UINT64_MAX;
    return world->tick + duration;
}

static void set_state_active(RcObjectState *state,
                             const RcObjectPlacement *replacement) {
    if (!replacement) {
        state->active_obj_id = -1;
        state->active_x = state->x;
        state->active_y = state->y;
        state->active_plane = state->plane;
        state->active_type = state->base_type;
        state->active_rotation = state->base_rotation;
        state->flags |= RC_OBJECT_STATE_HIDDEN;
        return;
    }
    state->active_obj_id = (int)replacement->obj_id;
    state->active_x = replacement->x;
    state->active_y = replacement->y;
    state->active_plane = replacement->plane;
    state->active_type = replacement->type;
    state->active_rotation = replacement->rotation;
    state->flags &= (uint8_t)~RC_OBJECT_STATE_HIDDEN;
}

RcObjectMutationResult rc_world_object_replace(
    RcWorld *world, const RcObjectPlacement *base,
    const RcObjectPlacement *replacement, RcTick duration,
    uint8_t state_flags) {
    if (!world || !valid_placement(base) || !base->key
            || (replacement && !valid_placement(replacement))
            || (replacement && rc_object_layer_for_type(replacement->type)
                            != rc_object_layer_for_type(base->type))) {
        return RC_OBJECT_MUTATION_INVALID;
    }
    RcObjectState *state = rc_world_object_state_find_key(world, base->key);
    RcObjectPlacement exact;
    if (!state && (!rc_object_placement_find_key(
                        base->key, base->x, base->y, base->plane, &exact)
            || exact.obj_id != base->obj_id || exact.type != base->type
            || exact.rotation != base->rotation)) {
        return RC_OBJECT_MUTATION_NOT_FOUND;
    }
    if (!state && world->object_state_count >= RC_MAX_OBJECT_STATES)
        return RC_OBJECT_MUTATION_CAPACITY;

    RcObjectPlacement old;
    if (state) {
        if (state->base_obj_id != (int)base->obj_id
                || state->x != base->x || state->y != base->y
                || state->plane != base->plane
                || state->base_type != base->type
                || state->base_rotation != base->rotation
                || state->layer != rc_object_layer_for_type(base->type)) {
            return RC_OBJECT_MUTATION_CONFLICT;
        }
        if (state_to_active_placement(state, &old))
            change_object_collision(world, &old, 0);
    } else {
        state = &world->object_states[world->object_state_count++];
        memset(state, 0, sizeof(*state));
        state->placement_key = base->key;
        state->base_obj_id = (int)base->obj_id;
        state->x = base->x;
        state->y = base->y;
        state->plane = base->plane;
        state->base_type = base->type;
        state->base_rotation = base->rotation;
        state->layer = (uint8_t)rc_object_layer_for_type(base->type);
        change_object_collision(world, base, 0);
    }
    state->flags = (uint8_t)(RC_OBJECT_STATE_DYNAMIC | state_flags);
    if (world->player.storage_session
            && world->player.storage_source.kind == RC_INTERACTION_OBJECT
            && world->player.storage_source.placement_key == base->key)
        rc_storage_close(world);
    set_state_active(state, replacement);
    state->revert_tick = expiry_tick(world, duration);
    if (replacement) change_object_collision(world, replacement, 1);
    schedule_next_change(world);
    return RC_OBJECT_MUTATION_OK;
}

RcObjectMutationResult rc_world_object_delete(
    RcWorld *world, const RcObjectPlacement *base, RcTick duration,
    uint8_t state_flags) {
    return rc_world_object_replace(world, base, NULL, duration, state_flags);
}

static uint64_t next_dynamic_key(RcWorld *world) {
    if (!world->next_dynamic_object_key)
        world->next_dynamic_object_key = UINT64_MAX;
    for (int i = 0; i <= RC_MAX_OBJECT_STATES; i++) {
        uint64_t key = world->next_dynamic_object_key--;
        if (key && !rc_world_object_state_find_key(world, key)) return key;
    }
    return 0;
}

RcObjectMutationResult rc_world_object_add(
    RcWorld *world, const RcObjectPlacement *object, RcTick duration,
    uint64_t *out_key) {
    if (!world || !valid_placement(object))
        return RC_OBJECT_MUTATION_INVALID;
    int layer = rc_object_layer_for_type(object->type);
    for (int i = 0; i < world->object_state_count; i++) {
        const RcObjectState *state = &world->object_states[i];
        if (state->active_x == object->x && state->active_y == object->y
                && state->active_plane == object->plane
                && state->layer == layer) {
            return RC_OBJECT_MUTATION_CONFLICT;
        }
    }
    RcObjectPlacement base;
    int found = rc_object_placement_find_layer(
        object->x, object->y, object->plane, layer, &base);
    if (found < 0) return RC_OBJECT_MUTATION_CONFLICT;
    if (found > 0) {
        RcObjectMutationResult result = rc_world_object_replace(
            world, &base, object, duration, 0);
        if (result == RC_OBJECT_MUTATION_OK && out_key) *out_key = base.key;
        return result;
    }
    if (world->object_state_count >= RC_MAX_OBJECT_STATES)
        return RC_OBJECT_MUTATION_CAPACITY;
    uint64_t key = next_dynamic_key(world);
    if (!key) return RC_OBJECT_MUTATION_CAPACITY;
    RcObjectState *state = &world->object_states[world->object_state_count++];
    memset(state, 0, sizeof(*state));
    state->placement_key = key;
    state->base_obj_id = -1;
    state->active_obj_id = (int)object->obj_id;
    state->x = object->x;
    state->y = object->y;
    state->plane = object->plane;
    state->active_x = object->x;
    state->active_y = object->y;
    state->active_plane = object->plane;
    state->base_type = object->type;
    state->base_rotation = object->rotation;
    state->active_type = object->type;
    state->active_rotation = object->rotation;
    state->layer = (uint8_t)layer;
    state->flags = RC_OBJECT_STATE_DYNAMIC | RC_OBJECT_STATE_SPAWNED;
    state->revert_tick = expiry_tick(world, duration);
    change_object_collision(world, object, 1);
    schedule_next_change(world);
    if (out_key) *out_key = key;
    return RC_OBJECT_MUTATION_OK;
}

RcObjectMutationResult rc_world_object_revert(RcWorld *world, uint64_t key) {
    if (!world || !key) return RC_OBJECT_MUTATION_INVALID;
    int index = -1;
    for (int i = 0; i < world->object_state_count; i++) {
        if (world->object_states[i].placement_key == key) {
            index = i;
            break;
        }
    }
    if (index < 0) return RC_OBJECT_MUTATION_NOT_FOUND;
    if (world->player.storage_session
            && world->player.storage_source.kind == RC_INTERACTION_OBJECT
            && world->player.storage_source.placement_key == key)
        rc_storage_close(world);
    RcObjectState *state = &world->object_states[index];
    RcObjectPlacement object;
    if (state_to_active_placement(state, &object))
        change_object_collision(world, &object, 0);
    if (state_to_base_placement(state, &object))
        change_object_collision(world, &object, 1);
    int remaining = world->object_state_count - index - 1;
    if (remaining > 0) {
        memmove(&world->object_states[index], &world->object_states[index + 1],
                (size_t)remaining * sizeof(world->object_states[0]));
    }
    world->object_state_count--;
    memset(&world->object_states[world->object_state_count], 0,
           sizeof(world->object_states[0]));
    schedule_next_change(world);
    return RC_OBJECT_MUTATION_OK;
}

void rc_world_objects_tick(RcWorld *world) {
    if (!world || !world->next_object_change_tick
            || world->tick < world->next_object_change_tick) {
        return;
    }
    int i = 0;
    while (i < world->object_state_count) {
        RcObjectState *state = &world->object_states[i];
        if (state->revert_tick > 0 && state->revert_tick <= world->tick) {
            uint64_t key = state->placement_key;
            (void)rc_world_object_revert(world, key);
        } else {
            i++;
        }
    }
    schedule_next_change(world);
}

int rc_world_objects_replay_collision(RcWorld *world) {
    if (!world) return 0;
    for (int i = 0; i < world->object_state_count; i++) {
        RcObjectPlacement object;
        if (state_to_base_placement(&world->object_states[i], &object))
            change_object_collision(world, &object, 0);
        if (state_to_active_placement(&world->object_states[i], &object))
            change_object_collision(world, &object, 1);
    }
    return 1;
}

int rc_world_object_active_id(const RcWorld *world, int obj_id, int x, int y,
                              int plane) {
    const RcObjectState *match = NULL;
    for (int i = 0; world && i < world->object_state_count; i++) {
        const RcObjectState *state = &world->object_states[i];
        if (!rc_world_object_state_matches(state, obj_id, x, y, plane))
            continue;
        if (match) return -1;
        match = state;
    }
    if (match) return match->active_obj_id;
    int resolved_id = obj_id;
    return rc_world_object_def_resolve(world, obj_id, &resolved_id)
         ? resolved_id : -1;
}

int rc_world_object_active_state(const RcWorld *world, int obj_id, int x,
                                 int y, int plane, RcObjectState *out) {
    if (!out) return 0;
    const RcObjectState *state = rc_world_object_state_find_const(
        world, obj_id, x, y, plane);
    if (!state) return 0;
    *out = *state;
    return 1;
}

int rc_world_object_active_state_layer(const RcWorld *world, int obj_id,
                                       int x, int y, int plane, int layer,
                                       RcObjectState *out) {
    if (!world || !out || layer < 0 || layer > 3) return 0;
    const RcObjectState *match = NULL;
    for (int i = 0; i < world->object_state_count; i++) {
        const RcObjectState *state = &world->object_states[i];
        if (state->layer != layer
                || !rc_world_object_state_matches(
                    state, obj_id, x, y, plane)) {
            continue;
        }
        if (match) return 0;
        match = state;
    }
    if (!match) return 0;
    *out = *match;
    return 1;
}

int rc_world_object_active_state_by_key(const RcWorld *world,
                                        uint64_t placement_key,
                                        RcObjectState *out) {
    if (!out) return 0;
    const RcObjectState *state = rc_world_object_state_find_key_const(
        world, placement_key);
    if (!state) return 0;
    *out = *state;
    return 1;
}
