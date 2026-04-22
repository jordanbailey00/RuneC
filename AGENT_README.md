# RuneC — Repo Truth

This file is the highest-level source of truth for what RuneC is,
what it is trying to become, and how the repository is meant to be
structured.

Use this file for:
- project identity
- architecture boundaries
- required system categories
- required data categories
- scope rules

Do not use this file for planning or milestone tracking.
Planning lives in `work.md`.
Historical detail lives in `changelog.md`.

---

## 1. What RuneC is

RuneC is a playable single-player OSRS game in C with a Raylib client.

RuneC is also a modular headless simulation backend for RL training,
evaluation, and large-scale automated play. The same simulation
implementation must support both:
- a playable game client with visuals and UI
- stripped-down headless sims with only the systems and content needed
  for a narrow task

The long-term target is the OSRS game as a whole, excluding anything
explicitly out of scope in `ignore.md`. Current implementation coverage
is smaller than that target, but the architecture must be shaped for
the full game, not only for the current Varrock slice.

The backend must scale to very high throughput when headless. Target:
tens of millions of simulation steps per second in aggregate across
threads for narrow isolated workloads.

---

## 2. Core goals

- OSRS-faithful behavior. Tick order, combat math, movement, pathing,
  prayer interactions, encounter logic, drops, and skilling behavior
  should match OSRS as closely as the available data allows.
- One backend, not separate game and sim implementations. The playable
  client and the headless sim must consume the same simulation logic so
  they do not drift apart.
- Modular composition. A build should be able to enable only the
  systems and content it needs.
- Data-first implementation. The repo should capture the data needed
  for OSRS parity before leaning on hand-written approximations.
- High performance. The tick path must stay tight, predictable, and
  free of rendering or other unrelated overhead.
- Reproducible execution. The same config and inputs should produce the
  same simulation behavior, which matters for RL evaluation and
  automated validation.

---

## 3. What modularity means here

RuneC is not meant to be modular only at the code-organization level.
It is meant to be modular at runtime and build time.

Examples:
- A woodcutting RL sim should be able to keep world tiles/collision,
  movement, action handling, inventory/equipment, the woodcutting skill
  loop, axe behavior, and tree/resource-node logic while excluding
  unrelated combat, quests, shops, music, and bosses.
- A Fight Caves sim should be able to keep the Fight Caves arena,
  movement, combat, prayer, food/potions, equipment, and Fight Caves
  NPC/encounter logic while excluding unrelated regions, quests,
  skilling, and other bosses.
- A full playable game build should be able to include the viewer, UI,
  world streaming, broad region coverage, combat, skilling, NPC
  interaction, items/equipment, encounters, and the rest of the game
  systems that are in scope.

The architecture must support all three without forking the gameplay
logic into separate codepaths.

---

## 4. Repository architecture

### `rc-core/`

Generic simulation engine.

This layer owns:
- tick loop
- RNG
- world state
- pathfinding and LOS
- player/NPC state containers
- subsystem dispatch
- combat math
- inventory/equipment/consumables foundations
- skills foundations
- loot foundations
- encounter runtime and generic encounter primitives

This layer must stay:
- headless
- content-agnostic
- free of rendering dependencies
- free of OSRS-specific one-off scripts

`rc-core` should know how to execute a generic mechanic, not that a
specific boss or quest exists by name.

### `rc-content/`

OSRS-specific content modules layered on top of `rc-core`.

This layer owns:
- boss-specific encounter scripts
- region-specific behavior
- quest-specific state machines
- other OSRS-specific logic that should not live in the generic engine

This layer exists so narrow sims can link only the content they need.

### `rc-viewer/`

Interactive client and visual debugger.

This layer owns:
- rendering
- camera
- client-side input handling
- UI and menus
- animation presentation
- viewer-only overlays
- audio/music presentation if enabled

This layer must not own gameplay rules.

### `tools/`

Offline data pipeline.

This layer owns scraping, extraction, normalization, export, reports,
and validation. It pulls from the OSRS cache, OSRS Wiki, and approved
reference repositories, then emits runtime binaries and curated source
files.

### `data/`

Runtime data and curated source-of-truth inputs.

