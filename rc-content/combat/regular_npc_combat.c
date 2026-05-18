#include "content.h"

#include "activity_mechanics.h"
#include "combat.h"
#include "combat_formula.h"
#include "combat_hit.h"
#include "items.h"
#include "monster_mechanics.h"
#include "npc.h"
#include "prayer.h"
#include "rng.h"
#include "skills.h"
#include "spells.h"
#include <string.h>

static char lower_ascii(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static bool contains_ci(const char *s, const char *needle) {
    if (!s || !needle || !needle[0]) return false;
    for (; *s; s++) {
        const char *a = s;
        const char *b = needle;
        while (*a && *b && lower_ascii(*a) == lower_ascii(*b)) {
            a++;
            b++;
        }
        if (!*b) return true;
    }
    return false;
}

static bool item_name_contains(int item_id, const char *needle) {
    const RcItemDef *def = rc_item_def_get(item_id);
    return def && contains_ci(def->name, needle);
}

static const char *equipped_name(const RcPlayer *p, int slot) {
    if (slot < 0 || slot >= RC_EQUIP_COUNT) return NULL;
    const RcItemDef *def = rc_item_def_get(p->equipment[slot].item_id);
    return def ? def->name : NULL;
}

static bool equipment_contains(const RcPlayer *p, const char *needle) {
    for (int i = 0; i < RC_EQUIP_COUNT; i++) {
        if (item_name_contains(p->equipment[i].item_id, needle)) return true;
    }
    return false;
}

static bool equipment_slot_contains(const RcPlayer *p, int slot,
                                    const char *needle) {
    const char *name = equipped_name(p, slot);
    return name && contains_ci(name, needle);
}

static bool inventory_or_equipment_contains(const RcPlayer *p,
                                            const char *needle) {
    if (equipment_contains(p, needle)) return true;
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        if (item_name_contains(p->inventory[i].item_id, needle)) return true;
    }
    return false;
}

static uint64_t monster_tags_for_def(int def_id) {
    if (def_id < 0 || def_id >= g_npc_def_count) return 0;
    int npc_id = g_npc_defs[def_id].id;
    if (npc_id < 0) return 0;
    return rc_monster_mechanics_tags_for_npc((uint32_t)npc_id);
}

static uint64_t activity_behavior_for_def(int def_id) {
    if (def_id < 0 || def_id >= g_npc_def_count) return 0;
    int npc_id = g_npc_defs[def_id].id;
    if (npc_id < 0) return 0;
    return rc_activity_mechanics_behavior_for_npc((uint32_t)npc_id);
}

static uint16_t activity_profile_for_def(int def_id) {
    if (def_id < 0 || def_id >= g_npc_def_count) return 0;
    int npc_id = g_npc_defs[def_id].id;
    if (npc_id < 0) return 0;
    return rc_activity_mechanics_profile_for_npc((uint32_t)npc_id);
}

static RcNpc *find_npc_by_uid(RcWorld *world, int uid) {
    if (!world || uid < 0) return NULL;
    for (int i = 0; i < world->npc_count; i++) {
        RcNpc *npc = &world->npcs[i];
        if (npc->active && npc->uid == uid) return npc;
    }
    return NULL;
}

static int find_npc_def_idx_exact(const char *name) {
    for (int i = 0; i < g_npc_def_count; i++) {
        if (strcmp(g_npc_defs[i].name, name) == 0) return i;
    }
    return -1;
}

static bool style_is_melee(RcCombatStyle style) {
    return style == COMBAT_MELEE_STAB ||
           style == COMBAT_MELEE_SLASH ||
           style == COMBAT_MELEE_CRUSH;
}

static bool has_restricted_damage_gear(const RcPlayer *p) {
    const char *weapon = equipped_name(p, EQUIP_WEAPON);
    if (style_is_melee(p->combat_style)) {
        return weapon && (contains_ci(weapon, "leaf-bladed") ||
                          contains_ci(weapon, "leaf bladed"));
    }
    if (p->combat_style == COMBAT_RANGED) {
        const char *ammo = equipped_name(p, EQUIP_AMMO);
        return (ammo && contains_ci(ammo, "broad")) ||
               (weapon && contains_ci(weapon, "broad"));
    }
    if (p->combat_style == COMBAT_MAGIC) {
        return weapon && (contains_ci(weapon, "slayer's staff") ||
                          contains_ci(weapon, "slayer staff"));
    }
    return false;
}

static bool has_halberd(const RcPlayer *p) {
    return equipment_slot_contains(p, EQUIP_WEAPON, "halberd");
}

static bool activity_profile_blocks_player_damage(const RcPlayer *p,
                                                  uint16_t profile) {
    return profile == RC_ACTIVITY_PROFILE_DAWN &&
           style_is_melee(p->combat_style) && !has_halberd(p);
}

static bool has_finisher_item(const RcPlayer *p) {
    if ((p->slayer_unlocks & RC_SLAYER_UNLOCK_AUTO_FINISHER) != 0) {
        return true;
    }
    return inventory_or_equipment_contains(p, "rock hammer") ||
           inventory_or_equipment_contains(p, "granite hammer") ||
           inventory_or_equipment_contains(p, "bag of salt") ||
           inventory_or_equipment_contains(p, "fungicide") ||
           inventory_or_equipment_contains(p, "ice cooler");
}

static bool is_breath_shield_id(int id) {
    switch (id) {
        case 1540:
        case 2890:
        case 9731:
        case 11283:
        case 21633:
        case 21634:
        case 22002:
        case 22278:
            return true;
        default:
            return false;
    }
}

static bool has_breath_mitigation(const RcPlayer *p) {
    int shield_id = p->equipment[EQUIP_SHIELD].item_id;
    if (is_breath_shield_id(shield_id)) return true;
    const char *shield = equipped_name(p, EQUIP_SHIELD);
    return shield && (
        contains_ci(shield, "anti-dragon") ||
        contains_ci(shield, "dragonfire") ||
        contains_ci(shield, "ancient wyvern") ||
        contains_ci(shield, "wyvern shield") ||
        contains_ci(shield, "elemental shield") ||
        contains_ci(shield, "mind shield"));
}

