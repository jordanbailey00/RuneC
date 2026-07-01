#include "activity_states.h"
#include "activity_spawns.h"
#include "io.h"
#include "types.h"

#include <stdio.h>
#include <string.h>

#define ASTA_MAGIC 0x41545341u
#define ASTA_VERSION 1u

RcActivityStateMachine g_rc_activity_states[RC_ACTIVITY_STATE_MAX_ROWS];
int g_rc_activity_state_count = 0;
static int g_rc_activity_state_by_npc[RC_MAX_NPC_ID];
static int g_rc_activity_state_index_built = 0;
static const RcActivityStateMachine *g_active_activity_states =
    g_rc_activity_states;
static int g_active_activity_state_count = 0;
static const int *g_active_activity_state_by_npc =
    g_rc_activity_state_by_npc;
static int g_active_activity_state_index_built = 0;

static void rebuild_state_index_into(RcActivityStateData *data);

void rc_activity_state_data_init(RcActivityStateData *data) {
    if (!data) return;
    memset(data->rows, 0, sizeof(data->rows));
    data->count = 0;
    for (int i = 0; i < RC_MAX_NPC_ID; i++) data->by_npc[i] = -1;
    data->index_built = 0;
}

void rc_activity_state_data_free(RcActivityStateData *data) {
    rc_activity_state_data_init(data);
}

int rc_activity_state_data_import_globals(RcActivityStateData *data) {
    if (!data) return 0;
    memcpy(data->rows, g_rc_activity_states, sizeof(data->rows));
    data->count = g_rc_activity_state_count;
    if (g_rc_activity_state_index_built) {
        memcpy(data->by_npc, g_rc_activity_state_by_npc,
               sizeof(data->by_npc));
        data->index_built = g_rc_activity_state_index_built;
    } else {
        rebuild_state_index_into(data);
    }
    return 1;
}

void rc_activity_states_use_data(const RcActivityStateData *data) {
    if (!data) {
        g_active_activity_states = g_rc_activity_states;
        g_active_activity_state_count = g_rc_activity_state_count;
        g_active_activity_state_by_npc = g_rc_activity_state_by_npc;
        g_active_activity_state_index_built =
            g_rc_activity_state_index_built;
        return;
    }
    g_active_activity_states = data->rows;
    g_active_activity_state_count = data->count;
    g_active_activity_state_by_npc = data->by_npc;
    g_active_activity_state_index_built = data->index_built;
}

void rc_activity_states_reset_data_if_active(
    const RcActivityStateData *data) {
    if (data && g_active_activity_states == data->rows) {
        rc_activity_states_use_data(NULL);
    }
}

static int read_pstr(FILE *f, char *out, int cap,
                     const char *path, const char *what) {
    uint8_t len;
    if (!rc_read_exact(f, &len, sizeof(len), 1, path, what)) return 0;
    if (len >= cap) return 0;
    if (len && !rc_read_exact(f, out, sizeof(char), len, path, what)) return 0;
    out[len] = '\0';
    return 1;
}

static void rebuild_state_index_into(RcActivityStateData *data) {
    if (!data) return;
    for (int i = 0; i < RC_MAX_NPC_ID; i++) data->by_npc[i] = -1;
    for (int i = 0; i < data->count; i++) {
        const RcActivityStateMachine *row = &data->rows[i];
        for (uint16_t j = 0; j < row->npc_count; j++) {
            uint32_t npc_id = row->npc_ids[j];
            if (npc_id < RC_MAX_NPC_ID) data->by_npc[npc_id] = i;
        }
    }
    data->index_built = 1;
}

