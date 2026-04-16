#ifndef RC_COMBAT_H
#define RC_COMBAT_H

#include "types.h"

typedef struct {
    int attack_roll;
    int defence_roll;
    float hit_chance;
    int max_hit;
} RcCombatCalc;

// Player vs NPC
RcCombatCalc rc_calc_melee(const RcPlayer *attacker, int npc_def_id);
RcCombatCalc rc_calc_ranged(const RcPlayer *attacker, int npc_def_id);
RcCombatCalc rc_calc_magic(const RcPlayer *attacker, int npc_def_id);

// NPC vs Player
RcCombatCalc rc_calc_npc_attack(int npc_def_id, const RcPlayer *defender);

// Hit chance: if att > def: 1 - (def+2)/(2*(att+1)), else: att/(2*(def+1))
float rc_hit_chance(int att_roll, int def_roll);

// Queue a pending hit with delay
void rc_queue_hit(RcPendingHit *hits, int *count, int damage, int delay,
                  int style, int source_idx);

#endif
