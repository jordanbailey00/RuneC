#include "api.h"
#include "config.h"
#include "drops.h"
#include "events.h"
#include "encounter.h"
#include "interaction.h"
#include "items.h"
#include "npc.h"
#include "combat.h"
#include "combat_hit.h"
#include "prayer.h"
#include "skills.h"
#include "pathfinding.h"
#include "objects.h"
#include "player_actions.h"
#include "spells.h"
#include "storage.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define RC_OBJECT_DOOR_REVERT_TICKS 100
#define RC_BLOCK_ACCESS_NORTH 0x1
#define RC_BLOCK_ACCESS_EAST  0x2
#define RC_BLOCK_ACCESS_SOUTH 0x4
#define RC_BLOCK_ACCESS_WEST  0x8

// Stub input processing — will be filled in as systems are built
static void process_player_input(RcWorld *world) {
    (void)world;
}

static void spawn_npc_loot(RcWorld *world, const RcNpc *npc) {
    if (!world || !npc || !(world->enabled & RC_SUB_LOOT)) return;
    if (npc->def_id < 0 || npc->def_id >= g_npc_def_count) return;
    int npc_id = g_npc_defs[npc->def_id].id;
    RcLootDrop drops[RC_MAX_LOOT_DROPS];
    int count = rc_roll_npc_loot(world, npc_id, drops, RC_MAX_LOOT_DROPS);
    for (int i = 0; i < count; i++) {
        rc_ground_item_spawn(world, drops[i].item_id, drops[i].quantity,
                             npc->x, npc->y, npc->plane,
                             RC_GROUND_OWNER_LOCAL_PLAYER);
    }
}

static RcNpc *api_find_npc_by_uid(RcWorld *world, int uid);
static int api_option_from_interaction_op(RcInteractionOp op);
static int api_npc_attack_option_index(const RcNpcDef *def);
static void api_register_default_npc_handlers(void);
static void api_register_default_object_handlers(void);
static void api_register_default_ground_item_handlers(void);
static void api_set_player_route(RcPlayer *p, const RcRoute *route);
static void process_player_interaction(RcWorld *world, int dispatch_ready);
static RcObjectState *object_state_find(RcWorld *world, int obj_id,
                                        int x, int y, int plane);
static const RcObjectState *object_state_find_const(const RcWorld *world,
                                                    int obj_id, int x, int y,
                                                    int plane);
static RcObjectState *object_state_find_by_key(RcWorld *world,
                                               uint64_t placement_key);
static const RcObjectState *object_state_find_by_key_const(
    const RcWorld *world, uint64_t placement_key);
static int object_state_matches_target(const RcObjectState *st, int obj_id,
                                       int x, int y, int plane);
static int object_exact_placement(int obj_id, int x, int y, int plane,
                                  RcObjectPlacement *out);
static int object_exact_placement_key(int obj_id, int x, int y, int plane,
                                      uint64_t placement_key,
                                      RcObjectPlacement *out);
static int current_object_placement(const RcWorld *world, int obj_id,
                                    int x, int y, int plane,
                                    RcObjectPlacement *out);
static int current_object_placement_key(const RcWorld *world, int obj_id,
                                        int x, int y, int plane,
                                        uint64_t placement_key,
                                        RcObjectPlacement *out);
static int placement_dimensions(const RcObjectDef *def,
                                const RcObjectPlacement *placement,
                                int *out_w, int *out_l);
static int tile_reaches_object_target(const RcWorld *world,
                                      const RcInteractionTarget *target,
                                      int tx, int ty);
static const RcTraversalEdge *object_traversal_edge(
    const RcWorld *world, int obj_id, int x, int y, int plane, int opt,
    uint64_t placement_key);
static int object_option_available(const RcObjectDef *def,
                                   const RcObjectBehavior *behavior,
                                   const RcTraversalEdge *edge, int opt);
static int string_has_ci(const char *s, const char *needle);
static void apply_door_collision(RcWorld *world, int obj_id, int x, int y,
                                 int plane, int open);
static void start_player_action_lock(RcWorld *world, int lock_ticks);
static void schedule_player_traversal(RcWorld *world,
                                      const RcTraversalEdge *edge,
                                      int delay_ticks);

static void process_player_route(RcWorld *world) {
    process_player_interaction(world, 0);
}

static void process_player_action_timers(RcWorld *world) {
    if (!world) return;
    RcPlayer *p = &world->player;
    if (p->pending_traversal_active
            && world->tick >= p->pending_traversal_tick) {
        p->prev_x = p->x;
        p->prev_y = p->y;
        p->x = p->pending_traversal_x;
        p->y = p->pending_traversal_y;
        p->plane = p->pending_traversal_plane;
        p->route_len = 0;
        p->route_idx = 0;
        p->pending_traversal_active = 0;
        p->pending_traversal_x = -1;
        p->pending_traversal_y = -1;
        p->pending_traversal_plane = -1;
        p->action_lock_timer = 0;
    }
    if (p->action_lock_timer > 0)
        p->action_lock_timer--;
}

static void process_player_movement(RcWorld *world) {
    if (!world) return;
    RcPlayer *p = &world->player;
    p->prev_x = p->x;
    p->prev_y = p->y;
    if (p->action_lock_timer > 0) return;
    if (p->freeze_timer > 0) return;
    if (p->route_idx >= p->route_len) return;
    int steps = p->running ? 2 : 1;
    for (int s = 0; s < steps && p->route_idx < p->route_len; s++) {
        int nx = p->route_x[p->route_idx];
        int ny = p->route_y[p->route_idx];
        int dx = nx - p->x;
        int dy = ny - p->y;
        if (dx > 1) dx = 1;
        if (dx < -1) dx = -1;
        if (dy > 1) dy = 1;
        if (dy < -1) dy = -1;
        if (!dx && !dy) {
            p->route_idx++;
            continue;
        }
        if (!rc_can_move(&world->map, p->x, p->y, dx, dy, p->plane)) {
            p->route_len = 0;
            p->route_idx = 0;
            return;
        }
        p->x += dx;
        p->y += dy;
        if (p->x == nx && p->y == ny) p->route_idx++;
    }
}

static void nearest_npc_tile_to_player(const RcNpc *npc, const RcPlayer *p,
                                       int *tx, int *ty) {
    const RcNpcDef *def = npc->def_id >= 0 && npc->def_id < g_npc_def_count
                        ? &g_npc_defs[npc->def_id] : NULL;
    int size = def && def->size > 0 ? def->size : 1;
    int min_x = npc->x;
    int min_y = npc->y;
    int max_x = npc->x + size - 1;
    int max_y = npc->y + size - 1;
    if (p->x < min_x) *tx = min_x;
    else if (p->x > max_x) *tx = max_x;
    else *tx = p->x;
    if (p->y < min_y) *ty = min_y;
    else if (p->y > max_y) *ty = max_y;
    else *ty = p->y;
}

static int player_distance_to_npc(const RcPlayer *p, const RcNpc *npc) {
    if (!p || !npc || p->plane != npc->plane) return INT_MAX;
    int tx, ty;
    nearest_npc_tile_to_player(npc, p, &tx, &ty);
    int dx = p->x - tx;
    int dy = p->y - ty;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx > dy ? dx : dy;
}

static void face_player_to_npc(RcPlayer *p, const RcNpc *npc) {
    int tx, ty;
    nearest_npc_tile_to_player(npc, p, &tx, &ty);
    p->facing_entity = npc->uid;
    p->facing_x = tx;
    p->facing_y = ty;
}

static int npc_interaction_has_los(const RcWorld *world, const RcPlayer *p,
                                   const RcNpc *npc, int range) {
    if (!p || !npc || p->plane != npc->plane) return 0;
    if (range <= 1) return 1;
    int tx, ty;
    nearest_npc_tile_to_player(npc, p, &tx, &ty);
    return rc_has_los(&world->map, p->x, p->y, tx, ty, p->plane);
}

static void route_player_toward_interaction_npc(RcWorld *world, RcPlayer *p,
                                                const RcNpc *npc, int range) {
    if (!p || !npc || p->plane != npc->plane) return;
    const RcNpcDef *def = npc->def_id >= 0 && npc->def_id < g_npc_def_count
                        ? &g_npc_defs[npc->def_id] : NULL;
    int size = def && def->size > 0 ? def->size : 1;
    int min_x = npc->x;
    int min_y = npc->y;
    int max_x = npc->x + size - 1;
    int max_y = npc->y + size - 1;
    int tx = p->x;
    int ty = p->y;
    if (p->x < min_x) tx = min_x - range;
    else if (p->x > max_x) tx = max_x + range;
    if (p->y < min_y) ty = min_y - range;
    else if (p->y > max_y) ty = max_y + range;
    RcRoute route = rc_find_path(&world->map, p->x, p->y, tx, ty,
                                 1, p->plane, true);
    api_set_player_route(p, &route);
    p->interaction.flags |= RC_INTERACTION_MOVED;
}

static void nearest_target_tile_to_player(const RcInteractionTarget *target,
                                          const RcPlayer *p, int *tx,
                                          int *ty) {
    int min_x = target->tile_x;
    int min_y = target->tile_y;
    int max_x = target->tile_x + target->footprint_width - 1;
    int max_y = target->tile_y + target->footprint_height - 1;
    if (p->x < min_x) *tx = min_x;
    else if (p->x > max_x) *tx = max_x;
    else *tx = p->x;
    if (p->y < min_y) *ty = min_y;
    else if (p->y > max_y) *ty = max_y;
    else *ty = p->y;
}

static int player_distance_to_target(const RcPlayer *p,
                                     const RcInteractionTarget *target) {
    if (!p || !target || p->plane != target->plane) return INT_MAX;
    int tx, ty;
    nearest_target_tile_to_player(target, p, &tx, &ty);
    int dx = p->x - tx;
    int dy = p->y - ty;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx > dy ? dx : dy;
}

static int player_has_active_route(const RcPlayer *p) {
    return p && p->route_idx < p->route_len;
}

static void face_player_to_target(RcPlayer *p,
                                  const RcInteractionTarget *target) {
    int tx, ty;
    nearest_target_tile_to_player(target, p, &tx, &ty);
    p->facing_entity = -1;
    p->facing_x = tx;
    p->facing_y = ty;
}

static int tile_distance_to_target_rect(int x, int y, int min_x, int min_y,
                                        int max_x, int max_y) {
    int dx = 0;
    int dy = 0;
    if (x < min_x) dx = min_x - x;
    else if (x > max_x) dx = x - max_x;
    if (y < min_y) dy = min_y - y;
    else if (y > max_y) dy = y - max_y;
    return dx > dy ? dx : dy;
}

static int route_player_toward_interaction_target(RcWorld *world, RcPlayer *p,
                                                  const RcInteractionTarget *t,
                                                  int range) {
    if (!world || !p || !t || p->plane != t->plane) return 0;
    if (range < 0) range = 0;
    if (t->kind == RC_INTERACTION_OBJECT) {
        int opt = api_option_from_interaction_op(p->interaction.op);
        const RcTraversalEdge *edge = object_traversal_edge(
            world, t->definition_id, t->tile_x, t->tile_y, t->plane, opt,
            t->placement_key);
        if (edge && edge->start_plane == p->plane
                && edge->start_x != 0xFFFFu && edge->start_y != 0xFFFFu) {
            if (p->x == (int)edge->start_x && p->y == (int)edge->start_y)
                return 1;
            RcRoute route = rc_find_path(&world->map, p->x, p->y,
                                         edge->start_x, edge->start_y, 1,
                                         p->plane, false);
            if (!route.success || route.length <= 0)
                return 0;
            api_set_player_route(p, &route);
            p->interaction.flags |= RC_INTERACTION_MOVED;
            return 1;
        }
    }
    int min_x = t->tile_x;
    int min_y = t->tile_y;
    int max_x = t->tile_x + t->footprint_width - 1;
    int max_y = t->tile_y + t->footprint_height - 1;
    if (t->footprint_width <= 0) max_x = min_x;
    if (t->footprint_height <= 0) max_y = min_y;

    RcRoute best_route = {0};
    int best_len = INT_MAX;
    int best_dist = INT_MAX;
    int found = 0;
    for (int tx = min_x - range; tx <= max_x + range; tx++) {
        for (int ty = min_y - range; ty <= max_y + range; ty++) {
            int dist = tile_distance_to_target_rect(tx, ty, min_x, min_y,
                                                    max_x, max_y);
            if (dist > range) continue;
            if (range > 0 && dist == 0) {
                int allow_inside = 0;
                if (t->kind == RC_INTERACTION_OBJECT) {
                    RcObjectPlacement placement;
                    allow_inside = current_object_placement_key(
                        world, t->definition_id, t->tile_x, t->tile_y,
                        t->plane, t->placement_key, &placement)
                        && placement.type <= 3;
                }
                if (!allow_inside)
                    continue;
            }
            if (t->kind == RC_INTERACTION_OBJECT &&
                    !tile_reaches_object_target(world, t, tx, ty)) {
                continue;
            }
            RcRoute route = rc_find_path(&world->map, p->x, p->y, tx, ty,
                                         1, p->plane, false);
            if (!route.success || route.length <= 0) continue;
            int player_dx = p->x - tx;
            int player_dy = p->y - ty;
            if (player_dx < 0) player_dx = -player_dx;
            if (player_dy < 0) player_dy = -player_dy;
            int player_dist = player_dx > player_dy ? player_dx : player_dy;
            if (!found || route.length < best_len ||
                    (route.length == best_len && player_dist < best_dist)) {
                best_route = route;
                best_len = route.length;
                best_dist = player_dist;
                found = 1;
            }
        }
    }
    if (!found) return 0;
    api_set_player_route(p, &best_route);
    p->interaction.flags |= RC_INTERACTION_MOVED;
    return 1;
}

