# rc-core

`rc-core` is the headless game backend for RuneC. Its job is to power
the playable game while also serving as the high-performance baseline
for headless RL simulation. It runs OSRS-faithful ticks with zero
rendering, zero I/O in the tick path, and zero dependencies on
`rc-viewer` or any graphics library.

Use this file for:
- engine role and boundaries
- subsystem model
- state-layout rules
- tick-path and determinism rules
- the `rc-core` / `rc-content` split

Planning lives in `docs/work.md`. This file is a technical overview of
the component boundary, architecture rules, and runtime invariants.

This document is **normative**. Every design and refactor decision
must honor it. If a change requires relaxing a rule, update this
document first with the reasoning rather than quietly violating it.

---

## 1. Purpose

`rc-core` serves three consumers:

1. **Interactive play** via `rc-viewer` — one world, one player, live
   rendering.
2. **Headless simulation** for RL / eval — thousands of parallel
   worlds, zero rendering, target **tens of millions of ticks/sec**
   aggregate throughput across threads.
3. **Automated correctness tests** — repeatable scenarios validating
   combat and encounter behavior against curated expectations.

All three consume the same API. `rc-core` never knows which caller
it has. If a feature requires the renderer to be present, it does
not belong in `rc-core`.

The goal is one simulation implementation, not separate "game" and
"sim" codepaths that drift apart.

---

## 2. Core + subsystems

`rc-core` has a **tiny always-on base** and a set of **optional
subsystems**.

### Base (always on, always loaded)
- World position/grid semantics
- Region/collision query API
- Active-area activation for collision windows and NPC spawn slices
- Player position + route
- NPC position + route
- Pathfinding (BFS + LOS)
- Tick counter + RNG state
- Varbit state (empty if no subsystem uses varbits)

### Subsystems (individually opt-in)
- **combat** — hit rolls, damage, pending-hit queue, protection
  prayers, selected spell max-hit hints, and stable-id combat visual lookup
- **prayer** — data-backed active prayers, drain, conflicts, boosts
- **equipment** — worn items, equipment bonuses, weapon stance
- **inventory** — 28-slot inventory, stacks, item ops
- **consumables** — food heal, potion boost/drain
- **loot** — drop table rolls, ground items, pickup
- **skills** — XP table, level-up, recipes, skill-drop sources, and
  gathering-node lookup
- **quests** — quest state machine, varbit triggers
- **dialogue** — NPC dialogue state trees
- **shops** — shop stock and static price/restock metadata
- **storage** — bank/deposit/storage object access and bank item state
- **slayer** — task metadata, assignment, unlock/block/prefer filters,
  task progress
- **encounter** — boss phase/rotation dispatcher + primitive registry
- **objects** — object definitions, placed-object lookup, typed object
  behavior tags, placement-key object interaction, altar restore, dynamic door
  state, and gathering-node action/depletion/respawn
- **traversal** — unified object/item/spell traversal-edge lookup and
  player relocation helper
- **regions** — source-backed collision shared by movement/action
  systems; provisional area-flag lookup for wilderness, wilderness
  level, multicombat, singles-plus, and safe-zone checks

Audio and music selection are presentation concerns owned by `rc-viewer`.
`rc-core` may expose area/region state that the viewer can observe, but it
must not hold audio playback state.

A Colosseum / Inferno simulator needs: **base + combat + prayer +
equipment + inventory + consumables + encounter**. Nothing else.

---

## 3. `RcWorldConfig` drives bring-up

```c
typedef struct {
    uint32_t subsystems;     // bitmask: RC_SUB_COMBAT | RC_SUB_PRAYER | ...
    const char *npc_defs_path;
    const char *items_path;
    // ...asset paths loaded only by enabled owning subsystems
} RcWorldConfig;

RcWorld *rc_world_create_config(const RcWorldConfig *cfg);

// Presets:
RcWorldConfig rc_preset_full_game(void);
RcWorldConfig rc_preset_combat_only(void);   // Colosseum / Inferno sim
RcWorldConfig rc_preset_skilling_only(void);
```

