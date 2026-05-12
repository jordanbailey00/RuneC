# rc-content/regions — region-specific code

This directory holds region-specific runtime code that cannot be
expressed cleanly as generic data or generic object interaction rules.

The region subsystem in `rc-core` loads terrain,
collision, objects, and NPC spawns generically via binary formats
(`data/regions/<region>.terrain/.objects/.cmap` and
`data/regions/<region>.npc-spawns.bin`). Most region-specific behavior
is expressible as data.

Use this file for:
- what region modules are for
- when region behavior should become code
- how region files should be organized

This directory is reserved for region-specific behavior that cannot be
expressed as data, e.g.:

- **Entrance triggers** — the Varrock palace wall auto-doors; cave
  entrance varbit checks that spawn instanced content.
- **Region-wide effects** — Wilderness combat-level bracket warnings;
  dungeon music auto-switch on entry; deadman-mode auto-skull.
- **Non-standard object interactions** — specific doors / ladders that
  don't follow the generic open/close/climb semantics.

## Module pattern

```c
// rc-content/regions/varrock.c
#include "../content.h"

static void on_enter_palace(struct RcWorld *w, ...) { ... }

void rc_content_varrock_register(struct RcWorld *world) {
    rc_event_subscribe(world, RC_EVT_PLAYER_MOVED, on_enter_palace, ...);
}
```

Same rules as `encounters/`: one file per region, static internals,
single public register fn, reference repos (Void for Varrock,
2011Scape for overlap areas) are valid sources.

## When code belongs here

Region code belongs here when the behavior is:
- scoped to one region or tightly related region family
- not a generic engine rule
- not better represented as pure data or a reusable primitive

This directory may be empty at times. That does not change its role.
