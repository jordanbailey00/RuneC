#include "api.h"
#include "combat.h"
#include "combat_visuals.h"
#include "area_flags.h"
#include "activity_mechanics.h"
#include "activity_schemas.h"
#include "activity_spawns.h"
#include "activity_states.h"
#include "rng.h"
#include "skills.h"
#include "config.h"
#include "collision.h"
#include "dialogue.h"
#include "drops.h"
#include "events.h"
#include "encounter.h"
#include "items.h"
#include "interaction.h"
#include "monster_mechanics.h"
#include "npc.h"
#include "normalization.h"
#include "objects.h"
#include "player_actions.h"
#include "prayer.h"
#include "quests.h"
#include "slayer.h"
#include "spells.h"
#include "shops.h"
#include "traversal.h"
#include "varbits.h"
#include <stdlib.h>
#include <string.h>

static void init_player_defaults(RcPlayer *p) {
    // Varrock centre start for legacy-seeded worlds. RL configs
    // typically override player position via their own init step.
    p->x = 3213;
    p->y = 3428;
    p->plane = 0;
    p->prev_x = p->x;
    p->prev_y = p->y;
    p->attack_target = -1;
    p->attack_target_def_id = -1;
    p->interact_type = RC_INTERACT_NONE;
    p->interact_target = -1;
    p->interact_option = -1;
    p->storage_target = -1;
    p->storage_option = -1;
    p->skill_target_x = -1;
    p->skill_target_y = -1;
    p->action_anim_id = -1;
    p->pending_traversal_x = -1;
    p->pending_traversal_y = -1;
    p->pending_traversal_plane = -1;
    rc_interaction_clear(p);
    p->combat_style = COMBAT_MELEE_CRUSH;
    p->attack_style_idx = 0;
    p->attack_stance = RC_ATTACK_STANCE_ACCURATE;
    p->combat_xp_mask = RC_COMBAT_XP_ATTACK;
    p->special_energy = 10000;
    p->selected_spell = -1;
    p->manual_spell_cast = -1;
    p->last_hit = -1;
    p->facing_entity = -1;
    p->facing_x = -1;
    p->facing_y = -1;
    p->slayer_master_idx = -1;
    p->slayer_task_idx = -1;
    p->run_energy = 10000;
    p->auto_retaliate = true;
    rc_combat_init_player_state(p);

    // Default stats: level 1 everything, 10 HP.
    for (int i = 0; i < SKILL_COUNT; i++) {
        p->skills.base_level[i] = 1;
        p->skills.boosted_level[i] = 1;
        p->skills.xp[i] = 0;
    }
    p->skills.base_level[SKILL_HITPOINTS] = 10;
    p->skills.boosted_level[SKILL_HITPOINTS] = 10;
    p->skills.xp[SKILL_HITPOINTS] = 1154;   // XP for level 10
    p->current_hp = 100;                    // tenths-of-HP
    p->max_hp = 100;

    // Empty inventory + equipment.
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        p->inventory[i].item_id = -1;
    }
    for (int i = 0; i < RC_BANK_SIZE; i++) {
        p->bank[i].item_id = -1;
    }
    for (int i = 0; i < RC_EQUIP_COUNT; i++) {
        p->equipment[i].item_id = -1;
    }
}

