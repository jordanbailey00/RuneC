# RuneC Database

This file is the authoritative catalog of RuneC's database categories,
their storage locations, their source authority, their acquisition
methods, and the expected shape of each dataset.

Use this file to answer:
- what data categories RuneC stores
- where each category lives
- which sources feed each category
- how each category is obtained
- what fields an entry is expected to contain
- which datasets are curated only versus compiled for runtime

Do not use this file for planning or milestone tracking.
Planning lives in `work.md`.
There is no separate database-template authority document.
For concrete curated-file examples, inspect `data/curated/`.
For compiled-layout notes, inspect the relevant exporter docstrings.

Counts change over time. For current inventories and coverage, trust
`tools/reports/*.txt` instead of hard-coded counts in this file.

---

## 1. Scope

This database is for the full intended OSRS game scope, not only the
current Varrock slice. It catalogs both:
- gameplay/runtime datasets consumed by `rc-core`
- curated structured source files that are part of RuneC's database,
  even when they are not yet compiled into a runtime binary

Item coverage is broader than gameplay-availability coverage.
RuneC should capture the full item corpus in the database even when an
item's normal obtaining path belongs to a quest, minigame, seasonal
system, or other feature that is out of scope for v1.

Out-of-scope categories remain defined by `ignore.md`.

---

## 2. Data layout

RuneC data is organized into five layers.

### 2.1 External sources

Primary external sources live outside the repo under
`/home/joe/projects/runescape-rl-reference/`.

The important ones are:
- `valo_envs/ocean/osrs/scripts/` — b237 cache readers and asset export
  scripts used for terrain, objects, collision, models, and animations
- `osrsreboxed-db/` — structured item and monster JSON
- `data_osrs/` — OSRS spawn, teleport, transport, and varbit-adjacent
  JSON dumps
- `2011Scape-game/` — overlap/legacy helper only; not a canonical OSRS
  gameplay authority

### 2.2 Raw cached inputs

RuneC caches scraped wiki data under:
- `tools/wiki_cache/*.json` — bucket query results
- `tools/wiki_cache/pages/*.json` — full page wikitext cache

These caches are the raw inputs for most structured exporters and
extractors. Re-runs should consume the cache rather than rely on fresh
network fetches.

### 2.3 Curated structured inputs

Hand-authored or hand-corrected structured data lives under:
- `data/curated/encounters/`
- `data/curated/mechanics/`
- `data/curated/dialogue/`
- `data/curated/quests/`
- `data/curated/specials/`

This is the human-editable source-of-truth layer for categories that
need curation or post-scrape normalization.

### 2.4 Runtime data

Compiled runtime datasets live under:
- `data/defs/` — gameplay/runtime binaries
- `data/spawns/` — global spawn binaries
- `data/regions/` — per-region world/render binaries
- `data/models/` — compiled model sets
- `data/anims/` — compiled animation sets

These are what the engine and viewer actually load.

### 2.5 Validation and coverage

Reports live under `tools/reports/`. They are part of the database
workflow and must be treated as first-class validation artifacts.

---

## 3. Source authority and acquisition rules

### 3.1 Source authority

Use sources in this order, depending on category:

1. **b237 cache exports**
   - authoritative for raw cache-backed assets and definitions:
     terrain, objects, collision, models, animations, many IDs,
     object/NPC/item base definitions
2. **OSRS Wiki bucket cache**
   - authoritative for structured gameplay metadata already exposed in
     Cargo/bucket form: drops, shops, music, quests, recipes, spells,
     varbits, transcript indexes
3. **OSRS Wiki page wikitext cache**
   - authoritative for prose-only or section-level data that is not
     available as clean bucket fields: mechanics extracts, dialogue
     trees, quest walkthrough steps, slayer tables, shared drop tables,
     item special attack prose
4. **osrsreboxed-db**
   - authoritative structured source for item equipment/weapon data and
     broad monster combat metadata
5. **data_osrs**
   - authoritative structured source for global OSRS NPC spawns and
     related transport/teleport JSON
6. **Manual curation**
   - final correction layer when automatic extraction is not enough

