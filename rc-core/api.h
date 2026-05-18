#ifndef RC_API_H
#define RC_API_H

#include "types.h"
#include "config.h"
#include "interaction.h"
#include "npc.h"
#include "skills.h"
#include "traversal.h"

enum {
    RC_ACTIVE_AREA_LOAD_COLLISION = 1u << 0,
    RC_ACTIVE_AREA_LOAD_NPCS      = 1u << 1,
    RC_ACTIVE_AREA_CLEAR_NPCS     = 1u << 2,
};

typedef struct {
    int origin_x, origin_y;
    int width, height;
    int min_plane, max_plane;
    uint32_t flags;
    const char *npc_spawns_path;
} RcActiveAreaRequest;

typedef struct {
    int collision_regions;
    int spawned_npcs;
    RcNpcSpawnLoadStats npc_stats;
    RcActiveArea active_area;
} RcActiveAreaStats;

typedef struct {
    int index;
    int uid;
    int spawned;
} RcNpcEnsureResult;

// Lifecycle. Prefer `rc_world_create_config` for new code — it lets
// you pick a subsystem preset (full game, combat-only sim,
// skilling-only, base-only). `rc_world_create(seed)` is a
// convenience wrapper for the full-game preset.
RcWorld *rc_world_create_config(const RcWorldConfig *cfg);
RcWorld *rc_world_create(uint32_t seed);
void     rc_world_destroy(RcWorld *world);

// Backend-owned gameplay area activation. The viewer may choose which visual
// scene to display, but core owns the collision window and active NPC slice.
int rc_world_activate_area(RcWorld *world, const RcActiveAreaRequest *request,
                           RcActiveAreaStats *stats);
const RcActiveArea *rc_world_get_active_area(const RcWorld *world);
int rc_world_find_npc_near(const RcWorld *world, int npc_id, int x, int y,
                           int plane, int radius);
int rc_world_ensure_npc_near(RcWorld *world, int npc_id, int x, int y,
                             int plane, int radius,
                             RcNpcEnsureResult *result);

// Tick
void rc_world_tick(RcWorld *world);

// Player input (queued, processed next tick)
void rc_player_walk_to(RcWorld *world, int tile_x, int tile_y);
void rc_player_run_to(RcWorld *world, int tile_x, int tile_y);
void rc_player_attack_npc(RcWorld *world, int npc_uid);
void rc_player_set_attack_style(RcWorld *world, int style_idx);
void rc_player_set_prayer(RcWorld *world, int prayer_id);
void rc_player_set_spellbook(RcWorld *world, int spellbook);
void rc_player_select_spell(RcWorld *world, int spell_idx);
void rc_player_set_autocast_spell(RcWorld *world, int spell_idx,
                                  int defensive);
void rc_player_eat(RcWorld *world, int inv_slot);
void rc_player_drink(RcWorld *world, int inv_slot);
int  rc_player_move_inventory_item(RcWorld *world, int from_slot,
                                   int to_slot);
void rc_player_equip(RcWorld *world, int inv_slot);
void rc_player_unequip(RcWorld *world, int equip_slot);
void rc_player_interact_npc(RcWorld *world, int npc_uid, int option);
void rc_player_interact_object(RcWorld *world, int obj_id, int option);
int  rc_player_interact_object_at(RcWorld *world, int obj_id, int x, int y,
                                  int plane, int option);
int  rc_player_interact_object_placement(RcWorld *world, int obj_id, int x,
                                         int y, int plane,
                                         uint64_t placement_key, int option);
int  rc_player_interact_inventory_item(RcWorld *world, int inv_slot,
                                       int option);
int  rc_player_interact_equipment_item(RcWorld *world, int equip_slot,
                                       int option);
int  rc_player_widget_action(RcWorld *world, int widget_id,
                             int component_id, int action);
int  rc_player_use_inventory_item_on_npc(RcWorld *world, int inv_slot,
                                         int npc_uid);
int  rc_player_use_inventory_item_on_inventory_item(RcWorld *world,
                                                    int source_slot,
                                                    int target_slot);
int  rc_player_use_inventory_item_on_object(RcWorld *world, int inv_slot,
                                            int obj_id, int x, int y,
                                            int plane);
int  rc_player_use_inventory_item_on_object_placement(
    RcWorld *world, int inv_slot, int obj_id, int x, int y, int plane,
    uint64_t placement_key);
int  rc_player_use_inventory_item_on_ground_item(RcWorld *world,
                                                 int inv_slot,
                                                 int ground_item_idx);
int  rc_player_use_inventory_item_on_widget(RcWorld *world, int inv_slot,
                                            int widget_id, int component_id);
int  rc_player_cast_spell_on_npc(RcWorld *world, int spell_id, int npc_uid);
int  rc_player_cast_spell_on_inventory_item(RcWorld *world, int spell_id,
                                            int target_slot);
int  rc_player_cast_spell_on_object(RcWorld *world, int spell_id,
                                    int obj_id, int x, int y, int plane);
int  rc_player_cast_spell_on_object_placement(
    RcWorld *world, int spell_id, int obj_id, int x, int y, int plane,
    uint64_t placement_key);
int  rc_player_cast_spell_on_ground_item(RcWorld *world, int spell_id,
                                         int ground_item_idx);
int  rc_player_cast_spell_on_widget(RcWorld *world, int spell_id,
                                    int widget_id, int component_id);
int  rc_world_object_active_id(const RcWorld *world, int obj_id, int x, int y,
                               int plane);
int  rc_world_object_active_state(const RcWorld *world, int obj_id, int x,
                                  int y, int plane, RcObjectState *out);
int  rc_world_object_active_state_by_key(const RcWorld *world,
                                         uint64_t placement_key,
                                         RcObjectState *out);
int  rc_player_open_storage_object(RcWorld *world, int obj_id, int option);
int  rc_player_open_storage_npc(RcWorld *world, int npc_uid, int option);
int  rc_player_close_storage(RcWorld *world);
int  rc_bank_add_item(RcWorld *world, int item_id, int quantity);
int  rc_bank_add_item_tab(RcWorld *world, int item_id, int quantity, int tab);
int  rc_bank_deposit_slot(RcWorld *world, int inv_slot, int quantity);
int  rc_bank_withdraw_slot(RcWorld *world, int bank_slot, int quantity);
int  rc_player_apply_traversal(RcWorld *world, const RcTraversalEdge *edge);
int  rc_player_apply_recipe(RcWorld *world, const RcRecipe *recipe);
void rc_player_drop_item(RcWorld *world, int inv_slot);
void rc_player_pickup_item(RcWorld *world, int ground_item_idx);

// State read (frontend reads, never writes)
const RcPlayer *rc_get_player(const RcWorld *world);
const RcNpc    *rc_get_npcs(const RcWorld *world, int *count);

#endif
