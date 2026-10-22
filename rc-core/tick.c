#include "api.h"
#include "config.h"
#include "drops.h"
#include "events.h"
#include "encounter.h"
#include "interaction.h"
#include "items.h"
#include "npc.h"
#include "combat.h"
#include "consumables.h"
#include "combat_hit.h"
#include "prayer.h"
#include "skills.h"
#include "pathfinding.h"
#include "objects.h"
#include "player_actions.h"
#include "player_command.h"
#include "spells.h"
#include "storage.h"

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RC_OBJECT_DOOR_REVERT_TICKS 100
static void process_player_input(RcWorld *world) {
    if (world && world->player_commands.count > 0)
        rc_player_command_process(world);
}

static const RcNpcDef *npc_def_for(const RcWorld *world, const RcNpc *npc) {
    return rc_npc_def_for_npc(world, npc);
}

static RcNpc *api_find_npc_by_uid(RcWorld *world, int uid);
static int api_option_from_interaction_op(RcInteractionOp op);
static int api_npc_attack_option_index(const RcNpcDef *def);
static int api_register_default_npc_handlers(RcWorld *world);
static int api_register_default_object_handlers(RcWorld *world);
static int api_register_default_ground_item_handlers(RcWorld *world);
static int api_route_player(RcWorld *world, const RcRouteTarget *target,
                            int entity_width, int entity_height,
                            int allow_alternative);
static void process_player_interaction(RcWorld *world, int dispatch_ready);
static int object_exact_placement(int obj_id, int x, int y, int plane,
                                  RcObjectPlacement *out);
static int current_object_placement(const RcWorld *world, int obj_id,
                                    int x, int y, int plane,
                                    RcObjectPlacement *out);
static int placement_dimensions(const RcObjectDef *def,
                                const RcObjectPlacement *placement,
                                int *out_w, int *out_l);
static int tile_reaches_object_target(const RcWorld *world,
                                      const RcInteractionTarget *target,
                                      int tx, int ty);
static int rotate_block_access_flags(int flags, int angle);
static const RcTraversalEdge *object_traversal_edge(
    const RcWorld *world, int obj_id, int x, int y, int plane, int opt,
    uint64_t placement_key, int *has_candidate);
static int object_option_available(const RcWorld *world, int obj_id,
                                   int x, int y, int plane,
                                   uint64_t placement_key,
                                   const RcObjectDef *def,
                                   const RcObjectBehavior *behavior,
                                   const RcTraversalEdge *edge, int opt);
static void start_player_action_lock(RcWorld *world, int lock_ticks);

static void process_player_route(RcWorld *world) {
    process_player_interaction(world, 0);
}

static void process_player_action_timers(RcWorld *world) {
    rc_traversal_tick(world);
}

static int player_agility_level(const RcPlayer *player) {
    int level = player ? player->skills.base_level[SKILL_AGILITY] : 1;
    if (level < 1) level = 1;
    if (level > 99) level = 99;
    return level;
}

static int player_run_energy_loss(const RcWorld *world) {
    const RcPlayer *player = &world->player;
    int kilograms = player && player->weight > 0
        ? player->weight / 1000 : 0;
    if (kilograms > 64) kilograms = 64;
    int loss = (60 + (67 * kilograms) / 64)
             * (300 - player_agility_level(player)) / 300;
    if (world->tick < player->stamina_until) loss = loss * 30 / 100;
    return loss > 0 ? loss : 1;
}

static void update_player_run_energy(RcWorld *world, int ran_two_steps) {
    RcPlayer *player = &world->player;
    if (ran_two_steps) {
        player->run_energy -= player_run_energy_loss(world);
        if (player->run_energy <= 0) {
            player->run_energy = 0;
            player->running = false;
        }
        return;
    }
    int recovery = 15 + player_agility_level(player) / 10;
    player->run_energy += recovery;
    if (player->run_energy > 10000) player->run_energy = 10000;
}

static int continue_player_route(RcWorld *world) {
    RcPlayer *player = world ? &world->player : NULL;
    if (!player || !player->route_continue) return 0;
    RcRoute route = rc_find_route(
        &world->map, player->x, player->y,
        player->route_entity_width, player->route_entity_height,
        player->plane, &player->route_target,
        player->route_allow_alternative);
    if (!rc_player_route_admit(
            player, &route, &player->route_target,
            player->route_entity_width, player->route_entity_height,
            player->route_allow_alternative)) {
        rc_player_route_clear(player, RC_MOVEMENT_NO_ROUTE);
        return 0;
    }
    return player->route_idx < player->route_len;
}

static void process_player_movement(RcWorld *world) {
    if (!world) return;
    RcPlayer *p = &world->player;
    p->movement_step_count = 0;
    if (p->route_idx >= p->route_len && !continue_player_route(world)) {
        update_player_run_energy(world, 0);
        return;
    }
    if (world->player_action.active
            && world->player_action.category == RC_ACTION_CATEGORY_STRONG
            && world->player_action.ready_tick > world->tick) {
        update_player_run_energy(world, 0);
        return;
    }
    if (rc_player_is_frozen(world)) {
        update_player_run_energy(world, 0);
        return;
    }
    p->prev_x = p->x;
    p->prev_y = p->y;
    int steps = p->running && p->run_energy > 0 ? 2 : 1;
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
        int step_world_x = p->x + dx;
        int step_world_y = p->y + dy;
        if (!world->active_area.active
                || step_world_x / RC_MAPSQUARE_SIZE
                    != p->x / RC_MAPSQUARE_SIZE
                || step_world_y / RC_MAPSQUARE_SIZE
                    != p->y / RC_MAPSQUARE_SIZE) {
            if (rc_world_activate_area_around(
                    world, step_world_x, step_world_y,
                    p->plane, NULL) < 0) {
                rc_player_route_clear(p, RC_MOVEMENT_BLOCKED);
                break;
            }
        }
        int entity_width = p->route_entity_width > 0
            ? p->route_entity_width : 1;
        int entity_height = p->route_entity_height > 0
            ? p->route_entity_height : 1;
        if (!rc_can_move_rect(&world->map, p->x, p->y,
                              entity_width, entity_height,
                              dx, dy, p->plane)) {
            rc_player_route_clear(p, RC_MOVEMENT_BLOCKED);
            break;
        }
        p->x += dx;
        p->y += dy;
        p->movement_step_x[p->movement_step_count] = p->x;
        p->movement_step_y[p->movement_step_count] = p->y;
        p->movement_step_count++;
        p->movement_result = RC_MOVEMENT_MOVED;
        if (p->x == nx && p->y == ny) p->route_idx++;
    }
    update_player_run_energy(world, p->movement_step_count == 2);
    if (p->route_idx >= p->route_len && !p->route_continue) {
        p->movement_result = p->route_status == RC_ROUTE_ALTERNATIVE
            ? RC_MOVEMENT_NO_ROUTE : RC_MOVEMENT_ARRIVED;
    }
}

static int player_distance_to_npc(const RcWorld *world, const RcPlayer *p,
                                  const RcNpc *npc) {
    const RcNpcDef *def = npc_def_for(world, npc);
    int size = def && def->size > 0 ? def->size : 1;
    RcTileBounds player_bounds, npc_bounds;
    int distance;
    if (!p || !npc
            || !rc_tile_bounds_from_origin_size(p->x, p->y, 1, 1,
                                                p->plane, &player_bounds)
            || !rc_tile_bounds_from_origin_size(npc->x, npc->y, size, size,
                                                npc->plane, &npc_bounds)
            || (distance = rc_tile_bounds_distance(
                    &player_bounds, &npc_bounds)) < 0) {
        return INT_MAX;
    }
    return distance;
}

static int npc_interaction_has_los(const RcWorld *world, const RcPlayer *p,
                                   const RcNpc *npc, int range) {
    if (!p || !npc || p->plane != npc->plane) return 0;
    if (range <= 1) return 1;
    const RcNpcDef *def = npc_def_for(world, npc);
    int size = def && def->size > 0 ? def->size : 1;
    return rc_has_los_rect(&world->map, p->x, p->y, 1, 1,
                           npc->x, npc->y, size, size, p->plane);
}

static int route_player_toward_interaction_npc(RcWorld *world, RcPlayer *p,
                                               const RcNpc *npc, int range) {
    if (!world || !p || !npc || p->plane != npc->plane) return 0;
    const RcNpcDef *def = npc_def_for(world, npc);
    int size = def && def->size > 0 ? def->size : 1;
    RcRouteTarget target = rc_route_target_rectangle(
        npc->x, npc->y, size, size, 1, range, false, range > 1);
    if (!api_route_player(world, &target, 1, 1, false)) return 0;
    p->interaction.flags |= RC_INTERACTION_MOVED;
    return 1;
}

static int player_distance_to_target(const RcPlayer *p,
                                     const RcInteractionTarget *target) {
    RcTileBounds player_bounds, target_bounds;
    int distance;
    if (!p || !target
            || !rc_tile_bounds_from_origin_size(p->x, p->y, 1, 1,
                                                p->plane, &player_bounds)
            || !rc_tile_bounds_from_origin_size(
                target->tile_x, target->tile_y,
                target->footprint_width, target->footprint_height,
                target->plane, &target_bounds)
            || (distance = rc_tile_bounds_distance(
                    &player_bounds, &target_bounds)) < 0) {
        return INT_MAX;
    }
    return distance;
}

static int tile_distance_to_target_rect(int x, int y, int min_x, int min_y,
                                        int max_x, int max_y) {
    RcTileRect target;
    int closest_x, closest_y;
    if (!rc_tile_rect_make(min_x, min_y, max_x, max_y, &target)
            || !rc_world_coord_valid(x) || !rc_world_coord_valid(y)
            || !rc_tile_rect_closest_point(
                &target, x, y, &closest_x, &closest_y)) {
        return INT_MAX;
    }
    return rc_tile_distance(x, y, 0, closest_x, closest_y, 0);
}

static int player_has_active_route(const RcPlayer *p) {
    return p && p->route_idx < p->route_len;
}

static const RcTraversalState *planned_object_traversal(
    const RcPlayer *player, const RcInteractionTarget *target) {
    if (!player || !target || !player->traversal.active
            || player->traversal.phase != RC_TRAVERSAL_PHASE_APPROACH
            || player->traversal.interaction_generation
                != player->interaction.generation
            || target->kind != RC_INTERACTION_OBJECT
            || player->traversal.source_kind != RC_TRAVERSAL_OBJECT
            || player->traversal.source_id != target->definition_id
            || player->traversal.source_x != target->tile_x
            || player->traversal.source_y != target->tile_y
            || player->traversal.source_plane != target->plane
            || (target->placement_key != 0
                && player->traversal.source_key != target->placement_key)) {
        return NULL;
    }
    return &player->traversal;
}

