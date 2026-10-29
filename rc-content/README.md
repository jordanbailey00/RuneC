# rc-content

`rc-content` holds the OSRS-specific game content: boss scripts, quest
state machines, and region-specific NPC behavior. It links against
`rc-core` (the generic engine) and exposes per-module registration
functions that callers invoke after creating a world.

`consumables.c` registers reviewed B237 food/potion definitions and the prayer
restoration equipment bonus. Core owns transactions/timers; content owns item
identities, replacement links and effect values. Registration validates the
installed item definitions and fails openly. Repeat registration after world
reset. See [consumables](../docs/consumables.md) for support and exclusions.

`storage.c` registers B237 bank/deposit option bindings and bankability from
`storage_policy.inc`. Regenerate/check with
`python3 tools/export_storage_policy.py --cache "$RUNEC_B237_CACHE" [--check]`.
The exporter verifies the B237 `no_bank` parameter signature, preserves item
parameters and rejects incomplete definitions. Generic Deposit/Collect/Use
actions do not grant personal-bank access. Core owns arrival, session lifetime,
amounts, notes, capacity and transactions. Register again after world reset;
no reference checkout, extra runtime pack or loader fallback is needed.

This layer is what turns the generic engine into an OSRS game while
still allowing narrow RL builds to link only the content they need.
An Inferno sim should not need quest code, unrelated bosses, or other
region behavior just to run Inferno combat.

This document is **normative**. Every design decision involving
where new content goes must honor the rules here. See also
`rc-core/README.md` for the engine-side rules; this file is its
content-side counterpart.

