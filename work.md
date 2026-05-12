# Work Plan

Active roadmap only. Completed implementation history belongs in
`changelog.md`; detailed closed planning docs are retained only as reference.

Read discipline:
- Always read `AGENT.md` before repo work.
- Do not read every Markdown file on every prompt.
- Read other docs only when the user asks, the task needs that context, or the
  file is directly affected.

Execution rule:
- For code, logic, generated-data, runtime, or performance-sensitive changes,
  follow `AGENT.md`: validate with tests, coverage review, and benchmarks where
  applicable, or document blocked verification.
- For every repo change, update `changelog.md` unless the change is pure
  Markdown-only documentation.
- Keep `work.md` and `work_highlevel.md` aligned whenever priorities, next
  steps, or stop points change.

---

## Current Stop Point

`code_cleanup.md` is closed for the current pass. Return to combat fidelity
refinement next.

Cache pipeline cleanup now has the b237 data foundation, region/plane
streaming, animation/projectile foundations, dynamic object interaction
runtime, active dynamic-object transport/visual replacement fixes, and
simplified exporter orchestration in place. Linked-below terrain/object
rendering now follows the client draw-level rules for transported
basement/bridge-style areas, linked-below gameplay object placements now use
the collision/traversal scene plane, and multi-model b237 locs are exported as
composite meshes instead of partial first-model geometry. Obsolete unused
viewer export wrappers were removed; compatibility helpers remain only where
active exporters still require them.

Combat visuals now have a generated `combat_visuals.tsv` table, runtime player
visual consumers, runtime NPC visual-row support, spell-on-NPC projectile
handoff, actor-targeted projectile landing, and first-pass impact drawing.
Remaining combat work is parity/data breadth, especially magic projectile
alpha/detail for all spellbooks, non-arrow ranged projectile families, special
attacks, spell/autocast state, and combat presentation polish.

UI cleanup is closed for the current pass and tracked only as a closed
reference in `ui_cleanup.md`.

Interaction Engine v1 is closed. `interaction_engine.md` remains an archival
design/reference doc, not an active implementation lane.

Loot and ground-item interaction work is closed through the current runtime
slice. `loot_interaction.md` remains an archival design/reference doc, not an
active implementation lane.

Combat rewrite Phases 0 through 11 are structurally complete.

## Next Sequence

1. Finish combat fidelity:
   - broaden cache/content-backed projectile and spot-animation rows for all
     mage spellbooks, ranged bolt/thrown projectile families, special attacks,
     NPC/boss attacks, and equipment edge cases
   - launch/impact spotanim presentation and actor-specific projectile offsets
   - spellbook and autocast state in core gameplay
   - staff default melee behavior unless spell/autocast/manual-cast is active
   - combat animation, facing, approach feel, hitsplats, and target feedback
   - OSRS-style interaction menu/camera behavior
2. Add banking/storage runtime UI.
3. Add skilling v1.
4. Add core consumables.
5. Promote targeted encounter parity only when explicitly requested.

## Closed Reference Docs

- `interaction_engine.md`: Interaction Engine v1 design and implementation
  history. No active work remains there unless a regression is found.
- `loot_interaction.md`: loot and ground-item interaction design and
  implementation history through the current runtime slice. Remaining exact
  parity items are future polish, not a standalone active lane.
- `ui_cleanup.md`: UI asset/runtime cleanup pass through Phase 6. Remaining UI
  work is system integration or later parity polish, not an active cleanup
  phase.

## Combat Fidelity Lane

Resume this only after `code_cleanup.md` is closed or explicitly paused.

Remaining work:
- Combat visuals are now selected from `combat_visuals.tsv` for the first
  player weapon/ammo/spell pass, and NPC visual rows are runtime-supported.
  Broaden the table for all mage spellbooks, non-elemental spells, special
  attacks, boss/NPC attacks, crossbow bolts/special bolts/thrown ranged
  weapons, and equipment edge cases.
- Ranged and magic projectiles now use cache-backed spotanim model/sequence
  lookup plus RSMod/OpenRS-style projectile profile timing, orientation,
  heights, slope, start offset, actor-targeted landing, and first-pass impact
  drawing. Ranged arrows validate well in the current path; magic projectile
  alpha/detail/impact presentation and other ranged projectile families still
  need parity work. Continue tuning launch/impact spotanim presentation,
  actor-specific launch/target offsets, sequence playback breadth, and
  content-specific presentation.
- Staffs should melee by default unless a selected spell, manual cast, or
  autocast state is active.
- Spellbook selection and autocast state need to become core gameplay state
  that drives magic attack type, rune requirements, animation, projectile, hit
  delay, and UI feedback.
- Special attacks must work correctly for every weapon that has one: special
  energy cost, activation state, combat formula changes, hit count/timing,
  accuracy/damage modifiers, animations, graphics/projectiles, messages, UI
  feedback, and weapon-specific side effects must be data-backed or scripted
  through the combat/content systems.
- Viewer combat presentation must render combat state from core only.
- Combat approach, facing, attack/block animations, projectiles, hitsplats,
  target HP, and NPC movement animation should be tuned against RSMod,
  RuneLite, VoidPS, 2011Scape, and OSRS cache/reference behavior.

Interaction feel in this lane:
- Replace the current context toolbox/menu presentation with an
  OSRS-faithful menu sourced from cache/reference assets where possible.
- Right-click ground/camera pan must not open the interaction menu or fight
  camera control.
- Keep menu input as viewer intent translation only; option validation and
  dispatch stay in the interaction engine.

## Banking And Storage Runtime UI

Goal:
- Make bank/storage interactions usable from the viewer.

