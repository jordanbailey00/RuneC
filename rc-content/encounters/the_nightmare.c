#include "../content.h"
#include "../../rc-core/encounter.h"
#include "../../rc-core/types.h"

// The Nightmare — multi-phase multi-player boss.
//
// Encounters.toml currently wires all combat mechanics as generic
// primitives (totems, husks, spores, surge, etc.) and uses phase
// entry scripts as data-only routing hooks:
//   - nightmare_phase_1
//   - nightmare_phase_2
//   - nightmare_phase_3
//
// These phase-entry scripts are stubbed here to keep encounter bootstrapping
// intact while generic runtime coverage for these mechanics lands.

static void nightmare_phase_1(struct RcWorld *world, int enc_idx) {
    if (!world || enc_idx < 0 || enc_idx >= RC_ENC_MAX_ACTIVE) return;
    world->encounter.active[enc_idx].mechanic_progress = 0;
    world->encounter.active[enc_idx].shield_points = 0;
}

static void nightmare_phase_2(struct RcWorld *world, int enc_idx) {
    if (!world || enc_idx < 0 || enc_idx >= RC_ENC_MAX_ACTIVE) return;
    world->encounter.active[enc_idx].mechanic_progress = 0;
    world->encounter.active[enc_idx].shield_points = 0;
}

static void nightmare_phase_3(struct RcWorld *world, int enc_idx) {
    if (!world || enc_idx < 0 || enc_idx >= RC_ENC_MAX_ACTIVE) return;
    world->encounter.active[enc_idx].mechanic_progress = 0;
    world->encounter.active[enc_idx].shield_points = 0;
}

void rc_content_the_nightmare_register(struct RcWorld *world) {
    rc_encounter_register_script(world, "nightmare_phase_1", nightmare_phase_1);
    rc_encounter_register_script(world, "nightmare_phase_2", nightmare_phase_2);
    rc_encounter_register_script(world, "nightmare_phase_3", nightmare_phase_3);
}