Planning and task tracking are kept in local Markdown files. This README is
the tracked technical overview of content boundaries and module structure.

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
├── combat/dragonfire.c   breath protection tables (npc_breath_max_hit)
├── encounters/           per-boss modules (one .c per boss)
│   ├── README.md
│   ├── vorkath.c          Poke wakes the sleeping form
│   └── ...                scripts for unadmitted bosses (inactive)
├── raids/                Chambers of Xeric (see 2.2)
├── regions/              region-specific NPC / object behavior
└── quests/               per-quest varbit state machines
```

## 2.1 Key files

- `combat/magic.c`
  - owns spell/staff compatibility, elemental scaling, native powered attacks,
    charge capacities/depletion and basic landed magic effects
  - charged magic weapons use remaining charges in the existing item-state
    scalar; the validation bank seeds charged/empty versions, while recharge
    interfaces and several weapon passives/specials remain DSA-031
  - `content/combat/spell_effects.tsv` supplies reviewed combat values to both
    spell export and snapshot-based rebuilds without changing spell indices,
    rune costs or unrelated definitions
  - restricted targets use the reviewed subset in
    `content/combat/npc_spell_targets.inc`; unknown eligibility rejects
  - owns supported spell/weapon gameplay arrival delays; attack events carry
    per-hit timing, never projectile assets
- `combat/runes.c`
  - one staged payment algorithm for inventory, combination/substitute runes,
    carried regular/divine pouches, finite tomes, Bryophyta and Iban charges
  - pouch deposit/empty and payment use core item transactions; no detached
    pouch supplies, empty-tome supplies or partial payment on failure
- `combat/ammunition.c`
  - compatible ammo/fuel slots, supported atlatl damage and chinchompa fuse
    inputs; salamander fuel applies across melee/ranged/magic modes
  - Ava/recovery, secondary targets and unsupported internal containers remain
    explicit audit follow-ups, not complete weapon-content parity
- `combat/ralos.c`
  - owns Tonalztics charge actions, independent hits and B237-era Division
  - for this item family, the existing stack `state_id` encodes remaining
    charges (1..20,000 for charged 28922, zero for uncharged 28919); it is not
    a shared handle table or a new per-player charge field
  - transfers and depletion use core item transactions; the core hit batch
    reserves capacity, commits payment and publishes attacks atomically
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
  - `raid_transports.c` handles raid entrances and exits (ToB, ToA,
    The Fractured Archive): it moves the player between each lobby and the
    raid's static map template, which serves as the single-player instance.
    Chambers of Xeric owns its own entry and exit (`raids/cox_raid.c`)
- `quests/`
  - quest-specific state machines and handlers

## 2.2 Chambers of Xeric (`raids/`)

The whole raid except loot: party and difficulty, layout generation, room
chaining, every room's mechanics, resources and potions, and the Great Olm.
Solo only (RuneC worlds hold one player), so every party-size rule is
evaluated at one player.

| File | Owns |
| --- | --- |
| `cox.h` | shared types (`CoxRaid`, `CoxRoom`, room state, hazards, stats) and helpers |
| `cox_raid.c` | entry and difficulty, lobby, layout, room chain, floors, holes and ropes, timer, points, overlay variables, death, leaving |
| `cox_combat_rooms.c` | Tekton, Vanguards, Vasa Nistirio, Guardians, Lizardman shamans, Skeletal mystics, Muttadiles, Vespula; `cox_modify_player_hit` |
| `cox_puzzle_rooms.c` | Ice demon, Thieving, Tightrope, Jewelled crabs |
| `cox_resources.c` | scavengers and their shortcut, farming rooms, weeds, fishing, bats, cooking, herblore, potion effects, tools, storage units |
| `cox_olm.c` | the Great Olm |

### How it is built

- **No instances.** Rooms are the cache's static 32x32 room templates, used
  in place. Each template exists in three exit variants (x 3264 / 3296 /
  3328: exit west, north, east; entry always at the south edge). A raid
  picks one variant per room and chains them by moving the player, so a
  room's tiles, objects and collision are the cache's own. Floor starts,
  ends and the Olm chamber are templates too.
- **Per-world state.** `rc_content_chambers_of_xeric_register` allocates the
  raid state through `rc_world_add_content_tick` (released on reset and
  destroy). Hooks and handlers reach it with `rc_world_content_ctx`.
- **NPCs.** Bosses are `owner_controlled` NPCs the room module drives with
  `rc_combat_npc_scripted_attack`; simple NPCs (scavenger beasts, soldiers,
  crabs, guards) use the core combat AI with per-life stat overrides
  (`rc_npc_apply_combat_override`). Room NPCs spawn when the room is
  entered and are re-resolved by uid every tick. The active area drops
  NPCs far from the player; a revisited room restores each one as it was
  last seen (form, HP, stats, targetability).
- **Locs.** Room changes use runtime loc states (add, replace, delete,
  revert; timed ones through `rc_world_object_expire`). Walkability checks
  read the live map where it is loaded, so gates, crystals and spawned
  rocks block like the cache's own locs. Every state the viewer must draw is listed in
  `content/world/runtime_locs.toml`: `[[dynamic]]` rows become dynamic loc
  sidecar rows in region exports, `[[spawn]]` rows become spawn templates
  (`tools/cache_pipeline/export_objects.py`,
  `tools/cache_pipeline/export_spawn_objects.py`). Leaving reverts every
  loc state inside the raid area.
- **Combat hooks.** The module wraps `on_player_hit_npc` (points, room
  reactions), `on_npc_hit_player` (raid NPCs follow their room's rules, not
  the regular poisonous-monster tags) and sets `modify_player_hit` (style
  and weapon rules: Tekton, Vasa's crystals, Guardians' pickaxes, the Ice
  demon, crabs, Olm, Vespula). Generic drops are suppressed for raid NPCs.
- **Presentation.** Attack keys (`"Spark"`, `"Boulder"`, `"Sphere melee"`
  ...) map to curated rows in `content/combat_visuals/visuals.tsv`. The
  points overlay reads the raid's client variables, as in game: party
  score (varbit 5431), personal score (varp 4609), timer in ticks (varbit
  6386) and progress (varbit 5425, 5 stops the timer); the viewer formats
  them. Choices use the chatbox choice prompt; messages use the game
  message ring.

### Raid flow

- **Entry and lobby.** RuneC raids are solo, so there is no party and the
  recruiting board does nothing. Entering the steps asks which raid to
  start: a normal raid with the Small (half small, half medium patterns),
  Large or Full (three fixed floors) layout, or Challenge Mode (always
  Full). Captain Rimor's Set-difficulty disables level scaling. The
  lobby's steps also offer Reload (new layout, before the start only). The
  first room passage asks "Start the raid?"; starting begins the timer and
  points.
- **Layout.** RuneLite's 26 known room patterns per floor (small 8, medium
  16, large 2). Combat rooms are a contiguous run of one of the two
  rotations, in either direction; puzzles come from the cycle Ice demon,
  Tightrope, Crabs, Thieving. A room's variant follows the turn its path
  takes; a template already in use takes the other free variant.
- **Moving between rooms.** Combat and puzzle exits are passages (cross 4
  tiles); open rooms switch on the exit edge. Walking back off a room's
  entry row returns to the previous room. Exits stay blocked (crystals,
  tendrils, invisible barriers) until the room is complete. Once a fight
  has raised its entry gate (fire or tendrils), a raider coming back
  arrives in front of it and must Cross or Pass it, taking its damage.
- **Floors.** A hole at each floor's end drops to the next floor's start; a
  rope climbs back to the previous floor's end. The last hole leads to the
  Olm antechamber.
- **Scaling** (OSRS DPS calculator): C = combat level clamped to 60..126,
  H = 55 + 44·HP/99 clamped to 55..99. Solo offence and defence are
  ⌊base·H/99⌋, HP ⌊base·C/126⌋; Challenge Mode adds 50% (Tekton defence
  +20%, Vasa crystals magic only, Olm HP unscaled). Clamps follow the
  calculator.
- **Points.** Damage dealt (5 per HP at combat 126), plus room and
  skilling points; death costs 40% of personal points.
- **Death** drops raid items where the player died and respawns them after
  5 ticks at the end of the previous floor (the lobby on the first floor).
  **Leaving** (steps, teleport, walking out) removes every raid item (lore
  books stay), resets the raid and reverts its locs.

### Rooms

- **Tekton** wakes when approached, walks to the raider and swings every 3
  ticks (stab, slash, crush; Protect from Melee lets half through) for a
  27-tick first session and 47-tick later ones, then repairs at the anvil
  (healing, sparks on each raider's 3x3); enraged after repair with higher
  defences. Ranged does nothing, magic a fifth, and it cannot be hurt at
  the anvil.
- **Vanguards** burst from their holes when approached; melee hits three
  times, ranged and magic hit the raider plus two tile strikes. A 40% HP
  gap heals and shuffles the living ones.
- **Vasa Nistirio** wakes from its crystal, throws boulders and siphons a
  crystal while teleporting the raider beside it (prayers off, freeze,
  delayed blast); destroying a crystal discards its stored heal.
- **Guardians** take damage only from pickaxes (scaled by Mining level);
  melee when adjacent, rockfall otherwise; statues remain after death.
- **Lizardman shamans** spit, splash acid (poison), jump onto the raider and
  release exploding spawn.
- **Skeletal mystics** cast fire blast and a defence-draining vulnerability
  or melee up close.
- **Muttadiles**: the small one first; it eats from the meat tree to heal
  (chop it to stop that); the large one emerges after the small dies.
- **Vespula** stings lux grubs (they turn infected and offer Feed) into
  soldiers; blossoms picked from the flowered roots heal a grub by 35. She
  lands when low and enrages when her portal is hit; killing the portal
  ends the room. Her ranged attacks poison from 20, soldiers from 15.
- **Ice demon**: light the braziers with kindling to thaw it; fire spells
  deal 2.5x, demonbane 1.15x, anything else a third.
- **Thieving**: picklock chests for grubs (or bats, or gas), feed the
  corrupted scavenger, which then lets the raid through.
- **Tightrope**: cross under fire from the deathly guards, take the
  keystone, dispel the barrier (which kills the guards).
- **Jewelled crabs**: an orb leaves the carving every time the last one
  ends; attack styles colour the crabs (Smash with a hammer roots one and
  turns it red), a crab turns the orb right and colours a white one, a
  clashing colour or a wall ends it, and a raider in its path takes 22.
  Each crystal needs its colour; four lit crystals open the room.
- **Scavengers** drop supplies; the large room's shortcut (stump or rubble)
  needs a skill level. **Farming rooms** have seeds, patches, the gourd
  tree, the geyser, fishing and bats. Cooking, herblore (overloads, Xeric's
  aid, revitalisation, prayer enhance, elder/twisted/kodai potions) and
  storage units (building only) work as in game.

### The Great Olm

Rises in an east or west wall with both claws; claw phases (three solo)
each rise with one power (acid, flame or crystal; no repeat until all
three are used), the penultimate phase allows all powers and four
specials, and the final stand is the head alone on the penultimate wall.
The head acts on a 4-step clock (basic, null, basic, special), turns to
the raider's view and skips a basic when it must turn; spheres sap
prayers; the left claw casts crystal burst, lightning, teleport portals
and (penultimate) heal, and clenches when hit hard after a null step. Claw
deaths sink the head under a crystal shower, then it rises on the other
wall. The head's death opens the exit; the reward chest is empty (no loot).

### Sources

OSRS Wiki pages for the raid, every room NPC and room object, and the
strategy and Challenge Mode pages; Jagex posts and Mod Ash replies; the
OSRS DPS calculator's CoX scaling (weirdgloop/osrs-dps-calc); RuneLite's
raids plugin (layout patterns, rotations) and `gameval` constants; the
b241 cache for ids, room templates and collision; and a third-party
analysis of public RSProx recordings of live OSRS (MRobertEvers
oldschool-clientc, `docs/rsprox-analysis/cox`) for tick timings, tiles
and spawn positions. The recordings were mostly Grid Master worlds, so
their damage numbers read low; only their timings and tiles are used.

### Values chosen where sources conflict

- Level scaling: the DPS calculator's linear scaling to 126, not the
  "115 combat" wording of the 2021 post.
- Respawn: the end of the previous floor (strategy page), not its start.
- Tekton: 3-tick swings; sessions of 27 then 47 ticks (capture and the
  OpenOSRS helper agree); anvil 15-21 ticks.
- Olm: one power per claw phase, all powers in the penultimate phase
  (current wiki); transition crystals 16-25 / 10-16 and power crystals
  15-20 (wiki); crystal bombs 60 falling to 15 (wiki), kept in the final
  phase; teleport damage 1 + ⌊4.3·distance⌋ (capture); deep burn 6 hits
  (wiki); clench 45 ticks (capture); power attack 1 in 3, 1 in 2 in the
  final stand (Mod Ash, read as the head phase).
- Guardian HP: 151 + average Mining (both formulas agree solo).

### Estimates (no published value)

- Puzzle cycle order (Ice demon, Tightrope, Crabs, Thieving) and the Full
  layout's grid path.
- The large scavenger room's shortcut tiles.
- Cooking burn rates (half at the requirement, none 30 levels above).
- Revitalisation (−) and regular amounts (9 + 20%, 10 + 25%); prayer
  enhance (−) and regular (8 fewer pulses per tier).
- Ice demon thaw rate (10 heat per kindling, 540 to thaw solo) and max hit.
- Crab points per lit crystal (500) and crab max hit.
- Vespula and soldier poison chance (1 in 4 damaging hits).
- Medivaemia roots flower again 30 ticks after a blossom is picked.
- Olm sphere rate (1 in 6), acid pool and drip lifetimes, lightning damage
  (10-20).

### Not implemented

- Loot, the score board and unclaimed loot. Raids are solo by design.
- Item storage in storage units (building works; Private and Shared say so).
- The Olm head cannot be attacked during claw phases (melee on the head is
  unverified in sources); the snake in farming rooms is not spawned.
- The tightrope walk has no crossing animation; the raider moves to the far
  end after the crossing time.

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
// rc-content/encounters/vorkath.c
#include "../content.h"

int rc_content_vorkath_register(struct RcWorld *world) {
    // Register scripts or handlers; return 1 on success, 0 on failure.
}
```

Signature: `int rc_content_<slug>_register(struct RcWorld *)`, returning 1
on success and 0 on failure. Declared in `content.h`. Install the aggregate
with `rc_world_install_content(world, rc_content_register_all)` so it runs
again after `rc_world_reset`; isolation builds install their own function.

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
