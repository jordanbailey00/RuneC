#include "../content.h"
#include "../../rc-core/encounter.h"
#include "../../rc-core/npc.h"
#include "../../rc-core/types.h"

// The Leviathan — DT2 boss. Current content script only owns
// encounter-local enrage setup; mechanics remain generic primitives.

static void leviathan_enter_enrage(struct RcWorld *world, int enc_idx) {
    if (!world || enc_idx < 0 || enc_idx >= RC_ENC_MAX_ACTIVE) return;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    a->attack_count = 0;
    a->attack_special_toggle = 0;
    RcNpc *npc = rc_npc_resolve(world, a->boss_id);
    if (npc) npc->attack_timer = 0;
}

int rc_content_leviathan_register(struct RcWorld *world) {
    int ok = 1;
    ok &= rc_encounter_register_script(world, "leviathan_enter_enrage",
                                 leviathan_enter_enrage) >= 0;
    return ok;
}
