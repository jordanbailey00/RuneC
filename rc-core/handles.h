#ifndef RC_HANDLES_H
#define RC_HANDLES_H

#include <stdint.h>

// Integer handles used for cross-subsystem references. Pointers are
// NEVER stored across subsystem boundaries or across ticks. See
// rc-core/README.md §5.
//
// These are indices into the owning arrays inside RcWorld:
//   RcNpcId        → world->npcs[id]
//   RcItemSlot     → world->player.inventory[slot] / equipment[slot]
//   RcGroundItemId → world->ground_items[id]
//
// Sentinels for "none / unset":
//   RC_NPC_NONE, RC_ITEM_SLOT_NONE, RC_GROUND_ITEM_NONE

typedef uint16_t RcNpcId;
typedef uint8_t  RcItemSlot;
typedef uint16_t RcGroundItemId;

#define RC_NPC_NONE         ((RcNpcId)0xFFFF)
#define RC_ITEM_SLOT_NONE   ((RcItemSlot)0xFF)
#define RC_GROUND_ITEM_NONE ((RcGroundItemId)0xFFFF)

#endif