static bool has_status_immunity(const RcPlayer *p, uint64_t tags) {
    if ((tags & RC_MONSTER_TAG_VENOM) &&
            equipment_contains(p, "serpentine")) {
        return true;
    }
    if ((tags & RC_MONSTER_TAG_POISON) &&
            (equipment_contains(p, "serpentine") ||
             equipment_contains(p, "poison ivy"))) {
        return true;
    }
    return false;
}

static bool has_slayer_special_protection(const RcPlayer *p, uint64_t tags) {
    if ((tags & RC_MONSTER_TAG_MIRROR_SHIELD_REQUIRED) != 0) {
        return equipment_slot_contains(p, EQUIP_SHIELD, "mirror shield") ||
               equipment_slot_contains(p, EQUIP_SHIELD, "v's shield");
    }
    if ((tags & RC_MONSTER_TAG_NOSE_PEG_REQUIRED) != 0) {
        return equipment_contains(p, "nose peg") ||
               equipment_contains(p, "slayer helmet");
    }
    if ((tags & RC_MONSTER_TAG_EARMUFFS_REQUIRED) != 0) {
        return equipment_contains(p, "earmuff") ||
               equipment_contains(p, "slayer helmet");
    }
    if ((tags & RC_MONSTER_TAG_FACE_MASK_REQUIRED) != 0) {
        return equipment_contains(p, "face mask") ||
               equipment_contains(p, "facemask") ||
               equipment_contains(p, "slayer helmet");
    }
    if ((tags & RC_MONSTER_TAG_WITCHWOOD_ICON_REQUIRED) != 0) {
        return equipment_contains(p, "witchwood icon") ||
               equipment_contains(p, "slayer helmet");
    }
    return false;
}

static void drain_skill(RcPlayer *p, int idx, int amount) {
    p->skills.boosted_level[idx] -= amount;
    if (p->skills.boosted_level[idx] < 0) {
        p->skills.boosted_level[idx] = 0;
    }
}

static void drain_player_combat_stats(RcPlayer *p, int amount) {
    static const int skills[] = {
        SKILL_ATTACK, SKILL_STRENGTH, SKILL_DEFENCE,
        SKILL_RANGED, SKILL_MAGIC,
    };
    for (unsigned i = 0; i < sizeof(skills) / sizeof(skills[0]); i++) {
        drain_skill(p, skills[i], amount);
    }
}

static int percent_at_least_one(int value, int pct) {
    int out = (value * pct) / 100;
    return out > 0 ? out : 1;
}

static bool one_in_four(RcWorld *world) {
    return rc_rng_range(&world->rng_state, 3) == 0;
}

static void heal_npc(RcNpc *npc, int amount) {
    if (!npc || amount <= 0 || npc->def_id < 0 ||
            npc->def_id >= g_npc_def_count) {
        return;
    }
    int cap = g_npc_defs[npc->def_id].hitpoints;
    if (cap <= 0) return;
    npc->current_hp += amount;
    if (npc->current_hp > cap) npc->current_hp = cap;
}

static int regular_player_damage(const RcWorld *world, const RcNpc *target,
                                 int damage) {
    if (!world || !target || damage <= 0) return damage;
    uint64_t tags = monster_tags_for_def(target->def_id);
    uint64_t activity = activity_behavior_for_def(target->def_id);
    uint16_t profile = activity_profile_for_def(target->def_id);
    const RcPlayer *p = &world->player;
    uint64_t gate = RC_MONSTER_TAG_WEAPON_DAMAGE_GATE |
                    RC_MONSTER_TAG_AMMO_DAMAGE_GATE |
                    RC_MONSTER_TAG_SPELL_DAMAGE_GATE;
    if ((tags & gate) != 0 && !has_restricted_damage_gear(p)) return 0;
    if ((activity & RC_ACTIVITY_BEHAVIOR_DAMAGE_GATE) != 0 &&
            activity_profile_blocks_player_damage(p, profile)) {
        return 0;
    }
    if ((tags & RC_MONSTER_TAG_FINISHER_ITEM) != 0 &&
            target->current_hp - damage <= 0 &&
            !has_finisher_item(p)) {
        return target->current_hp > 1 ? target->current_hp - 1 : 0;
    }
    return damage;
}

static int regular_npc_attack_damage(RcWorld *world, RcNpc *attacker,
                                     int damage) {
    if (!world || !attacker || damage <= 0) return damage;
    uint64_t tags = monster_tags_for_def(attacker->def_id);
    uint64_t activity = activity_behavior_for_def(attacker->def_id);
    uint16_t profile = activity_profile_for_def(attacker->def_id);
    uint64_t breath = RC_MONSTER_TAG_DRAGONFIRE | RC_MONSTER_TAG_ICY_BREATH;
    if ((((tags & breath) != 0 &&
          (tags & RC_MONSTER_TAG_EQUIPMENT_MITIGATION) != 0) ||
         (activity & RC_ACTIVITY_BEHAVIOR_DRAGONFIRE) != 0) &&
            has_breath_mitigation(&world->player)) {
        damage /= 4;
    }
    if (profile == RC_ACTIVITY_PROFILE_SHELLBANE_GRYPHON) {
        bool shield = equipment_slot_contains(&world->player, EQUIP_SHIELD,
                                              "tortugan shield");
        if (shield && (world->player.active_prayers & PRAYER_PROTECT_MELEE)) {
            return 0;
        }
        if (!shield) damage += 10;
    }
    if ((tags & RC_MONSTER_TAG_EQUIPMENT_REQUIRED) != 0 &&
            !has_slayer_special_protection(&world->player, tags)) {
        if ((tags & RC_MONSTER_TAG_STAT_DRAIN) != 0) {
            drain_player_combat_stats(&world->player, 1);
        }
        if ((tags & RC_MONSTER_TAG_SPECIAL_DAMAGE_PREVENTION) != 0) {
            damage += 2;
        }
    }
    return damage;
}

