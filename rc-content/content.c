#include "content.h"

// Aggregate content registration. Calls every per-module register
// fn; isolation builds simply omit the .c files they don't want,
// and the linker resolves only the modules actually compiled in.
//
// Adding a new content module:
//   1. Drop the .c file under the appropriate category directory
//      (encounters/, regions/, quests/).
//   2. Declare its register fn in rc-content/content.h.
//   3. Add a call below under the matching section.
//
// Why this is not a table or a weak-symbol dance: symmetric, easy
// to read, and a missing register fn is a link error at build time
// (good — isolation builds fail loudly when they reference content
// they've excluded).

void rc_content_register_all(struct RcWorld *world) {
    // ---- Combat -----------------------------------------------------
    rc_content_combat_register(world);

    // ---- Encounters -------------------------------------------------
    rc_content_encounter_script_stubs_register(world);
    rc_content_scurrius_register(world);
    rc_content_kalphite_queen_register(world);
    rc_content_alchemical_hydra_register(world);
    rc_content_the_nightmare_register(world);
    rc_content_phantom_muspah_register(world);
    rc_content_duke_sucellus_register(world);
    rc_content_leviathan_register(world);
    rc_content_whisperer_register(world);
    rc_content_gauntlet_register(world);
    rc_content_nex_register(world);
    rc_content_raids_register(world);

    // ---- Regions ----------------------------------------------------
    // (none yet)

    // ---- Quests -----------------------------------------------------
    // (none yet)
}
