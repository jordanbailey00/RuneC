#include "prayer.h"
#include "io.h"

#include <stdio.h>
#include <string.h>

#define PRAY_MAGIC 0x59415250u
#define PRAY_VERSION 1u

RcPrayerDef g_rc_prayer_defs[RC_MAX_PRAYER_DEFS];
int g_rc_prayer_count = 0;

int rc_load_prayers(const char *path) {
    if (!path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, path, "version")
            || !rc_read_exact(f, &count, sizeof(count), 1, path, "count")
            || magic != PRAY_MAGIC || version != PRAY_VERSION) {
        fclose(f);
        return -1;
    }

    memset(g_rc_prayer_defs, 0, sizeof(g_rc_prayer_defs));
    int loaded = 0;
    for (uint32_t i = 0; i < count; i++) {
        RcPrayerDef row;
        memset(&row, 0, sizeof(row));
        uint8_t name_len;
        if (!rc_read_exact(f, &row.id, sizeof(row.id), 1, path, "id")
                || !rc_read_exact(f, &row.level, sizeof(row.level), 1, path, "level")
                || !rc_read_exact(f, &row.drain, sizeof(row.drain), 1, path, "drain")
                || !rc_read_exact(f, &row.flags, sizeof(row.flags), 1, path, "flags")
                || !rc_read_exact(f, &row.varbit, sizeof(row.varbit), 1, path, "varbit")
                || !rc_read_exact(f, &row.groups, sizeof(row.groups), 1, path, "groups")
                || !rc_read_exact(f, &row.attack, sizeof(row.attack), 1, path, "attack")
                || !rc_read_exact(f, &row.strength, sizeof(row.strength), 1, path, "strength")
                || !rc_read_exact(f, &row.defence, sizeof(row.defence), 1, path, "defence")
                || !rc_read_exact(f, &row.ranged_attack, sizeof(row.ranged_attack), 1,
                                  path, "ranged attack")
                || !rc_read_exact(f, &row.ranged_strength, sizeof(row.ranged_strength), 1,
                                  path, "ranged strength")
                || !rc_read_exact(f, &row.magic_attack, sizeof(row.magic_attack), 1,
                                  path, "magic attack")
                || !rc_read_exact(f, &row.magic_defence, sizeof(row.magic_defence), 1,
                                  path, "magic defence")
                || !rc_read_exact(f, &row.magic_damage, sizeof(row.magic_damage), 1,
                                  path, "magic damage")
                || !rc_read_exact(f, &name_len, sizeof(name_len), 1, path, "name len")) {
            fclose(f);
            return -1;
        }
        uint8_t keep = name_len < sizeof(row.name)
                     ? name_len : (uint8_t)sizeof(row.name) - 1;
        if (keep && !rc_read_exact(f, row.name, 1, keep, path, "name")) {
            fclose(f);
            return -1;
        }
        row.name[keep] = '\0';
        if (name_len > keep &&
                !rc_seek(f, name_len - keep, SEEK_CUR, path, "name")) {
            fclose(f);
            return -1;
        }
        if (row.id < RC_MAX_PRAYER_DEFS) {
            row.loaded = 1;
            g_rc_prayer_defs[row.id] = row;
            loaded++;
        }
    }
    fclose(f);
    g_rc_prayer_count = loaded;
    return loaded;
}

const RcPrayerDef *rc_prayer_def_get(int prayer_id) {
    if (prayer_id < 0 || prayer_id >= RC_MAX_PRAYER_DEFS) return NULL;
    return g_rc_prayer_defs[prayer_id].loaded
         ? &g_rc_prayer_defs[prayer_id] : NULL;
}

uint32_t rc_prayer_bit(int prayer_id) {
    if (prayer_id < 0 || prayer_id >= 32) return 0;
    return 1u << prayer_id;
}