static RcNpc *validate_pending_npc_interaction(RcWorld *world,
                                               RcPendingInteraction *pending) {
    if (!world || !pending || !pending->active ||
            pending->target.kind != RC_INTERACTION_NPC) {
        return NULL;
    }
    RcNpc *npc = api_find_npc_by_uid(world, pending->target.entity_uid);
    if (!npc) {
        pending->last_failure = RC_INTERACTION_FAIL_TARGET_MISSING;
        return NULL;
    }
    if (npc->is_dead || npc->player_untargetable) {
        pending->last_failure = RC_INTERACTION_FAIL_TARGET_DEAD;
        return NULL;
    }
    if (npc->def_id < 0 || npc->def_id >= g_npc_def_count) {
        pending->last_failure = RC_INTERACTION_FAIL_INVALID_TARGET;
        return NULL;
    }
    const RcNpcDef *def = &g_npc_defs[npc->def_id];
    if (pending->target.definition_id >= 0 &&
            pending->target.definition_id != def->id) {
        pending->last_failure = RC_INTERACTION_FAIL_TARGET_VERSION_CHANGED;
        return NULL;
    }
    int opt = api_option_from_interaction_op(pending->op);
    int attack = 0;
    if (pending->op == RC_INTERACTION_USE_ON) {
        if (pending->source_item_id < 0) {
            pending->last_failure = RC_INTERACTION_FAIL_INVALID_SOURCE;
            return NULL;
        }
    } else if (pending->op == RC_INTERACTION_SPELL_ON) {
        if (pending->source_spell_id < 0) {
            pending->last_failure = RC_INTERACTION_FAIL_INVALID_SOURCE;
            return NULL;
        }
    } else {
        const char *option = rc_npc_def_option(def, opt);
        if (opt < 0 || !option || !option[0]) {
            pending->last_failure = RC_INTERACTION_FAIL_OPTION_UNAVAILABLE;
            return NULL;
        }
        attack = rc_npc_def_option_is_attack(def, opt);
        if (pending->target.content_group ==
                RC_INTERACTION_CONTENT_GROUP_NPC_ATTACK && !attack) {
            pending->last_failure = RC_INTERACTION_FAIL_OPTION_UNAVAILABLE;
            return NULL;
        }
    }
    int size = def->size > 0 ? def->size : 1;
    if (attack) {
        pending->target.content_group =
            RC_INTERACTION_CONTENT_GROUP_NPC_ATTACK;
    }
    pending->target.tile_x = npc->x;
    pending->target.tile_y = npc->y;
    pending->target.plane = npc->plane;
    pending->target.footprint_width = size;
    pending->target.footprint_height = size;
    return npc;
}

static int validate_pending_object_interaction(RcWorld *world,
                                               RcPendingInteraction *pending) {
    if (!world || !pending || !pending->active ||
            pending->target.kind != RC_INTERACTION_OBJECT) {
        return 0;
    }
    int opt = api_option_from_interaction_op(pending->op);
    const RcObjectDef *def = rc_object_def_get(pending->target.definition_id);
    const RcObjectBehavior *behavior =
        rc_object_behavior_get(pending->target.definition_id);
    const RcTraversalEdge *edge = object_traversal_edge(
        world, pending->target.definition_id, pending->target.tile_x,
        pending->target.tile_y, pending->target.plane, opt,
        pending->target.placement_key);
    if (!def) {
        pending->last_failure = RC_INTERACTION_FAIL_INVALID_TARGET;
        return 0;
    }
    if (pending->target.tile_x >= 0 && pending->target.tile_y >= 0
            && pending->target.plane >= 0
            && g_rc_object_placement_count > 0
            && !current_object_placement_key(
                world, pending->target.definition_id,
                pending->target.tile_x, pending->target.tile_y,
                pending->target.plane, pending->target.placement_key, NULL)) {
        pending->last_failure = RC_INTERACTION_FAIL_TARGET_MISSING;
        return 0;
    }
    RcObjectState *state = NULL;
    if (pending->target.tile_x >= 0 && pending->target.tile_y >= 0 &&
            pending->target.plane >= 0) {
        state = pending->target.placement_key
            ? object_state_find_by_key(world, pending->target.placement_key)
            : object_state_find(world, pending->target.definition_id,
                                pending->target.tile_x,
                                pending->target.tile_y,
                                pending->target.plane);
        if (state && !object_state_matches_target(
                state, pending->target.definition_id, pending->target.tile_x,
                pending->target.tile_y, pending->target.plane)) {
            pending->last_failure = RC_INTERACTION_FAIL_TARGET_MISSING;
            return 0;
        }
        if (state) {
            const RcObjectBehavior *base_behavior =
                rc_object_behavior_get(state->base_obj_id);
            if (base_behavior)
                behavior = base_behavior;
        }
    }
    if (pending->op == RC_INTERACTION_USE_ON) {
        if (pending->source_item_id < 0) {
            pending->last_failure = RC_INTERACTION_FAIL_INVALID_SOURCE;
            return 0;
        }
    } else if (pending->op == RC_INTERACTION_SPELL_ON) {
        if (pending->source_spell_id < 0) {
            pending->last_failure = RC_INTERACTION_FAIL_INVALID_SOURCE;
            return 0;
        }
    } else if (!object_option_available(def, behavior, edge, opt)) {
        pending->last_failure = RC_INTERACTION_FAIL_OPTION_UNAVAILABLE;
        return 0;
    }
    if (state && (state->flags & RC_OBJECT_STATE_DEPLETED)) {
        pending->last_failure = RC_INTERACTION_FAIL_TARGET_MISSING;
        return 0;
    }
    if (state && state->active_obj_id != pending->target.definition_id) {
        pending->last_failure = RC_INTERACTION_FAIL_TARGET_VERSION_CHANGED;
        return 0;
    }
    RcObjectPlacement placement;
    if (current_object_placement_key(world, pending->target.definition_id,
                                     pending->target.tile_x,
                                     pending->target.tile_y,
                                     pending->target.plane,
                                     pending->target.placement_key,
                                     &placement)) {
        int w = 1, l = 1;
        placement_dimensions(def, &placement, &w, &l);
        pending->target.footprint_width = w;
        pending->target.footprint_height = l;
        if (pending->target.placement_key == 0)
            pending->target.placement_key = placement.key;
    } else {
        pending->target.footprint_width = def->width > 0 ? def->width : 1;
        pending->target.footprint_height = def->length > 0 ? def->length : 1;
    }
    return 1;
}

static int validate_pending_ground_item_interaction(
    RcWorld *world, RcPendingInteraction *pending) {
    if (!world || !pending || !pending->active ||
            pending->target.kind != RC_INTERACTION_GROUND_ITEM) {
        return 0;
    }
    int idx = pending->target.ground_item_instance;
    if (idx < 0 || idx >= world->ground_item_count) {
        pending->last_failure = RC_INTERACTION_FAIL_TARGET_MISSING;
        return 0;
    }
    RcGroundItem *item = &world->ground_items[idx];
    if (!item->active || item->quantity <= 0) {
        pending->last_failure = RC_INTERACTION_FAIL_TARGET_MISSING;
        return 0;
    }
    if (item->visibility != RC_GROUND_VIS_PUBLIC &&
            item->owner_uid != RC_GROUND_OWNER_LOCAL_PLAYER) {
        pending->last_failure = RC_INTERACTION_FAIL_TARGET_MISSING;
        return 0;
    }
    if (item->uid != pending->target.entity_uid ||
            item->version != pending->target.entity_generation) {
        pending->last_failure = RC_INTERACTION_FAIL_TARGET_VERSION_CHANGED;
        return 0;
    }
    if (item->item_id != pending->target.definition_id ||
            item->x != pending->target.tile_x ||
            item->y != pending->target.tile_y ||
            item->plane != pending->target.plane) {
        pending->last_failure = RC_INTERACTION_FAIL_TARGET_VERSION_CHANGED;
        return 0;
    }
    pending->target.footprint_width = 1;
    pending->target.footprint_height = 1;
    return 1;
}

static void finish_interaction_result(RcPlayer *p,
                                      RcInteractionHandlerResult result) {
    if (result.code == RC_INTERACTION_HANDLER_CONTINUE_APPROACH) {
        if (result.approach_range >= 0) {
            p->interaction.approach_range = result.approach_range;
        }
        return;
    }
    if (result.code == RC_INTERACTION_HANDLER_FAILURE ||
            result.code == RC_INTERACTION_HANDLER_CANCEL) {
        rc_interaction_cancel(p, result.failure);
        return;
    }
    p->interaction.active = false;
    p->interaction.flags |= RC_INTERACTION_INTERACTED |
                            RC_INTERACTION_COMPLETED;
    p->interaction.last_failure = RC_INTERACTION_FAIL_NONE;
}

static void process_player_interaction(RcWorld *world, int dispatch_ready) {
    if (!world || !world->player.interaction.active) return;
    RcPlayer *p = &world->player;
    if (p->interaction.target.kind == RC_INTERACTION_NPC) {
        RcNpc *npc = validate_pending_npc_interaction(world, &p->interaction);
        if (!npc) {
            rc_interaction_cancel(p, p->interaction.last_failure);
            return;
        }
        if (npc->plane != p->plane) {
            rc_interaction_cancel(p, RC_INTERACTION_FAIL_CANNOT_REACH);
            return;
        }
        int range = p->interaction.approach_range > 0
                  ? p->interaction.approach_range : 1;
        if (player_distance_to_npc(p, npc) > range) {
            if (player_has_active_route(p))
                return;
            route_player_toward_interaction_npc(world, p, npc, range);
            return;
        }
        if (!npc_interaction_has_los(world, p, npc, range)) {
            rc_interaction_cancel(p, RC_INTERACTION_FAIL_LOS_BLOCKED);
            return;
        }
        face_player_to_npc(p, npc);
        if (dispatch_ready) api_register_default_npc_handlers();
    } else if (p->interaction.target.kind == RC_INTERACTION_OBJECT) {
        if (!validate_pending_object_interaction(world, &p->interaction)) {
            rc_interaction_cancel(p, p->interaction.last_failure);
            return;
        }
        if (p->interaction.target.plane != p->plane) {
            rc_interaction_cancel(p, RC_INTERACTION_FAIL_CANNOT_REACH);
            return;
        }
        int range = p->interaction.approach_range > 0
                  ? p->interaction.approach_range : 1;
        int in_range =
            player_distance_to_target(p, &p->interaction.target) <= range;
        int opt = api_option_from_interaction_op(p->interaction.op);
        const RcTraversalEdge *edge = object_traversal_edge(
            world, p->interaction.target.definition_id,
            p->interaction.target.tile_x, p->interaction.target.tile_y,
            p->interaction.target.plane, opt,
            p->interaction.target.placement_key);
        if (edge && edge->start_plane == p->plane
                && edge->start_x != 0xFFFFu && edge->start_y != 0xFFFFu) {
            in_range = p->x == (int)edge->start_x
                    && p->y == (int)edge->start_y;
        }
        if (in_range &&
                p->interaction.target.kind == RC_INTERACTION_OBJECT) {
            in_range = tile_reaches_object_target(
                world, &p->interaction.target, p->x, p->y);
            if (!in_range && edge && edge->start_plane == p->plane
                    && p->x == (int)edge->start_x
                    && p->y == (int)edge->start_y) {
                in_range = 1;
            }
        }
        if (!in_range) {
            if (player_has_active_route(p))
                return;
            if (!route_player_toward_interaction_target(
                    world, p, &p->interaction.target, range)) {
                rc_interaction_cancel(p, RC_INTERACTION_FAIL_CANNOT_REACH);
            }
            return;
        }
        face_player_to_target(p, &p->interaction.target);
        if (dispatch_ready) api_register_default_object_handlers();
    } else if (p->interaction.target.kind == RC_INTERACTION_GROUND_ITEM) {
        if (!validate_pending_ground_item_interaction(world,
                                                      &p->interaction)) {
            rc_interaction_cancel(p, p->interaction.last_failure);
            return;
        }
        if (p->interaction.target.plane != p->plane) {
            rc_interaction_cancel(p, RC_INTERACTION_FAIL_CANNOT_REACH);
            return;
        }
        int range = p->interaction.approach_range;
        if (player_distance_to_target(p, &p->interaction.target) > range) {
            if (player_has_active_route(p))
                return;
            if (!route_player_toward_interaction_target(
                    world, p, &p->interaction.target, range)) {
                rc_interaction_cancel(p, RC_INTERACTION_FAIL_CANNOT_REACH);
            }
            return;
        }
        face_player_to_target(p, &p->interaction.target);
        if (dispatch_ready) api_register_default_ground_item_handlers();
    } else if (p->interaction.target.kind == RC_INTERACTION_INVENTORY_ITEM ||
            p->interaction.target.kind == RC_INTERACTION_EQUIPMENT_ITEM ||
            p->interaction.target.kind == RC_INTERACTION_WIDGET) {
        if (!rc_interaction_target_valid(&p->interaction.target)) {
            rc_interaction_cancel(p, RC_INTERACTION_FAIL_INVALID_TARGET);
            return;
        }
    } else {
        return;
    }
    p->interaction.flags |= RC_INTERACTION_ARRIVED;
    if (!dispatch_ready) return;
    RcInteractionHandlerResult result = rc_interaction_dispatch(world, p);
    finish_interaction_result(p, result);
}

static int api_prepare_spatial_interaction(RcWorld *world) {
    if (!world || !world->player.interaction.active)
        return 0;
    RcPlayer *p = &world->player;
    p->route_len = 0;
    p->route_idx = 0;
    process_player_interaction(world, 0);
    return p->interaction.active;
}

static void mark_object_depleted(RcWorld *world, RcObjectState *st,
                                 int respawn_tick) {
    st->flags |= RC_OBJECT_STATE_DEPLETED;
    st->respawn_tick = respawn_tick;
    if (world->next_object_respawn_tick <= 0
            || respawn_tick < world->next_object_respawn_tick) {
        world->next_object_respawn_tick = respawn_tick;
    }
}

static void process_player_combat(RcWorld *world) {
    rc_combat_tick_player(world);
}

static void process_player_skilling(RcWorld *world) {
    if (!world || world->player.skill_timer <= 0) return;
    world->player.skill_timer--;
    if (world->player.skill_timer > 0 || world->player.skill_action <= 0) {
        return;
    }
    if (world->player.skill_target_x >= 0 && world->player.skill_target_y >= 0
            && world->player.plane >= 0) {
        const RcGatheringNode *node = rc_gathering_node_find(
            world->player.skill_action, world->player.skill_target_x,
            world->player.skill_target_y, world->player.plane);
        if (node) {
            int respawn = 8;
            if (node->skill == RC_OBJ_SKILL_FARMING) respawn = 20;
            else if (node->skill == RC_OBJ_SKILL_FISHING) respawn = 4;
            for (int i = 0; i < world->object_state_count; i++) {
                RcObjectState *st = &world->object_states[i];
                if (st->base_obj_id == (int)node->obj_id
                        && st->x == world->player.skill_target_x
                        && st->y == world->player.skill_target_y
                        && st->plane == world->player.plane) {
                    mark_object_depleted(world, st, world->tick + respawn);
                    world->player.skill_action = 0;
                    return;
                }
            }
            if (world->object_state_count < RC_MAX_OBJECT_STATES) {
                RcObjectState *st =
                    &world->object_states[world->object_state_count++];
                st->base_obj_id = (int)node->obj_id;
                st->active_obj_id = (int)node->obj_id;
                st->x = world->player.skill_target_x;
                st->y = world->player.skill_target_y;
                st->plane = world->player.plane;
                st->placement_key = 0;
                st->active_x = st->x;
                st->active_y = st->y;
                st->active_plane = st->plane;
                st->base_type = 10;
                st->base_rotation = 0;
                st->active_type = st->base_type;
                st->active_rotation = st->base_rotation;
                st->revert_tick = 0;
                st->flags = 0;
                mark_object_depleted(world, st, world->tick + respawn);
            }
        }
    }
    world->player.skill_action = 0;
}

