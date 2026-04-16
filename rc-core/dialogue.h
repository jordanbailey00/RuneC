#ifndef RC_DIALOGUE_H
#define RC_DIALOGUE_H

#include "types.h"

typedef enum {
    DIALOGUE_NONE,
    DIALOGUE_NPC_CHAT,
    DIALOGUE_PLAYER_CHAT,
    DIALOGUE_OPTIONS,
    DIALOGUE_ITEM_GIVEN,
} RcDialogueType;

typedef struct {
    RcDialogueType type;
    int npc_id;
    char text[256];
    char options[5][64];
    int option_count;
    int next_state[5];      // state after each option (or after continue)
} RcDialogueNode;

// Current dialogue state (part of world, not player, since it's UI state)
typedef struct {
    bool active;
    int npc_uid;
    int node_index;
    const RcDialogueNode *nodes;
    int node_count;
} RcDialogueState;

void rc_dialogue_start(RcWorld *world, int npc_uid, const RcDialogueNode *nodes, int count);
void rc_dialogue_continue(RcWorld *world);
void rc_dialogue_choose(RcWorld *world, int option);

#endif
