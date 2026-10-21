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

Planning and task tracking are kept in local Markdown files. This README is
the tracked technical overview of the component boundary, architecture rules,
and runtime invariants.

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
not belong in `rc-core`. Rendering, camera, UI, sprites, textures,
model IDs, spotanim metadata, projectile visuals, and animation
playback live in `rc-viewer`. When a generated gameplay binary also
carries render metadata, core may consume and skip those bytes to
preserve binary compatibility, but it must not expose them through
core structs or APIs.

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
  prayers, selected spell max-hit hints, hit-delay profiles, and logical
  attack events for presentation consumers
- **prayer** — data-backed active prayers, drain, conflicts, boosts
- **equipment** — worn items, equipment bonuses, weapon stance
- **inventory** — 28-slot inventory, stacks, item ops
- **consumables** — food heal, potion boost/drain
- Consumables use `consumables.h`: queued generation-checked Eat/Drink, atomic
  replacements, resource effects and player-owned deadlines. Register reviewed
  definitions through `rc-content` after creation/reset. Unsupported cache
  actions reject without consuming. See [the contract and support inventory](../docs/consumables.md).
- **loot** — drop table rolls, ground items, pickup
- **skills** — XP table, level-up, recipes, skill-drop sources, and
  gathering-node lookup
- **quests** — quest state machine, varbit triggers
- **dialogue** — NPC dialogue state trees
- **shops** — shop stock and static price/restock metadata
- **storage** — validated bank/deposit-box sessions and atomic bank transfers.
  Open through the normal placed-object/NPC interaction, not a direct open API.
  Queued withdrawals identify a surviving stack; quantity changes alone do not
  invalidate other clicks. Zero means All, negative amounts reject. Deposits
  gather matching inventory items while preserving separate scalar states.
  `rc_bank_withdraw_slot_mode` requests Item/Note, using reciprocal item links.
  Its keep-one option evaluates the remaining quantity at execution time.
  `rc_bank_deposit_all` deposits current inventory or equipment in one queued,
  session-checked transaction, preserving states and refreshing equipment
  bonuses/unequip events. Unbankable items stay carried; partial results are explicit.
  Partial capacity moves only the amount that fits. `player.bank_result` gives
  completed amount and a sequenced reason; queue admission is not completion.
  Content supplies exact option bindings/bankability; missing policy rejects.
  `rc_bank_add_item[_tab]` is privileged fixture setup, not player access.
- **slayer** — task metadata, assignment, unlock/block/prefer filters,
  task progress
- **encounter** — admitted boss runs: forms, engagement, phases, attack
  selection, owned helpers and hazards (pools that land after their spit and
  show their loc, helpers that land after their launch), and the verified
  primitives
- **objects** — object definitions, placed-object lookup, typed object
  behavior tags, complete varbit/varp transforms, exact layer-aware dynamic
  loc mutation, altar restore, doors, and gathering-node depletion/respawn
- **traversal** - indexed traversal facts, explicit destination-policy
  resolution, generation-owned traversal phases/outcomes, and checked player
  relocation. Only a source-owning object/item/spell/activity handler may
  admit executable travel; installed graph rows alone do not grant an action.
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
    uint32_t seed;           // zero selects the documented deterministic default
    int npc_capacity;        // dense active-NPC slots owned by this world
    RcWorldStreamingConfig streaming;
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

Creation validates known subsystem bits, preset capacity, and required data
paths before loading. Successfully loaded data determines the capabilities a
world may claim; an enabled subsystem with missing required data fails at
startup with a diagnostic instead of remaining partially initialized.

The process publishes one immutable `RcGameData` authority and shares it among
compatible worlds. Repeated loads with the same subsystem/path/backend identity
retain that object. A different identity while worlds are live fails explicitly
rather than switching global definition views underneath existing worlds.
Callers that need another runtime-data identity must finish the current worlds
or use a separate process.

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

