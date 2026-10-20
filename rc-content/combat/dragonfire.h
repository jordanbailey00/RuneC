#ifndef RC_CONTENT_DRAGONFIRE_H
#define RC_CONTENT_DRAGONFIRE_H

#include <stdbool.h>
#include <stdint.h>
#include "../../rc-core/types.h"

// Breath profiles named by encounter attacks (`breath = "..."` in
// content/encounters/*.toml); ids must match BREATH_PROFILES in
// tools/export_encounters.py.
enum {
    RC_DRAGONFIRE_KBD_FIERY = 1,
    RC_DRAGONFIRE_KBD_SPECIAL = 2,        // toxic, shocking and icy breath
    RC_DRAGONFIRE_VORKATH = 3,
    RC_DRAGONFIRE_VORKATH_COLOURED = 4,   // venomous and corrupting breath
};

// RcCombatContentHooks.npc_breath_max_hit implementation.
int rc_content_dragonfire_max_hit(const RcWorld *world, const RcNpc *npc,
                                  uint8_t breath, bool resisted,
                                  bool *shielded);

#endif
