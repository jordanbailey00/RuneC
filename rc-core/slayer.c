#include "slayer.h"
#include "api.h"
#include "events.h"
#include "io.h"
#include "npc.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define SLAY_MAGIC 0x59414C53u
#define SLAY_VERSION 3u

RcSlayerMasterDef g_rc_slayer_masters[RC_SLAYER_MAX_MASTERS];
int g_rc_slayer_master_count = 0;

static int read_pstr(FILE *f, char *out, int cap,
                     const char *path, const char *what) {
    uint8_t len;
    if (!rc_read_exact(f, &len, sizeof(len), 1, path, what)) return 0;
    if (len >= cap) return 0;
    if (len && !rc_read_exact(f, out, sizeof(char), len, path, what)) return 0;
    out[len] = '\0';
    return 1;
}

static int read_str16(FILE *f, char *out, int cap,
                      const char *path, const char *what) {
    uint16_t len;
    if (!rc_read_exact(f, &len, sizeof(len), 1, path, what)) return 0;
    if (len >= cap) return 0;
    if (len && !rc_read_exact(f, out, sizeof(char), len, path, what)) return 0;
    out[len] = '\0';
    return 1;
}

static void parse_task_locations(RcSlayerTaskDef *task);
static void parse_task_boss_candidates(RcSlayerTaskDef *task);

static void task_key(const char *in, char *out, int cap) {
    const char *s = strrchr(in ? in : "", '/');
    s = s ? s + 1 : (in ? in : "");
    char tmp[80];
    int n = 0;
    while (*s && n < (int)sizeof(tmp) - 1) {
        tmp[n++] = (char)tolower((unsigned char)*s++);
    }
    tmp[n] = '\0';
    const char suffix[] = " slayer task";
    int slen = (int)strlen(suffix);
    if (n > slen && strcmp(tmp + n - slen, suffix) == 0) {
        tmp[n - slen] = '\0';
    }
    int w = 0;
    for (int i = 0; tmp[i] && w < cap - 1; i++) {
        if (isalnum((unsigned char)tmp[i])) out[w++] = tmp[i];
    }
    if (w > 3 && out[w - 1] == 's') w--;
    out[w] = '\0';
}

static uint32_t task_key_hash(const char *name) {
    char key[80];
    task_key(name, key, sizeof(key));
    uint32_t h = 2166136261u;
    for (int i = 0; key[i]; i++) {
        h ^= (uint8_t)key[i];
        h *= 16777619u;
    }
    return h ? h : 1u;
}

static bool key_matches(const char *wanted_name, const char *npc_name) {
    char wanted[80], npc[80];
    task_key(wanted_name, wanted, sizeof(wanted));
    task_key(npc_name, npc, sizeof(npc));
    return wanted[0] &&
           (strcmp(wanted, npc) == 0 ||
            strstr(npc, wanted) != NULL ||
            strstr(wanted, npc) != NULL);
}

static bool npc_matches_name(int def_idx, const char *name) {
    if (def_idx < 0 || def_idx >= g_npc_def_count) return false;
    return key_matches(name, g_npc_defs[def_idx].name);
}

static bool npc_matches_group_alias(int def_idx, const char *group) {
    if (def_idx < 0 || def_idx >= g_npc_def_count || !group) return false;
    const char *npc = g_npc_defs[def_idx].name;
    if (key_matches(group, npc)) return true;
    if (key_matches(group, "Barrows brothers")) {
        return key_matches("Ahrim the Blighted", npc) ||
               key_matches("Dharok the Wretched", npc) ||
               key_matches("Guthan the Infested", npc) ||
               key_matches("Karil the Tainted", npc) ||
               key_matches("Torag the Corrupted", npc) ||
               key_matches("Verac the Defiled", npc);
    }
    if (key_matches(group, "Dagannoth Kings")) {
        return key_matches("Dagannoth Rex", npc) ||
               key_matches("Dagannoth Prime", npc) ||
               key_matches("Dagannoth Supreme", npc);
    }
    if (key_matches(group, "Grotesque Guardians")) {
        return key_matches("Dawn", npc) || key_matches("Dusk", npc);
    }
    if (key_matches(group, "Vet'ion")) return key_matches("Calvar'ion", npc);
    if (key_matches(group, "Venenatis")) return key_matches("Spindel", npc);
    if (key_matches(group, "Callisto")) return key_matches("Artio", npc);
    if (key_matches(group, "Crazy archaeologist")) {
        return key_matches("Deranged archaeologist", npc);
    }
    return false;
}

