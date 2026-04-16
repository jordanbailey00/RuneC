#include "api.h"
#include "rng.h"
#include "skills.h"
#include <stdlib.h>
#include <string.h>

RcWorld *rc_world_create(uint32_t seed) {
    RcWorld *world = calloc(1, sizeof(RcWorld));
    if (!world) return NULL;

    world->rng_state = seed;
    world->tick = 0;

    // Initialize player at Varrock center
    world->player.x = 3213;
    world->player.y = 3428;
    world->player.plane = 0;
    world->player.prev_x = world->player.x;
    world->player.prev_y = world->player.y;
    world->player.attack_target = -1;
    world->player.run_energy = 10000;
    world->player.auto_retaliate = true;

    // Default stats: level 1 everything, 10 HP
    for (int i = 0; i < SKILL_COUNT; i++) {
        world->player.skills.base_level[i] = 1;
        world->player.skills.boosted_level[i] = 1;
        world->player.skills.xp[i] = 0;
    }
    world->player.skills.base_level[SKILL_HITPOINTS] = 10;
    world->player.skills.boosted_level[SKILL_HITPOINTS] = 10;
    world->player.skills.xp[SKILL_HITPOINTS] = 1154;  // XP for level 10
    world->player.current_hp = 100;   // 10.0 HP in tenths
    world->player.max_hp = 100;

    // Empty inventory
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        world->player.inventory[i].item_id = -1;
    }
    for (int i = 0; i < RC_EQUIP_COUNT; i++) {
        world->player.equipment[i].item_id = -1;
    }

    return world;
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
