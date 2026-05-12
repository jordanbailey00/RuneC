#include "activity_mechanics.h"
#include "io.h"
#include "types.h"

#include <stdio.h>
#include <string.h>

#define AMCH_MAGIC 0x48434D41u
#define AMCH_VERSION 3u

RcActivityMechanic g_rc_activity_mechanics[RC_ACTIVITY_MECH_MAX_ROWS];
int g_rc_activity_mechanic_count = 0;
static uint64_t g_rc_activity_behavior_by_npc[RC_MAX_NPC_ID];
static uint16_t g_rc_activity_profile_by_npc[RC_MAX_NPC_ID];
static int g_rc_activity_behavior_index_built = 0;

static int read_pstr(FILE *f, char *out, int cap,
                     const char *path, const char *what) {
    uint8_t len;
    if (!rc_read_exact(f, &len, sizeof(len), 1, path, what)) return 0;
    if (len >= cap) return 0;
    if (len && !rc_read_exact(f, out, sizeof(char), len, path, what)) return 0;
    out[len] = '\0';
    return 1;
}

int rc_load_activity_mechanics(const char *path) {
    if (!path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "activity_mechanics: can't open %s\n", path);
        return -1;
    }

    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, path, "version")
            || !rc_read_exact(f, &count, sizeof(count), 1, path, "count")) {
        fclose(f);
        return -1;
    }
    if (magic != AMCH_MAGIC || version == 0 || version > AMCH_VERSION) {
        fclose(f);
        fprintf(stderr, "activity_mechanics: bad header\n");
        return -1;
    }

    memset(g_rc_activity_mechanics, 0, sizeof(g_rc_activity_mechanics));
    g_rc_activity_mechanic_count = 0;

    int loaded = 0;
    for (uint32_t i = 0; i < count; i++) {
        RcActivityMechanic row;
        memset(&row, 0, sizeof(row));
        uint16_t npc_count, section_count;
        if (!rc_read_exact(f, &row.status, sizeof(row.status), 1,
                           path, "status")
                || !rc_read_exact(f, &row.flags, sizeof(row.flags), 1,
                                  path, "flags")
                || !rc_read_exact(f, &npc_count, sizeof(npc_count), 1,
                                  path, "npc count")
                || !rc_read_exact(f, &section_count, sizeof(section_count),
                                  1, path, "section count")
                || !read_pstr(f, row.slug, sizeof(row.slug), path, "slug")
                || !read_pstr(f, row.name, sizeof(row.name), path, "name")
                || !read_pstr(f, row.source_pages, sizeof(row.source_pages),
                              path, "source pages")
                || !read_pstr(f, row.encounter_slug,
                              sizeof(row.encounter_slug),
                              path, "encounter slug")) {
            fclose(f);
            return -1;
        }
        if (version >= 2 &&
                !rc_read_exact(f, &row.behavior_bits,
                               sizeof(row.behavior_bits), 1,
                               path, "behavior bits")) {
            fclose(f);
            return -1;
        }
        if (version >= 3 &&
                !rc_read_exact(f, &row.profile_id,
                               sizeof(row.profile_id), 1,
                               path, "profile id")) {
            fclose(f);
            return -1;
        }

        for (uint16_t j = 0; j < npc_count; j++) {
            uint32_t npc_id;
            if (!rc_read_exact(f, &npc_id, sizeof(npc_id), 1,
                               path, "npc id")) {
                fclose(f);
                return -1;
            }
            if (j < RC_ACTIVITY_MECH_MAX_NPC_IDS) {
                row.npc_ids[row.npc_count++] = npc_id;
            }
        }

        for (uint16_t j = 0; j < section_count; j++) {
            RcActivityMechanicSection sec;
            memset(&sec, 0, sizeof(sec));
            if (!read_pstr(f, sec.title, sizeof(sec.title),
                           path, "section title")
                    || !rc_read_exact(f, &sec.text_hash,
                                      sizeof(sec.text_hash), 1,
                                      path, "section hash")
                    || !rc_read_exact(f, &sec.text_len,
                                      sizeof(sec.text_len), 1,
                                      path, "section length")) {
                fclose(f);
                return -1;
            }
            if (j < RC_ACTIVITY_MECH_MAX_SECTIONS) {
                row.sections[row.section_count++] = sec;
            }
        }

        if (g_rc_activity_mechanic_count < RC_ACTIVITY_MECH_MAX_ROWS) {
            g_rc_activity_mechanics[g_rc_activity_mechanic_count++] = row;
            loaded++;
        }
    }

    fclose(f);
    rc_activity_mechanics_rebuild_index();
    fprintf(stderr, "activity_mechanics: loaded %d rows from %s\n",
            loaded, path);
    return loaded;
}

void rc_activity_mechanics_rebuild_index(void) {
    memset(g_rc_activity_behavior_by_npc, 0,
           sizeof(g_rc_activity_behavior_by_npc));
    memset(g_rc_activity_profile_by_npc, 0,
           sizeof(g_rc_activity_profile_by_npc));
    for (int i = 0; i < g_rc_activity_mechanic_count; i++) {
        const RcActivityMechanic *row = &g_rc_activity_mechanics[i];
        for (uint16_t j = 0; j < row->npc_count; j++) {
            uint32_t npc_id = row->npc_ids[j];
            if (npc_id < RC_MAX_NPC_ID) {
                g_rc_activity_behavior_by_npc[npc_id] |= row->behavior_bits;
                if (row->profile_id != RC_ACTIVITY_PROFILE_NONE) {
                    g_rc_activity_profile_by_npc[npc_id] = row->profile_id;
                }
            }
        }
    }
    g_rc_activity_behavior_index_built = 1;
}

int rc_activity_mechanics_find_slug(const char *slug) {
    if (!slug) return -1;
    for (int i = 0; i < g_rc_activity_mechanic_count; i++) {
        if (strcmp(g_rc_activity_mechanics[i].slug, slug) == 0) return i;
    }
    return -1;
}

uint64_t rc_activity_mechanics_behavior_for_npc(uint32_t npc_id) {
    if (!g_rc_activity_behavior_index_built || npc_id >= RC_MAX_NPC_ID) {
        return 0;
    }
    return g_rc_activity_behavior_by_npc[npc_id];
}

uint16_t rc_activity_mechanics_profile_for_npc(uint32_t npc_id) {
    if (!g_rc_activity_behavior_index_built || npc_id >= RC_MAX_NPC_ID) {
        return RC_ACTIVITY_PROFILE_NONE;
    }
    return g_rc_activity_profile_by_npc[npc_id];
}

uint64_t rc_activity_mechanics_behavior_for_owner(const char *owner) {
    if (!owner) return 0;
    uint64_t bits = 0;
    for (int i = 0; i < g_rc_activity_mechanic_count; i++) {
        const RcActivityMechanic *row = &g_rc_activity_mechanics[i];
        if (row->behavior_bits == 0) continue;
        if (strcmp(row->encounter_slug, owner) == 0) bits |= row->behavior_bits;
    }
    return bits;
}

bool rc_activity_mechanics_has_npc(int idx, uint32_t npc_id) {
    if (idx < 0 || idx >= g_rc_activity_mechanic_count) return false;
    const RcActivityMechanic *row = &g_rc_activity_mechanics[idx];
    for (uint16_t i = 0; i < row->npc_count; i++) {
        if (row->npc_ids[i] == npc_id) return true;
    }
    return false;
}