static bool npc_matches_list(int def_idx, const char *list) {
    if (!list || !list[0]) return false;
    const char *s = list;
    while (*s) {
        while (*s == ' ' || *s == ',' || *s == ';') s++;
        char token[80];
        int w = 0;
        while (*s && *s != ',' && *s != ';' && w < (int)sizeof(token) - 1) {
            token[w++] = *s++;
        }
        token[w] = '\0';
        while (w > 0 && token[w - 1] == ' ') token[--w] = '\0';
        if (token[0] && npc_matches_group_alias(def_idx, token)) return true;
    }
    return false;
}

static bool npc_matches_task_def(int def_idx, const RcSlayerTaskDef *task,
                                 const RcPlayer *player) {
    if (!task) return false;
    if ((task->task_flags & RC_SLAYER_TASK_BOSS_SECOND_ROLL) != 0 &&
            player && player->slayer_boss_name[0]) {
        return npc_matches_group_alias(def_idx, player->slayer_boss_name);
    }
    return npc_matches_name(def_idx, task->name) ||
           npc_matches_list(def_idx, task->alternatives);
}

int rc_load_slayer(const char *path) {
    if (!path) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "slayer: can't open %s\n", path);
        return -1;
    }

    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic") ||
            !rc_read_exact(f, &version, sizeof(version), 1, path, "version") ||
            !rc_read_exact(f, &count, sizeof(count), 1, path, "count")) {
        rc_asset_close(f);
        return -1;
    }
    if (magic != SLAY_MAGIC || version == 0 || version > SLAY_VERSION) {
        rc_asset_close(f);
        fprintf(stderr, "slayer: bad header\n");
        return -1;
    }

    memset(g_rc_slayer_masters, 0, sizeof(g_rc_slayer_masters));
    g_rc_slayer_master_count = 0;

    for (uint32_t i = 0; i < count; i++) {
        RcSlayerMasterDef master;
        memset(&master, 0, sizeof(master));
        uint16_t task_count;
        if (!read_pstr(f, master.name, sizeof(master.name), path, "master") ||
                !rc_read_exact(f, &task_count, sizeof(task_count), 1,
                               path, "task count")) {
            rc_asset_close(f);
            return -1;
        }
        if (version >= 2 &&
                (!rc_read_exact(f, &master.req_slayer,
                                sizeof(master.req_slayer), 1,
                                path, "master slayer req") ||
                 !rc_read_exact(f, &master.req_combat,
                                sizeof(master.req_combat), 1,
                                path, "master combat req"))) {
            rc_asset_close(f);
            return -1;
        }
        for (uint16_t j = 0; j < task_count; j++) {
            uint16_t weight;
            char name[64];
            if (!rc_read_exact(f, &weight, sizeof(weight), 1,
                               path, "task weight")) {
                rc_asset_close(f);
                return -1;
            }
            uint16_t amount_min = 0, amount_max = 0;
            uint16_t extended_min = 0, extended_max = 0;
            uint32_t unlock_flags = 0;
            uint64_t progression_flags = 0;
            uint64_t progression_any_flags = 0;
            uint16_t task_flags = 0;
            uint8_t req_slayer = 0, req_combat = 0;
            char alternatives[128] = "";
            char requirement_text[192] = "";
            char locations[512] = "";
            char boss_candidates[2048] = "";
            if (version >= 2 &&
                    (!rc_read_exact(f, &amount_min, sizeof(amount_min), 1,
                                    path, "task amount min") ||
                     !rc_read_exact(f, &amount_max, sizeof(amount_max), 1,
                                    path, "task amount max") ||
                     !rc_read_exact(f, &extended_min, sizeof(extended_min), 1,
                                    path, "task extended min") ||
                     !rc_read_exact(f, &extended_max, sizeof(extended_max), 1,
                                    path, "task extended max") ||
                     !rc_read_exact(f, &unlock_flags, sizeof(unlock_flags), 1,
                                    path, "task unlock flags") ||
                     !rc_read_exact(f, &req_slayer, sizeof(req_slayer), 1,
                                    path, "task slayer req") ||
                     !rc_read_exact(f, &req_combat, sizeof(req_combat), 1,
                                    path, "task combat req"))) {
                rc_asset_close(f);
                return -1;
            }
            if (!read_pstr(f, name, sizeof(name), path, "task")) {
                rc_asset_close(f);
                return -1;
            }
            if (version >= 2 &&
                    (!read_pstr(f, alternatives, sizeof(alternatives),
                                path, "task alternatives") ||
                     !read_pstr(f, requirement_text,
                                sizeof(requirement_text),
                                path, "task requirements"))) {
                rc_asset_close(f);
                return -1;
            }
            if (version >= 3 &&
                    (!rc_read_exact(f, &progression_flags,
                                    sizeof(progression_flags), 1,
                                    path, "task progression flags") ||
                     !rc_read_exact(f, &progression_any_flags,
                                    sizeof(progression_any_flags), 1,
                                    path, "task progression any flags") ||
                     !rc_read_exact(f, &task_flags, sizeof(task_flags), 1,
                                    path, "task flags") ||
                     !read_str16(f, locations, sizeof(locations),
                                 path, "task locations") ||
                     !read_str16(f, boss_candidates,
                                 sizeof(boss_candidates),
                                 path, "task boss candidates"))) {
                rc_asset_close(f);
                return -1;
            }
            if (master.task_count < RC_SLAYER_MAX_TASKS) {
                RcSlayerTaskDef *task = &master.tasks[master.task_count++];
                task->weight = weight;
                task->amount_min = amount_min;
                task->amount_max = amount_max;
                task->extended_min = extended_min;
                task->extended_max = extended_max;
                task->unlock_flags = unlock_flags;
                task->progression_flags = progression_flags;
                task->progression_any_flags = progression_any_flags;
                task->task_flags = task_flags;
                task->req_slayer = req_slayer;
                task->req_combat = req_combat;
                strncpy(task->name, name, sizeof(task->name) - 1);
                strncpy(task->alternatives, alternatives,
                        sizeof(task->alternatives) - 1);
                strncpy(task->requirement_text, requirement_text,
                        sizeof(task->requirement_text) - 1);
                strncpy(task->locations, locations,
                        sizeof(task->locations) - 1);
                strncpy(task->boss_candidates, boss_candidates,
                        sizeof(task->boss_candidates) - 1);
                parse_task_locations(task);
                parse_task_boss_candidates(task);
                master.total_weight += weight;
            }
        }
        if (g_rc_slayer_master_count < RC_SLAYER_MAX_MASTERS) {
            g_rc_slayer_masters[g_rc_slayer_master_count++] = master;
        }
    }

    rc_asset_close(f);
    fprintf(stderr, "slayer: loaded %d masters from %s\n",
            g_rc_slayer_master_count, path);
    return g_rc_slayer_master_count;
}

