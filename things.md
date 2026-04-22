# RuneC In-Scope Taxonomy

This file is the source of truth for the systems and data families
RuneC is intended to support.

Use this file to answer:
- what parent systems RuneC is meant to have
- what data families RuneC must capture
- what is a hard v1 requirement versus later in-scope work

Do not use this file for planning or progress tracking.
Planning lives in `work.md`.
Explicit v1 exclusions live in `ignore.md`.

Current live OSRS has 24 skills. RuneC v1 targets the 23 legacy skills
first and defers Sailing per `ignore.md`.

---

## 1. Core engine systems

RuneC is expected to support:
- deterministic world tick loop
- deterministic RNG
- world state containers for players, NPCs, projectiles, ground items,
  and encounter state
- movement, pathfinding, line-of-sight, and collision
- action queues, timers, delayed hits, and status effects
- save/load state
- subsystem enable/disable so narrow sims can load only what they need

These are the systems that most playable builds and most RL sims will
almost always need.

---

## 2. Core gameplay systems

RuneC is expected to support:
- player and NPC entities
- combat:
  - melee, ranged, and magic
  - attack speed and timing
  - max-hit and accuracy formulas
  - prayers and protection mechanics
  - spellbooks and rune use
  - special attacks
  - poison, venom, freeze, stun, and similar status effects
  - death, loot, and respawn rules
- items:
  - inventory
  - equipment
  - bank
  - ground items
  - consumables
  - item actions
- encounters:
  - boss state machines
  - phases
  - scripted mechanics
  - wave/arena content
- regular combat NPC mechanics beyond raw stats when needed

These are also core systems for both the playable game and headless RL
training slices.

---

## 3. Skills

RuneC is expected to support the legacy 23 OSRS skills:
- Attack
- Strength
- Defence
- Hitpoints
- Ranged
- Prayer
- Magic
- Runecraft
- Construction
- Agility
- Herblore
- Thieving
- Crafting
- Fletching
- Slayer
- Hunter
- Mining
- Smithing
- Fishing
- Cooking
- Firemaking
- Woodcutting
- Farming

For each skill family, RuneC should be able to represent:
- action loops
- XP gain and level gating
- tools and required items
- success/fail behavior
- resource depletion and respawn where relevant
- product outputs, rewards, and by-products

Sailing is deferred from v1 per `ignore.md`.

---

## 4. World and content systems

RuneC is expected to support:
- regions, areas, and instanced spaces
- terrain, static objects, dynamic object states, and collision
- plane transitions and navigation objects such as doors, gates,
  ladders, trapdoors, stairs, ferries, and teleports
- skilling nodes and world interactables
- shops, banks, and service NPCs
- NPC dialogue and interaction flows
- region rules such as safe zones, multi-combat, encounter arenas, and
  similar area flags

The world model should work both for broad playable slices and for
narrow isolated sim slices.

---

## 5. Client and presentation systems

RuneC is expected to support:
- a playable client with rendering
- camera, animation, and model presentation
- render scaling and culling so larger world slices remain practical
- minimap and local-world interaction feedback
- local chat / game-message output
- UI panels for inventory, equipment, combat, skills, prayers,
  spellbooks, bank, shops, and dialogue
- right-click menu and click-to-interact behavior

This layer exists for the playable game. Headless RL sims should be able
to omit it entirely.

---

## 6. Required data families

RuneC needs data for:
- world/render data:
  - terrain
  - object placements
  - collision
  - models
  - animations
  - textures
  - sprites
  - audio IDs and region music mapping
- NPC data:
  - definitions
  - combat metadata
  - spawns
  - drops
  - dialogue hooks
  - encounter bindings
- item data:
  - complete item definitions for all item IDs across all categories
    and types, not only the items v1 can currently obtain
  - equipment bonuses
  - consumable behavior
  - special attacks
  - note/placeholder/variant mappings
  - shop values and related metadata
- object/interactable data:
  - definitions
  - interaction options
  - state toggles
  - transport behavior
  - resource-node behavior
- player/system data:
  - XP tables
  - prayers
  - spells
  - recipes
  - teleports
  - varbits and related state IDs
  - area flags
- curated content data:
  - encounter specs
  - mechanics extracts
  - dialogue
  - quest metadata and steps
  - shop inventories

`database.md` is the canonical catalog for where these datasets live and
what fields they contain.

---

## 7. Hard v1 requirements

The first version must provide:
- a playable single-player OSRS slice
- a modular headless simulation backend for RL
- movement, collision, pathfinding, and action timing
- combat, prayers, spell use, items, equipment, consumables, drops, and
  bank/inventory foundations
- complete item-database coverage so any needed item can be spawned,
  equipped, rendered, or used even if its normal acquisition path is
  deferred
- priority bosses and priority regular combat NPC families
- first local skilling loops
- enough regions, objects, NPCs, and data to validate the baseline

---

## 8. Later in-scope work

These remain in-scope for RuneC, but are not hard v1 requirements:
- quests
- broader world coverage beyond the initial playable slices
- more skilling activities and encounter families
- richer audio and presentation polish
- additional UI depth once the baseline is stable

If something belongs to RuneC long-term and is not explicitly excluded
in `ignore.md`, it should fit somewhere in this taxonomy.
