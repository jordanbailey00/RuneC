# Work Log

Current state, what's next, and the remaining TODOs by section. All
historical detail lives in `changelog.md`; don't duplicate it here.

---

## State snapshot — what's built

**Engine (rc-core).** Generic backend — content-agnostic. Base:
types, api, world, tick loop, pathfinding (BFS + Bresenham LOS,
`_Thread_local` scratch), npc (NDEF v2 + NSPN), skills XP table,
prayer drain, items. Subsystems: `RcWorldConfig` + subsystem bitmask
+ event bus + handles (pass 1); combat engine with OSRS DPS math +
protection prayers + pending-hit queue + `RC_EVT_PLAYER_DAMAGED`
emission (pass 1 + 2.1); encounter subsystem with registry,
event-driven lifecycle, phase HP transitions, mechanic scheduler
(pass 1) + generic primitive registry + 6 pilot primitives + event
wiring for drain-prayer-on-hit (pass 2 + 2.1) + simple named
phase-enter / phase-exit trigger dispatch for the current HP%-based
phase model (bounded pass 2.2 slice).

**Content (rc-content) — NEW LAYER.** OSRS-specific scripts. Static
library that depends on `rc-core`. Per-module scaffolding for
`encounters/` (scurrius.c, kalphite_queen.c — placeholders until
boss-specific scripts land), `regions/`, `quests/`. Aggregate
`rc_content_register_all()` called by viewer + test_encounter_prims.
See `rc-content/README.md` and `rc-core/README.md` §18 for the
engine/content boundary and the rule: `rc-core` never mentions a
specific content instance by name.

**Renderer (rc-viewer).** Terrain / objects / models / anims /
collision loaders; orbital camera; click-to-move; WASD; tick_frac
interpolation; player + NPC animation; HUD; N-key debug markers.
Links both `rc-core` and `rc-content`.

**Data.** Phase 0-6 pipeline is operational. Binaries exist for NPC
defs, items, varbits, music, quests, shops, recipes, spells,
teleports, drops, skill drops, slayer, dialogue, encounters, and
world / Varrock spawns. Curated boss encounter TOMLs exist for 50
fights; boss-mechanics extracts exist for 96 bosses. The pipeline is
built, but parity against the intended first-version scope is not yet
signed off. Trust the current reports over stale counts:
`tools/reports/drops.txt`, `encounters.txt`, `dialogue.txt`,
`spawn_coverage.txt`, `xvalidate_monsters.txt`.

**Reference repos** under `/home/joe/projects/runescape-rl-reference/`:
`runelite` (cache format / IDs), `rsmod` (tick + combat formulas),
`void_rsps` (skill formulas / Varrock data / doors), `2011Scape-game`
(tertiary spawn source), `osrsreboxed-db` (item / NPC stats),
`data_osrs` (NPCList, teleports, varbit semantics).

**Tests green (8/8):** `test_base_only`, `test_combat`,
`test_combat_e2e`, `test_encounter`, `test_encounter_bin`,
`test_encounter_prims`, `test_determinism`, `test_pathfinding`.

---

## Immediate next action

Reopen the data/parity gate in §1 now that the repo/build/test hygiene
pass in §0 is complete. If that gate finds no blocker, resume the
wilderness primitive batch in §2.1.

---

## 0. Repo / build / test hygiene

Completed 2026-04-22.

- Build/test trust: CTest now owns the tracked 8-test suite, tests no
  longer depend on the shell cwd, and test assertions stay live in
  Release builds.
- Repo hygiene: validation now uses clean out-of-tree builds, repo
  docs are no longer gitignored, `.codex/` and the stray `typescript`
  file are treated as local clutter, and the stale in-repo `build/`
  dir was removed.
- Loader cleanup: added a shared checked-read helper and removed the
  unchecked `fread` warning storm from the current RuneC-owned binary
  loaders and the collision diagnostic.
- Verification: clean `RelWithDebInfo` and `Release` builds both
  succeeded from `/tmp`; CTest passed 8/8 in both profiles; the manual
  `test_collision` diagnostic also runs cleanly.

---

## 1. Data / parity gate

