#include "../content.h"
#include "../../rc-core/encounter.h"
#include "../../rc-core/types.h"

// Phantom Muspah — DT2 boss.
//
// Current TOML wiring references phase entry scripts for form/special
// transitions:
//   - muspah_first_special
//   - muspah_form_swap
//   - muspah_second_special
//   - muspah_shield_up

static void muspah_first_special(struct RcWorld *world, int enc_idx) {
    if (!world || enc_idx < 0 || enc_idx >= RC_ENC_MAX_ACTIVE) return;
    world->encounter.active[enc_idx].script_flags |= 1u;
}

static void muspah_form_swap(struct RcWorld *world, int enc_idx) {
    if (!world || enc_idx < 0 || enc_idx >= RC_ENC_MAX_ACTIVE) return;
    world->encounter.active[enc_idx].attack_special_toggle++;
}

static void muspah_second_special(struct RcWorld *world, int enc_idx) {
    if (!world || enc_idx < 0 || enc_idx >= RC_ENC_MAX_ACTIVE) return;
    world->encounter.active[enc_idx].script_flags |= 2u;
}

static void muspah_shield_up(struct RcWorld *world, int enc_idx) {
    if (!world || enc_idx < 0 || enc_idx >= RC_ENC_MAX_ACTIVE) return;
    world->encounter.active[enc_idx].mechanic_progress = 0;
}

void rc_content_phantom_muspah_register(struct RcWorld *world) {
    rc_encounter_register_script(world, "muspah_first_special",
                                 muspah_first_special);
    rc_encounter_register_script(world, "muspah_form_swap", muspah_form_swap);
    rc_encounter_register_script(world, "muspah_second_special",
                                 muspah_second_special);
    rc_encounter_register_script(world, "muspah_shield_up", muspah_shield_up);
}
