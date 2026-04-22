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
    // ---- Encounters -------------------------------------------------
    rc_content_scurrius_register(world);
    rc_content_kalphite_queen_register(world);
    // ... additional boss modules land here as they're authored.

    // ---- Regions ----------------------------------------------------
    // (none yet)

    // ---- Quests -----------------------------------------------------
    // (none yet)
}
