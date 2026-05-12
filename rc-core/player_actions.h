#ifndef RC_PLAYER_ACTIONS_H
#define RC_PLAYER_ACTIONS_H

#include <stdint.h>

#define RC_MAX_PLAYER_ACTIONS 32

enum {
    RC_PLAYER_ACTION_WALK_TO = 0,
    RC_PLAYER_ACTION_RUN_TO = 1,
    RC_PLAYER_ACTION_ATTACK_NPC = 2,
    RC_PLAYER_ACTION_SET_PRAYER = 3,
    RC_PLAYER_ACTION_EAT = 4,
    RC_PLAYER_ACTION_DRINK = 5,
    RC_PLAYER_ACTION_EQUIP = 6,
    RC_PLAYER_ACTION_UNEQUIP = 7,
    RC_PLAYER_ACTION_INTERACT_NPC = 8,
    RC_PLAYER_ACTION_INTERACT_OBJECT = 9,
    RC_PLAYER_ACTION_DROP_ITEM = 10,
    RC_PLAYER_ACTION_PICKUP_ITEM = 11,
    RC_PLAYER_ACTION_SELECT_SPELL = 12,
};

typedef struct {
    char name[32];
    uint32_t required_subsystems;
    uint8_t kind, loaded;
} RcPlayerActionDef;

extern RcPlayerActionDef g_rc_player_actions[RC_MAX_PLAYER_ACTIONS];
extern int g_rc_player_action_count;

int rc_load_player_actions(const char *path);
const RcPlayerActionDef *rc_player_action_def_get(int action_id);
int rc_player_action_allowed(uint32_t enabled, int action_id);

#endif