int rc_load_activity_states_into(const char *path,
                                 RcActivityStateData *data) {
    if (!path || !data) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "activity_states: can't open %s\n", path);
        return -1;
    }

    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, path, "version")
            || !rc_read_exact(f, &count, sizeof(count), 1, path, "count")) {
        rc_asset_close(f);
        return -1;
    }
    if (magic != ASTA_MAGIC || version == 0 || version > ASTA_VERSION) {
        rc_asset_close(f);
        fprintf(stderr, "activity_states: bad header\n");
        return -1;
    }

    memset(data->rows, 0, sizeof(data->rows));
    data->count = 0;

    int loaded = 0;
    for (uint32_t i = 0; i < count; i++) {
        RcActivityStateMachine row;
        memset(&row, 0, sizeof(row));
        uint16_t npc_count, state_count, transition_count, param_count;
        if (!rc_read_exact(f, &row.kind, sizeof(row.kind), 1, path, "kind")
                || !rc_read_exact(f, &row.status, sizeof(row.status), 1,
                                  path, "status")
                || !rc_read_exact(f, &npc_count, sizeof(npc_count), 1,
                                  path, "npc count")
                || !rc_read_exact(f, &state_count, sizeof(state_count), 1,
                                  path, "state count")
                || !rc_read_exact(f, &transition_count,
                                  sizeof(transition_count), 1,
                                  path, "transition count")
                || !rc_read_exact(f, &param_count, sizeof(param_count), 1,
                                  path, "param count")
                || !rc_read_exact(f, &row.flags, sizeof(row.flags), 1,
                                  path, "flags")
                || !rc_read_exact(f, &row.flags_hi, sizeof(row.flags_hi), 1,
                                  path, "flags hi")
                || !read_pstr(f, row.slug, sizeof(row.slug), path, "slug")
                || !read_pstr(f, row.name, sizeof(row.name), path, "name")
                || !read_pstr(f, row.source_rows, sizeof(row.source_rows),
                              path, "source rows")
                || !read_pstr(f, row.source_pages, sizeof(row.source_pages),
                              path, "source pages")) {
            rc_asset_close(f);
            return -1;
        }
        for (uint16_t j = 0; j < npc_count; j++) {
            uint32_t npc_id;
            if (!rc_read_exact(f, &npc_id, sizeof(npc_id), 1,
                               path, "npc id")) {
                rc_asset_close(f);
                return -1;
            }
            if (j < RC_ACTIVITY_STATE_MAX_NPC_IDS) {
                row.npc_ids[row.npc_count++] = npc_id;
            }
        }
        for (uint16_t j = 0; j < state_count; j++) {
            RcActivityStateNode state;
            memset(&state, 0, sizeof(state));
            if (!rc_read_exact(f, &state.role, sizeof(state.role), 1,
                               path, "state role")
                    || !rc_read_exact(f, &state.flags, sizeof(state.flags), 1,
                                      path, "state flags")
                    || !rc_read_exact(f, &state.flags_hi,
                                      sizeof(state.flags_hi), 1,
                                      path, "state flags hi")
                    || !rc_read_exact(f, &state.value, sizeof(state.value), 1,
                                      path, "state value")
                    || !read_pstr(f, state.id, sizeof(state.id),
                                  path, "state id")) {
                rc_asset_close(f);
                return -1;
            }
            if (j < RC_ACTIVITY_STATE_MAX_STATES) {
                row.states[row.state_count++] = state;
            }
        }
        for (uint16_t j = 0; j < transition_count; j++) {
            RcActivityStateTransition tr;
            memset(&tr, 0, sizeof(tr));
            if (!rc_read_exact(f, &tr.event, sizeof(tr.event), 1,
                               path, "transition event")
                    || !rc_read_exact(f, &tr.value, sizeof(tr.value), 1,
                                      path, "transition value")
                    || !read_pstr(f, tr.from, sizeof(tr.from), path,
                                  "transition from")
                    || !read_pstr(f, tr.to, sizeof(tr.to), path,
                                  "transition to")) {
                rc_asset_close(f);
                return -1;
            }
            if (j < RC_ACTIVITY_STATE_MAX_TRANSITIONS) {
                row.transitions[row.transition_count++] = tr;
            }
        }
        for (uint16_t j = 0; j < param_count; j++) {
            RcActivityStateParam param;
            memset(&param, 0, sizeof(param));
            if (!read_pstr(f, param.key, sizeof(param.key), path,
                           "param key")
                    || !rc_read_exact(f, &param.value, sizeof(param.value), 1,
                                      path, "param value")) {
                rc_asset_close(f);
                return -1;
            }
            if (j < RC_ACTIVITY_STATE_MAX_PARAMS) {
                row.params[row.param_count++] = param;
            }
        }
        if (data->count < RC_ACTIVITY_STATE_MAX_ROWS) {
            data->rows[data->count++] = row;
            loaded++;
        }
    }

    rc_asset_close(f);
    rebuild_state_index_into(data);
    fprintf(stderr, "activity_states: loaded %d rows from %s\n",
            loaded, path);
    return loaded;
}

