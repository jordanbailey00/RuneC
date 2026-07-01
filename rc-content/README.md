# rc-content

`rc-content` holds the OSRS-specific game content: boss scripts, quest
state machines, and region-specific NPC behavior. It links against
`rc-core` (the generic engine) and exposes per-module registration
functions that callers invoke after creating a world.

This layer is what turns the generic engine into an OSRS game while
still allowing narrow RL builds to link only the content they need.
An Inferno sim should not need quest code, unrelated bosses, or other
region behavior just to run Inferno combat.

This document is **normative**. Every design decision involving
where new content goes must honor the rules here. See also
`rc-core/README.md` for the engine-side rules; this file is its
content-side counterpart.

Planning and task tracking live in `docs/work.md`; this README is a
technical overview of content boundaries and module structure.

---

## 1. Why this split exists

`rc-core` is the **generic game engine** — tick loop, pathfinding,
combat math, event bus, subsystem dispatch, generic encounter
primitives (`telegraphed_aoe_tile`, `spawn_npcs`, etc.). It knows
nothing about "Scurrius" or "Kalphite Queen" by name. The same
binary could, in principle, run a different game with the same
tick/combat shape.

`rc-content` is the **OSRS content layer** — boss-specific scripts,
per-quest state machines, region-specific NPC overrides. This is
where "Scurrius's phase-2 heal walk to the food pile" lives, not
in the engine.

### What we gain

1. **Isolated-sim build targets.** Link `rc-core` +
   only the needed `rc-content/` modules = an Inferno-only or
   Colosseum-only simulator that doesn't drag in unrelated boss,
   quest, or region code. Target RL training workloads that only
   care about one slice of the game.
2. **Clean content boundaries.** Each boss / region / quest is
   self-contained in its own file. To understand how Scurrius
   works, you open `encounters/scurrius.c` — not grep across
   `rc-core/`.
3. **Engine reusability.** `rc-core` can be reused for non-OSRS
   simulations or for headless testing without content deps.

### What we explicitly avoid

- **No plugin loader.** Static linking. Modules are `.c` files +
  `register` fns; CMake picks which ones compile in.
- **No runtime discovery.** A module not linked in is a link
  error if referenced — not a silent no-op.
- **No over-modularization.** 99% of NPCs (bankers, shopkeepers,
  wandering guards) are pure data. Only content with **code**
  (boss scripts, quest state machines) gets a module here.

---

## 2. Directory layout

```
rc-content/
├── content.h             shared content-registration API
├── content.c             aggregate rc_content_register_all()
├── encounters/           per-boss modules (one .c per boss)
│   ├── README.md
│   ├── scurrius.c
│   ├── kalphite_queen.c
│   ├── scripts.c          shared registry for currently stubbed hooks
│   └── ...
├── regions/              region-specific NPC / object behavior
└── quests/               per-quest varbit state machines
```

## 2.1 Key files

- `content.h`
  - shared declarations for per-module register functions
- `content.c`
  - aggregate registration entry point for the full content build
- `encounters/`
  - boss-specific scripts and encounter-only helpers
  - `scripts.c` registers authored script names that are not yet
    backed by a per-boss implementation
- `regions/`
  - region-scoped overrides that do not belong in generic engine code
- `quests/`
  - quest-specific state machines and handlers

---

## 3. Per-module conventions

Every content module follows the same shape:

**File naming.** One file per content unit, snake_case matching the
TOML/slug:
- `rc-content/encounters/scurrius.c` matches
  `content/encounters/scurrius.toml`.
- `rc-content/quests/cooks_assistant.c` matches
  `content/quests/Cook_s_Assistant/steps.toml`.

**Register function.** Every module exposes exactly one public
symbol:

```c
// rc-content/encounters/scurrius.c
#include "../content.h"

void rc_content_scurrius_register(struct RcWorld *world) {
    // Subscribe to events, register scripts, etc.
}
```

Signature: `void rc_content_<slug>_register(struct RcWorld *)`.
Declared in `content.h`. Called once per world by
`rc_content_register_all` or by isolation builds directly.

**Internals are file-static.** Script functions inside the module
(e.g. `static void scurrius_heal_at_food_pile(...)`) are `static`
— never exposed. Only the register fn is public.

**No cross-module calls.** Scurrius's code never calls KQ's code.
If two bosses need the same logic, it's a generic primitive and
belongs in `rc-core/encounter_prims.c`, not here.

