#ifndef RC_COMBAT_VISUALS_H
#define RC_COMBAT_VISUALS_H

#include "types.h"

#define RC_MAX_COMBAT_VISUAL_DEFS 8192

enum {
    RC_COMBAT_VISUAL_ANY = -1,
    RC_COMBAT_VISUAL_ITEM = 1,
    RC_COMBAT_VISUAL_SPELL = 2,
    RC_COMBAT_VISUAL_NPC = 3,
    RC_COMBAT_VISUAL_SPECIAL = 4,
};

typedef struct {
    uint8_t kind;
    int key_id;
    char key_name[64];
    int style;
    int attack_anim_id;
    int launch_spotanim_id;
    int travel_spotanim_id;
    int impact_spotanim_id;
    int projectile_model_id;
    int projectile_anim_id;
    int hit_delay;
    int client_delay;
    int projectile_start_height;
    int projectile_end_height;
    int projectile_delay;
    int projectile_angle;
    int projectile_length_adjustment;
    int projectile_progress;
    int projectile_step_multiplier;
} RcCombatVisualDef;

extern RcCombatVisualDef g_rc_combat_visual_defs[RC_MAX_COMBAT_VISUAL_DEFS];
extern int g_rc_combat_visual_count;

int rc_load_combat_visuals(const char *path);
const RcCombatVisualDef *rc_combat_visual_for_item(int item_id,
                                                   RcCombatStyle style);
const RcCombatVisualDef *rc_combat_visual_for_spell(const char *spell_name,
                                                    RcCombatStyle style);
const RcCombatVisualDef *rc_combat_visual_for_npc(int npc_id,
                                                  RcCombatStyle style);
const RcCombatVisualDef *rc_combat_visual_for_special_item(int item_id,
                                                           RcCombatStyle style);

#endif
