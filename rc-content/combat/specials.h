#ifndef RC_CONTENT_SPECIALS_H
#define RC_CONTENT_SPECIALS_H

#include "gear.h"

// Energy in hundredths of a percent, or -1 when the weapon cannot use its
// special right now (*failure says why when known).
int rc_content_special_cost(const RcWorld *world, const RcPlayer *player, const RcNpc *target,
                            int weapon_id, const char **failure);
// Fills `hits` for the special and returns their count; negative rejects.
int rc_content_special_hits(RcGearAttack *a, RcCombatCalc *calc,
                            RcPendingHit *hits, int capacity, const char **failure);
void rc_content_special_landed(RcWorld *world, RcNpc *target,
                               const RcPendingHit *hit, int damage);
// Spec-bar click: instant effects that need no target. 1 = used up the click.
int rc_content_special_activate(RcWorld *world);
int rc_content_special_attack_speed(RcWorld *world, int weapon_id, int speed);
int rc_content_special_hit_delay(int weapon_id, int distance, int hit_index);
int rc_content_shield_slam(RcWorld *world);

#endif