static void regular_on_npc_hit_player(RcWorld *world,
                                      const RcPendingHit *hit, int damage) {
    if (!world || !hit || damage <= 0 || hit->source_idx < 0) return;
    RcNpc *source = find_npc_by_uid(world, hit->source_idx);
    if (!source) return;
    uint64_t tags = monster_tags_for_def(source->def_id);
    uint64_t activity = activity_behavior_for_def(source->def_id);
    uint16_t profile = activity_profile_for_def(source->def_id);
    RcPlayer *p = &world->player;

    bool handled_heal = false;
    if (profile == RC_ACTIVITY_PROFILE_BARROWS_GUTHAN) {
        if (one_in_four(world)) heal_npc(source, damage);
        handled_heal = true;
    } else if (profile == RC_ACTIVITY_PROFILE_REVENANT_MALEDICTUS) {
        if ((source->attack_count % 3) == 2) heal_npc(source, damage / 2);
        handled_heal = true;
    } else if (profile == RC_ACTIVITY_PROFILE_BLOOD_MOON) {
        int hit_idx = source->attack_count % 3;
        int amount = hit_idx == 0 ? damage * 3 : damage * hit_idx;
        if (hit_idx == 0 && amount < 30) amount = 30;
        if (hit_idx == 0 && amount > 50) amount = 50;
        heal_npc(source, amount);
        handled_heal = true;
    }
    if (!handled_heal &&
            ((tags & RC_MONSTER_TAG_HEALING) != 0 ||
             (activity & RC_ACTIVITY_BEHAVIOR_HEAL_ON_HIT) != 0)) {
        heal_npc(source, damage / 4);
    }

    uint64_t status_tags = tags;
    if ((activity & RC_ACTIVITY_BEHAVIOR_POISON) != 0) {
        status_tags |= RC_MONSTER_TAG_POISON;
    }
    if ((activity & RC_ACTIVITY_BEHAVIOR_VENOM) != 0) {
        status_tags |= RC_MONSTER_TAG_VENOM;
    }
    if (((status_tags & (RC_MONSTER_TAG_POISON | RC_MONSTER_TAG_VENOM)) != 0) &&
            !has_status_immunity(p, status_tags)) {
        bool venom = (status_tags & RC_MONSTER_TAG_VENOM) != 0;
        if (venom) {
            if (p->venom_damage < 6) p->venom_damage = 6;
            if (p->venom_tick_counter <= 0) p->venom_tick_counter = 30;
        } else if (p->poison_damage < 2) {
            p->poison_damage = 2;
            p->poison_tick_counter = 30;
        }
    }

    bool handled_prayer = false;
    if (profile == RC_ACTIVITY_PROFILE_ARAXXOR &&
            (activity & RC_ACTIVITY_BEHAVIOR_PRAYER_DRAIN) != 0) {
        if (hit->attack_style == COMBAT_MAGIC &&
                p->current_prayer_points > 0) {
            p->current_prayer_points -=
                percent_at_least_one(p->current_prayer_points, 9);
            if (p->current_prayer_points < 0) p->current_prayer_points = 0;
        }
        handled_prayer = true;
    }
    if (!handled_prayer &&
            (activity & RC_ACTIVITY_BEHAVIOR_PRAYER_DRAIN) != 0 &&
            p->current_prayer_points > 0) {
        p->current_prayer_points -= damage > 5 ? 5 : damage;
        if (p->current_prayer_points < 0) p->current_prayer_points = 0;
    }

    bool handled_drain = false;
    switch (profile) {
        case RC_ACTIVITY_PROFILE_BARROWS_AHRIM:
            if (hit->attack_style == COMBAT_MAGIC && one_in_four(world)) {
                drain_skill(p, SKILL_STRENGTH, 5);
            }
            handled_drain = true;
            break;
        case RC_ACTIVITY_PROFILE_BARROWS_KARIL:
            if (hit->attack_style == COMBAT_RANGED && one_in_four(world)) {
                drain_skill(p, SKILL_AGILITY,
                            percent_at_least_one(
                                p->skills.boosted_level[SKILL_AGILITY], 20));
            }
            handled_drain = true;
            break;
        case RC_ACTIVITY_PROFILE_BARROWS_TORAG:
            if (style_is_melee((RcCombatStyle)hit->attack_style) &&
                    one_in_four(world)) {
                p->run_energy -= 2000;
                if (p->run_energy < 0) p->run_energy = 0;
            }
            handled_drain = true;
            break;
        case RC_ACTIVITY_PROFILE_ARAXXOR:
            if (hit->attack_style == COMBAT_RANGED) {
                drain_skill(p, SKILL_DEFENCE,
                            percent_at_least_one(
                                p->skills.boosted_level[SKILL_DEFENCE], 9));
            }
            handled_drain = true;
            break;
        default:
            break;
    }
    if (!handled_drain &&
            (activity & RC_ACTIVITY_BEHAVIOR_STAT_DRAIN) != 0) {
        drain_player_combat_stats(p, 1);
    }
    if ((tags & RC_MONSTER_TAG_DISEASE) != 0) {
        if (p->disease_tick_counter <= 0) p->disease_tick_counter = 30;
        drain_player_combat_stats(p, 1);
    }

    bool rev_freeze = profile == RC_ACTIVITY_PROFILE_REVENANT_MALEDICTUS &&
                      (source->attack_count % 3) == 1;
    if (profile != RC_ACTIVITY_PROFILE_REVENANT_MALEDICTUS &&
            ((tags & RC_MONSTER_TAG_TELEBLOCK) != 0 ||
             (activity & RC_ACTIVITY_BEHAVIOR_TELEBLOCK) != 0) &&
            p->teleblock_timer < 500) {
        p->teleblock_timer = 500;
    }
    if ((rev_freeze ||
         (profile != RC_ACTIVITY_PROFILE_REVENANT_MALEDICTUS &&
          ((tags & RC_MONSTER_TAG_FREEZE) != 0 ||
           (activity & RC_ACTIVITY_BEHAVIOR_FREEZE) != 0))) &&
            p->freeze_timer < 10) {
        p->freeze_timer = 10;
    }
}

