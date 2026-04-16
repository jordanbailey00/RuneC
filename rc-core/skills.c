#include "skills.h"
#include <math.h>

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
