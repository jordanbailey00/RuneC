#include "../content.h"

// Scurrius — OSRS solo/multi rat boss (cache NPC id 7221/7222).
//
// MOST of Scurrius runs on generic primitives in rc-core and needs
// no code here. The TOML at `data/curated/encounters/scurrius.toml`
// references:
//   - telegraphed_aoe_tile  (Falling Bricks)      — generic
//   - spawn_npcs            (Giant Rat minions)   — generic
//   - heal_at_object        (Food Pile heal)      — generic
//
// This file exists to hold the *boss-specific* scripts listed in
// the TOML's `script = "..."` fields. Per work.md §1.1 pass 2
// roster:
//
//   - scurrius_heal_at_food_pile   (phase=heal walk-to-pile logic)
//   - scurrius_center_rage         (phase=enraged center-of-arena state)
//
// Neither is implemented yet. When they land, each becomes a
// static function in this file and is registered with the encounter
// script registry (API to be added as pass-2 event wiring extends to
// phase-triggered mechanics).
//
// Reference: no pre-2013 counterpart (Scurrius is OSRS-only, 2024).
// Wiki prose + Scurrius/Strategies page are the only sources.

void rc_content_scurrius_register(struct RcWorld *world) {
    (void)world;
    // No boss-specific scripts registered yet — the generic primitive
    // registry covers all 3 current TOML mechanics. This fn exists
    // for symmetry + to hold future script registrations.
}
