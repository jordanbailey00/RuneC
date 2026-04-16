#ifndef RC_PRAYER_H
#define RC_PRAYER_H

#include "types.h"

// Prayer IDs (bitfield positions in active_prayers)
#define PRAYER_PROTECT_MAGIC  (1 << 0)
#define PRAYER_PROTECT_RANGE  (1 << 1)
#define PRAYER_PROTECT_MELEE  (1 << 2)
#define PRAYER_THICK_SKIN     (1 << 3)
#define PRAYER_BURST_OF_STR   (1 << 4)
#define PRAYER_CLARITY        (1 << 5)
#define PRAYER_SHARP_EYE      (1 << 6)
#define PRAYER_MYSTIC_WILL    (1 << 7)
#define PRAYER_STEEL_SKIN     (1 << 8)
#define PRAYER_ULTIMATE_STR   (1 << 9)
#define PRAYER_INCREDIBLE_REF (1 << 10)
#define PRAYER_EAGLE_EYE      (1 << 11)
#define PRAYER_MYSTIC_MIGHT   (1 << 12)
#define PRAYER_PIETY          (1 << 13)
#define PRAYER_RIGOUR         (1 << 14)
#define PRAYER_AUGURY         (1 << 15)

// Counter-based drain (matches OSRS exactly)
void rc_prayer_drain_tick(RcPlayer *player);

// Toggle a prayer on/off
void rc_prayer_toggle(RcPlayer *player, uint32_t prayer_bit);

// Get prayer bonus for combat (percentage modifier)
int rc_prayer_attack_bonus(uint32_t active_prayers);
int rc_prayer_strength_bonus(uint32_t active_prayers);
int rc_prayer_defence_bonus(uint32_t active_prayers);

#endif