RcWorld *rc_world_create_config(const RcWorldConfig *cfg) {
    if (!cfg) return NULL;

    // calloc is fine here — world_create is not on the tick path
    // (see rc-core/README.md §10).
    RcWorld *world = calloc(1, sizeof(RcWorld));
    if (!world) return NULL;

    world->rng_state = cfg->seed;
    world->tick = 0;
    world->enabled = cfg->subsystems;

    rc_events_init(&world->events);
    if (cfg->varbits_path && g_rc_varbit_count == 0) {
        rc_load_varbits(cfg->varbits_path);
    }
    if (cfg->varps_path && g_rc_varp_count == 0) {
        rc_load_varps(cfg->varps_path);
    }
    uint32_t npc_users = RC_SUB_COMBAT | RC_SUB_DIALOGUE | RC_SUB_SHOPS
                       | RC_SUB_SLAYER | RC_SUB_ENCOUNTER;
    if ((cfg->subsystems & npc_users) && cfg->npc_defs_path
            && g_npc_def_count == 0) {
        rc_load_npc_defs(cfg->npc_defs_path);
    }
    uint32_t item_users = RC_SUB_EQUIPMENT | RC_SUB_INVENTORY
                        | RC_SUB_CONSUMABLES | RC_SUB_LOOT
                        | RC_SUB_SKILLS | RC_SUB_SHOPS | RC_SUB_STORAGE;
    if ((cfg->subsystems & item_users) && cfg->items_path
            && g_item_def_count == 0) {
        rc_load_item_defs(cfg->items_path);
    }
    uint32_t normalization_users = npc_users | item_users | RC_SUB_LOOT
                                 | RC_SUB_SHOPS | RC_SUB_STORAGE;
    if ((cfg->subsystems & normalization_users) && cfg->normalization_path
            && g_rc_item_normalization_count == 0) {
        rc_load_normalization(cfg->normalization_path);
    }
    if ((cfg->subsystems & RC_SUB_LOOT) && cfg->drops_path
            && g_rc_drop_table_count == 0) {
        rc_load_drops(cfg->drops_path);
    }
    if ((cfg->subsystems & RC_SUB_LOOT) && cfg->rdt_path
            && g_rc_rdt_entry_count == 0) {
        rc_load_rdt(cfg->rdt_path);
    }
    if ((cfg->subsystems & RC_SUB_LOOT) && cfg->gdt_path
            && g_rc_gdt_entry_count == 0) {
        rc_load_gdt(cfg->gdt_path);
    }
    if ((cfg->subsystems & RC_SUB_LOOT) && cfg->mrdt_path
            && g_rc_mrdt_entry_count == 0) {
        rc_load_mrdt(cfg->mrdt_path);
    }
    if ((cfg->subsystems & (RC_SUB_LOOT | RC_SUB_SKILLS))
            && cfg->skill_drops_path && g_rc_skill_drop_source_count == 0) {
        rc_load_skill_drops(cfg->skill_drops_path);
    }
    if (cfg->player_actions_path && g_rc_player_action_count == 0) {
        rc_load_player_actions(cfg->player_actions_path);
    }
    if ((cfg->subsystems & RC_SUB_PRAYER) && cfg->prayers_path
            && g_rc_prayer_count == 0) {
        rc_load_prayers(cfg->prayers_path);
    }
    if ((cfg->subsystems & RC_SUB_COMBAT) && cfg->spells_path
            && g_rc_spell_count == 0) {
        rc_load_spells(cfg->spells_path);
    }
    if ((cfg->subsystems & RC_SUB_COMBAT) && cfg->combat_visuals_path
            && g_rc_combat_visual_count == 0) {
        rc_load_combat_visuals(cfg->combat_visuals_path);
    }
    if (cfg->subsystems & RC_SUB_SKILLS) {
        if (cfg->recipes_path && g_rc_recipe_count == 0) {
            rc_load_recipes(cfg->recipes_path);
        }
        if (cfg->gathering_nodes_path && g_rc_gathering_node_count == 0) {
            rc_load_gathering_nodes(cfg->gathering_nodes_path);
        }
    }
    if (cfg->subsystems & RC_SUB_OBJECTS) {
        if (cfg->object_defs_path && g_rc_object_def_count == 0) {
            rc_load_object_defs(cfg->object_defs_path);
        }
        if (cfg->object_behaviors_path && g_rc_object_behavior_count == 0) {
            rc_load_object_behaviors(cfg->object_behaviors_path);
        }
        if (cfg->object_placements_path && g_rc_object_placement_count == 0) {
            rc_load_object_placements(cfg->object_placements_path);
        }
        if (cfg->object_transports_path && g_rc_object_transport_count == 0) {
            rc_load_object_transports(cfg->object_transports_path);
        }
    }
    if ((cfg->subsystems & RC_SUB_REGIONS) && cfg->collision_tiles_path
            && g_rc_collision_region_count == 0) {
        rc_load_collision_tiles(cfg->collision_tiles_path);
    }
    if ((cfg->subsystems & RC_SUB_REGIONS) && cfg->area_flags_path
            && g_rc_area_flag_count == 0) {
        rc_load_area_flags(cfg->area_flags_path);
    }
    if ((cfg->subsystems & RC_SUB_TRAVERSAL) && cfg->traversal_edges_path
            && g_rc_traversal_edge_count == 0) {
        rc_load_traversal_edges(cfg->traversal_edges_path);
    }
    uint32_t mechanics_users = RC_SUB_COMBAT | RC_SUB_SLAYER
                             | RC_SUB_ENCOUNTER;
    if ((cfg->subsystems & mechanics_users) && cfg->monster_mechanics_path
            && g_rc_monster_mechanic_family_count == 0) {
        rc_load_monster_mechanics(cfg->monster_mechanics_path);
    }
    if ((cfg->subsystems & RC_SUB_SLAYER) && cfg->slayer_path
            && g_rc_slayer_master_count == 0) {
        rc_load_slayer(cfg->slayer_path);
    }
    if ((cfg->subsystems & RC_SUB_SHOPS) && cfg->shops_path
            && g_shop_count == 0) {
        rc_load_shops(cfg->shops_path);
    }
    if ((cfg->subsystems & RC_SUB_QUESTS) && cfg->quests_path
            && g_rc_quest_count == 0) {
        rc_load_quests(cfg->quests_path);
    }
    if ((cfg->subsystems & RC_SUB_DIALOGUE) && cfg->dialogue_path
            && g_rc_dialogue_transcript_count == 0) {
        rc_load_dialogue(cfg->dialogue_path);
    }
    if (cfg->subsystems & RC_SUB_SLAYER) {
        rc_slayer_init(world);
    }
    // Only enabled subsystems subscribe to the event bus — keeps
    // disabled-subsystem worlds event-free (README §7 + §8).
    if (cfg->subsystems & RC_SUB_ENCOUNTER) {
        if (cfg->activity_schemas_path
                && g_rc_activity_schema_count == 0) {
            rc_load_activity_schemas(cfg->activity_schemas_path);
        }
        if (cfg->activity_spawns_path
                && g_rc_activity_spawn_count == 0) {
            rc_load_activity_spawns(cfg->activity_spawns_path);
        }
        if (cfg->activity_mechanics_path
                && g_rc_activity_mechanic_count == 0) {
            rc_load_activity_mechanics(cfg->activity_mechanics_path);
        }
        if (cfg->activity_states_path
                && g_rc_activity_state_count == 0) {
            rc_load_activity_states(cfg->activity_states_path);
        }
        rc_encounter_init(world);
        if (cfg->encounters_path) {
            rc_encounter_load(world, cfg->encounters_path);
        }
    }
    init_player_defaults(&world->player);
    // Full world spawns stay explicit: callers load a world slice with
    // rc_load_npc_spawns_rect/near after choosing the active area.

    return world;
}

// Legacy create — used by viewer + tests that haven't been updated
// to pass a full config. Defaults to the full-game preset.
RcWorld *rc_world_create(uint32_t seed) {
    RcWorldConfig cfg = rc_preset_full_game();
    cfg.seed = seed;
    return rc_world_create_config(&cfg);
}

void rc_world_destroy(RcWorld *world) {
    free(world);
}

const RcPlayer *rc_get_player(const RcWorld *world) {
    return &world->player;
}

const RcNpc *rc_get_npcs(const RcWorld *world, int *count) {
    if (count) *count = world->npc_count;
    return world->npcs;
}
