#include "../content.h"
#include "../../rc-core/encounter.h"
#include "../../rc-core/types.h"

// Duke Sucellus — DT2-era slayer boss.
//
// This encounter currently depends on phase hooks for wake/transition
// control and continues to rely on generic mechanics for combat behavior.

static void duke_sleeping_phase(struct RcWorld *world, int enc_idx) {
    if (!world || enc_idx < 0 || enc_idx >= RC_ENC_MAX_ACTIVE) return;
    world->encounter.active[enc_idx].mechanic_progress = 0;
}

static void duke_awakening_animation(struct RcWorld *world, int enc_idx) {
    rc_encounter_set_phase(world, enc_idx, "battle");
}

void rc_content_duke_sucellus_register(struct RcWorld *world) {
    rc_encounter_register_script(world, "duke_sleeping_phase",
                                 duke_sleeping_phase);
    rc_encounter_register_script(world, "duke_awakening_animation",
                                 duke_awakening_animation);
}