Gameplay area activation is explicit and backend-owned:

```c
RcActiveAreaRequest req = {
    .origin_x = 3072,
    .origin_y = 3264,
    .width = 320,
    .height = 320,
    .min_plane = 0,
    .max_plane = RC_MAX_PLANES - 1,
};
rc_world_activate_area(world, &req, NULL);
```

`rc_world_activate_area` populates `RcWorld.map` from `collision_tiles.bin`,
clears/reloads the active NPC spawn slice from `world.npc-spawns.bin`, and
records the active area generation on `RcWorld`. The viewer and headless agents
use the same API before issuing gameplay actions. Scenario/dev validation code
that needs a guaranteed target uses `rc_world_ensure_npc_near`; presentation
frontends should not resolve NPC definitions or mutate `world->npcs` directly.

Object interactions that originate from placed scene data should pass the
placement key into core. Placement-key APIs make dynamic loc state local to one
exact placed object; tile/id APIs are compatibility paths for tests and tools
that do not have placement identity.

Config is consumed **once** at world creation. After that, no
config-driven branching appears on the tick path. The enabled
bitmask is checked only by the tick dispatcher:

```c
void rc_world_tick(RcWorld *w) {
    base_tick(w);
    if (w->enabled & RC_SUB_COMBAT)   combat_tick(w);
    if (w->enabled & RC_SUB_PRAYER)   prayer_tick(w);
    if (w->enabled & RC_SUB_ENCOUNTER) encounter_tick(w);
    // ...
}
```

Each `*_tick()` is a direct function call. No vtable. No dispatch
cost beyond a cache-resident bitmask-AND.

---

## 4. State layout — arena, inline, per-world

**All subsystem state lives inline inside `RcWorld`** at fixed
offsets. Disabled subsystems' fields occupy memory but are never
touched.

```c
typedef struct {
    // Base (always present, always valid)
    RcWorldMap map;
    RcPlayer player;
    RcNpc npcs[RC_MAX_NPCS];
    uint32_t tick;
    uint64_t rng_state;
    uint32_t enabled;            // RcWorldConfig.subsystems

    // Subsystems (fields present always; only touched if enabled)
    RcCombatState combat;
    RcPrayerState prayer;
    RcLootState loot;
    RcQuestState quests;
    RcDialogueState dialogue;
    RcEncounterState encounter;
    // ...
} RcWorld;
```

**Rationale:**
- A full world is one contiguous arena → `memcpy` rollback / snapshot
  is O(sizeof(RcWorld)).
- No per-world malloc fragmentation across thousands of parallel envs.
- A "disabled" subsystem costs some wasted struct space (~kB total
  across all subsystems) — trivial compared to the throughput win.

**Corollary:** no dynamic allocation of sub-buffers at world creation.
Max counts are compile-time constants (`RC_MAX_NPCS`, `RC_MAX_GROUND_ITEMS`,
`RC_MAX_PENDING_HITS`, etc.). Over-sizing is fine; allocation in the
tick path is not.

---

## 5. Handles, not pointers, across subsystems

When one subsystem references an entity in another, use an integer
**handle** (index into the owning array) — never a `*` pointer.

```c
typedef uint16_t RcNpcId;
typedef uint16_t RcItemSlot;
typedef uint32_t RcGroundItemId;

// GOOD — combat stores the target as a handle:
typedef struct {
    RcNpcId target;   // RC_NPC_NONE = no target
    // ...
} RcCombatState;

// BAD — pointer into an array that may be compacted / memcpy'd:
// RcNpc *target;
```

**Rationale:**
- `memcpy`-based snapshot/rollback works cleanly (pointers would
  need fixup).
- Arrays can be compacted / reordered without breaking references.
- Handles are smaller than pointers (better cache utilization).