void rc_prayer_drain_tick(RcPlayer *player) {
    if (player->active_prayers == 0) return;
    if (player->current_prayer_points <= 0) {
        player->active_prayers = 0;
        return;
    }

    int drain_rate = 0;
    for (int i = 0; i < RC_MAX_PRAYER_DEFS; i++) {
        uint32_t bit = rc_prayer_bit(i);
        const RcPrayerDef *def = rc_prayer_def_get(i);
        if ((player->active_prayers & bit) && def) drain_rate += def->drain;
    }

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

void rc_prayer_toggle(RcPlayer *player, int prayer_id) {
    const RcPrayerDef *def = rc_prayer_def_get(prayer_id);
    uint32_t bit = rc_prayer_bit(prayer_id);
    if (!def || bit == 0) return;
    if (player->active_prayers & bit) {
        player->active_prayers &= ~bit;
        return;
    }
    if (def->groups) {
        for (int i = 0; i < RC_MAX_PRAYER_DEFS; i++) {
            const RcPrayerDef *other = rc_prayer_def_get(i);
            if (other && (other->groups & def->groups)) {
                player->active_prayers &= ~rc_prayer_bit(i);
            }
        }
    }
    player->active_prayers |= bit;
}

int rc_prayer_attack_bonus(uint32_t active_prayers) {
    int best = 0;
    for (int i = 0; i < RC_MAX_PRAYER_DEFS; i++) {
        const RcPrayerDef *def = rc_prayer_def_get(i);
        if ((active_prayers & rc_prayer_bit(i)) && def && def->attack > best) {
            best = def->attack;
        }
    }
    return best;
}

int rc_prayer_strength_bonus(uint32_t active_prayers) {
    int best = 0;
    for (int i = 0; i < RC_MAX_PRAYER_DEFS; i++) {
        const RcPrayerDef *def = rc_prayer_def_get(i);
        if ((active_prayers & rc_prayer_bit(i)) && def && def->strength > best) {
            best = def->strength;
        }
    }
    return best;
}

int rc_prayer_defence_bonus(uint32_t active_prayers) {
    int total = 0;
    for (int i = 0; i < RC_MAX_PRAYER_DEFS; i++) {
        const RcPrayerDef *def = rc_prayer_def_get(i);
        if ((active_prayers & rc_prayer_bit(i)) && def) {
            total += def->defence;
        }
    }
    return total;
}

int rc_prayer_ranged_attack_bonus(uint32_t active_prayers) {
    int best = 0;
    for (int i = 0; i < RC_MAX_PRAYER_DEFS; i++) {
        const RcPrayerDef *def = rc_prayer_def_get(i);
        if ((active_prayers & rc_prayer_bit(i)) && def
                && def->ranged_attack > best) {
            best = def->ranged_attack;
        }
    }
    return best;
}

int rc_prayer_ranged_strength_bonus(uint32_t active_prayers) {
    int best = 0;
    for (int i = 0; i < RC_MAX_PRAYER_DEFS; i++) {
        const RcPrayerDef *def = rc_prayer_def_get(i);
        if ((active_prayers & rc_prayer_bit(i)) && def
                && def->ranged_strength > best) {
            best = def->ranged_strength;
        }
    }
    return best;
}

int rc_prayer_magic_attack_bonus(uint32_t active_prayers) {
    int best = 0;
    for (int i = 0; i < RC_MAX_PRAYER_DEFS; i++) {
        const RcPrayerDef *def = rc_prayer_def_get(i);
        if ((active_prayers & rc_prayer_bit(i)) && def
                && def->magic_attack > best) {
            best = def->magic_attack;
        }
    }
    return best;
}

int rc_prayer_magic_damage_bonus(uint32_t active_prayers) {
    int best = 0;
    for (int i = 0; i < RC_MAX_PRAYER_DEFS; i++) {
        const RcPrayerDef *def = rc_prayer_def_get(i);
        if ((active_prayers & rc_prayer_bit(i)) && def
                && def->magic_damage > best) {
            best = def->magic_damage;
        }
    }
    return best;
}
