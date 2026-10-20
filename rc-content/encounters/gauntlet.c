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
    RcNpc *npc = rc_npc_resolve(world, a->boss_id);
    if (npc) {
        npc->player_untargetable = false;
        npc->attack_timer = 2;
        npc->target_uid = 0;
    }
}

int rc_content_gauntlet_register(struct RcWorld *world) {
    int ok = 1;
    ok &= rc_encounter_register_script(world, "gauntlet_prep_phase",
                                 gauntlet_prep_phase) >= 0;
    ok &= rc_encounter_register_script(world, "hunllef_engage",
                                 hunllef_engage) >= 0;
    return ok;
}
