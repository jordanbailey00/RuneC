# Loot And Ground Item Implementation

RuneC now routes loot, ground-item lifetime, pickup, player drops, and NPC
death loot through local `rc-core` data structures. The viewer only renders the
resulting ground-item state.

## Ground Item Model

- `RcWorld` owns a fixed `ground_items` array of `RcGroundItem` records.
- `RcGroundItem` stores `uid`, `version`, `item_id`, `quantity`, `x`, `y`,
  `plane`, `owner_uid`, `original_owner_uid`, `reveal_timer`,
  `despawn_timer`, `visibility`, and `active`.
- Visibility states are public, private, and permanent-private.
- `uid` and `version` are used to reject stale interaction targets after a
  ground item has changed, merged, despawned, or been picked up.

## Spawn And Lifetime

- `rc_ground_item_spawn` is the central spawn path for player drops, NPC loot,
  and direct ground-item creation.
- Stackable items on the same tile/plane and owner merge into an existing
  active pile when safe.
- Non-stackable item quantities split into individual ground-item records.
- Quantity merge uses overflow checks; invalid/overflowing requests fail
  without corrupting the pile.
- Tile pile cap behavior rejects excess piles deterministically instead of
  silently overwriting unrelated items.
- World ticking advances reveal and despawn timers. Private items can become
  public after their reveal timer; despawn removes the active record.

## Pickup

- Ground-item pickup is implemented as an interaction target plus a concrete
  take helper.
- `rc_player_take_ground_item(world, index, expected_uid, expected_version)`
  validates the slot, active state, uid/version, visibility, and inventory
  capacity before mutating state.
- Successful pickup inserts the item into inventory, clears the ground item,
  bumps stale-target state through the item version, and emits
  `RC_EVT_ITEM_PICKED_UP`.
- Full inventory returns `RC_GROUND_TAKE_FULL` and leaves the ground item
  active.
- Stale uid/version returns `RC_GROUND_TAKE_STALE` and leaves the ground item
  active.
- Same-tile pickup and route-then-pickup both use the same take path after the
  interaction processor reaches the target.

## Player Drops

- `rc_player_drop_item` removes the selected inventory slot and spawns the item
  at the player's current tile/plane through `rc_ground_item_spawn`.
- Successful drops emit `RC_EVT_ITEM_DROPPED`.
- Tradeable and untradeable item visibility/timer behavior is assigned during
  the same centralized spawn path used by other ground items.
- Current inventory slots preserve only `item_id` and `quantity`; charged or
  instance-specific item metadata is not represented in `RcInvSlot`.

## NPC Death Loot

- NPC death handling calls `rc_roll_npc_loot` when the loot subsystem is
  enabled and the death source is the local player.
- `rc_roll_npc_loot` reads generated `RcDropTable` and `RcDropEntry` data from
  `rc-core/drops.c`.
- The roller emits guaranteed drops, selects one weighted main drop, applies
  quantity ranges, and keeps tertiary rows on the same data path.
- Rows with `rarity_inv == 0` remain loaded in the DB but are not auto-spawned
  by the runtime roller.
- Rolled drops spawn at the NPC death tile through `rc_ground_item_spawn`, so
  ownership, private/public reveal, despawn, stack merging, quantity splitting,
  and pile caps are all shared with player drops.

## Interaction Integration

- Ground items use `RC_INTERACTION_GROUND_ITEM` targets in the shared
  interaction engine.
- Target snapshots store item id, tile, plane, quantity, ground-item instance,
  uid/version, and ownership/visibility state needed by pickup validation.
- The default `Take` handler routes, faces, validates, and dispatches through
  the interaction processor before calling the take helper.
- Item-on-ground-item and spell-on-ground-item keep source item/spell metadata
  in the interaction dispatch key.
- Unsupported item-on/spell-on ground-item actions fail through
  `RC_INTERACTION_FAIL_NO_HANDLER` and do not mutate source inventory or target
  ground-item state.

## Viewer Contract

- `rc-viewer` reads ground items from core state and renders them with the
  existing local item asset path.
- Rendering uses item render metadata, `ground_model_id`, `items.models`, item
  PNGs where applicable, and quantity labels for stacks.
- Gameplay fields such as owner, timers, uid, and version are core-only and do
  not change the viewer's authority: the viewer displays active ground items
  and translates input into interaction requests.
