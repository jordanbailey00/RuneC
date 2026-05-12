#ifndef RC_COMBAT_FORMULA_H
#define RC_COMBAT_FORMULA_H

#include "combat.h"

#define RC_HIT_CHANCE_SCALE 10000

// Phase-0 extraction boundary for reusable combat math/style helpers.
// These functions are still consumed by the legacy combat loop until
// the new combat-state engine replaces that loop in later phases.
int rc_hit_chance_scaled(int att_roll, int def_roll);
int rc_player_effective_attack_level(const RcPlayer *player);
int rc_player_effective_strength_level(const RcPlayer *player);
int rc_player_effective_defence_level(const RcPlayer *player);
int rc_player_effective_magic_defence_level(const RcPlayer *player);
int rc_player_effective_ranged_attack_level(const RcPlayer *player);
int rc_player_effective_ranged_strength_level(const RcPlayer *player);
int rc_player_effective_magic_attack_level(const RcPlayer *player);
int rc_player_offensive_roll(const RcPlayer *player, RcCombatStyle style);
int rc_player_defensive_roll(const RcPlayer *player, RcCombatStyle style);
int rc_player_max_hit_melee(const RcPlayer *player);
int rc_player_max_hit_ranged(const RcPlayer *player);
int rc_player_max_hit_magic(const RcPlayer *player, int spell_max_hit);
int rc_npc_offensive_roll(int npc_def_id, RcCombatStyle style);
int rc_npc_defensive_roll(int npc_def_id, RcCombatStyle style);
RcCombatStyle rc_combat_npc_preferred_style(int attack_types);
RcCombatCalc rc_calc_npc_attack_style(int npc_def_id,
                                      const RcPlayer *defender,
                                      RcCombatStyle style);

#endif