// (Player-side hit resolution lives in combat.c as
// rc_resolve_player_hits — it fires RC_EVT_PLAYER_DAMAGED per hit.)

static void resolve_npc_hits(RcWorld *world, RcNpc *npc) {
    bool was_alive = !npc->is_dead;
    int death_source = -1;
    int w = 0;
    for (int i = 0; i < npc->num_pending_hits; i++) {
        RcPendingHit *h = &npc->pending_hits[i];
        if (!h->active) continue;
        if (h->ticks_remaining > 0) {
            h->ticks_remaining--;
            if (w != i) npc->pending_hits[w] = *h;
            w++;
            continue;
        }
        int damage = npc->is_dead ? 0 :
                     rc_combat_resolve_hit_damage(
                         h, false /* npc defender */);
        uint8_t hit_type = damage <= 0 ? RC_HIT_TYPE_MISS :
                           (h->max_hit > 0 && damage >= h->max_hit
                            ? RC_HIT_TYPE_MAX : RC_HIT_TYPE_NORMAL);
        rc_combat_actor_record_hit(&npc->combat, damage, h->max_hit,
                                   h->attack_style, h->source_idx,
                                   hit_type, h->flags, 4);
        h->active = 0;
        if (damage <= 0) continue;

        npc->current_hp -= damage;
        npc->last_hit = damage;
        npc->last_hit_timer = 4;
        if (h->source_idx < 0) {
            RcCombatActorRef player_actor = {
                .kind = RC_COMBAT_ACTOR_PLAYER,
                .uid = 0,
            };
            rc_combat_actor_register_attacker(&npc->combat, player_actor);
            rc_award_player_combat_xp(world, damage);
            if (!npc->is_dead) {
                rc_combat_start_npc_vs_player(world, npc->uid, 0);
            }
        }
        if (npc->current_hp <= 0) {
            npc->current_hp = 0;
            npc->is_dead = true;
            npc->death_timer = 3;
            int respawn = g_npc_defs[npc->def_id].respawn_ticks;
            npc->respawn_timer = respawn > 0 ? respawn : 25;
            npc->target_uid = -1;
            death_source = h->source_idx;
            if (world->player.attack_target == npc->uid) {
                world->player.attack_target = -1;
                world->player.attack_target_def_id = -1;
            }
        }
        RcPayloadNpcDamaged damaged = {
            .npc_id = (uint16_t)npc->uid,
            .source_npc_id = h->source_idx >= 0
                           ? (uint16_t)h->source_idx : 0xFFFFu,
            .damage = (uint16_t)(damage & 0xFFFF),
            .current_hp = (uint16_t)(npc->current_hp & 0xFFFF),
            .max_hp = npc->def_id >= 0 && npc->def_id < g_npc_def_count
                    ? (uint16_t)(g_npc_defs[npc->def_id].hitpoints & 0xFFFF)
                    : 0,
            .style = (uint8_t)h->attack_style,
        };
        rc_event_fire(world, RC_EVT_NPC_DAMAGED, &damaged);
    }
    npc->num_pending_hits = w;
    npc->combat.hp_current = npc->current_hp;
    npc->combat.hp_max = npc->def_id >= 0 && npc->def_id < g_npc_def_count
                       ? g_npc_defs[npc->def_id].hitpoints : 0;
    // Fire death event once on the transition alive → dead.
    if (was_alive && npc->is_dead) {
        if (death_source < 0) spawn_npc_loot(world, npc);
        RcPayloadNpcEvent payload = {
            .npc_id = (uint16_t)npc->uid,
            .def_id = (uint32_t)g_npc_defs[npc->def_id].id,
        };
        rc_event_fire(world, RC_EVT_NPC_DIED, &payload);
    }
}

static void check_deaths(RcWorld *world) {
    (void)world;
}

static void schedule_object_state_tick(RcWorld *world, int tick) {
    if (!world || tick <= 0) return;
    if (world->next_object_respawn_tick <= 0
            || tick < world->next_object_respawn_tick) {
        world->next_object_respawn_tick = tick;
    }
}

static void reset_object_state_active(RcObjectState *st) {
    if (!st) return;
    st->active_obj_id = st->base_obj_id;
    st->active_x = st->x;
    st->active_y = st->y;
    st->active_plane = st->plane;
    st->active_type = st->base_type;
    st->active_rotation = st->base_rotation;
    st->revert_tick = 0;
    st->flags &= (uint8_t)~(RC_OBJECT_STATE_OPEN | RC_OBJECT_STATE_DYNAMIC);
}

static void tick_respawns(RcWorld *world) {
    if (!world) return;
    if (world->next_object_respawn_tick <= 0
            || world->tick < world->next_object_respawn_tick) {
        return;
    }
    int next = 0;
    for (int i = 0; i < world->object_state_count; i++) {
        RcObjectState *st = &world->object_states[i];
        if ((st->flags & RC_OBJECT_STATE_DYNAMIC) != 0
                && st->revert_tick > 0) {
            if (world->tick >= st->revert_tick) {
                int was_open = (st->flags & RC_OBJECT_STATE_OPEN) != 0;
                if (was_open) {
                    apply_door_collision(world, st->base_obj_id, st->x,
                                         st->y, st->plane, 0);
                }
                reset_object_state_active(st);
            } else if (next == 0 || st->revert_tick < next) {
                next = st->revert_tick;
            }
        }
        if ((st->flags & RC_OBJECT_STATE_DEPLETED) != 0
                && st->respawn_tick > 0) {
            if (world->tick >= st->respawn_tick) {
                st->flags &= (uint8_t)~RC_OBJECT_STATE_DEPLETED;
                st->respawn_tick = 0;
            } else if (next == 0 || st->respawn_tick < next) {
                next = st->respawn_tick;
            }
        }
    }
    world->next_object_respawn_tick = next;
}

static void tick_ground_items(RcWorld *world) {
    if (!world) return;
    for (int i = 0; i < world->ground_item_count; i++) {
        RcGroundItem *item = &world->ground_items[i];
        if (!item->active) continue;
        if (item->visibility == RC_GROUND_VIS_PRIVATE &&
                item->reveal_timer > 0) {
            item->reveal_timer--;
            if (item->reveal_timer == 0) {
                item->visibility = RC_GROUND_VIS_PUBLIC;
                item->owner_uid = RC_GROUND_OWNER_NONE;
                item->version++;
            }
        }
        if (item->despawn_timer <= 0) continue;
        item->despawn_timer--;
        if (item->despawn_timer == 0) {
            item->active = false;
            item->quantity = 0;
            item->version++;
        }
    }
}

// Tick dispatcher. Per rc-core/README.md §3, per-subsystem ticks are
// gated by a cache-resident bitmask-AND; the base (player position,
// NPC position, pathfinding, tick counter) always runs.
void rc_world_tick(RcWorld *world) {
    const uint32_t on = world->enabled;
    rc_combat_clear_attack_events(world);

    // Phase 1 — input (base): always runs.
    process_player_input(world);

    // Phase 2 — route planning (base): always runs.
    process_player_route(world);

    // Phase 3 — NPC tick (base): position + wander + route advance.
    // Encounter mechanics dispatch happens inside npc_tick when
    // RC_SUB_ENCOUNTER is enabled.
    for (int i = 0; i < world->npc_count; i++) {
        if (world->npcs[i].active) {
            rc_npc_tick(world, &world->npcs[i]);
        }
    }

    // Phase 3.5 — NPC combat (COMBAT subsystem): after positions
    // resolve, each NPC may queue an attack on its target.
    if (on & RC_SUB_COMBAT) {
        for (int i = 0; i < world->npc_count; i++) {
            if (world->npcs[i].active) {
                rc_combat_tick_npc(world, &world->npcs[i]);
            }
        }
    }

    // Phase 3.6 — encounter mechanic dispatcher.
    if (on & RC_SUB_ENCOUNTER) rc_encounter_tick(world);
    if (on & RC_SUB_ENCOUNTER) rc_encounter_tick_effects(world);

    // Phase 4 — player action resolution (gated per subsystem).
    process_player_action_timers(world);
    process_player_movement(world);
    process_player_interaction(world, 1);
    if (on & RC_SUB_COMBAT)   process_player_combat(world);
    if (on & RC_SUB_SKILLS)   process_player_skilling(world);

    // Phase 5 — pending-hit resolution (combat subsystem).
    if (on & RC_SUB_COMBAT) {
        rc_resolve_player_hits(world);
        rc_combat_tick_player_status(world);
        for (int i = 0; i < world->npc_count; i++) {
            if (world->npcs[i].active) {
                resolve_npc_hits(world, &world->npcs[i]);
            }
        }
    }

    // Phase 6 — prayer drain (prayer subsystem).
    if (on & RC_SUB_PRAYER)   rc_prayer_drain_tick(&world->player);

    // Phase 7 — stat regen (skills subsystem, but hp regen baseline
    // runs in base since it's part of the base player model).
    rc_stat_restore_tick(&world->player.skills);

    // Phase 8 — deaths / respawns / ground items.
    if (on & RC_SUB_COMBAT)   check_deaths(world);
    tick_respawns(world);     // base — NPC wander reset clock
    if (on & RC_SUB_LOOT)     tick_ground_items(world);

    world->tick++;
}

// Input stubs — filled in as we build each system
static RcNpc *api_find_npc_by_uid(RcWorld *world, int uid) {
    if (!world) return NULL;
    for (int i = 0; i < world->npc_count; i++) {
        if (world->npcs[i].active && world->npcs[i].uid == uid) {
            return &world->npcs[i];
        }
    }
    return NULL;
}

static RcInteractionTarget api_npc_interaction_target(const RcNpc *npc) {
    RcInteractionTarget target = {0};
    target.kind = RC_INTERACTION_NPC;
    target.entity_uid = npc ? npc->uid : -1;
    target.entity_generation = 0;
    target.definition_id = -1;
    target.placement_key = 0;
    target.content_group = -1;
    target.tile_x = npc ? npc->x : -1;
    target.tile_y = npc ? npc->y : -1;
    target.plane = npc ? npc->plane : -1;
    target.footprint_width = 1;
    target.footprint_height = 1;
    target.inventory_slot = -1;
    target.equipment_slot = -1;
    target.widget_id = -1;
    target.component_id = -1;
    target.ground_item_instance = -1;
    if (npc && npc->def_id >= 0 && npc->def_id < g_npc_def_count) {
        const RcNpcDef *def = &g_npc_defs[npc->def_id];
        int size = def->size > 0 ? def->size : 1;
        target.definition_id = def->id;
        target.footprint_width = size;
        target.footprint_height = size;
    }
    return target;
}

static RcInteractionTarget api_object_interaction_target(
    const RcObjectDef *def, int obj_id, int x, int y, int plane) {
    RcInteractionTarget target = {0};
    target.kind = RC_INTERACTION_OBJECT;
    target.entity_uid = -1;
    target.entity_generation = 0;
    target.definition_id = obj_id;
    target.placement_key = 0;
    target.content_group = -1;
    target.tile_x = x;
    target.tile_y = y;
    target.plane = plane;
    target.footprint_width = def && def->width > 0 ? def->width : 1;
    target.footprint_height = def && def->length > 0 ? def->length : 1;
    target.inventory_slot = -1;
    target.equipment_slot = -1;
    target.widget_id = -1;
    target.component_id = -1;
    target.ground_item_instance = -1;
    return target;
}

static RcInteractionTarget api_placed_object_interaction_target(
    const RcWorld *world, const RcObjectDef *def, int obj_id, int x, int y,
    int plane, uint64_t placement_key) {
    RcInteractionTarget target =
        api_object_interaction_target(def, obj_id, x, y, plane);
    target.placement_key = placement_key;
    if (x < 0 || y < 0 || plane < 0)
        return target;

    RcObjectPlacement placement;
    if (current_object_placement_key(world, obj_id, x, y, plane,
                                     placement_key, &placement)) {
        target.placement_key = placement.key;
        if (!(placement.rotation & 1u))
            return target;
        int width = target.footprint_width;
        target.footprint_width = target.footprint_height;
        target.footprint_height = width;
    }
    return target;
}

static RcInteractionTarget api_ground_item_interaction_target(
    const RcGroundItem *item, int idx) {
    RcInteractionTarget target = {0};
    target.kind = RC_INTERACTION_GROUND_ITEM;
    target.entity_uid = item ? item->uid : -1;
    target.entity_generation = item ? item->version : 0;
    target.definition_id = item ? item->item_id : -1;
    target.placement_key = 0;
    target.content_group = -1;
    target.tile_x = item ? item->x : -1;
    target.tile_y = item ? item->y : -1;
    target.plane = item ? item->plane : -1;
    target.footprint_width = 1;
    target.footprint_height = 1;
    target.inventory_slot = -1;
    target.equipment_slot = -1;
    target.widget_id = -1;
    target.component_id = -1;
    target.ground_item_instance = idx;
    return target;
}

static RcInteractionTarget api_inventory_item_interaction_target(
    const RcPlayer *player, int slot) {
    RcInteractionTarget target = {0};
    target.kind = RC_INTERACTION_INVENTORY_ITEM;
    target.entity_uid = -1;
    target.entity_generation = 0;
    target.definition_id = slot >= 0 && slot < RC_INVENTORY_SIZE
                         ? player->inventory[slot].item_id : -1;
    target.placement_key = 0;
    target.content_group = -1;
    target.tile_x = -1;
    target.tile_y = -1;
    target.plane = -1;
    target.footprint_width = 0;
    target.footprint_height = 0;
    target.inventory_slot = slot;
    target.equipment_slot = -1;
    target.widget_id = -1;
    target.component_id = -1;
    target.ground_item_instance = -1;
    return target;
}

static RcInteractionTarget api_equipment_item_interaction_target(
    const RcPlayer *player, int slot) {
    RcInteractionTarget target = {0};
    target.kind = RC_INTERACTION_EQUIPMENT_ITEM;
    target.entity_uid = -1;
    target.entity_generation = 0;
    target.definition_id = slot >= 0 && slot < RC_EQUIP_COUNT
                         ? player->equipment[slot].item_id : -1;
    target.placement_key = 0;
    target.content_group = -1;
    target.tile_x = -1;
    target.tile_y = -1;
    target.plane = -1;
    target.footprint_width = 0;
    target.footprint_height = 0;
    target.inventory_slot = -1;
    target.equipment_slot = slot;
    target.widget_id = -1;
    target.component_id = -1;
    target.ground_item_instance = -1;
    return target;
}

