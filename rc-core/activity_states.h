#ifndef RC_ACTIVITY_STATES_H
#define RC_ACTIVITY_STATES_H

#include "types.h"

#include <stdbool.h>
#include <stdint.h>

#define RC_ACTIVITY_STATE_MAX_ROWS 32
#define RC_ACTIVITY_STATE_MAX_NPC_IDS 64
#define RC_ACTIVITY_STATE_MAX_STATES 32
#define RC_ACTIVITY_STATE_MAX_TRANSITIONS 64
#define RC_ACTIVITY_STATE_MAX_PARAMS 24

enum {
    RC_ACTIVITY_STATE_KIND_WAVE = 1,
    RC_ACTIVITY_STATE_KIND_MULTI_BOSS = 2,
    RC_ACTIVITY_STATE_KIND_DUAL_BOSS = 3,
    RC_ACTIVITY_STATE_KIND_SKILLING_BOSS = 4,
    RC_ACTIVITY_STATE_KIND_CRYPT = 5,
    RC_ACTIVITY_STATE_KIND_SINGLE_BOSS = 6,
};

enum {
    RC_ACTIVITY_STATE_STATUS_TYPED = 1,
};

enum {
    RC_ACTIVITY_STATE_ROLE_ENTRY = 1,
    RC_ACTIVITY_STATE_ROLE_COMBAT = 2,
    RC_ACTIVITY_STATE_ROLE_PHASE = 3,
    RC_ACTIVITY_STATE_ROLE_BOSS = 4,
    RC_ACTIVITY_STATE_ROLE_WAVE = 5,
    RC_ACTIVITY_STATE_ROLE_RESOURCE = 6,
    RC_ACTIVITY_STATE_ROLE_HAZARD = 7,
    RC_ACTIVITY_STATE_ROLE_REWARD = 8,
    RC_ACTIVITY_STATE_ROLE_FAIL = 9,
};

enum {
    RC_ACTIVITY_STATE_EVT_TUNNEL_BROTHER_SELECTED = 1,
    RC_ACTIVITY_STATE_EVT_REQUIRED_BOSSES_DEAD = 2,
    RC_ACTIVITY_STATE_EVT_WAVE_REACHED = 3,
    RC_ACTIVITY_STATE_EVT_BOSS_DEAD = 4,
    RC_ACTIVITY_STATE_EVT_HP_THRESHOLD = 5,
    RC_ACTIVITY_STATE_EVT_RESOURCE_ZERO = 6,
    RC_ACTIVITY_STATE_EVT_TIMER = 7,
};

typedef struct {
    char id[32];
    uint8_t role;
    uint16_t value;
    uint64_t flags;
    uint64_t flags_hi;
} RcActivityStateNode;

typedef struct {
    char from[32];
    char to[32];
    uint8_t event;
    uint16_t value;
} RcActivityStateTransition;

typedef struct {
    char key[48];
    int32_t value;
} RcActivityStateParam;

typedef struct {
    char slug[64];
    char name[64];
    char source_rows[192];
    char source_pages[192];
    uint8_t kind;
    uint8_t status;
    uint16_t npc_count;
    uint16_t state_count;
    uint16_t transition_count;
    uint16_t param_count;
    uint64_t flags;
    uint64_t flags_hi;
    uint32_t npc_ids[RC_ACTIVITY_STATE_MAX_NPC_IDS];
    RcActivityStateNode states[RC_ACTIVITY_STATE_MAX_STATES];
    RcActivityStateTransition transitions[RC_ACTIVITY_STATE_MAX_TRANSITIONS];
    RcActivityStateParam params[RC_ACTIVITY_STATE_MAX_PARAMS];
} RcActivityStateMachine;

typedef struct {
    int machine_idx;
    int state_idx;
    uint16_t wave;
    uint16_t progress;
    uint16_t cycles;
    int32_t resource;
    uint32_t dead_npc_ids[16];
    uint8_t dead_npc_count;
    uint8_t complete;
} RcActivityRun;

typedef struct {
    RcActivityStateMachine rows[RC_ACTIVITY_STATE_MAX_ROWS];
    int count;
    int by_npc[RC_MAX_NPC_ID];
    int index_built;
} RcActivityStateData;

extern RcActivityStateMachine
    g_rc_activity_states[RC_ACTIVITY_STATE_MAX_ROWS];
extern int g_rc_activity_state_count;

int rc_load_activity_states(const char *path);
void rc_activity_state_data_init(RcActivityStateData *data);
void rc_activity_state_data_free(RcActivityStateData *data);
int rc_activity_state_data_import_globals(RcActivityStateData *data);
int rc_load_activity_states_into(const char *path,
                                 RcActivityStateData *data);
int rc_activity_states_mirror_to_globals(const RcActivityStateData *data);
void rc_activity_states_use_data(const RcActivityStateData *data);
void rc_activity_states_reset_data_if_active(const RcActivityStateData *data);
void rc_activity_states_rebuild_index(void);
int rc_activity_state_find_slug(const char *slug);
int rc_activity_state_find_for_npc(uint32_t npc_id);
bool rc_activity_state_has_npc(int idx, uint32_t npc_id);
int rc_activity_state_find_node(const RcActivityStateMachine *row,
                                const char *id);
int rc_activity_state_step(const RcActivityStateMachine *row, int state_idx,
                           uint8_t event, uint16_t value);
int rc_activity_state_param(const RcActivityStateMachine *row,
                            const char *key, int fallback);
int rc_activity_run_start(RcActivityRun *run, const char *slug);
int rc_activity_run_start_idx(RcActivityRun *run, int machine_idx);
int rc_activity_run_event(RcActivityRun *run, uint8_t event, uint16_t value);
int rc_activity_run_object_event(RcActivityRun *run, const char *key,
                                 int amount);

#endif