---

## 4. What goes where — the split rule

| Code | Lives in | Why |
|---|---|---|
| `telegraphed_aoe_tile`, `spawn_npcs`, `drain_prayer_on_hit` etc. — generic, shared across many bosses | `rc-core/encounter_prims.c` | Used by 15+ bosses; duplicating per-boss would be absurd. |
| `scurrius_heal_at_food_pile` — one-off boss-specific logic | `rc-content/encounters/scurrius.c` | Only Scurrius needs it. |
| `kq_shed_exoskeleton` — one-off transition animation | `rc-content/encounters/kalphite_queen.c` | Only KQ. |
| Cook's Assistant state machine | `rc-content/quests/cooks_assistant.c` | Quest-specific varbit transitions. |
| Varrock NPC-specific tick behavior (if any emerges) | `rc-content/regions/varrock.c` | Region-scoped. |
| `RcWorld`, `RcEncounterSpec`, tick dispatcher | `rc-core/*` | Engine; content-agnostic. |
| NPC def data (`npc_defs.bin`), item defs, drop tables | `data/defs/*.bin` | Pure data — not code. |
| Encounter TOML data | `content/encounters/*.toml` | Pure data — compiled to `encounters.bin`. |

**The test:** if you removed this content module, would the engine
still compile and run? If yes → it's correctly in `rc-content`.
If no → it's wrongly placed (belongs in `rc-core`).

**Conversely:** if the engine knows this content module exists by
name (e.g. hardcodes `if (npc_id == 7221) ...`), it's wrong —
should be data-driven or should move to content.

---

## 5. Data and reference sources

RuneC content logic and reviewed source content live in this repo. Generated
data and generated assets live in the ignored local `data/` runtime install and
are populated by `scripts/setup-data.sh` or maintainer rebuild tooling.

External codebases are reference material only. They are audited from local
checkouts outside the shipped runtime, with tool paths supplied through
documented CLI arguments or environment variables. They must not be called at
runtime or during normal export/rendering:

- **RSMod** — modern OSRS server emulator. Useful for combat formulas,
  tick order, pathing, and any encounter logic it actually implements.
- **OSRS Wiki** — content facts, strategy prose, and transcripts that need
  review before becoming executable content.
- **osrsreboxed** — item / NPC combat-stat research.
- **RuneLite** — cache format and ID-constant research.

Private-server and wrong-game sources are not accepted as content authority.
When an approved OSRS-native source does not cover a fact, record a source gap
or add reviewed authored content with citations.

### Rules for porting content logic

1. **Check the source class first.** Prefer b237 cache/dumps, reviewed
   RuneC-owned content, source-gap rows, and OSRS-native research references
   that have been converted into RuneC-owned code or tables.
2. **OSRS-only content** (Scurrius, Vorkath, raids, DT2 chain,
   Muspah, Hueycoatl, Royal Titans, etc.) must be reconstructed from approved
   OSRS-native sources or tracked as a source gap.
3. **Validate timing and damage against the wiki** even when a
   reference repo has working logic.
4. **Port logic, not data.** Drop tables, attack stats, etc. already
   live in our binaries.
5. **Credit in comments.** If a script is substantially ported, note
   the source file + repo in a comment.

### Example comment style

```c
// kq_shed_exoskeleton: 20-tick transition animation.
// Ported from rsmod content/bosses/kalphite_queen/KalphiteQueen.kt
//   transition() method.
// Validated against OSRS Wiki Kalphite Queen#Attacks section.
```

---

## 6. Selective content linking

Consumers can link only the content modules they need instead of
calling `rc_content_register_all`. A narrower binary can expose a
custom registration surface while still depending on the same
`rc-core` engine:

```cmake
add_library(rc-content-colosseum STATIC
    rc-content/encounters/colosseum.c
    # only the modules this sim needs
)
target_link_libraries(rc-content-colosseum PUBLIC rc-core)

add_executable(rc-sim-colosseum sims/colosseum/main.c)
target_link_libraries(rc-sim-colosseum rc-content-colosseum)
```

The sim's `main` calls only the specific register fns it needs.
Unused modules are not compiled into that target.

---

## 7. When this document and reality disagree

If content code is in the wrong place (for example boss-specific logic
in `rc-core`, or a shared generic primitive in a boss module), fix the
code or update this document first if the rule itself is what needs to
change.
