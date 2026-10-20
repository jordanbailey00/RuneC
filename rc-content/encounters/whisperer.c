#include "../content.h"
#include "../../rc-core/encounter.h"
#include "../../rc-core/npc.h"
#include "../../rc-core/types.h"

// The Whisperer — DT2 boss. The script handles the authored enrage
// heal/transport state; sanity and arena pressure are generic.

static void whisperer_enter_shadow_realm(struct RcWorld *world, int enc_idx) {
    if (!world || enc_idx < 0 || enc_idx >= RC_ENC_MAX_ACTIVE) return;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    a->attack_count = 0;
    a->attack_special_toggle = 0;
    if (a->mechanic_progress == 0) a->mechanic_progress = 100;
    RcNpc *npc = rc_npc_resolve(world, a->boss_id);
    if (npc) {
        npc->current_hp += 140;
        const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
        if (def) {
            int cap = def->hitpoints;
            if (npc->current_hp > cap) npc->current_hp = cap;
        }
        world->player.prev_x = world->player.x;
        world->player.prev_y = world->player.y;
        world->player.x = npc->x + 6;
        world->player.y = npc->y;
        world->player.plane = npc->plane;
    }
}

int rc_content_whisperer_register(struct RcWorld *world) {
    int ok = 1;
    ok &= rc_encounter_register_script(world, "whisperer_enter_shadow_realm",
                                 whisperer_enter_shadow_realm) >= 0;
    return ok;
}
