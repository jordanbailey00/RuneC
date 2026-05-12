#include "../content.h"
#include "../../rc-core/encounter.h"
#include "../../rc-core/npc.h"
#include "../../rc-core/types.h"

static RcNpc *raid_boss(struct RcWorld *world, int enc_idx) {
    if (!world || enc_idx < 0 || enc_idx >= RC_ENC_MAX_ACTIVE) return 0;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    if (!a->active) return 0;
    for (int i = 0; i < world->npc_count; i++) {
        RcNpc *npc = &world->npcs[i];
        if (npc->active && npc->uid == a->boss_id) return npc;
    }
    return 0;
}

static void raid_enter_phase(struct RcWorld *world, int enc_idx,
                             const char *name, uint8_t style) {
    RcNpc *boss = raid_boss(world, enc_idx);
    if (!boss) return;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    a->attack_count = 0;
    a->attack_special_toggle = 0;
    a->active_mechanic_idx = 0xFFu;
    a->active_mechanic_ticks = 0;
    boss->target_uid = 0;
    boss->player_untargetable = false;
    rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                            boss->x, boss->y, boss->plane,
                            world->player.x, world->player.y,
                            5, (uint16_t)boss->uid, style, 0, name, "");
}

static void olm_phase_1(struct RcWorld *world, int enc_idx) {
    raid_enter_phase(world, enc_idx, "Olm Acid Phase", COMBAT_RANGED);
}

static void olm_phase_2(struct RcWorld *world, int enc_idx) {
    raid_enter_phase(world, enc_idx, "Olm Crystal Phase", COMBAT_MAGIC);
}

static void olm_phase_3(struct RcWorld *world, int enc_idx) {
    raid_enter_phase(world, enc_idx, "Olm Final Phase", COMBAT_NONE);
}

static void verzik_phase_1(struct RcWorld *world, int enc_idx) {
    raid_enter_phase(world, enc_idx, "Verzik Phase 1", COMBAT_MAGIC);
}

static void verzik_phase_2(struct RcWorld *world, int enc_idx) {
    raid_enter_phase(world, enc_idx, "Verzik Phase 2", COMBAT_MAGIC);
}

static void verzik_phase_3(struct RcWorld *world, int enc_idx) {
    raid_enter_phase(world, enc_idx, "Verzik Phase 3", COMBAT_NONE);
}

static void wardens_phase_1(struct RcWorld *world, int enc_idx) {
    raid_enter_phase(world, enc_idx, "Wardens Phase 1", COMBAT_NONE);
}

static void wardens_phase_2(struct RcWorld *world, int enc_idx) {
    raid_enter_phase(world, enc_idx, "Wardens Phase 2", COMBAT_MAGIC);
}

static void wardens_phase_3(struct RcWorld *world, int enc_idx) {
    raid_enter_phase(world, enc_idx, "Wardens Phase 3", COMBAT_NONE);
}

void rc_content_raids_register(struct RcWorld *world) {
    rc_encounter_register_script(world, "olm_phase_1", olm_phase_1);
    rc_encounter_register_script(world, "olm_phase_2", olm_phase_2);
    rc_encounter_register_script(world, "olm_phase_3", olm_phase_3);
    rc_encounter_register_script(world, "verzik_phase_1", verzik_phase_1);
    rc_encounter_register_script(world, "verzik_phase_2", verzik_phase_2);
    rc_encounter_register_script(world, "verzik_phase_3", verzik_phase_3);
    rc_encounter_register_script(world, "wardens_phase_1", wardens_phase_1);
    rc_encounter_register_script(world, "wardens_phase_2", wardens_phase_2);
    rc_encounter_register_script(world, "wardens_phase_3", wardens_phase_3);
}
