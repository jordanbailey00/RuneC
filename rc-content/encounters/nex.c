#include "../content.h"
#include "../../rc-core/encounter.h"
#include "../../rc-core/npc.h"
#include "../../rc-core/types.h"
#include <string.h>

static RcNpc *nex_boss(struct RcWorld *world, int enc_idx) {
    if (!world || enc_idx < 0 || enc_idx >= RC_ENC_MAX_ACTIVE) return 0;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    if (!a->active) return 0;
    for (int i = 0; i < world->npc_count; i++) {
        RcNpc *npc = &world->npcs[i];
        if (npc->active && npc->uid == a->boss_id) return npc;
    }
    return 0;
}

static int nex_def_idx(const char *name) {
    for (int i = 0; i < g_npc_def_count; i++) {
        if (strcmp(g_npc_defs[i].name, name) == 0) return i;
    }
    return -1;
}

static void nex_spawn_minion(struct RcWorld *world, RcNpc *boss,
                             const char *name, int dx, int dy) {
    int def_idx = nex_def_idx(name);
    if (!boss || def_idx < 0) return;
    rc_npc_spawn(world, def_idx, boss->x + dx, boss->y + dy, boss->plane);
}

static void nex_enter_phase(struct RcWorld *world, int enc_idx,
                            const char *phase_name, const char *minion,
                            int dx, int dy) {
    RcNpc *boss = nex_boss(world, enc_idx);
    if (!boss) return;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    a->attack_count = 0;
    a->attack_special_toggle = 0;
    a->active_mechanic_idx = 0xFFu;
    a->active_mechanic_ticks = 0;
    boss->target_uid = 0;
    rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                            boss->x, boss->y, boss->plane,
                            world->player.x, world->player.y,
                            4, (uint16_t)boss->uid, COMBAT_NONE, 0,
                            phase_name, "");
    if (minion && minion[0]) nex_spawn_minion(world, boss, minion, dx, dy);
}

static void nex_smoke_phase(struct RcWorld *world, int enc_idx) {
    nex_enter_phase(world, enc_idx, "Nex Smoke Phase", "Fumus", -6, 6);
}

static void nex_shadow_phase(struct RcWorld *world, int enc_idx) {
    nex_enter_phase(world, enc_idx, "Nex Shadow Phase", "Umbra", 6, 6);
}

static void nex_blood_phase(struct RcWorld *world, int enc_idx) {
    nex_enter_phase(world, enc_idx, "Nex Blood Phase", "Cruor", -6, -6);
}

static void nex_ice_phase(struct RcWorld *world, int enc_idx) {
    nex_enter_phase(world, enc_idx, "Nex Ice Phase", "Glacies", 6, -6);
}

static void nex_zaros_phase(struct RcWorld *world, int enc_idx) {
    nex_enter_phase(world, enc_idx, "Nex Zaros Phase", "", 0, 0);
    RcNpc *boss = nex_boss(world, enc_idx);
    if (!boss || boss->def_id < 0 || boss->def_id >= g_npc_def_count) return;
    boss->current_hp += 500;
    if (boss->current_hp > g_npc_defs[boss->def_id].hitpoints) {
        boss->current_hp = g_npc_defs[boss->def_id].hitpoints;
    }
}

void rc_content_nex_register(struct RcWorld *world) {
    rc_encounter_register_script(world, "nex_smoke_phase", nex_smoke_phase);
    rc_encounter_register_script(world, "nex_shadow_phase", nex_shadow_phase);
    rc_encounter_register_script(world, "nex_blood_phase", nex_blood_phase);
    rc_encounter_register_script(world, "nex_ice_phase", nex_ice_phase);
    rc_encounter_register_script(world, "nex_zaros_phase", nex_zaros_phase);
}