### 3.2 Current non-authorities

- `2011Scape-game` is not a canonical OSRS gameplay data source.
  Use it only as a legacy helper or overlap reference where explicitly
  documented.
- Historical docs that still describe older pipeline phases are not
  authoritative. This file, current exporter docstrings, and current
  reports are.

### 3.3 Acquisition methods

RuneC currently uses four acquisition methods:
- **cache decoding** — direct parsing of b237 cache content
- **bucket joins** — joining cached wiki bucket JSON on names or IDs
- **page-section extraction** — parsing cached page wikitext to extract
  structured sections
- **reference-repo readers** — lazy readers over cloned JSON or source
  repos such as `osrsreboxed-db` and `data_osrs`

When adding a new category, define its authority and acquisition method
here before landing the exporter.

---

## 4. Database catalog

This section is the canonical list of stored categories.

## 4.1 NPC and combat-entity data

### NPC definitions

- Purpose: one row per NPC ID with combat-relevant and render-relevant
  base data.
- Lives:
  - raw: b237 cache + `osrsreboxed-db` + cached wiki
    `infobox_monster_*.json`
  - runtime: `data/defs/npc_defs.bin`
- Built by: `tools/export_npcs.py`
- Validation: `tools/reports/xvalidate_monsters.txt`
- Entry includes:
  - `id`, `name`, `size`, `combat_level`, `hitpoints`
  - six combat stats: attack, defence, strength, hitpoints, ranged,
    magic
  - five anim IDs: stand, walk, run, attack, death
  - aggression and combat behavior fields:
    `aggressive`, `aggro_range`, `max_hit`, `attack_speed`,
    `slayer_level`, `attack_types`, `weakness`,
    `poison_immune`, `venom_immune`
- Notes:
  - cache provides the base def/anims/model relationship
  - `osrsreboxed-db` provides most combat metadata
  - wiki overlays `max_hit` and poison/venom immunity where
    `osrsreboxed-db` is incomplete

### NPC spawns

- Purpose: world placement of NPC instances.
- Lives:
  - raw: `data_osrs/NPCList_OSRS.json`
  - cross-check: cached wiki `locline_*.json`
  - runtime: `data/spawns/world.npc-spawns.bin`,
    `data/regions/varrock.npc-spawns.bin`
- Built by: `tools/export_spawns.py`
- Validation: `tools/reports/spawn_coverage.txt`
- Entry includes:
  - `npc_id`, `x`, `y`, `plane`
  - `direction`
  - `wander_range`
  - `flags` when needed for special handling
- Notes:
  - current primary source is `data_osrs`, not `2011Scape`
  - global spawns and region-scoped spawns share the same NSPN shape

### NPC drop tables

- Purpose: per-NPC loot tables.
- Lives:
  - raw: cached wiki `dropsline_*.json`
  - joins: cached wiki `infobox_item_*.json` +
    `infobox_monster_*.json`
  - runtime: `data/defs/drops.bin`
- Built by: `tools/export_drops.py`
- Validation: `tools/reports/drops.txt`
- Entry includes:
  - NPC table key
  - grouped entries for guaranteed/main/tertiary-style drops
  - per drop: `item_id`, `qmin`, `qmax`, `rarity_inv`
- Notes:
  - `rarity_inv` is stored as fixed-point inverse probability for
    runtime efficiency
  - unresolved names and odd rarities are surfaced in the coverage
    report and must not be silently ignored

### Shared drop tables

- Purpose: shared tables referenced by many NPCs.
- Lives:
  - raw: wiki page wikitext for Rare drop table, Mega-rare drop table,
    Gem drop table
  - runtime: `data/defs/rdt.bin`, `data/defs/mrdt.bin`,
    `data/defs/gdt.bin`
- Built by: `tools/scrape_rdt.py`
- Validation: `tools/reports/rdt_gdt.txt`
- Entry includes:
  - `item_id`, `qmin`, `qmax`, `rarity_inv`
- Notes:
  - unresolved symbolic rows like `Nothing` or nested table references
    are kept with `item_id = 0` so table slot structure is preserved

