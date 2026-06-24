#ifndef RC_COMBAT_PROFILES_H
#define RC_COMBAT_PROFILES_H

#include "types.h"

#define RC_MAX_COMBAT_PROFILE_DEFS 16384

enum {
    RC_COMBAT_PROFILE_ANY = -1,
    RC_COMBAT_PROFILE_ITEM = 1,
    RC_COMBAT_PROFILE_SPELL = 2,
    RC_COMBAT_PROFILE_NPC = 3,
    RC_COMBAT_PROFILE_SPECIAL = 4,
};

typedef struct {
    uint8_t kind;
    int key_id;
    char key_name[64];
    int style;
    int stance_idx;
    int hit_delay;
} RcCombatProfileDef;

extern RcCombatProfileDef g_rc_combat_profile_defs[RC_MAX_COMBAT_PROFILE_DEFS];
extern int g_rc_combat_profile_count;

int rc_load_combat_profiles(const char *path);
const RcCombatProfileDef *rc_combat_profile_for_item(int item_id,
                                                     RcCombatStyle style);
const RcCombatProfileDef *rc_combat_profile_for_item_stance(
    int item_id, RcCombatStyle style, int stance_idx);
const RcCombatProfileDef *rc_combat_profile_for_spell(const char *spell_name,
                                                      RcCombatStyle style);
const RcCombatProfileDef *rc_combat_profile_for_spell_id(
    int spell_idx, const char *fallback_name, RcCombatStyle style);
const RcCombatProfileDef *rc_combat_profile_for_npc(int npc_id,
                                                    RcCombatStyle style);
const RcCombatProfileDef *rc_combat_profile_for_special_item(
    int item_id, RcCombatStyle style);

#endif