World positions obey one shared contract in `coordinates.h`: X and Y are
global 14-bit OSRS tiles in `0..16383`, planes are `0..3`, mapsquares are
64-by-64, and zones are 8-by-8. Checked helpers own mapsquare/zone keys,
representable inclusive rectangles, clipped scan windows, generic footprints,
same-plane Chebyshev separation, and eight-direction deltas. Data loaders and
public mutation APIs reject invalid positions, planes, or footprints before
narrowing or changing world state; high bits are never discarded to make a
bad coordinate appear valid. Existing entity fields remain signed global
integers, and existing binary layouts remain unchanged.

Core never stores render-local positions or a camera origin. A frontend may
project global positions for presentation, but it must not write those
presentation coordinates back as gameplay state.

Every preset carries the same initial backend streaming policy: an active
radius of two mapsquares and a 64-page cache limit. The fixed collision map
supports at most this `5x5` active window. Visual lookahead is a separate
`rc-viewer` policy; core has no preload-radius setting.

`rc_world_activate_area` stages one complete world-owned activation profile for
the enabled subsystems: collision, NPCs, static and dynamic ground items, and
object-placement cache warming. Only records in mapsquares intersecting the
requested area are read. The map, active entities, dormant records, descriptor,
generation, and telemetry publish together only after all capacity and load
checks pass; failure leaves the prior authoritative state unchanged. An
identical request is idempotent. `rc_world_activate_area_around` applies the
configured radius and is used by core movement and relocation before player
coordinates enter another mapsquare. Object definitions, behaviors, and
transports stay global while spatial instances live in bounded LRUs. The
viewer and headless agents therefore observe the same backend transition.
Scenario/dev validation code that needs a guaranteed target uses
`rc_world_ensure_npc_near`; presentation frontends should not resolve NPC
definitions or mutate `world->npcs` directly.

Indexed NPC and static-ground-item records carry stable identities derived from
their source path and source ordinal. Before replacement, `rc-core` preserves
the complete live state of source-backed NPCs so overlapping and returning
entities keep identity, health, timers, pending work, facing, and combat state.
Runtime-created NPCs inside the destination remain active; those leaving are
removed explicitly, which clears stale player/NPC references and retires any
encounter slot or effect owned by the removed UID. Static item overrides and
dynamic ground items use their existing stable keys and UIDs.

Dormant records are not scanned on every tick. They store the world tick at
which they left and reconcile observable deadlines on return. NPC death and
respawn, and ground-item reveal and despawn, therefore advance by elapsed world
time. Expired dynamic items are discarded deterministically. Eligible state
that cannot fit the active capacity fails the staged activation instead of
silently disappearing.

The generic dormant NPC store owns indexed world spawns. NPCs created by an
encounter or script have no indexed spawn identity and remain owned by that
subsystem's lifecycle. Dynamic ground items do have generic dormant ownership
and are restored by UID when their area becomes active again.

NPC creation enters through checked `rc_npc_spawn_ex`. It validates the
definition, coordinate, plane, footprint, direction, capacity, and UID range,
and assigns source identity before publishing the entity. Indexed source keys
make repeated slice loads idempotent. `rc_npc_remove` clears cross-system
references and releases the slot; the next occupant receives a new monotonic
world UID.

`rc_npc_reset_life` is the single initial-spawn/respawn baseline. Core owns the
alive, dying, hidden, respawned, and removed phases and publishes lifecycle
events only after each state is complete. NPC policy submits wander, chase, and
return routes to `rc_npc_movement_tick`, so one footprint-aware step owner moves
an NPC each cycle. Ordinary travel tries a direct diagonal, then X, then Y,
without searching obstacle detours. Blocked pursuit can resume when its target
or collision changes. Obstacle-searching routes require an explicit
`RC_NPC_ROUTE_SCRIPTED` request; this is not a blanket boss policy. NDEF v5
stores explicit lifecycle, hunt, regeneration, and
transform policy; the active definition is resolved from the world's
varbit/varp state. The viewer may animate and draw these phases/forms but does
not own them.

`rc_world_get_streaming_telemetry` returns the latest collision/spatial-page
and full active-area timings, loaded page count, active NPC count, and active
ground-item count. It also reports resident dormant NPC/ground-item counts and
the number saved, restored, or expired by the latest activation. Spawn and
object-placement stats expose indexed pages and rows read, plus resident
collision and object pages/rows. Measurement is confined to area activation
and does not add work to the tick path.

