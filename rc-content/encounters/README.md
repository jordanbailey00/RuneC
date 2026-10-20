# rc-content/encounters

One `.c` file per boss encounter. Holds the **boss-specific scripts**
referenced by the encounter's TOML `script = "..."` fields. Does NOT
hold generic primitives (those live in `rc-core/encounter_prims.c`).

See the parent `rc-content/README.md` for the engine/content split
rationale. This README covers the per-encounter specifics.

Use this file for:
- what encounter modules are for
- what belongs here versus in TOML or `rc-core`
- how encounter files are organized
- how encounter code relates to encounter data

---

## What belongs in this directory

**In-scope (goes here):**
- Boss-specific phase scripts and option handlers — e.g. Vorkath's Poke,
  which wakes the sleeping form through `rc_encounter_wake`.
- Boss-specific attack rotations that don't generalize — e.g. Zulrah's
  tick-indexed phase rotation, Olm's hand-vs-head coordination.
- Boss-specific environmental setup (if not expressible in TOML).

**Out of scope (goes elsewhere):**
- Generic primitives used by ≥2 bosses — `rc-core/encounter_prims.c`.
- NPC stat data (hp, max_hit, attack_types) — `data/defs/npc_defs.bin`
  via `tools/export_npc_defs_full.py` into the ignored local runtime install.
- Attack / phase / mechanic *data* — `content/encounters/<boss>.toml`
  → `data/defs/encounters.bin` via `tools/export_encounters.py`.
- Drops — `data/defs/drops.bin`.

**The test:** if you can express the behavior in TOML + a generic
primitive that another boss could reuse, it doesn't belong here.
Only *genuinely one-off* code lives in these files.

## Relationship to encounter data

Only documents declaring `support = "full"` or `support = "attacks_only"`
are exported; the rest are research notes the game never loads. A full spec
may name a phase script only once that script is registered here; otherwise
binding refuses the spec with a diagnostic.

Encounter behavior is split across three layers:
- `content/encounters/<boss>.toml`
  - boss data: forms, helpers, spawned objects, attacks, phases, mechanics,
    params; owned by RuneC source
- `content/combat_visuals/visuals.tsv`
  - presentation for every attack, mechanic part and form change by name
    (`docs/encounter_primitives.md`); `tests/test_encounter_visuals.c`
    checks coverage. Neither core nor this directory names a model,
    sequence or spotanim
- `rc-core/encounter_prims.c`
  - reusable generic mechanic implementations referenced by
    `primitive = "..."`
- `rc-content/encounters/<boss>.c`
  - one-off boss logic referenced by `script = "..."`

If a mechanic can be described as reusable behavior with parameters, it
belongs in the primitive layer, not here.

---

## File structure

One `.c` file per encounter, named after the TOML slug:

```
encounters/
├── README.md                (this file)
├── vorkath.c                → content/encounters/vorkath.toml
└── ...                      scripts for unadmitted bosses (inactive)
```

Kalphite Queen, King Black Dragon, Chaos Elemental and General Graardor need
no module: their behavior is entirely data. There are no stub registries: an
unregistered script name refuses its spec instead of running a no-op.

For multi-file encounters (raids with many rooms, wave bosses like
Inferno with per-wave logic), use a subdirectory:

```
encounters/
├── chambers_of_xeric/
│   ├── chambers_of_xeric.c       (entry + register)
│   ├── olm.c                     (final boss)
│   ├── tekton.c                  (room mini-bosses)
│   └── ...
├── inferno/
│   ├── inferno.c                 (wave progression)
│   ├── zuk.c                     (final boss)
│   └── ...
```

The register fn always lives in the entry file and is named for the
encounter slug: `rc_content_chambers_of_xeric_register`.

---

## Per-encounter file template

```c
#include "../content.h"

// <Boss Name> — <1-sentence summary>.
// Cache NPC id(s): <list>.
//
// MOST of <Boss> runs on generic primitives in rc-core. The TOML at
// `content/encounters/<slug>.toml` references:
//   - <primitive_name>  <TOML mechanic name>  — generic
//   - <primitive_name>  <TOML mechanic name>  — generic
//
// This file exists to hold the *boss-specific* scripts listed in
// the TOML's `script = "..."` fields:
//
//   - <script_name>
//       <what it does + why it can't be expressed as a generic primitive>
//
// Reference: <approved OSRS-native source, or source gap if unavailable>

// ---- Boss-specific scripts (static — not exported) --------------------

// static void <script_name>(struct RcWorld *world, int enc_idx,
//                           const void *params) {
//     // ...
// }

// ---- Registration ------------------------------------------------------

int rc_content_<slug>_register(struct RcWorld *world) {
    // Register encounter-specific hooks; 1 on success, 0 on failure.
    return rc_encounter_register_script(world, "<script_name>", <fn>) >= 0;
}
```

---

## Reference sources

Per `rc-content/README.md`:
- `rsmod` is the modern OSRS engine/code reference when it has the
  needed encounter
- b237 cache/dumps, RuneLite, RSMod, OSRS Wiki, and reviewed authored content
  are the accepted authority classes
- missing facts become source-gap rows until approved evidence exists

### Encounter reconstruction checklist

When porting from a reference repo:

1. Verify the boss exists in OSRS for the target cache/content revision.
2. Locate approved source evidence in b237 cache/dumps, RuneLite, RSMod,
   OSRS Wiki, or reviewed authored content.
3. Read the wiki for OSRS-specific behavior (OSRS sometimes
   rebalanced old content — max hits, drop rates, attack speeds
   can differ).
4. Port the **logic** to C. Don't port data (stats, drops) — those
   already live in our binaries from Phase 1-2.
5. Comment the approved source in the script file, or link the source-gap row.
6. Write a regression test asserting at least one attack rotation
   or damage value matches the wiki.

### OSRS-only reconstruction checklist

1. Review the boss page + strategies page, using OSRS Wiki evidence or
   tracked RuneC content notes.
2. Read the wiki mechanics section + infobox + strategies.
3. Encode phases / attacks / mechanic triggers in the TOML.
4. For anything not expressible in the existing primitive set,
   author a boss-specific script in this directory.
5. Regression test against wiki-stated HP curves, attack cadence,
   phase transitions.
