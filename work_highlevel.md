# RuneC High-Level Work Map

Human-facing status only. Detailed active planning lives in `work.md` and
`code_cleanup.md`; implementation history lives in `changelog.md`. Closed
reference docs are listed below.

## Current Position

Database completion v1 is closed.

The current playable slice has a usable OSRS-style viewer, inventory and
equipment runtime, basic combat stats, NPC attack flow, ground-item handling,
and a data-backed object/context interaction path.

Interaction Engine v1, loot/ground items, and the structural combat rewrite
are complete enough for the current runtime slice.

UI cleanup is complete enough for this pass and remains documented in
`ui_cleanup.md`.

Cache pipeline cleanup in `code_cleanup.md` is closed for the current pass.
The b237 cache foundation, region/plane streaming, animation/projectile
foundations, dynamic object interaction runtime, simplified exporter
orchestration, active dynamic-object transport/visual replacement fixes, and
linked-below terrain/object render-level fixes are in place. Linked-below
gameplay object placements now use the collision/traversal scene plane, and
multi-model b237 locs now export as composite meshes instead of partial
first-model geometry. Safe obsolete-path retirement is also complete.

Next is combat fidelity work in `work.md`. Combat Phases 0 through 11 are
structurally complete; the remaining combat work is parity refinement and data
breadth, not another rewrite. The first data-backed combat visual table,
runtime player consumers, and runtime NPC visual-row support are in place.
Manual spell-on-NPC projectile handoff and visible world-object animation
playback are in place. Standard spellbook fallback icons/names and first-pass
cache-backed projectile model orientation/scale/cleanup are corrected, and
projectile travel now uses RSMod/OpenRS-style profile fields for the current
viewer. Actor-targeted landing and first-pass impact drawing are in place, but
magic projectile alpha/detail across all spellbooks and non-arrow ranged
projectile families remain combat-fidelity work.

Encounter expansion is paused unless explicitly resumed.

## Next Sequence

1. Finish combat fidelity: broaden projectile/spotanim rows for all mage
   spellbooks, ranged bolts/thrown weapons, special attacks, and NPC/boss
   attacks; fix magic projectile alpha/detail and impact presentation; add
   launch spotanims and actor-specific projectile offsets; add spell/autocast
   state, staff default behavior, combat animation/facing/approach feel, and
   OSRS-style menu/camera behavior.
2. Add banking/storage runtime UI.
3. Add skilling v1.
4. Add core consumables.
5. Promote targeted encounter parity only when explicitly requested.

## Boundaries

`rc-core` owns gameplay state and rules.

`rc-content` owns OSRS-specific scripts and content hooks.

`rc-viewer` owns rendering, UI presentation, camera, animation playback, and
input-intent translation only.

Do not put gameplay rules in the viewer.

## Closed Reference Docs

- `interaction_engine.md`: closed Interaction Engine v1 reference.
- `loot_interaction.md`: closed loot/ground-item interaction reference.
- `ui_cleanup.md`: closed UI cleanup pass reference.

## Deferred

- Full cache/asset parity beyond the current b237 viewer slice.
- Remaining UI/system parity beyond the current cleanup pass: final minimap
  accuracy, bank/shop/dialogue surfaces, quick-prayer/autocast/chat routing,
  broader listener/script behavior, and any missing item-icon/font edge cases.
- Movement, click-to-tile, and pathfinding polish for remaining tile selection
  and route edge cases discovered during manual validation.
- Static transport edge-case testing for representative dungeons, caves,
  stairs, ladders, doors, rifts, portals, and same-plane `+6400` moves.
- Dynamic object validation for remaining door/gate/shortcut edge cases that
  prove they need script-specific metadata beyond the current generic registry.
- Instance-specific transport logic for instance-flagged transports, template
  chunks, dynamic chunk remapping, and activity-specific destination loading.
- Combat polish beyond the current fidelity pass, including exact mage
  projectile alpha/detail for all spellbooks and broader ranged projectile
  families beyond the currently validated arrow path.
- Broader world streaming and full instanced-area coverage beyond the Step 10
  static traversal/generated-slice pass.
- Broader gameplay polish such as run energy, noted items, spawn overrides,
  quests, and exact encounter parity.
