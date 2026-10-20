# rc-viewer

`rc-viewer` is the playable client for RuneC. It renders world state
produced by `rc-core` + `rc-content` and handles presentation concerns
such as camera, input, HUD, animation playback, and other viewer-only
systems.

This directory is not the simulation engine. Headless RL sims and
other stripped-down builds should be able to omit `rc-viewer`
entirely.

Use this file for:
- what the viewer owns
- what the viewer depends on
- what code lives in this directory
- what the viewer is and is not responsible for

Planning and task tracking are kept in local Markdown files. This README is
the tracked technical overview of the viewer boundary, runtime assets, and
presentation responsibilities.

## Role

- Interactive local play and visual debugging.
- Rendering and presentation of engine state.
- Viewer-only systems such as camera control, HUD, and audio/music.

## Boundary

- Depends on `rc-core` and `rc-content`.
- May choose which content modules to register at startup.
- Must not own gameplay rules, combat formulas, encounter mechanics,
  quest state machines, or other simulation logic.
- Must treat `data/` as ignored local generated/runtime assets, not as source
  files in the main RuneC repo.

## Key files

Scene right-click menus collect overlapping NPCs, the picked object, and
visible ground piles independently. Each row retains its own target identity;
long menus have Previous/More options. Left-click priority is unchanged. Take
uses the core's UID/version checks and ordinary reach, visibility and capacity
rules, even when an NPC occupies the item's tile.

The playable configuration loads `defs/drops.bin`, `rdt.bin`, `gdt.bin` and
`mrdt.bin` through the core. Overrides are `RUNEC_DROPS`, `RUNEC_RDT`,
`RUNEC_GDT` and `RUNEC_MRDT`. A missing or rejected NPC table emits an explicit
death diagnostic; enabling loot is not a claim that every table is verified.

- `viewer.c`
  - main window lifecycle
  - input handling
  - camera control
  - visual scene selection and generation that follows core-owned active-area
    activation without controlling or rolling back gameplay state
  - world ticking for the current viewer path
- `tick_pacing.c` / `tick_pacing.h`
  - monotonic 600 ms world-tick accumulator
  - bounded five-tick catch-up after a slow frame
  - explicit dropped-overload telemetry and interpolation fraction
- `streaming.c` / `streaming.h`
  - viewer streaming defaults and load telemetry counters
  - monotonic scene/chunk timing independent of the render implementation
  - deterministic center-first mapsquare planning and asset path contracts
- `ui.c` / `ui.h`
  - cache-sprite-backed OSRS viewer UI shell
  - clickable chatbox, minimap/orbs, tabs, inventory/equipment panels,
    prayer/spellbook surfaces, and context menus
  - viewer-owned minimap texture state for the circular moving map
    surface and hookable map dots
  - hook-ready UI state and intents for future gameplay systems
- `ui_assets.c` / `ui_assets.h`
  - small Raylib texture/font loader for viewer UI sprites
  - loads `data/sprites/ui/*.png` generated from the current OpenRS2
    oldschool live cache through
    `tools/cache_pipeline/export_sprites_modern.py` and the local
    RuneLite OSRS font when present
- `ui_spellbook.c` / `spell_icons.inc`
  - project the active book and backend-owned weapon/autocast compatibility
  - 195 B237 spell buttons across Standard, Ancient, Lunar and Arceuus; the
    checked-in catalog drives both icon export and required UI sprite loading
  - left click selects Cast; right click offers compatible normal/defensive
    Autocast. Eligible weapons expose both spell pickers beside their melee
    styles in Combat. Choosing melee clears autocast; powered weapons retain
    their native attacks rather than offering incompatible book spells.
- `terrain.h`
  - terrain-binary loader and terrain mesh helpers
- `objects.h`
  - static object mesh loader
- `models.h`
  - model-set loading for player and NPC meshes
- `anims.h`
  - animation-cache loading and mesh deformation helpers
- `equipment_render.h`
  - viewer-only generated item render map loader
  - maps item IDs to cache-composed ground/equipped render model IDs
    plus body-part hide masks and reviewed weapon ready/walk/run poses
- `item_render_defs.c` / `item_render_defs.h`
  - viewer-only fallback item-definition render metadata loader
  - extracts ground/equipped model IDs from `items.bin` without exposing
    those fields through `rc-core`