Within a subsystem's own code, using `RcNpc *` transiently on the
stack during one tick is fine. Just don't store it across ticks
or across subsystem boundaries.

---

## 6. Hot / cold data separation

Per-NPC and per-player state is split into **hot** and **cold**
parallel arrays, indexed by the same id.

```c
// HOT — accessed every tick, dense layout matters for cache.
typedef struct {
    int16_t x, y;
    int8_t plane;
    uint16_t hp;
    RcNpcId target;
    uint8_t cooldown;
    uint8_t flags;
} RcNpcHot;   // target: ≤ 16 bytes, 4 per cache line

// COLD — accessed at spawn / death / rare events.
typedef struct {
    uint32_t def_id;
    int16_t spawn_x, spawn_y;
    uint8_t wander_range;
    uint8_t anim_state;
    // ...
} RcNpcCold;

RcNpcHot  npc_hot[RC_MAX_NPCS];
RcNpcCold npc_cold[RC_MAX_NPCS];
```

Today `RcNpc` is still a transitional monolithic struct. Treat it as
hot-dominant and avoid adding rarely-touched cold fields to the hot
path as you change this area.

---

## 7. Events — for episodic concerns only

A simple function-pointer registry lets subsystems subscribe to
episodic cross-system events:

```c
// Defined events (stable ids):
enum {
    RC_EVT_NPC_DIED = 1,
    RC_EVT_NPC_SPAWNED,
    RC_EVT_PLAYER_DAMAGED,
    RC_EVT_ITEM_PICKED_UP,
    RC_EVT_DROP_GRANTED,
    RC_EVT_PHASE_TRANSITION,
    RC_EVT_DIALOGUE_OPENED,
    RC_EVT_QUEST_STAGE_CHANGED,
    // ...
};

void rc_event_subscribe(RcWorld *w, int evt, RcEventFn fn);
void rc_event_fire(RcWorld *w, int evt, const void *payload);
```

**Rules:**
- Events fire **episodically** — deaths, drops, phase shifts, quest
  stage changes, dialogue transitions. Hundreds per second at most.
- **Never** fire an event per tick per entity. NPC tick loop, combat
  damage resolve, pathfinding step — all direct calls.
- Handlers may not re-enter `rc_event_fire` for the same event type
  within the same dispatch (caught at dev-assert).
- A disabled subsystem never subscribes, so its handlers never fire
  and its code never runs.

**Rationale:** events are the clean decoupling mechanism. Keeping
them episodic means the dispatch cost is negligible aggregated over
a tick, and the hot path stays tight.

Current encounter events:
- `RC_EVT_PLAYER_DAMAGED` routes on-hit primitives.
- `RC_EVT_PHASE_TRANSITION` routes phase-enter/phase-exit primitives
  and phase scripts.
- `RC_EVT_NPC_ATTACK` routes encounter after-attack, attack-counter,
  and mechanic-window hooks.

---

## 8. No cross-subsystem reach-through

- `combat.c` may not call `loot_*` directly. It fires `RC_EVT_NPC_DIED`;
  `loot.c` subscribes and rolls drops.
- `loot.c` may not read `combat_state` internals. It operates on the
  event payload + base world state.
- `quests.c` may not poll `dialogue_state`. It subscribes to
  `RC_EVT_DIALOGUE_OPENED` / `RC_EVT_DIALOGUE_CHOICE`.

The base struct is the **only shared territory** that any subsystem
can freely read. Hot NPC array, player position, varbits — these are
public. Subsystem-private state is private.

This discipline is what makes "run only combat + prayer + encounter"
a one-line config change instead of a month-long refactor.

---

## 9. Binary loading — per-subsystem, lazy

All runtime data paths resolve under the local `data/` directory, which is a
separate `RuneC-DB` checkout. The main RuneC repository owns loaders and
runtime code; `RuneC-DB` owns generated binaries, curated DB inputs, generated
sprites/models/regions, and local source corpora.

