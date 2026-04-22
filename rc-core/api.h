#ifndef RC_API_H
#define RC_API_H

#include "types.h"
#include "config.h"

// Lifecycle. Prefer `rc_world_create_config` for new code — it lets
// you pick a subsystem preset (full game, combat-only sim,
// skilling-only, base-only). `rc_world_create(seed)` is a
// convenience wrapper for the full-game preset.
RcWorld *rc_world_create_config(const RcWorldConfig *cfg);
RcWorld *rc_world_create(uint32_t seed);
void     rc_world_destroy(RcWorld *world);

// Tick
void rc_world_tick(RcWorld *world);

// Player input (queued, processed next tick)
void rc_player_walk_to(RcWorld *world, int tile_x, int tile_y);
void rc_player_run_to(RcWorld *world, int tile_x, int tile_y);
void rc_player_attack_npc(RcWorld *world, int npc_uid);
void rc_player_set_prayer(RcWorld *world, int prayer_id);
void rc_player_eat(RcWorld *world, int inv_slot);
void rc_player_drink(RcWorld *world, int inv_slot);
void rc_player_equip(RcWorld *world, int inv_slot);
void rc_player_unequip(RcWorld *world, int equip_slot);
void rc_player_interact_npc(RcWorld *world, int npc_uid, int option);
void rc_player_interact_object(RcWorld *world, int obj_id, int option);
void rc_player_drop_item(RcWorld *world, int inv_slot);
void rc_player_pickup_item(RcWorld *world, int ground_item_idx);

// State read (frontend reads, never writes)
const RcPlayer *rc_get_player(const RcWorld *world);
const RcNpc    *rc_get_npcs(const RcWorld *world, int *count);

#endif
