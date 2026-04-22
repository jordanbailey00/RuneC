#ifndef RC_COMBAT_H
#define RC_COMBAT_H

#include "types.h"

// Equipment bonus array indices (matches osrsreboxed-db layout, 14 slots).
#define EQ_STAB_ATK       0
#define EQ_SLASH_ATK      1
#define EQ_CRUSH_ATK      2
#define EQ_MAGIC_ATK      3
#define EQ_RANGED_ATK     4
#define EQ_STAB_DEF       5
#define EQ_SLASH_DEF      6
#define EQ_CRUSH_DEF      7
#define EQ_MAGIC_DEF      8
#define EQ_RANGED_DEF     9
#define EQ_STR            10
#define EQ_RANGED_STR     11
#define EQ_MAGIC_DMG      12
#define EQ_PRAYER         13

// Tick delays per attack style (projectile travel time).
#define HIT_DELAY_MELEE   0
#define HIT_DELAY_RANGED  1
#define HIT_DELAY_MAGIC   2

typedef struct {
    int attack_roll;
    int defence_roll;
    float hit_chance;
    int max_hit;
} RcCombatCalc;

// Player vs NPC — accuracy + damage calculation per RSMod formulas.
// `npc_def_id` = index into g_npc_defs[] (use rc_npc_def_find to resolve).
RcCombatCalc rc_calc_melee(const RcPlayer *attacker, int npc_def_id);
RcCombatCalc rc_calc_ranged(const RcPlayer *attacker, int npc_def_id);
RcCombatCalc rc_calc_magic(const RcPlayer *attacker, int npc_def_id,
                           int spell_max_hit);

// NPC vs Player — picks the NPC's highest-weighted attack style from
// its attack_types bitfield.
RcCombatCalc rc_calc_npc_attack(int npc_def_id, const RcPlayer *defender);

// Hit chance: OSRS accuracy formula.
//   if att > def: 1 - (def+2) / (2*(att+1))
//   else:         att / (2*(def+1))
float rc_hit_chance(int att_roll, int def_roll);

// Roll one attack: accuracy check + uniform damage [0, max_hit].
// Returns damage (0 if miss). `rng_state` is on the world.
int rc_roll_attack(const RcCombatCalc *calc, uint32_t *rng_state);

// Queue a pending hit on the defender. `prayer_snapshot` is the
// defender's prayer state AT QUEUE TIME — protection prayers active
// now determine damage when the hit resolves, even if the defender
// turns them off mid-flight (OSRS "prayer flick" semantics).
void rc_queue_hit(RcPendingHit *hits, int *count, int damage, int delay,
                  int style, int source_idx, uint32_t prayer_snapshot,
                  int world_tick);

// Resolve one tick of a pending-hit queue. Applies protection-prayer
// damage scaling based on the snapshot, decrements timers, returns
// total damage landed this tick. Fired hits are deactivated in-place.
// `is_player_defender` controls protection-prayer semantics:
//   true  → player is the defender (NPC hits player; full-block).
//   false → NPC is the defender (player hits NPC; 50% reduction).
int rc_resolve_pending(RcPendingHit *hits, int *count,
                       bool is_player_defender);

// Advance attack timers + fire auto-attacks when timer hits 0 and
// target is in range. Wraps calc + roll + queue.
void rc_combat_tick_player(struct RcWorld *world);
void rc_combat_tick_npc(struct RcWorld *world, RcNpc *npc);

// Resolve pending hits on the player for this tick. Fires
// RC_EVT_PLAYER_DAMAGED per landing hit (after protection-prayer
// mitigation, before hp deduction). Applies total damage to hp.
// Must be called only when RC_SUB_COMBAT is enabled.
void rc_resolve_player_hits(struct RcWorld *world);

#endif