void rc_activity_states_rebuild_index(void) {
    for (int i = 0; i < RC_MAX_NPC_ID; i++) g_rc_activity_state_by_npc[i] = -1;
    for (int i = 0; i < g_rc_activity_state_count; i++) {
        const RcActivityStateMachine *row = &g_rc_activity_states[i];
        for (uint16_t j = 0; j < row->npc_count; j++) {
            uint32_t npc_id = row->npc_ids[j];
            if (npc_id < RC_MAX_NPC_ID) g_rc_activity_state_by_npc[npc_id] = i;
        }
    }
    g_rc_activity_state_index_built = 1;
    rc_activity_states_use_data(NULL);
}

int rc_activity_states_mirror_to_globals(const RcActivityStateData *data) {
    if (!data) return 0;
    memcpy(g_rc_activity_states, data->rows, sizeof(g_rc_activity_states));
    g_rc_activity_state_count = data->count;
    memcpy(g_rc_activity_state_by_npc, data->by_npc,
           sizeof(g_rc_activity_state_by_npc));
    g_rc_activity_state_index_built = data->index_built;
    return 1;
}

int rc_load_activity_states(const char *path) {
    RcActivityStateData data;
    rc_activity_state_data_init(&data);
    int loaded = rc_load_activity_states_into(path, &data);
    if (loaded >= 0 && !rc_activity_states_mirror_to_globals(&data))
        loaded = -1;
    rc_activity_state_data_free(&data);
    if (loaded >= 0) rc_activity_states_use_data(NULL);
    return loaded;
}

int rc_activity_state_find_slug(const char *slug) {
    if (!slug) return -1;
    const RcActivityStateMachine *rows = g_active_activity_states
                                       ? g_active_activity_states
                                       : g_rc_activity_states;
    int count = g_active_activity_states ? g_active_activity_state_count
                                         : g_rc_activity_state_count;
    for (int i = 0; rows && i < count; i++) {
        if (strcmp(rows[i].slug, slug) == 0) return i;
    }
    return -1;
}

int rc_activity_state_find_for_npc(uint32_t npc_id) {
    int built = g_active_activity_states ? g_active_activity_state_index_built
                                         : g_rc_activity_state_index_built;
    const int *by_npc = g_active_activity_state_by_npc
                      ? g_active_activity_state_by_npc
                      : g_rc_activity_state_by_npc;
    if (!built || npc_id >= RC_MAX_NPC_ID) {
        return -1;
    }
    return by_npc[npc_id];
}

bool rc_activity_state_has_npc(int idx, uint32_t npc_id) {
    const RcActivityStateMachine *rows = g_active_activity_states
                                       ? g_active_activity_states
                                       : g_rc_activity_states;
    int count = g_active_activity_states ? g_active_activity_state_count
                                         : g_rc_activity_state_count;
    if (idx < 0 || idx >= count || !rows) return false;
    const RcActivityStateMachine *row = &rows[idx];
    for (uint16_t i = 0; i < row->npc_count; i++) {
        if (row->npc_ids[i] == npc_id) return true;
    }
    return false;
}

int rc_activity_state_find_node(const RcActivityStateMachine *row,
                                const char *id) {
    if (!row || !id) return -1;
    for (uint16_t i = 0; i < row->state_count; i++) {
        if (strcmp(row->states[i].id, id) == 0) return i;
    }
    return -1;
}