Scope:
- Bank object/NPC interaction dispatch through Interaction Engine v1.
- Bank container state.
- Deposit/withdraw.
- Quantity actions.
- Stack behavior.
- Simple bank UI first; OSRS-accurate bank UI later.

Known gaps:
- Tabs/placeholders.
- Collection box.
- Shop stock and economy interactions.
- Full OSRS bank interface parity.

## Skilling v1

Goal:
- First playable core skilling loops using the same interaction engine.

Initial targets:
- Woodcutting.
- Mining.
- Fishing.
- Cooking.
- Smithing or basic production-chain follow-up.

Scope:
- Object click routes to node.
- Tool and level requirements.
- Animation/timer loop.
- Resource reward.
- XP reward.
- Depletion/respawn where applicable.

Known gaps:
- Exact success rates.
- Tool-specific animation breadth.
- Banking integration for long loops.
- Random events and advanced skilling content.

## Core Consumables

Goal:
- Make food, potions, prayer restore, and boosts/drains usable through
  inventory actions.

Scope:
- Food healing.
- Potion doses.
- Prayer restore.
- Combat/stat boosts and drains.
- Consume/destroy/container replacement item transforms.
- Action delay and interrupt behavior.

Known gaps:
- Exact potion formulas for every item.
- Anti-poison/venom, overheal, special restores, and niche consumables.
- Full generated item-effect coverage from curated data.

## Deferred UI/System Integration

Current UI is acceptable for runtime work. Do not block backend progress on UI
polish unless explicitly requested.

Future UI work:
- Final OSRS minimap rendering from scene minimap colors, mapscene/
  mapfunction sprites, plane state, collision/wall/object data, and the real
  gameframe mask.
- Quick-prayer presets, autocast combat state, and final chat/message routing.
- Object/player selected-target dispatch once scene/player picking is ready.
- Bank, shop, dialogue, and other modal interfaces as their gameplay systems
  come online.
- Broader CS2/listener behavior only when a concrete UI correctness gap proves
  it is needed.

Boundary:
- `rc-core` owns gameplay state and rules.
- `rc-content` owns OSRS-specific scripts and content hooks.
- `rc-viewer` owns rendering, UI presentation, camera, animation playback, and
  input-intent translation only.

## Encounter And Boss Work

Status:
- Paused unless explicitly resumed.

Future target set when encounter work resumes:
- All GWD bosses including Nex.
- Fight Caves.
- Inferno.
- Araxxor.
- Zulrah.
- Vardorvis.
- Colosseum encounters.
- Gauntlet/Hunllef.

Deferred until explicitly promoted:
- Raids: Chambers of Xeric, Theatre of Blood, Tombs of Amascut.
- Wilderness/KBD bosses: Callisto/Artio, Venenatis/Spindel, Vetion/Calvarion,
  King Black Dragon.

## Deferred Movement, Minimap, And Transport Polish

Track these after `code_cleanup.md` Step 12 unless they block a current
runtime validation path:

- Minimap parity: replace the current temporary map basis with OSRS-correct
  minimap rendering from scene data, mapscene/mapfunction sprites, plane state,
  walls/objects/collision, NPC/player markers, and the real gameframe mask.
- Movement/click-to-tile/pathfinding polish: continue tightening tile picking,
  route selection, blocked-tile behavior, and OSRS-style pathfinding edge cases
  as more areas are manually validated.
- Static transport edge cases: Step 12 covers the current Rat Pits/Varrock
  scene-reload path and exact placement-footprint source matching. Keep
  testing a broader representative set of ladders, stairs, doors, cave
  entrances, dungeon entrances, rifts, portals, and same-plane `+6400` area
  moves; add missing rows or source-anchor corrections where real object
  options still fail.
- Instance-specific transports: implement explicit handling for
  instance-flagged transports, template chunks, dynamic chunk remapping, and
  activity-specific destination loading instead of treating those as normal
  static-world coordinate moves.

## Deferred Rendering And Asset Corrections

Track these after the current cache/interaction cleanup unless a defect blocks
active validation:

- Correct transported-area rendering and asset parity defects found during
  broader dungeon/cave/portal validation.
- Audit and fix remaining terrain, object, item, equipment, NPC, projectile,
  texture, material, alpha, animation, lighting, and UI asset discrepancies
  against the b237 cache and reference behavior.
- Keep fixes systemic in the cache/export/render path; avoid one-off visual
  hacks unless a specific OSRS content script requires one.

## Deferred Performance And Optimization

Track these after the current cache/interaction cleanup unless they block a
current validation path:

- Frontend/viewer performance: do not load, animate, or render every NPC in
  the game. Keep active NPC models, animations, overhead state, minimap
  markers, and draw work limited to a small player-visible radius plus any
  explicitly needed preload margin.
- Backend/runtime performance: audit interaction dispatch, object state,
  collision updates, NPC ticking, pathfinding, transport loading, and scene
  streaming so active gameplay work scales with the local area around the
  player instead of broad world scans.
- Asset/runtime loading: keep scene slices, animation/model packs, and dynamic
  object replacement data cached and incrementally updated. Avoid full scene
  rebuilds or broad regenerated sidecars during normal interaction use.
- Add benchmarks for viewer frame pacing and core tick cost before broadening
  world/NPC/content coverage further.

## Deferred Sound And Audio

Track this after the current cache/interaction/combat priorities unless it
blocks a validation path:

- Sound and audio work implementation and correctness: add cache-backed sound
  effect/music/synth loading, object open/close sounds, combat/projectile
  sounds, UI sounds, area music, distance/plane attenuation, and runtime hooks
  so audio behavior is correct without blocking Step 12 dynamic-object work.