static RcInteractionTarget api_widget_interaction_target(
    int widget_id, int component_id, int action) {
    RcInteractionTarget target = {0};
    target.kind = RC_INTERACTION_WIDGET;
    target.entity_uid = -1;
    target.entity_generation = 0;
    target.definition_id = action;
    target.placement_key = 0;
    target.content_group = -1;
    target.tile_x = -1;
    target.tile_y = -1;
    target.plane = -1;
    target.footprint_width = 0;
    target.footprint_height = 0;
    target.inventory_slot = -1;
    target.equipment_slot = -1;
    target.widget_id = widget_id;
    target.component_id = component_id;
    target.ground_item_instance = -1;
    return target;
}

static int api_npc_attack_option_index(const RcNpcDef *def) {
    if (!def) return -1;
    for (int i = 0; i < RC_NPC_OPTION_COUNT; i++) {
        if (rc_npc_def_option_is_attack(def, i)) return i;
    }
    return -1;
}

static int api_option_from_interaction_op(RcInteractionOp op) {
    switch (op) {
    case RC_INTERACTION_OP1: return 0;
    case RC_INTERACTION_OP2: return 1;
    case RC_INTERACTION_OP3: return 2;
    case RC_INTERACTION_OP4: return 3;
    case RC_INTERACTION_OP5: return 4;
    default: return -1;
    }
}

static void api_stop_player_combat(RcWorld *world) {
    if (!world) return;
    int npc_uid = -1;
    if (world->player.combat.target.kind == RC_COMBAT_ACTOR_NPC) {
        npc_uid = world->player.combat.target.uid;
    } else if (world->player.attack_target >= 0) {
        npc_uid = world->player.attack_target;
    }
    RcCombatActorRef actor = {
        .kind = RC_COMBAT_ACTOR_PLAYER,
        .uid = 0,
    };
    rc_combat_stop_actor(world, actor, RC_COMBAT_STATE_CANCELLED);
    world->player.manual_spell_cast = -1;
    if (npc_uid >= 0) {
        RcCombatActorRef npc_actor = {
            .kind = RC_COMBAT_ACTOR_NPC,
            .uid = npc_uid,
        };
        rc_combat_stop_actor(world, npc_actor, RC_COMBAT_STATE_CANCELLED);
    }
}

static int spellbook_valid(int spellbook) {
    return spellbook >= RC_SPELL_BOOK_STANDARD &&
           spellbook <= RC_SPELL_BOOK_ARCEUUS;
}

static int spell_usable_on_current_book(const RcPlayer *player,
                                        const RcSpellDef *spell) {
    return player && spell && spell->loaded &&
           spell->book == player->current_spellbook;
}

static int spell_is_autocast_candidate(const RcPlayer *player,
                                       const RcSpellDef *spell) {
    return spell_usable_on_current_book(player, spell) &&
           spell->type == RC_SPELL_TYPE_COMBAT &&
           spell->max_hit > 0;
}

static RcInteractionHandlerResult api_default_npc_attack_handler(
    RcWorld *world, RcPlayer *player, const RcPendingInteraction *pending,
    void *ctx) {
    (void)ctx;
    if (!world || !player || !pending ||
            pending->target.kind != RC_INTERACTION_NPC) {
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_INVALID_TARGET, "Invalid NPC attack");
    }
    RcNpc *npc = api_find_npc_by_uid(world, pending->target.entity_uid);
    if (!npc || npc->is_dead || npc->def_id < 0 ||
            npc->def_id >= g_npc_def_count) {
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_TARGET_MISSING, "NPC target missing");
    }
    if (pending->op == RC_INTERACTION_SPELL_ON) {
        if (pending->source_spell_id < 0 ||
                !rc_spell_def_get(pending->source_spell_id)) {
            return rc_interaction_result_failure(
                RC_INTERACTION_FAIL_INVALID_SOURCE,
                "Invalid combat spell");
        }
        player->manual_spell_cast = pending->source_spell_id;
        rc_refresh_player_combat_style(player);
        if (!rc_combat_start_player_vs_npc(world, 0, npc->uid)) {
            return rc_interaction_result_failure(
                RC_INTERACTION_FAIL_INVALID_TARGET,
                "NPC spell attack start failed");
        }
        player->interact_type = RC_INTERACT_NPC_ATTACK;
        player->interact_target = npc->uid;
        player->interact_option = 0;
        return rc_interaction_result_combat_handoff(npc->uid);
    }

    int opt = api_option_from_interaction_op(pending->op);
    const RcNpcDef *def = &g_npc_defs[npc->def_id];
    if (opt < 0 || !rc_npc_def_option_is_attack(def, opt)) {
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_OPTION_UNAVAILABLE,
            "NPC attack option unavailable");
    }

    if (!rc_combat_start_player_vs_npc(world, 0, npc->uid)) {
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_INVALID_TARGET, "NPC attack start failed");
    }
    player->interact_type = RC_INTERACT_NPC_ATTACK;
    player->interact_target = npc->uid;
    player->interact_option = opt;
    return rc_interaction_result_combat_handoff(npc->uid);
}

static RcInteractionHandlerResult api_default_npc_option_handler(
    RcWorld *world, RcPlayer *player, const RcPendingInteraction *pending,
    void *ctx) {
    (void)ctx;
    if (!world || !player || !pending ||
            pending->target.kind != RC_INTERACTION_NPC) {
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_INVALID_TARGET, "Invalid NPC interaction");
    }
    RcNpc *npc = api_find_npc_by_uid(world, pending->target.entity_uid);
    if (!npc || npc->is_dead || npc->def_id < 0 ||
            npc->def_id >= g_npc_def_count) {
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_TARGET_MISSING, "NPC target missing");
    }
    int opt = api_option_from_interaction_op(pending->op);
    if (opt < 0) {
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_OPTION_UNAVAILABLE, "Invalid NPC option");
    }
    const RcNpcDef *def = &g_npc_defs[npc->def_id];
    if (rc_npc_def_option_is_attack(def, opt)) {
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_NO_HANDLER, "NPC attack handler unavailable");
    }
    api_stop_player_combat(world);
    if (rc_player_open_storage_npc(world, npc->uid, opt)) {
        return rc_interaction_result_complete();
    }
    player->interact_type = RC_INTERACT_NPC;
    player->interact_target = npc->uid;
    player->interact_option = opt;
    return rc_interaction_result_complete();
}

static void api_register_default_npc_handlers(void) {
    for (int i = 0; i < 5; i++) {
        RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
        key.kind = RC_INTERACTION_NPC;
        key.op = rc_interaction_op_from_option(i);
        key.content_group = RC_INTERACTION_CONTENT_GROUP_NPC_ATTACK;
        if (rc_interaction_find_handler(&key) < 0) {
            rc_interaction_register_handler(&key,
                                            api_default_npc_attack_handler,
                                            NULL);
        }
        key.content_group = RC_INTERACTION_KEY_ANY;
        if (rc_interaction_find_handler(&key) < 0) {
            rc_interaction_register_handler(&key,
                                            api_default_npc_option_handler,
                                            NULL);
        }
    }
    RcInteractionDispatchKey spell_key = rc_interaction_dispatch_key_any();
    spell_key.kind = RC_INTERACTION_NPC;
    spell_key.op = RC_INTERACTION_SPELL_ON;
    spell_key.content_group = RC_INTERACTION_CONTENT_GROUP_NPC_ATTACK;
    if (rc_interaction_find_handler(&spell_key) < 0) {
        rc_interaction_register_handler(&spell_key,
                                        api_default_npc_attack_handler,
                                        NULL);
    }
}

static int action_allowed_if_loaded(RcWorld *world, int action_id) {
    return world && (g_rc_player_action_count == 0 ||
                     rc_player_action_allowed(world->enabled, action_id));
}

static void api_set_player_route(RcPlayer *p, const RcRoute *route) {
    if (!p) return;
    p->route_len = 0;
    p->route_idx = 0;
    if (!route || route->length <= 0) return;
    int n = route->length < RC_MAX_ROUTE ? route->length : RC_MAX_ROUTE;
    for (int i = 0; i < n; i++) {
        p->route_x[i] = route->waypoints_x[i];
        p->route_y[i] = route->waypoints_y[i];
    }
    p->route_len = n;
    p->route_idx = 0;
}

static void start_player_action_lock(RcWorld *world, int lock_ticks) {
    if (!world) return;
    RcPlayer *p = &world->player;
    if (lock_ticks > p->action_lock_timer)
        p->action_lock_timer = lock_ticks;
}

static void schedule_player_traversal(RcWorld *world,
                                      const RcTraversalEdge *edge,
                                      int delay_ticks) {
    if (!world || !edge) return;
    if (delay_ticks <= 0) {
        rc_player_apply_traversal(world, edge);
        return;
    }
    RcPlayer *p = &world->player;
    p->pending_traversal_active = 1;
    p->pending_traversal_tick = world->tick + delay_ticks;
    p->pending_traversal_x = edge->dest_x;
    p->pending_traversal_y = edge->dest_y;
    p->pending_traversal_plane = edge->dest_plane;
    p->route_len = 0;
    p->route_idx = 0;
    if (p->action_lock_timer < delay_ticks + 1)
        p->action_lock_timer = delay_ticks + 1;
}

void rc_player_walk_to(RcWorld *world, int x, int y) {
    if (!action_allowed_if_loaded(world, RC_PLAYER_ACTION_WALK_TO)) return;
    RcPlayer *p = &world->player;
    if (p->action_lock_timer > 0) return;
    RcRoute route = rc_find_path(&world->map, p->x, p->y, x, y,
                                 1, p->plane, false);
    api_set_player_route(p, &route);
    p->running = false;
    api_stop_player_combat(world);
    rc_interaction_cancel(p, RC_INTERACTION_FAIL_CANCELLED);
}

void rc_player_run_to(RcWorld *world, int x, int y) {
    if (!action_allowed_if_loaded(world, RC_PLAYER_ACTION_RUN_TO)) return;
    RcPlayer *p = &world->player;
    if (p->action_lock_timer > 0) return;
    RcRoute route = rc_find_path(&world->map, p->x, p->y, x, y,
                                 1, p->plane, false);
    api_set_player_route(p, &route);
    p->running = true;
    api_stop_player_combat(world);
    rc_interaction_cancel(p, RC_INTERACTION_FAIL_CANCELLED);
}

void rc_player_attack_npc(RcWorld *world, int npc_uid) {
    if (!action_allowed_if_loaded(world, RC_PLAYER_ACTION_ATTACK_NPC)) return;
    if (world->player.action_lock_timer > 0) return;
    RcNpc *npc = api_find_npc_by_uid(world, npc_uid);
    if (!npc || npc->is_dead || npc->player_untargetable) return;
    if (npc->plane != world->player.plane) return;
    if (npc->def_id < 0 || npc->def_id >= g_npc_def_count) return;
    const RcNpcDef *def = &g_npc_defs[npc->def_id];
    int opt = api_npc_attack_option_index(def);
    if (opt < 0) return;
    RcInteractionTarget target = api_npc_interaction_target(npc);
    target.content_group = RC_INTERACTION_CONTENT_GROUP_NPC_ATTACK;
    if (!rc_interaction_begin(&world->player, 0,
                              rc_interaction_op_from_option(opt),
                              rc_npc_def_option(def, opt), &target,
                              rc_player_attack_range(&world->player))) {
        return;
    }
    (void)api_prepare_spatial_interaction(world);
}
void rc_player_set_prayer(RcWorld *world, int prayer_id) {
    if (!world || !rc_player_action_allowed(world->enabled,
                                            RC_PLAYER_ACTION_SET_PRAYER)) {
        return;
    }
    if (world->player.current_prayer_points <= 0) return;
    const RcPrayerDef *def = rc_prayer_def_get(prayer_id);
    if (!def || world->player.skills.base_level[SKILL_PRAYER] < def->level) return;
    rc_prayer_toggle(&world->player, prayer_id);
}
void rc_player_set_spellbook(RcWorld *world, int spellbook) {
    if (!world || !spellbook_valid(spellbook)) return;
    RcPlayer *p = &world->player;
    if (p->current_spellbook == spellbook) return;
    p->current_spellbook = spellbook;
    p->selected_spell = -1;
    p->manual_spell_cast = -1;
    p->autocast_spell = -1;
    p->defensive_autocast = false;
    rc_refresh_player_combat_style(p);
}
void rc_player_select_spell(RcWorld *world, int spell_idx) {
    if (!world || !rc_player_action_allowed(world->enabled,
                                            RC_PLAYER_ACTION_SELECT_SPELL)) {
        return;
    }
    const RcSpellDef *spell = rc_spell_def_get(spell_idx);
    if (!spell_usable_on_current_book(&world->player, spell)) return;
    world->player.selected_spell = spell_idx;
}
void rc_player_set_autocast_spell(RcWorld *world, int spell_idx,
                                  int defensive) {
    if (!world || !rc_player_action_allowed(world->enabled,
                                            RC_PLAYER_ACTION_SELECT_SPELL)) {
        return;
    }
    RcPlayer *p = &world->player;
    if (spell_idx < 0) {
        p->autocast_spell = -1;
        p->defensive_autocast = false;
        rc_refresh_player_combat_style(p);
        return;
    }
    const RcSpellDef *spell = rc_spell_def_get(spell_idx);
    if (!spell_is_autocast_candidate(p, spell) ||
            !rc_player_weapon_can_autocast(p)) {
        return;
    }
    p->autocast_spell = spell_idx;
    p->defensive_autocast = defensive != 0;
    rc_refresh_player_combat_style(p);
}
void rc_player_eat(RcWorld *world, int inv_slot) { (void)world; (void)inv_slot; }
void rc_player_drink(RcWorld *world, int inv_slot) { (void)world; (void)inv_slot; }
void rc_player_interact_npc(RcWorld *world, int npc_uid, int opt) {
    if (!action_allowed_if_loaded(world, RC_PLAYER_ACTION_INTERACT_NPC)) return;
    if (world->player.action_lock_timer > 0) return;
    RcNpc *npc = api_find_npc_by_uid(world, npc_uid);
    if (!npc || npc->is_dead) return;
    if (npc->plane != world->player.plane) return;
    if (npc->def_id < 0 || npc->def_id >= g_npc_def_count) return;
    const RcNpcDef *def = &g_npc_defs[npc->def_id];
    const char *option = rc_npc_def_option(def, opt);
    if (!option || !option[0]) return;
    RcInteractionTarget target = api_npc_interaction_target(npc);
    int attack = rc_npc_def_option_is_attack(def, opt);
    if (attack) target.content_group = RC_INTERACTION_CONTENT_GROUP_NPC_ATTACK;
    int range = attack ? rc_player_attack_range(&world->player) : 1;
    if (!rc_interaction_begin(&world->player, 0,
                              rc_interaction_op_from_option(opt),
                              option, &target, range)) {
        return;
    }
    if (!attack) api_stop_player_combat(world);
    (void)api_prepare_spatial_interaction(world);
}
void rc_player_interact_object(RcWorld *world, int obj_id, int opt) {
    (void)rc_player_interact_object_at(world, obj_id, -1, -1, -1, opt);
}

