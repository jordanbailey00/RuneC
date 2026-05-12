# UI Asset And Runtime Implementation

RuneC now has a repo-local b237 UI asset and runtime path. The viewer uses
local decoded interface data, local sprites, local fonts, local item icons, and
`rc-core` APIs for gameplay-facing actions. Reference repos are not called at
runtime.

## Asset Export

- `tools/cache_pipeline/rc_cache/interfaces.py` decodes b237 IF1 and IF3
  components, including ids, parent/child links, layout fields, scroll extents,
  text, colors, sprites, models, actions, click masks, listener payloads, and
  var/inv/stat transmit trigger arrays.
- `tools/cache_pipeline/export_ui_interfaces.py` resolves the core gameframe
  groups from the local Joshua-F symbol dump and writes:
  - `data/ui/interface_manifest.json`
  - `data/ui/interface_debug.txt`
  - `data/ui/interfaces.bin`
- `interfaces.bin` version 2 stores the exported component tree plus listener
  and transmit metadata. The C loader remains backward-compatible with the
  earlier v1 binary.
- `tools/cache_pipeline/export_sprites_modern.py` exports b237 UI sprites into
  `data/sprites/ui/`; `tools/cache_pipeline/validate_ui_assets.py` validates
  required UI sprites and selected transparent cutout assets.
- Item icons are local files under `data/sprites/items/item_<id>.png`.
  `data/sprites/items/item_stack_variants.tsv` maps base item ids and
  quantities to display item ids so stacks such as coins/arrows use the correct
  icon variant.
- Runtime fonts are loaded only from local `data/fonts/runescape.ttf` and
  `data/fonts/runescape_small.ttf`; text rendering strips client markup,
  rounds draw positions, and point-samples the font atlas for sharper UI text.

## Interface Loader And Renderer

- `rc-viewer/ui_interfaces.h` and `rc-viewer/ui_interfaces.c` load
  `data/ui/interfaces.bin` into `RuneCUiInterfaceStore`.
- The store owns decoded groups, components, action strings, listener masks,
  listener payload values, transmit triggers, and a bounded sprite cache.
- The decoded renderer supports layer, rectangle, text, sprite/graphic, tiled
  sprite, line, and basic model-widget handling.
- Rendering applies decoded parent layout, child offsets, scroll positions,
  clipping/scissor rectangles, sprite flip, sprite tiling, sprite opacity,
  shadow color, and border outlines.
- Runtime overrides can replace decoded component text, hidden state, model id,
  item id/quantity, selected state, animation id, color, and scroll position.
- Item-container overrides populate decoded inventory and worn-item component
  slots from live runtime state instead of baking item contents into the
  exported interface binary.
- Hit testing walks opened component trees in reverse child order and returns
  the deepest interactive component with group id, component id, screen rect,
  action strings, target verb, listener mask, and click-mask data.

## Runtime State

- `rc-viewer/ui.h` defines `RuneCUiState`, which owns active tab state,
  inventory/equipment slot mirrors, combat style data, skill values, prayers,
  selected item/spell target state, drag state, context menu state, minimap
  pixels/dots, item-icon cache, decoded interface store, opened interfaces,
  component overrides, item-container overrides, and decoded event masks.
- The viewer opens `toplevel_osrs_stretch` as the top interface and mounts
  chatbox, minimap/orbs, overlays, modals, and side-content groups through the
  local opened-interface table.
- Side-tab switching updates the opened side-content interface entry; decoded
  rendering reads that entry instead of relying on hardcoded tab rectangles.
- Active modals and side overlays capture hit testing and Escape close behavior
  through their opened-interface mount rectangles.
- `RUNEC_UI_DECODED=1` enables decoded side/interface rendering. The manual UI
  path remains as the fallback renderer.
- `RUNEC_UI_RUNTIME_SELFTEST=1` runs the viewer UI runtime selftest after the
  same initialization path used by normal rendering.

## Input And Gameplay Hooks

- `runec_ui_handle_input` translates mouse/keyboard state into typed
  `RuneCUiIntent` values. It handles tab switching, context actions,
  inventory/equipment slot selection, inventory dragging, selected item/spell
  targets, prayer clicks, spell clicks, combat style clicks, run toggle,
  special-attack toggle, minimap clicks, chat focus, and chat send.
- Decoded component dispatch validates opened state and event masks before
  producing a component action intent.
- Inventory hooks call `rc-core` for equip, drop, remove, drag/reorder,
  item-on-item, item-on-widget, item-on-NPC, and item-on-ground-item paths.
- Equipment hooks call the same core interaction surface for remove/examine
  style actions.
- Prayer buttons call `rc_player_set_prayer`; active state is mirrored back
  into UI and rendered with `prayeron_*` versus `prayeroff_*` sprites.
- Magic buttons enter selected-spell mode and can dispatch spell-on-item,
  spell-on-component, spell-on-NPC, and spell-on-ground-item intents.
- Combat tab rendering uses local b237 `combat_interface_weapon_category` data
  and `combaticons*` sprites. The selected weapon category comes from core
  combat state, so style labels/icons/modes change with the equipped weapon.
- Stats tab renders b237 skill icons and live `RcSkills` current/base values.
- Minimap rendering uses local scene/terrain/collision data plus live player
  and NPC dots. The downloaded world-map image is only an explicit debug
  fallback behind `RUNEC_MINIMAP_WORLD_MAP=1`.
- Chat focus/send are represented as typed intents and hook emissions.

## Listener Bridge

- Decoded listeners and transmit triggers are preserved in `interfaces.bin`
  and loaded into C structures even when they are not handled.
- `rc-viewer/ui.c` implements a narrow table-driven listener registry keyed by
  decoded group/component id and listener kind.
- Current local handlers cover magic spellbook filter load/click behavior,
  worn-equipment stats modal open, guide-price side overlay open,
  death-kept-items modal open, follower-call hook, modal/overlay hit capture,
  Escape close, and var/inv/stat transmit dispatch after runtime state refresh.
- This is a local handler bridge, not a CS2 VM.