int rc_slayer_find_master(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < g_rc_slayer_master_count; i++) {
        if (strcmp(g_rc_slayer_masters[i].name, name) == 0) return i;
    }
    return -1;
}

static int player_combat_level(const RcPlayer *p) {
    int attack = p->skills.base_level[SKILL_ATTACK];
    int strength = p->skills.base_level[SKILL_STRENGTH];
    int defence = p->skills.base_level[SKILL_DEFENCE];
    int hp = p->skills.base_level[SKILL_HITPOINTS];
    int prayer = p->skills.base_level[SKILL_PRAYER];
    int ranged = p->skills.base_level[SKILL_RANGED];
    int magic = p->skills.base_level[SKILL_MAGIC];
    int base = (defence + hp + prayer / 2) * 100 / 4;
    int melee = (attack + strength) * 325 / 1000;
    int range = (ranged * 3 / 2) * 325 / 1000;
    int mage = (magic * 3 / 2) * 325 / 1000;
    int best = melee;
    if (range > best) best = range;
    if (mage > best) best = mage;
    return base / 100 + best;
}

static int player_has_task_key(const RcPlayer *p, const uint32_t *keys,
                               int count, const char *task_name) {
    uint32_t h = task_key_hash(task_name);
    for (int i = 0; i < count; i++) {
        if (keys[i] == h) return 1;
    }
    return 0;
}