The NSPI/GSPI v1 files use a fixed 65,536-entry mapsquare directory. Each
entry stores the first record and count for `(region_x << 8) | region_y`.
Records carry their original source ordinal; `rc-core/spawn_index.c` reads
only selected page ranges and restores that order before subsystem loaders
apply exact tile/plane filters.

Current GSPI records are 26 bytes, including the source ordinal and a trailing
uint32 respawn interval in ticks. The ground-item loader rejects the old
22-byte shape; rebuild the spawn assets and matching pack together. Static
respawn and delayed arrival use the same elapsed-time owner as active/dormant
ground-item expiry. Ground batches reserve capacity before item transactions
commit; `RC_EVT_DROP_GRANTED` acknowledges reservation (not visible arrival),
and reports the full uint32 quantity and pile UID. Loot-table source migration
is still in progress; these lifecycle changes do not validate legacy odds.

DROP v2 adds per-table source-rejection flags. Rejected definitions contain no
reward entries and return `RC_LOOT_ROLL_UNVERIFIED` before RNG or grants.
Death preparation records the failure and prints the NPC/reason once. See
`content/loot/rejections.txt` for the named revisit inventory. The production
snapshot installer applies that manifest; raw DROP v1 runtime files are
rejected. Non-rejected legacy tables and shared-table v1 assets still await
the exact-roll/source migration and are not certified OSRS parity.

The OPLI v1 object-placement file uses the same fixed mapsquare directory.
Placement records remain in source order. `rc-core/objects.c` range-reads pages
on demand and retains at most `max_cached_regions` pages; interaction lookup
uses a direct mapsquare-to-cache-slot table.

The CTPI v1 collision file is
`data/regions/world.collision-tiles.indexed.bin`. It uses a fixed 65,536-entry
mapsquare directory followed by sparse collision records. `rc-core/collision.c`
range-reads requested mapsquares, expands each resident page to the existing
dense four-plane query layout, and retains at most `max_cached_regions` pages.
Collision queries therefore remain constant-time without loading the complete
world table.

`RcAssetReader` provides bounded reads over loose files and stored pack
entries. Indexed spatial files are packed without compression so the pack
backend seeks directly to directory and record ranges. A packed range reader
rejects compressed entries rather than silently materializing the whole asset;
ordinary compressed assets continue to use `rc_asset_fopen` or
`rc_asset_read_all`.

Object interactions that originate from placed scene data should pass the
placement key into core. Placement-key APIs make dynamic loc state local to one
exact placed object; tile/id APIs are compatibility paths for tests and tools
that do not have placement identity.

`rc-core/object_runtime.c` owns dynamic loc identity and lifecycle. Static
placements use their immutable key and loc layer; spawned locs receive a
world-local key. Add, delete, replace, timed revert, and collision replay all
use this owner. Transient overrides are reclaimed when they revert, and
capacity or identity conflicts are reported explicitly. The viewer reads the
resolved active placement but never mutates object state.

`rc-core/items.c` owns player inventory/equipment mutation. Production callers
stage changes with `RcItemTransaction` or call the typed player item APIs;
direct fixed-array helpers are limited to fixtures and private staged copies.
Every committed slot has a valid definition, positive bounded quantity,
compatible `state_id`, and a changed-slot generation. Delayed commands retain
that generation, failed transactions publish a typed reason without partial
mutation, and successful commits publish container revisions plus derived
weight/bonus/style state once. IDEF v3 is the current metadata authority;
legacy v1/v2 files remain readable but do not define the current export
contract. `rc-viewer` projects core state and implemented actions and must not
invent item gameplay or mutate slots directly.

Config is consumed **once** at world creation. After that, no path or loader
configuration appears on the tick path. The enabled bitmask gates the tick
dispatcher and public subsystem entry points that must reject disabled work:

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