static RcObjectState *object_state_find(RcWorld *world, int obj_id,
                                        int x, int y, int plane) {
    if (!world || x < 0 || y < 0 || plane < 0) return NULL;
    for (int i = 0; i < world->object_state_count; i++) {
        RcObjectState *st = &world->object_states[i];
        int base_match = st->x == x && st->y == y && st->plane == plane;
        int active_match = st->active_x == x && st->active_y == y
                         && st->active_plane == plane;
        if ((base_match || active_match)
                && (st->base_obj_id == obj_id
                    || st->active_obj_id == obj_id)) {
            return st;
        }
    }
    return NULL;
}

static const RcObjectState *object_state_find_const(const RcWorld *world,
                                                    int obj_id, int x, int y,
                                                    int plane) {
    if (!world || x < 0 || y < 0 || plane < 0) return NULL;
    for (int i = 0; i < world->object_state_count; i++) {
        const RcObjectState *st = &world->object_states[i];
        int base_match = st->x == x && st->y == y && st->plane == plane;
        int active_match = st->active_x == x && st->active_y == y
                         && st->active_plane == plane;
        if ((base_match || active_match)
                && (st->base_obj_id == obj_id
                    || st->active_obj_id == obj_id)) {
            return st;
        }
    }
    return NULL;
}

static RcObjectState *object_state_find_by_key(RcWorld *world,
                                               uint64_t placement_key) {
    if (!world || placement_key == 0) return NULL;
    for (int i = 0; i < world->object_state_count; i++) {
        RcObjectState *st = &world->object_states[i];
        if (st->placement_key == placement_key)
            return st;
    }
    return NULL;
}

static const RcObjectState *object_state_find_by_key_const(
    const RcWorld *world, uint64_t placement_key) {
    if (!world || placement_key == 0) return NULL;
    for (int i = 0; i < world->object_state_count; i++) {
        const RcObjectState *st = &world->object_states[i];
        if (st->placement_key == placement_key)
            return st;
    }
    return NULL;
}

static void object_state_to_placement(const RcObjectState *st,
                                      RcObjectPlacement *out) {
    if (!st || !out) return;
    memset(out, 0, sizeof(*out));
    out->obj_id = (uint32_t)st->active_obj_id;
    out->key = st->placement_key;
    out->x = (uint16_t)st->active_x;
    out->y = (uint16_t)st->active_y;
    out->mapsquare =
        (uint16_t)(((st->active_x >> 6) << 8) | (st->active_y >> 6));
    out->plane = (uint8_t)st->active_plane;
    out->type = st->active_type;
    out->rotation = st->active_rotation;
}

static int object_state_matches_target(const RcObjectState *st, int obj_id,
                                       int x, int y, int plane) {
    if (!st) return 0;
    if (obj_id >= 0 && st->base_obj_id != obj_id
            && st->active_obj_id != obj_id) {
        return 0;
    }
    if (x < 0 || y < 0 || plane < 0)
        return 1;
    int base_match = st->x == x && st->y == y && st->plane == plane;
    int active_match = st->active_x == x && st->active_y == y
                     && st->active_plane == plane;
    return base_match || active_match;
}

static RcObjectState *object_state_get_key(RcWorld *world, int obj_id,
                                           int x, int y, int plane,
                                           uint64_t placement_key) {
    RcObjectState *st = placement_key
        ? object_state_find_by_key(world, placement_key)
        : object_state_find(world, obj_id, x, y, plane);
    if (st && placement_key
            && !object_state_matches_target(st, obj_id, x, y, plane)) {
        return NULL;
    }
    if (st || !world || world->object_state_count >= RC_MAX_OBJECT_STATES) {
        return st;
    }
    st = &world->object_states[world->object_state_count++];
    st->placement_key = 0;
    st->base_obj_id = obj_id;
    st->active_obj_id = obj_id;
    st->x = x;
    st->y = y;
    st->plane = plane;
    st->active_x = x;
    st->active_y = y;
    st->active_plane = plane;
    st->base_type = 10;
    st->base_rotation = 0;
    st->active_type = st->base_type;
    st->active_rotation = st->base_rotation;
    st->respawn_tick = 0;
    st->revert_tick = 0;
    st->flags = 0;
    RcObjectPlacement placement;
    if (object_exact_placement_key(obj_id, x, y, plane, placement_key,
                                   &placement)) {
        st->placement_key = placement.key;
        st->base_type = placement.type;
        st->base_rotation = placement.rotation;
        st->active_type = placement.type;
        st->active_rotation = placement.rotation;
    }
    return st;
}

static RcObjectState *object_state_get(RcWorld *world, int obj_id,
                                       int x, int y, int plane) {
    return object_state_get_key(world, obj_id, x, y, plane, 0);
}

static int object_exact_placement(int obj_id, int x, int y, int plane,
                                  RcObjectPlacement *out) {
    if (obj_id < 0 || x < 0 || y < 0 || plane < 0) return 0;
    RcObjectPlacement rows[32];
    int count = rc_object_placements_at(x, y, plane, rows, 32);
    for (int i = 0; i < count; i++) {
        if ((int)rows[i].obj_id != obj_id) continue;
        if (out) *out = rows[i];
        return 1;
    }
    return 0;
}

static int object_exact_placement_key(int obj_id, int x, int y, int plane,
                                      uint64_t placement_key,
                                      RcObjectPlacement *out) {
    if (placement_key == 0)
        return object_exact_placement(obj_id, x, y, plane, out);
    if (x < 0 || y < 0 || plane < 0)
        return 0;
    RcObjectPlacement rows[32];
    int count = rc_object_placements_at(x, y, plane, rows, 32);
    for (int i = 0; i < count; i++) {
        if (rows[i].key != placement_key)
            continue;
        if (obj_id >= 0 && (int)rows[i].obj_id != obj_id)
            continue;
        if (out) *out = rows[i];
        return 1;
    }
    return 0;
}

static int placement_dimensions(const RcObjectDef *def,
                                const RcObjectPlacement *placement,
                                int *out_w, int *out_l) {
    if (!placement) return 0;
    int w = def && def->width > 0 ? def->width : 1;
    int l = def && def->length > 0 ? def->length : 1;
    if (placement->rotation & 1u) {
        int tmp = w;
        w = l;
        l = tmp;
    }
    if (out_w) *out_w = w;
    if (out_l) *out_l = l;
    return 1;
}

static int current_object_placement(const RcWorld *world, int obj_id,
                                    int x, int y, int plane,
                                    RcObjectPlacement *out) {
    return current_object_placement_key(world, obj_id, x, y, plane, 0, out);
}

static int current_object_placement_key(const RcWorld *world, int obj_id,
                                        int x, int y, int plane,
                                        uint64_t placement_key,
                                        RcObjectPlacement *out) {
    if (placement_key != 0) {
        const RcObjectState *st =
            object_state_find_by_key_const(world, placement_key);
        if (st) {
            if (!object_state_matches_target(st, obj_id, x, y, plane))
                return 0;
            if (out) object_state_to_placement(st, out);
            return 1;
        }
        return object_exact_placement_key(obj_id, x, y, plane, placement_key,
                                          out);
    }
    if (object_exact_placement(obj_id, x, y, plane, out))
        return 1;
    const RcObjectState *st = object_state_find_const(world, obj_id, x, y,
                                                      plane);
    if (!st) return 0;
    if (out) object_state_to_placement(st, out);
    return 1;
}

static int wall_side_reaches(const RcObjectPlacement *p, int tx, int ty) {
    int x = p->x;
    int y = p->y;
    int r = p->rotation & 3;
    if (tx == x && ty == y)
        return 1;
    if (p->type == 0 || p->type == 3) {
        if (r == 0) return tx == x - 1 && ty == y;
        if (r == 1) return tx == x && ty == y + 1;
        if (r == 2) return tx == x + 1 && ty == y;
        return tx == x && ty == y - 1;
    }
    if (p->type == 2) {
        if (r == 0) return (tx == x - 1 && ty == y)
                         || (tx == x && ty == y + 1);
        if (r == 1) return (tx == x + 1 && ty == y)
                         || (tx == x && ty == y + 1);
        if (r == 2) return (tx == x + 1 && ty == y)
                         || (tx == x && ty == y - 1);
        return (tx == x - 1 && ty == y)
            || (tx == x && ty == y - 1);
    }
    if (p->type == 1) {
        if (r == 0) return tx == x - 1 && ty == y + 1;
        if (r == 1) return tx == x + 1 && ty == y + 1;
        if (r == 2) return tx == x + 1 && ty == y - 1;
        return tx == x - 1 && ty == y - 1;
    }
    return 0;
}

static int rotate_block_access_flags(int flags, int angle) {
    angle &= 3;
    if (angle == 0) return flags & 0xF;
    return ((flags << angle) & 0xF) | ((flags & 0xF) >> (4 - angle));
}

static int rectangle_side_reaches(const RcWorld *world, int plane,
                                  int min_x, int min_y, int max_x,
                                  int max_y, int tx, int ty,
                                  int block_access) {
    if (tx >= min_x && tx <= max_x && ty >= min_y && ty <= max_y)
        return 1;
    if (tx == min_x - 1 && ty >= min_y && ty <= max_y
            && (block_access & RC_BLOCK_ACCESS_WEST) == 0) {
        return (rc_get_flags(world ? &world->map : NULL, tx, ty, plane)
                & COL_WALL_E) == 0;
    }
    if (tx == max_x + 1 && ty >= min_y && ty <= max_y
            && (block_access & RC_BLOCK_ACCESS_EAST) == 0) {
        return (rc_get_flags(world ? &world->map : NULL, tx, ty, plane)
                & COL_WALL_W) == 0;
    }
    if (ty == min_y - 1 && tx >= min_x && tx <= max_x
            && (block_access & RC_BLOCK_ACCESS_SOUTH) == 0) {
        return (rc_get_flags(world ? &world->map : NULL, tx, ty, plane)
                & COL_WALL_N) == 0;
    }
    if (ty == max_y + 1 && tx >= min_x && tx <= max_x
            && (block_access & RC_BLOCK_ACCESS_NORTH) == 0) {
        return (rc_get_flags(world ? &world->map : NULL, tx, ty, plane)
                & COL_WALL_S) == 0;
    }
    return 0;
}

static int tile_reaches_object_target(const RcWorld *world,
                                      const RcInteractionTarget *target,
                                      int tx, int ty) {
    if (!target || target->kind != RC_INTERACTION_OBJECT)
        return 0;
    RcObjectPlacement placement;
    if (!current_object_placement_key(world, target->definition_id,
                                      target->tile_x, target->tile_y,
                                      target->plane,
                                      target->placement_key, &placement)) {
        int min_x = target->tile_x;
        int min_y = target->tile_y;
        int max_x = target->tile_x + target->footprint_width - 1;
        int max_y = target->tile_y + target->footprint_height - 1;
        return g_rc_object_placement_count == 0
            && tile_distance_to_target_rect(tx, ty, min_x, min_y, max_x,
                                            max_y) <= 1;
    }
    if (placement.type <= 3)
        return wall_side_reaches(&placement, tx, ty);

    const RcObjectDef *def = rc_object_def_get((int)placement.obj_id);
    int w = 1, l = 1;
    placement_dimensions(def, &placement, &w, &l);
    int min_x = placement.x;
    int min_y = placement.y;
    int max_x = placement.x + w - 1;
    int max_y = placement.y + l - 1;
    int block_access =
        rotate_block_access_flags(def ? def->force_approach : 0,
                                  placement.rotation);
    return rectangle_side_reaches(world, target->plane, min_x, min_y,
                                  max_x, max_y, tx, ty, block_access);
}

static int tile_distance_to_placement(const RcObjectPlacement *placement,
                                      const RcObjectDef *def,
                                      int source_x, int source_y) {
    int w = 1, l = 1;
    if (!placement_dimensions(def, placement, &w, &l)) return INT_MAX;
    int min_x = placement->x;
    int min_y = placement->y;
    int max_x = placement->x + w - 1;
    int max_y = placement->y + l - 1;
    int dx = 0;
    int dy = 0;
    if (source_x < min_x) dx = min_x - source_x;
    else if (source_x > max_x) dx = source_x - max_x;
    if (source_y < min_y) dy = min_y - source_y;
    else if (source_y > max_y) dy = source_y - max_y;
    return dx + dy;
}

static int source_tile_matches_placement(const RcObjectPlacement *placement,
                                         const RcObjectDef *def,
                                         int source_x, int source_y) {
    return tile_distance_to_placement(placement, def, source_x, source_y) <= 1;
}

static int first_transform_id(const RcObjectDef *def) {
    if (!def) return -1;
    for (int i = 0; i < def->transform_count
            && i < RC_OBJECT_MAX_TRANSFORMS; i++) {
        if (def->transforms[i] >= 0) return def->transforms[i];
    }
    return -1;
}

static int traversal_direction_score(const RcTraversalEdge *row) {
    if (!row) return 0;
    int delta = (int)row->dest_plane - (int)row->start_plane;
    if (string_has_ci(row->action, "down")
            || string_has_ci(row->action, "descend")) {
        if (delta < 0) return 2;
        if (delta > 0) return -2;
    } else if (string_has_ci(row->action, "up")
            || string_has_ci(row->action, "ascend")) {
        if (delta > 0) return 2;
        if (delta < 0) return -2;
    }
    return 0;
}

static int traversal_edge_better(const RcTraversalEdge *candidate,
                                 int candidate_dist,
                                 const RcTraversalEdge *best,
                                 int best_dist) {
    if (!candidate) return 0;
    if (!best) return 1;
    int candidate_score = traversal_direction_score(candidate);
    int best_score = traversal_direction_score(best);
    if (candidate_score != best_score)
        return candidate_score > best_score;
    return candidate_dist < best_dist;
}

