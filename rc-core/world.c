#include "api.h"
#include "rng.h"
#include "skills.h"
#include "config.h"
#include "events.h"
#include "encounter.h"
#include "npc.h"
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
    p->run_energy = 10000;
    p->auto_retaliate = true;

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
    uint32_t npc_users = RC_SUB_COMBAT | RC_SUB_DIALOGUE | RC_SUB_SHOPS
                       | RC_SUB_SLAYER | RC_SUB_ENCOUNTER;
    if ((cfg->subsystems & npc_users) && cfg->npc_defs_path
            && g_npc_def_count == 0) {
        rc_load_npc_defs(cfg->npc_defs_path);
    }
    // Only enabled subsystems subscribe to the event bus — keeps
    // disabled-subsystem worlds event-free (README §7 + §8).
    if (cfg->subsystems & RC_SUB_ENCOUNTER) {
        rc_encounter_init(world);
        if (cfg->encounters_path) {
            rc_encounter_load(world, cfg->encounters_path);
        }
    }
    init_player_defaults(&world->player);
    // TODO(todo#2-pass2): wire the rest (loot, quests, dialogue, etc.)
    // as subsystem init fns land.

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