static int add_task_key(uint32_t *keys, uint8_t *count, int cap,
                        const char *task_name) {
    uint32_t h = task_key_hash(task_name);
    for (int i = 0; i < *count; i++) {
        if (keys[i] == h) return 0;
    }
    if (*count >= cap) return -1;
    keys[*count] = h;
    (*count)++;
    return 0;
}

static bool has_progression(const RcPlayer *p, uint64_t all_flags,
                            uint64_t any_flags) {
    if ((all_flags & p->slayer_progression_flags) != all_flags) return false;
    if (any_flags && (any_flags & p->slayer_progression_flags) == 0) {
        return false;
    }
    return true;
}

static const char *copy_token(const char *s, char *out, int cap) {
    int w = 0;
    while (*s && *s != ';' && w < cap - 1) out[w++] = *s++;
    out[w] = '\0';
    return *s == ';' ? s + 1 : s;
}

static int split_pipe4(char *token, char **a, char **b,
                       char **c, char **d) {
    *a = token;
    *b = strchr(token, '|');
    if (!*b) return 0;
    *(*b)++ = '\0';
    *c = strchr(*b, '|');
    if (!*c) return 0;
    *(*c)++ = '\0';
    *d = strchr(*c, '|');
    if (!*d) return 0;
    *(*d)++ = '\0';
    return 1;
}

static void parse_task_locations(RcSlayerTaskDef *task) {
    task->location_count = 0;
    const char *s = task->locations;
    while (*s && task->location_count < RC_SLAYER_MAX_LOCATIONS) {
        char token[160];
        const char *next = copy_token(s, token, sizeof(token));
        char *name = token;
        char *flags_s = strchr(token, '|');
        if (flags_s) *flags_s++ = '\0';
        if (name[0]) {
            RcSlayerLocationDef *loc =
                &task->location_defs[task->location_count++];
            strncpy(loc->name, name, sizeof(loc->name) - 1);
            loc->progression_flags =
                flags_s ? strtoull(flags_s, NULL, 0) : 0;
        }
        if (next == s) break;
        s = next;
    }
}

static void parse_task_boss_candidates(RcSlayerTaskDef *task) {
    task->boss_candidate_count = 0;
    const char *s = task->boss_candidates;
    while (*s && task->boss_candidate_count < RC_SLAYER_MAX_BOSS_CANDIDATES) {
        char token[160];
        const char *next = copy_token(s, token, sizeof(token));
        char *name, *req, *all, *any;
        if (split_pipe4(token, &name, &req, &all, &any) && name[0]) {
            RcSlayerBossCandidateDef *boss =
                &task->boss_candidate_defs[task->boss_candidate_count++];
            strncpy(boss->name, name, sizeof(boss->name) - 1);
            boss->req_slayer = (uint8_t)atoi(req);
            boss->progression_flags = strtoull(all, NULL, 0);
            boss->progression_any_flags = strtoull(any, NULL, 0);
        }
        if (next == s) break;
        s = next;
    }
}

static bool boss_allowed_for_master(const char *master, const char *boss) {
    if (!master || !boss) return false;
    if (strcmp(master, "Krystilia") == 0) {
        return strcmp(boss, "Callisto") == 0 ||
               strcmp(boss, "Chaos Elemental") == 0 ||
               strcmp(boss, "Chaos Fanatic") == 0 ||
               strcmp(boss, "Crazy archaeologist") == 0 ||
               strcmp(boss, "Scorpia") == 0 ||
               strcmp(boss, "Venenatis") == 0 ||
               strcmp(boss, "Vet'ion") == 0;
    }
    if (strcmp(boss, "Alchemical Hydra") == 0) {
        return strcmp(master, "Konar quo Maten") == 0;
    }
    return strcmp(master, "Konar quo Maten") == 0 ||
           strcmp(master, "Nieve") == 0 ||
           strcmp(master, "Steve") == 0 ||
           strcmp(master, "Duradel") == 0;
}