int rc_activity_state_step(const RcActivityStateMachine *row, int state_idx,
                           uint8_t event, uint16_t value) {
    if (!row || state_idx < 0 || state_idx >= row->state_count) return -1;
    const char *from = row->states[state_idx].id;
    for (uint16_t i = 0; i < row->transition_count; i++) {
        const RcActivityStateTransition *tr = &row->transitions[i];
        if (tr->event != event) continue;
        if (tr->value != 0 && tr->value != value) continue;
        if (strcmp(tr->from, from) != 0) continue;
        int next = rc_activity_state_find_node(row, tr->to);
        return next >= 0 ? next : state_idx;
    }
    return state_idx;
}

int rc_activity_state_param(const RcActivityStateMachine *row,
                            const char *key, int fallback) {
    if (!row || !key) return fallback;
    for (uint16_t i = 0; i < row->param_count; i++) {
        if (strcmp(row->params[i].key, key) == 0) {
            return row->params[i].value;
        }
    }
    return fallback;
}

static int step_hp_threshold(const RcActivityStateMachine *row, int state_idx,
                             uint16_t current_value) {
    if (!row || state_idx < 0 || state_idx >= row->state_count) return -1;
    const char *from = row->states[state_idx].id;
    for (uint16_t i = 0; i < row->transition_count; i++) {
        const RcActivityStateTransition *tr = &row->transitions[i];
        if (tr->event != RC_ACTIVITY_STATE_EVT_HP_THRESHOLD) continue;
        if (tr->value != 0 && current_value > tr->value) continue;
        if (strcmp(tr->from, from) != 0) continue;
        int next = rc_activity_state_find_node(row, tr->to);
        return next >= 0 ? next : state_idx;
    }
    return state_idx;
}

static void mark_complete_if_reward(RcActivityRun *run,
                                    const RcActivityStateMachine *row) {
    if (!run || !row || run->state_idx < 0 ||
            run->state_idx >= row->state_count) return;
    if (row->states[run->state_idx].role == RC_ACTIVITY_STATE_ROLE_REWARD) {
        run->complete = 1;
    }
}

static void record_boss_dead(RcActivityRun *run, uint16_t npc_id) {
    if (!run) return;
    if (npc_id == 0) {
        run->progress++;
        return;
    }
    for (uint8_t i = 0; i < run->dead_npc_count; i++) {
        if (run->dead_npc_ids[i] == npc_id) return;
    }
    if (run->dead_npc_count < sizeof(run->dead_npc_ids) /
            sizeof(run->dead_npc_ids[0])) {
        run->dead_npc_ids[run->dead_npc_count++] = npc_id;
        run->progress = run->dead_npc_count;
    }
}

static bool object_key_allowed(const RcActivityStateMachine *row,
                               const char *key) {
    if (!row || !key || !key[0]) return false;
    int count = 0;
    const RcActivitySpawn *spawns =
        rc_activity_spawns_for(row->slug, &count);
    if (!spawns || count <= 0) return true;
    return rc_activity_spawn_find_key(row->slug,
        RC_ACTIVITY_SPAWN_OBJECT_ANCHOR, key) != NULL;
}

int rc_activity_run_start_idx(RcActivityRun *run, int machine_idx) {
    const RcActivityStateMachine *rows = g_active_activity_states
                                       ? g_active_activity_states
                                       : g_rc_activity_states;
    int count = g_active_activity_states ? g_active_activity_state_count
                                         : g_rc_activity_state_count;
    if (!run || machine_idx < 0 || machine_idx >= count || !rows) {
        return -1;
    }
    memset(run, 0, sizeof(*run));
    run->machine_idx = machine_idx;
    run->state_idx = rows[machine_idx].state_count > 0 ? 0 : -1;
    return run->state_idx;
}

int rc_activity_run_start(RcActivityRun *run, const char *slug) {
    return rc_activity_run_start_idx(run, rc_activity_state_find_slug(slug));
}

