#ifndef RC_QUESTS_H
#define RC_QUESTS_H

#include "types.h"

typedef struct {
    int id;
    char name[64];
    int completed_state;
    int quest_points;
} RcQuestDef;

// Quest state per player (simple integer per quest)
#define RC_MAX_QUESTS 16

typedef struct {
    int state[RC_MAX_QUESTS];   // 0 = not started, completed_state = done
} RcQuestProgress;

int rc_quest_get_state(const RcQuestProgress *progress, int quest_id);
void rc_quest_set_state(RcQuestProgress *progress, int quest_id, int state);

#endif
