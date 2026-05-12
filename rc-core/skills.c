#include "skills.h"
#include "config.h"
#include "io.h"
#include "items.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RCIP_MAGIC 0x50494352u
#define SDRP_MAGIC 0x50524453u
#define GNOD_MAGIC 0x444F4E47u
#define SKILL_DATA_VERSION 1u
#define MAX_NODE_REGION 65536

typedef struct {
    uint32_t first, count;
} RcSkillIndex;

RcRecipe *g_rc_recipes = NULL;
RcSkillDropSource *g_rc_skill_drop_sources = NULL;
RcSkillDrop *g_rc_skill_drops = NULL;
RcGatheringNode *g_rc_gathering_nodes = NULL;
int g_rc_recipe_count = 0;
int g_rc_skill_drop_source_count = 0;
int g_rc_skill_drop_count = 0;
int g_rc_gathering_node_count = 0;

static int g_recipe_by_output[RC_MAX_ITEM_DEFS];
static int *g_recipe_next = NULL;
static RcSkillIndex g_gathering_region_index[MAX_NODE_REGION];

static int read_header(FILE *f, const char *path, uint32_t expect,
                       uint32_t *count) {
    uint32_t magic, version;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, path, "version")
            || !rc_read_exact(f, count, sizeof(*count), 1, path, "count")) {
        return 0;
    }
    return magic == expect && version == SKILL_DATA_VERSION;
}

static int read_str8(FILE *f, char *out, int cap,
                     const char *path, const char *what) {
    uint8_t len;
    if (!rc_read_exact(f, &len, sizeof(len), 1, path, what)) return 0;
    int keep = len < (uint8_t)(cap - 1) ? (int)len : cap - 1;
    if (keep && !rc_read_exact(f, out, 1, (size_t)keep, path, what)) return 0;
    out[keep] = '\0';
    if (len > (uint8_t)keep &&
            !rc_seek(f, (long)(len - (uint8_t)keep), SEEK_CUR, path, what)) {
        return 0;
    }
    return 1;
}

static int inv_count_item(const RcInvSlot *inv, int item_id) {
    int total = 0;
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        if (inv[i].item_id == item_id) total += inv[i].quantity;
    }
    return total;
}

static void inv_remove_qty(RcInvSlot *inv, int item_id, int qty) {
    for (int i = 0; i < RC_INVENTORY_SIZE && qty > 0; i++) {
        if (inv[i].item_id != item_id) continue;
        int take = inv[i].quantity < qty ? inv[i].quantity : qty;
        inv[i].quantity -= take;
        qty -= take;
        if (inv[i].quantity == 0) inv[i].item_id = -1;
    }
}

static int inv_can_add(const RcInvSlot *inv, int item_id, int quantity) {
    const RcItemDef *def = rc_item_def_get(item_id);
    if (def && def->stackable && rc_inv_find(inv, item_id) >= 0) return 1;
    int needed = def && def->stackable ? 1 : quantity;
    int free_slots = 0;
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        if (inv[i].item_id == -1) free_slots++;
    }
    return free_slots >= needed;
}

