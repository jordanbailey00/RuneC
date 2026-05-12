#include "../content.h"
#include "../../rc-core/encounter.h"
#include "../../rc-core/npc.h"
#include "../../rc-core/types.h"

static void gauntlet_prep_phase(struct RcWorld *world, int enc_idx) {
    if (!world || enc_idx < 0 || enc_idx >= RC_ENC_MAX_ACTIVE) return;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    if (!a->active) return;
    a->attack_count = 0;
    a->attack_special_toggle = 0;
    a->mechanic_progress = 0;
    rc_encounter_set_phase(world, enc_idx, "hunllef_magic");
}

static void hunllef_engage(struct RcWorld *world, int enc_idx) {
    if (!world || enc_idx < 0 || enc_idx >= RC_ENC_MAX_ACTIVE) return;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    if (!a->active) return;
    a->attack_count = 0;
    a->active_mechanic_idx = 0xFFu;
    a->active_mechanic_ticks = 0;
    for (int i = 0; i < world->npc_count; i++) {
        RcNpc *npc = &world->npcs[i];
        if (npc->active && npc->uid == a->boss_id) {
            npc->player_untargetable = false;
            npc->attack_timer = 2;
            npc->target_uid = 0;
            break;
        }
    }
}

void rc_content_gauntlet_register(struct RcWorld *world) {
    rc_encounter_register_script(world, "gauntlet_prep_phase",
                                 gauntlet_prep_phase);
    rc_encounter_register_script(world, "hunllef_engage",
                                 hunllef_engage);
}