Public gameplay requests made outside `rc_world_tick` are admitted to one
fixed 32-entry player-command queue. They execute in insertion order during the
next tick's phase-1 input pass; they do not mutate authoritative gameplay state
in the caller's render frame or headless control step. Admission and execution
publish explicit queued, executed, invalid, full, dead, busy, or cancelled
results. A full queue never falls back to immediate execution.

Movement uses one world-owned collision and route contract. Missing, invalid,
or unknown tiles fail closed; a loaded sparse mapsquare window can explicitly
represent open tiles. `rc_find_route` accepts exact, rectangle/range/LOS, and
wall-reach targets and returns a reached endpoint, traveled cost, and explicit
failed, blocked, already-arrived, exact, alternative, or partial status.
Bounded partial routes are source-connected and retain their target for
deterministic continuation.

Route search and execution share the same cardinal/diagonal validator for
size-one and rectangular footprints. Projectile LOS likewise evaluates both
footprints, directional projectile walls, full blockers, and endpoints.
Dynamic doors mutate movement and projectile clipping together. Player walk,
run, directional step, interaction approach, and combat approach all install
routes through the same atomic admission path; a failed request does not erase
a valid route or unrelated action.

Running is core-owned. Each run tile is a separately validated ordered substep,
the second substep requires positive energy, energy drains only when it occurs,
and otherwise recovers within bounds. The viewer may observe routes for
animation and prefetch but never writes route arrays or gameplay run mode.

`RcPlayerActionState` records the current movement, interaction, combat,
traversal, skill, or modal owner. Soft/background commands can coexist, normal
commands replace through `rc_player_cancel_action`, and an unexpired strong
action rejects non-soft replacement. Central cancellation clears commands,
routes, interactions, combat, manual casts, traversals, skills, and storage.
Only `rc_world_tick` advances the complete world schedule.

Traversal admission snapshots exact source and approach identity, resolves one
destination through an explicit policy, and hands the interaction to a strong
generation-owned action. Core publishes approach, takeoff, transit, relocate,
and landing events plus exactly one success/failure/cancellation outcome.
Unsupported item/spell/instance rows and ambiguous imported object rows fail
openly until their owning content provides requirements and destination
policy. The viewer can observe these events but cannot resolve or apply travel.

Generic interactions have the same single-owner rule. An accepted target gets
a monotonic generation and a stable source/target snapshot. Late execution
revalidates inventory/equipment generations, ground-item UID/version, exact
object placement, NPC identity, and current spellbook before invoking a
world-local handler. Handler results apply only to the generation dispatched,
so replacement work cannot be cleared by an old callback. Required engine
defaults install during world creation/reset, handler precedence is
deterministic, and setup fails when the bounded registry cannot hold them.

Handlers perform their owning subsystem mutation and return status only. Core
publishes terminal failures and messages as a sequenced
`RcInteractionOutcome`; frontends may present that state but do not decide the
gameplay result. Player-to-player targets and authoritative widget masks are
not accepted through partial fallback paths while their owning repositories do
not exist.

`RcTick` is a 64-bit monotonic cycle value. Delayed work stores an explicit
start or absolute ready/expiry tick, so an N-tick delay has the same boundary
meaning regardless of which phase created it. Pending-hit and command capacity
failures are observable rather than silently dropping gameplay work.

Combat target selection does not start the opponent's action. A launch checks
reach and bounded hit/event capacity before committing its resource cost,
cooldown, XP and attack event. Cancelling one actor does not cancel the other.
NPCs retain one selected attack while chasing and repeat at their declared
tick interval. Melee uses shared cardinal/line-of-walk reach; ranged and magic
use projectile LOS. Area flags supply multi-combat policy, with the explicit
world override retained for configured encounters and tests.

Reach is measured against the NPC's whole footprint (its tile plus its
`npc_defs.bin` size east and north). Ordinary melee needs a tile sharing a
side with it; a player under the footprint must step out first. Before the
player steps (phase 4), `rc_combat_player_approach` stops an attacker who is
already in reach (clearing any remaining route) and otherwise re-paths toward
the target's current footprint when the player is standing or the footprint
moved, so a moving NPC is followed the same tick and never leaves the player
on a stale route. The attack after movement stays the authority: it keeps a
route that already leads to the current footprint instead of searching each
tick and does not repeat a search the approach lost that tick. The attack
records what decided an in-reach verdict (target slot, both tiles, weapon,
attack style and spells); while that still matches on the next tick a standing
attacker skips the pre-step check. A change it does not key on (a door) only
delays the next step by a tick.