static const RcTraversalEdge *object_traversal_edge(
    const RcWorld *world, int obj_id, int x, int y, int plane, int opt,
    uint64_t placement_key) {
    if (!world || !(world->enabled & RC_SUB_TRAVERSAL)
            || x < 0 || y < 0 || plane < 0 || opt < 0) {
        return NULL;
    }
    RcObjectPlacement placement;
    int have_placement = current_object_placement_key(
        world, obj_id, x, y, plane, placement_key, &placement);
    if (g_rc_object_placement_count > 0 && !have_placement) {
        return NULL;
    }
    const RcTraversalEdge *exact =
        rc_traversal_find(RC_TRAVERSAL_OBJECT, obj_id, x, y, plane, opt);
    if (!have_placement) return exact;

    int count = 0;
    const RcTraversalEdge *rows =
        rc_traversal_edges_for(RC_TRAVERSAL_OBJECT, obj_id, &count);
    const RcObjectDef *def = rc_object_def_get(obj_id);
    const RcTraversalEdge *player_source = NULL;
    int player_source_dist = INT_MAX;
    const RcTraversalEdge *placement_source = NULL;
    int placement_source_dist = INT_MAX;
    const RcTraversalEdge *nearest = NULL;
    int nearest_dist = INT_MAX;
    for (int i = 0; rows && i < count; i++) {
        const RcTraversalEdge *row = &rows[i];
        if (row->kind != RC_TRAVERSAL_OBJECT
                || row->source_id != (uint32_t)obj_id)
            continue;
        if ((int)row->option != opt || (int)row->start_plane != plane)
            continue;
        if (source_tile_matches_placement(&placement, def, row->start_x,
                                          row->start_y)) {
            int dx = (int)row->start_x - world->player.x;
            int dy = (int)row->start_y - world->player.y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            int dist = dx > dy ? dx : dy;
            if ((int)row->start_x == world->player.x
                    && (int)row->start_y == world->player.y
                    && (int)row->start_plane == world->player.plane
                    && traversal_edge_better(row, dist, player_source,
                                             player_source_dist)) {
                player_source = row;
                player_source_dist = dist;
            }
            if ((int)row->start_x == x && (int)row->start_y == y
                    && traversal_edge_better(row, dist, placement_source,
                                             placement_source_dist)) {
                placement_source = row;
                placement_source_dist = dist;
            }
            if (traversal_edge_better(row, dist, nearest, nearest_dist)) {
                nearest = row;
                nearest_dist = dist;
            }
        }
    }
    if (player_source) return player_source;
    if (placement_source) return placement_source;
    return nearest ? nearest : exact;
}

static int object_def_has_action(const RcObjectDef *def, int opt) {
    return def && opt >= 0 && opt < RC_OBJECT_ACTIONS
        && def->actions[opt][0] != '\0';
}

static int object_option_available(const RcObjectDef *def,
                                   const RcObjectBehavior *behavior,
                                   const RcTraversalEdge *edge, int opt) {
    if (opt < 0 || opt >= RC_OBJECT_ACTIONS)
        return 0;
    if (object_def_has_action(def, opt))
        return 1;
    if (behavior && (behavior->action_mask & (1u << opt)))
        return 1;
    return edge != NULL;
}

static uint32_t *map_tile_flags_mut(RcWorldMap *map, int x, int y,
                                    int plane) {
    if (!map || x < 0 || y < 0 || plane < 0 || plane >= RC_MAX_PLANES)
        return NULL;
    int region_x = x / RC_REGION_SIZE;
    int region_y = y / RC_REGION_SIZE;
    int local_x = x % RC_REGION_SIZE;
    int local_y = y % RC_REGION_SIZE;
    for (int i = 0; i < map->region_count; i++) {
        RcRegion *reg = &map->regions[i];
        if (reg->loaded && reg->region_x == region_x
                && reg->region_y == region_y) {
            return &reg->tiles[plane][local_x][local_y].collision_flags;
        }
    }
    return NULL;
}

static void set_map_wall_flag(RcWorld *world, int x, int y, int plane,
                              uint32_t flag, int set) {
    uint32_t *flags = map_tile_flags_mut(world ? &world->map : NULL, x, y,
                                         plane);
    if (!flags) return;
    if (set)
        *flags |= flag;
    else
        *flags &= ~flag;
}

static void apply_wall_shape_collision(RcWorld *world, int x, int y,
                                       int plane, int type, int rotation,
                                       int set) {
    int r = rotation & 3;
    if (type == 0 || type == 3) {
        if (r == 0) {
            set_map_wall_flag(world, x, y, plane, COL_WALL_W, set);
            set_map_wall_flag(world, x - 1, y, plane, COL_WALL_E, set);
        } else if (r == 1) {
            set_map_wall_flag(world, x, y, plane, COL_WALL_N, set);
            set_map_wall_flag(world, x, y + 1, plane, COL_WALL_S, set);
        } else if (r == 2) {
            set_map_wall_flag(world, x, y, plane, COL_WALL_E, set);
            set_map_wall_flag(world, x + 1, y, plane, COL_WALL_W, set);
        } else {
            set_map_wall_flag(world, x, y, plane, COL_WALL_S, set);
            set_map_wall_flag(world, x, y - 1, plane, COL_WALL_N, set);
        }
    } else if (type == 2) {
        if (r == 0) {
            set_map_wall_flag(world, x, y, plane, COL_WALL_W | COL_WALL_N,
                              set);
            set_map_wall_flag(world, x - 1, y, plane, COL_WALL_E, set);
            set_map_wall_flag(world, x, y + 1, plane, COL_WALL_S, set);
        } else if (r == 1) {
            set_map_wall_flag(world, x, y, plane, COL_WALL_E | COL_WALL_N,
                              set);
            set_map_wall_flag(world, x + 1, y, plane, COL_WALL_W, set);
            set_map_wall_flag(world, x, y + 1, plane, COL_WALL_S, set);
        } else if (r == 2) {
            set_map_wall_flag(world, x, y, plane, COL_WALL_E | COL_WALL_S,
                              set);
            set_map_wall_flag(world, x + 1, y, plane, COL_WALL_W, set);
            set_map_wall_flag(world, x, y - 1, plane, COL_WALL_N, set);
        } else {
            set_map_wall_flag(world, x, y, plane, COL_WALL_W | COL_WALL_S,
                              set);
            set_map_wall_flag(world, x - 1, y, plane, COL_WALL_E, set);
            set_map_wall_flag(world, x, y - 1, plane, COL_WALL_N, set);
        }
    } else if (type == 1) {
        if (r == 0) {
            set_map_wall_flag(world, x, y, plane, COL_WALL_NW, set);
            set_map_wall_flag(world, x - 1, y + 1, plane, COL_WALL_SE, set);
        } else if (r == 1) {
            set_map_wall_flag(world, x, y, plane, COL_WALL_NE, set);
            set_map_wall_flag(world, x + 1, y + 1, plane, COL_WALL_SW, set);
        } else if (r == 2) {
            set_map_wall_flag(world, x, y, plane, COL_WALL_SE, set);
            set_map_wall_flag(world, x + 1, y - 1, plane, COL_WALL_NW, set);
        } else {
            set_map_wall_flag(world, x, y, plane, COL_WALL_SW, set);
            set_map_wall_flag(world, x - 1, y - 1, plane, COL_WALL_NE, set);
        }
    }
}

static void apply_door_collision(RcWorld *world, int obj_id, int x, int y,
                                 int plane, int open) {
    RcObjectPlacement rows[16];
    int count = rc_object_placements_at(x, y, plane, rows, 16);
    for (int i = 0; i < count; i++) {
        if ((int)rows[i].obj_id != obj_id)
            continue;
        apply_wall_shape_collision(world, x, y, plane, rows[i].type,
                                   rows[i].rotation, !open);
        return;
    }
}

static void door_translate_open(int shape, int rotation, int *x, int *y) {
    if (!x || !y) return;
    int r = rotation & 3;
    if (shape == 0) {
        if (r == 0) (*x)--;
        else if (r == 1) (*y)++;
        else if (r == 2) (*x)++;
        else (*y)--;
    } else if (shape == 1) {
        if (r == 0) (*y)++;
        else if (r == 1) (*x)++;
        else if (r == 2) (*y)--;
        else (*x)--;
    }
}

