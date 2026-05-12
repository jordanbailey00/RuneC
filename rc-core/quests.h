#ifndef RC_QUESTS_H
#define RC_QUESTS_H

#include "types.h"

#include <stdint.h>

#define RC_QUEST_MAX_SKILL_REQS 16

typedef struct {
    uint8_t skill_id, level;
} RcQuestSkillReq;

typedef struct {
    char name[96];
    uint8_t difficulty, length, req_count;
    RcQuestSkillReq reqs[RC_QUEST_MAX_SKILL_REQS];
} RcQuestDef;

// Quest state per player (simple integer per quest)
#define RC_MAX_QUESTS 512

typedef struct {
    int state[RC_MAX_QUESTS];   // 0 = not started, completed_state = done
} RcQuestProgress;

extern RcQuestDef *g_rc_quest_defs;
extern int g_rc_quest_count;

int rc_load_quests(const char *path);
const RcQuestDef *rc_quest_def_get(int quest_idx);
int rc_quest_find(const char *name);
int rc_quest_get_state(const RcQuestProgress *progress, int quest_id);
void rc_quest_set_state(RcQuestProgress *progress, int quest_id, int state);

#endif