Before more feature work, verify that first-version scope has enough
data for OSRS parity (excluding `ignore.md`) and make every accepted
simplification explicit.

### 1.1 Visual / build parity

- `rc-viewer` builds cleanly and loads the current Varrock slice.
- Clear or classify the current visual blockers: textured-object tiling
  / atlas bleed, plane-aware NPC / player placement, static NPC
  wander, obvious Varrock geometry holes, environment animation gaps.
- Mark each issue as `blocks v1` or `defer`.

### 1.2 Core data parity

- NPC defs: review `xvalidate_monsters.txt`; close or explicitly accept
  remaining stat / max-hit / immunity mismatches.
- Items: verify `items.bin` is aiming at full item coverage rather than
  only v1-obtainable items. Quest/minigame/deferred-source items should
  still exist in the database with their core metadata, bonuses, and
  usable asset links so they can be spawned or equipped in-game or in
  headless sims.
- Drops: review unresolved names in `drops.txt`; separate out-of-scope
  rows from real missing NPC/item mappings.
- Spawns: review `spawn_coverage.txt`; decide where 2011Scape-derived
  positions are acceptable and where OSRS overrides are required.
- Encounters: treat the 50 encounter TOMLs as source of truth; keep
  unsupported trigger forms/scripts explicitly deferred until the
  richer phase-model pass.
- Regular combat NPCs: define the first-version non-boss families that
  require explicit parity review beyond generic combat stats.

### 1.3 First-version regular combat NPC audit

- Simple mobs with no special behavior can ship from `npc_defs.bin` +
  `drops.bin` + `world.npc-spawns.bin` once combat is correct.
- Special-case families that need explicit review: dragonfire users /
  dragons, slayer gear-check monsters, monsters with drains, binds,
  teleports, poison/venom, or prayer interactions, and abyssal /
  demon / wyvern / spectre / basilisk families with bespoke behavior.
- First audit targets: dragons, abyssal demons, banshees /
  spectres / basilisks / cockatrice / cave horrors / fever spiders,
  kurasks / turoths, wyverns.
- Decide whether each mechanic belongs in generic combat code,
  extended NPC defs, or a small curated non-boss combat layer.

### 1.4 Exit criteria

- For each required data family, mark one of: `ready`, `ready with
  accepted simplifications`, `missing and blocks implementation`.
- Treat item-database completeness as its own gate. Missing gameplay
  acquisition loops are acceptable; missing item definitions/assets for
  real OSRS items are not.
- Update this file with the accepted v1 data boundary before
  returning to feature work.

---

## 2. Core engine work required for v1

### 2.1 Encounter pass 2 — active

**Landed**
- binary v2 param blocks
- primitive registry + 6 pilot primitives
- `RC_EVT_PLAYER_DAMAGED` + simple `RC_EVT_PHASE_TRANSITION` wiring
- `rc-content/` scaffold for future boss-specific scripts

**Next**
- Resume the wilderness batch: Obor, Bryophyta, Scorpia, Chaos
  Elemental, Giant Mole.
- Keep richer trigger forms inert during the primitive grind.
- Then continue: GWD → DKS → Corp/Cerberus/Kraken →
  Zulrah/Vorkath → Hydra/Nightmare/Muspah/DT2 → raids/waves →
  remaining bosses.

**Deferred until the richer phase-model pass**
- hard-hp-zero transitions, timed transitions, `enter_after`
- broader trigger DSL (`phase_in`, `while_in_phase`, `after_attack`,
  unions)
- script registry API + boss-specific phase scripts

**Later encounter format work**
- multi-boss `[[bosses]]`
- raid `[[rooms]]` / wave `[[waves]]`
- damage modifiers / protections / variants / rotations
- replay-style regression test per encounter

### 2.2 Combat pass 2 — not started

- weapon speeds, stance selection, auto-retaliate, combat XP rewards
- ranged/magic prayer bonuses and NPC overhead prayers
- ammo/rune consumption, special attacks, poison/venom/freeze/stun
- attack range + LOS
- hitsplats + HP bars

### 2.3 rc-core modularity pass 2 (was TODO #2)

- per-subsystem binary loaders
- hot/cold NPC + player splits
- header cleanup (base vs subsystem)
- remaining handle cleanup
- narrow combat-sim target + throughput benches