`rc_combat_set_auto_retaliate` queues the requested boolean, not a toggle.
Disabling retaliation prevents a new automatic attack but does not cancel an
existing target. Content supplies ranged weapon/ammo compatibility and the
resource slot; core validates and consumes that slot through item transactions.
Weapons not using the ammo slot cannot gain its ammunition bonuses. Unsupported
charge containers fail explicitly, without spending unrelated ammunition.

Magic preparation accepts either a selected spell or a content-owned native
weapon attack. Content validates compatibility and prepares the calculation;
core owns capacity checks, resource payment and launch. Manual casts are one
shot; autocast retains the target. Pending hits retain spell/weapon identity
for landed content effects, while attack events expose accuracy and hit count
without presentation metadata. NPC immobilization uses absolute world ticks.

Encounter actions are named, never drawn, by core: an encounter attack's event
carries `RC_COMBAT_ACTION_ENCOUNTER` and the attack name (tile attacks aim at
the launch tile, `target_kind = RC_COMBAT_ACTOR_NONE`), and mechanic parts and
form changes record presentation-only events through
`rc_combat_record_encounter_action` ("<mechanic>[:<part>]", `form:leave`,
`form:enter`; `docs/encounter_primitives.md`). Those records leave 16 event
slots free for launches. Presentation maps names to visuals from data.

Spell selection/autocast/book APIs return `RcSpellResult`; queued admission is
not execution. `rc_spell_available` checks the bound world's definitions,
book, supported type, levels, membership, weapon and resource inputs. The
source-owning handler still validates its actual target/executor. Rejections
carry a reason; dead actors and full queues are separate outcomes.
Accepted spellbook casts award stored base Magic XP once after payment,
including splashes; damage XP remains separate. Native powered shots receive
no spellbook base XP. Existing item transactions now snapshot/commit pouch
contents with inventory/equipment, rejecting stale state. Core-only modular
worlds retain inventory-only spell payment; a registered content owner is
authoritative and never falls back after rejecting a payment.

Content can supply bounded individual gameplay hit delays. Core reserves and
pays the full batch, queues those arrival ticks and emits each hit's delay and
accuracy for clients. A real one-tile weapon range stays one; melee staff reach
is distinct from spell casting, and long-range cannot exceed ten tiles.
Secondary-target batches and fired-ammo recovery are not implemented here.

Rolls use live NPC stats. Accuracy is a separate pending-hit fact from damage.
Player attack XP captures the launch stance and target-HP cap, retaining
hundredths; impacts do not award XP again. NPC damage is capped to remaining
HP. Player hit records retain rolled damage, while damage events report actual
HP lost (`damage_tenths`) after mutation. Ordinary protection uses the existing
queue-time snapshot; no universal impact-time prayer rule is implied.

Prayer points are stored in tenths. `prayer.c` owns all refill, restore, drain
and active-set writes; content uses `rc_prayer_drain_points`,
`rc_prayer_drain_tenths`, and `rc_prayer_restore_points` with explicit units.
Player toggles and quick-prayer requests use the soft command queue and return
typed admission/execution outcomes. PRAY v2 definitions supply requirements,
conflicts, supported effects and unlock replacements; v1 requires regeneration,
not a compatibility fallback. Presets are members worlds; `members_world=false`
enforces free-world restrictions. Combat's preset loads unlock varbits/varps.
HP regeneration, drained-stat recovery and boosted-stat decay have separate
timers for Rapid Heal, Rapid Restore and Preserve. Protect Item, Retribution
and Redemption are explicitly unavailable pending their downstream rules;
ordinary NPCs do not gain artificial Prayer pools for Smite.

