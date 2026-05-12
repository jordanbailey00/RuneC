# Interaction Engine Implementation

RuneC now has a shared `rc-core` interaction engine for NPCs, objects, ground
items, inventory items, equipment items, widgets, item-on targets, and
spell-on targets. The viewer requests typed interactions; core owns pending
state, routing, validation, dispatch, combat handoff, and failure state.

## Core Data Model

- `rc-core/types.h` defines `RcInteractionKind`, `RcInteractionOp`,
  `RcInteractionFailure`, `RcInteractionTarget`, and
  `RcPendingInteraction`.
- `RcInteractionKind` covers NPC, object, ground item, inventory item,
  equipment item, player, and widget targets.
- `RcInteractionOp` covers OP1 through OP5, examine, item-use-on target,
  spell-on target, and widget action.
- `RcInteractionTarget` is a durable target snapshot. It stores target kind,
  entity uid/generation, definition id, content group, tile, plane, footprint,
  inventory slot, equipment slot, widget/component ids, and ground-item
  instance id.
- `RcPendingInteraction` stores the active request for the local player:
  source actor uid, selected op, option text, target snapshot, source item id,
  source spell id, source widget/component ids, approach range, requested move
  mode, lifecycle flags, AP range state, and last failure.
- One active pending interaction is stored on `RcPlayer`. Starting a new
  interaction replaces the previous one deterministically.

## Public API

- `rc-core/interaction.h` exposes begin/cancel/clear/query APIs:
  `rc_interaction_begin`, `rc_interaction_begin_with_source`,
  `rc_interaction_cancel`, `rc_interaction_clear`,
  `rc_interaction_is_active`, `rc_interaction_get`, and
  `rc_interaction_target_kind`.
- `rc_interaction_begin_with_source` is used for item-on, spell-on, and widget
  actions that require source item/spell/widget metadata in the dispatch key.
- Target validation is structural: enum validity, required ids, valid planes,
  slot bounds, footprint sanity, widget/component ids, and approach-range
  bounds.
- Cancellation records a concrete `RcInteractionFailure` while marking the
  pending interaction inactive.

## Handler Dispatch

- `RcInteractionDispatchKey` contains target kind, op, definition id, content
  group, source item id, source spell id, widget id, and component id.
- `RC_INTERACTION_KEY_ANY` is the wildcard for optional dispatch dimensions.
- `rc_interaction_register_handler` registers a handler function and context
  for a dispatch key.
- `rc_interaction_find_handler` matches registered keys against the active
  pending key and chooses the highest-specificity match. Definition id,
  content group, source item/spell, and widget/component ids all contribute to
  specificity.
- `rc_interaction_dispatch` validates the active pending target, finds a
  handler, and calls it. Missing handlers return
  `RC_INTERACTION_FAIL_NO_HANDLER`.
- Handler results can complete, cancel, continue approach, start combat,
  return a message, return failure, or report a system handoff for dialogue,
  shop, bank, or skilling systems.

## Tick Processor

- `rc-core/tick.c` processes active pending interactions during
  `rc_world_tick`.
- NPC interactions revalidate live uid/generation, target alive state,
  interactability, tile, plane, footprint, and movement each tick.
- Object interactions validate the exact placed object where placement data is
  available; broad same-id radius matching is not used for current transport
  paths.
- Ground-item interactions validate active state, uid/version, visibility,
  tile, plane, and reach before dispatch.
- The processor routes the player toward the target footprint, refreshes
  moving target snapshots, checks approach range, sets facing, then dispatches
  once reachable.
- Cannot-reach, missing target, stale target, dead target, invalid option, and
  no-handler states are recorded as interaction failures instead of silently
  firing behavior.

## Integrated Target Types

- NPC options create `RC_INTERACTION_NPC` targets from live NPC state and
  decoded NPC option data. Attack is a normal registered NPC option handler,
  not a viewer special case.
- NPC attack handoff enters core combat target lock; repeated attack timing,
  hits, retaliation, death, and respawn are owned by combat after the
  interaction completes.
- Object clicks create `RC_INTERACTION_OBJECT` targets with definition id,
  tile, plane, footprint, type/shape where available, and selected option.
- Static object traversal uses coordinate-backed object interaction APIs and
  authoritative traversal rows. Transport rows are matched against the clicked
  placement's exact footprint/edge instead of an object-id radius fallback.
- Ground-item `Take` creates `RC_INTERACTION_GROUND_ITEM` targets and dispatches
  through the same route/facing/reach/failure path as NPCs and objects.
- Inventory and equipment actions create inventory/equipment targets and route
  through dispatch when they need core behavior.
- Widget actions create widget targets with group/component ids and preserve
  selected source metadata for item-on-widget and spell-on-widget.
- Item-on and spell-on targets keep source item/spell ids in
  `RcPendingInteraction`, so content handlers can distinguish direct OP
  actions from selected-target actions.

## Ownership Boundaries

- `rc-core` owns pending state, target validation, route/reach/facing,
  dispatch, combat handoff, ground-item mutation, inventory/equipment mutation,
  and failure reasons.
- `rc-content` is the intended owner for OSRS-specific scripts and registered
  content handlers.
- `rc-viewer` owns menu presentation, hit translation, and rendering only. It
  must not decide gameplay behavior after translating a click into a typed
  interaction request.