int rc_activity_run_event(RcActivityRun *run, uint8_t event, uint16_t value) {
    RcActivityStateMachine *rows = (RcActivityStateMachine *)
        (g_active_activity_states ? g_active_activity_states
                                  : g_rc_activity_states);
    int count = g_active_activity_states ? g_active_activity_state_count
                                         : g_rc_activity_state_count;
    if (!run || run->machine_idx < 0 ||
            run->machine_idx >= count || run->state_idx < 0 || !rows) {
        return -1;
    }
    RcActivityStateMachine *row = &rows[run->machine_idx];
    if (event == RC_ACTIVITY_STATE_EVT_WAVE_REACHED && value > run->wave) {
        run->wave = value;
    } else if (event == RC_ACTIVITY_STATE_EVT_BOSS_DEAD) {
        record_boss_dead(run, value);
    } else if (event == RC_ACTIVITY_STATE_EVT_REQUIRED_BOSSES_DEAD) {
        run->progress = value;
    } else if (event == RC_ACTIVITY_STATE_EVT_TIMER) {
        run->cycles++;
    }

    int next = event == RC_ACTIVITY_STATE_EVT_HP_THRESHOLD
        ? step_hp_threshold(row, run->state_idx, value)
        : rc_activity_state_step(row, run->state_idx, event, value);
    if (next >= 0) run->state_idx = next;

    if (row->kind == RC_ACTIVITY_STATE_KIND_WAVE &&
            event == RC_ACTIVITY_STATE_EVT_BOSS_DEAD) {
        int boss = rc_activity_state_param(row, "zuk_npc_id", 0);
        if (boss > 0 && value == (uint16_t)boss) run->complete = 1;
    } else if (row->kind == RC_ACTIVITY_STATE_KIND_SINGLE_BOSS &&
            event == RC_ACTIVITY_STATE_EVT_BOSS_DEAD) {
        run->complete = 1;
    } else if (row->kind == RC_ACTIVITY_STATE_KIND_DUAL_BOSS &&
            event == RC_ACTIVITY_STATE_EVT_BOSS_DEAD &&
            run->progress >= 2) {
        run->complete = 1;
    } else if (row->kind == RC_ACTIVITY_STATE_KIND_MULTI_BOSS &&
            event == RC_ACTIVITY_STATE_EVT_BOSS_DEAD) {
        int needed = rc_activity_state_param(row, "boss_count",
                                             row->npc_count);
        if (needed > 0 && run->progress >= (uint16_t)needed) {
            run->complete = 1;
        }
    }
    mark_complete_if_reward(run, row);
    return run->state_idx;
}

int rc_activity_run_object_event(RcActivityRun *run, const char *key,
                                 int amount) {
    RcActivityStateMachine *rows = (RcActivityStateMachine *)
        (g_active_activity_states ? g_active_activity_states
                                  : g_rc_activity_states);
    int count = g_active_activity_states ? g_active_activity_state_count
                                         : g_rc_activity_state_count;
    if (!run || !key || run->machine_idx < 0 ||
            run->machine_idx >= count || !rows) {
        return -1;
    }
    RcActivityStateMachine *row = &rows[run->machine_idx];
    if (row->kind != RC_ACTIVITY_STATE_KIND_SKILLING_BOSS) {
        return run->state_idx;
    }
    if (!object_key_allowed(row, key)) return -1;
    if (amount <= 0) {
        int next = rc_activity_state_step(row, run->state_idx,
            RC_ACTIVITY_STATE_EVT_TIMER, 1);
        if (next >= 0 && next != run->state_idx) {
            return rc_activity_run_event(run, RC_ACTIVITY_STATE_EVT_TIMER, 1);
        }
        return run->state_idx;
    }
    if (run->resource <= 0) {
        run->resource = rc_activity_state_param(row,
            "object_resource_initial", 100);
    }
    int delta = amount > 0 ? amount : rc_activity_state_param(row,
        "object_resource_delta", 10);
    run->resource -= delta;
    if (run->resource <= 0) {
        run->resource = 0;
        return rc_activity_run_event(run, RC_ACTIVITY_STATE_EVT_RESOURCE_ZERO,
                                     0);
    }
    return run->state_idx;
}