Each subsystem owns its binary(s):

| Subsystem | Binaries it loads |
|---|---|
| base | player action gates when configured |
| NPC-using subsystems | `npc_defs.bin` |
| shared support | `normalization.bin` when item/NPC/loot/shop/storage users are enabled |
| equipment / inventory | `items.bin` |
| varbit/varp state | `varbits.bin`, `varps.bin` when configured |
| loot | `drops.bin`, `rdt.bin`, `gdt.bin`, `mrdt.bin` |
| quests | `quests.bin` |
| dialogue | `dialogue.bin` |
| shops | `shops.bin` |
| storage | object behavior data + per-world bank state |
| traversal | `traversal_edges.bin` |
| regions | `collision_tiles.bin`, `area_flags.bin` |
| skills | `recipes.bin`, `skill_drops.bin`, `gathering_nodes.bin` |
| combat / slayer / encounter | `regular_npc_mechanics.bin` |
| encounter | `encounters.bin` (ENCT v12), `activity_schemas.bin`, `activity_spawns.bin`, `activity_mechanics.bin`, `activity_states.bin`, curated encounter TOMLs |
| slayer | `slayer.bin` |
| active area | world NPC spawns via `rc_world_activate_area`; lower-level rect/near loaders remain available for tools/tests |
| (audio → rc-viewer) | `music.bin` |

If a subsystem is disabled in the config, its binaries are never
opened. No "load everything just in case" — we pay only for what we
run.

---

## 10. Tick-path discipline

Rules for every function on the tick path (`*_tick`):

- **No malloc / free / realloc.** Everything is preallocated.
- **No file I/O, no syscalls** other than the tick clock read.
- **No logging to stdout/stderr.** If logging is needed, write to an
  in-memory ring buffer drained by a separate thread (opt-in).
- **No shared mutable globals.** All state lives on `RcWorld` or in
  `_Thread_local` scratch.
- **No recursive event dispatch within one tick** (see §7).
- **Scratch buffers** (pathfinding queues, visibility arrays, etc.)
  live in `_Thread_local` static arrays inside their owning function.
  Per the FC lessons memory, shared `static` scratch caused segfaults
  under OpenMP parallelism — enforce `_Thread_local`.

---

## 11. Types and headers

- `types.h` contains **only** base types: `RcWorld` (partial, just
  enough that subsystem headers can embed their structs in it),
  `RcPoint`, `RcNpcId`, `RcItemSlot`, collision flag constants.
- Each subsystem has its own header: `combat.h`, `prayer.h`,
  `loot.h`, etc. — defining its state struct, its public API, and
  the events it emits/consumes.
- `RcWorld` is defined in `types.h` by including each subsystem's
  state-struct header. Circular include is broken with forward
  declarations + struct-by-value inclusion.
- No subsystem header may transitively require another subsystem's
  header to compile a base-only world.

Enforcement: add a CMake target `test_base_only` that builds
`rc-core` with every subsystem disabled, run a smoke test, and fail
if any subsystem's code is linked in.

---

## 12. Concurrency model

- One `RcWorld` = one independent simulation. Worlds share nothing.
- `rc-core` functions operate on the `RcWorld *` argument and must
  not touch any global mutable state.
- Parallelism is the caller's responsibility: spawn N worlds across
  N threads; each runs independently.
- Per-tick scratch uses `_Thread_local` (C11) so parallel worlds on
  separate threads never collide on scratch memory.
- No locks inside `rc-core`. If a caller needs cross-world
  coordination (e.g. RL rollout aggregator), it handles that
  externally.

---

## 13. Determinism

Determinism is an engine property for reproducible RL rollouts and
repeatable automated correctness checks.

Given the same `RcWorldConfig` + initial state + input sequence, the
world must produce byte-identical output state. Requirements:

- RNG is a field on `RcWorld` (`rng_state`). No system calls like
  `rand()` or `time()` on the tick path.