static int route_player_toward_interaction_target(RcWorld *world, RcPlayer *p,
                                                  const RcInteractionTarget *t,
                                                  int range) {
    if (!world || !p || !t || p->plane != t->plane) return 0;
    if (range < 0) range = 0;
    if (t->kind == RC_INTERACTION_OBJECT) {
        const RcTraversalState *traversal =
            planned_object_traversal(p, t);
        if (traversal && traversal->approach_plane == p->plane) {
            if (p->x == traversal->approach_x
                    && p->y == traversal->approach_y) {
                return 1;
            }
            RcRouteTarget target = rc_route_target_point(
                traversal->approach_x, traversal->approach_y);
            if (!api_route_player(world, &target, 1, 1, false)) return 0;
            p->interaction.flags |= RC_INTERACTION_MOVED;
            return 1;
        }
    }
    RcRouteTarget target = rc_route_target_rectangle(
        t->tile_x, t->tile_y, t->footprint_width, t->footprint_height,
        range > 0 ? 1 : 0, range, range == 0, range > 1);
    if (t->kind == RC_INTERACTION_GROUND_ITEM && range == 1) {
        target.min_distance = 0;
        target.allow_inside = true;
    }
    if (t->kind == RC_INTERACTION_OBJECT) {
        RcObjectPlacement placement;
        if (rc_world_object_current_placement(
                world, t->definition_id, t->tile_x, t->tile_y,
                t->plane, t->placement_key, &placement)) {
            if (placement.type <= 3) {
                target.kind = RC_ROUTE_REACH_WALL;
                target.shape = placement.type;
                target.rotation = placement.rotation;
                target.width = 1;
                target.height = 1;
                target.min_distance = 0;
                target.max_distance = 1;
            } else {
                const RcObjectDef *def = rc_object_def_get(
                    (int)placement.obj_id);
                placement_dimensions(def, &placement,
                                     &target.width, &target.height);
                target.x = placement.x;
                target.y = placement.y;
                target.block_access = rotate_block_access_flags(
                    def ? def->force_approach : 0, placement.rotation);
            }
        }
    }
    if (!api_route_player(world, &target, 1, 1, false)) return 0;
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
    const RcNpcDef *def = npc_def_for(world, npc);
    if (!def) {
        pending->last_failure = RC_INTERACTION_FAIL_INVALID_TARGET;
        return NULL;
    }
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
    int obj_id = pending->target.definition_id;
    RcObjectPlacement placement = {0};
    int placed = pending->target.tile_x >= 0 && pending->target.tile_y >= 0
              && pending->target.plane >= 0;
    if (placed && rc_object_has_placements()) {
        if (!rc_world_object_current_placement(
                world, obj_id, pending->target.tile_x,
                pending->target.tile_y, pending->target.plane,
                pending->target.placement_key, &placement)) {
            pending->last_failure = RC_INTERACTION_FAIL_TARGET_MISSING;
            return 0;
        }
        obj_id = (int)placement.obj_id;
    }
    const RcObjectDef *def = rc_object_def_get(obj_id);
    if (!def) {
        pending->last_failure = RC_INTERACTION_FAIL_INVALID_TARGET;
        return 0;
    }
    RcObjectState *state = NULL;
    if (pending->target.tile_x >= 0 && pending->target.tile_y >= 0 &&
            pending->target.plane >= 0) {
        state = pending->target.placement_key
            ? rc_world_object_state_find_key(world, pending->target.placement_key)
            : rc_world_object_state_find(world, pending->target.definition_id,
                                pending->target.tile_x,
                                pending->target.tile_y,
                                pending->target.plane);
        if (state && !rc_world_object_state_matches(
                state, pending->target.definition_id, pending->target.tile_x,
                pending->target.tile_y, pending->target.plane)) {
            pending->last_failure = RC_INTERACTION_FAIL_TARGET_MISSING;
            return 0;
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
    } else if (!rc_world_object_option_supported(
                    world, obj_id, pending->target.tile_x,
                    pending->target.tile_y, pending->target.plane,
                    pending->target.placement_key, opt)) {
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
    if (placed && (placement.key || rc_world_object_current_placement(
            world, obj_id, pending->target.tile_x, pending->target.tile_y,
            pending->target.plane, pending->target.placement_key,
            &placement))) {
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
    if (!rc_ground_item_visible(item, RC_GROUND_OWNER_LOCAL_PLAYER)) {
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

static int validate_pending_item_references(
    const RcPlayer *player, RcPendingInteraction *pending) {
    if (!player || !pending) return 0;
    if (pending->source_item_id >= 0
            && pending->source_inventory_slot >= 0) {
        int slot = pending->source_inventory_slot;
        if (slot < 0 || slot >= RC_INVENTORY_SIZE
                || player->inventory[slot].item_id
                    != pending->source_item_id
                || player->inventory[slot].generation
                    != pending->source_item_generation) {
            pending->last_failure = RC_INTERACTION_FAIL_INVALID_SOURCE;
            return 0;
        }
    }
    if (pending->target.kind == RC_INTERACTION_INVENTORY_ITEM) {
        int slot = pending->target.inventory_slot;
        if (slot < 0 || slot >= RC_INVENTORY_SIZE
                || player->inventory[slot].item_id
                    != pending->target.definition_id
                || player->inventory[slot].generation
                    != pending->target.item_generation) {
            pending->last_failure = RC_INTERACTION_FAIL_TARGET_VERSION_CHANGED;
            return 0;
        }
    } else if (pending->target.kind == RC_INTERACTION_EQUIPMENT_ITEM) {
        int slot = pending->target.equipment_slot;
        if (slot < 0 || slot >= RC_EQUIP_COUNT
                || player->equipment[slot].item_id
                    != pending->target.definition_id
                || player->equipment[slot].generation
                    != pending->target.item_generation) {
            pending->last_failure = RC_INTERACTION_FAIL_TARGET_VERSION_CHANGED;
            return 0;
        }
    }
    if (pending->source_spell_id >= 0) {
        const RcSpellDef *spell = rc_spell_def_get(pending->source_spell_id);
        if (!spell || !spell->loaded
                || spell->book != player->current_spellbook) {
            pending->last_failure = RC_INTERACTION_FAIL_INVALID_SOURCE;
            return 0;
        }
    }
    return 1;
}

static void process_player_interaction(RcWorld *world, int dispatch_ready) {
    if (!world || !world->player.interaction.active) return;
    RcPlayer *p = &world->player;
    if (p->is_dead || p->current_hp <= 0) {
        rc_interaction_cancel(p, RC_INTERACTION_FAIL_ACTOR_DEAD);
        return;
    }
    if (world->player_action.active
            && world->player_action.owner != RC_ACTION_OWNER_INTERACTION
            && world->player_action.category == RC_ACTION_CATEGORY_STRONG
            && world->player_action.ready_tick > world->tick) {
        rc_interaction_cancel(p, RC_INTERACTION_FAIL_ACTOR_BUSY);
        return;
    }
    if (!validate_pending_item_references(p, &p->interaction)) {
        rc_interaction_cancel(p, p->interaction.last_failure);
        return;
    }
    if (p->interaction.target.kind == RC_INTERACTION_NPC) {
        RcNpc *npc = validate_pending_npc_interaction(world, &p->interaction);
        if (!npc) {
            rc_interaction_cancel(p, p->interaction.last_failure);
            return;
        }
        if (npc->plane != p->plane) {
            rc_interaction_cancel(p, RC_INTERACTION_FAIL_CANNOT_REACH);
            return;
        } else {
            int range = p->interaction.approach_range > 0
                      ? p->interaction.approach_range : 1;
            if (player_distance_to_npc(world, p, npc) > range) {
                if (!route_player_toward_interaction_npc(
                        world, p, npc, range)) {
                    rc_interaction_cancel(p, RC_INTERACTION_FAIL_CANNOT_REACH);
                }
                return;
            }
            if (!npc_interaction_has_los(world, p, npc, range)) {
                rc_interaction_cancel(p, RC_INTERACTION_FAIL_LOS_BLOCKED);
                return;
            }
        }
    } else if (p->interaction.target.kind == RC_INTERACTION_OBJECT) {
        if (!validate_pending_object_interaction(world, &p->interaction)) {
            rc_interaction_cancel(p, p->interaction.last_failure);
            return;
        }
        if (p->interaction.target.plane != p->plane) {
            rc_interaction_cancel(p, RC_INTERACTION_FAIL_CANNOT_REACH);
            return;
        } else {
            int range = p->interaction.approach_range > 0
                      ? p->interaction.approach_range : 1;
            int in_range =
                player_distance_to_target(p, &p->interaction.target) <= range;
            const RcTraversalState *traversal = planned_object_traversal(
                p, &p->interaction.target);
            if (traversal && traversal->approach_plane == p->plane) {
                in_range = p->x == traversal->approach_x
                        && p->y == traversal->approach_y;
            }
            if (in_range) {
                in_range = tile_reaches_object_target(
                    world, &p->interaction.target, p->x, p->y);
                if (!in_range && traversal
                        && traversal->approach_plane == p->plane
                        && p->x == traversal->approach_x
                        && p->y == traversal->approach_y) {
                    in_range = 1;
                }
            }
            if (!in_range) {
                if (!route_player_toward_interaction_target(
                        world, p, &p->interaction.target, range)) {
                    rc_interaction_cancel(p, RC_INTERACTION_FAIL_CANNOT_REACH);
                }
                return;
            }
        }
    } else if (p->interaction.target.kind == RC_INTERACTION_GROUND_ITEM) {
        if (!validate_pending_ground_item_interaction(world,
                                                      &p->interaction)) {
            rc_interaction_cancel(p, p->interaction.last_failure);
            return;
        }
        if (p->interaction.target.plane != p->plane) {
            rc_interaction_cancel(p, RC_INTERACTION_FAIL_CANNOT_REACH);
            return;
        } else {
            int range = p->interaction.approach_range;
            int reached = range == 1
                ? rc_ground_item_reachable(world, p->interaction.target.tile_x,
                    p->interaction.target.tile_y, p->interaction.target.plane)
                : player_distance_to_target(p, &p->interaction.target) <= range;
            if (!reached) {
                if (!route_player_toward_interaction_target(
                        world, p, &p->interaction.target, range)) {
                    rc_interaction_cancel(p, RC_INTERACTION_FAIL_CANNOT_REACH);
                }
                return;
            }
            if (dispatch_ready && p->interaction.op == RC_INTERACTION_OP1
                    && (p->x != p->interaction.target.tile_x
                        || p->y != p->interaction.target.tile_y)) {
                if (player_has_active_route(p))
                    rc_player_route_clear(p, RC_MOVEMENT_ARRIVED);
                if (!p->interaction.ready_tick)
                    p->interaction.ready_tick = world->tick + 1;
                if (world->tick < p->interaction.ready_tick) return;
            }
        }
    } else if (p->interaction.target.kind == RC_INTERACTION_INVENTORY_ITEM ||
            p->interaction.target.kind == RC_INTERACTION_EQUIPMENT_ITEM ||
            p->interaction.target.kind == RC_INTERACTION_WIDGET) {
        if (!rc_interaction_target_valid(&p->interaction.target)) {
            rc_interaction_cancel(p, RC_INTERACTION_FAIL_INVALID_TARGET);
            return;
        }
    } else {
        rc_interaction_cancel(p, RC_INTERACTION_FAIL_INVALID_TARGET);
        return;
    }
    p->interaction.flags |= RC_INTERACTION_ARRIVED;
    if (player_has_active_route(p))
        rc_player_route_clear(p, RC_MOVEMENT_ARRIVED);
    if (!dispatch_ready) return;
    uint64_t generation = p->interaction.generation;
    RcInteractionHandlerResult result = rc_interaction_dispatch(world, p);
    (void)rc_interaction_apply_result(p, generation, result);
}

static int api_prepare_spatial_interaction(RcWorld *world) {
    if (!world || !world->player.interaction.active)
        return 0;
    RcPlayer *p = &world->player;
    process_player_interaction(world, 0);
    return p->interaction.active;
}

static void process_player_combat(RcWorld *world) {
    rc_combat_tick_player(world);
}

// The face entity lasts only while an interaction or attack does (rsmod
// PlayerMainProcess.refreshFaceEntity); the client keeps its last angle.
static void refresh_player_face_entity(RcPlayer *p) {
    if (p->facing_entity >= 0 && !p->interaction.active
            && p->attack_target < 0)
        p->facing_entity = -1;
}

// A pending face square goes out once, as an angle from the player toward
// its centre, when the player ends a tick without moving (rsmod
// PlayerFaceSquareProcessor). Walking away does not cancel it.
static void send_player_face_square(RcPlayer *p) {
    if (p->facing_x < 0 || p->facing_y < 0 || p->movement_step_count)
        return;
    int angle = rc_face_angle(p->x, p->y, 1, 1, p->facing_x, p->facing_y,
                              p->facing_width ? p->facing_width : 1,
                              p->facing_length ? p->facing_length : 1);
    if (angle >= 0) {
        p->face_angle = (int16_t)angle;
        p->face_angle_seq++;
    }
    p->facing_x = p->facing_y = -1;
}

static void process_player_skilling(RcWorld *world) {
    if (!world || world->player.skill_action <= 0
            || world->player.skill_ready_tick > world->tick) {
        return;
    }
    if (world->player.skill_target_x >= 0 && world->player.skill_target_y >= 0
            && world->player.plane >= 0) {
        const RcGatheringNode *node = rc_gathering_node_find(
            world->player.skill_action, world->player.skill_target_x,
            world->player.skill_target_y, world->player.plane);
        if (node) {
            RcObjectPlacement base;
            RcObjectMutationResult result = RC_OBJECT_MUTATION_NOT_FOUND;
            if (rc_object_placement_find_key(
                    node->placement_key, node->x, node->y, node->plane,
                    &base)) {
                if (node->replacement_obj_id >= 0) {
                    RcObjectPlacement replacement = base;
                    replacement.obj_id = (uint32_t)node->replacement_obj_id;
                    result = rc_world_object_replace(
                        world, &base, &replacement, node->respawn_ticks,
                        RC_OBJECT_STATE_DEPLETED);
                } else {
                    result = rc_world_object_delete(
                        world, &base, node->respawn_ticks,
                        RC_OBJECT_STATE_DEPLETED);
                }
            }
            if (result != RC_OBJECT_MUTATION_OK) {
                world->player.interaction.last_failure =
                    RC_INTERACTION_FAIL_INVALID_TARGET;
            }
        }
    }
    world->player.skill_action = 0;
}

// (Player-side hit resolution lives in combat.c as
// rc_resolve_player_hits — it fires RC_EVT_PLAYER_DAMAGED per hit.)

static void resolve_npc_hits(RcWorld *world, RcNpc *npc) {
    const RcNpcDef *def = npc_def_for(world, npc);
    bool was_alive = !npc->is_dead;
    int w = 0;
    for (int i = 0; i < npc->num_pending_hits; i++) {
        RcPendingHit *h = &npc->pending_hits[i];
        if (!h->active) continue;
        if (world->tick < h->apply_tick) {
            if (w != i) npc->pending_hits[w] = *h;
            w++;
            continue;
        }
        if (npc->is_dead || npc->current_hp <= 0) {
            h->active = 0;
            continue;
        }
        int damage = rc_combat_resolve_hit_damage(
                         h, false /* npc defender */);
        if (damage > npc->current_hp) damage = npc->current_hp;
        if (damage < 0) damage = 0;
        uint8_t hit_type = damage <= 0 ? RC_HIT_TYPE_MISS :
                           (h->max_hit > 0 && damage >= h->max_hit
                            ? RC_HIT_TYPE_MAX : RC_HIT_TYPE_NORMAL);
        rc_combat_actor_record_hit(&npc->combat, damage, h->max_hit,
                                   h->attack_style, h->source_idx,
                                   hit_type, h->flags, 4);
        h->active = 0;
        npc->current_hp -= damage;
        if (h->source_idx == RC_HIT_SOURCE_PLAYER && damage > 0)
            npc->player_loot_credit = true;
        if (h->accurate && h->defence_drain > 0) {
            npc->stats[1] -= h->defence_drain;
            if (npc->stats[1] < 0) npc->stats[1] = 0;
        }
        if (h->source_idx == RC_HIT_SOURCE_PLAYER
                && world->combat_hooks.on_player_hit_npc)
            world->combat_hooks.on_player_hit_npc(world, npc, h, damage);
        npc->last_hit = damage;
        npc->last_hit_timer = 4;
        if (h->source_idx == RC_HIT_SOURCE_PLAYER) {
            RcCombatActorRef player_actor = {
                .kind = RC_COMBAT_ACTOR_PLAYER,
                .uid = 0,
            };
            rc_combat_actor_register_attacker(&npc->combat, player_actor);
            if (!npc->is_dead && npc->current_hp > 0) {
                rc_combat_start_npc_vs_player(world, npc->uid, 0);
            }
        }
        if (npc->current_hp <= 0
                && rc_encounter_intercept_lethal(world, (RcNpcId)npc->uid)) {
            // A nonterminal encounter form: no death, loot or NPC_DIED.
        } else if (npc->current_hp <= 0) {
            npc->current_hp = 0;
            npc->is_dead = true;
            if (world->player.storage_session
                    && world->player.storage_source.kind == RC_INTERACTION_NPC
                    && world->player.storage_source.entity_uid == npc->uid)
                rc_storage_close(world);
            npc->death_timer = 3;
            npc->respawn_timer = npc->respawns && def
                               ? def->respawn_ticks : 0;
            npc->target_uid = -1;
            rc_npc_route_clear(npc, RC_MOVEMENT_NONE);
            if (world->player.attack_target == npc->uid) {
                world->player.attack_target = -1;
                world->player.attack_target_def_id = -1;
            }
        }
        RcPayloadNpcDamaged damaged = {
            .npc_id = (uint32_t)npc->uid,
            .source_npc_id = h->source_idx >= 0
                           ? (uint32_t)h->source_idx : UINT32_MAX,
            .damage = (uint16_t)(damage & 0xFFFF),
            .current_hp = (uint16_t)(npc->current_hp & 0xFFFF),
            .max_hp = def ? (uint16_t)(def->hitpoints & 0xFFFF) : 0,
            .style = (uint8_t)h->attack_style,
        };
        rc_event_fire(world, RC_EVT_NPC_DAMAGED, &damaged);
    }
    npc->num_pending_hits = w;
    npc->combat.hp_current = npc->current_hp;
    npc->combat.hp_max = def ? def->hitpoints : 0;
    // Fire death event once on the transition alive → dead.
    if (was_alive && npc->is_dead) {
        rc_npc_prepare_loot(world, npc);
        RcPayloadNpcEvent payload = {
            .npc_id = (uint32_t)npc->uid,
            .def_id = def ? (uint32_t)def->id : 0,
            .spawn_key = npc->spawn_key,
        };
        rc_event_fire(world, RC_EVT_NPC_DIED, &payload);
    }
}

static void check_deaths(RcWorld *world) {
    if (!world) return;
    RcPlayer *player = &world->player;
    if (player->current_hp > 0 || player->is_dead) return;
    player->current_hp = 0;
    player->is_dead = true;
    player->death_tick = world->tick;
    rc_player_cancel_action(world, RC_ACTION_CANCEL_DEATH);
    rc_prayer_disable_all(world, RC_PRAYER_DEAD);
    RcPayloadPlayerDeath payload = {
        .tick = world->tick,
        .x = player->x,
        .y = player->y,
        .plane = player->plane,
    };
    rc_event_fire(world, RC_EVT_PLAYER_DIED, &payload);
}

static void tick_respawns(RcWorld *world) {
    rc_world_objects_tick(world);
}

// Tick dispatcher. Per rc-core/README.md §3, per-subsystem ticks are
// gated by a cache-resident bitmask-AND; the base (player position,
// NPC position, pathfinding, tick counter) always runs.
void rc_world_tick(RcWorld *world) {
    if (!world || world->in_tick) return;
    world->in_tick = true;
    const uint32_t on = world->enabled;
    rc_combat_clear_attack_events(world);

    // Phase 1 — input (base): always runs.
    if (on & RC_SUB_CONSUMABLES) rc_consumables_tick(world);
    process_player_input(world);

    // Phase 2 — route planning (base): always runs.
    process_player_route(world);

    // Phase 3 — NPC lifecycle and autonomous policy (base).
    for (int i = 0; i < world->npc_count; i++) {
        if (world->npcs[i].active) {
            rc_npc_tick(world, &world->npcs[i]);
        }
    }

    // Phase 3.5 — NPC combat planning (COMBAT subsystem).
    if (on & RC_SUB_COMBAT) {
        for (int i = 0; i < world->npc_count; i++) {
            if (world->npcs[i].active) {
                rc_combat_tick_npc(world, &world->npcs[i]);
            }
        }
    }

    // Phase 3.55 — one validated movement step per live NPC. Combat and
    // autonomous policy both submit routes to this single owner.
    for (int i = 0; i < world->npc_count; i++) {
        if (world->npcs[i].active)
            rc_npc_movement_tick(world, &world->npcs[i]);
    }

    // Phase 3.6 — encounter mechanic dispatcher.
    if (on & RC_SUB_ENCOUNTER) rc_encounter_tick(world);
    if (on & RC_SUB_ENCOUNTER) rc_encounter_tick_effects(world);

    // Phase 4 — player action resolution (gated per subsystem).
    refresh_player_face_entity(&world->player);
    process_player_action_timers(world);
    if (!(on & RC_SUB_COMBAT) || rc_combat_player_approach(world)) {
        process_player_movement(world);
    } else {
        // In reach of the attack target: the player holds this tick.
        world->player.movement_step_count = 0;
        update_player_run_energy(world, 0);
    }
    process_player_interaction(world, 1);
    process_player_action_timers(world);
    if (on & RC_SUB_COMBAT)   process_player_combat(world);
    if (on & RC_SUB_SKILLS)   process_player_skilling(world);
    send_player_face_square(&world->player);

    // Phase 5 — pending-hit resolution (combat subsystem).
    if (on & RC_SUB_COMBAT) {
        rc_resolve_player_hits(world);
        rc_combat_tick_player_status(world);
        for (int i = 0; i < world->npc_count; i++) {
            if (world->npcs[i].active) {
                rc_npc_status_tick(world, &world->npcs[i]);
                resolve_npc_hits(world, &world->npcs[i]);
            }
        }
    }

    // Phase 6 — prayer drain (prayer subsystem).
    if (on & RC_SUB_PRAYER)   rc_prayer_drain_tick(world);

    // Phase 7 — stat regen (skills subsystem, but hp regen baseline
    // runs in base since it's part of the base player model).
    rc_stat_restore_tick(&world->player);

    // Phase 8 — deaths / respawns / ground items.
    if (on & RC_SUB_COMBAT)   check_deaths(world);
    tick_respawns(world);     // base — NPC wander reset clock
    if (on & RC_SUB_LOOT)     rc_ground_items_tick(world);

    world->tick++;
    if (world->player_action.active) rc_player_action_refresh(world);
    world->in_tick = false;
}

// Player-input target construction and dispatch helpers.
static RcNpc *api_find_npc_by_uid(RcWorld *world, int uid) {
    return rc_npc_resolve(world, uid);
}

static RcInteractionTarget api_npc_interaction_target(
    const RcWorld *world, const RcNpc *npc) {
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
    const RcNpcDef *def = npc_def_for(world, npc);
    if (def) {
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
    if (rc_world_object_current_placement(world, obj_id, x, y, plane,
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
    target.item_generation = slot >= 0 && slot < RC_INVENTORY_SIZE
                           ? player->inventory[slot].generation : 0;
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
    target.item_generation = slot >= 0 && slot < RC_EQUIP_COUNT
                           ? player->equipment[slot].generation : 0;
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
    RcCombatActorRef actor = {
        .kind = RC_COMBAT_ACTOR_PLAYER,
        .uid = 0,
    };
    rc_combat_stop_actor(world, actor, RC_COMBAT_STATE_CANCELLED);
    world->player.manual_spell_cast = -1;
}

static void api_commit_interaction_admission(RcWorld *world) {
    if (!world) return;
    RcPlayer *player = &world->player;
    int replacing_storage = player->storage_kind != RC_STORAGE_NONE;
    rc_player_route_clear(player, RC_MOVEMENT_NONE);
    rc_traversal_cancel(world, RC_ACTION_CANCEL_REPLACED);
    player->skill_action = 0;
    player->skill_ready_tick = 0;
    rc_storage_close(world);
    if (replacing_storage
            && (player->interact_type == RC_INTERACT_OBJECT
                || player->interact_type == RC_INTERACT_NPC)) {
        player->interact_type = RC_INTERACT_NONE;
        player->interact_target = -1;
        player->interact_option = -1;
    }
    api_stop_player_combat(world);
}

static int spellbook_valid(int spellbook) {
    return spellbook >= RC_SPELL_BOOK_STANDARD &&
           spellbook <= RC_SPELL_BOOK_ARCEUUS;
}

static int spell_usable_on_current_book(const RcPlayer *player,
                                        const RcSpellDef *spell) {
    return player && spell && spell->loaded &&
           (spell->book == player->current_spellbook || spell->book == RC_SPELL_BOOK_ALL);
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
    const RcNpcDef *def = npc_def_for(world, npc);
    if (!npc || npc->is_dead || !def) {
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_TARGET_MISSING, "NPC target missing");
    }
    if (pending->op == RC_INTERACTION_SPELL_ON) {
        RcSpellResult available = rc_spell_available(world, pending->source_spell_id, 0);
        if (available != RC_SPELL_OK || !rc_spell_is_combat(rc_spell_def_get(pending->source_spell_id)))
            return rc_interaction_result_failure(RC_INTERACTION_FAIL_INVALID_SOURCE,
                rc_spell_result_message(available == RC_SPELL_OK ? RC_SPELL_UNSUPPORTED : available));
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
        return rc_interaction_result_complete();
    }

    int opt = api_option_from_interaction_op(pending->op);
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
    return rc_interaction_result_complete();
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
    const RcNpcDef *def = npc_def_for(world, npc);
    if (!npc || npc->is_dead || !def) {
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_TARGET_MISSING, "NPC target missing");
    }
    int opt = api_option_from_interaction_op(pending->op);
    if (opt < 0) {
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_OPTION_UNAVAILABLE, "Invalid NPC option");
    }
    if (rc_npc_def_option_is_attack(def, opt)) {
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_NO_HANDLER, "NPC attack handler unavailable");
    }
    api_stop_player_combat(world);
    if (rc_storage_open_interaction(world)) {
        return rc_interaction_result_complete();
    }
    const char *npc_option = rc_npc_def_option(def, opt);
    if (npc_option && (!strcmp(npc_option, "Bank") || !strcmp(npc_option, "Collect")))
        return rc_interaction_result_failure(RC_INTERACTION_FAIL_NO_HANDLER,
                                             "This storage service is not supported.");
    player->interact_type = RC_INTERACT_NPC;
    player->interact_target = npc->uid;
    player->interact_option = opt;
    return rc_interaction_result_complete();
}

static int register_default_handler(
    RcWorld *world, const RcInteractionDispatchKey *key,
    RcInteractionHandlerFn handler) {
    if (!world || !key || !handler) return 0;
    if (rc_interaction_find_world_handler(world, key) >= 0) return 1;
    return rc_interaction_register_world_handler(world, key, handler, NULL);
}

static int api_register_default_npc_handlers(RcWorld *world) {
    if (!world) return 0;
    for (int i = 0; i < 5; i++) {
        RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
        key.kind = RC_INTERACTION_NPC;
        key.op = rc_interaction_op_from_option(i);
        key.content_group = RC_INTERACTION_CONTENT_GROUP_NPC_ATTACK;
        if (!register_default_handler(world, &key,
                                      api_default_npc_attack_handler))
            return 0;
        key.content_group = RC_INTERACTION_KEY_ANY;
        if (!register_default_handler(world, &key,
                                      api_default_npc_option_handler))
            return 0;
    }
    RcInteractionDispatchKey spell_key = rc_interaction_dispatch_key_any();
    spell_key.kind = RC_INTERACTION_NPC;
    spell_key.op = RC_INTERACTION_SPELL_ON;
    spell_key.content_group = RC_INTERACTION_CONTENT_GROUP_NPC_ATTACK;
    return register_default_handler(world, &spell_key,
                                    api_default_npc_attack_handler);
}

static int action_allowed_if_loaded(RcWorld *world, int action_id) {
    return world && (g_rc_player_action_count == 0 ||
                     rc_player_action_allowed(world->enabled, action_id));
}

static int queue_player_command(RcWorld *world, RcPlayerCommandKind kind,
                                RcActionCategory category,
                                int a0, int a1, int a2, int a3,
                                int a4, uint64_t key) {
    int args[8] = {a0, a1, a2, a3, a4, 0, 0, 0};
    return rc_player_command_submit(world, kind, category, args, key);
}

static int interaction_spell_source_valid(const RcWorld *world,
                                          int spell_id) {
    if (!world) return 0;
    const RcSpellDef *spell = rc_spell_def_get(spell_id);
    return spell_usable_on_current_book(&world->player, spell);
}

static int route_targets_equal(const RcRouteTarget *a,
                               const RcRouteTarget *b) {
    return a && b && a->x == b->x && a->y == b->y
        && a->width == b->width && a->height == b->height
        && a->min_distance == b->min_distance
        && a->max_distance == b->max_distance
        && a->kind == b->kind && a->shape == b->shape
        && a->rotation == b->rotation
        && a->block_access == b->block_access
        && a->allow_inside == b->allow_inside
        && a->require_los == b->require_los;
}

static int api_route_player(RcWorld *world, const RcRouteTarget *target,
                            int entity_width, int entity_height,
                            int allow_alternative) {
    if (!world || !target) return 0;
    RcPlayer *player = &world->player;
    if (player_has_active_route(player)
            && player->route_entity_width == entity_width
            && player->route_entity_height == entity_height
            && player->route_allow_alternative == (allow_alternative != 0)
            && route_targets_equal(&player->route_target, target)) {
        return 1;
    }
    RcRoute route = rc_find_route(
        &world->map, player->x, player->y,
        entity_width, entity_height, player->plane,
        target, allow_alternative != 0);
    return rc_player_route_admit(
        player, &route, target, entity_width, entity_height,
        allow_alternative != 0);
}

static void start_player_action_lock(RcWorld *world, int lock_ticks) {
    if (!world) return;
    if (lock_ticks < 0) lock_ticks = 0;
    RcPlayerActionState *action = &world->player_action;
    RcTick ready_tick = world->tick + (RcTick)lock_ticks + 1;
    if (!action->active || action->category < RC_ACTION_CATEGORY_STRONG
            || ready_tick > action->ready_tick) {
        action->active = true;
        action->owner = RC_ACTION_OWNER_TRAVERSAL;
        action->category = RC_ACTION_CATEGORY_STRONG;
        action->started_tick = world->tick;
        action->ready_tick = ready_tick;
    }
}

// A walk click: it drops the current action and face target, then routes to
// the clicked tile or, when that is unreachable, the nearest tile that is
// (rsmod MoveGameClickHandler, moveNear). The new route always replaces the
// old one, so a click with no route stops the player where they stand.
static int api_move_click(RcWorld *world, int x, int y, int action_id,
                          bool run) {
    if (!world || !rc_world_coord_valid(x) || !rc_world_coord_valid(y)
            || !rc_world_tile_valid(world->player.x, world->player.y,
                                    world->player.plane)
            || !action_allowed_if_loaded(world, action_id)) {
        return 0;
    }
    RcPlayer *p = &world->player;
    rc_player_replace_action_with_movement(
        world, RC_ACTION_CANCEL_REPLACED);
    p->facing_entity = -1;
    RcRouteTarget target = rc_route_target_point(x, y);
    if (!api_route_player(world, &target, 1, 1, true))
        rc_player_route_clear(p, RC_MOVEMENT_NO_ROUTE);
    p->running = run && p->run_energy > 0;
    return 1;
}

int rc_player_walk_to(RcWorld *world, int x, int y) {
    if (rc_player_command_should_queue(world)) {
        return queue_player_command(world, RC_PLAYER_COMMAND_WALK_TO,
                                    RC_ACTION_CATEGORY_NORMAL,
                                    x, y, 0, 0, 0, 0);
    }
    return api_move_click(world, x, y, RC_PLAYER_ACTION_WALK_TO, false);
}

int rc_player_run_to(RcWorld *world, int x, int y) {
    if (rc_player_command_should_queue(world)) {
        return queue_player_command(world, RC_PLAYER_COMMAND_RUN_TO,
                                    RC_ACTION_CATEGORY_NORMAL,
                                    x, y, 0, 0, 0, 0);
    }
    return api_move_click(world, x, y, RC_PLAYER_ACTION_RUN_TO, true);
}

int rc_player_step(RcWorld *world, int dx, int dy) {
    if (rc_player_command_should_queue(world)) {
        return queue_player_command(world, RC_PLAYER_COMMAND_STEP,
                                    RC_ACTION_CATEGORY_NORMAL,
                                    dx, dy, 0, 0, 0, 0);
    }
    if (!world || dx < -1 || dx > 1 || dy < -1 || dy > 1
            || (!dx && !dy)
            || !rc_world_tile_valid(world->player.x + dx,
                                    world->player.y + dy,
                                    world->player.plane)
            || !action_allowed_if_loaded(world,
                                         RC_PLAYER_ACTION_WALK_TO)) {
        return 0;
    }
    RcPlayer *player = &world->player;
    RcRouteTarget target = rc_route_target_point(
        player->x + dx, player->y + dy);
    if (!api_route_player(world, &target, 1, 1, false)) return 0;
    rc_player_replace_action_with_movement(
        world, RC_ACTION_CANCEL_REPLACED);
    return 1;
}

int rc_player_set_running(RcWorld *world, int enabled) {
    if (rc_player_command_should_queue(world)) {
        return queue_player_command(world, RC_PLAYER_COMMAND_SET_RUNNING,
                                    RC_ACTION_CATEGORY_SOFT,
                                    enabled != 0, 0, 0, 0, 0, 0);
    }
    if (!world) return 0;
    world->player.running = enabled != 0 && world->player.run_energy > 0;
    return 1;
}

int rc_player_attack_npc(RcWorld *world, int npc_uid) {
    if (rc_player_command_should_queue(world)) {
        return queue_player_command(world, RC_PLAYER_COMMAND_ATTACK_NPC,
                                    RC_ACTION_CATEGORY_NORMAL,
                                    npc_uid, 0, 0, 0, 0, 0);
    }
    if (!action_allowed_if_loaded(world, RC_PLAYER_ACTION_ATTACK_NPC)) return 0;
    RcNpc *npc = api_find_npc_by_uid(world, npc_uid);
    if (!npc || npc->is_dead || npc->player_untargetable) return 0;
    if (npc->plane != world->player.plane) return 0;
    const RcNpcDef *def = npc_def_for(world, npc);
    if (!def) return 0;
    int opt = api_npc_attack_option_index(def);
    if (opt < 0) return 0;
    RcInteractionTarget target = api_npc_interaction_target(world, npc);
    target.content_group = RC_INTERACTION_CONTENT_GROUP_NPC_ATTACK;
    if (!rc_interaction_begin(&world->player, 0,
                              rc_interaction_op_from_option(opt),
                              rc_npc_def_option(def, opt), &target,
                              rc_player_attack_range(&world->player))) {
        return 0;
    }
    api_commit_interaction_admission(world);
    (void)api_prepare_spatial_interaction(world);
    return 1;
}
static RcSpellResult spell_result(RcWorld *world, RcSpellResult result) {
    if (world && result != RC_SPELL_QUEUED)
        world->player.combat.failure_reason = result == RC_SPELL_OK ? NULL : rc_spell_result_message(result);
    return result;
}

RcSpellResult rc_player_set_spellbook(RcWorld *world, int spellbook) {
    if (!world || !spellbook_valid(spellbook)) return spell_result(world, RC_SPELL_INVALID);
    if (world->player.is_dead || world->player.current_hp <= 0) return spell_result(world, RC_SPELL_DEAD);
    if (rc_player_command_should_queue(world))
        return spell_result(world, queue_player_command(world, RC_PLAYER_COMMAND_SET_SPELLBOOK,
            RC_ACTION_CATEGORY_SOFT, spellbook, 0, 0, 0, 0, 0) ? RC_SPELL_QUEUED : RC_SPELL_QUEUE_FULL);
    RcPlayer *p = &world->player;
    if (p->current_spellbook == spellbook) return spell_result(world, RC_SPELL_OK);
    p->current_spellbook = spellbook;
    p->selected_spell = p->manual_spell_cast = p->autocast_spell = -1;
    p->defensive_autocast = false;
    rc_refresh_player_combat_style(p);
    return spell_result(world, RC_SPELL_OK);
}

RcSpellResult rc_player_select_spell(RcWorld *world, int spell_idx) {
    if (world && (world->player.is_dead || world->player.current_hp <= 0)) return spell_result(world, RC_SPELL_DEAD);
    if (rc_player_command_should_queue(world))
        return spell_result(world, queue_player_command(world, RC_PLAYER_COMMAND_SELECT_SPELL,
            RC_ACTION_CATEGORY_SOFT, spell_idx, 0, 0, 0, 0, 0) ? RC_SPELL_QUEUED : RC_SPELL_QUEUE_FULL);
    RcSpellResult result = rc_spell_available(world, spell_idx, 0);
    if (result == RC_SPELL_OK) world->player.selected_spell = spell_idx;
    return spell_result(world, result);
}

RcSpellResult rc_player_set_autocast_spell(RcWorld *world, int spell_idx, int defensive) {
    if (world && (world->player.is_dead || world->player.current_hp <= 0)) return spell_result(world, RC_SPELL_DEAD);
    if (rc_player_command_should_queue(world))
        return spell_result(world, queue_player_command(world, RC_PLAYER_COMMAND_SET_AUTOCAST,
            RC_ACTION_CATEGORY_SOFT, spell_idx, defensive, 0, 0, 0, 0) ? RC_SPELL_QUEUED : RC_SPELL_QUEUE_FULL);
    if (!world) return RC_SPELL_INVALID;
    RcSpellResult result = spell_idx < 0 ? RC_SPELL_OK : rc_spell_available(world, spell_idx, 1);
    if (result != RC_SPELL_OK) return spell_result(world, result);
    world->player.autocast_spell = spell_idx < 0 ? -1 : spell_idx;
    world->player.defensive_autocast = spell_idx >= 0 && defensive != 0;
    rc_refresh_player_combat_style(&world->player);
    return spell_result(world, RC_SPELL_OK);
}
void rc_player_interact_npc(RcWorld *world, int npc_uid, int opt) {
    if (rc_player_command_should_queue(world)) {
        (void)queue_player_command(world, RC_PLAYER_COMMAND_INTERACT_NPC,
                                   RC_ACTION_CATEGORY_NORMAL,
                                   npc_uid, opt, 0, 0, 0, 0);
        return;
    }
    if (!action_allowed_if_loaded(world, RC_PLAYER_ACTION_INTERACT_NPC)) return;
    RcNpc *npc = api_find_npc_by_uid(world, npc_uid);
    if (!npc || npc->is_dead) return;
    if (npc->plane != world->player.plane) return;
    const RcNpcDef *def = npc_def_for(world, npc);
    if (!def) return;
    const char *option = rc_npc_def_option(def, opt);
    if (!option || !option[0]) return;
    RcInteractionTarget target = api_npc_interaction_target(world, npc);
    int attack = rc_npc_def_option_is_attack(def, opt);
    if (attack) target.content_group = RC_INTERACTION_CONTENT_GROUP_NPC_ATTACK;
    int range = attack ? rc_player_attack_range(&world->player) : 1;
    if (!rc_interaction_begin(&world->player, 0,
                              rc_interaction_op_from_option(opt),
                              option, &target, range)) {
        return;
    }
    api_commit_interaction_admission(world);
    (void)api_prepare_spatial_interaction(world);
}
void rc_player_interact_object(RcWorld *world, int obj_id, int opt) {
    (void)rc_player_interact_object_at(world, obj_id, -1, -1, -1, opt);
}

static int object_exact_placement(int obj_id, int x, int y, int plane,
                                  RcObjectPlacement *out) {
    if (!out || obj_id < 0 || !rc_world_tile_valid(x, y, plane)) return 0;
    uint16_t mapsquare;
    if (!rc_world_to_mapsquare(x, y, &mapsquare, NULL, NULL)) return 0;
    int count = 0;
    int matches = 0;
    const RcObjectPlacement *rows = rc_object_region_placements(
        mapsquare, &count);
    for (int i = 0; rows && i < count; i++) {
        if ((int)rows[i].obj_id == obj_id && rows[i].x == x
                && rows[i].y == y && rows[i].plane == plane) {
            *out = rows[i];
            matches++;
        }
    }
    return matches == 1;
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
    return rc_world_object_current_placement(world, obj_id, x, y, plane, 0, out);
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
    if (!rc_world_object_current_placement(world, target->definition_id,
                                      target->tile_x, target->tile_y,
                                      target->plane,
                                      target->placement_key, &placement)) {
        int min_x = target->tile_x;
        int min_y = target->tile_y;
        int max_x = target->tile_x + target->footprint_width - 1;
        int max_y = target->tile_y + target->footprint_height - 1;
        return !rc_object_has_placements()
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

static int traversal_edge_better(const RcTraversalEdge *candidate,
                                 int candidate_dist,
                                 const RcTraversalEdge *best,
                                 int best_dist) {
    if (!candidate) return 0;
    if (!best) return 1;
    if (candidate_dist != best_dist) return candidate_dist < best_dist;
    if (candidate->start_x != best->start_x)
        return candidate->start_x < best->start_x;
    if (candidate->start_y != best->start_y)
        return candidate->start_y < best->start_y;
    if (candidate->dest_plane != best->dest_plane)
        return candidate->dest_plane < best->dest_plane;
    if (candidate->dest_x != best->dest_x)
        return candidate->dest_x < best->dest_x;
    return candidate->dest_y < best->dest_y;
}

static int traversal_source_has_one_destination(
    const RcTraversalEdge *rows, int count, const RcTraversalEdge *candidate) {
    if (!rows || !candidate) return 0;
    for (int i = 0; i < count; i++) {
        const RcTraversalEdge *row = &rows[i];
        if (row->kind != candidate->kind
                || row->source_id != candidate->source_id
                || row->option != candidate->option
                || row->start_x != candidate->start_x
                || row->start_y != candidate->start_y
                || row->start_plane != candidate->start_plane) {
            continue;
        }
        if (row->dest_x != candidate->dest_x
                || row->dest_y != candidate->dest_y
                || row->dest_plane != candidate->dest_plane) {
            return 0;
        }
    }
    return 1;
}

static const RcTraversalEdge *object_traversal_edge(
    const RcWorld *world, int obj_id, int x, int y, int plane, int opt,
    uint64_t placement_key, int *has_candidate) {
    if (has_candidate) *has_candidate = 0;
    if (!world || !(world->enabled & RC_SUB_TRAVERSAL)
            || x < 0 || y < 0 || plane < 0 || opt < 0) {
        return NULL;
    }
    RcObjectPlacement placement;
    int have_placement = rc_world_object_current_placement(
        world, obj_id, x, y, plane, placement_key, &placement);
    if (rc_object_has_placements() && !have_placement) {
        return NULL;
    }
    if (!have_placement) return NULL;

    int count = 0;
    const RcTraversalEdge *rows =
        rc_traversal_edges_for(RC_TRAVERSAL_OBJECT, obj_id, &count);
    const RcObjectDef *def = rc_object_def_get(obj_id);
    const RcTraversalEdge *player_source = NULL;
    const RcTraversalEdge *nearest = NULL;
    int player_source_seen = 0;
    int player_source_blocked = 0;
    int nearest_dist = INT_MAX;
    for (int i = 0; rows && i < count; i++) {
        const RcTraversalEdge *row = &rows[i];
        if (row->kind != RC_TRAVERSAL_OBJECT
                || row->source_id != (uint32_t)obj_id)
            continue;
        if ((int)row->option != opt || (int)row->start_plane != plane) {
            continue;
        }
        if (source_tile_matches_placement(&placement, def, row->start_x,
                                          row->start_y)) {
            if (has_candidate) *has_candidate = 1;
            int at_player_source = (int)row->start_x == world->player.x
                && (int)row->start_y == world->player.y
                && (int)row->start_plane == world->player.plane;
            if (at_player_source) player_source_seen = 1;
            if (row->flags != 0
                    || row->source_semantics
                        != RC_TRAVERSAL_SOURCE_PLAYER_TILE
                    || !traversal_source_has_one_destination(
                        rows, count, row)) {
                if (at_player_source) player_source_blocked = 1;
                continue;
            }
            int dx = (int)row->start_x - world->player.x;
            int dy = (int)row->start_y - world->player.y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            int dist = dx > dy ? dx : dy;
            if (at_player_source
                    && traversal_edge_better(
                        row, dist, player_source, 0)) {
                player_source = row;
            }
            if (traversal_edge_better(row, dist, nearest, nearest_dist)) {
                nearest = row;
                nearest_dist = dist;
            }
        }
    }
    if (player_source_seen)
        return player_source_blocked ? NULL : player_source;
    return nearest;
}

static int object_option_available(const RcWorld *world, int obj_id,
                                   int x, int y, int plane,
                                   uint64_t placement_key,
                                   const RcObjectDef *def,
                                   const RcObjectBehavior *behavior,
                                   const RcTraversalEdge *edge, int opt) {
    (void)def;
    (void)placement_key;
    if (opt < 0 || opt >= RC_OBJECT_ACTIONS)
        return 0;
    if (edge) return edge->flags == 0;
    RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
    key.kind = RC_INTERACTION_OBJECT;
    key.op = rc_interaction_op_from_option(opt);
    key.definition_id = obj_id;
    if (rc_interaction_has_specific_world_handler(world, &key)) return 1;
    if (!behavior || !(behavior->action_mask & (1u << opt))) return 0;
    if (behavior->flags & RC_OBJ_BEHAVIOR_DOOR) return 1;
    if ((behavior->flags & (RC_OBJ_BEHAVIOR_BANK |
                            RC_OBJ_BEHAVIOR_STORAGE))
            && world && (world->enabled & RC_SUB_STORAGE)) {
        return 1;
    }
    if ((behavior->flags & RC_OBJ_BEHAVIOR_ALTAR)
            && behavior->skill == RC_OBJ_SKILL_PRAYER
            && world && (world->enabled & RC_SUB_PRAYER)) {
        return 1;
    }
    if ((behavior->flags & RC_OBJ_BEHAVIOR_RESOURCE)
            && world && (world->enabled & RC_SUB_SKILLS)
            && rc_world_tile_valid(x, y, plane)) {
        const RcGatheringNode *node = rc_gathering_node_find(
            obj_id, x, y, plane);
        return node && (node->action_mask & (1u << opt));
    }
    return 0;
}

int rc_world_object_option_supported(const RcWorld *world, int obj_id,
                                     int x, int y, int plane,
                                     uint64_t placement_key, int option) {
    if (!world || obj_id < 0 || option < 0 || option >= RC_OBJECT_ACTIONS)
        return 0;
    RcObjectPlacement placement;
    if (rc_world_tile_valid(x, y, plane) && rc_object_has_placements()) {
        if (!rc_world_object_current_placement(
                world, obj_id, x, y, plane, placement_key, &placement)) {
            return 0;
        }
        obj_id = (int)placement.obj_id;
        x = placement.x;
        y = placement.y;
        plane = placement.plane;
        placement_key = placement.key;
    } else {
        int effective_id = obj_id;
        if (rc_world_object_def_resolve(world, obj_id, &effective_id))
            obj_id = effective_id;
    }
    const RcObjectDef *def = rc_object_def_get(obj_id);
    if (!def) return 0;
    const RcObjectBehavior *behavior = rc_object_behavior_get(obj_id);
    const RcObjectState *state = placement_key
        ? rc_world_object_state_find_key_const(world, placement_key) : NULL;
    if (state && state->base_obj_id >= 0) {
        const RcObjectBehavior *base_behavior =
            rc_object_behavior_get(state->base_obj_id);
        if (base_behavior) behavior = base_behavior;
    }
    int has_traversal_candidate = 0;
    const RcTraversalEdge *edge = object_traversal_edge(
        world, obj_id, x, y, plane, option, placement_key,
        &has_traversal_candidate);
    if (!edge && has_traversal_candidate) return 0;
    return object_option_available(world, obj_id, x, y, plane, placement_key,
                                   def, behavior, edge, option);
}

const char *rc_world_object_option_label(
    const RcWorld *world, int obj_id, int x, int y, int plane,
    uint64_t placement_key, int option) {
    if (!world || option < 0 || option >= RC_OBJECT_ACTIONS) return "";
    const RcObjectDef *def = rc_object_def_get(obj_id);
    if (def && def->actions[option][0]) return def->actions[option];
    const RcTraversalEdge *edge = object_traversal_edge(
        world, obj_id, x, y, plane, option, placement_key, NULL);
    return edge && edge->action[0] ? edge->action : "";
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
    if (behavior && (behavior->flags & RC_OBJ_BEHAVIOR_PAIR_WIDE))
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
            && (behavior->flags & RC_OBJ_BEHAVIOR_PAIR_WIDE)
            && shape == 0) {
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

static int find_paired_dynamic_placement(const RcObjectPlacement *base,
                                         const RcObjectBehavior *behavior,
                                         RcObjectPlacement *out) {
    if (!base || !behavior ||
            !(behavior->flags & (RC_OBJ_BEHAVIOR_PAIR_LEFT |
                                 RC_OBJ_BEHAVIOR_PAIR_RIGHT))) {
        return 0;
    }
    int dx, dy;
    pair_left_to_right_delta(base->rotation, &dx, &dy);
    uint32_t need_flag = RC_OBJ_BEHAVIOR_PAIR_RIGHT;
    if (behavior->flags & RC_OBJ_BEHAVIOR_PAIR_RIGHT) {
        dx = -dx;
        dy = -dy;
        need_flag = RC_OBJ_BEHAVIOR_PAIR_LEFT;
    }
    int x = base->x + dx;
    int y = base->y + dy;
    uint16_t mapsquare;
    if (!rc_world_to_mapsquare(x, y, &mapsquare, NULL, NULL)) return 0;
    int count = 0;
    const RcObjectPlacement *rows = rc_object_region_placements(
        mapsquare, &count);
    int matches = 0;
    for (int i = 0; rows && i < count; i++) {
        if (rows[i].x != x || rows[i].y != y || rows[i].plane != base->plane)
            continue;
        const RcObjectBehavior *other =
            rc_object_behavior_get((int)rows[i].obj_id);
        if (!other)
            continue;
        if ((other->flags & need_flag) == 0)
            continue;
        if (rows[i].type != base->type)
            continue;
        if ((rows[i].rotation & 3) != (base->rotation & 3))
            continue;
        if (out) *out = rows[i];
        matches++;
    }
    return matches == 1;
}

static void door_open_replacement(const RcObjectPlacement *base,
                                  const RcObjectBehavior *behavior,
                                  RcObjectPlacement *replacement) {
    *replacement = *base;
    if (behavior && behavior->next_loc_stage >= 0)
        replacement->obj_id = (uint32_t)behavior->next_loc_stage;
    const RcObjectDef *def = rc_object_def_get((int)base->obj_id);
    replacement->rotation = (uint8_t)dynamic_open_rotation(
        behavior, def, base->type, base->rotation);
    int x = base->x;
    int y = base->y;
    dynamic_open_tile(behavior, def, base->type, base->rotation, &x, &y);
    replacement->x = (uint16_t)x;
    replacement->y = (uint16_t)y;
    (void)rc_world_to_mapsquare(x, y, &replacement->mapsquare, NULL, NULL);
}

static int apply_door_state(RcWorld *world, int obj_id, int x, int y,
                            int plane, uint64_t placement_key,
                            const RcObjectBehavior *behavior) {
    if (!world || !rc_world_tile_valid(x, y, plane)) return 0;
    RcObjectState *state = placement_key
        ? rc_world_object_state_find_key(world, placement_key)
        : rc_world_object_state_find(world, obj_id, x, y, plane);
    if (state) {
        RcObjectPlacement base = {
            .obj_id = (uint32_t)state->base_obj_id,
            .key = state->placement_key,
            .x = (uint16_t)state->x,
            .y = (uint16_t)state->y,
            .plane = (uint8_t)state->plane,
            .type = state->base_type,
            .rotation = state->base_rotation,
        };
        RcObjectPlacement pair;
        int paired = find_paired_dynamic_placement(&base, behavior, &pair);
        if (paired) (void)rc_world_object_revert(world, pair.key);
        return rc_world_object_revert(world, base.key)
            == RC_OBJECT_MUTATION_OK;
    }

    RcObjectPlacement base;
    if (placement_key) {
        if (!rc_object_placement_find_key(placement_key, x, y, plane, &base))
            return 0;
    } else if (!object_exact_placement(obj_id, x, y, plane, &base)) {
        return 0;
    }
    RcObjectPlacement replacement;
    door_open_replacement(&base, behavior, &replacement);
    RcObjectMutationResult result = rc_world_object_replace(
        world, &base, &replacement, RC_OBJECT_DOOR_REVERT_TICKS,
        RC_OBJECT_STATE_OPEN);
    if (result != RC_OBJECT_MUTATION_OK) return 0;

    RcObjectPlacement pair;
    if (!find_paired_dynamic_placement(&base, behavior, &pair)) return 1;
    const RcObjectBehavior *pair_behavior =
        rc_object_behavior_get((int)pair.obj_id);
    if (!pair_behavior) {
        (void)rc_world_object_revert(world, base.key);
        return 0;
    }
    RcObjectPlacement pair_replacement;
    door_open_replacement(&pair, pair_behavior, &pair_replacement);
    result = rc_world_object_replace(
        world, &pair, &pair_replacement, RC_OBJECT_DOOR_REVERT_TICKS,
        RC_OBJECT_STATE_OPEN);
    if (result != RC_OBJECT_MUTATION_OK) {
        (void)rc_world_object_revert(world, base.key);
        return 0;
    }
    return 1;
}

static void apply_altar_effect(RcWorld *world, const RcObjectDef *def,
                               int obj_id, int opt) {
    const char *action = def ? def->actions[opt] : "";
    int restores = !action || action[0] == '\0' || action[0] == 'P'
                || action[0] == 'R' || action[0] == 'W';
    if (restores && (world->enabled & RC_SUB_PRAYER)) {
        rc_prayer_recharge(&world->player);
    }
    if (action && action[0] == 'O' && (world->enabled & RC_SUB_SKILLS)) {
        world->player.skill_action = obj_id;
        world->player.skill_ready_tick = world->tick + 1;
    }
}

static int api_apply_object_interaction(RcWorld *world, const RcObjectDef *def,
                                        const RcObjectBehavior *behavior,
                                        int obj_id, int x, int y, int plane,
                                        uint64_t placement_key, int opt) {
    if (!world || opt < 0 || opt >= RC_OBJECT_ACTIONS) return 0;
    RcObjectPlacement current = {0};
    if (rc_world_tile_valid(x, y, plane) && rc_object_has_placements()) {
        if (!rc_world_object_current_placement(
                world, obj_id, x, y, plane, placement_key, &current)) {
            return 0;
        }
        obj_id = (int)current.obj_id;
        x = current.x;
        y = current.y;
        plane = current.plane;
        placement_key = current.key;
        def = rc_object_def_get(obj_id);
        behavior = rc_object_behavior_get(obj_id);
    }
    RcObjectState *state = placement_key
        ? rc_world_object_state_find_key(world, placement_key)
        : rc_world_object_state_find(world, obj_id, x, y, plane);
    if (state && placement_key
            && !rc_world_object_state_matches(state, obj_id, x, y, plane)) {
        return 0;
    }
    const RcObjectBehavior *effective_behavior = behavior;
    if (state) {
        const RcObjectBehavior *base_behavior =
            rc_object_behavior_get(state->base_obj_id);
        if (base_behavior)
            effective_behavior = base_behavior;
    }
    RcTraversalState *traversal = &world->player.traversal;
    int planned_traversal = traversal->active
        && traversal->phase == RC_TRAVERSAL_PHASE_APPROACH
        && traversal->interaction_generation
            == world->player.interaction.generation
        && traversal->source_kind == RC_TRAVERSAL_OBJECT
        && traversal->source_id == obj_id
        && traversal->source_option == opt
        && (!placement_key || traversal->source_key == placement_key);
    if (!planned_traversal
            && !object_option_available(
                world, obj_id, x, y, plane, placement_key,
                def, effective_behavior, NULL, opt)) {
        return 0;
    }
    int applied = 0;
    if (rc_storage_kind_for_object(world, obj_id, opt)
            || (effective_behavior && (effective_behavior->flags
                & (RC_OBJ_BEHAVIOR_BANK | RC_OBJ_BEHAVIOR_STORAGE)))) {
        if (!rc_storage_open_interaction(world)) {
            rc_interaction_publish_message(&world->player,
                                            "This storage service is not supported.");
            return 0;
        }
        applied = 1;
    }
    if (effective_behavior
            && (effective_behavior->flags & RC_OBJ_BEHAVIOR_DOOR)
            && !planned_traversal && !applied) {
        if (!apply_door_state(world, obj_id, x, y, plane, placement_key,
                              effective_behavior)) {
            return 0;
        }
        start_player_action_lock(world, 1);
        applied = 1;
    }
    if (effective_behavior && (effective_behavior->flags & RC_OBJ_BEHAVIOR_ALTAR)
            && effective_behavior->skill == RC_OBJ_SKILL_PRAYER
            && !planned_traversal && !applied) {
        apply_altar_effect(world, def, obj_id, opt);
        applied = 1;
    }
    if (x >= 0 && y >= 0 && plane >= 0
            && effective_behavior
            && (effective_behavior->flags & RC_OBJ_BEHAVIOR_RESOURCE)
            && (world->enabled & RC_SUB_SKILLS) && !planned_traversal) {
        const RcGatheringNode *node = rc_gathering_node_find(
            obj_id, x, y, plane);
        if (node && (node->action_mask & (1u << opt))) {
            world->player.skill_action = obj_id;
            world->player.skill_ready_tick = world->tick + 1;
            world->player.skill_target_x = x;
            world->player.skill_target_y = y;
            applied = 1;
        }
    }
    if (planned_traversal && !applied) {
        if (!rc_traversal_start(
                world, world->player.interaction.generation)) {
            return 0;
        }
        applied = 1;
    }
    if (!applied) return 0;
    world->player.interact_type = RC_INTERACT_OBJECT;
    world->player.interact_target = obj_id;
    world->player.interact_option = opt;
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
    uint64_t generation = pending->generation;
    if (!api_apply_object_interaction(world, def, behavior,
                                      pending->target.definition_id,
                                      pending->target.tile_x,
                                      pending->target.tile_y,
                                      pending->target.plane,
                                      pending->target.placement_key, opt)) {
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_NO_HANDLER,
            "No object interaction handler");
    }
    if (world->player.traversal.active
            && world->player.traversal.interaction_generation == generation
            && world->player.traversal.phase
                != RC_TRAVERSAL_PHASE_APPROACH) {
        return rc_interaction_result_handoff();
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

static int api_register_default_object_handlers(RcWorld *world) {
    if (!world) return 0;
    for (int i = 0; i < 5; i++) {
        RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
        key.kind = RC_INTERACTION_OBJECT;
        key.op = rc_interaction_op_from_option(i);
        if (!register_default_handler(world, &key,
                                      api_default_object_handler))
            return 0;
    }
    return 1;
}

static int api_register_default_ground_item_handlers(RcWorld *world) {
    if (!world) return 0;
    RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
    key.kind = RC_INTERACTION_GROUND_ITEM;
    key.op = RC_INTERACTION_OP1;
    if (!register_default_handler(world, &key,
                                  api_default_ground_item_handler))
        return 0;
    key.op = RC_INTERACTION_USE_ON;
    if (!register_default_handler(world, &key,
                                  api_default_ground_item_handler))
        return 0;
    key.op = RC_INTERACTION_SPELL_ON;
    if (!register_default_handler(world, &key,
                                  api_default_ground_item_handler))
        return 0;
    return 1;
}

int rc_interaction_install_world_defaults(RcWorld *world) {
    if (!api_register_default_npc_handlers(world)
            || !api_register_default_object_handlers(world)
            || !api_register_default_ground_item_handlers(world)) {
        return 0;
    }
    return 1;
}

int rc_player_interact_object_at(RcWorld *world, int obj_id, int x, int y,
                                 int plane, int opt) {
    return rc_player_interact_object_placement(world, obj_id, x, y, plane, 0,
                                               opt);
}

int rc_player_interact_object_placement(RcWorld *world, int obj_id, int x,
                                        int y, int plane,
                                        uint64_t placement_key, int opt) {
    if (rc_player_command_should_queue(world)) {
        return queue_player_command(world, RC_PLAYER_COMMAND_INTERACT_OBJECT,
                                    RC_ACTION_CATEGORY_NORMAL,
                                    obj_id, x, y, plane, opt, placement_key);
    }
    if (!world || !rc_player_action_allowed(world->enabled,
                                            RC_PLAYER_ACTION_INTERACT_OBJECT)) {
        return 0;
    }
    if (x >= 0 && y >= 0 && plane >= 0
            && plane != world->player.plane) {
        return 0;
    }
    RcObjectPlacement placement = {0};
    if (x >= 0 && y >= 0 && plane >= 0 && rc_object_has_placements()) {
        if (!rc_world_object_current_placement(
                world, obj_id, x, y, plane, placement_key, &placement)) {
            return 0;
        }
        obj_id = (int)placement.obj_id;
        x = placement.x;
        y = placement.y;
        plane = placement.plane;
        placement_key = placement.key;
    }
    const RcObjectDef *def = rc_object_def_get(obj_id);
    if (opt < 0 || opt >= RC_OBJECT_ACTIONS) return 0;
    if (!rc_world_object_option_supported(world, obj_id, x, y, plane,
                                          placement_key, opt)) {
        return 0;
    }
    const RcObjectBehavior *behavior = rc_object_behavior_get(obj_id);
    const RcTraversalEdge *edge = object_traversal_edge(
        world, obj_id, x, y, plane, opt, placement_key, NULL);
    RcObjectState *state = rc_world_object_state_find(world, obj_id, x, y, plane);
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
        api_commit_interaction_admission(world);
        if (edge) {
            int climb = behavior
                && (behavior->flags & (RC_OBJ_BEHAVIOR_LADDER |
                                       RC_OBJ_BEHAVIOR_STAIR));
            RcTraversalPresentation presentation = climb
                ? RC_TRAVERSAL_PRESENTATION_CLIMB
                : RC_TRAVERSAL_PRESENTATION_INSTANT;
            RcTraversalDestinationSpec destination = {
                .policy = RC_TRAVERSAL_POLICY_EXACT,
                .destination = {
                    .x = edge->dest_x,
                    .y = edge->dest_y,
                    .plane = edge->dest_plane,
                },
            };
            RcTraversalMotionSpec motion = {
                .presentation = presentation,
                .takeoff_ticks = climb ? 1 : 0,
            };
            if (!rc_traversal_plan_object(
                    world, edge, placement_key,
                    world->player.interaction.generation, &destination,
                    &motion)) {
                rc_interaction_cancel(&world->player,
                                      RC_INTERACTION_FAIL_INVALID_SOURCE);
                return 0;
            }
        }
        int prepared = api_prepare_spatial_interaction(world);
        if (!prepared)
            rc_traversal_cancel(world, RC_ACTION_CANCEL_REPLACED);
        return prepared;
    }
    int applied = api_apply_object_interaction(world, def, behavior, obj_id,
                                               x, y, plane, 0, opt);
    if (applied) api_stop_player_combat(world);
    return applied;
}

int rc_player_interact_inventory_item(RcWorld *world, int inv_slot,
                                      int option) {
    if (world && inv_slot >= 0 && inv_slot < RC_INVENTORY_SIZE) {
        int action = rc_consumable_action(world->player.inventory[inv_slot].item_id, option);
        if (action) return action == 2 ? rc_player_drink(world, inv_slot)
                                      : rc_player_eat(world, inv_slot);
    }
    if (rc_player_command_should_queue(world)) {
        uint32_t generation = world && inv_slot >= 0
                            && inv_slot < RC_INVENTORY_SIZE
                            ? world->player.inventory[inv_slot].generation : 0;
        return queue_player_command(world,
                                    RC_PLAYER_COMMAND_INTERACT_INVENTORY,
                                    RC_ACTION_CATEGORY_NORMAL,
                                    inv_slot, option, (int)generation,
                                    0, 0, 0);
    }
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
    if (begun) {
        p->interaction.source_inventory_slot = inv_slot;
        p->interaction.source_item_generation =
            p->inventory[inv_slot].generation;
    }
    if (begun) api_commit_interaction_admission(world);
    return begun;
}

int rc_player_interact_equipment_item(RcWorld *world, int equip_slot,
                                      int option) {
    if (rc_player_command_should_queue(world)) {
        uint32_t generation = world && equip_slot >= 0
                            && equip_slot < RC_EQUIP_COUNT
                            ? world->player.equipment[equip_slot].generation
                            : 0;
        return queue_player_command(world,
                                    RC_PLAYER_COMMAND_INTERACT_EQUIPMENT,
                                    RC_ACTION_CATEGORY_NORMAL,
                                    equip_slot, option, (int)generation,
                                    0, 0, 0);
    }
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
    if (begun) api_commit_interaction_admission(world);
    return begun;
}

int rc_player_widget_action(RcWorld *world, int widget_id,
                            int component_id, int action) {
    if (rc_player_command_should_queue(world)) {
        return queue_player_command(world, RC_PLAYER_COMMAND_WIDGET_ACTION,
                                    RC_ACTION_CATEGORY_NORMAL,
                                    widget_id, component_id, action,
                                    0, 0, 0);
    }
    if (!world || (widget_id < 0 && component_id < 0) || action < 0) {
        return 0;
    }
    RcInteractionTarget target =
        api_widget_interaction_target(widget_id, component_id, action);
    int begun = rc_interaction_begin_with_source(
        &world->player, 0, RC_INTERACTION_WIDGET_ACTION, "Widget",
        &target, 0, RC_INTERACTION_KEY_ANY, RC_INTERACTION_KEY_ANY,
        widget_id, component_id);
    if (begun) api_commit_interaction_admission(world);
    return begun;
}

int rc_player_use_inventory_item_on_npc(RcWorld *world, int inv_slot,
                                        int npc_uid) {
    if (rc_player_command_should_queue(world)) {
        uint32_t generation = world && inv_slot >= 0
                            && inv_slot < RC_INVENTORY_SIZE
                            ? world->player.inventory[inv_slot].generation : 0;
        return queue_player_command(world, RC_PLAYER_COMMAND_USE_ITEM_ON_NPC,
                                    RC_ACTION_CATEGORY_NORMAL,
                                    inv_slot, npc_uid, (int)generation,
                                    0, 0, 0);
    }
    if (!world || inv_slot < 0 || inv_slot >= RC_INVENTORY_SIZE) return 0;
    RcPlayer *p = &world->player;
    int item_id = p->inventory[inv_slot].item_id;
    if (item_id < 0 || p->inventory[inv_slot].quantity <= 0) return 0;
    RcNpc *npc = api_find_npc_by_uid(world, npc_uid);
    if (!npc || npc->is_dead) return 0;
    RcInteractionTarget target = api_npc_interaction_target(world, npc);
    int begun = rc_interaction_begin_with_source(
        p, 0, RC_INTERACTION_USE_ON, "Use", &target, 1, item_id,
        RC_INTERACTION_KEY_ANY, RC_INTERACTION_KEY_ANY,
        RC_INTERACTION_KEY_ANY);
    if (begun) {
        p->interaction.source_inventory_slot = inv_slot;
        p->interaction.source_item_generation =
            p->inventory[inv_slot].generation;
    }
    if (begun) api_commit_interaction_admission(world);
    return begun ? api_prepare_spatial_interaction(world) : 0;
}

int rc_player_use_inventory_item_on_inventory_item(RcWorld *world,
                                                   int source_slot,
                                                   int target_slot) {
    if (rc_player_command_should_queue(world)) {
        uint32_t source_generation = world && source_slot >= 0
                                   && source_slot < RC_INVENTORY_SIZE
                                   ? world->player.inventory[source_slot]
                                         .generation : 0;
        uint32_t target_generation = world && target_slot >= 0
                                   && target_slot < RC_INVENTORY_SIZE
                                   ? world->player.inventory[target_slot]
                                         .generation : 0;
        return queue_player_command(world, RC_PLAYER_COMMAND_USE_ITEM_ON_ITEM,
                                    RC_ACTION_CATEGORY_NORMAL,
                                    source_slot, target_slot,
                                    (int)source_generation,
                                    (int)target_generation, 0, 0);
    }
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
    if (begun) {
        p->interaction.source_inventory_slot = source_slot;
        p->interaction.source_item_generation =
            p->inventory[source_slot].generation;
    }
    if (begun) api_commit_interaction_admission(world);
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
    if (rc_player_command_should_queue(world)) {
        uint32_t generation = world && inv_slot >= 0
                            && inv_slot < RC_INVENTORY_SIZE
                            ? world->player.inventory[inv_slot].generation : 0;
        int args[8] = {
            inv_slot, obj_id, x, y, plane, (int)generation, 0, 0
        };
        return rc_player_command_submit(
            world, RC_PLAYER_COMMAND_USE_ITEM_ON_OBJECT,
            RC_ACTION_CATEGORY_NORMAL, args, placement_key);
    }
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
    if (begun) {
        p->interaction.source_inventory_slot = inv_slot;
        p->interaction.source_item_generation =
            p->inventory[inv_slot].generation;
    }
    if (begun) api_commit_interaction_admission(world);
    return begun ? api_prepare_spatial_interaction(world) : 0;
}

int rc_player_use_inventory_item_on_ground_item(RcWorld *world,
                                                int inv_slot,
                                                int ground_item_idx) {
    if (rc_player_command_should_queue(world)) {
        uint32_t generation = world && inv_slot >= 0
                            && inv_slot < RC_INVENTORY_SIZE
                            ? world->player.inventory[inv_slot].generation : 0;
        int ground_uid = -1;
        int ground_version = -1;
        if (world && ground_item_idx >= 0
                && ground_item_idx < world->ground_item_count) {
            ground_uid = world->ground_items[ground_item_idx].uid;
            ground_version = world->ground_items[ground_item_idx].version;
        }
        return queue_player_command(world,
                                    RC_PLAYER_COMMAND_USE_ITEM_ON_GROUND,
                                    RC_ACTION_CATEGORY_NORMAL,
                                    inv_slot, ground_item_idx,
                                    (int)generation, ground_uid,
                                    ground_version, 0);
    }
    if (!world || inv_slot < 0 || inv_slot >= RC_INVENTORY_SIZE ||
            ground_item_idx < 0 ||
            ground_item_idx >= world->ground_item_count) {
        return 0;
    }
    RcPlayer *p = &world->player;
    int item_id = p->inventory[inv_slot].item_id;
    if (item_id < 0 || p->inventory[inv_slot].quantity <= 0) return 0;
    RcGroundItem *ground = &world->ground_items[ground_item_idx];
    if (!rc_ground_item_visible(ground, RC_GROUND_OWNER_LOCAL_PLAYER)) return 0;
    if (ground->plane != p->plane) return 0;
    RcInteractionTarget target =
        api_ground_item_interaction_target(ground, ground_item_idx);
    int begun = rc_interaction_begin_with_source(
        p, 0, RC_INTERACTION_USE_ON, "Use", &target, 0, item_id,
        RC_INTERACTION_KEY_ANY, RC_INTERACTION_KEY_ANY,
        RC_INTERACTION_KEY_ANY);
    if (begun) {
        p->interaction.source_inventory_slot = inv_slot;
        p->interaction.source_item_generation =
            p->inventory[inv_slot].generation;
    }
    if (begun) api_commit_interaction_admission(world);
    return begun ? api_prepare_spatial_interaction(world) : 0;
}

int rc_player_use_inventory_item_on_widget(RcWorld *world, int inv_slot,
                                           int widget_id, int component_id) {
    if (rc_player_command_should_queue(world)) {
        uint32_t generation = world && inv_slot >= 0
                            && inv_slot < RC_INVENTORY_SIZE
                            ? world->player.inventory[inv_slot].generation : 0;
        return queue_player_command(world,
                                    RC_PLAYER_COMMAND_USE_ITEM_ON_WIDGET,
                                    RC_ACTION_CATEGORY_NORMAL,
                                    inv_slot, widget_id, component_id,
                                    (int)generation, 0, 0);
    }
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
    if (begun) {
        p->interaction.source_inventory_slot = inv_slot;
        p->interaction.source_item_generation =
            p->inventory[inv_slot].generation;
    }
    if (begun) api_commit_interaction_admission(world);
    return begun;
}

int rc_player_cast_spell_on_npc(RcWorld *world, int spell_id, int npc_uid) {
    if (rc_player_command_should_queue(world)) {
        return queue_player_command(world, RC_PLAYER_COMMAND_CAST_ON_NPC,
                                    RC_ACTION_CATEGORY_NORMAL,
                                    spell_id, npc_uid, 0, 0, 0, 0);
    }
    if (!interaction_spell_source_valid(world, spell_id)) return 0;
    RcNpc *npc = api_find_npc_by_uid(world, npc_uid);
    if (!npc || npc->is_dead) return 0;
    if (npc->plane != world->player.plane) return 0;
    RcInteractionTarget target = api_npc_interaction_target(world, npc);
    target.content_group = RC_INTERACTION_CONTENT_GROUP_NPC_ATTACK;
    int begun = rc_interaction_begin_with_source(
        &world->player, 0, RC_INTERACTION_SPELL_ON, "Cast", &target, 10,
        RC_INTERACTION_KEY_ANY, spell_id, RC_INTERACTION_KEY_ANY,
        RC_INTERACTION_KEY_ANY);
    if (begun) api_commit_interaction_admission(world);
    return begun ? api_prepare_spatial_interaction(world) : 0;
}

int rc_player_cast_spell_on_inventory_item(RcWorld *world, int spell_id,
                                           int target_slot) {
    if (rc_player_command_should_queue(world)) {
        uint32_t generation = world && target_slot >= 0
                            && target_slot < RC_INVENTORY_SIZE
                            ? world->player.inventory[target_slot].generation
                            : 0;
        return queue_player_command(world, RC_PLAYER_COMMAND_CAST_ON_ITEM,
                                    RC_ACTION_CATEGORY_NORMAL,
                                    spell_id, target_slot, (int)generation,
                                    0, 0, 0);
    }
    if (!interaction_spell_source_valid(world, spell_id) || target_slot < 0
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
    if (begun) api_commit_interaction_admission(world);
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
    if (rc_player_command_should_queue(world)) {
        return queue_player_command(world, RC_PLAYER_COMMAND_CAST_ON_OBJECT,
                                    RC_ACTION_CATEGORY_NORMAL,
                                    spell_id, obj_id, x, y, plane,
                                    placement_key);
    }
    if (!interaction_spell_source_valid(world, spell_id)) return 0;
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
    if (begun) api_commit_interaction_admission(world);
    return begun ? api_prepare_spatial_interaction(world) : 0;
}

int rc_player_cast_spell_on_widget(RcWorld *world, int spell_id,
                                   int widget_id, int component_id) {
    if (rc_player_command_should_queue(world)) {
        return queue_player_command(world, RC_PLAYER_COMMAND_CAST_ON_WIDGET,
                                    RC_ACTION_CATEGORY_NORMAL,
                                    spell_id, widget_id, component_id,
                                    0, 0, 0);
    }
    if (!interaction_spell_source_valid(world, spell_id)
            || (widget_id < 0 && component_id < 0)) {
        return 0;
    }
    RcInteractionTarget target =
        api_widget_interaction_target(widget_id, component_id, 0);
    int begun = rc_interaction_begin_with_source(
        &world->player, 0, RC_INTERACTION_SPELL_ON, "Cast", &target, 0,
        RC_INTERACTION_KEY_ANY, spell_id, RC_INTERACTION_KEY_ANY,
        RC_INTERACTION_KEY_ANY);
    if (begun) api_commit_interaction_admission(world);
    return begun;
}

int rc_player_cast_spell_on_ground_item(RcWorld *world, int spell_id,
                                        int ground_item_idx) {
    if (rc_player_command_should_queue(world)) {
        int uid = -1, version = -1;
        if (world && ground_item_idx >= 0
                && ground_item_idx < world->ground_item_count) {
            uid = world->ground_items[ground_item_idx].uid;
            version = world->ground_items[ground_item_idx].version;
        }
        return queue_player_command(world, RC_PLAYER_COMMAND_CAST_ON_GROUND,
                                    RC_ACTION_CATEGORY_NORMAL,
                                    spell_id, ground_item_idx, uid, version,
                                    0, 0);
    }
    if (!interaction_spell_source_valid(world, spell_id)
            || ground_item_idx < 0 ||
            ground_item_idx >= world->ground_item_count) {
        return 0;
    }
    RcGroundItem *ground = &world->ground_items[ground_item_idx];
    if (!rc_ground_item_visible(ground, RC_GROUND_OWNER_LOCAL_PLAYER)) return 0;
    if (ground->plane != world->player.plane) return 0;
    RcInteractionTarget target =
        api_ground_item_interaction_target(ground, ground_item_idx);
    int begun = rc_interaction_begin_with_source(
        &world->player, 0, RC_INTERACTION_SPELL_ON, "Cast", &target, 10,
        RC_INTERACTION_KEY_ANY, spell_id, RC_INTERACTION_KEY_ANY,
        RC_INTERACTION_KEY_ANY);
    if (begun) api_commit_interaction_admission(world);
    return begun ? api_prepare_spatial_interaction(world) : 0;
}

static int publish_examine(RcPlayer *player, const char *name,
                           const char *description) {
    if (!player) return 0;
    if (description && description[0]) {
        rc_interaction_publish_message(player, description);
        return 1;
    }
    if (!name || !name[0]) return 0;
    char message[RC_INTERACTION_RESULT_MESSAGE_LEN];
    snprintf(message, sizeof(message), "It's a %s.", name);
    rc_interaction_publish_message(player, message);
    return 1;
}

int rc_player_examine_npc(RcWorld *world, int npc_uid) {
    if (rc_player_command_should_queue(world)) {
        return queue_player_command(world, RC_PLAYER_COMMAND_EXAMINE_NPC,
                                    RC_ACTION_CATEGORY_SOFT,
                                    npc_uid, 0, 0, 0, 0, 0);
    }
    RcNpc *npc = api_find_npc_by_uid(world, npc_uid);
    const RcNpcDef *def = npc_def_for(world, npc);
    if (!npc || npc->is_dead || !def) return 0;
    return publish_examine(&world->player, def->name, NULL);
}

int rc_player_examine_object_placement(
    RcWorld *world, int obj_id, int x, int y, int plane,
    uint64_t placement_key) {
    if (rc_player_command_should_queue(world)) {
        return queue_player_command(world, RC_PLAYER_COMMAND_EXAMINE_OBJECT,
                                    RC_ACTION_CATEGORY_SOFT,
                                    obj_id, x, y, plane, 0, placement_key);
    }
    if (!world || !rc_world_tile_valid(x, y, plane)) return 0;
    RcObjectPlacement placement;
    if (rc_object_has_placements()) {
        if (!rc_world_object_current_placement(
                world, obj_id, x, y, plane, placement_key, &placement)) {
            return 0;
        }
        obj_id = (int)placement.obj_id;
    }
    const RcObjectDef *def = rc_object_def_get(obj_id);
    return def ? publish_examine(&world->player, def->name, NULL) : 0;
}

int rc_player_examine_inventory_item(RcWorld *world, int inv_slot) {
    if (rc_player_command_should_queue(world)) {
        uint32_t generation = world && inv_slot >= 0
                            && inv_slot < RC_INVENTORY_SIZE
                            ? world->player.inventory[inv_slot].generation : 0;
        return queue_player_command(world, RC_PLAYER_COMMAND_EXAMINE_INVENTORY,
                                    RC_ACTION_CATEGORY_SOFT,
                                    inv_slot, (int)generation, 0, 0, 0, 0);
    }
    if (!world || inv_slot < 0 || inv_slot >= RC_INVENTORY_SIZE) return 0;
    int item_id = world->player.inventory[inv_slot].item_id;
    const RcItemDef *def = rc_item_def_get(item_id);
    return def ? publish_examine(&world->player, def->name, def->examine) : 0;
}

int rc_player_examine_equipment_item(RcWorld *world, int equip_slot) {
    if (rc_player_command_should_queue(world)) {
        uint32_t generation = world && equip_slot >= 0
                            && equip_slot < RC_EQUIP_COUNT
                            ? world->player.equipment[equip_slot].generation : 0;
        return queue_player_command(world, RC_PLAYER_COMMAND_EXAMINE_EQUIPMENT,
                                    RC_ACTION_CATEGORY_SOFT,
                                    equip_slot, (int)generation, 0, 0, 0, 0);
    }
    if (!world || equip_slot < 0 || equip_slot >= RC_EQUIP_COUNT) return 0;
    int item_id = world->player.equipment[equip_slot].item_id;
    const RcItemDef *def = rc_item_def_get(item_id);
    return def ? publish_examine(&world->player, def->name, def->examine) : 0;
}

int rc_player_examine_ground_item(RcWorld *world, int ground_item_idx) {
    if (rc_player_command_should_queue(world)) {
        int uid = -1, version = -1;
        if (world && ground_item_idx >= 0
                && ground_item_idx < world->ground_item_count) {
            uid = world->ground_items[ground_item_idx].uid;
            version = world->ground_items[ground_item_idx].version;
        }
        return queue_player_command(
            world, RC_PLAYER_COMMAND_EXAMINE_GROUND_ITEM,
            RC_ACTION_CATEGORY_SOFT,
            ground_item_idx, uid, version, 0, 0, 0);
    }
    if (!world || ground_item_idx < 0
            || ground_item_idx >= world->ground_item_count) return 0;
    const RcGroundItem *ground = &world->ground_items[ground_item_idx];
    if (!rc_ground_item_visible(ground, RC_GROUND_OWNER_LOCAL_PLAYER)
            || ground->plane != world->player.plane || ground->quantity <= 0) return 0;
    const RcItemDef *def = rc_item_def_get(ground->item_id);
    return def ? publish_examine(&world->player, def->name, def->examine) : 0;
}
