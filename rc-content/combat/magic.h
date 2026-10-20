#ifndef RC_CONTENT_MAGIC_H
#define RC_CONTENT_MAGIC_H

#include "combat.h"
#include "spells.h"

int rc_content_prepare_magic(RcWorld *world, const RcNpc *target,
                              const RcSpellDef *spell, RcCombatCalc *calc,
                              int *speed, const char **failure);
int rc_content_can_autocast(const RcPlayer *player, const RcSpellDef *spell);
int rc_content_player_hit_delay(const RcWorld *world, const RcNpc *target,
                                const RcSpellDef *spell, int hit_index);
int rc_content_magic_consume_charge(RcWorld *world, int weapon_id);
uint32_t rc_content_magic_charge_capacity(int item_id);
int rc_content_has_spell_runes(const RcWorld *world, const RcPlayer *player,
                               const RcSpellDef *spell);
int rc_content_consume_spell_runes(RcWorld *world, RcPlayer *player,
                                   const RcSpellDef *spell);
int rc_content_runes_register(RcWorld *world);
void rc_content_magic_hit(RcWorld *world, RcNpc *target,
                          const RcPendingHit *hit, int damage);

#endif
