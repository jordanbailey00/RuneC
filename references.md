# RuneC References

This file is a lightweight index of the external references RuneC uses.

Use this file to answer:
- what external references matter
- where each reference lives
- what each reference is useful for
- how authoritative each reference is

Do not use this file for planning, scope, or progress tracking.
Use `AGENT_README.md`, `things.md`, `database.md`, and `work.md` for
those roles.

---

## 1. Core references

### b237 cache

- Path: `/tmp/osrs_cache_modern/cache/`
- Use:
  - raw world, object, NPC, item, model, animation, texture, and sprite
    data
- Authority:
  - authoritative for cache-backed game assets and definitions

### Cache reader scripts

- Path:
  `/home/joe/projects/runescape-rl-reference/valo_envs/ocean/osrs/scripts/`
- Use:
  - export helpers for terrain, objects, models, textures, sprites, and
    animations
- Authority:
  - implementation helper around the cache, not a gameplay authority

### RuneLite

- Path: `/home/joe/projects/runescape-rl-reference/runelite/`
- Use:
  - cache format and opcode decoding
  - IDs and enums
  - client-side definitions and data structures
- Authority:
  - primary authority for cache-backed structures and client IDs

### RSMod

- Path: `/home/joe/projects/runescape-rl-reference/rsmod/`
- Use:
  - tick order
  - pathfinding and collision usage
  - combat formulas and equipment-bonus patterns
  - shop and dialogue architecture patterns
- Authority:
  - primary server-side reference for modern OSRS-style engine behavior

### Void RSPS

- Path: `/home/joe/projects/runescape-rl-reference/void_rsps/`
- Use:
  - skill logic patterns
  - object interaction patterns
  - older quest/dialogue/shop examples
  - overlap content where OSRS still shares older RuneScape behavior
- Authority:
  - secondary overlap reference only, not a canonical OSRS authority

### 2011Scape-game

- Path: `/home/joe/projects/runescape-rl-reference/2011Scape-game/`
- Use:
  - tertiary overlap reference for older shared content
  - occasional helper data for spawns, transports, or world logic where
    explicitly cross-checked
- Authority:
  - helper only; do not treat as canonical OSRS gameplay data

### osrsreboxed-db

- Path: `/home/joe/projects/runescape-rl-reference/osrsreboxed-db/`
- Use:
  - structured item metadata
  - structured monster combat metadata
- Authority:
  - primary structured helper for item and NPC combat metadata, subject
    to wiki/cache cross-checks

### data_osrs

- Path: `/home/joe/projects/runescape-rl-reference/data_osrs/`
- Use:
  - structured NPC spawns
  - transport and teleport-adjacent data
  - varbit-adjacent structured dumps
- Authority:
  - primary structured helper for global spawn placement and related
    world metadata

### Fight Caves demo references

- Paths:
  - `/home/joe/projects/runescape-rl-reference/current_fightcaves_demo/`
  - `/home/joe/projects/runescape-rl-reference/old-fightcaves-demo/`
  - `/home/joe/projects/runescape-rl/claude/runescape-rl/`
- Use:
  - proven C patterns for wave logic, combat timing, prayer timing, and
    viewer/runtime integration
- Authority:
  - narrow implementation reference, not a general OSRS authority

---

## 2. Wiki-backed references

### OSRS Wiki cached bucket data

- Path: `tools/wiki_cache/*.json`
- Use:
  - structured gameplay metadata already exposed in bucket/Cargo form:
    drops, shops, music, quests, recipes, spells, varbits, transcripts,
    and similar categories
- Authority:
  - primary gameplay-metadata authority once cached locally

### OSRS Wiki cached page data

- Path: `tools/wiki_cache/pages/*.json`
- Use:
  - mechanics prose
  - dialogue extraction
  - quest step extraction
  - shared drop tables
  - item special-attack extraction
- Authority:
  - primary fallback when structured bucket fields are not enough

---

## 3. Rules

- Prefer the cache and RuneLite for cache-backed definitions.
- Prefer RSMod for modern engine behavior and combat structure.
- Prefer the cached OSRS Wiki for gameplay metadata that is not present
  in the cache.
- Treat Void and 2011Scape as overlap helpers, not canonical OSRS
  sources.
- When two references disagree, resolve the conflict in `database.md`,
  the relevant exporter, and the relevant validation report.
