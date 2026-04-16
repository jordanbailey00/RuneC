#include "prayer.h"

// Counter-based drain matching OSRS exactly.
// Drain rate per prayer, resistance = 60 + 2 * prayer_bonus.
// Counter increments by drain_rate each tick. When counter > resistance,
// subtract 1 prayer point and reset counter.
// 1-tick flicks are free: only drains if prayer was active at tick start.

void rc_prayer_drain_tick(RcPlayer *player) {
    if (player->active_prayers == 0) return;
    if (player->current_prayer_points <= 0) {
        player->active_prayers = 0;
        return;
    }

    // Sum drain rates of all active prayers
    int drain_rate = 0;
    // Protection prayers: 12 each
    if (player->active_prayers & PRAYER_PROTECT_MAGIC)  drain_rate += 12;
    if (player->active_prayers & PRAYER_PROTECT_RANGE)  drain_rate += 12;
    if (player->active_prayers & PRAYER_PROTECT_MELEE)  drain_rate += 12;
    // Overhead prayers are the most common; add more as needed
    if (player->active_prayers & PRAYER_THICK_SKIN)     drain_rate += 3;
    if (player->active_prayers & PRAYER_BURST_OF_STR)   drain_rate += 3;
    if (player->active_prayers & PRAYER_CLARITY)        drain_rate += 3;
    if (player->active_prayers & PRAYER_SHARP_EYE)      drain_rate += 3;
    if (player->active_prayers & PRAYER_MYSTIC_WILL)    drain_rate += 3;
    if (player->active_prayers & PRAYER_STEEL_SKIN)     drain_rate += 6;
    if (player->active_prayers & PRAYER_ULTIMATE_STR)   drain_rate += 6;
    if (player->active_prayers & PRAYER_INCREDIBLE_REF) drain_rate += 6;
    if (player->active_prayers & PRAYER_EAGLE_EYE)      drain_rate += 6;
    if (player->active_prayers & PRAYER_MYSTIC_MIGHT)   drain_rate += 6;
    if (player->active_prayers & PRAYER_PIETY)          drain_rate += 24;
    if (player->active_prayers & PRAYER_RIGOUR)         drain_rate += 24;
    if (player->active_prayers & PRAYER_AUGURY)         drain_rate += 24;

    if (drain_rate == 0) return;

    int prayer_bonus = player->equipment_bonuses[13]; // prayer bonus slot
    int resistance = 60 + 2 * prayer_bonus;

    player->prayer_drain_counter += drain_rate;
    while (player->prayer_drain_counter > resistance) {
        player->current_prayer_points -= 10; // 1 point = 10 tenths
        player->prayer_drain_counter -= resistance;
        if (player->current_prayer_points <= 0) {
            player->current_prayer_points = 0;
            player->active_prayers = 0;
            break;
        }
    }
}

void rc_prayer_toggle(RcPlayer *player, uint32_t prayer_bit) {
    player->active_prayers ^= prayer_bit;
}

int rc_prayer_attack_bonus(uint32_t active_prayers) {
    if (active_prayers & PRAYER_PIETY) return 20;       // +20%
    if (active_prayers & PRAYER_INCREDIBLE_REF) return 15;
    if (active_prayers & PRAYER_CLARITY) return 5;
    return 0;
}

int rc_prayer_strength_bonus(uint32_t active_prayers) {
    if (active_prayers & PRAYER_PIETY) return 23;       // +23%
    if (active_prayers & PRAYER_ULTIMATE_STR) return 15;
    if (active_prayers & PRAYER_BURST_OF_STR) return 5;
    return 0;
}

int rc_prayer_defence_bonus(uint32_t active_prayers) {
    if (active_prayers & PRAYER_PIETY) return 25;       // +25%
    if (active_prayers & PRAYER_STEEL_SKIN) return 15;
    if (active_prayers & PRAYER_THICK_SKIN) return 5;
    return 0;
}
