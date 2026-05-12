#ifndef RC_PRAYER_H
#define RC_PRAYER_H

#include "types.h"

#define RC_MAX_PRAYER_DEFS 64

enum {
    RC_PRAYER_THICK_SKIN = 0,
    RC_PRAYER_BURST_OF_STRENGTH = 1,
    RC_PRAYER_CLARITY_OF_THOUGHT = 2,
    RC_PRAYER_SHARP_EYE = 3,
    RC_PRAYER_MYSTIC_WILL = 4,
    RC_PRAYER_ROCK_SKIN = 5,
    RC_PRAYER_SUPERHUMAN_STRENGTH = 6,
    RC_PRAYER_IMPROVED_REFLEXES = 7,
    RC_PRAYER_RAPID_RESTORE = 8,
    RC_PRAYER_RAPID_HEAL = 9,
    RC_PRAYER_PROTECT_ITEM = 10,
    RC_PRAYER_HAWK_EYE = 11,
    RC_PRAYER_MYSTIC_LORE = 12,
    RC_PRAYER_STEEL_SKIN = 13,
    RC_PRAYER_ULTIMATE_STRENGTH = 14,
    RC_PRAYER_INCREDIBLE_REFLEXES = 15,
    RC_PRAYER_PROTECT_FROM_MAGIC = 16,
    RC_PRAYER_PROTECT_FROM_MISSILES = 17,
    RC_PRAYER_PROTECT_FROM_MELEE = 18,
    RC_PRAYER_EAGLE_EYE = 19,
    RC_PRAYER_MYSTIC_MIGHT = 20,
    RC_PRAYER_RETRIBUTION = 21,
    RC_PRAYER_REDEMPTION = 22,
    RC_PRAYER_SMITE = 23,
    RC_PRAYER_PRESERVE = 24,
    RC_PRAYER_CHIVALRY = 25,
    RC_PRAYER_DEADEYE = 26,
    RC_PRAYER_MYSTIC_VIGOUR = 27,
    RC_PRAYER_PIETY = 28,
    RC_PRAYER_RIGOUR = 29,
    RC_PRAYER_AUGURY = 30,
};

#define PRAYER_THICK_SKIN     (1u << RC_PRAYER_THICK_SKIN)
#define PRAYER_BURST_OF_STR   (1u << RC_PRAYER_BURST_OF_STRENGTH)
#define PRAYER_CLARITY        (1u << RC_PRAYER_CLARITY_OF_THOUGHT)
#define PRAYER_SHARP_EYE      (1u << RC_PRAYER_SHARP_EYE)
#define PRAYER_MYSTIC_WILL    (1u << RC_PRAYER_MYSTIC_WILL)
#define PRAYER_STEEL_SKIN     (1u << RC_PRAYER_STEEL_SKIN)
#define PRAYER_ULTIMATE_STR   (1u << RC_PRAYER_ULTIMATE_STRENGTH)
#define PRAYER_INCREDIBLE_REF (1u << RC_PRAYER_INCREDIBLE_REFLEXES)
#define PRAYER_PROTECT_MAGIC  (1u << RC_PRAYER_PROTECT_FROM_MAGIC)
#define PRAYER_PROTECT_RANGE  (1u << RC_PRAYER_PROTECT_FROM_MISSILES)
#define PRAYER_PROTECT_MELEE  (1u << RC_PRAYER_PROTECT_FROM_MELEE)
#define PRAYER_EAGLE_EYE      (1u << RC_PRAYER_EAGLE_EYE)
#define PRAYER_MYSTIC_MIGHT   (1u << RC_PRAYER_MYSTIC_MIGHT)
#define PRAYER_PIETY          (1u << RC_PRAYER_PIETY)
#define PRAYER_RIGOUR         (1u << RC_PRAYER_RIGOUR)
#define PRAYER_AUGURY         (1u << RC_PRAYER_AUGURY)
#define PRAYER_DEADEYE        (1u << RC_PRAYER_DEADEYE)
#define PRAYER_MYSTIC_VIGOUR  (1u << RC_PRAYER_MYSTIC_VIGOUR)

typedef struct {
    char name[64];
    uint16_t varbit, groups;
    uint8_t id, level, drain, flags, loaded;
    int8_t attack, strength, defence;
    int8_t ranged_attack, ranged_strength;
    int8_t magic_attack, magic_defence, magic_damage;
} RcPrayerDef;

extern RcPrayerDef g_rc_prayer_defs[RC_MAX_PRAYER_DEFS];
extern int g_rc_prayer_count;

int rc_load_prayers(const char *path);
const RcPrayerDef *rc_prayer_def_get(int prayer_id);
uint32_t rc_prayer_bit(int prayer_id);

// Counter-based drain (matches OSRS exactly)
void rc_prayer_drain_tick(RcPlayer *player);

// Toggle by prayer id, clearing conflicting groups when enabling.
void rc_prayer_toggle(RcPlayer *player, int prayer_id);

// Get prayer bonus for combat (percentage modifier)
int rc_prayer_attack_bonus(uint32_t active_prayers);
int rc_prayer_strength_bonus(uint32_t active_prayers);
int rc_prayer_defence_bonus(uint32_t active_prayers);
int rc_prayer_ranged_attack_bonus(uint32_t active_prayers);
int rc_prayer_ranged_strength_bonus(uint32_t active_prayers);
int rc_prayer_magic_attack_bonus(uint32_t active_prayers);
int rc_prayer_magic_damage_bonus(uint32_t active_prayers);

#endif