### Skill and non-NPC drops

- Purpose: drops whose source is not an NPC, such as trees, rocks,
  chests, reward caskets, and activity containers.
- Lives:
  - raw: cached wiki `dropsline_*.json`
  - runtime: `data/defs/skill_drops.bin`
- Built by: `tools/export_skill_drops.py`
- Validation: `tools/reports/skill_drops.txt`
- Entry includes:
  - `source_name`
  - per drop: `item_id`, `qmin`, `qmax`, `rarity_inv`
- Notes:
  - NPC-backed rows are intentionally excluded here and belong in
    `drops.bin`

### Slayer task assignments

- Purpose: master-to-task weighted assignment data.
- Lives:
  - raw: wiki page wikitext for slayer master pages
  - runtime: `data/defs/slayer.bin`
- Built by: `tools/scrape_slayer.py`
- Validation: `tools/reports/slayer.txt`
- Entry includes:
  - master name
  - per task: `npc_name`, `weight`
- Notes:
  - current dataset is intentionally scoped to assignment weights
  - amount ranges and unlock/extension metadata are not yet first-class

### Encounter specifications

- Purpose: hand-curated boss encounter data for the generic encounter
  runtime.
- Lives:
  - curated: `data/curated/encounters/*.toml`
  - runtime: `data/defs/encounters.bin`
- Built by: `tools/export_encounters.py`
- Validation: `tools/reports/encounters.txt`
- Curated entry includes:
  - top-level identity: `name`, `slug`, `npc_ids`, `source_pages`
  - optional `stats_override`
  - `[[attacks]]` with fields such as `name`, `style`, `max_hit`,
    `warning_ticks`, and encounter-specific attack metadata
  - `[[phases]]` with fields such as `id`, `enter_at_hp_pct`,
    optional `script`, allowed attacks, style weights, and phase rules
  - `[[mechanics]]` with `name`, `primitive`, trigger/period fields,
    and primitive-specific `params.*`
- Runtime entry includes:
  - encounter slug
  - NPC ID list
  - attacks
  - phases
  - mechanics with primitive ID, trigger binding, and fixed param block
- Notes:
  - curated TOML is the source-of-truth layer
  - runtime currently supports only a bounded subset of trigger forms;
    deferred trigger bindings are reported, not silently dropped

### Boss mechanics extracts

- Purpose: page-section captures used to author encounter TOMLs.
- Lives:
  - curated only: `data/curated/mechanics/*.toml`
- Built by: `tools/extract_mechanics.py`
- Validation: `tools/reports/mechanics_extract.txt`
- Entry includes:
  - `name`
  - `source_pages`
  - `npc_ids`
  - `[sections]` mapping section headers to raw extracted prose
- Notes:
  - this is a curated/reference layer, not a runtime binary
  - it is intentionally boss-focused, not a universal regular-monster
    mechanics database

## 4.2 Item, combat-support, and skilling data

### Item definitions

- Purpose: one row per item ID for the full item corpus, with enough
  metadata to spawn, render, equip, and use items even when their
  acquisition path is deferred or out of scope for v1.
- Lives:
  - raw: `osrsreboxed-db/docs/items-json/*.json`
  - runtime: `data/defs/items.bin`
- Built by: `tools/export_items.py`
- Validation: `tools/reports/xvalidate_bonuses.txt`
- Entry includes:
  - core identity and flags: `id`, `name`, stackable, tradeable,
    members, quest item, noted, placeholder
  - economic/static values: `weight`, `highalch`, `lowalch`, `cost`,
    linked noted ID
  - cache-backed presentation links needed to identify/render the item
    in-game when available through the source pipeline
  - equipment block when present:
    slot, skill requirements, 14 combat/prayer bonus fields
  - weapon block when present:
    `attack_speed`, `weapon_type`, stance bits, stance labels
- Notes:
  - GE prices are intentionally excluded
  - quest-locked, minigame-locked, clue-only, seasonal, or otherwise
    not-yet-obtainable items still belong in this dataset
  - weapon attack-style metadata currently lives inside this item
    dataset rather than a separate binary

