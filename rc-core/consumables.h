#ifndef RC_CONSUMABLES_H
#define RC_CONSUMABLES_H

#include "types.h"

typedef enum { RC_CONSUME_FOOD, RC_CONSUME_COMBO, RC_CONSUME_DRINK } RcConsumeKind;
typedef enum {
    RC_CONSUME_OK, RC_CONSUME_QUEUED, RC_CONSUME_INVALID,
    RC_CONSUME_DISABLED, RC_CONSUME_UNSUPPORTED, RC_CONSUME_BUSY,
    RC_CONSUME_STALE, RC_CONSUME_COOLDOWN, RC_CONSUME_LOW_HP,
    RC_CONSUME_ITEM_FAILURE, RC_CONSUME_BAD_DATA, RC_CONSUME_MEMBERS
} RcConsumeResult;
typedef enum { RC_STAT_NONE, RC_STAT_BOOST, RC_STAT_RESTORE, RC_STAT_DRAIN } RcConsumeStatOp;
typedef struct {
    uint8_t op, percent, flat;
} RcConsumeStat;

// Content supplies explicit identities, replacements and bounded effect parameters.
typedef struct RcConsumableDef {
    int item_id, replacement_id;
    uint8_t kind, eat_delay, attack_delay;
    uint8_t heal, heal_percent, overheal, angler;
    uint8_t delayed_heal, delayed_energy, delayed_cure;
    uint8_t energy, prayer_flat, prayer_percent;
    uint8_t cure; // 1: poison cure / venom downgrade; 2: both
    uint16_t poison_immunity, venom_immunity, stamina_ticks, divine_ticks;
    uint16_t antifire_ticks, super_antifire_ticks;
    RcConsumeStat stats[SKILL_COUNT];
} RcConsumableDef;

int rc_consumables_register(RcWorld *world, const RcConsumableDef *defs, int count);
const RcConsumableDef *rc_consumable_get(const RcWorld *world, int item_id);
int rc_consumable_action(int item_id, int option); // 0: other, 1: Eat, 2: Drink
RcConsumeResult rc_player_consume_expected(RcWorld *world, int slot,
                                         uint32_t generation, int drink);
int rc_consume_accepted(RcConsumeResult result);
const char *rc_consume_result_message(int result);
void rc_consumables_tick(RcWorld *world);
void rc_consumables_reset(RcPlayer *player);
void rc_player_apply_toxin(RcWorld *world, int damage, bool venom);
void rc_player_toxin_tick(RcWorld *world);

#endif
