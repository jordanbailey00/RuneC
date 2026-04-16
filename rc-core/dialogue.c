#include "dialogue.h"

void rc_dialogue_start(RcWorld *world, int npc_uid,
                       const RcDialogueNode *nodes, int count) {
    // TODO: set dialogue state
    (void)world; (void)npc_uid; (void)nodes; (void)count;
}

void rc_dialogue_continue(RcWorld *world) {
    (void)world;
}

void rc_dialogue_choose(RcWorld *world, int option) {
    (void)world; (void)option;
}
