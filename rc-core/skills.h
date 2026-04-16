#ifndef RC_SKILLS_H
#define RC_SKILLS_H

#include "types.h"

// Precomputed XP table (xp_table[level] = xp required for that level)
extern const int RC_XP_TABLE[100];

// Level from XP (binary search)
int rc_level_for_xp(int xp);

// Add XP to a skill, update base level if it changed
void rc_add_xp(RcSkills *skills, RcSkill skill, int xp);

// Combat level formula
int rc_combat_level(const RcSkills *skills);

// Stat restore: boosted stats decay toward base (every 60 ticks)
void rc_stat_restore_tick(RcSkills *skills);

#endif
