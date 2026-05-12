#include "../content.h"
#include "../../rc-core/encounter.h"
#include "../../rc-core/types.h"

// Alchemical Hydra — Karuulm lair boss (cache NPC ids 8615+).
//
// This encounter is mostly data-driven. The following scripts are
// phase-entry transitions that are still required for TOML wiring:
//   - hydra_serpentine_entry
//   - hydra_head_drop
//   - hydra_enrage_entry
//
// Runtime support for the Hydra primitive set is currently staged;
// until that work lands, these scripts are intentionally minimal so
// the encounter can still be registered and launched.

static void hydra_serpentine_entry(struct RcWorld *world, int enc_idx) {
    if (!world || enc_idx < 0 || enc_idx >= RC_ENC_MAX_ACTIVE) return;
    world->encounter.active[enc_idx].attack_count = 0;
    world->encounter.active[enc_idx].attack_special_toggle = 0;
}

static void hydra_head_drop(struct RcWorld *world, int enc_idx) {
    if (!world || enc_idx < 0 || enc_idx >= RC_ENC_MAX_ACTIVE) return;
    world->encounter.active[enc_idx].attack_count = 0;
    world->encounter.active[enc_idx].attack_special_toggle = 0;
}

static void hydra_enrage_entry(struct RcWorld *world, int enc_idx) {
    if (!world || enc_idx < 0 || enc_idx >= RC_ENC_MAX_ACTIVE) return;
    world->encounter.active[enc_idx].attack_count = 0;
    world->encounter.active[enc_idx].active_mechanic_idx = 0xFFu;
    world->encounter.active[enc_idx].active_mechanic_ticks = 0;
}

void rc_content_alchemical_hydra_register(struct RcWorld *world) {
    rc_encounter_register_script(world, "hydra_serpentine_entry",
                                 hydra_serpentine_entry);
    rc_encounter_register_script(world, "hydra_head_drop", hydra_head_drop);
    rc_encounter_register_script(world, "hydra_enrage_entry",
                                 hydra_enrage_entry);
}