static int eligible_boss_count(const RcWorld *world, int master_idx,
                               const RcSlayerTaskDef *task) {
    if (!world || !task || task->boss_candidate_count == 0 ||
            master_idx < 0 || master_idx >= g_rc_slayer_master_count) {
        return 0;
    }
    const char *master = g_rc_slayer_masters[master_idx].name;
    const RcPlayer *p = &world->player;
    int slayer = p->skills.base_level[SKILL_SLAYER];
    int count = 0;
    for (uint8_t i = 0; i < task->boss_candidate_count; i++) {
        const RcSlayerBossCandidateDef *boss = &task->boss_candidate_defs[i];
        if (boss_allowed_for_master(master, boss->name) &&
                slayer >= boss->req_slayer &&
                has_progression(p, boss->progression_flags,
                                boss->progression_any_flags)) {
            count++;
        }
    }
    return count;
}

int rc_slayer_task_eligible(const struct RcWorld *world, int master_idx,
                            int task_idx) {
    if (!world || master_idx < 0 || master_idx >= g_rc_slayer_master_count) {
        return 0;
    }
    RcSlayerMasterDef *m = &g_rc_slayer_masters[master_idx];
    if (task_idx < 0 || task_idx >= (int)m->task_count) return 0;
    const RcSlayerTaskDef *task = &m->tasks[task_idx];
    const RcPlayer *p = &world->player;
    int slayer = p->skills.base_level[SKILL_SLAYER];
    int combat = player_combat_level(p);
    if (slayer < m->req_slayer || combat < m->req_combat) return 0;
    if (slayer < task->req_slayer || combat < task->req_combat) return 0;
    if ((task->unlock_flags & p->slayer_unlocks) != task->unlock_flags) {
        return 0;
    }
    if (!has_progression(p, task->progression_flags,
                         task->progression_any_flags)) {
        return 0;
    }
    if ((task->task_flags & RC_SLAYER_TASK_BOSS_SECOND_ROLL) != 0 &&
            eligible_boss_count(world, master_idx, task) == 0) {
        return 0;
    }
    return 1;
}

int rc_slayer_block_task(struct RcWorld *world, const char *task_name) {
    if (!world || !task_name) return -1;
    return add_task_key(world->player.slayer_blocked_keys,
                        &world->player.slayer_block_count,
                        RC_SLAYER_BLOCK_SLOTS, task_name);
}

int rc_slayer_prefer_task(struct RcWorld *world, const char *task_name) {
    if (!world || !task_name) return -1;
    return add_task_key(world->player.slayer_preferred_keys,
                        &world->player.slayer_prefer_count,
                        RC_SLAYER_PREFER_SLOTS, task_name);
}

int rc_slayer_task_amount_for_roll(const RcSlayerTaskDef *task,
                                   uint32_t roll, int extended) {
    if (!task) return 0;
    uint16_t min = task->amount_min;
    uint16_t max = task->amount_max;
    if (extended && task->extended_min && task->extended_max) {
        min = task->extended_min;
        max = task->extended_max;
    }
    if (!min && !max) return 1;
    if (max < min) max = min;
    return (int)min + (int)(roll % ((uint32_t)max - min + 1u));
}

int rc_slayer_task_amount_for_world(const RcWorld *world,
                                    const RcSlayerTaskDef *task,
                                    uint32_t roll, int extended) {
    if (!task) return 0;
    if ((task->task_flags & RC_SLAYER_TASK_BOSS_SECOND_ROLL) == 0) {
        return rc_slayer_task_amount_for_roll(task, roll, extended);
    }
    uint16_t min = task->amount_min ? task->amount_min : 3;
    uint16_t max = task->amount_max ? task->amount_max : 35;
    int tier = world ? world->player.slayer_combat_achievement_tier : 0;
    if (tier > 6) tier = 6;
    max = (uint16_t)(max + tier * 5);
    if (max > 65) max = 65;
    return (int)min + (int)(roll % ((uint32_t)max - min + 1u));
}