- Iteration order over NPCs / items is fixed (by array index, not
  by insertion time or hash).
- Floating point is avoided where possible; integer math + fixed-
  point (`rarity_inv` pattern) for rates and ratios.
- No reliance on thread scheduling — each world is single-threaded
  internally.

---

## 14. Throughput budget

Target: **tens of millions of ticks/sec** aggregate across all threads.

Per-tick cost budget for a combat-only sim on one thread:
- Tick rate: ~1M ticks/sec per thread (single-thread target)
- Budget per tick: ~1000 ns (1μs)
- Pathfinding for 10 NPCs: ~300 ns (30 ns each amortized)
- Combat resolve for 3 active fights: ~100 ns
- Hot-array NPC tick for 10 NPCs: ~200 ns
- Slack: ~400 ns for overhead, cache misses, player tick

At 64 parallel worlds × 1M tps/thread = **64M tps aggregate** on a
64-core host. Modulate as the target requires.

If a subsystem's tick cost breaks this budget, profile it and either
(a) optimize, (b) make it lower-frequency (skip N ticks), or (c)
accept a lower per-thread ceiling.

---

## 15. What does NOT go in `rc-core`

- Rendering, window management, input (rc-viewer).
- Asset decoders (cache parsers, model/terrain/atlas loaders — these
  are `tools/` scripts producing binaries for rc-core).
- Network, multiplayer, save serialization (not in scope yet; when
  added, save goes through an explicit `rc_world_serialize` boundary
  — subsystems expose their own serializers).
- Logging to console (use the opt-in ring buffer instead).
- Any std input, anything platform-specific beyond libc + pthread.
- **OSRS-specific content** — boss scripts, per-quest state machines,
  region-specific NPC code. These live in `rc-content/` (see §18).
  `rc-core` may mention "encounter" as a **mechanism** but never
  "Scurrius" or "Cook's Assistant" by name. If engine code needs to
  special-case a specific content instance, that's a smell — make it
  data-driven or push the logic to `rc-content`.

---

## 16. Enforcement checklist (for PRs touching rc-core)

Before merging any change to `rc-core`:

1. `grep -r "malloc\|calloc\|free" rc-core/*_tick.c` → must be empty.
2. `grep -r "static " rc-core/*.c | grep -v "_Thread_local"` → review
   every hit; globals without `_Thread_local` are a red flag.
3. `grep -r "fprintf\|printf\|puts" rc-core/*_tick.c` → must be empty.
4. Does this subsystem compile when its `RC_SUB_*` flag is off?
   Build `test_base_only` target.
5. Does any subsystem reach into another subsystem's state struct?
   Should be events instead.
6. New types: are they in the right header (base vs subsystem)?
7. New state: handle (index) or pointer? Handle unless there's a
   documented reason.

---

## 17. When this document and reality disagree

If you find `rc-core` code that violates one of these rules, the
code is wrong, not the document. Fix the code or update this document
first if the rule itself is what needs to change.

---

## 18. Engine / content boundary

`rc-core` is the **generic game engine**. It ships with tick,
pathfinding, combat, prayer, encounter *mechanism*, etc. — but
zero OSRS-specific content. Every boss, every quest, every
region-specific behavior lives in **`rc-content/`** (a separate
static library that depends on `rc-core`).

### The split rule