int rc_load_recipes(const char *path) {
    if (!path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint32_t count;
    if (!read_header(f, path, RCIP_MAGIC, &count)) {
        fclose(f);
        return -1;
    }
    RcRecipe *rows = calloc(count ? count : 1u, sizeof(*rows));
    int *next = malloc((count ? count : 1u) * sizeof(*next));
    if (!rows || !next) {
        free(rows);
        free(next);
        fclose(f);
        return -1;
    }
    for (uint32_t i = 0; i < count; i++) {
        next[i] = -1;
        RcRecipe *row = &rows[i];
        uint8_t n;
        if (!read_str8(f, row->name, sizeof(row->name), path, "name")
                || !rc_read_exact(f, &n, sizeof(n), 1, path, "req count")) {
            free(rows); free(next); fclose(f); return -1;
        }
        row->req_count = n < RC_RECIPE_MAX_REQS ? n : RC_RECIPE_MAX_REQS;
        for (int j = 0; j < n; j++) {
            uint8_t skill, level;
            uint16_t xp_q1;
            if (!rc_read_exact(f, &skill, sizeof(skill), 1, path, "skill")
                    || !rc_read_exact(f, &level, sizeof(level), 1, path, "level")
                    || !rc_read_exact(f, &xp_q1, sizeof(xp_q1), 1, path, "xp")) {
                free(rows); free(next); fclose(f); return -1;
            }
            if (j < RC_RECIPE_MAX_REQS) row->reqs[j] = (RcRecipeReq){
                .skill = skill, .level = level, .xp_q1 = xp_q1,
            };
        }
        if (!rc_read_exact(f, &n, sizeof(n), 1, path, "input count")) {
            free(rows); free(next); fclose(f); return -1;
        }
        row->input_count = n < RC_RECIPE_MAX_INPUTS ? n : RC_RECIPE_MAX_INPUTS;
        for (int j = 0; j < n; j++) {
            uint32_t item_id;
            uint16_t qty;
            if (!rc_read_exact(f, &item_id, sizeof(item_id), 1, path, "input")
                    || !rc_read_exact(f, &qty, sizeof(qty), 1, path, "qty")) {
                free(rows); free(next); fclose(f); return -1;
            }
            if (j < RC_RECIPE_MAX_INPUTS) row->inputs[j] = (RcRecipeItemQty){
                .item_id = item_id, .quantity = qty,
            };
        }
        if (!rc_read_exact(f, &n, sizeof(n), 1, path, "tool count")) {
            free(rows); free(next); fclose(f); return -1;
        }
        row->tool_count = n < RC_RECIPE_MAX_TOOLS ? n : RC_RECIPE_MAX_TOOLS;
        for (int j = 0; j < n; j++) {
            uint32_t item_id;
            if (!rc_read_exact(f, &item_id, sizeof(item_id), 1, path, "tool")) {
                free(rows); free(next); fclose(f); return -1;
            }
            if (j < RC_RECIPE_MAX_TOOLS) row->tools[j] = item_id;
        }
        if (!read_str8(f, row->facility, sizeof(row->facility), path, "facility")
                || !rc_read_exact(f, &row->output_item, sizeof(row->output_item), 1,
                                  path, "output")
                || !rc_read_exact(f, &row->output_qty, sizeof(row->output_qty), 1,
                                  path, "output qty")
                || !rc_read_exact(f, &row->ticks, sizeof(row->ticks), 1,
                                  path, "ticks")
                || !rc_read_exact(f, &row->flags, sizeof(row->flags), 1,
                                  path, "flags")) {
            free(rows); free(next); fclose(f); return -1;
        }
    }
    fclose(f);
    for (int i = 0; i < RC_MAX_ITEM_DEFS; i++) g_recipe_by_output[i] = -1;
    for (int i = (int)count - 1; i >= 0; i--) {
        uint32_t item_id = rows[i].output_item;
        if (item_id < RC_MAX_ITEM_DEFS) {
            next[i] = g_recipe_by_output[item_id];
            g_recipe_by_output[item_id] = i;
        }
    }
    free(g_rc_recipes);
    free(g_recipe_next);
    g_rc_recipes = rows;
    g_recipe_next = next;
    g_rc_recipe_count = (int)count;
    return g_rc_recipe_count;
}

int rc_load_skill_drops(const char *path) {
    if (!path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint32_t count;
    if (!read_header(f, path, SDRP_MAGIC, &count)) {
        fclose(f);
        return -1;
    }
    RcSkillDropSource *sources = calloc(count ? count : 1u, sizeof(*sources));
    RcSkillDrop *drops = NULL;
    int drop_count = 0;
    if (!sources) {
        fclose(f);
        return -1;
    }
    for (uint32_t i = 0; i < count; i++) {
        RcSkillDropSource *src = &sources[i];
        uint16_t n;
        if (!read_str8(f, src->name, sizeof(src->name), path, "source")
                || !rc_read_exact(f, &n, sizeof(n), 1, path, "drop count")) {
            free(sources); free(drops); fclose(f); return -1;
        }
        RcSkillDrop *next = realloc(drops, (size_t)(drop_count + n) * sizeof(*drops));
        if (n && !next) {
            free(sources); free(drops); fclose(f); return -1;
        }
        drops = next;
        src->first_drop = (uint32_t)drop_count;
        src->drop_count = n;
        for (uint16_t j = 0; j < n; j++) {
            RcSkillDrop *drop = &drops[drop_count++];
            if (!rc_read_exact(f, &drop->item_id, sizeof(drop->item_id), 1,
                               path, "item")
                    || !rc_read_exact(f, &drop->qmin, sizeof(drop->qmin), 1,
                                      path, "qmin")
                    || !rc_read_exact(f, &drop->qmax, sizeof(drop->qmax), 1,
                                      path, "qmax")
                    || !rc_read_exact(f, &drop->rarity_inv,
                                      sizeof(drop->rarity_inv), 1,
                                      path, "rarity")) {
                free(sources); free(drops); fclose(f); return -1;
            }
        }
    }
    fclose(f);
    free(g_rc_skill_drop_sources);
    free(g_rc_skill_drops);
    g_rc_skill_drop_sources = sources;
    g_rc_skill_drops = drops;
    g_rc_skill_drop_source_count = (int)count;
    g_rc_skill_drop_count = drop_count;
    return g_rc_skill_drop_source_count;
}

int rc_load_gathering_nodes(const char *path) {
    if (!path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint32_t count;
    if (!read_header(f, path, GNOD_MAGIC, &count)) {
        fclose(f);
        return -1;
    }
    RcGatheringNode *rows = malloc((count ? count : 1u) * sizeof(*rows));
    if (!rows) {
        fclose(f);
        return -1;
    }
    for (int i = 0; i < MAX_NODE_REGION; i++) {
        g_gathering_region_index[i].first = UINT32_MAX;
        g_gathering_region_index[i].count = 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        RcGatheringNode *row = &rows[i];
        uint16_t pad;
        if (!rc_read_exact(f, &row->obj_id, sizeof(row->obj_id), 1, path,
                           "object")
                || !rc_read_exact(f, &row->x, sizeof(row->x), 1, path, "x")
                || !rc_read_exact(f, &row->y, sizeof(row->y), 1, path, "y")
                || !rc_read_exact(f, &row->mapsquare, sizeof(row->mapsquare),
                                  1, path, "mapsquare")
                || !rc_read_exact(f, &row->plane, sizeof(row->plane), 1,
                                  path, "plane")
                || !rc_read_exact(f, &row->skill, sizeof(row->skill), 1,
                                  path, "skill")
                || !rc_read_exact(f, &row->action_mask, sizeof(row->action_mask),
                                  1, path, "action")
                || !rc_read_exact(f, &row->type, sizeof(row->type), 1,
                                  path, "type")
                || !rc_read_exact(f, &row->rotation, sizeof(row->rotation), 1,
                                  path, "rotation")
                || !rc_read_exact(f, &row->flags, sizeof(row->flags), 1,
                                  path, "flags")
                || !rc_read_exact(f, &pad, sizeof(pad), 1, path, "pad")) {
            free(rows); fclose(f); return -1;
        }
        RcSkillIndex *idx = &g_gathering_region_index[row->mapsquare];
        if (idx->first == UINT32_MAX) idx->first = i;
        idx->count++;
    }
    fclose(f);
    free(g_rc_gathering_nodes);
    g_rc_gathering_nodes = rows;
    g_rc_gathering_node_count = (int)count;
    return g_rc_gathering_node_count;
}

const RcRecipe *rc_recipe_get(int idx) {
    if (idx < 0 || idx >= g_rc_recipe_count || !g_rc_recipes) return NULL;
    return &g_rc_recipes[idx];
}

const RcRecipe *rc_recipe_find_output(int item_id) {
    if (item_id < 0 || item_id >= RC_MAX_ITEM_DEFS || !g_rc_recipes) return NULL;
    int idx = g_recipe_by_output[item_id];
    return idx >= 0 ? &g_rc_recipes[idx] : NULL;
}

const RcSkillDropSource *rc_skill_drop_source_find(const char *name) {
    if (!name || !g_rc_skill_drop_sources) return NULL;
    for (int i = 0; i < g_rc_skill_drop_source_count; i++) {
        if (strcmp(g_rc_skill_drop_sources[i].name, name) == 0) {
            return &g_rc_skill_drop_sources[i];
        }
    }
    return NULL;
}

const RcSkillDrop *rc_skill_drops_for(const RcSkillDropSource *source,
                                      int *count) {
    if (!source || !g_rc_skill_drops) {
        if (count) *count = 0;
        return NULL;
    }
    if (count) *count = source->drop_count;
    return &g_rc_skill_drops[source->first_drop];
}

const RcGatheringNode *rc_gathering_nodes_in_region(uint16_t mapsquare,
                                                    int *count) {
    RcSkillIndex idx = g_gathering_region_index[mapsquare];
    if (count) *count = idx.first == UINT32_MAX ? 0 : (int)idx.count;
    if (idx.first == UINT32_MAX || !g_rc_gathering_nodes) return NULL;
    return &g_rc_gathering_nodes[idx.first];
}

const RcGatheringNode *rc_gathering_node_find(int obj_id, int x, int y,
                                              int plane) {
    if (x < 0 || y < 0 || plane < 0 || !g_rc_gathering_nodes) return NULL;
    uint16_t mapsquare = (uint16_t)(((x >> 6) << 8) | (y >> 6));
    int count = 0;
    const RcGatheringNode *rows = rc_gathering_nodes_in_region(mapsquare, &count);
    for (int i = 0; rows && i < count; i++) {
        const RcGatheringNode *row = &rows[i];
        if ((int)row->obj_id == obj_id && row->x == x && row->y == y
                && row->plane == plane) {
            return row;
        }
    }
    return NULL;
}

int rc_recipe_player_can_make(const RcWorld *world, const RcRecipe *recipe) {
    if (!world || !recipe || !(world->enabled & RC_SUB_SKILLS)) return 0;
    for (int i = 0; i < recipe->req_count; i++) {
        int skill = recipe->reqs[i].skill;
        if (skill >= SKILL_COUNT ||
                world->player.skills.base_level[skill] < recipe->reqs[i].level) {
            return 0;
        }
    }
    for (int i = 0; i < recipe->input_count; i++) {
        int item_id = (int)recipe->inputs[i].item_id;
        if (inv_count_item(world->player.inventory, item_id)
                < recipe->inputs[i].quantity) {
            return 0;
        }
    }
    for (int i = 0; i < recipe->tool_count; i++) {
        if (rc_inv_find(world->player.inventory, (int)recipe->tools[i]) < 0) {
            return 0;
        }
    }
    if (recipe->output_item > 0 && recipe->output_qty > 0 &&
            !inv_can_add(world->player.inventory, (int)recipe->output_item,
                         recipe->output_qty)) {
        return 0;
    }
    return 1;
}

int rc_player_apply_recipe(RcWorld *world, const RcRecipe *recipe) {
    if (!rc_recipe_player_can_make(world, recipe)) return 0;
    for (int i = 0; i < recipe->input_count; i++) {
        inv_remove_qty(world->player.inventory, (int)recipe->inputs[i].item_id,
                       recipe->inputs[i].quantity);
    }
    if (recipe->output_item > 0 && recipe->output_qty > 0) {
        const RcItemDef *def = rc_item_def_get((int)recipe->output_item);
        if (def && def->stackable) {
            if (rc_inv_add(world->player.inventory, (int)recipe->output_item,
                           recipe->output_qty) < 0) {
                return 0;
            }
        } else {
            for (int i = 0; i < recipe->output_qty; i++) {
                if (rc_inv_add(world->player.inventory,
                               (int)recipe->output_item, 1) < 0) {
                    return 0;
                }
            }
        }
    }
    for (int i = 0; i < recipe->req_count; i++) {
        int xp = (recipe->reqs[i].xp_q1 + 5) / 10;
        if (xp > 0 && recipe->reqs[i].skill < SKILL_COUNT) {
            rc_add_xp(&world->player.skills,
                      (RcSkill)recipe->reqs[i].skill, xp);
        }
    }
    world->player.skill_action = (int)recipe->output_item;
    world->player.skill_timer = recipe->ticks;
    return 1;
}

// Precomputed XP table: RC_XP_TABLE[level] = total XP needed for that level
// Formula: sum(floor(x + 300 * 2^(x/7)) / 4) for x=1..level-1
const int RC_XP_TABLE[100] = {
    0,        // level 1
    83, 174, 276, 388, 512, 650, 801, 969, 1154,           // 2-10
    1358, 1584, 1833, 2107, 2411, 2746, 3115, 3523, 3973, 4470,  // 11-20
    5018, 5624, 6291, 7028, 7842, 8740, 9730, 10824, 12031, 13363, // 21-30
    14833, 16456, 18247, 20224, 22406, 24815, 27473, 30408, 33648, 37224, // 31-40
    41171, 45529, 50339, 55649, 61512, 67983, 75127, 83014, 91721, 101333, // 41-50
    111945, 123660, 136594, 150872, 166636, 184040, 203254, 224466, 247886, 273742, // 51-60
    302288, 333804, 368599, 407015, 449428, 496254, 547953, 605032, 668051, 737627, // 61-70
    814445, 899257, 992895, 1096278, 1210421, 1336443, 1475581, 1629200, 1798808, 1986068, // 71-80
    2192818, 2421087, 2673114, 2951373, 3258594, 3597792, 3972294, 4385776, 4842295, 5346332, // 81-90
    5902831, 6517253, 7195629, 7944614, 8771558, 9684577, 10692629, 11805606, 13034431,       // 91-99
};

int rc_level_for_xp(int xp) {
    for (int level = 98; level >= 0; level--) {
        if (xp >= RC_XP_TABLE[level]) return level + 1;
    }
    return 1;
}

void rc_add_xp(RcSkills *skills, RcSkill skill, int xp) {
    skills->xp[skill] += xp;
    if (skills->xp[skill] > 200000000) skills->xp[skill] = 200000000;

    int new_level = rc_level_for_xp(skills->xp[skill]);
    if (new_level > skills->base_level[skill]) {
        int diff = new_level - skills->base_level[skill];
        skills->base_level[skill] = new_level;
        skills->boosted_level[skill] += diff;
    }
}

int rc_combat_level(const RcSkills *skills) {
    int atk = skills->base_level[SKILL_ATTACK];
    int str = skills->base_level[SKILL_STRENGTH];
    int def = skills->base_level[SKILL_DEFENCE];
    int hp  = skills->base_level[SKILL_HITPOINTS];
    int pray = skills->base_level[SKILL_PRAYER];
    int rng = skills->base_level[SKILL_RANGED];
    int mag = skills->base_level[SKILL_MAGIC];

    double base = 0.25 * (def + hp + (pray / 2));
    double melee = 0.325 * (atk + str);
    double ranged = 0.325 * (rng / 2 + rng);
    double magic = 0.325 * (mag / 2 + mag);

    double type_bonus = melee;
    if (ranged > type_bonus) type_bonus = ranged;
    if (magic > type_bonus) type_bonus = magic;

    return (int)(base + type_bonus);
}

void rc_stat_restore_tick(RcSkills *skills) {
    // TODO: every 60 ticks, boosted stats decay toward base by 1
    (void)skills;
}
