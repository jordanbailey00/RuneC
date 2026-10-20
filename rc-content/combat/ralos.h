#ifndef RC_CONTENT_RALOS_H
#define RC_CONTENT_RALOS_H

#include "combat.h"

int rc_content_ralos_hits(RcWorld *world, const RcNpc *target,
                          const RcCombatCalc *calc, bool special,
                          RcPendingHit *hits, int capacity, const char **failure);
int rc_content_ralos_consume_charge(RcWorld *world, int weapon_id);
int rc_content_ralos_register(RcWorld *world);

#endif