| Code | Lives in | Why |
|---|---|---|
| Tick dispatch, event bus, pathfinding, combat math, subsystem handles | `rc-core/` | Engine — content-agnostic. |
| Generic encounter primitives (`telegraphed_aoe_tile`, `spawn_npcs`, `drain_prayer_on_hit`, etc. — used by many bosses) | `rc-core/encounter_prims.c` | Shared mechanism, not content. |
| Encounter script registry and trigger dispatch | `rc-core/encounter.c` | Generic routing by name/type; script bodies stay in content. |
| Encounter attack tables, targetability, damage modifiers, and protection scaling | `rc-core/encounter.c` + `rc-core/combat.c` | Generic phase-owned attack selection, phase targetability, player damage scaling, attack effects, and prayer damage scaling. Boss-specific attacks stay in data. |
| Regular NPC tag consumers for generic combat rules | `rc-core/combat.c` | Generic mechanics such as breath mitigation and damage gates. |
| Activity-mechanics behavior/profile dispatch | `rc-core/activity_mechanics.c` + `rc-core/combat.c` | Generic data-driven owner effects; full boss scripts stay in content. |
| Typed activity state-machine loading and `RcActivityRun` stepping | `rc-core/activity_states.c` | Shared activity flow mechanism; object events validate loaded activity anchors and multi-boss runs count unique deaths. |
| Activity schema indexing | `rc-core/activity_schemas.c` | Loadable summary of encounters, typed activities, NPC IDs, object IDs, and activity-local arena/spawn metadata. |
| Activity-local spawn/anchor lookup | `rc-core/activity_spawns.c` | Exact activity-local points, dynamic pools, wave-filtered NPC materialization, wave-region resolution, object anchors, and explicit unresolved source blockers. |
| Provisional area-flag lookup | `rc-core/area_flags.c` | Sparse mapsquare-indexed flags for wilderness, wilderness level, multicombat, singles-plus, and safe-zone behavior. Rows are provisional until authoritative OSRS geometry is sourced. |
| Normalization and canonical form lookup | `rc-core/normalization.c` | Shared item form, NPC alias, and source-name join support; exact IDs stay available. |
| Coordinate-explicit object transport consumption | `rc-core/tick.c` + `rc-core/traversal.c` | Generic object action dispatch can consume source-backed traversal edges when callers provide exact object tiles. |
| Generic object consumers | `rc-core/tick.c` + `rc-core/skills.c` | Prayer altars restore prayer from object behavior data; doors record per-world open state; resource objects start, deplete, and respawn source-backed gathering nodes. |
| Slayer task loading, assignment filters, amount selection, and kill-credit routing | `rc-core/slayer.c` | Generic task owner; exact quest/location/boss-task edge rules come from data/content. |
| Boss-specific scripts (`scurrius_heal_at_food_pile`, `kq_shed_exoskeleton`) | `rc-content/encounters/<boss>.c` | Content — one boss only. |
| Quest state machines (Cook's Assistant, Dragon Slayer II) | `rc-content/quests/<slug>.c` | Content — one quest only. |
| Region-specific NPC / object behavior | `rc-content/regions/<region>.c` | Content — one region only. |
| Pure data (stats, drops, attack TOMLs, items, prayers, spells, action gates) | `data/defs/*.bin` + `data/curated/` | Data, not code. |

### What this enables

1. **Isolated-sim build targets.** Link `rc-core` + a specific
   subset of `rc-content/` modules to produce a binary that only
   contains the content you need (e.g. Colosseum-only, Scurrius-only).
   RL training workloads that only care about one encounter don't
   pay compile or runtime cost for the other 49.
2. **Engine reusability.** `rc-core` could, in principle, drive a
   different game with the same tick / combat shape.
3. **Content boundaries.** "How does Scurrius work?" → open one
   file in `rc-content/encounters/`. No grep across `rc-core/`.

### Enforcement

- **rc-core may not `#include` anything from rc-content.** One-way
  dependency.
- **rc-core may not mention a specific content instance by name.**
  Grep check: `rg -w "scurrius|kalphite|vorkath|zulrah|..." rc-core/`
  → must be empty.
- **rc-core may not infer gameplay from item or NPC display names.**
  Generated IDs, tags, params, and content callbacks are the allowed
  boundary. If a rule needs a specific item family or NPC family,
  export that family into data first.
- **rc-content register fns are called by the caller** (viewer,
  tests, sim main), not by rc-core. rc-core stays content-agnostic
  even at init.

See `rc-content/README.md` for the content-side design doc + per-
category conventions (encounters, regions, quests).
