# rc-core — architecture principles

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

Planning lives in `work.md`. This file is about architecture and
component behavior, not roadmap sequencing.

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
- World tiles + collision flags
- Region loader (terrain / objects / collision)
- Player position + route
- NPC position + route
- Pathfinding (BFS + LOS)
- Tick counter + RNG state
- Varbit state (empty if no subsystem uses varbits)

### Subsystems (individually opt-in)
- **combat** — hit rolls, damage, pending-hit queue, protection prayers
- **prayer** — active prayers, drain, boosts
- **equipment** — worn items, equipment bonuses, weapon stance
- **inventory** — 28-slot inventory, stacks, item ops
- **consumables** — food heal, potion boost/drain
- **loot** — drop table rolls, ground items, pickup
- **skills** — XP table, level-up, per-skill state
- **quests** — quest state machine, varbit triggers
- **dialogue** — NPC dialogue state trees
- **shops** — shop stock, buy/sell, restock
- **slayer** — task assignment, task progress
- **encounter** — boss phase/rotation dispatcher + primitive registry
- **audio** — track selection / region music (lives in rc-viewer;
  listed here so base doesn't accidentally hold audio state)

A Colosseum / Inferno simulator needs: **base + combat + prayer +
equipment + inventory + consumables + encounter**. Nothing else.

---

## 3. `RcWorldConfig` drives bring-up

```c
typedef struct {
    uint32_t subsystems;     // bitmask: RC_SUB_COMBAT | RC_SUB_PRAYER | ...
    RcRegion *regions;       // regions to load (or NULL = deferred)
    const char *npc_defs_path;
    const char *items_path;
    // ...per-subsystem asset paths; NULL means "this subsystem must
    // be disabled in the bitmask"
} RcWorldConfig;

RcWorld *rc_world_create(const RcWorldConfig *cfg);

// Presets:
RcWorldConfig rc_preset_full_game(void);
RcWorldConfig rc_preset_combat_only(void);   // Colosseum / Inferno sim
RcWorldConfig rc_preset_skilling_only(void);
```

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

Each subsystem owns its binary(s):

| Subsystem | Binaries it loads |
|---|---|
| base | `npc_defs.bin`, `varbits.bin`, region files, spawn files |
| equipment / inventory | `items.bin` |
| loot | `drops.bin`, `skill_drops.bin`, `rdt.bin`, `gdt.bin`, `mrdt.bin` |
| quests | `quests.bin` |
| dialogue | `dialogue.bin` |
| shops | `shops.bin` |
| skills | `recipes.bin` |
| encounter | `encounter.bin` (Phase 4) + per-encounter TOMLs |
| slayer | `slayer.bin` |
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
| Boss-specific scripts (`scurrius_heal_at_food_pile`, `kq_shed_exoskeleton`) | `rc-content/encounters/<boss>.c` | Content — one boss only. |
| Quest state machines (Cook's Assistant, Dragon Slayer II) | `rc-content/quests/<slug>.c` | Content — one quest only. |
| Region-specific NPC / object behavior | `rc-content/regions/<region>.c` | Content — one region only. |
| Pure data (stats, drops, attack TOMLs, items, spells) | `data/defs/*.bin` + `data/curated/` | Data, not code. |

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
- **rc-content register fns are called by the caller** (viewer,
  tests, sim main), not by rc-core. rc-core stays content-agnostic
  even at init.

See `rc-content/README.md` for the content-side design doc + per-
category conventions (encounters, regions, quests).