int rc_slayer_task_location_for_roll(const RcWorld *world,
                                     const RcSlayerTaskDef *task,
                                     uint32_t roll, char *out, int cap) {
    if (!world || !task || !out || cap <= 0 ||
            task->location_count == 0) {
        return -1;
    }
    out[0] = '\0';
    int eligible = 0;
    for (uint8_t i = 0; i < task->location_count; i++) {
        const RcSlayerLocationDef *loc = &task->location_defs[i];
        if (has_progression(&world->player, loc->progression_flags, 0)) {
            eligible++;
        }
    }
    if (eligible == 0) return -1;

    int target = (int)(roll % (uint32_t)eligible);
    int seen = 0;
    for (uint8_t i = 0; i < task->location_count; i++) {
        const RcSlayerLocationDef *loc = &task->location_defs[i];
        if (has_progression(&world->player, loc->progression_flags, 0)) {
            if (seen == target) {
                strncpy(out, loc->name, (size_t)cap - 1);
                out[cap - 1] = '\0';
                return 0;
            }
            seen++;
        }
    }
    return -1;
}

int rc_slayer_boss_task_for_roll(const RcWorld *world, int master_idx,
                                 const RcSlayerTaskDef *task,
                                 uint32_t roll, char *out, int cap) {
    if (!world || !task || !out || cap <= 0 ||
            task->boss_candidate_count == 0 ||
            master_idx < 0 || master_idx >= g_rc_slayer_master_count) {
        return -1;
    }
    out[0] = '\0';
    const char *master = g_rc_slayer_masters[master_idx].name;
    const RcPlayer *p = &world->player;
    int slayer = p->skills.base_level[SKILL_SLAYER];
    int eligible = eligible_boss_count(world, master_idx, task);
    if (eligible == 0) return -1;
    int target = (int)(roll % (uint32_t)eligible);
    int seen = 0;
    for (uint8_t i = 0; i < task->boss_candidate_count; i++) {
        const RcSlayerBossCandidateDef *boss = &task->boss_candidate_defs[i];
        if (boss_allowed_for_master(master, boss->name) &&
                slayer >= boss->req_slayer &&
                has_progression(p, boss->progression_flags,
                                boss->progression_any_flags)) {
            if (seen == target) {
                strncpy(out, boss->name, (size_t)cap - 1);
                out[cap - 1] = '\0';
                return 0;
            }
            seen++;
        }
    }
    return -1;
}

int rc_slayer_find_task(int master_idx, const char *task_name) {
    if (master_idx < 0 || master_idx >= g_rc_slayer_master_count ||
            !task_name) {
        return -1;
    }
    char wanted[80], got[80];
    task_key(task_name, wanted, sizeof(wanted));
    RcSlayerMasterDef *m = &g_rc_slayer_masters[master_idx];
    for (uint16_t i = 0; i < m->task_count; i++) {
        task_key(m->tasks[i].name, got, sizeof(got));
        if (strcmp(wanted, got) == 0) return i;
    }
    return -1;
}

int rc_slayer_start_task(struct RcWorld *world, const char *master_name,
                         const char *task_name, int amount) {
    if (!world || amount <= 0) return -1;
    int master = rc_slayer_find_master(master_name);
    int task = rc_slayer_find_task(master, task_name);
    if (master < 0 || task < 0) return -1;
    if (!rc_slayer_task_eligible(world, master, task)) return -1;
    world->player.slayer_master_idx = master;
    world->player.slayer_task_idx = task;
    world->player.slayer_task_remaining = amount;
    world->player.slayer_boss_name[0] = '\0';
    world->player.slayer_location[0] = '\0';
    return 0;
}

