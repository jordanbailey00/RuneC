#ifndef RC_COMBAT_HIT_H
#define RC_COMBAT_HIT_H

#include "combat.h"

// Phase-0 extraction boundary for delayed-hit helpers. The existing
// public queue/resolve names remain available through combat.h.
int rc_combat_hit_delay_for_style(RcCombatStyle style);
int rc_combat_apply_protection(int damage, int style, uint32_t snapshot,
                               bool is_player_defender);
int rc_combat_resolve_hit_damage(const RcPendingHit *hit,
                                 bool is_player_defender);
void rc_queue_hit_meta(RcPendingHit *hits, int *count, int damage, int delay,
                       int style, int source_idx, uint32_t prayer_snapshot,
                       int world_tick, uint8_t flags, int max_hit);
void rc_combat_actor_record_hit(RcCombatActorState *state, int damage,
                                int max_hit, int style, int source_uid,
                                uint8_t hit_type, uint8_t flags, int timer);
void rc_combat_actor_tick_recent_hits(RcCombatActorState *state);

#endif
