#include "../content.h"

// Kalphite Queen — two-phase desert boss (cache NPC ids 965 / 4304).
//
// MOST of KQ runs on generic primitives in rc-core. The TOML at
// `data/curated/encounters/kalphite_queen.toml` references:
//   - drain_prayer_on_hit                     — generic
//   - chain_magic_to_nearest_player           — generic (solo no-op)
//   - preserve_stat_drains_across_transition  — generic
//
// This file exists to hold the *boss-specific* scripts listed in
// the TOML's `script = "..."` fields. Per work.md §1.1 pass 2
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
// Reference: pre-2013 RuneScape had KQ with nearly identical
// mechanics. See:
//   - rsmod (search "kalphite")
//   - void_rsps (search "KalphiteQueen")
//   - 2011Scape-game (overlap source)
// These are legitimate references for KQ since the mechanics carry
// over — use them to validate attack rotation timing, max hits,
// prayer drain amounts, and transition animation duration.

void rc_content_kalphite_queen_register(struct RcWorld *world) {
    (void)world;
    // Generic primitives and shared script stubs cover the current TOML.
}
