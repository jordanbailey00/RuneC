#include "quests.h"

int rc_quest_get_state(const RcQuestProgress *progress, int quest_id) {
    if (quest_id < 0 || quest_id >= RC_MAX_QUESTS) return 0;
    return progress->state[quest_id];
}

void rc_quest_set_state(RcQuestProgress *progress, int quest_id, int state) {
    if (quest_id < 0 || quest_id >= RC_MAX_QUESTS) return;
    progress->state[quest_id] = state;
}