- `npc_render_defs.c` / `npc_render_defs.h`
  - viewer-only NPC animation/model metadata loader
  - extracts NPC stand/walk/attack/death animation IDs and model links from
    `npc_defs.bin` without exposing them through `rc-core`
  - applies the required reviewed B237 basic attack mapping from
    `data/defs/npc_attack_anims.tsv`; exact per-NPC/style combat visuals take
    precedence over this unambiguous stance-family data
- `combat_visuals.c` / `combat_visuals.h`
  - viewer-only combat presentation profile loader
  - owns projectile, spotanim, animation, attachment, and visual timing
    metadata; `rc-core` only consumes backend hit-delay profiles
  - encounter actions (`RC_COMBAT_ACTION_ENCOUNTER`) resolve by NPC definition
    and `attack_key` (`rc_combat_visual_for_npc_action`): each attack, mechanic
    part and form change has its own row. A missing attack row falls back to
    the NPC's style row, a missing part row draws nothing; both are logged once
- `hitsplats.c` / `hitsplats.h`
  - viewer-only B237 hitsplat catalog, four-slot presentation state, and basic
    hit-to-visual selection
  - consumes sequenced core hit events without owning damage or combat timing
- `object_action_visuals.c` / `object_action_visuals.h`
  - viewer-only object action presentation metadata loader
  - extracts action animation IDs from `object_behaviors.bin` without
    exposing them through `rc-core`
- `dev_validation.c` / `dev_validation.h`
  - viewer-only combat testing helpers
  - owns the temporary validation bank seed, Clan-tab boss transports,
    and Varrock-bank combat dummy spawn
  - food/potion testing stock starts at 10,000 per supplied identity; withdrawals
    use the selected ordinary quantity, including the final item
  - god mode subscribes to core damage events before death processing; it is
    a development override, not a gameplay immunity rule
  - bank lookup prefers player-equippable variants; charged Tonalztics stock
    starts with 1,000 charges and the uncharged version is also available
  - magic stock includes charged and empty powered weapons; charged stock uses
    the content owner's capacity and keeps that state through normal transfers
  - Mage starts with 100,000 of each of 23 rune types, including Body,
    combination, Sunfire and Aether runes; withdrawals use ordinary inventory rules
  - Clan tab Spellbook buttons select Standard, Ancient, Lunar or Arceuus through
    the existing queued player command, then open the Magic tab. Book changes
    clear previous spell/autocast selection. All four books use icon grids,
    independently of this testing control. Buttons lacking a usable runtime
    definition report that limitation in chat. This UI does not implement
    missing utility, teleport, area-effect or summoning content.
  - can be disabled with `RUNEC_DEV_VALIDATION=0`

## Runtime expectations

- Actors and projectile/spot effects share `all.anims`, including powered-cast
  sequences. Missing shared data is a startup error, not a silent subset fallback.
  Sequence/base lookups sort once at load and use binary search during playback.
  The former NPC/player/fallback animation overrides are removed.
- Player visual selection keeps weapon effects when ammunition supplies the
  projectile. Special `weapon:ammo` rows distinguish Dark bow dragon-arrow heads
  from other ammunition; selected spells own their cast animation. Missed spells
  use splash effects. Flight-end and impact heights are separate fields.
- Ancient Rush/Blitz use sequence 1978 and Burst/Barrage use 1979, distinct
  from Standard staff casting. Attack playback restarts for each launch and
  uses the sequence's duration instead of a fixed two-tick cutoff; NPC attack
  sequences also play their full length (a mechanic's 30-tick sequence is
  not cut short), mechanic parts without a sequence leave a running one alone,
  and `form:leave` holds its last frame for the whole transition. An NPC only
  plays a sequence on the skeleton of its own stand sequence, so a previous
  form's (or a graphic's) sequence never deforms its model.
- Tile-aimed encounter projectiles (target `RC_COMBAT_ACTOR_NONE`) land on the
  core's impact tick (`hit_delay`), so acid, rapid fire and the spawn arrive
  when their pool, strike or helper does.
