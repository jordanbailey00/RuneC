#include "../content.h"

// Kalphite Queen — two-phase desert boss (cache NPC ids 965 / 4304).
//
// MOST of KQ runs on generic primitives in rc-core. The TOML at
// `content/encounters/kalphite_queen.toml` references:
//   - drain_prayer_on_hit                     — generic
//   - chain_magic_to_nearest_player           — generic (solo no-op)
//   - preserve_stat_drains_across_transition  — generic
//
// This file exists to hold the *boss-specific* scripts listed in
// the TOML's `script = "..."` fields. Per docs/work.md §1.1 pass 2
// roster:
//
//   - kq_shed_exoskeleton
//       The 20-tick grounded→airborne transition animation. Queen
//       is untargetable during this window; hp transfers to phase 2.
//       Needs to mutate boss def_id (965 → 4304) + reset HP to the
//       airborne form's max + flip the `untargetable` flag on
//       RcActiveEncounter.
//
// Not implemented yet. The name is currently routed by
// encounters/scripts.c as a no-op stub; real behavior belongs here
// when authored.
//
// Reference: use approved OSRS-native sources only. `rsmod` coverage and
// OSRS Wiki KQ pages can guide reconstruction where they directly cover the
// OSRS behavior. Uncovered timing, damage, prayer-drain, or transition details
// should remain source-gap rows until backed by approved evidence.

void rc_content_kalphite_queen_register(struct RcWorld *world) {
    (void)world;
    // Generic primitives and shared script stubs cover the current TOML.
}