static RcCombatStyle regular_select_npc_style(RcWorld *world, RcNpc *npc,
                                              const RcPlayer *player,
                                              RcCombatStyle default_style) {
    if (!world || !npc || !player) return default_style;
    uint16_t profile = activity_profile_for_def(npc->def_id);
    if (profile != RC_ACTIVITY_PROFILE_TZTOK_JAD) return default_style;
    int dx = npc->x > player->x ? npc->x - player->x : player->x - npc->x;
    int dy = npc->y > player->y ? npc->y - player->y : player->y - npc->y;
    if ((dx > dy ? dx : dy) <= 1) return COMBAT_MELEE_CRUSH;
    return rc_rng_range(&world->rng_state, 1) == 0
           ? COMBAT_RANGED : COMBAT_MAGIC;
}

static int regular_npc_attack_range(const RcWorld *world, const RcNpc *npc,
                                    RcCombatStyle style, int default_range) {
    (void)world;
    if (!npc) return default_range;
    if (activity_profile_for_def(npc->def_id) == RC_ACTIVITY_PROFILE_TZTOK_JAD) {
        return 15;
    }
    return default_range;
}

static int regular_modify_npc_roll_damage(RcWorld *world, RcNpc *npc,
                                          RcCombatStyle style, int damage) {
    (void)world;
    (void)style;
    if (!npc || npc->def_id < 0 || npc->def_id >= g_npc_def_count) {
        return damage;
    }
    const RcNpcDef *def = &g_npc_defs[npc->def_id];
    uint16_t profile = activity_profile_for_def(npc->def_id);
    if (profile == RC_ACTIVITY_PROFILE_BARROWS_DHAROK &&
            damage > 0 && def->hitpoints > 0 &&
            npc->current_hp < def->hitpoints) {
        int missing = def->hitpoints - npc->current_hp;
        damage += (damage * missing) / def->hitpoints;
    }
    return damage;
}

static int activity_area_damage(uint16_t profile) {
    switch (profile) {
        case RC_ACTIVITY_PROFILE_ARAXXOR: return 18;
        case RC_ACTIVITY_PROFILE_REVENANT_MALEDICTUS: return 25;
        case RC_ACTIVITY_PROFILE_BRUTUS: return 19;
        case RC_ACTIVITY_PROFILE_DEMONIC_BRUTUS: return 56;
        default: return 12;
    }
}

static bool activity_area_due(uint16_t profile, int attack_count) {
    switch (profile) {
        case RC_ACTIVITY_PROFILE_TZTOK_JAD:
            return false;
        case RC_ACTIVITY_PROFILE_REVENANT_MALEDICTUS:
            return (attack_count % 3) == 0;
        case RC_ACTIVITY_PROFILE_BLOOD_MOON:
        case RC_ACTIVITY_PROFILE_BLUE_MOON:
        case RC_ACTIVITY_PROFILE_ECLIPSE_MOON:
            return (attack_count % 6) == 0;
        case RC_ACTIVITY_PROFILE_BRUTUS:
            return (attack_count % 5) == 0;
        case RC_ACTIVITY_PROFILE_DEMONIC_BRUTUS:
            return (attack_count % 3) == 0;
        default:
            return (attack_count % 5) == 0;
    }
}

static void regular_after_npc_swing(RcWorld *world, RcNpc *npc,
                                    RcCombatStyle style) {
    (void)style;
    if (!world || !npc) return;
    uint64_t tags = monster_tags_for_def(npc->def_id);
    uint64_t shaman = RC_MONSTER_TAG_JUMP_ATTACK |
                      RC_MONSTER_TAG_AREA_ATTACK |
                      RC_MONSTER_TAG_MINION_SPAWN;
    RcPlayer *p = &world->player;
    if ((tags & shaman) != 0 && (npc->attack_count % 5) == 0) {
        if ((tags & RC_MONSTER_TAG_JUMP_ATTACK) != 0) {
            npc->prev_x = npc->x;
            npc->prev_y = npc->y;
            npc->x += npc->x >= p->x ? 2 : -2;
            npc->y += npc->y >= p->y ? 2 : -2;
        }
        if ((tags & RC_MONSTER_TAG_AREA_ATTACK) != 0) {
            rc_queue_hit(p->pending_hits, &p->num_pending_hits,
                         15, 2, COMBAT_RANGED, npc->uid,
                         p->active_prayers, world->tick);
        }
        if ((tags & RC_MONSTER_TAG_MINION_SPAWN) != 0) {
            int spawn_def = find_npc_def_idx_exact("Spawn");
            if (spawn_def >= 0) {
                rc_npc_spawn(world, spawn_def, npc->x + 1, npc->y,
                             npc->plane);
            }
        }
    }

    uint64_t activity = activity_behavior_for_def(npc->def_id);
    uint16_t profile = activity_profile_for_def(npc->def_id);
    if ((activity & RC_ACTIVITY_BEHAVIOR_AREA_PRESSURE) != 0 &&
            activity_area_due(profile, npc->attack_count)) {
        rc_queue_hit(p->pending_hits, &p->num_pending_hits,
                     activity_area_damage(profile), 2, COMBAT_RANGED,
                     npc->uid, p->active_prayers, world->tick);
    }
}