static int string_has_ci(const char *s, const char *needle) {
    if (!s || !needle || !needle[0]) return 0;
    size_t n = strlen(needle);
    for (; *s; s++) {
        size_t i = 0;
        while (i < n && s[i]) {
            char a = s[i];
            char b = needle[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
            i++;
        }
        if (i == n) return 1;
    }
    return 0;
}

static int string_eq_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

static int object_action_opens_dynamic(const char *action) {
    return string_eq_ci(action, "open") || string_eq_ci(action, "unlock");
}

static int object_action_closes_dynamic(const char *action) {
    return string_eq_ci(action, "close") || string_eq_ci(action, "lock");
}

static int object_action_mutates_dynamic(const char *action) {
    return object_action_opens_dynamic(action)
        || object_action_closes_dynamic(action);
}

static void gate_right_translate_open(int rotation, int *x, int *y) {
    if (!x || !y) return;
    int r = rotation & 3;
    if (r == 0) {
        *x -= 2;
        *y -= 1;
    } else if (r == 1) {
        *x -= 1;
        *y += 2;
    } else if (r == 2) {
        *x += 2;
        *y += 1;
    } else {
        *x += 1;
        *y -= 2;
    }
}

static int traversal_edge_uses_shortcut_motion(const RcTraversalEdge *edge) {
    return edge && edge->action[0] &&
        (string_has_ci(edge->action, "cross")
         || string_has_ci(edge->action, "squeeze")
         || string_has_ci(edge->action, "jump")
         || string_has_ci(edge->action, "climb")
         || string_has_ci(edge->action, "balance"));
}

static int traversal_edge_is_agility_like(const RcTraversalEdge *edge) {
    if (!edge) return 0;
    return string_has_ci(edge->action, "squeeze")
        || string_has_ci(edge->action, "jump")
        || string_has_ci(edge->action, "balance")
        || string_has_ci(edge->target, "gap")
        || string_has_ci(edge->target, "railing");
}

static int player_can_use_agility_like_shortcut(const RcWorld *world) {
    if (!world || !(world->enabled & RC_SUB_SKILLS))
        return 1;
    return world->player.skills.base_level[SKILL_AGILITY] > 0
        && world->player.skills.boosted_level[SKILL_AGILITY] > 0;
}

static int action_climb_dir(const char *action, int *out_up) {
    if (!string_has_ci(action, "climb")) return 0;
    if (string_has_ci(action, "up")) {
        if (out_up) *out_up = 1;
        return 1;
    }
    if (string_has_ci(action, "down")) {
        if (out_up) *out_up = 0;
        return 1;
    }
    return 0;
}

static int object_action_climb_dir(const RcObjectDef *def, int opt,
                                   int *out_up) {
    if (!def || opt < 0 || opt >= RC_OBJECT_ACTIONS) return 0;
    return action_climb_dir(def->actions[opt], out_up);
}

static int object_def_has_climb_dir(const RcObjectDef *def, int up) {
    for (int i = 0; def && i < RC_OBJECT_ACTIONS; i++) {
        int action_up = 0;
        if (object_action_climb_dir(def, i, &action_up)
                && action_up == up) {
            return 1;
        }
    }
    return 0;
}

static void set_inferred_climb_edge(RcTraversalEdge *out,
                                    const RcObjectDef *def, int obj_id,
                                    int x, int y, int plane, int opt,
                                    int dest_x, int dest_y, int dest_plane) {
    memset(out, 0, sizeof(*out));
    out->kind = RC_TRAVERSAL_OBJECT;
    out->option = (uint8_t)opt;
    out->source_id = (uint32_t)obj_id;
    out->start_x = (uint16_t)x;
    out->start_y = (uint16_t)y;
    out->start_plane = (uint8_t)plane;
    out->dest_x = (uint16_t)dest_x;
    out->dest_y = (uint16_t)dest_y;
    out->dest_plane = (uint8_t)dest_plane;
    strncpy(out->action, def->actions[opt], sizeof(out->action) - 1);
    strncpy(out->target, def->name, sizeof(out->target) - 1);
}

static int infer_vertical_climb_edge(int obj_id, int x, int y, int plane,
                                     int opt, RcTraversalEdge *out) {
    if (!out || obj_id < 0 || x < 0 || y < 0 || plane < 0)
        return 0;
    const RcObjectDef *def = rc_object_def_get(obj_id);
    int up = 0;
    if (!object_action_climb_dir(def, opt, &up))
        return 0;

    RcObjectPlacement placement;
    if (object_exact_placement(obj_id, x, y, plane, &placement)) {
        int best_score = INT_MAX;
        int inferred_plane = -1;
        int inferred_x = -1;
        int inferred_y = -1;
        for (int i = 0; g_rc_traversal_edges && i < g_rc_traversal_edge_count; i++) {
            const RcTraversalEdge *row = &g_rc_traversal_edges[i];
            if (row->kind != RC_TRAVERSAL_OBJECT)
                continue;
            int row_up = 0;
            if (!action_climb_dir(row->action, &row_up) || row_up == up)
                continue;
            if ((int)row->dest_plane != plane)
                continue;
            if (up) {
                if ((int)row->start_plane <= plane)
                    continue;
            } else if ((int)row->start_plane >= plane) {
                continue;
            }
            int dist = tile_distance_to_placement(&placement, def,
                                                  row->dest_x, row->dest_y);
            int anchor_dist = abs((int)row->dest_x - x)
                            + abs((int)row->dest_y - y);
            int score = dist * 64
                      + abs((int)row->start_plane - plane) * 8
                      + anchor_dist;
            if (dist > 1 || score >= best_score)
                continue;
            best_score = score;
            inferred_x = row->start_x;
            inferred_y = row->start_y;
            inferred_plane = row->start_plane;
        }
        if (inferred_plane >= 0) {
            set_inferred_climb_edge(out, def, obj_id, x, y, plane, opt,
                                    inferred_x, inferred_y, inferred_plane);
            return 1;
        }
    }

    int best_plane = -1;
    for (int p = 0; p < RC_MAX_PLANES; p++) {
        if (p == plane) continue;
        if (up && p < plane) continue;
        if (!up && p > plane) continue;
        RcObjectPlacement rows[32];
        int n = rc_object_placements_at(x, y, p, rows, 32);
        for (int i = 0; i < n; i++) {
            const RcObjectDef *other = rc_object_def_get((int)rows[i].obj_id);
            if (!object_def_has_climb_dir(other, !up))
                continue;
            if (best_plane < 0
                    || (up && p < best_plane)
                    || (!up && p > best_plane)) {
                best_plane = p;
            }
        }
    }
    if (best_plane < 0)
        return 0;
    set_inferred_climb_edge(out, def, obj_id, x, y, plane, opt,
                            x, y, best_plane);
    return 1;
}

static void award_agility_like_shortcut_xp(RcWorld *world) {
    if (!world || !(world->enabled & RC_SUB_SKILLS))
        return;
    rc_add_xp(&world->player.skills, SKILL_AGILITY, 1);
}

static int dynamic_open_rotation(const RcObjectBehavior *behavior,
                                 const RcObjectDef *def,
                                 int shape, int base_rotation) {
    if (shape > 3)
        return base_rotation;
    int pair = behavior &&
        (behavior->flags & (RC_OBJ_BEHAVIOR_PAIR_LEFT |
                            RC_OBJ_BEHAVIOR_PAIR_RIGHT));
    if (!pair)
        return (base_rotation + 1) & 3;
    if (def && string_has_ci(def->name, "gate"))
        return (base_rotation + 3) & 3;
    if (behavior->flags & RC_OBJ_BEHAVIOR_PAIR_LEFT)
        return (base_rotation + 3) & 3;
    return (base_rotation + 1) & 3;
}

static void dynamic_open_tile(const RcObjectBehavior *behavior,
                              const RcObjectDef *def, int shape,
                              int rotation, int *x, int *y) {
    if (shape > 3)
        return;
    if (behavior && (behavior->flags & RC_OBJ_BEHAVIOR_PAIR_RIGHT)
            && def && string_has_ci(def->name, "gate") && shape == 0) {
        gate_right_translate_open(rotation, x, y);
        return;
    }
    door_translate_open(shape, rotation, x, y);
}

static void pair_left_to_right_delta(int rotation, int *dx, int *dy) {
    int r = rotation & 3;
    *dx = 0;
    *dy = 0;
    if (r == 0) *dy = 1;
    else if (r == 1) *dx = 1;
    else if (r == 2) *dy = -1;
    else *dx = -1;
}

static int find_paired_dynamic_placement(const RcObjectState *st,
                                         const RcObjectBehavior *behavior,
                                         RcObjectPlacement *out) {
    if (!st || !behavior ||
            !(behavior->flags & (RC_OBJ_BEHAVIOR_PAIR_LEFT |
                                 RC_OBJ_BEHAVIOR_PAIR_RIGHT))) {
        return 0;
    }
    int dx, dy;
    pair_left_to_right_delta(st->base_rotation, &dx, &dy);
    uint32_t need_flag = RC_OBJ_BEHAVIOR_PAIR_RIGHT;
    if (behavior->flags & RC_OBJ_BEHAVIOR_PAIR_RIGHT) {
        dx = -dx;
        dy = -dy;
        need_flag = RC_OBJ_BEHAVIOR_PAIR_LEFT;
    }
    RcObjectPlacement rows[16];
    int count = rc_object_placements_at(st->x + dx, st->y + dy, st->plane,
                                        rows, 16);
    for (int i = 0; i < count; i++) {
        const RcObjectBehavior *other =
            rc_object_behavior_get((int)rows[i].obj_id);
        if (!other)
            continue;
        if ((other->flags & need_flag) == 0)
            continue;
        if (rows[i].type != st->base_type)
            continue;
        if ((rows[i].rotation & 3) != (st->base_rotation & 3))
            continue;
        if (out) *out = rows[i];
        return 1;
    }
    return 0;
}

int rc_world_object_active_id(const RcWorld *world, int obj_id, int x, int y,
                              int plane) {
    if (!world || x < 0 || y < 0 || plane < 0) return obj_id;
    const RcObjectState *st = object_state_find_const(world, obj_id, x, y,
                                                      plane);
    if (st) return st->active_obj_id;
    return obj_id;
}

int rc_world_object_active_state(const RcWorld *world, int obj_id, int x,
                                 int y, int plane, RcObjectState *out) {
    if (!out) return 0;
    const RcObjectState *st = object_state_find_const(world, obj_id, x, y,
                                                      plane);
    if (!st) return 0;
    *out = *st;
    return 1;
}

int rc_world_object_active_state_by_key(const RcWorld *world,
                                        uint64_t placement_key,
                                        RcObjectState *out) {
    if (!out) return 0;
    const RcObjectState *st =
        object_state_find_by_key_const(world, placement_key);
    if (!st) return 0;
    *out = *st;
    return 1;
}

static void apply_dynamic_object_state(RcWorld *world, RcObjectState *st,
                                       int open,
                                       const RcObjectBehavior *behavior) {
    if (!world || !st) return;
    const RcObjectDef *base_def = rc_object_def_get(st->base_obj_id);
    if (open) {
        int transform = behavior && behavior->next_loc_stage >= 0
            ? behavior->next_loc_stage
            : first_transform_id(base_def);
        st->active_obj_id = transform >= 0 ? transform : st->base_obj_id;
        st->active_x = st->x;
        st->active_y = st->y;
        st->active_plane = st->plane;
        st->active_type = st->base_type;
        st->active_rotation = (uint8_t)dynamic_open_rotation(
            behavior, base_def, st->base_type, st->base_rotation);
        dynamic_open_tile(behavior, base_def, st->base_type,
                          st->base_rotation, &st->active_x, &st->active_y);
        st->revert_tick = world->tick + RC_OBJECT_DOOR_REVERT_TICKS;
        st->flags |= RC_OBJECT_STATE_OPEN | RC_OBJECT_STATE_DYNAMIC;
        schedule_object_state_tick(world, st->revert_tick);
    } else {
        reset_object_state_active(st);
    }
    apply_door_collision(world, st->base_obj_id, st->x, st->y, st->plane,
                         open);
}

static void apply_door_state(RcWorld *world, int obj_id, int x, int y,
                             int plane, uint64_t placement_key, int opt,
                             const RcObjectBehavior *behavior) {
    if (x < 0 || y < 0 || plane < 0) return;
    RcObjectState *st =
        object_state_get_key(world, obj_id, x, y, plane, placement_key);
    if (!st) return;
    const RcObjectDef *base_def = rc_object_def_get(st->base_obj_id);
    const RcObjectDef *clicked_def = rc_object_def_get(obj_id);
    const RcObjectDef *action_def = clicked_def ? clicked_def : base_def;
    const char *action = action_def ? action_def->actions[opt] : "";
    int open = object_action_opens_dynamic(action);
    int close = object_action_closes_dynamic(action);
    if (!open && !close) return;
    apply_dynamic_object_state(world, st, open, behavior);

    RcObjectPlacement pair;
    if (!find_paired_dynamic_placement(st, behavior, &pair))
        return;
    RcObjectState *pair_state = object_state_get(world, (int)pair.obj_id,
                                                 pair.x, pair.y, pair.plane);
    const RcObjectBehavior *pair_behavior =
        rc_object_behavior_get((int)pair.obj_id);
    if (pair_state && pair_behavior)
        apply_dynamic_object_state(world, pair_state, open, pair_behavior);
}

static void apply_altar_effect(RcWorld *world, const RcObjectDef *def,
                               int obj_id, int opt) {
    const char *action = def ? def->actions[opt] : "";
    int restores = !action || action[0] == '\0' || action[0] == 'P'
                || action[0] == 'R' || action[0] == 'W';
    if (restores && (world->enabled & RC_SUB_PRAYER)) {
        world->player.current_prayer_points =
            world->player.skills.base_level[SKILL_PRAYER] * 10;
        world->player.prayer_drain_counter = 0;
    }
    if (action && action[0] == 'O' && (world->enabled & RC_SUB_SKILLS)) {
        world->player.skill_action = obj_id;
        world->player.skill_timer = 1;
    }
}

static int api_apply_object_interaction(RcWorld *world, const RcObjectDef *def,
                                        const RcObjectBehavior *behavior,
                                        int obj_id, int x, int y, int plane,
                                        uint64_t placement_key, int opt) {
    if (!world || opt < 0 || opt >= RC_OBJECT_ACTIONS) return 0;
    RcObjectState *state = placement_key
        ? object_state_find_by_key(world, placement_key)
        : object_state_find(world, obj_id, x, y, plane);
    if (state && placement_key
            && !object_state_matches_target(state, obj_id, x, y, plane)) {
        return 0;
    }
    const RcObjectBehavior *effective_behavior = behavior;
    if (state) {
        const RcObjectBehavior *base_behavior =
            rc_object_behavior_get(state->base_obj_id);
        if (base_behavior)
            effective_behavior = base_behavior;
    }
    RcTraversalEdge inferred_edge;
    const RcTraversalEdge *edge =
        object_traversal_edge(world, obj_id, x, y, plane, opt,
                              placement_key);
    if (!edge && infer_vertical_climb_edge(obj_id, x, y, plane, opt,
                                           &inferred_edge)) {
        edge = &inferred_edge;
    }
    if (!object_option_available(def, effective_behavior, edge, opt)) {
        return 0;
    }
    int agility_like_shortcut = traversal_edge_is_agility_like(edge);
    if (agility_like_shortcut && !player_can_use_agility_like_shortcut(world))
        return 0;
    if (effective_behavior && rc_player_open_storage_object(world, obj_id, opt))
        return 1;
    world->player.interact_type = RC_INTERACT_OBJECT;
    world->player.interact_target = obj_id;
    world->player.interact_option = opt;
    const char *action = def && opt >= 0 && opt < RC_OBJECT_ACTIONS
                       ? def->actions[opt] : "";
    if (effective_behavior
            && (effective_behavior->flags & RC_OBJ_BEHAVIOR_DOOR)
            && object_action_mutates_dynamic(action)) {
        apply_door_state(world, obj_id, x, y, plane, placement_key, opt,
                         effective_behavior);
        start_player_action_lock(world, 1);
    }
    if (effective_behavior && (effective_behavior->flags & RC_OBJ_BEHAVIOR_ALTAR)
            && effective_behavior->skill == RC_OBJ_SKILL_PRAYER) {
        apply_altar_effect(world, def, obj_id, opt);
    }
    if (x >= 0 && y >= 0 && plane >= 0
            && effective_behavior
            && (effective_behavior->flags & RC_OBJ_BEHAVIOR_RESOURCE)
            && (world->enabled & RC_SUB_SKILLS)) {
        const RcGatheringNode *node = rc_gathering_node_find(
            obj_id, x, y, plane);
        if (node && (node->action_mask & (1u << opt))) {
            world->player.skill_action = obj_id;
            world->player.skill_timer = 1;
            world->player.skill_target_x = x;
            world->player.skill_target_y = y;
        }
    }
    if (edge) {
        int climb = effective_behavior
            && (effective_behavior->flags & (RC_OBJ_BEHAVIOR_LADDER |
                                             RC_OBJ_BEHAVIOR_STAIR));
        int shortcut = !climb && traversal_edge_uses_shortcut_motion(edge);
        if (climb) {
            start_player_action_lock(world, 2);
            schedule_player_traversal(world, edge, 1);
        } else if (shortcut) {
            if (agility_like_shortcut)
                award_agility_like_shortcut_xp(world);
            start_player_action_lock(world, 1);
            schedule_player_traversal(world, edge, 1);
        } else {
            rc_player_apply_traversal(world, edge);
            world->player.action_lock_timer = 0;
        }
    }
    return 1;
}

static RcInteractionHandlerResult api_default_object_handler(
    RcWorld *world, RcPlayer *player, const RcPendingInteraction *pending,
    void *ctx) {
    (void)player;
    (void)ctx;
    if (!world || !pending ||
            pending->target.kind != RC_INTERACTION_OBJECT) {
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_INVALID_TARGET, "Invalid object interaction");
    }
    int opt = api_option_from_interaction_op(pending->op);
    const RcObjectDef *def = rc_object_def_get(pending->target.definition_id);
    const RcObjectBehavior *behavior =
        rc_object_behavior_get(pending->target.definition_id);
    if (!api_apply_object_interaction(world, def, behavior,
                                      pending->target.definition_id,
                                      pending->target.tile_x,
                                      pending->target.tile_y,
                                      pending->target.plane,
                                      pending->target.placement_key, opt)) {
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_OPTION_UNAVAILABLE,
            "Object option unavailable");
    }
    return rc_interaction_result_complete();
}

static RcInteractionHandlerResult api_default_ground_item_handler(
    RcWorld *world, RcPlayer *player, const RcPendingInteraction *pending,
    void *ctx) {
    (void)player;
    (void)ctx;
    if (!world || !pending ||
            pending->target.kind != RC_INTERACTION_GROUND_ITEM) {
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_INVALID_TARGET,
            "Invalid ground item interaction");
    }
    if (pending->op == RC_INTERACTION_USE_ON) {
        if (pending->source_item_id < 0) {
            return rc_interaction_result_failure(
                RC_INTERACTION_FAIL_INVALID_SOURCE,
                "Invalid source item");
        }
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_NO_HANDLER,
            "No item-on-ground item handler");
    }
    if (pending->op == RC_INTERACTION_SPELL_ON) {
        if (pending->source_spell_id < 0) {
            return rc_interaction_result_failure(
                RC_INTERACTION_FAIL_INVALID_SOURCE,
                "Invalid source spell");
        }
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_NO_HANDLER,
            "No spell-on-ground item handler");
    }
    int idx = pending->target.ground_item_instance;
    if (idx < 0 || idx >= world->ground_item_count) {
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_TARGET_MISSING, "Ground item missing");
    }
    int take = rc_player_take_ground_item(
        world, idx, pending->target.entity_uid,
        pending->target.entity_generation);
    if (take == RC_GROUND_TAKE_OK)
        return rc_interaction_result_complete();
    if (take == RC_GROUND_TAKE_STALE) {
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_TARGET_VERSION_CHANGED,
            "Ground item changed");
    }
    if (take == RC_GROUND_TAKE_FULL) {
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_INVALID_TARGET, "Inventory full");
    }
    return rc_interaction_result_failure(
        RC_INTERACTION_FAIL_TARGET_MISSING, "Ground item missing");
}

static void api_register_default_object_handlers(void) {
    for (int i = 0; i < 5; i++) {
        RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
        key.kind = RC_INTERACTION_OBJECT;
        key.op = rc_interaction_op_from_option(i);
        if (rc_interaction_find_handler(&key) < 0) {
            rc_interaction_register_handler(&key, api_default_object_handler,
                                            NULL);
        }
    }
}

static void api_register_default_ground_item_handlers(void) {
    RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
    key.kind = RC_INTERACTION_GROUND_ITEM;
    key.op = RC_INTERACTION_OP1;
    if (rc_interaction_find_handler(&key) < 0) {
        rc_interaction_register_handler(&key,
                                        api_default_ground_item_handler,
                                        NULL);
    }
    key.op = RC_INTERACTION_USE_ON;
    if (rc_interaction_find_handler(&key) < 0) {
        rc_interaction_register_handler(&key,
                                        api_default_ground_item_handler,
                                        NULL);
    }
    key.op = RC_INTERACTION_SPELL_ON;
    if (rc_interaction_find_handler(&key) < 0) {
        rc_interaction_register_handler(&key,
                                        api_default_ground_item_handler,
                                        NULL);
    }
}

int rc_player_interact_object_at(RcWorld *world, int obj_id, int x, int y,
                                 int plane, int opt) {
    return rc_player_interact_object_placement(world, obj_id, x, y, plane, 0,
                                               opt);
}

