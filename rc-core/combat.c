#include "combat.h"
#include "npc.h"

float rc_hit_chance(int att_roll, int def_roll) {
    if (att_roll > def_roll) {
        return 1.0f - ((float)(def_roll + 2) / (2.0f * (att_roll + 1)));
    } else {
        return (float)att_roll / (2.0f * (def_roll + 1));
    }
}

void rc_queue_hit(RcPendingHit *hits, int *count, int damage, int delay,
                  int style, int source_idx) {
    if (*count >= RC_MAX_PENDING_HITS) return;
    RcPendingHit *hit = &hits[*count];
    hit->active = 1;
    hit->damage = damage;
    hit->ticks_remaining = delay;
    hit->attack_style = style;
    hit->source_idx = source_idx;
    hit->prayer_snapshot = 0;
    hit->prayer_lock_tick = 0;
    (*count)++;
}

// TODO: implement full melee/ranged/magic calc using RSMod formulas
RcCombatCalc rc_calc_melee(const RcPlayer *attacker, int npc_def_id) {
    (void)attacker; (void)npc_def_id;
    return (RcCombatCalc){0, 0, 0.0f, 0};
}

RcCombatCalc rc_calc_ranged(const RcPlayer *attacker, int npc_def_id) {
    (void)attacker; (void)npc_def_id;
    return (RcCombatCalc){0, 0, 0.0f, 0};
}

RcCombatCalc rc_calc_magic(const RcPlayer *attacker, int npc_def_id) {
    (void)attacker; (void)npc_def_id;
    return (RcCombatCalc){0, 0, 0.0f, 0};
}

RcCombatCalc rc_calc_npc_attack(int npc_def_id, const RcPlayer *defender) {
    (void)npc_def_id; (void)defender;
    return (RcCombatCalc){0, 0, 0.0f, 0};
}
