#ifndef RC_HANDLES_H
#define RC_HANDLES_H

#include <stdint.h>

// Integer handles used for cross-subsystem references. Pointers are
// NEVER stored across subsystem boundaries or across ticks. See
// rc-core/README.md §5.
//
// NPC IDs are world-local durable identities and must be resolved through
// rc_npc_resolve(). They are not array slots and are not reused while a world
// is alive. Item and ground-item handles remain owner-array slots:
//   RcItemSlot     → world->player.inventory[slot] / equipment[slot]
//   RcGroundItemId → world->ground_items[id]
//
// Sentinels for "none / unset":
//   RC_NPC_NONE, RC_ITEM_SLOT_NONE, RC_GROUND_ITEM_NONE

typedef uint32_t RcNpcId;
typedef uint64_t RcTick;     // monotonic world tick; see rc-core/README.md §3
typedef uint8_t  RcItemSlot;
typedef uint16_t RcGroundItemId;

#define RC_NPC_NONE         ((RcNpcId)UINT32_MAX)
#define RC_ITEM_SLOT_NONE   ((RcItemSlot)0xFF)
#define RC_GROUND_ITEM_NONE ((RcGroundItemId)0xFFFF)

#endif