### Item special-attack extracts

- Purpose: curated prose capture of special-attack behavior from wiki
  weapon pages.
- Lives:
  - curated only: `data/curated/specials/*.toml`
- Built by: `tools/scrape_item_specials.py`
- Validation: `tools/reports/item_specials.txt`
- Entry includes:
  - `name`
  - `item_ids`
  - `[special]` mapping section headers to extracted prose blocks
- Notes:
  - this is not yet a compiled runtime dataset
  - it exists as a reference/capture layer for future combat work

### Spells

- Purpose: spell metadata and rune costs.
- Lives:
  - raw: cached wiki `infobox_spell_*.json`
  - joins: cached wiki `infobox_item_*.json` for rune item IDs
  - runtime: `data/defs/spells.bin`
- Built by: `tools/export_spells.py`
- Entry includes:
  - spell `name`
  - `spellbook`, `type`, `level`, `slayer_level`
  - XP
  - flags
  - rune list as `(item_id, qty)` pairs

### Teleports

- Purpose: teleport-specific subset of spell data.
- Lives:
  - raw: same source as spells
  - runtime: `data/defs/teleports.bin`
- Built by: `tools/export_spells.py`
- Entry includes:
  - same core record shape as spells for teleport-class entries
- Notes:
  - current teleport dataset is spell-derived; non-spell transports are
    not yet a first-class runtime dataset

### Recipes and skilling conversions

- Purpose: generic skill/action recipes.
- Lives:
  - raw: cached wiki `recipe_*.json`
  - joins: cached wiki `infobox_item_*.json`
  - runtime: `data/defs/recipes.bin`
- Built by: `tools/export_recipes.py`
- Entry includes:
  - `name`
  - skill requirements and XP
  - input items and quantities
  - tools
  - facility string
  - output item, output quantity, tick cost
  - members flag
- Notes:
  - this is a general recipe dataset, not a full per-skill activity
    state machine by itself

### Varbits

- Purpose: semantic name-to-index mapping for client/game state bits.
- Lives:
  - raw: cached wiki `varbit_*.json`
  - runtime: `data/defs/varbits.bin`
- Built by: `tools/export_varbits.py`
- Entry includes:
  - numeric varbit index
  - symbolic varbit name
- Notes:
  - this is the semantic lookup layer, not the raw cache bit-range
    structure dump

## 4.3 Quests, dialogue, shops, and music

### Dialogue transcripts

- Purpose: structured dialogue trees derived from transcript pages.
- Lives:
  - raw: cached wiki `transcript_*.json` plus page wikitext cache
  - curated: `data/curated/dialogue/*.toml`
  - runtime: `data/defs/dialogue.bin`
- Built by:
  - extract: `tools/extract_dialogue.py`
  - compile: `tools/export_dialogue.py`
- Validation: `tools/reports/dialogue.txt`
- Curated entry includes:
  - `title`
  - `npcs`
  - `node_count`
  - `[[nodes]]` with:
    `id`, `depth`, `kind`, optional `speaker`, `text`,
    `parent`, optional `children`, optional `is_terminal`
- Runtime entry includes:
  - transcript slug
  - NPC names
  - flattened node tree

### Quest metadata

- Purpose: compact quest list with classification and skill
  requirements.
- Lives:
  - raw: cached wiki `quest_*.json`
  - runtime: `data/defs/quests.bin`
- Built by: `tools/export_quests.py`
- Entry includes:
  - quest name
  - difficulty
  - length
  - skill requirement pairs
- Notes:
  - this is not the full quest state-machine dataset
  - it is metadata only

### Quest walkthrough steps

- Purpose: structured step lists extracted from full quest pages.
- Lives:
  - raw: wiki page wikitext cache
  - curated only: `data/curated/quests/<Quest>/steps.toml`
- Built by: `tools/extract_quest_steps.py`
- Validation: `tools/reports/quest_steps.txt`
- Entry includes:
  - `quest`
  - `step_count`
  - `[[steps]]` with:
    `index`, `name`, `links`, `text`
