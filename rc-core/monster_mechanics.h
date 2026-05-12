#ifndef RC_MONSTER_MECHANICS_H
#define RC_MONSTER_MECHANICS_H

#include <stdbool.h>
#include <stdint.h>

#define RC_MONSTER_MECH_MAX_FAMILIES 64
#define RC_MONSTER_MECH_MAX_NPC_IDS 512

enum {
    RC_MONSTER_MECH_STATUS_MISSING_RUNTIME = 0,
    RC_MONSTER_MECH_STATUS_RUNTIME_INDEXED = 1,
    RC_MONSTER_MECH_STATUS_PARTIAL_RUNTIME_INDEXED = 2,
};

enum {
    RC_MONSTER_TAG_DRAGONFIRE                = 1ull << 0,
    RC_MONSTER_TAG_ICY_BREATH                = 1ull << 1,
    RC_MONSTER_TAG_EQUIPMENT_MITIGATION      = 1ull << 2,
    RC_MONSTER_TAG_POTION_MITIGATION         = 1ull << 3,
    RC_MONSTER_TAG_PRAYER_INTERACTION        = 1ull << 4,
    RC_MONSTER_TAG_EQUIPMENT_REQUIRED        = 1ull << 5,
    RC_MONSTER_TAG_STAT_DRAIN                = 1ull << 6,
    RC_MONSTER_TAG_SPECIAL_DAMAGE_PREVENTION = 1ull << 7,
    RC_MONSTER_TAG_WEAPON_DAMAGE_GATE        = 1ull << 8,
    RC_MONSTER_TAG_AMMO_DAMAGE_GATE          = 1ull << 9,
    RC_MONSTER_TAG_SPELL_DAMAGE_GATE         = 1ull << 10,
    RC_MONSTER_TAG_SPECIAL_ATTACK_EXCEPTION  = 1ull << 11,
    RC_MONSTER_TAG_FINISHER_ITEM             = 1ull << 12,
    RC_MONSTER_TAG_LOW_HP_RULE               = 1ull << 13,
    RC_MONSTER_TAG_AUTO_FINISHER_UNLOCK      = 1ull << 14,
    RC_MONSTER_TAG_AMBUSH_TRIGGER            = 1ull << 15,
    RC_MONSTER_TAG_CHILD_SPAWN               = 1ull << 16,
    RC_MONSTER_TAG_MINION_SPAWN              = 1ull << 17,
    RC_MONSTER_TAG_AREA_ATTACK               = 1ull << 18,
    RC_MONSTER_TAG_POISON                    = 1ull << 19,
    RC_MONSTER_TAG_VENOM                     = 1ull << 20,
    RC_MONSTER_TAG_DISEASE                   = 1ull << 21,
    RC_MONSTER_TAG_IMMUNITY_CHECK            = 1ull << 22,
    RC_MONSTER_TAG_CURE_INTERACTION          = 1ull << 23,
    RC_MONSTER_TAG_HEALING                   = 1ull << 24,
    RC_MONSTER_TAG_TELEBLOCK                 = 1ull << 25,
    RC_MONSTER_TAG_FREEZE                    = 1ull << 26,
    RC_MONSTER_TAG_WILDERNESS_RULE           = 1ull << 27,
    RC_MONSTER_TAG_JUMP_ATTACK               = 1ull << 28,
    RC_MONSTER_TAG_MIRROR_SHIELD_REQUIRED    = 1ull << 29,
    RC_MONSTER_TAG_NOSE_PEG_REQUIRED         = 1ull << 30,
    RC_MONSTER_TAG_EARMUFFS_REQUIRED         = 1ull << 31,
    RC_MONSTER_TAG_FACE_MASK_REQUIRED        = 1ull << 32,
    RC_MONSTER_TAG_WITCHWOOD_ICON_REQUIRED   = 1ull << 33,
};

typedef struct {
    char key[48];
    uint8_t status;
    uint32_t system_bits;
    uint64_t tag_bits;
    uint16_t npc_count;
    uint32_t npc_ids[RC_MONSTER_MECH_MAX_NPC_IDS];
} RcMonsterMechanicFamily;

extern RcMonsterMechanicFamily
    g_rc_monster_mechanic_families[RC_MONSTER_MECH_MAX_FAMILIES];
extern int g_rc_monster_mechanic_family_count;

int rc_load_monster_mechanics(const char *path);
int rc_monster_mechanics_find_family(const char *key);
bool rc_monster_mechanics_family_has_npc(int family_idx, uint32_t npc_id);
uint64_t rc_monster_mechanics_tags_for_npc(uint32_t npc_id);

#endif
