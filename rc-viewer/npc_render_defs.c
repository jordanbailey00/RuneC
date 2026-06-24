#include "npc_render_defs.h"

#include "../rc-core/assets.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    NDEF_MAGIC = 0x4E444546,
    NDEF_V1 = 1,
    NDEF_V2 = 2,
    NDEF_V3 = 3,
    NDEF_V4 = 4,
};

static int read_exact(FILE *f, void *out, size_t size, size_t count) {
    return fread(out, size, count, f) == count;
}

void runec_npc_render_defs_init(RuneCNpcRenderDefs *defs) {
    if (!defs) return;
    memset(defs, 0, sizeof(*defs));
    for (int i = 0; i < RC_MAX_NPC_ID; i++)
        defs->by_id[i] = -1;
}

int runec_npc_render_defs_load(RuneCNpcRenderDefs *defs, const char *path) {
    if (!defs || !path) return 0;
    runec_npc_render_defs_init(defs);

    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "npc_render_defs: can't open %s\n", path);
        return 0;
    }

    uint32_t magic, version, count;
    if (!read_exact(f, &magic, sizeof(magic), 1)
            || !read_exact(f, &version, sizeof(version), 1)
            || !read_exact(f, &count, sizeof(count), 1)) {
        rc_asset_close(f);
        return 0;
    }
    if (magic != NDEF_MAGIC || (version != NDEF_V1 && version != NDEF_V2
            && version != NDEF_V3 && version != NDEF_V4)) {
        fprintf(stderr, "npc_render_defs: bad header in %s\n", path);
        rc_asset_close(f);
        return 0;
    }

    int loaded = 0;
    for (uint32_t i = 0; i < count && loaded < RC_MAX_NPC_DEFS; i++) {
        RuneCNpcRenderDef *d = &defs->defs[loaded];
        memset(d, 0, sizeof(*d));

        uint32_t id;
        uint8_t size, name_len;
        int16_t combat_level;
        uint16_t hitpoints;
        uint16_t stats[6];
        int32_t anims[5];

        if (!read_exact(f, &id, sizeof(id), 1)
                || !read_exact(f, &size, sizeof(size), 1)
                || !read_exact(f, &combat_level, sizeof(combat_level), 1)
                || !read_exact(f, &hitpoints, sizeof(hitpoints), 1)
                || !read_exact(f, stats, sizeof(stats[0]), 6)
                || !read_exact(f, anims, sizeof(anims[0]), 5)
                || !read_exact(f, &name_len, sizeof(name_len), 1)) {
            rc_asset_close(f);
            return 0;
        }
        if (fseek(f, (long)name_len, SEEK_CUR) != 0) {
            rc_asset_close(f);
            return 0;
        }

        d->id = (int)id;
        d->stand_anim = anims[0];
        d->walk_anim = anims[1];
        d->run_anim = anims[2];
        d->attack_anim = anims[3];
        d->death_anim = anims[4];

        if (version >= NDEF_V2) {
            uint8_t aggr, atk_spd, aggro_r, atk_types, weak, immu;
            uint16_t max_hit, slayer_lvl;
            if (!read_exact(f, &aggr, sizeof(aggr), 1)
                    || !read_exact(f, &max_hit, sizeof(max_hit), 1)
                    || !read_exact(f, &atk_spd, sizeof(atk_spd), 1)
                    || !read_exact(f, &aggro_r, sizeof(aggro_r), 1)
                    || !read_exact(f, &slayer_lvl, sizeof(slayer_lvl), 1)
                    || !read_exact(f, &atk_types, sizeof(atk_types), 1)
                    || !read_exact(f, &weak, sizeof(weak), 1)
                    || !read_exact(f, &immu, sizeof(immu), 1)) {
                rc_asset_close(f);
                return 0;
            }
        }

        if (version >= NDEF_V3) {
            uint8_t model_count;
            if (!read_exact(f, &model_count, sizeof(model_count), 1)) {
                rc_asset_close(f);
                return 0;
            }
            for (uint8_t j = 0; j < model_count; j++) {
                uint32_t model_id;
                if (!read_exact(f, &model_id, sizeof(model_id), 1)) {
                    rc_asset_close(f);
                    return 0;
                }
                if (j < RUNEC_NPC_RENDER_MAX_MODELS) {
                    d->model_ids[j] = (int)model_id;
                    d->model_count++;
                }
            }
        }

        if (version >= NDEF_V4) {
            for (int j = 0; j < 5; j++) {
                uint8_t option_len;
                if (!read_exact(f, &option_len, sizeof(option_len), 1)
                        || fseek(f, (long)option_len, SEEK_CUR) != 0) {
                    rc_asset_close(f);
                    return 0;
                }
            }
        }

        d->loaded = 1;
        if (id < RC_MAX_NPC_ID)
            defs->by_id[id] = loaded;
        loaded++;
    }

    rc_asset_close(f);
    defs->count = loaded;
    defs->loaded = 1;
    fprintf(stderr, "npc_render_defs: loaded %d defs from %s\n", loaded, path);
    return loaded;
}

const RuneCNpcRenderDef *runec_npc_render_find(
    const RuneCNpcRenderDefs *defs, int npc_id) {
    if (!defs || !defs->loaded) return NULL;
    if (npc_id >= 0 && npc_id < RC_MAX_NPC_ID) {
        int idx = defs->by_id[npc_id];
        if (idx >= 0 && idx < defs->count)
            return &defs->defs[idx];
    }
    for (int i = 0; i < defs->count; i++) {
        if (defs->defs[i].id == npc_id)
            return &defs->defs[i];
    }
    return NULL;
}