static int regular_modify_npc_attack_speed(RcWorld *world, RcNpc *npc,
                                           int default_speed) {
    (void)world;
    if (!npc || npc->def_id < 0 || npc->def_id >= g_npc_def_count) {
        return default_speed;
    }
    const RcNpcDef *def = &g_npc_defs[npc->def_id];
    uint64_t activity = activity_behavior_for_def(npc->def_id);
    uint16_t profile = activity_profile_for_def(npc->def_id);
    if ((activity & RC_ACTIVITY_BEHAVIOR_ENRAGE) == 0) return default_speed;
    if (profile == RC_ACTIVITY_PROFILE_ARAXXOR) {
        return npc->current_hp < 255 && default_speed > 4 ? 4 : default_speed;
    }
    if (profile == RC_ACTIVITY_PROFILE_BARROWS_DHAROK) return default_speed;
    if (def->hitpoints > 0 &&
            npc->current_hp * 100 <= def->hitpoints * 25 &&
            default_speed > 2) {
        return default_speed - 2;
    }
    return default_speed;
}

static int regular_modify_incoming_after_protection(RcWorld *world,
                                                    const RcPendingHit *hit,
                                                    int damage) {
    if (!world || !hit || hit->source_idx < 0 || damage != 0) return damage;
    RcNpc *source = find_npc_by_uid(world, hit->source_idx);
    if (!source) return damage;
    uint64_t activity = activity_behavior_for_def(source->def_id);
    uint16_t profile = activity_profile_for_def(source->def_id);
    if ((activity & RC_ACTIVITY_BEHAVIOR_PROTECTION_PIERCE) == 0) {
        return damage;
    }
    if (profile == RC_ACTIVITY_PROFILE_BARROWS_VERAC) {
        return one_in_four(world) ? hit->damage : 0;
    }
    if (profile == RC_ACTIVITY_PROFILE_DUSK) return hit->damage / 5;
    if (profile == RC_ACTIVITY_PROFILE_BRUTUS) {
        return hit->attack_style == COMBAT_RANGED ? hit->damage : damage;
    }
    if (profile == RC_ACTIVITY_PROFILE_DEMONIC_BRUTUS) {
        return style_is_melee((RcCombatStyle)hit->attack_style)
               ? (hit->damage > 4 ? 4 : hit->damage)
               : hit->damage;
    }
    if (profile != RC_ACTIVITY_PROFILE_TZTOK_JAD) return hit->damage / 2;
    return damage;
}

enum {
    RC_SPEC_FLAG_MIN_HIT = 1u << 0,
    RC_SPEC_FLAG_CAP = 1u << 1,
    RC_SPEC_FLAG_CONSUME_EXTRA_AMMO = 1u << 2,
    RC_SPEC_FLAG_SGS_RESTORE = 1u << 3,
    RC_SPEC_FLAG_VOIDWAKER_ROLL = 1u << 4,
};

typedef struct {
    int item_id;
    int cost;
    int damage_pct;
    int min_hit;
    int max_cap;
    uint8_t flags;
} RcPlayerSpecialDef;

static const RcPlayerSpecialDef g_player_specials[] = {
    {1215, 2500, 115, 0, 0, 0},      // Dragon dagger
    {1231, 2500, 115, 0, 0, 0},
    {5680, 2500, 115, 0, 0, 0},
    {5698, 2500, 115, 0, 0, 0},
    {1305, 2500, 125, 0, 0, 0},      // Dragon longsword
    {4153, 6000, 100, 0, 0, 0},      // Granite maul
    {12848, 6000, 100, 0, 0, 0},
    {24225, 5000, 100, 0, 0, 0},
    {24227, 5000, 100, 0, 0, 0},
    {11235, 5500, 130, 5, 0,
     RC_SPEC_FLAG_MIN_HIT | RC_SPEC_FLAG_CONSUME_EXTRA_AMMO},
    {12765, 5500, 130, 5, 0,
     RC_SPEC_FLAG_MIN_HIT | RC_SPEC_FLAG_CONSUME_EXTRA_AMMO},
    {12766, 5500, 130, 5, 0,
     RC_SPEC_FLAG_MIN_HIT | RC_SPEC_FLAG_CONSUME_EXTRA_AMMO},
    {12767, 5500, 130, 5, 0,
     RC_SPEC_FLAG_MIN_HIT | RC_SPEC_FLAG_CONSUME_EXTRA_AMMO},
    {12768, 5500, 130, 5, 0,
     RC_SPEC_FLAG_MIN_HIT | RC_SPEC_FLAG_CONSUME_EXTRA_AMMO},
    {20408, 5500, 130, 5, 0,
     RC_SPEC_FLAG_MIN_HIT | RC_SPEC_FLAG_CONSUME_EXTRA_AMMO},
    {27853, 5500, 130, 7, 0,
     RC_SPEC_FLAG_MIN_HIT | RC_SPEC_FLAG_CONSUME_EXTRA_AMMO},
    {11802, 5000, 137, 0, 0, 0},     // Armadyl godsword
    {11804, 5000, 121, 0, 0, 0},     // Bandos godsword
    {11806, 5000, 110, 0, 0, RC_SPEC_FLAG_SGS_RESTORE},
    {11808, 5000, 110, 0, 0, 0},     // Zamorak godsword
    {20368, 5000, 137, 0, 0, 0},
    {20370, 5000, 121, 0, 0, 0},
    {20372, 5000, 110, 0, 0, RC_SPEC_FLAG_SGS_RESTORE},
    {20374, 5000, 110, 0, 0, 0},
    {26233, 5000, 110, 0, 0, 0},     // Ancient godsword
    {12926, 5000, 150, 0, 0, 0},     // Toxic blowpipe
    {28688, 5000, 150, 0, 0, 0},
    {13265, 2500, 85, 0, 0, 0},      // Abyssal dagger
    {13267, 2500, 85, 0, 0, 0},
    {13269, 2500, 85, 0, 0, 0},
    {13271, 2500, 85, 0, 0, 0},
    {13652, 5000, 150, 0, 0, 0},     // Dragon claws, first-pass
    {20784, 5000, 150, 0, 0, 0},
    {28039, 5000, 150, 0, 0, 0},
    {28534, 5000, 150, 0, 0, 0},
    {13576, 5000, 150, 0, 0, 0},     // Dragon warhammer
    {26710, 5000, 150, 0, 0, 0},
    {19478, 6500, 125, 0, 0, 0},     // Light ballista
    {19481, 6500, 125, 0, 0, 0},     // Heavy ballista
    {26712, 6500, 125, 0, 0, 0},
    {21902, 6000, 120, 0, 0, 0},     // Dragon crossbow
    {11785, 5000, 100, 0, 0, 0},     // Armadyl crossbow
    {26374, 7500, 100, 0, 0, 0},     // Zaryte crossbow
    {27690, 5000, 100, 0, 0, RC_SPEC_FLAG_VOIDWAKER_ROLL},
    {27869, 5000, 100, 0, 0, RC_SPEC_FLAG_VOIDWAKER_ROLL},
};

