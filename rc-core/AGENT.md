# rc-core Agent Rules

`rc-core` is the headless gameplay backend. Keep it runnable without a
window, renderer, graphics library, UI layer, audio system, or viewer asset
pipeline.

Mandatory boundary:

- Gameplay logic belongs in `rc-core`: ticks, movement, collision, combat
  math, pending hits, inventory/equipment rules, drops, shops, storage,
  skilling, NPC behavior, encounters, objects, traversal, varbits, and
  deterministic state transitions.
- Presentation logic belongs in `rc-viewer`: rendering, camera, input
  translation, UI/HUD, sprites, textures, meshes, model IDs, animation
  playback, spotanim/projectile visuals, validation-only visual helpers, and
  anything whose purpose is "what the user sees."
- `rc-core` must not include or depend on `rc-viewer`, raylib, renderer
  headers, UI code, texture/sprite/model loaders, or viewer-only asset maps.
- If a cache/generated gameplay binary also contains render metadata,
  `rc-core` may read and skip those bytes for format compatibility, but must
  not store or expose them in core structs. Add a viewer-owned loader for
  presentation metadata instead.
- Core events and query APIs should expose backend facts only: actor IDs,
  tile positions, styles, hit delays, action kinds, HP, inventory/equipment
  state, and other gameplay state. Do not add animation IDs, sprite names,
  model IDs, colors, camera hints, UI layout, or render timing to core APIs.
- `rc-core` tests should assert gameplay behavior. Tests that validate
  visual metadata should compile the relevant `rc-viewer` loader explicitly.

When changing this module, run a boundary audit before finishing. At minimum,
search `rc-core` for render/presentation terms such as `visual`, `render`,
`sprite`, `texture`, `model_id`, `spotanim`, `projectile_model`, `ui`,
`raylib`, and `animation` and justify or remove any matches.
