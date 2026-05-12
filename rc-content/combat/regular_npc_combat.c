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
    };
    rc_combat_register_content_hooks(world, &hooks);
}