static const RcPlayerSpecialDef *player_special_def(int item_id) {
    for (unsigned i = 0;
            i < sizeof(g_player_specials) / sizeof(g_player_specials[0]);
            i++) {
        if (g_player_specials[i].item_id == item_id)
            return &g_player_specials[i];
    }
    return NULL;
}

static bool is_dragon_arrow_id(int item_id) {
    return item_id == 11212;
}

static int regular_player_special_energy_cost(const RcWorld *world,
                                              const RcPlayer *player,
                                              const RcNpc *target,
                                              int weapon_id) {
    (void)world;
    (void)target;
    const RcPlayerSpecialDef *def = player_special_def(weapon_id);
    if (!def) return 0;
    if ((def->flags & RC_SPEC_FLAG_CONSUME_EXTRA_AMMO) != 0) {
        int qty = player ? player->equipment[EQUIP_AMMO].quantity : 0;
        if (qty < 2) return 0;
    }
    return def->cost;
}

static void consume_extra_ammo_for_special(RcWorld *world) {
    if (!world) return;
    RcInvSlot *ammo = &world->player.equipment[EQUIP_AMMO];
    if (ammo->item_id < 0 || ammo->quantity <= 0) return;
    ammo->quantity--;
    if (ammo->quantity <= 0) {
        ammo->item_id = -1;
        ammo->quantity = 0;
    }
}

static void restore_saradomin_godsword(RcWorld *world, int damage) {
    if (!world || damage <= 0) return;
    RcPlayer *p = &world->player;
    int hp = (damage + 1) / 2;
    if (hp < 10) hp = 10;
    p->current_hp += hp;
    if (p->current_hp > p->max_hp) p->current_hp = p->max_hp;
    int prayer = (damage + 3) / 4;
    if (prayer < 5) prayer = 5;
    int cap = p->skills.base_level[SKILL_PRAYER];
    if (cap <= 0) cap = 99;
    p->current_prayer_points += prayer;
    if (p->current_prayer_points > cap) p->current_prayer_points = cap;
}

static int regular_modify_player_special_damage(RcWorld *world,
                                                const RcPlayer *player,
                                                const RcNpc *target,
                                                int weapon_id,
                                                RcCombatStyle style,
                                                int damage,
                                                int max_hit) {
    (void)target;
    (void)style;
    const RcPlayerSpecialDef *def = player_special_def(weapon_id);
    if (!def) return damage;
    int out = damage;
    if ((def->flags & RC_SPEC_FLAG_VOIDWAKER_ROLL) != 0) {
        int low = (max_hit + 1) / 2;
        int high = (max_hit * 3) / 2;
        if (target && target->force_player_max_hit) {
            out = high;
        } else if (high > low && world) {
            out = low + rc_rng_range(&world->rng_state, high - low);
        } else {
            out = low;
        }
    } else if (out > 0 && def->damage_pct != 100) {
        out = (out * def->damage_pct) / 100;
    }
    if ((def->flags & RC_SPEC_FLAG_MIN_HIT) != 0 && out > 0) {
        int min_hit = def->min_hit;
        if (player && is_dragon_arrow_id(player->equipment[EQUIP_AMMO].item_id)) {
            min_hit = min_hit < 8 ? 8 : min_hit;
            if (def->item_id != 27853) out = (damage * 150) / 100;
            if (out > 48) out = 48;
        }
        if (out < min_hit) out = min_hit;
    }
    if ((def->flags & RC_SPEC_FLAG_CAP) != 0 && def->max_cap > 0 &&
            out > def->max_cap) {
        out = def->max_cap;
    }
    if ((def->flags & RC_SPEC_FLAG_CONSUME_EXTRA_AMMO) != 0)
        consume_extra_ammo_for_special(world);
    if ((def->flags & RC_SPEC_FLAG_SGS_RESTORE) != 0)
        restore_saradomin_godsword(world, out);
    return out;
}

enum {
    RUNE_AIR = 556,
    RUNE_WATER = 555,
    RUNE_EARTH = 557,
    RUNE_FIRE = 554,
    RUNE_NATURE = 561,
    RUNE_MIST = 4695,
    RUNE_DUST = 4696,
    RUNE_SMOKE = 4697,
    RUNE_STEAM = 4694,
    RUNE_MUD = 4698,
    RUNE_LAVA = 4699,
    RUNE_SUNFIRE = 28929,
};

enum {
    RUNE_MASK_AIR    = 1u << 0,
    RUNE_MASK_WATER  = 1u << 1,
    RUNE_MASK_EARTH  = 1u << 2,
    RUNE_MASK_FIRE   = 1u << 3,
    RUNE_MASK_NATURE = 1u << 4,
};

typedef struct {
    int combo_id;
    int rune1;
    int rune2;
} RcRuneCombo;

typedef struct {
    int item_id;
    int remaining;
} RcRuneReq;