Player respawn clears old hits, statuses and active/dormant NPC targeting.
NPC removal/new-life reset cancels its pending melee and detaches surviving
ranged/magic hits from source-dependent effects and retaliation. Those hits
retain their damage and ordinary protection snapshot. Exact orphaned boss
effects, per-attack travel timing, NPC bonus data and specialty weapon policy
remain explicitly scoped in the systems audit; they are not accepted parity.

`rc_world_reset` is the supported in-place rollout reset. It keeps the world's
immutable game data, allocated NPC storage, enabled subsystem set, streaming
policy, configured spawn path, and initial seed, while destroying dormant and
other owned mutable buffers and rebuilding initial gameplay state. Content
installed through `rc_world_install_content` (hooks, handlers, encounter
scripts) is installed again after the reset. Raw byte copies of `RcWorld` are
not snapshots and are unsupported.

---

## 4. State layout — bounded mutable worlds, shared immutable data

Mutable gameplay state is world-owned. Most bounded state remains inline, but
the dense active-NPC array and dormant persistence stores are owned dynamic
buffers. NPC capacity is selected by the preset or caller: 64 slots for base,
1,024 for simulation/skilling, and 4,096 for the full viewer by default.
Disabled subsystem fields remain present but are not ticked.

```c
typedef struct {
    // Base (always present, always valid)
    RcWorldMap map;
    RcPlayer player;
    RcNpc *npcs;                 // npc_capacity dense slots
    int npc_capacity;
    RcTick tick;
    uint32_t rng_state;
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

The measured x86-64 budget is 2,572,728 bytes for the fixed world body. Including
the active-NPC allocation, default base, simulation, and full worlds consume
approximately 2.64 MB, 3.69 MB, and 7.03 MB respectively, before dormant state
that exists only when populated. This replaces the previous 35.2 MB mandatory
NPC allocation while retaining dense iteration.

Allocation is permitted during create, reset, area activation, and destroy.
It remains forbidden on the normal tick path. `RcGameData` owns definitions and
other immutable runtime tables once per process rather than once per world.

---

## 5. Handles, not pointers, across subsystems

When one subsystem references an entity in another, use an integer handle,
never a stored `*` pointer. NPC handles are world-local durable UIDs, not array
indices; resolve them through `rc_npc_resolve` or `rc_npc_resolve_const`.

```c
typedef uint32_t RcNpcId;
typedef uint8_t RcItemSlot;
typedef uint16_t RcGroundItemId;

// GOOD — combat stores the target as a handle:
typedef struct {
    RcNpcId target;   // RC_NPC_NONE = no target
    // ...
} RcCombatState;

// BAD — pointer into an array that may be replaced or compacted:
// RcNpc *target;
```

NPC UIDs increase monotonically for the lifetime of a world and are not reset
by `rc_world_reset`, so a stale UID cannot resolve to a replacement NPC after
slot reuse. Item and ground-item slot handles retain their owner-specific
contracts.

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
    RC_EVT_NPC_REMOVED,
    RC_EVT_PLAYER_DAMAGED,
    RC_EVT_ITEM_PICKED_UP,
    RC_EVT_DROP_GRANTED,
    RC_EVT_PHASE_TRANSITION,
    RC_EVT_DIALOGUE_OPENED,
    RC_EVT_QUEST_STAGE_CHANGED,
    // ...
};

int rc_event_subscribe(RcWorld *w, int evt, RcEventFn fn, void *ctx);
int rc_event_fire(RcWorld *w, int evt, const void *payload);
```

**Rules:**
- Events fire **episodically** — deaths, drops, phase shifts, quest
  stage changes, dialogue transitions. Hundreds per second at most.
- **Never** fire an event per tick per entity. NPC tick loop, combat
  damage resolve, pathfinding step — all direct calls.
- Null/duplicate registrations and same-event reentry fail in release builds.
- Dispatch uses a stable handler snapshot. Subscriptions changed by a handler
  take effect on the next event and cannot skip or add calls mid-dispatch.
- A disabled subsystem never subscribes, so its handlers never fire
  and its code never runs.

