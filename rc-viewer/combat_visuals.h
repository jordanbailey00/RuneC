#ifndef RUNEC_VIEWER_COMBAT_VISUALS_H
#define RUNEC_VIEWER_COMBAT_VISUALS_H

#include "../rc-core/types.h"

#define RC_MAX_COMBAT_VISUAL_DEFS 16384

enum {
    RC_COMBAT_VISUAL_ANY = -1,
    RC_COMBAT_VISUAL_ITEM = 1,
    RC_COMBAT_VISUAL_SPELL = 2,
    RC_COMBAT_VISUAL_NPC = 3,
    RC_COMBAT_VISUAL_SPECIAL = 4,
};

typedef enum {
    RC_COMBAT_VISUAL_PRIMITIVE_NONE = 0,
    RC_COMBAT_VISUAL_PRIMITIVE_LAUNCH_EFFECT = 1,
    RC_COMBAT_VISUAL_PRIMITIVE_TRAVEL_PROJECTILE = 2,
    RC_COMBAT_VISUAL_PRIMITIVE_TARGET_IMPACT = 3,
    RC_COMBAT_VISUAL_PRIMITIVE_FIXED_TILE_IMPACT = 4,
    RC_COMBAT_VISUAL_PRIMITIVE_GROUND_EFFECT = 5,
    RC_COMBAT_VISUAL_PRIMITIVE_AREA_EFFECT = 6,
    RC_COMBAT_VISUAL_PRIMITIVE_MULTI_PROJECTILE = 7,
} RcCombatVisualPrimitiveType;

typedef enum {
    RC_COMBAT_VISUAL_ATTACH_NONE = 0,
    RC_COMBAT_VISUAL_ATTACH_SOURCE_ACTOR = 1,
    RC_COMBAT_VISUAL_ATTACH_SOURCE_CENTER = 2,
    RC_COMBAT_VISUAL_ATTACH_SOURCE_TILE = 3,
    RC_COMBAT_VISUAL_ATTACH_TARGET_ACTOR = 4,
    RC_COMBAT_VISUAL_ATTACH_TARGET_TILE = 5,
    RC_COMBAT_VISUAL_ATTACH_FIXED_TILE = 6,
    RC_COMBAT_VISUAL_ATTACH_GROUND_TILE = 7,
} RcCombatVisualAttachmentRule;

typedef struct {
    uint8_t kind;
    int key_id;
    char key_name[64];
    int style;
    int stance_idx;
    int attack_anim_id;
    int launch_spotanim_id;
    int double_launch_spotanim_id;
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
    int projectile_count;
    int alt_projectile_start_height;
    int alt_projectile_end_height;
    int alt_projectile_delay;
    int alt_projectile_angle;
    int alt_projectile_length_adjustment;
    int alt_projectile_progress;
    int alt_projectile_step_multiplier;
    int aux_travel_spotanim_id;
    int aux_impact_spotanim_id;
    int aux_projectile_model_id;
    int aux_projectile_anim_id;
    int impact_on_last_only;
    int launch_spotanim_height;
    int impact_spotanim_height;
    int impact_spotanim_delay;
    int impact_spotanim_rotation;
    uint8_t primitive_type;
    uint8_t source_attachment;
    uint8_t target_attachment;
    uint8_t launch_attachment;
    uint8_t impact_attachment;
    char authority[64];
} RcCombatVisualDef;

extern RcCombatVisualDef g_rc_combat_visual_defs[RC_MAX_COMBAT_VISUAL_DEFS];
extern int g_rc_combat_visual_count;

const char *rc_combat_visual_primitive_name(int primitive_type);
const char *rc_combat_visual_attachment_name(int attachment_rule);
int rc_load_combat_visuals(const char *path);
const RcCombatVisualDef *rc_combat_visual_for_item(int item_id,
                                                   RcCombatStyle style);
const RcCombatVisualDef *rc_combat_visual_for_item_stance(
    int item_id, RcCombatStyle style, int stance_idx);
const RcCombatVisualDef *rc_combat_visual_for_spell(const char *spell_name,
                                                    RcCombatStyle style);
const RcCombatVisualDef *rc_combat_visual_for_spell_id(
    int spell_idx, const char *fallback_name, RcCombatStyle style);
const RcCombatVisualDef *rc_combat_visual_for_npc(int npc_id,
                                                  RcCombatStyle style);
const RcCombatVisualDef *rc_combat_visual_for_special_item(int item_id,
                                                           RcCombatStyle style);

#endif