static const RcRuneCombo g_rune_combos[] = {
    {RUNE_MIST, RUNE_AIR, RUNE_WATER},
    {RUNE_DUST, RUNE_AIR, RUNE_EARTH},
    {RUNE_MUD, RUNE_WATER, RUNE_EARTH},
    {RUNE_SMOKE, RUNE_AIR, RUNE_FIRE},
    {RUNE_STEAM, RUNE_WATER, RUNE_FIRE},
    {RUNE_LAVA, RUNE_EARTH, RUNE_FIRE},
};

static uint32_t rune_mask_for_item(int item_id) {
    switch (item_id) {
        case RUNE_AIR: return RUNE_MASK_AIR;
        case RUNE_WATER: return RUNE_MASK_WATER;
        case RUNE_EARTH: return RUNE_MASK_EARTH;
        case RUNE_FIRE: return RUNE_MASK_FIRE;
        case RUNE_NATURE: return RUNE_MASK_NATURE;
        default: return 0;
    }
}

static uint32_t unlimited_rune_mask_for_item(int item_id) {
    const RcItemDef *def = rc_item_def_get(item_id);
    if (!def || !def->name[0]) return 0;
    const char *name = def->name;
    uint32_t mask = 0;
    if (contains_ci(name, "devil's element")) {
        return RUNE_MASK_AIR | RUNE_MASK_WATER |
               RUNE_MASK_EARTH | RUNE_MASK_FIRE;
    }
    if (contains_ci(name, "kodai wand")) mask |= RUNE_MASK_WATER;
    if (contains_ci(name, "tome of water")) mask |= RUNE_MASK_WATER;
    if (contains_ci(name, "tome of earth")) mask |= RUNE_MASK_EARTH;
    if (contains_ci(name, "tome of fire")) mask |= RUNE_MASK_FIRE;
    if (contains_ci(name, "bryophyta")) mask |= RUNE_MASK_NATURE;

    bool staff = contains_ci(name, "staff") || contains_ci(name, "wand");
    if (!staff) return mask;
    if (contains_ci(name, "mist")) mask |= RUNE_MASK_AIR | RUNE_MASK_WATER;
    if (contains_ci(name, "dust")) mask |= RUNE_MASK_AIR | RUNE_MASK_EARTH;
    if (contains_ci(name, "mud")) mask |= RUNE_MASK_WATER | RUNE_MASK_EARTH;
    if (contains_ci(name, "smoke")) mask |= RUNE_MASK_AIR | RUNE_MASK_FIRE;
    if (contains_ci(name, "steam")) mask |= RUNE_MASK_WATER | RUNE_MASK_FIRE;
    if (contains_ci(name, "lava")) mask |= RUNE_MASK_EARTH | RUNE_MASK_FIRE;
    if (contains_ci(name, "staff of air") ||
            contains_ci(name, "air battlestaff") ||
            contains_ci(name, "mystic air staff")) {
        mask |= RUNE_MASK_AIR;
    }
    if (contains_ci(name, "staff of water") ||
            contains_ci(name, "water battlestaff") ||
            contains_ci(name, "mystic water staff")) {
        mask |= RUNE_MASK_WATER;
    }
    if (contains_ci(name, "staff of earth") ||
            contains_ci(name, "earth battlestaff") ||
            contains_ci(name, "mystic earth staff")) {
        mask |= RUNE_MASK_EARTH;
    }
    if (contains_ci(name, "staff of fire") ||
            contains_ci(name, "fire battlestaff") ||
            contains_ci(name, "mystic fire staff")) {
        mask |= RUNE_MASK_FIRE;
    }
    return mask;
}

static bool has_unlimited_rune_source(const RcPlayer *p, int rune_id) {
    if (!p) return false;
    uint32_t rune = rune_mask_for_item(rune_id);
    if (!rune) return false;
    uint32_t worn = unlimited_rune_mask_for_item(p->equipment[EQUIP_WEAPON].item_id)
                  | unlimited_rune_mask_for_item(p->equipment[EQUIP_SHIELD].item_id);
    return (worn & rune) != 0;
}

static int pouch_quantity(const RcPlayer *p, int item_id) {
    int total = 0;
    if (!p || item_id < 0) return 0;
    for (int i = 0; i < 4; i++) {
        if (p->rune_pouch[i].item_id == item_id &&
                p->rune_pouch[i].quantity > 0) {
            total += p->rune_pouch[i].quantity;
        }
    }
    return total;
}

static int local_inventory_quantity(const RcInvSlot *inv, int item_id) {
    if (!inv || item_id < 0) return 0;
    int total = 0;
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        if (inv[i].item_id == item_id && inv[i].quantity > 0)
            total += inv[i].quantity;
    }
    return total;
}

static int resource_quantity(const RcPlayer *p, int item_id) {
    return local_inventory_quantity(p ? p->inventory : NULL, item_id) +
           pouch_quantity(p, item_id);
}

static int rune_substitute_quantity(const RcPlayer *p, int rune_id) {
    if (rune_id == RUNE_FIRE)
        return resource_quantity(p, RUNE_SUNFIRE);
    return 0;
}

static int rune_resource_quantity(const RcPlayer *p, int rune_id) {
    return resource_quantity(p, rune_id) + rune_substitute_quantity(p, rune_id);
}

static int remove_from_pouch(RcPlayer *p, int item_id, int quantity) {
    if (!p || item_id < 0 || quantity <= 0) return 0;
    int removed_total = 0;
    for (int i = 0; i < 4 && quantity > 0; i++) {
        RcInvSlot *slot = &p->rune_pouch[i];
        if (slot->item_id != item_id || slot->quantity <= 0) continue;
        int removed = quantity < slot->quantity ? quantity : slot->quantity;
        slot->quantity -= removed;
        quantity -= removed;
        removed_total += removed;
        if (slot->quantity <= 0) {
            slot->item_id = -1;
            slot->quantity = 0;
        }
    }
    return removed_total;
}

