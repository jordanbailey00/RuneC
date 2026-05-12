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
    for (int i = 0; i < world->npc_count; i++) {
        RcNpc *npc = &world->npcs[i];
        if (npc->active && npc->uid == a->boss_id) {
            npc->attack_timer = 0;
            break;
        }
    }
}

void rc_content_leviathan_register(struct RcWorld *world) {
    rc_encounter_register_script(world, "leviathan_enter_enrage",
                                 leviathan_enter_enrage);
}