### 2.4 Required non-boss combat parity

- After the boss foundation is stable enough, cover the regular combat
  NPCs needed for the first playable / RL baseline.
- Start with families that have meaningful mechanics, not pure stat
  sticks.
- Reuse generic combat where possible; only add new data/runtime
  layers for monsters that actually need them.

---

## 3. Required playable-game systems for v1

### 3.1 UI

- chat, minimap, inventory, equipment, prayer/spell/skills tabs
- right-click menus, NPC dialogue overlay, shop/bank panels
- fixed vs resizable layout
- OSRS-styled framing, fonts, palette

### 3.2 NPC world behavior

- static NPCs stay put
- attack / death animations fire correctly
- plane-aware rendering for upper floors and caves

### 3.3 Items & equipment

- equip / unequip wiring
- NPC drops to ground items
- pickup / drop / noted behavior
- food and potion consumption

### 3.4 Skills

- skills tab
- first local skill loops: mining, smithing, cooking, woodcutting,
  firemaking, prayer, fishing
- node depletion / respawn
- level-up feedback

### 3.5 NPC interaction

- dialogue state machine
- shops and banking
- doors / ladders / stairs / basic object interactions
- wire Varrock service NPCs

### 3.6 Viewer / render scalability

- Add a rendering path that scales beyond a single tightly bounded area
  without brute-forcing every loaded region every frame.
- Separate simulation scope from render scope so headless sims stay
  lean and the playable client only draws what is meaningfully visible.
- Add chunk/region visibility control, frustum/distance culling, and
  batched or otherwise cheaper static-world rendering.
- Budget distant NPC/object/animation updates so broad world coverage
  does not overwhelm CPU or GPU.
- Treat this as the workaround that unlocks rendering larger slices of
  the game without being forced into "one area at a time."

Dependencies:
- UI unlocks items/equipment, skills, and NPC interaction.
- Combat + data parity unlock reliable non-boss NPC work.

---

## 4. Nice to have / time permitting

### 4.1 Quests

- varbit-based quest state tracking
- 10 MVP free quests
- quest journal
- modern quest reconstructions later

### 4.2 Expanded world coverage

- full-surface OSRS streaming beyond Varrock
- instanced placement rendering
- distant NPC stream-ticking

### 4.3 Optional follow-ups

- run energy drain/regen
- stat boost/drain timers
- Varrock visual hole sweep
- OSRS spawn overrides where 2011Scape drift remains
- longer-range pathfinding beyond the current 128×128 local grid

---

## Reference

### Repository layout
- `rc-core/` — **generic engine** (no render deps, no OSRS-specific
  content). See `rc-core/README.md`.
- `rc-content/` — **OSRS content modules** (boss scripts, quests,
  regions). Depends on rc-core. See `rc-content/README.md`.
- `rc-viewer/` — raylib frontend. Depends on both.
- `tools/` — scrapers + exporters (Python).
- `data/regions/`, `data/defs/`, `data/spawns/`, `data/curated/` —
  baked assets + curated TOMLs.
- External clones under `/home/joe/projects/runescape-rl-reference/`.

### Scope rule for per-boss extraction

When extracting from a boss / NPC page, only pull data describing:
- The NPC itself — hp, stats, attack styles, max hit, immunities.
- Mechanics — abilities, damage, timings, phases.
- Drops — loot, rarity, quantity.
- Spawns — x/y/plane, including dynamic / instanced.

Don't pull recommended equipment, inventory setups, suggested skills,
transportation, CA lists, or money-making-guide data from boss pages.
Applies to per-boss page extraction only; other pipelines (music,
shops, quests, skilling, spells) follow their own rules.

### Not in scope (see `ignore.md` for full list)
GE prices / P2P trade; random events; achievement diaries; combat
achievements; collection log; multiplayer chat / friends / clan /
GIM; bonds; hi-scores; world select; polls; emotes panel; character
customization salons; pet insurance; PvP minigames; complex
login/auth.

### Historical detail
See `changelog.md` for dated milestone history (Phase 0-6 build-out,
pass-1 engine landings, pass-2 primitive pilots, etc.).