static int consume_resource(RcPlayer *p, int item_id, int quantity) {
    if (!p || item_id < 0 || quantity <= 0) return 0;
    int left = quantity;
    for (int slot = 0; slot < RC_INVENTORY_SIZE && left > 0; slot++) {
        if (p->inventory[slot].item_id != item_id) continue;
        left -= rc_inv_remove_quantity(p->inventory, slot, left);
    }
    if (left > 0)
        left -= remove_from_pouch(p, item_id, left);
    return quantity - left;
}

static int consume_rune_resource(RcPlayer *p, int rune_id, int quantity) {
    if (!p || quantity <= 0) return 0;
    int left = quantity;
    left -= consume_resource(p, rune_id, left);
    if (left > 0 && rune_id == RUNE_FIRE)
        left -= consume_resource(p, RUNE_SUNFIRE, left);
    return quantity - left;
}

static int rune_req_index(RcRuneReq *reqs, int count, int item_id) {
    for (int i = 0; i < count; i++) {
        if (reqs[i].item_id == item_id)
            return i;
    }
    return -1;
}

static bool validate_or_consume_spell_runes(RcPlayer *p,
                                            const RcSpellDef *spell,
                                            bool consume) {
    if (!p || !spell) return false;
    RcRuneReq reqs[RC_SPELL_MAX_RUNES];
    int req_count = 0;
    for (int i = 0; i < spell->rune_count && i < RC_SPELL_MAX_RUNES; i++) {
        int item_id = (int)spell->runes[i].item_id;
        int qty = (int)spell->runes[i].qty;
        if (item_id < 0 || qty <= 0) continue;
        int idx = rune_req_index(reqs, req_count, item_id);
        if (idx >= 0) {
            reqs[idx].remaining += qty;
        } else {
            reqs[req_count++] = (RcRuneReq){item_id, qty};
        }
    }

    for (unsigned i = 0; i < sizeof(g_rune_combos) / sizeof(g_rune_combos[0]); i++) {
        const RcRuneCombo *combo = &g_rune_combos[i];
        int r1 = rune_req_index(reqs, req_count, combo->rune1);
        int r2 = rune_req_index(reqs, req_count, combo->rune2);
        if (r1 < 0 || r2 < 0 || reqs[r1].remaining <= 0 ||
                reqs[r2].remaining <= 0) {
            continue;
        }
        if (has_unlimited_rune_source(p, combo->rune1) ||
                has_unlimited_rune_source(p, combo->rune2)) {
            continue;
        }
        int need = reqs[r1].remaining < reqs[r2].remaining
                 ? reqs[r1].remaining : reqs[r2].remaining;
        if (resource_quantity(p, combo->combo_id) < need) continue;
        if (consume)
            consume_resource(p, combo->combo_id, need);
        reqs[r1].remaining -= need;
        reqs[r2].remaining -= need;
    }

    bool search_combos = false;
    for (int i = 0; i < req_count; i++) {
        if (reqs[i].remaining <= 0) continue;
        if (has_unlimited_rune_source(p, reqs[i].item_id)) {
            reqs[i].remaining = 0;
            continue;
        }
        if (rune_resource_quantity(p, reqs[i].item_id) >= reqs[i].remaining) {
            if (consume)
                consume_rune_resource(p, reqs[i].item_id, reqs[i].remaining);
            reqs[i].remaining = 0;
            continue;
        }
        search_combos = true;
    }

    if (search_combos) {
        for (unsigned i = 0; i < sizeof(g_rune_combos) / sizeof(g_rune_combos[0]); i++) {
            const RcRuneCombo *combo = &g_rune_combos[i];
            int r1 = rune_req_index(reqs, req_count, combo->rune1);
            int r2 = rune_req_index(reqs, req_count, combo->rune2);
            int rem1 = r1 >= 0 ? reqs[r1].remaining : 0;
            int rem2 = r2 >= 0 ? reqs[r2].remaining : 0;
            int need = rem1 > rem2 ? rem1 : rem2;
            if (need <= 0 || resource_quantity(p, combo->combo_id) < need)
                continue;
            if (consume)
                consume_resource(p, combo->combo_id, need);
            if (r1 >= 0) reqs[r1].remaining -= need;
            if (r2 >= 0) reqs[r2].remaining -= need;
        }
    }

    for (int i = 0; i < req_count; i++) {
        if (reqs[i].remaining > 0)
            return false;
    }
    return true;
}

static int regular_player_has_spell_runes(const RcWorld *world,
                                          const RcPlayer *player,
                                          const RcSpellDef *spell) {
    (void)world;
    return validate_or_consume_spell_runes((RcPlayer *)player, spell, false);
}

static int regular_player_consume_spell_runes(RcWorld *world,
                                              RcPlayer *player,
                                              const RcSpellDef *spell) {
    (void)world;
    if (!validate_or_consume_spell_runes(player, spell, false))
        return 0;
    int ok = validate_or_consume_spell_runes(player, spell, true);
    if (ok) rc_recalc_bonuses(player);
    return ok;
}

void rc_content_combat_register(struct RcWorld *world) {
    static const RcCombatContentHooks hooks = {
        .apply_player_damage = regular_player_damage,
        .apply_npc_attack_damage = regular_npc_attack_damage,
        .on_npc_hit_player = regular_on_npc_hit_player,
        .select_npc_style = regular_select_npc_style,
        .npc_attack_range = regular_npc_attack_range,
        .modify_npc_roll_damage = regular_modify_npc_roll_damage,
        .after_npc_swing = regular_after_npc_swing,
        .modify_npc_attack_speed = regular_modify_npc_attack_speed,
        .modify_incoming_damage_after_protection =
            regular_modify_incoming_after_protection,
        .player_special_energy_cost = regular_player_special_energy_cost,
        .modify_player_special_damage = regular_modify_player_special_damage,
        .player_has_spell_runes = regular_player_has_spell_runes,
        .player_consume_spell_runes = regular_player_consume_spell_runes,
    };
    rc_combat_register_content_hooks(world, &hooks);
}
