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

#define RC_SPECIAL_ENERGY_MAX       10000
#define RC_SPECIAL_RECOVER_AMOUNT   1000
#define RC_SPECIAL_RECOVER_TICKS    50

#define RC_SLAYER_UNLOCK_AUTO_FINISHER 1u

typedef struct {
    int attack_roll;
    int defence_roll;
    float hit_chance;
    int max_hit;
} RcCombatCalc;

typedef struct {
    int damage;
    int max_hit;
    int style;
    int source_uid;
    int timer;
    uint8_t hit_type;
    uint8_t flags;
} RcCombatHitView;

typedef struct {
    int selected_style_idx;
    int weapon_category;
    int attack_type;
    int combat_class;
    RcCombatStyle style;
    RcAttackStance stance;
    int xp_mask;
    int auto_retaliate;
    int special_pending;
    int special_energy;
    int attack_range;
    int attack_speed;
    int attack_cooldown;
    RcCombatTargetRef target;
    int target_hp_current;
    int target_hp_max;
    int target_recent_hit_count;
    RcCombatHitView target_recent_hits[4];
    int player_hp_current;
    int player_hp_max;
    int player_recent_hit_count;
    RcCombatHitView player_recent_hits[4];
} RcCombatViewState;

// Player vs NPC — accuracy + damage calculation per RSMod formulas.
// `npc_def_id` = index into g_npc_defs[] (use rc_npc_def_find to resolve).
RcCombatCalc rc_calc_melee(const RcPlayer *attacker, int npc_def_id);
RcCombatCalc rc_calc_ranged(const RcPlayer *attacker, int npc_def_id);
RcCombatCalc rc_calc_magic(const RcPlayer *attacker, int npc_def_id,
                           int spell_max_hit);

// Refresh selected combat style/stance/XP routing from equipped item
// data plus the player's selected style index.
void rc_refresh_player_combat_style(RcPlayer *player);
int  rc_player_weapon_can_autocast(const RcPlayer *player);
void rc_player_set_attack_style(struct RcWorld *world, int style_idx);
int  rc_player_attack_speed(const RcPlayer *player);
int  rc_player_attack_range(const RcPlayer *player);
void rc_award_player_combat_xp(struct RcWorld *world, int damage);

// Combat Phase 1 actor-state API. These calls mirror legacy combat
// fields during migration; later phases will make RcCombatActorState
// the primary runtime source of truth.
void rc_combat_init_player_state(RcPlayer *player);
void rc_combat_init_npc_state(RcNpc *npc);
int  rc_combat_start_player_vs_npc(struct RcWorld *world, int player_uid,
                                   int npc_uid);
int  rc_combat_start_npc_vs_player(struct RcWorld *world, int npc_uid,
                                   int player_uid);
void rc_combat_stop_actor(struct RcWorld *world, RcCombatActorRef actor,
                          int reason);
void rc_combat_tick_world(struct RcWorld *world);
void rc_combat_set_player_style(struct RcWorld *world, int style_idx);
void rc_combat_toggle_auto_retaliate(struct RcWorld *world);
void rc_combat_toggle_special(struct RcWorld *world);
int  rc_combat_actor_has_target(const RcCombatActorState *state);
void rc_combat_set_multi_combat(struct RcWorld *world, bool enabled);
int  rc_combat_is_multi_combat(const struct RcWorld *world);
void rc_combat_register_content_hooks(struct RcWorld *world,
                                      const RcCombatContentHooks *hooks);
int  rc_combat_actor_attacker_count(const RcCombatActorState *state);
int  rc_combat_actor_is_under_attack(const RcCombatActorState *state);
void rc_combat_actor_register_attacker(RcCombatActorState *target,
                                       RcCombatActorRef attacker);
void rc_combat_tick_actor_threat(RcCombatActorState *state);
int  rc_combat_get_player_view(const struct RcWorld *world,
                               RcCombatViewState *out);
const RcCombatAttackEvent *rc_combat_attack_events(
    const struct RcWorld *world, int *count);
void rc_combat_clear_attack_events(struct RcWorld *world);

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

// Regular-NPC mechanic consumers. These are deterministic damage-rule
// hooks backed by regular_npc_mechanics.bin tags.
int rc_combat_apply_regular_npc_player_damage_rules(
    const struct RcWorld *world, const RcNpc *target, int damage);
int rc_combat_apply_regular_npc_attack_rules(
    struct RcWorld *world, RcNpc *attacker, int damage);

// Tick player-side combat statuses applied by regular NPC mechanics.
void rc_combat_tick_player_status(struct RcWorld *world);

// Queue a pending hit on the defender. `prayer_snapshot` is the
// defender's prayer state AT QUEUE TIME — protection prayers active
// now determine damage when the hit resolves, even if the defender
// turns them off mid-flight (OSRS "prayer flick" semantics).
void rc_queue_hit(RcPendingHit *hits, int *count, int damage, int delay,
                  int style, int source_idx, uint32_t prayer_snapshot,
                  int world_tick);
void rc_queue_hit_flags(RcPendingHit *hits, int *count, int damage, int delay,
                        int style, int source_idx, uint32_t prayer_snapshot,
                        int world_tick, uint8_t flags);

// Resolve one tick of a pending-hit queue. Applies protection-prayer
// damage scaling based on the snapshot, decrements timers, returns
// total damage landed this tick. Fired hits are deactivated in-place.
// `is_player_defender` controls protection-prayer semantics:
//   true  → player is the defender (NPC hits player; full-block).
//   false → NPC is the defender (player hits NPC; 50% reduction).
int rc_resolve_pending(RcPendingHit *hits, int *count,
                       bool is_player_defender);

// Phase-0 compatibility wrappers around the legacy combat loop.
// The new combat-state engine will replace these internals phase by
// phase while preserving public callers during migration.
void rc_combat_tick_player(struct RcWorld *world);
void rc_combat_tick_npc(struct RcWorld *world, RcNpc *npc);

// Resolve pending hits on the player for this tick. Fires
// RC_EVT_PLAYER_DAMAGED per landing hit (after protection-prayer
// mitigation, before hp deduction). Applies total damage to hp.
// Must be called only when RC_SUB_COMBAT is enabled.
void rc_resolve_player_hits(struct RcWorld *world);

#endif