**Rationale:** events are the clean decoupling mechanism. Keeping
them episodic means the dispatch cost is negligible aggregated over
a tick, and the hot path stays tight.

Current encounter events:
- `RC_EVT_NPC_SPAWNED` / `RC_EVT_NPC_RESPAWNED` bind runs;
  `RC_EVT_NPC_DIED` / `RC_EVT_NPC_REMOVED` end them or forget helpers.
- `RC_EVT_PLAYER_DAMAGED` applies the landed attack's effect, identified by
  the hit's encounter attack and run generation.
- `RC_EVT_NPC_ATTACK` counts launched attacks for the special rotation.
- `RC_EVT_PLAYER_DIED` disengages runs.
- `RC_EVT_PHASE_TRANSITION` is a notification only; `enter_phase` owns
  phase changes and runs their mechanics and scripts directly.

---

## 8. No cross-subsystem reach-through

- `combat.c` may not call loot code directly. On a final death `tick.c`
  prepares loot and then fires `RC_EVT_NPC_DIED`; an encounter's nonterminal
  form change is intercepted first (`rc_encounter_intercept_lethal`), so it
  gets neither.
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

## 9. Binary loading — per-subsystem, immutable startup

All runtime data paths resolve under the local ignored `data/` runtime install.
The main RuneC repository owns loaders, runtime code, source content, schemas,
and tooling; generated binaries, sprites, models, regions, and packs are local
or release artifacts rather than source files in the main repo.

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
| storage | `items.bin`, `npc_defs.bin`; object definitions/placements for object access; registered content policy + per-world bank state |
| traversal | `traversal_edges.bin` |
| regions | `world.collision-tiles.indexed.bin`, `area_flags.bin` |
| skills | `recipes.bin`, `skill_drops.bin`, `gathering_nodes.bin` |
| combat / slayer / encounter | `regular_npc_mechanics.bin` |
| encounter | `encounters.bin` (ENCT v13, admitted specs only), `activity_schemas.bin`, `activity_spawns.bin`, `activity_mechanics.bin`, `activity_states.bin` |
| slayer | `slayer.bin` |
| active area | mapsquare-indexed collision, world NPC/static ground-item spawns, and object placements via `rc_world_activate_area`; lower-level NPC rect/near loaders remain available for tools/tests |
| (audio → rc-viewer) | `music.bin` |

If a subsystem is disabled in the config, its binaries are never opened. The
selected tables are loaded and validated before the immutable `RcGameData` is
published. Asset backend/root configuration is likewise startup-only after the
first lookup. Pack discovery builds a temporary catalog and publishes it only
after every pack, range, compression type, path, and duplicate check succeeds.
`rc_asset_reset` exists only for explicitly single-threaded tests and tools.

---

## 10. Tick-path discipline

Rules for every function on the tick path (`*_tick`):

- **No malloc / free / realloc.** Everything is preallocated.
- **No file I/O, no syscalls** other than the tick clock read.
- **No logging to stdout/stderr.** If logging is needed, write to an
  in-memory ring buffer drained by a separate thread (opt-in).
- **No mutable gameplay globals.** State lives on `RcWorld`; immutable
  definition views come from the process `RcGameData`; scratch is
  `_Thread_local`.
- **No recursive event dispatch within one tick** (see §7).
- **Scratch buffers** (pathfinding queues, visibility arrays, etc.)
  live in `_Thread_local` static arrays inside their owning function.
  Per the FC lessons memory, shared `static` scratch caused segfaults
  under OpenMP parallelism — enforce `_Thread_local`.

---

## 11. Types and headers

- `types.h` owns the shared world, player, NPC, map, interaction, and bounded
  subsystem state layouts used across core. Small durable-handle definitions
  live in `handles.h`.
- Each subsystem header owns its public operations and specialized data types;
  shared structs are not duplicated behind compatibility typedefs.
- Header dependencies must remain acyclic enough for a base-only consumer to
  compile and link without viewer or content dependencies.

Enforcement: add a CMake target `test_base_only` that builds
`rc-core` with every subsystem disabled, run a smoke test, and fail
if any subsystem's code is linked in.

---