int rc_slayer_assign_task(struct RcWorld *world, const char *master_name,
                          uint32_t roll, int amount) {
    if (!world) return -1;
    int master = rc_slayer_find_master(master_name);
    if (master < 0) return -1;
    RcSlayerMasterDef *m = &g_rc_slayer_masters[master];
    if (m->task_count == 0 || m->total_weight == 0) return -1;
    uint32_t total = 0;
    for (uint16_t i = 0; i < m->task_count; i++) {
        if (!rc_slayer_task_eligible(world, master, i)) continue;
        if (player_has_task_key(&world->player,
                                world->player.slayer_blocked_keys,
                                world->player.slayer_block_count,
                                m->tasks[i].name)) {
            continue;
        }
        uint32_t weight = m->tasks[i].weight;
        if (player_has_task_key(&world->player,
                                world->player.slayer_preferred_keys,
                                world->player.slayer_prefer_count,
                                m->tasks[i].name)) {
            weight *= 2u;
        }
        total += weight;
    }
    if (total == 0) return -1;
    uint32_t pick = roll % total;
    uint32_t acc = 0;
    for (uint16_t i = 0; i < m->task_count; i++) {
        if (!rc_slayer_task_eligible(world, master, i)) continue;
        if (player_has_task_key(&world->player,
                                world->player.slayer_blocked_keys,
                                world->player.slayer_block_count,
                                m->tasks[i].name)) {
            continue;
        }
        uint32_t weight = m->tasks[i].weight;
        if (player_has_task_key(&world->player,
                                world->player.slayer_preferred_keys,
                                world->player.slayer_prefer_count,
                                m->tasks[i].name)) {
            weight *= 2u;
        }
        acc += weight;
        if (pick < acc) {
            world->player.slayer_master_idx = master;
            world->player.slayer_task_idx = i;
            world->player.slayer_task_remaining =
                amount > 0 ? amount :
                rc_slayer_task_amount_for_world(world, &m->tasks[i],
                                                roll >> 16, 0);
            world->player.slayer_boss_name[0] = '\0';
            world->player.slayer_location[0] = '\0';
            if ((m->tasks[i].task_flags & RC_SLAYER_TASK_BOSS_SECOND_ROLL)
                    != 0) {
                rc_slayer_boss_task_for_roll(
                    world, master, &m->tasks[i], roll >> 8,
                    world->player.slayer_boss_name,
                    sizeof(world->player.slayer_boss_name));
            }
            if (strcmp(m->name, "Konar quo Maten") == 0 &&
                    (m->tasks[i].task_flags & RC_SLAYER_TASK_HAS_LOCATIONS)
                    != 0) {
                rc_slayer_task_location_for_roll(
                    world, &m->tasks[i], roll >> 4,
                    world->player.slayer_location,
                    sizeof(world->player.slayer_location));
            }
            return 0;
        }
    }
    return -1;
}

const char *rc_slayer_current_task_name(const struct RcWorld *world) {
    if (!world) return NULL;
    int master = world->player.slayer_master_idx;
    int task = world->player.slayer_task_idx;
    if (master < 0 || master >= g_rc_slayer_master_count) return NULL;
    RcSlayerMasterDef *m = &g_rc_slayer_masters[master];
    if (task < 0 || task >= (int)m->task_count) return NULL;
    return m->tasks[task].name;
}

const char *rc_slayer_current_boss_name(const struct RcWorld *world) {
    if (!world || !world->player.slayer_boss_name[0]) return NULL;
    return world->player.slayer_boss_name;
}

const char *rc_slayer_current_location(const struct RcWorld *world) {
    if (!world || !world->player.slayer_location[0]) return NULL;
    return world->player.slayer_location;
}

static void on_npc_died(struct RcWorld *world, int evt,
                        const void *payload, void *ctx) {
    (void)evt;
    (void)ctx;
    if (!world || !payload || world->player.slayer_task_remaining <= 0) return;
    int def_idx = rc_npc_def_find((int)((const RcPayloadNpcEvent *)payload)->def_id);
    int master = world->player.slayer_master_idx;
    int task_idx = world->player.slayer_task_idx;
    if (master < 0 || master >= g_rc_slayer_master_count) return;
    RcSlayerMasterDef *m = &g_rc_slayer_masters[master];
    if (task_idx < 0 || task_idx >= (int)m->task_count) return;
    if (npc_matches_task_def(def_idx, &m->tasks[task_idx], &world->player)) {
        world->player.slayer_task_remaining--;
    }
}

void rc_slayer_init(struct RcWorld *world) {
    if (!world) return;
    rc_event_subscribe(world, RC_EVT_NPC_DIED, on_npc_died, NULL);
}