int rc_player_interact_object_placement(RcWorld *world, int obj_id, int x,
                                        int y, int plane,
                                        uint64_t placement_key, int opt) {
    if (!world || !rc_player_action_allowed(world->enabled,
                                            RC_PLAYER_ACTION_INTERACT_OBJECT)) {
        return 0;
    }
    if (world->player.action_lock_timer > 0) return 0;
    if (x >= 0 && y >= 0 && plane >= 0
            && plane != world->player.plane) {
        return 0;
    }
    if (x >= 0 && y >= 0 && plane >= 0
            && g_rc_object_placement_count > 0
            && !current_object_placement_key(world, obj_id, x, y, plane,
                                             placement_key, NULL)) {
        return 0;
    }
    const RcObjectDef *def = rc_object_def_get(obj_id);
    if (rc_encounter_interact_object(world, obj_id, def ? def->name : "",
                                     x, y, plane, opt)) {
        api_stop_player_combat(world);
        return 1;
    }
    const RcObjectBehavior *behavior = rc_object_behavior_get(obj_id);
    const RcTraversalEdge *edge =
        object_traversal_edge(world, obj_id, x, y, plane, opt,
                              placement_key);
    if (opt < 0 || opt >= RC_OBJECT_ACTIONS) return 0;
    if (!object_option_available(def, behavior, edge, opt)) {
        return 0;
    }
    if (traversal_edge_is_agility_like(edge)
            && !player_can_use_agility_like_shortcut(world)) {
        return 0;
    }
    RcObjectState *state = object_state_find(world, obj_id, x, y, plane);
    if (state && (state->flags & RC_OBJECT_STATE_DEPLETED)) return 0;
    if (x >= 0 && y >= 0 && plane >= 0) {
        RcInteractionTarget target =
            api_placed_object_interaction_target(world, def, obj_id, x, y,
                                                 plane, placement_key);
        const char *option = def ? def->actions[opt] : "";
        if ((!option || !option[0]) && edge)
            option = edge->action;
        if (!rc_interaction_begin(&world->player, 0,
                                  rc_interaction_op_from_option(opt),
                                  option, &target, 1)) {
            return 0;
        }
        api_stop_player_combat(world);
        return api_prepare_spatial_interaction(world);
    }
    int applied = api_apply_object_interaction(world, def, behavior, obj_id,
                                               x, y, plane, 0, opt);
    if (applied) api_stop_player_combat(world);
    return applied;
}

int rc_player_interact_inventory_item(RcWorld *world, int inv_slot,
                                      int option) {
    if (!world || inv_slot < 0 || inv_slot >= RC_INVENTORY_SIZE) return 0;
    RcPlayer *p = &world->player;
    if (p->inventory[inv_slot].item_id < 0 ||
            p->inventory[inv_slot].quantity <= 0) {
        return 0;
    }
    RcInteractionOp op = rc_interaction_op_from_option(option);
    RcInteractionTarget target =
        api_inventory_item_interaction_target(p, inv_slot);
    int begun = rc_interaction_begin_with_source(
        p, 0, op, "Op", &target, 0, target.definition_id,
        RC_INTERACTION_KEY_ANY, RC_INTERACTION_KEY_ANY,
        RC_INTERACTION_KEY_ANY);
    if (begun) api_stop_player_combat(world);
    return begun;
}

int rc_player_interact_equipment_item(RcWorld *world, int equip_slot,
                                      int option) {
    if (!world || equip_slot < 0 || equip_slot >= RC_EQUIP_COUNT) return 0;
    RcPlayer *p = &world->player;
    if (p->equipment[equip_slot].item_id < 0 ||
            p->equipment[equip_slot].quantity <= 0) {
        return 0;
    }
    RcInteractionOp op = rc_interaction_op_from_option(option);
    RcInteractionTarget target =
        api_equipment_item_interaction_target(p, equip_slot);
    int begun = rc_interaction_begin_with_source(
        p, 0, op, "Op", &target, 0, target.definition_id,
        RC_INTERACTION_KEY_ANY, RC_INTERACTION_KEY_ANY,
        RC_INTERACTION_KEY_ANY);
    if (begun) api_stop_player_combat(world);
    return begun;
}

int rc_player_widget_action(RcWorld *world, int widget_id,
                            int component_id, int action) {
    if (!world || (widget_id < 0 && component_id < 0) || action < 0) {
        return 0;
    }
    RcInteractionTarget target =
        api_widget_interaction_target(widget_id, component_id, action);
    int begun = rc_interaction_begin_with_source(
        &world->player, 0, RC_INTERACTION_WIDGET_ACTION, "Widget",
        &target, 0, RC_INTERACTION_KEY_ANY, RC_INTERACTION_KEY_ANY,
        widget_id, component_id);
    if (begun) api_stop_player_combat(world);
    return begun;
}

int rc_player_use_inventory_item_on_npc(RcWorld *world, int inv_slot,
                                        int npc_uid) {
    if (!world || inv_slot < 0 || inv_slot >= RC_INVENTORY_SIZE) return 0;
    RcPlayer *p = &world->player;
    int item_id = p->inventory[inv_slot].item_id;
    if (item_id < 0 || p->inventory[inv_slot].quantity <= 0) return 0;
    RcNpc *npc = api_find_npc_by_uid(world, npc_uid);
    if (!npc || npc->is_dead) return 0;
    RcInteractionTarget target = api_npc_interaction_target(npc);
    int begun = rc_interaction_begin_with_source(
        p, 0, RC_INTERACTION_USE_ON, "Use", &target, 1, item_id,
        RC_INTERACTION_KEY_ANY, RC_INTERACTION_KEY_ANY,
        RC_INTERACTION_KEY_ANY);
    if (begun) api_stop_player_combat(world);
    return begun ? api_prepare_spatial_interaction(world) : 0;
}

int rc_player_use_inventory_item_on_inventory_item(RcWorld *world,
                                                   int source_slot,
                                                   int target_slot) {
    if (!world || source_slot < 0 || source_slot >= RC_INVENTORY_SIZE
            || target_slot < 0 || target_slot >= RC_INVENTORY_SIZE) {
        return 0;
    }
    RcPlayer *p = &world->player;
    int item_id = p->inventory[source_slot].item_id;
    if (item_id < 0 || p->inventory[source_slot].quantity <= 0) return 0;
    if (p->inventory[target_slot].item_id < 0
            || p->inventory[target_slot].quantity <= 0) {
        return 0;
    }
    RcInteractionTarget target =
        api_inventory_item_interaction_target(p, target_slot);
    int begun = rc_interaction_begin_with_source(
        p, 0, RC_INTERACTION_USE_ON, "Use", &target, 0, item_id,
        RC_INTERACTION_KEY_ANY, RC_INTERACTION_KEY_ANY,
        RC_INTERACTION_KEY_ANY);
    if (begun) api_stop_player_combat(world);
    return begun;
}

int rc_player_use_inventory_item_on_object(RcWorld *world, int inv_slot,
                                           int obj_id, int x, int y,
                                           int plane) {
    return rc_player_use_inventory_item_on_object_placement(
        world, inv_slot, obj_id, x, y, plane, 0);
}

int rc_player_use_inventory_item_on_object_placement(
    RcWorld *world, int inv_slot, int obj_id, int x, int y, int plane,
    uint64_t placement_key) {
    if (!world || inv_slot < 0 || inv_slot >= RC_INVENTORY_SIZE) return 0;
    RcPlayer *p = &world->player;
    int item_id = p->inventory[inv_slot].item_id;
    if (item_id < 0 || p->inventory[inv_slot].quantity <= 0) return 0;
    if (x >= 0 && y >= 0 && plane >= 0 && plane != p->plane) return 0;
    const RcObjectDef *def = rc_object_def_get(obj_id);
    RcInteractionTarget target =
        api_placed_object_interaction_target(world, def, obj_id, x, y, plane,
                                             placement_key);
    int begun = rc_interaction_begin_with_source(
        p, 0, RC_INTERACTION_USE_ON, "Use", &target, 1, item_id,
        RC_INTERACTION_KEY_ANY, RC_INTERACTION_KEY_ANY,
        RC_INTERACTION_KEY_ANY);
    if (begun) api_stop_player_combat(world);
    return begun ? api_prepare_spatial_interaction(world) : 0;
}

int rc_player_use_inventory_item_on_ground_item(RcWorld *world,
                                                int inv_slot,
                                                int ground_item_idx) {
    if (!world || inv_slot < 0 || inv_slot >= RC_INVENTORY_SIZE ||
            ground_item_idx < 0 ||
            ground_item_idx >= world->ground_item_count) {
        return 0;
    }
    RcPlayer *p = &world->player;
    int item_id = p->inventory[inv_slot].item_id;
    if (item_id < 0 || p->inventory[inv_slot].quantity <= 0) return 0;
    RcGroundItem *ground = &world->ground_items[ground_item_idx];
    if (!ground->active) return 0;
    if (ground->plane != p->plane) return 0;
    RcInteractionTarget target =
        api_ground_item_interaction_target(ground, ground_item_idx);
    int begun = rc_interaction_begin_with_source(
        p, 0, RC_INTERACTION_USE_ON, "Use", &target, 0, item_id,
        RC_INTERACTION_KEY_ANY, RC_INTERACTION_KEY_ANY,
        RC_INTERACTION_KEY_ANY);
    if (begun) api_stop_player_combat(world);
    return begun ? api_prepare_spatial_interaction(world) : 0;
}

int rc_player_use_inventory_item_on_widget(RcWorld *world, int inv_slot,
                                           int widget_id, int component_id) {
    if (!world || inv_slot < 0 || inv_slot >= RC_INVENTORY_SIZE
            || (widget_id < 0 && component_id < 0)) {
        return 0;
    }
    RcPlayer *p = &world->player;
    int item_id = p->inventory[inv_slot].item_id;
    if (item_id < 0 || p->inventory[inv_slot].quantity <= 0) return 0;
    RcInteractionTarget target =
        api_widget_interaction_target(widget_id, component_id, 0);
    int begun = rc_interaction_begin_with_source(
        p, 0, RC_INTERACTION_USE_ON, "Use", &target, 0, item_id,
        RC_INTERACTION_KEY_ANY, RC_INTERACTION_KEY_ANY,
        RC_INTERACTION_KEY_ANY);
    if (begun) api_stop_player_combat(world);
    return begun;
}

int rc_player_cast_spell_on_npc(RcWorld *world, int spell_id, int npc_uid) {
    if (!world || spell_id < 0) return 0;
    if (!rc_spell_def_get(spell_id)) return 0;
    RcNpc *npc = api_find_npc_by_uid(world, npc_uid);
    if (!npc || npc->is_dead) return 0;
    if (npc->plane != world->player.plane) return 0;
    RcInteractionTarget target = api_npc_interaction_target(npc);
    target.content_group = RC_INTERACTION_CONTENT_GROUP_NPC_ATTACK;
    int begun = rc_interaction_begin_with_source(
        &world->player, 0, RC_INTERACTION_SPELL_ON, "Cast", &target, 10,
        RC_INTERACTION_KEY_ANY, spell_id, RC_INTERACTION_KEY_ANY,
        RC_INTERACTION_KEY_ANY);
    if (begun) api_stop_player_combat(world);
    return begun ? api_prepare_spatial_interaction(world) : 0;
}

int rc_player_cast_spell_on_inventory_item(RcWorld *world, int spell_id,
                                           int target_slot) {
    if (!world || spell_id < 0 || target_slot < 0
            || target_slot >= RC_INVENTORY_SIZE) {
        return 0;
    }
    RcPlayer *p = &world->player;
    if (p->inventory[target_slot].item_id < 0
            || p->inventory[target_slot].quantity <= 0) {
        return 0;
    }
    RcInteractionTarget target =
        api_inventory_item_interaction_target(p, target_slot);
    int begun = rc_interaction_begin_with_source(
        p, 0, RC_INTERACTION_SPELL_ON, "Cast", &target, 0,
        RC_INTERACTION_KEY_ANY, spell_id, RC_INTERACTION_KEY_ANY,
        RC_INTERACTION_KEY_ANY);
    if (begun) api_stop_player_combat(world);
    return begun;
}

int rc_player_cast_spell_on_object(RcWorld *world, int spell_id,
                                   int obj_id, int x, int y, int plane) {
    return rc_player_cast_spell_on_object_placement(
        world, spell_id, obj_id, x, y, plane, 0);
}

int rc_player_cast_spell_on_object_placement(
    RcWorld *world, int spell_id, int obj_id, int x, int y, int plane,
    uint64_t placement_key) {
    if (!world || spell_id < 0) return 0;
    if (x >= 0 && y >= 0 && plane >= 0
            && plane != world->player.plane) {
        return 0;
    }
    const RcObjectDef *def = rc_object_def_get(obj_id);
    RcInteractionTarget target =
        api_placed_object_interaction_target(world, def, obj_id, x, y, plane,
                                             placement_key);
    int begun = rc_interaction_begin_with_source(
        &world->player, 0, RC_INTERACTION_SPELL_ON, "Cast", &target, 10,
        RC_INTERACTION_KEY_ANY, spell_id, RC_INTERACTION_KEY_ANY,
        RC_INTERACTION_KEY_ANY);
    if (begun) api_stop_player_combat(world);
    return begun ? api_prepare_spatial_interaction(world) : 0;
}

int rc_player_cast_spell_on_widget(RcWorld *world, int spell_id,
                                   int widget_id, int component_id) {
    if (!world || spell_id < 0 || (widget_id < 0 && component_id < 0)) {
        return 0;
    }
    RcInteractionTarget target =
        api_widget_interaction_target(widget_id, component_id, 0);
    int begun = rc_interaction_begin_with_source(
        &world->player, 0, RC_INTERACTION_SPELL_ON, "Cast", &target, 0,
        RC_INTERACTION_KEY_ANY, spell_id, RC_INTERACTION_KEY_ANY,
        RC_INTERACTION_KEY_ANY);
    if (begun) api_stop_player_combat(world);
    return begun;
}

int rc_player_cast_spell_on_ground_item(RcWorld *world, int spell_id,
                                        int ground_item_idx) {
    if (!world || spell_id < 0 || ground_item_idx < 0 ||
            ground_item_idx >= world->ground_item_count) {
        return 0;
    }
    RcGroundItem *ground = &world->ground_items[ground_item_idx];
    if (!ground->active) return 0;
    if (ground->plane != world->player.plane) return 0;
    RcInteractionTarget target =
        api_ground_item_interaction_target(ground, ground_item_idx);
    int begun = rc_interaction_begin_with_source(
        &world->player, 0, RC_INTERACTION_SPELL_ON, "Cast", &target, 10,
        RC_INTERACTION_KEY_ANY, spell_id, RC_INTERACTION_KEY_ANY,
        RC_INTERACTION_KEY_ANY);
    if (begun) api_stop_player_combat(world);
    return begun ? api_prepare_spatial_interaction(world) : 0;
}