- Notes:
  - this is the current structured quest progression source
  - it is not yet compiled into a richer runtime quest-state binary

### Shops

- Purpose: shop stock and pricing/restock data.
- Lives:
  - raw: cached wiki `infobox_shop_*.json` + `storeline_*.json`
  - joins: cached wiki `infobox_item_*.json`
  - runtime: `data/defs/shops.bin`
- Built by: `tools/export_shops.py`
- Entry includes:
  - shop `name`, `owner`, `location`, `specialty`, members flag
  - stock list entries with:
    `item_id`, buy price, sell price, base stock,
    buy multiplier, sell multiplier, restock ticks

### Music

- Purpose: track metadata for region/music playback.
- Lives:
  - raw: cached wiki `music_*.json`
  - runtime: `data/defs/music.bin`
- Built by: `tools/export_music.py`
- Entry includes:
  - `cache_id`
  - track number
  - flags: members, jingle, event
  - duration
  - title
  - composer
- Notes:
  - this is track metadata only
  - region-to-track trigger mapping is not yet a first-class runtime
    dataset

## 4.4 World and render asset data

These are data categories RuneC stores and loads, but they are not
curated gameplay records in the same way as drops, quests, or shops.

### Region terrain

- Purpose: per-region terrain surface data for viewer/world loading.
- Lives:
  - runtime: `data/regions/*.terrain`
- Built from:
  - b237 cache via reference export scripts
- Entry shape:
  - per-region height/overlay/underlay tile data

### Region collision

- Purpose: per-tile collision flags for movement and LOS.
- Lives:
  - runtime: `data/regions/*.cmap`
- Built by: `tools/export_collision.py`
- Entry shape:
  - per region, per plane, per tile collision flags

### Region object placements

- Purpose: placed map objects used for rendering and world interaction.
- Lives:
  - runtime: `data/regions/*.objects`
- Built by: `tools/export_objects_bridge.py`
- Entry shape:
  - placed object instances with object ID, tile position, type,
    rotation, and visual-level-resolved inclusion

### Region atlas

- Purpose: region-specific render packing/atlas support.
- Lives:
  - runtime: `data/regions/*.atlas`
- Notes:
  - currently present for Varrock
  - viewer/render data, not gameplay-rule data

### Model sets

- Purpose: compiled model data for viewer rendering.
- Lives:
  - runtime: `data/models/npcs.models`, `data/models/player.models`
- Built by:
  - NPC models: `tools/export_npcs.py`
  - player models: reference cache export pipeline
- Entry shape:
  - mesh/model payloads, not hand-curated gameplay records

### Animation sets

- Purpose: compiled animation sequences for NPCs and player models.
- Lives:
  - runtime: `data/anims/all.anims`,
    `data/anims/npcs.anims`, `data/anims/player.anims`
- Built by:
  - NPC animation subset: `tools/export_npc_anims.py`
  - broader animation sets: reference cache export pipeline
- Entry shape:
  - framebase/frame/sequence payloads, not hand-curated gameplay
    records

## 4.5 Categories not yet represented as first-class datasets

These categories matter to the full game, but they are not currently
represented as a dedicated curated dataset or runtime binary in the
live pipeline:
- object definitions and object action metadata as a dedicated runtime
  dataset
- prayer definitions and prayer-effect metadata
- object interaction metadata such as doors, ladders, stairs, and
  transports
- ground-item spawn/respawn datasets
- varplayers
- interface/widget runtime datasets for UI logic
- region-area flags such as wilderness, multi-combat, and safe zones
- weather/environment metadata
- regular-monster mechanics as a general structured layer outside the
  boss encounter system

If one of these becomes a first-class dataset, add it here with source,
location, schema, and builder before landing the implementation.

---

## 5. Update rule

When adding or changing a dataset:
- update this file first with category, sources, paths, and field shape
- then update or add the extractor/exporter
- then update the relevant validation report

This file should always tell a future contributor exactly what the
RuneC database contains, where it came from, where it lives, and what
shape each category is supposed to have.
