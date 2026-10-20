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

int rc_content_the_nightmare_register(struct RcWorld *world) {
    int ok = 1;
    ok &= rc_encounter_register_script(world, "nightmare_phase_1", nightmare_phase_1) >= 0;
    ok &= rc_encounter_register_script(world, "nightmare_phase_2", nightmare_phase_2) >= 0;
    ok &= rc_encounter_register_script(world, "nightmare_phase_3", nightmare_phase_3) >= 0;
    return ok;
}
