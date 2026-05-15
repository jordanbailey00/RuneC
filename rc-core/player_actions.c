#include "player_actions.h"
#include "io.h"

#include <stdio.h>
#include <string.h>

#define PACT_MAGIC 0x54434150u
#define PACT_VERSION 1u

RcPlayerActionDef g_rc_player_actions[RC_MAX_PLAYER_ACTIONS];
int g_rc_player_action_count = 0;

int rc_load_player_actions(const char *path) {
    if (!path) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) return -1;

    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, path, "version")
            || !rc_read_exact(f, &count, sizeof(count), 1, path, "count")
            || magic != PACT_MAGIC || version != PACT_VERSION) {
        rc_asset_close(f);
        return -1;
    }

    memset(g_rc_player_actions, 0, sizeof(g_rc_player_actions));
    int loaded = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t action_id, kind;
        uint16_t name_len;
        uint32_t subsystems;
        if (!rc_read_exact(f, &action_id, sizeof(action_id), 1, path, "action id")
                || !rc_read_exact(f, &kind, sizeof(kind), 1, path, "kind")
                || !rc_read_exact(f, &name_len, sizeof(name_len), 1, path, "name len")
                || !rc_read_exact(f, &subsystems, sizeof(subsystems), 1,
                                  path, "subsystems")) {
            rc_asset_close(f);
            return -1;
        }
        char name[32] = {0};
        uint16_t keep = name_len < sizeof(name) ? name_len : (uint16_t)sizeof(name) - 1;
        if (keep && !rc_read_exact(f, name, 1, keep, path, "name")) {
            rc_asset_close(f);
            return -1;
        }
        if (name_len > keep && !rc_seek(f, name_len - keep, SEEK_CUR,
                                        path, "name")) {
            rc_asset_close(f);
            return -1;
        }
        if (action_id < RC_MAX_PLAYER_ACTIONS) {
            RcPlayerActionDef *row = &g_rc_player_actions[action_id];
            strncpy(row->name, name, sizeof(row->name) - 1);
            row->kind = kind;
            row->required_subsystems = subsystems;
            row->loaded = 1;
            loaded++;
        }
    }
    rc_asset_close(f);
    g_rc_player_action_count = loaded;
    return loaded;
}

const RcPlayerActionDef *rc_player_action_def_get(int action_id) {
    if (action_id < 0 || action_id >= RC_MAX_PLAYER_ACTIONS) return NULL;
    return g_rc_player_actions[action_id].loaded
         ? &g_rc_player_actions[action_id] : NULL;
}

int rc_player_action_allowed(uint32_t enabled, int action_id) {
    const RcPlayerActionDef *def = rc_player_action_def_get(action_id);
    if (!def) return 0;
    return (enabled & def->required_subsystems) == def->required_subsystems;
}