Broadly:
- `data/curated/` is hand-authored or hand-corrected source material
- `data/defs/` is compiled runtime data
- `data/regions/` and related assets hold baked world data

---

## 5. System categories

These are the parent system categories RuneC must cover. This is not an
exhaustive feature checklist; it is the high-level shape of the game.

### 5.1 Foundation systems

These are the always-on or near-always-on building blocks:
- tick loop and timing
- RNG and reproducible world execution
- world tiles, collision, and region loading
- entity transforms and state containers
- movement, routing, and LOS
- action/input queueing
- varbits and shared state flags

### 5.2 Common gameplay systems

These are the systems most playable builds and many isolated sims will
reuse:
- combat
- prayer
- items and inventory
- equipment and combat bonuses
- consumables
- loot and ground items
- skills and XP progression

### 5.3 Content and scenario systems

These are often required only for certain slices of the game:
- boss encounters and encounter-specific mechanics
- regular combat NPC special behavior
- region-specific scripts
- dialogue
- shops and banking
- slayer
- quests when time allows
- activities/minigames if later brought into scope

### 5.4 Client and presentation systems

These are required for the playable game client but not for headless
sims:
- world rendering
- animation playback/presentation
- camera
- UI panels and menus
- HUD, chatbox, tabs, minimap
- mouse interaction and right-click menus
- audio/music presentation

---

## 6. Core versus non-core systems

For repo design purposes:

- **Base systems** are the always-on engine foundations such as world
  state, movement, pathfinding, RNG, and action flow.
- **Core gameplay systems** are the reusable mechanics that many
  different gameplay slices depend on, such as combat, items,
  equipment, loot, consumables, and skills.
- **Non-core systems** are narrower or scenario-specific layers such as
  a specific boss script, a region override, dialogue trees, shops,
  quests, or music.

A system being "non-core" does not mean it is unimportant. It means the
repo should be able to omit it cleanly when a simulation does not need
it.

---

## 7. Data categories required for OSRS parity

RuneC needs enough data to reproduce OSRS behavior for the features we
intend to ship. At the parent-category level, that means:

- world geometry, terrain, and collision
- world object placement and object interaction metadata
- NPC definitions, combat stats, immunities, weaknesses, sizes, and
  behavior metadata
- NPC spawn points and respawn information
- item definitions for the full item corpus, including items whose
  acquisition path is out of scope for v1
- item metadata such as equipment bonuses, stackability, noted
  behavior, consumable effects, and related fields
- item asset links needed to render or instantiate those items in the
  playable game or a headless sim
- combat metadata such as attack styles, attack speed, max hit,
  projectiles, status effects, and protection interactions
- drops and loot tables
- spell, prayer, and varbit/state data
- encounter definitions and mechanic metadata for bosses and other
  complex fights
- skilling data such as resource nodes, recipes, XP values, tool
  requirements, and depletion/respawn rules
- dialogue, shop, banking, and NPC interaction data
- quest state and progression data when quests are in scope
- cache-derived assets needed by the viewer such as models, textures,
  animations, sprites, and map assets

If a required parent category is missing, the implementation will drift
from OSRS or stall out on approximations.

---

## 8. Scope shape

RuneC is not just a Varrock demo. Varrock is a useful integration slice
and current playable focus area, but the repo architecture and data plan
must be able to grow toward the broader OSRS game.

For the current first-version direction:
- hard requirements are the systems needed for a playable game slice
  and a strong modular RL baseline
- quests and some broader world/polish work are nice to have if time
  allows
- full item-database coverage is still a hard requirement even when an
  item's obtaining method belongs to a deferred quest, minigame, or
  other out-of-scope system

The exact sequencing lives in `work.md`, but the scope boundary defined
here should guide all future additions.

---

## 9. Working rules

- `AGENT_README.md` is the highest-level source of truth for repo
  identity, architecture, and scope.
- `README.md` stays concise: what the repo is, how it is structured,
  and how to build/run it.
- Component-root READMEs explain boundaries and responsibilities for
  that component.
- `work.md` is the only planning document.
- `changelog.md` holds dated historical detail.
- `ignore.md` defines intentional exclusions from scope.

When a new system or data category is proposed, it should fit this
document's architecture and scope model before it is added.
