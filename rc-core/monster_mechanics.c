#include "monster_mechanics.h"
#include "io.h"
#include "types.h"

#include <stdio.h>
#include <string.h>

#define RNME_MAGIC 0x454D4E52u
#define RNME_VERSION 1u

RcMonsterMechanicFamily
    g_rc_monster_mechanic_families[RC_MONSTER_MECH_MAX_FAMILIES];
int g_rc_monster_mechanic_family_count = 0;
static uint64_t g_rc_monster_mechanic_tags_by_npc[RC_MAX_NPC_ID];

static int read_pstr(FILE *f, char *out, int cap,
                     const char *path, const char *what) {
    uint8_t len;
    if (!rc_read_exact(f, &len, sizeof(len), 1, path, what)) return 0;
    if (len >= cap) return 0;
    if (len && !rc_read_exact(f, out, sizeof(char), len, path, what)) return 0;
    out[len] = '\0';
    return 1;
}

int rc_load_monster_mechanics(const char *path) {
    if (!path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "monster_mechanics: can't open %s\n", path);
        return -1;
    }

    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, path, "version")
            || !rc_read_exact(f, &count, sizeof(count), 1, path, "count")) {
        fclose(f);
        return -1;
    }
    if (magic != RNME_MAGIC || version != RNME_VERSION) {
        fclose(f);
        fprintf(stderr, "monster_mechanics: bad header\n");
        return -1;
    }

    memset(g_rc_monster_mechanic_families, 0,
           sizeof(g_rc_monster_mechanic_families));
    memset(g_rc_monster_mechanic_tags_by_npc, 0,
           sizeof(g_rc_monster_mechanic_tags_by_npc));
    g_rc_monster_mechanic_family_count = 0;

    int loaded = 0;
    for (uint32_t i = 0; i < count; i++) {
        RcMonsterMechanicFamily fam;
        memset(&fam, 0, sizeof(fam));
        uint16_t npc_count;
        if (!read_pstr(f, fam.key, sizeof(fam.key), path, "family key")
                || !rc_read_exact(f, &fam.status, sizeof(fam.status), 1,
                                  path, "family status")
                || !rc_read_exact(f, &npc_count, sizeof(npc_count), 1,
                                  path, "family npc count")
                || !rc_read_exact(f, &fam.system_bits, sizeof(fam.system_bits),
                                  1, path, "family system bits")
                || !rc_read_exact(f, &fam.tag_bits, sizeof(fam.tag_bits), 1,
                                  path, "family tag bits")) {
            fclose(f);
            return -1;
        }

        for (uint16_t j = 0; j < npc_count; j++) {
            uint32_t npc_id;
            if (!rc_read_exact(f, &npc_id, sizeof(npc_id), 1,
                               path, "family npc id")) {
                fclose(f);
                return -1;
            }
            if (j < RC_MONSTER_MECH_MAX_NPC_IDS) {
                fam.npc_ids[j] = npc_id;
                fam.npc_count++;
            }
        }

        if (g_rc_monster_mechanic_family_count
                < RC_MONSTER_MECH_MAX_FAMILIES) {
            for (uint16_t j = 0; j < fam.npc_count; j++) {
                uint32_t npc_id = fam.npc_ids[j];
                if (npc_id < RC_MAX_NPC_ID) {
                    g_rc_monster_mechanic_tags_by_npc[npc_id]
                        |= fam.tag_bits;
                }
            }
            g_rc_monster_mechanic_families
                [g_rc_monster_mechanic_family_count++] = fam;
            loaded++;
        }
    }

    fclose(f);
    fprintf(stderr, "monster_mechanics: loaded %d families from %s\n",
            loaded, path);
    return loaded;
}

int rc_monster_mechanics_find_family(const char *key) {
    if (!key) return -1;
    for (int i = 0; i < g_rc_monster_mechanic_family_count; i++) {
        if (strcmp(g_rc_monster_mechanic_families[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

bool rc_monster_mechanics_family_has_npc(int family_idx, uint32_t npc_id) {
    if (family_idx < 0
            || family_idx >= g_rc_monster_mechanic_family_count) {
        return false;
    }
    const RcMonsterMechanicFamily *fam =
        &g_rc_monster_mechanic_families[family_idx];
    for (uint16_t i = 0; i < fam->npc_count; i++) {
        if (fam->npc_ids[i] == npc_id) return true;
    }
    return false;
}

uint64_t rc_monster_mechanics_tags_for_npc(uint32_t npc_id) {
    if (npc_id >= RC_MAX_NPC_ID) return 0;
    return g_rc_monster_mechanic_tags_by_npc[npc_id];
}