- An NPC's projectiles and launch graphics are locked to the NPC as in the
  client: they start at its centre (rendered position plus half its size),
  `proj_progress` fine units (128 per tile) toward the target, at
  `proj_start_height` above the ground there. A large NPC therefore fires
  from the same body point (Vorkath's mouth) from every angle; there is no
  nearest-footprint-tile origin. Tile-sourced rows still start at the target
  tile.
- Locs the core spawns at runtime (`RC_OBJECT_STATE_SPAWNED`, e.g. Vorkath's
  acid pools) are drawn from `data/models/spawn_objects.models` and its
  `.oanim` templates (loc id, type, rotation; positions relative to the tile)
  written by `tools/cache_pipeline/export_spawn_objects.py`, on the terrain at
  the footprint centre, animated with `object.anims` when the loc has a
  sequence. A loc without a template is logged once. Encounter hazards have
  no debug tile overlay; their projectiles and locs are the presentation.
- Inventory, bank and equipment icons use native 36x32 cache-rendered PNGs,
  without auto-fit scaling, reference overlays or invented 3D/model icons.
  The exporter owns camera angles, perspective, face ordering, lighting,
  textures, stack variants and note/bought/placeholder composition. Missing
  PNGs are logged once and shown as a question mark, not a substitute item.
- Ground piles use B237 item-specific models from `data/models/ground/`,
  including cache-linked notes, recolors, textures, scaling and quantity forms.
  These are loaded on demand with a bounded cache and share `items.atlas`;
  absent models produce a diagnostic, not a gold-box substitute. Rebuild with
  `tools/cache_pipeline/export_ground_item_models.py --cache <b237-cache>`
  using the same cache as the equipment exporter. Packs include this directory.
- Testing teleports activate destination collision before selecting a landing
  tile. Landings must have an exit and a route to the boss footprint; a failed
  search rejects the teleport instead of using an unchecked default coordinate.
- `rc-viewer` expects to be launched with the project root as the
  working directory because it loads data by relative path.
- It depends on runtime assets such as:
  - `data/regions/{rx}_{ry}.p{plane}.terrain`
  - `data/regions/{rx}_{ry}.p{plane}.objects`
  - `data/regions/{rx}_{ry}.p{plane}.object_anim.models`
  - `data/regions/mapsquare.materials.atlas`
  - `data/regions/mapsquare.materials.tanim`
  - `data/regions/mapsquare.catalog`
  - `data/regions/world.collision-tiles.indexed.bin`
  - `data/defs/hitsplats.bin`
  - `data/defs/npc_attack_anims.tsv`
  - `data/defs/npc_defs.bin`
  - `data/spawns/world.npc-spawns.indexed.bin`
  - `data/spawns/world.ground-items.indexed.bin`
  - `data/models/*.models`
  - `data/models/item_render.map`
  - `data/anims/*.anims`
  - `data/sprites/ui/*.png`
- The normal viewer starts from prebuilt mapsquare data. Explicit development
  and compatibility datasets can be selected with environment overrides:
  - `RUNEC_TERRAIN`, `RUNEC_OBJECTS`, `RUNEC_CMAP`
  - `RUNEC_OBJECT_PLACEMENTS` (defaults to the indexed world placement file)
  - `RUNEC_COLLISION_TILES` (defaults to the indexed world collision file)
  - `RUNEC_NPC_DEFS`, `RUNEC_NPC_ATTACK_ANIMS`, `RUNEC_NPC_SPAWNS`,
    `RUNEC_NPC_MODELS`
  - `RUNEC_PLAYER_MODELS`, `RUNEC_ANIMS` (shared `data/anims/all.anims`)
  - `RUNEC_ITEM_MODELS`, `RUNEC_ITEM_RENDER_MAP`
  - `RUNEC_COMBAT_VISUALS`, `RUNEC_COMBAT_PROFILES`
  - `RUNEC_HITSPLATS`, `RUNEC_HITSPLAT_SPRITES`
  - `RUNEC_WORLD_ORIGIN_X`, `RUNEC_WORLD_ORIGIN_Y`,
    `RUNEC_WORLD_W`, `RUNEC_WORLD_H`
  - `RUNEC_PLAYER_START_X`, `RUNEC_PLAYER_START_Y`,
    `RUNEC_PLAYER_START_PLANE`
  - `RUNEC_WORLD_ACTIVE_RADIUS_REGIONS`,
    `RUNEC_WORLD_MAX_CACHED_REGIONS`
  - `RUNEC_VIEWER_SCENE_RADIUS_REGIONS`, `RUNEC_VIEWER_PRELOAD_RADIUS`,
    `RUNEC_VIEWER_MAX_CPU_CHUNKS`, `RUNEC_VIEWER_MAX_GPU_CHUNKS`,
    `RUNEC_VIEWER_UPLOAD_BUDGET_MB_PER_FRAME`
  - `RUNEC_MAPSQUARE_STREAMING`, `RUNEC_MAPSQUARE_DIR`,
    `RUNEC_ACTOR_DRAW_RADIUS`
  - `RUNEC_SCENE_AUTO_EXPORT=1` enables development-only mapsquare generation
    from a local b237 cache; it is off by default
  - `RUNEC_VIEWER_TELEMETRY_OVERLAY=1` for the opt-in streaming overlay
  - `RUNEC_DEV_VALIDATION`, `RUNEC_DEV_BANK_DUMMY`,
    `RUNEC_DEV_TRANSPORT_DEST`, `RUNEC_DEV_BOSS_ATTACKS`
- Those runtime assets are expected to be local in the RuneC working tree
  under `data/`, populated by `scripts/setup-data.sh` or by maintainer data
  rebuild tooling.

- The viewer must not read runtime assets from a sibling reference checkout.
  Reference repos are audit material; generated viewer assets are loaded from
  the local `data/` tree.
- It links both `rc-core` and `rc-content`, but it should still behave
  as a presentation shell rather than a second gameplay engine.

Ordinary viewer input calls the same queued player APIs used by headless
consumers. Gameplay mutations become authoritative during the next core input
phase, not during the render frame that observed the click or key. The viewer
may interpolate presentation between ticks, but it does not own action
replacement, cancellation, delayed effects, death, or cooldown progression.
After a long frame it advances at most five core ticks, records any discarded
overload ticks, and resumes from the remaining fractional accumulator.

Scene clicks provide immediate client-only feedback while the queued command
waits for that input phase. Movement uses B237's four yellow click-cross frames;
NPC, object, ground-item, and selected-target interactions use the four red
frames. Every frame lasts 100 ms. An accepted movement submission also places a
temporary minimap destination immediately. That temporary marker is reconciled
on the next core tick and is then replaced by the authoritative route endpoint
or removed when no route exists. This presentation path never computes routes,
predicts character tiles, or advances gameplay early.

Hitsplats are a read-only projection of sequenced core hit events. The viewer
uses all concrete B237 cache definitions and sprites, their cache-defined
duration/fade/offset metadata, and the client's four fixed display positions
and replacement policies. Basic combat selects the B237 zero, regular,
maximum, public-tint, and poison types. Specialty mechanics remain responsible
for selecting their own cache type; the viewer does not infer venom, healing,
prayer drain, shields, bleed, burn, or encounter semantics from damage values.

Ordinary actor health bars are also a read-only hit-event projection. A new
hit shows the B237 default `30x5` bar for 300 client cycles for either the
player or an NPC, including misses and killing hits. The green fill and red
remainder use the cache sprite colors exactly, with no decorative border. Bars
project from stable base-model logical height plus the client's 15-unit head
clearance; hitsplats remain centered on the model. Target selection alone does
not show a bar, and missing health does not leave one visible indefinitely.
While visible, bars track healing and other HP changes without restarting
their lifetime or inventing a damage splat. Divine-potion self-damage uses the
same sequenced hit path as other damage.
Special encounter/interface bars are separate authored content.

Scene and UI context options use right-click. The applicable OSRS-style menu
opens on the press frame; moving beyond the small drag threshold closes it and
continues right-drag camera orbit. The viewer presents the fixed-client menu at
a readable display scale using crisp 16-pixel bold text, a 24-pixel header, and
19-pixel rows while retaining the client's proportions, colors, hover behavior,
and NPC/object/item target colors. Moving beyond the client's 10-pixel margin
dismisses it. NPCs, objects, ground items, bare scene tiles, inventory,
equipment, prayers, spells, and decoded widgets share that presentation.
Bank slots offer Withdraw-1/5/10/X/All/All-but-1, Examine and Cancel;
inventory slots inside the bank offer the corresponding deposit actions.
X accepts a positive whole-number amount in the chat input area; Escape cancels.
Left-click transfers one; Shift-click transfers all. Menus/amount prompts reject
changed item identities. Item/Note buttons select the explicit withdrawal mode.
Deposit boxes show carried items and deposit-only actions, not bank withdrawals.
Inventory and equipment icon buttons beside Close deposit each whole container;
hovering names the action. Worn deposits do not need empty inventory slots.
Transfers use core's queued storage API; capacity, unsupported item, stale or
closed-access failures appear in chat. Closing/reopening storage clears old
quantity prompts and bank menus. A valid unnoteable item withdraws ordinarily
with an explicit notice. Full bank UI parity, search and placeholders remain out
of scope.
Inventory and bank hover labels show the default action, full orange item name,
and white additional-option count at top left, using the existing crisp bold font.
Examine, selected item/spell actions, distant ground-item Take, and Walk here
remain submissions to core. Selection clears only after accepted input, and
sequenced core interaction messages/failures are appended to chat. The viewer
does not revalidate targets or implement the resulting action.

The Prayer tab uses the generated B237 `prayer_icons.inc` catalog: 29 visible
slots with Eagle Eye/Deadeye and Mystic Might/Mystic Vigour replacements.
Core availability selects locked/available artwork; activation uses a separate
glow. The orb toggles core-owned quick prayers; its setup menu opens selection
with a Done control. Player overhead icons and failure/depletion messages
project core state. The existing testing-only validation mode seeds unlocks,
not an eligibility bypass. Unsupported effects remain visibly unavailable.
Regenerate the catalog with `export_prayer_icons.py --dump <B237-dump>` and
its sprites with `export_sprites_modern.py --cache <B237-cache> --prayers-only`.
Publish matching PRAY v2 definitions and UI sprite packs with these sources.

The terrain tile under the scene cursor is outlined with four corner brackets
using the loaded heightmap's real corner heights. An accepted movement click
immediately runs the existing core pathfinder as a read-only viewer preview and
highlights every traversed tile without waiting for a world tick. The next core
tick replaces that preview with the authoritative player route. Fill and edge
geometry are submitted in two batches. The preview never writes a destination,
route, position, or other gameplay state.

The viewer defaults preserve current behavior: scene radius 1, startup preload
radius 0, CPU/GPU chunk caps 128, and a 16 MB per-frame upload budget. Existing
`RUNEC_SCENE_RADIUS_REGIONS` and
`RUNEC_STARTUP_SCENE_RADIUS_REGIONS` overrides remain accepted.

`world_transform.[ch]` is the sole owner of viewer world-to-render conversion.
Core positions remain global tiles; the viewer transform stores the current
global render origin and provides checked, exact world/local round trips.
Every cached mapsquare chunk also records the render origin used for its
current geometry, so rebasing computes an absolute delta and repeated A-B-A
scene changes cannot accumulate position error. Window planning, prefetch,
scene transitions, and object picking reject invalid planes and handle the
lower and upper world edges symmetrically.

Normal gameplay uses a generation-tagged worker to load complete mapsquares
center-first. The worker reads loose/packed assets, decompresses pack entries,
parses terrain/static objects/animated-object models, prepares NPC model filters
from indexed spawns, and builds CPU meshes. The render thread drains results,
uploads GPU resources, attaches shared materials/shaders, retains overlap, and
evicts entries outside the new plan. A destination commits only after its full
visible window, NPC models, and backend area succeed; cancellation or failure
keeps the prior scene active.

Core-scheduled object traversals use the same transaction. The viewer consumes
the core traversal generation, selected destination, phase, and presentation
class; it does not resolve traversal rows independently. It prefetches during
approach/takeoff, starts supported climb presentation when the admitted action
begins, keeps presenting a source position/plane snapshot while a late visual
load finishes, and reveals the authoritative destination only with a complete
scene. Stale generations are discarded, and visual failure never moves or
rolls back the core player.

PR 11 hides this latency in the viewer without changing `rc-core`. The existing
async request now has a cache-only prefetch purpose: route and movement
lookahead warm one mapsquare center ahead, while an admitted object traversal
warms its core-selected destination during routing/action presentation. A matching
boundary or transport promotes that work instead of restarting it. Normal
movement does not show progress UI or block input. Active-scene culling keeps
overlapping cached chunks from drawing, and stale chunks retire incrementally
after commit within the existing hard cache limits.

GPU uploads obey the configured byte budget between upload units. Model sets
upload one entry at a time. A legacy mapsquare object mesh is one indivisible
unit, so a mesh larger than the configured budget is admitted alone to ensure
forward progress and is reported by peak-upload telemetry. Static object CPU
geometry is released after upload. The shared material page has separate
ownership from chunk geometry and is loaded once.

Split terrain uses neighboring mapsquares when calculating underlay smoothing,
lighting normals, and outer corner heights. New chunks carry a `65x65` corner
grid for their `64x64` tiles; runtime sampling clamps legacy `64x64` edges so a
player or actor cannot receive an out-of-range sentinel height at a boundary.
The shared mapsquare catalog is loaded once into an 8 KiB bitset. Cache-index
voids are removed from sparse destination plans, but every catalog-listed chunk
remains mandatory before a scene or transport commits.

Fixed and generated aggregate scenes remain an explicit development
compatibility path and are not included in runtime-data packs. Normal startup
and transitions never launch Python or read the source cache. Missing installed
assets report the first incomplete path and retain the prior visual scene.
They do not relocate the authoritative player back to that scene.
`RUNEC_SCENE_AUTO_EXPORT=1` retains bounded missing-mapsquare generation for
maintainers with a local b237 cache. Core activates destination simulation and
commits movement independently; the viewer replaces presentation only when a
complete destination window is ready.

Streaming telemetry logs startup and scene/active-area changes. It reports
worker decode and render-thread upload time, queued async jobs, staged CPU
chunks, current/peak upload bytes and budget, async frame count, maximum
main-thread streaming work, resident chunks/vertices, deduplicated texture
memory, estimated model memory/draw calls, active actors/items, and backend page
timings.

## Why it exists

RuneC is meant to support both:

- a playable OSRS game with a real client window
- high-performance headless RL sims that run without rendering

`rc-viewer` keeps those concerns separate so the simulation backend can
stay modular and fast.

## Current implementation shape

Today `rc-viewer` is primarily:
- a render/debug shell for the current world slice
- a movement/camera frontend
- a way to inspect terrain, collision, NPC placement, and animation
- an OSRS-style clickable UI shell with core-synced inventory/equipment
  state and stable intent surfaces for prayer/spell/chat integration
- cache-backed OSRS gameframe assets for the chatbox, minimap/orbs,
  side panel, side tabs, worn-slot icons, skill icons, prayer icons, and
  spell icons
- cache-backed item model overlays for worn equipment and dropped ground
  items, with generated render metadata loaded in `rc-viewer`
- a generated equipment render map that composes the player from
  default identity-kit body parts plus recolored/retextured equipped
  item models, with body-part hiding driven by viewer-only render
  metadata rather than core gameplay rules; tracked RuneC pose families supply
  item-specific ready/walk/run animations without making reference checkouts a
  build or runtime dependency
- a cache-backed circular minimap assembled from streamed per-mapsquare floor,
  wall, door, and mapscene rasters, with camera-rotated player/NPC/destination
  dots and inverse-rotated click-to-route behavior
- current-cache OpenRS2 b237 OSRS widget art for the native compass,
  minimap cover/masks, side icons, orb frames/fillers/icons, and skill
  icons; RuneLite gameval sprite IDs are the naming authority
- a renderer for core-owned dynamic object state, linked-below scenes,
  bounded mapsquare scenes, compatibility region slices, viewer-owned combat
  projectiles, spot animations, combat attack animations, and equipment visuals
- model-centered player/NPC hitsplats and footprint-centered NPC facing;
  health bars remain anchored above model bounds, while multi-tile actors face
  the target from their rendered center rather than their southwest tile
- isolated combat-validation helpers for local manual testing, kept out of
  `rc-core` so they can be disabled or removed without changing simulation
  rules

Each streamed visual chunk optionally owns a `512x512` centered minimap raster
named `<region_x>_<region_y>.p<plane>.minimap.png`. The worker decodes that PNG
alongside terrain and objects; the render thread projects resident rasters into
the `152x152` gameframe orb. The current player stays centered while the map,
compass, NPC dots, destination flag, and click transform follow camera yaw.
Missing rasters fall back pixel-by-pixel to the older terrain-derived surface,
which keeps older runtime-data packs usable until they are republished.

That means it is useful for presentation validation and the first
inventory/equipment runtime checks, but it is not yet the authority for
end-to-end gameplay validation across combat, shops, quests, banking,
or other UI-driven systems.