## 12. Concurrency model

- One `RcWorld` owns one independent mutable simulation. Compatible worlds
  share one immutable `RcGameData` authority.
- Tick functions operate on the `RcWorld *` argument and do not mutate shared
  definition data.
- Parallelism is the caller's responsibility: spawn N worlds across
  N threads; each runs independently.
- Per-tick scratch uses `_Thread_local` (C11) so parallel worlds on
  separate threads never collide on scratch memory.
- Startup publication and game-data reference ownership use a lifecycle lock
  and atomic references. There are no locks on the tick path. Callers still own
  cross-world scheduling and must not tick the same world concurrently.

---

## 13. Determinism

Determinism is an engine property for reproducible RL rollouts and
repeatable automated correctness checks.

Given the same `RcWorldConfig` + initial state + input sequence, the
world must produce byte-identical output state. Requirements:

- RNG is a field on `RcWorld` (`rng_state`). No system calls like
  `rand()` or `time()` on the tick path.
- A configured seed of zero maps to `RC_DEFAULT_SEED`; explicit nonzero seeds
  remain reproducible. Bounded selection uses rejection sampling and defines
  exclusive and inclusive bounds separately.
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
| Generic encounter primitives (`acid_field`, `spawn_owned_helpers`, the special rotation; unverified older ones stay disabled) | `rc-core/encounter_prims.c` | Shared mechanism, not content. |
| Encounter script registry and trigger dispatch | `rc-core/encounter.c` | Generic routing by name/type; script bodies stay in content. |
| Encounter attack tables, targetability, damage modifiers, and protection scaling | `rc-core/encounter.c` + `rc-core/combat.c` | Generic phase-owned attack selection, phase targetability, player damage scaling, attack effects, and prayer damage scaling. Boss-specific attacks stay in data. |
| Regular NPC tag consumers for generic combat rules | `rc-core/combat.c` | Generic mechanics such as damage gates; encounter bosses skip them. |
| Breath protection tables | `rc-content/combat/dragonfire.c` | Exact OSRS values are content, reached through `npc_breath_max_hit`. |
| Activity-mechanics behavior/profile dispatch | `rc-core/activity_mechanics.c` + `rc-core/combat.c` | Generic data-driven owner effects; full boss scripts stay in content. |
| Typed activity state-machine loading and `RcActivityRun` stepping | `rc-core/activity_states.c` | Shared activity flow mechanism; object events validate loaded activity anchors and multi-boss runs count unique deaths. |
| Activity schema indexing | `rc-core/activity_schemas.c` | Loadable summary of encounters, typed activities, NPC IDs, object IDs, and activity-local arena/spawn metadata. |
| Activity-local spawn/anchor lookup | `rc-core/activity_spawns.c` | Exact activity-local points, dynamic pools, wave-filtered NPC materialization, wave-region resolution, object anchors, and explicit unresolved source blockers. |
| Provisional area-flag lookup | `rc-core/area_flags.c` | Sparse mapsquare-indexed flags for wilderness, wilderness level, multicombat, singles-plus, and safe-zone behavior. Rows are provisional until authoritative OSRS geometry is sourced. |
| Normalization and canonical form lookup | `rc-core/normalization.c` | Shared item form, NPC alias, and source-name join support; exact IDs stay available. |
| Owned traversal execution | `rc-core/tick.c` + `rc-core/traversal.c` | Exact object admission can create one generation-owned traversal with explicit destination policy, phase timing, checked relocation, and a terminal outcome. Ambiguous or unsupported graph rows are not executable actions. |
| Generic object consumers | `rc-core/tick.c` + `rc-core/skills.c` | Prayer altars restore prayer from object behavior data; doors record per-world open state; resource objects start, deplete, and respawn source-backed gathering nodes. |
| Slayer task loading, assignment filters, amount selection, and kill-credit routing | `rc-core/slayer.c` | Generic task owner; exact quest/location/boss-task edge rules come from data/content. |
| Boss-specific content (Vorkath's Poke handler, phase scripts) | `rc-content/encounters/<boss>.c` | Content — one boss only. |
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
