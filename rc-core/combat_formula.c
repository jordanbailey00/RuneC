#include "combat_formula.h"

#include "items.h"
#include "npc.h"
#include "prayer.h"
#include "rng.h"
#include "spells.h"
#include <stddef.h>

// ---- Hit chance (OSRS DPS formula) ------------------------------------

float rc_hit_chance(int att_roll, int def_roll) {
    if (att_roll > def_roll) {
        return 1.0f - ((float)(def_roll + 2) / (2.0f * (att_roll + 1)));
    }
    return (float)att_roll / (2.0f * (def_roll + 1));
}

int rc_hit_chance_scaled(int att_roll, int def_roll) {
    if (att_roll <= 0 || def_roll < 0) return 0;
    if (att_roll > def_roll) {
        int denom = 2 * (att_roll + 1);
        int blocked = ((def_roll + 2) * RC_HIT_CHANCE_SCALE) / denom;
        int chance = RC_HIT_CHANCE_SCALE - blocked;
        if (chance < 0) return 0;
        if (chance > RC_HIT_CHANCE_SCALE) return RC_HIT_CHANCE_SCALE;
        return chance;
    }
    int denom = 2 * (def_roll + 1);
    int chance = (att_roll * RC_HIT_CHANCE_SCALE) / denom;
    if (chance < 0) return 0;
    if (chance > RC_HIT_CHANCE_SCALE) return RC_HIT_CHANCE_SCALE;
    return chance;
}

// ---- Player weapon/style helpers --------------------------------------

static const RcItemDef *equipped_weapon_def(const RcPlayer *p) {
    if (!p) return NULL;
    return rc_item_def_get(p->equipment[EQUIP_WEAPON].item_id);
}

enum {
    RC_WEAPON_TYPE_UNARMED = 0,
    RC_WEAPON_TYPE_2H_SWORD = 1,
    RC_WEAPON_TYPE_AXE = 2,
    RC_WEAPON_TYPE_BLUNT = 4,
    RC_WEAPON_TYPE_BLUDGEON = 5,
    RC_WEAPON_TYPE_BULWARK = 6,
    RC_WEAPON_TYPE_CHINCHOMPAS = 7,
    RC_WEAPON_TYPE_CLAW = 8,
    RC_WEAPON_TYPE_CROSSBOW = 9,
    RC_WEAPON_TYPE_WHIP = 10,
    RC_WEAPON_TYPE_FIXED_DEVICE = 11,
    RC_WEAPON_TYPE_GUN = 12,
    RC_WEAPON_TYPE_PICKAXE = 13,
    RC_WEAPON_TYPE_POLEARM = 14,
    RC_WEAPON_TYPE_POLESTAFF = 15,
    RC_WEAPON_TYPE_POWERED_STAFF = 16,
    RC_WEAPON_TYPE_SCYTHE = 17,
    RC_WEAPON_TYPE_SLASH_SWORD = 18,
    RC_WEAPON_TYPE_SPEAR = 19,
    RC_WEAPON_TYPE_SPIKED = 20,
    RC_WEAPON_TYPE_STAB_SWORD = 21,
    RC_WEAPON_TYPE_STAFF = 22,
    RC_WEAPON_TYPE_THROWN = 23,
    RC_WEAPON_TYPE_TWO_HANDED_SWORD = 24,
    RC_WEAPON_TYPE_BOW = 25,
    RC_WEAPON_TYPE_SALAMANDER = 26,
    RC_WEAPON_TYPE_MULTI_STYLE = 27,
    RC_WEAPON_TYPE_POWERED_WAND = 28,
    RC_WEAPON_TYPE_BLADED_STAFF = 29,
};

static RcCombatStyle best_melee_style_from_bonuses(const RcPlayer *p,
                                                   const RcItemDef *weapon) {
    int stab = weapon ? weapon->attack_stab : p->equipment_bonuses[EQ_STAB_ATK];
    int slash = weapon ? weapon->attack_slash
                       : p->equipment_bonuses[EQ_SLASH_ATK];
    int crush = weapon ? weapon->attack_crush
                       : p->equipment_bonuses[EQ_CRUSH_ATK];
    if (stab > slash && stab > crush) return COMBAT_MELEE_STAB;
    if (slash > crush) return COMBAT_MELEE_SLASH;
    return COMBAT_MELEE_CRUSH;
}

static int weapon_type_for_style(const RcItemDef *weapon) {
    if (!weapon || !weapon->equipable_weapon) return RC_WEAPON_TYPE_UNARMED;
    return weapon->weapon_type;
}

static int weapon_type_is_ranged(int weapon_type) {
    switch (weapon_type) {
        case RC_WEAPON_TYPE_CHINCHOMPAS:
        case RC_WEAPON_TYPE_CROSSBOW:
        case RC_WEAPON_TYPE_GUN:
        case RC_WEAPON_TYPE_THROWN:
        case RC_WEAPON_TYPE_BOW:
            return 1;
        default:
            return 0;
    }
}

static int weapon_type_is_magic(int weapon_type) {
    return weapon_type == RC_WEAPON_TYPE_FIXED_DEVICE ||
           weapon_type == RC_WEAPON_TYPE_POWERED_STAFF ||
           weapon_type == RC_WEAPON_TYPE_POWERED_WAND;
}

static int weapon_type_can_autocast(int weapon_type) {
    return weapon_type == RC_WEAPON_TYPE_STAFF ||
           weapon_type == RC_WEAPON_TYPE_POLESTAFF ||
           weapon_type == RC_WEAPON_TYPE_POWERED_STAFF ||
           weapon_type == RC_WEAPON_TYPE_POWERED_WAND ||
           weapon_type == RC_WEAPON_TYPE_FIXED_DEVICE;
}

static RcCombatAttackType attack_type_from_style(RcCombatStyle style) {
    switch (style) {
        case COMBAT_MELEE_STAB:  return RC_ATTACK_TYPE_STAB;
        case COMBAT_MELEE_SLASH: return RC_ATTACK_TYPE_SLASH;
        case COMBAT_MELEE_CRUSH: return RC_ATTACK_TYPE_CRUSH;
        case COMBAT_RANGED:      return RC_ATTACK_TYPE_RANGED;
        case COMBAT_MAGIC:       return RC_ATTACK_TYPE_MAGIC;
        default:                 return RC_ATTACK_TYPE_NONE;
    }
}

static RcCombatClass combat_class_from_style(RcCombatStyle style) {
    switch (style) {
        case COMBAT_MELEE_STAB:
        case COMBAT_MELEE_SLASH:
        case COMBAT_MELEE_CRUSH:
            return RC_COMBAT_CLASS_MELEE;
        case COMBAT_RANGED:
            return RC_COMBAT_CLASS_RANGED;
        case COMBAT_MAGIC:
            return RC_COMBAT_CLASS_MAGIC;
        default:
            return RC_COMBAT_CLASS_NONE;
    }
}

static void set_player_style_state(RcPlayer *p, RcCombatStyle style,
                                   RcAttackStance stance, int xp_mask,
                                   int weapon_type) {
    p->combat_style = style;
    p->attack_stance = stance;
    p->combat_xp_mask = xp_mask;
    p->combat.selected_style_idx = p->attack_style_idx;
    p->combat.style = style;
    p->combat.stance = stance;
    p->combat.xp_mask = xp_mask;
    p->combat.weapon_category = weapon_type;
    p->combat.attack_type = attack_type_from_style(style);
    p->combat.combat_class = combat_class_from_style(style);
}

static void set_magic_style_state(RcPlayer *p, int weapon_type,
                                  int defensive) {
    if (defensive) {
        set_player_style_state(p, COMBAT_MAGIC,
                               RC_ATTACK_STANCE_DEFENSIVE_CAST,
                               RC_COMBAT_XP_MAGIC | RC_COMBAT_XP_DEFENCE,
                               weapon_type);
    } else {
        set_player_style_state(p, COMBAT_MAGIC,
                               RC_ATTACK_STANCE_CAST,
                               RC_COMBAT_XP_MAGIC,
                               weapon_type);
    }
}

static void set_magic_style(RcPlayer *p, int weapon_type) {
    set_magic_style_state(p, weapon_type, p->attack_style_idx == 3);
}

static int player_has_active_autocast(const RcPlayer *p, int weapon_type) {
    if (!p || p->autocast_spell < 0 ||
            !weapon_type_can_autocast(weapon_type)) {
        return 0;
    }
    const RcSpellDef *spell = rc_spell_def_get(p->autocast_spell);
    return spell && spell->loaded &&
           spell->book == p->current_spellbook &&
           spell->type == RC_SPELL_TYPE_COMBAT &&
           spell->max_hit > 0;
}

int rc_player_weapon_can_autocast(const RcPlayer *p) {
    if (!p) return 0;
    return weapon_type_can_autocast(
        weapon_type_for_style(equipped_weapon_def(p)));
}

static void set_ranged_style(RcPlayer *p, int weapon_type) {
    if (p->attack_style_idx == 1) {
        set_player_style_state(p, COMBAT_RANGED, RC_ATTACK_STANCE_RAPID,
                               RC_COMBAT_XP_RANGED, weapon_type);
    } else if (p->attack_style_idx >= 2) {
        set_player_style_state(p, COMBAT_RANGED, RC_ATTACK_STANCE_LONGRANGE,
                               RC_COMBAT_XP_RANGED | RC_COMBAT_XP_DEFENCE,
                               weapon_type);
    } else {
        set_player_style_state(p, COMBAT_RANGED, RC_ATTACK_STANCE_ACCURATE,
                               RC_COMBAT_XP_RANGED, weapon_type);
    }
}

static void set_basic_melee_style(RcPlayer *p, int weapon_type,
                                  RcCombatStyle style) {
    switch (p->attack_style_idx) {
        case 1:
            set_player_style_state(p, style, RC_ATTACK_STANCE_AGGRESSIVE,
                                   RC_COMBAT_XP_STRENGTH, weapon_type);
            break;
        case 2:
            set_player_style_state(p, style, RC_ATTACK_STANCE_CONTROLLED,
                                   RC_COMBAT_XP_ATTACK |
                                   RC_COMBAT_XP_STRENGTH |
                                   RC_COMBAT_XP_DEFENCE,
                                   weapon_type);
            break;
        case 3:
            set_player_style_state(p, style, RC_ATTACK_STANCE_DEFENSIVE,
                                   RC_COMBAT_XP_DEFENCE, weapon_type);
            break;
        default:
            set_player_style_state(p, style, RC_ATTACK_STANCE_ACCURATE,
                                   RC_COMBAT_XP_ATTACK, weapon_type);
            break;
    }
}

static void set_axe_or_2h_style(RcPlayer *p, int weapon_type) {
    if (p->attack_style_idx == 2) {
        set_player_style_state(p, COMBAT_MELEE_CRUSH,
                               RC_ATTACK_STANCE_AGGRESSIVE,
                               RC_COMBAT_XP_STRENGTH, weapon_type);
        return;
    }
    set_basic_melee_style(p, weapon_type, COMBAT_MELEE_SLASH);
}

static void set_staff_style(RcPlayer *p, int weapon_type) {
    if (p->attack_style_idx >= 2) {
        set_player_style_state(p, COMBAT_MELEE_CRUSH,
                               RC_ATTACK_STANCE_DEFENSIVE,
                               RC_COMBAT_XP_DEFENCE, weapon_type);
    } else if (p->attack_style_idx == 1) {
        set_player_style_state(p, COMBAT_MELEE_CRUSH,
                               RC_ATTACK_STANCE_AGGRESSIVE,
                               RC_COMBAT_XP_STRENGTH, weapon_type);
    } else {
        set_player_style_state(p, COMBAT_MELEE_CRUSH,
                               RC_ATTACK_STANCE_ACCURATE,
                               RC_COMBAT_XP_ATTACK, weapon_type);
    }
}

static void set_whip_style(RcPlayer *p, int weapon_type) {
    if (p->attack_style_idx == 1) {
        set_player_style_state(p, COMBAT_MELEE_SLASH,
                               RC_ATTACK_STANCE_CONTROLLED,
                               RC_COMBAT_XP_ATTACK |
                               RC_COMBAT_XP_STRENGTH |
                               RC_COMBAT_XP_DEFENCE,
                               weapon_type);
    } else if (p->attack_style_idx >= 2) {
        set_player_style_state(p, COMBAT_MELEE_SLASH,
                               RC_ATTACK_STANCE_DEFENSIVE,
                               RC_COMBAT_XP_DEFENCE, weapon_type);
    } else {
        set_player_style_state(p, COMBAT_MELEE_SLASH,
                               RC_ATTACK_STANCE_ACCURATE,
                               RC_COMBAT_XP_ATTACK, weapon_type);
    }
}

static void set_spear_style(RcPlayer *p, int weapon_type) {
    if (p->attack_style_idx == 1) {
        set_player_style_state(p, COMBAT_MELEE_SLASH,
                               RC_ATTACK_STANCE_CONTROLLED,
                               RC_COMBAT_XP_ATTACK |
                               RC_COMBAT_XP_STRENGTH |
                               RC_COMBAT_XP_DEFENCE,
                               weapon_type);
    } else if (p->attack_style_idx == 2) {
        set_player_style_state(p, COMBAT_MELEE_CRUSH,
                               RC_ATTACK_STANCE_CONTROLLED,
                               RC_COMBAT_XP_ATTACK |
                               RC_COMBAT_XP_STRENGTH |
                               RC_COMBAT_XP_DEFENCE,
                               weapon_type);
    } else if (p->attack_style_idx >= 3) {
        set_player_style_state(p, COMBAT_MELEE_STAB,
                               RC_ATTACK_STANCE_DEFENSIVE,
                               RC_COMBAT_XP_DEFENCE, weapon_type);
    } else {
        set_player_style_state(p, COMBAT_MELEE_STAB,
                               RC_ATTACK_STANCE_CONTROLLED,
                               RC_COMBAT_XP_ATTACK |
                               RC_COMBAT_XP_STRENGTH |
                               RC_COMBAT_XP_DEFENCE,
                               weapon_type);
    }
}

static void set_slash_sword_style(RcPlayer *p, int weapon_type) {
    if (p->attack_style_idx == 2) {
        set_player_style_state(p, COMBAT_MELEE_STAB,
                               RC_ATTACK_STANCE_CONTROLLED,
                               RC_COMBAT_XP_ATTACK |
                               RC_COMBAT_XP_STRENGTH |
                               RC_COMBAT_XP_DEFENCE,
                               weapon_type);
        return;
    }
    set_basic_melee_style(p, weapon_type, COMBAT_MELEE_SLASH);
}

static void set_stab_sword_style(RcPlayer *p, int weapon_type) {
    if (p->attack_style_idx == 2) {
        set_player_style_state(p, COMBAT_MELEE_SLASH,
                               RC_ATTACK_STANCE_CONTROLLED,
                               RC_COMBAT_XP_ATTACK |
                               RC_COMBAT_XP_STRENGTH |
                               RC_COMBAT_XP_DEFENCE,
                               weapon_type);
        return;
    }
    set_basic_melee_style(p, weapon_type, COMBAT_MELEE_STAB);
}

static void set_salamander_style(RcPlayer *p, int weapon_type) {
    if (p->attack_style_idx == 1) {
        set_player_style_state(p, COMBAT_RANGED, RC_ATTACK_STANCE_RAPID,
                               RC_COMBAT_XP_RANGED, weapon_type);
    } else if (p->attack_style_idx >= 2) {
        set_magic_style(p, weapon_type);
    } else {
        set_player_style_state(p, COMBAT_MELEE_SLASH,
                               RC_ATTACK_STANCE_ACCURATE,
                               RC_COMBAT_XP_ATTACK, weapon_type);
    }
}

void rc_refresh_player_combat_style(RcPlayer *p) {
    if (!p) return;
    if (p->attack_style_idx < 0) p->attack_style_idx = 0;
    if (p->attack_style_idx > 3) p->attack_style_idx = 3;

    const RcItemDef *weapon = equipped_weapon_def(p);
    int weapon_type = weapon_type_for_style(weapon);

    if (p->manual_spell_cast >= 0) {
        set_magic_style(p, weapon_type);
    } else if (player_has_active_autocast(p, weapon_type)) {
        set_magic_style_state(p, weapon_type, p->defensive_autocast);
    } else if (weapon_type_is_magic(weapon_type)) {
        set_magic_style(p, weapon_type);
    } else if (weapon_type_is_ranged(weapon_type)) {
        set_ranged_style(p, weapon_type);
    } else {
        switch (weapon_type) {
            case RC_WEAPON_TYPE_2H_SWORD:
            case RC_WEAPON_TYPE_AXE:
            case RC_WEAPON_TYPE_SCYTHE:
            case RC_WEAPON_TYPE_TWO_HANDED_SWORD:
                set_axe_or_2h_style(p, weapon_type);
                break;
            case RC_WEAPON_TYPE_WHIP:
                set_whip_style(p, weapon_type);
                break;
            case RC_WEAPON_TYPE_SPEAR:
            case RC_WEAPON_TYPE_POLEARM:
                set_spear_style(p, weapon_type);
                break;
            case RC_WEAPON_TYPE_SLASH_SWORD:
            case RC_WEAPON_TYPE_BLADED_STAFF:
                set_slash_sword_style(p, weapon_type);
                break;
            case RC_WEAPON_TYPE_STAB_SWORD:
            case RC_WEAPON_TYPE_CLAW:
            case RC_WEAPON_TYPE_PICKAXE:
                set_stab_sword_style(p, weapon_type);
                break;
            case RC_WEAPON_TYPE_BLUNT:
            case RC_WEAPON_TYPE_BLUDGEON:
            case RC_WEAPON_TYPE_BULWARK:
            case RC_WEAPON_TYPE_SPIKED:
            case RC_WEAPON_TYPE_STAFF:
            case RC_WEAPON_TYPE_POLESTAFF:
                set_staff_style(p, weapon_type);
                break;
            case RC_WEAPON_TYPE_SALAMANDER:
            case RC_WEAPON_TYPE_MULTI_STYLE:
                set_salamander_style(p, weapon_type);
                break;
            default:
                set_basic_melee_style(p, weapon_type,
                                      best_melee_style_from_bonuses(p, weapon));
                break;
        }
    }
    p->combat.attack_range = rc_player_attack_range(p);
}

void rc_player_set_attack_style(struct RcWorld *world, int style_idx) {
    if (!world) return;
    if (style_idx < 0) style_idx = 0;
    if (style_idx > 3) style_idx = 3;
    world->player.attack_style_idx = style_idx;
    rc_refresh_player_combat_style(&world->player);
}

int rc_player_attack_speed(const RcPlayer *p) {
    const RcItemDef *weapon = equipped_weapon_def(p);
    int speed = 4;
    if (weapon && weapon->attack_speed > 0) {
        speed = weapon->attack_speed;
    } else if (p && p->combat_style == COMBAT_MAGIC) {
        speed = 5;
    } else if (p && p->combat_style == COMBAT_RANGED) {
        speed = 5;
    }
    if (p && p->attack_stance == RC_ATTACK_STANCE_RAPID && speed > 1) {
        speed--;
    }
    return speed;
}

int rc_player_attack_range(const RcPlayer *p) {
    const RcItemDef *weapon = equipped_weapon_def(p);
    int range = 1;
    if (weapon && weapon->attack_range > 1) {
        range = weapon->attack_range;
    } else if (p && p->combat_style == COMBAT_MAGIC) {
        range = 10;
    } else if (p && p->combat_style == COMBAT_RANGED) {
        range = 7;
    }
    if (p && p->attack_stance == RC_ATTACK_STANCE_LONGRANGE && range > 1) {
        range += 2;
    }
    return range;
}

// ---- Effective level helpers ------------------------------------------

static int stance_attack_bonus(const RcPlayer *p) {
    if (p->attack_stance == RC_ATTACK_STANCE_ACCURATE) return 3;
    if (p->attack_stance == RC_ATTACK_STANCE_CONTROLLED) return 1;
    return 0;
}

static int stance_strength_bonus(const RcPlayer *p) {
    if (p->attack_stance == RC_ATTACK_STANCE_AGGRESSIVE) return 3;
    if (p->attack_stance == RC_ATTACK_STANCE_CONTROLLED) return 1;
    return 0;
}

static int stance_defence_bonus(const RcPlayer *p) {
    if (p->attack_stance == RC_ATTACK_STANCE_DEFENSIVE) return 3;
    if (p->attack_stance == RC_ATTACK_STANCE_LONGRANGE) return 3;
    if (p->attack_stance == RC_ATTACK_STANCE_DEFENSIVE_CAST) return 3;
    if (p->attack_stance == RC_ATTACK_STANCE_CONTROLLED) return 1;
    return 0;
}

static int stance_ranged_attack_bonus(const RcPlayer *p) {
    if (p->attack_stance == RC_ATTACK_STANCE_ACCURATE) return 3;
    if (p->attack_stance == RC_ATTACK_STANCE_LONGRANGE) return 3;
    return 0;
}

static int stance_magic_attack_bonus(const RcPlayer *p) {
    return p->attack_stance == RC_ATTACK_STANCE_DEFENSIVE_CAST ? 0 : 3;
}

int rc_player_effective_attack_level(const RcPlayer *p) {
    if (!p) return 0;
    int base = p->skills.boosted_level[SKILL_ATTACK];
    int stance = stance_attack_bonus(p);
    int prayer = rc_prayer_attack_bonus(p->active_prayers);
    return ((base + stance) * (100 + prayer)) / 100 + 8;
}

int rc_player_effective_strength_level(const RcPlayer *p) {
    if (!p) return 0;
    int base = p->skills.boosted_level[SKILL_STRENGTH];
    int stance = stance_strength_bonus(p);
    int prayer = rc_prayer_strength_bonus(p->active_prayers);
    return ((base + stance) * (100 + prayer)) / 100 + 8;
}

int rc_player_effective_defence_level(const RcPlayer *p) {
    if (!p) return 0;
    int base = p->skills.boosted_level[SKILL_DEFENCE];
    int stance = stance_defence_bonus(p);
    int prayer = rc_prayer_defence_bonus(p->active_prayers);
    return ((base + stance) * (100 + prayer)) / 100 + 8;
}

int rc_player_effective_magic_defence_level(const RcPlayer *p) {
    if (!p) return 0;
    int defence = p->skills.boosted_level[SKILL_DEFENCE];
    int magic = p->skills.boosted_level[SKILL_MAGIC];
    int base = (magic * 7 + defence * 3) / 10;
    int stance = stance_defence_bonus(p);
    int prayer = rc_prayer_defence_bonus(p->active_prayers);
    return ((base + stance) * (100 + prayer)) / 100 + 8;
}

int rc_player_effective_ranged_attack_level(const RcPlayer *p) {
    if (!p) return 0;
    int base = p->skills.boosted_level[SKILL_RANGED];
    int stance = stance_ranged_attack_bonus(p);
    int prayer = rc_prayer_ranged_attack_bonus(p->active_prayers);
    return ((base + stance) * (100 + prayer)) / 100 + 8;
}

int rc_player_effective_ranged_strength_level(const RcPlayer *p) {
    if (!p) return 0;
    int base = p->skills.boosted_level[SKILL_RANGED];
    int prayer = rc_prayer_ranged_strength_bonus(p->active_prayers);
    return (base * (100 + prayer)) / 100 + 8;
}

int rc_player_effective_magic_attack_level(const RcPlayer *p) {
    if (!p) return 0;
    int base = p->skills.boosted_level[SKILL_MAGIC];
    int stance = stance_magic_attack_bonus(p);
    int prayer = rc_prayer_magic_attack_bonus(p->active_prayers);
    return ((base + stance) * (100 + prayer)) / 100 + 8;
}

static int npc_eff(int stat) { return stat + 9; }

static int atk_bonus_idx(RcCombatStyle style) {
    switch (style) {
        case COMBAT_MELEE_STAB:  return EQ_STAB_ATK;
        case COMBAT_MELEE_SLASH: return EQ_SLASH_ATK;
        case COMBAT_MELEE_CRUSH: return EQ_CRUSH_ATK;
        case COMBAT_RANGED:      return EQ_RANGED_ATK;
        case COMBAT_MAGIC:       return EQ_MAGIC_ATK;
        default:                 return EQ_CRUSH_ATK;
    }
}

static int def_bonus_idx(RcCombatStyle style) {
    switch (style) {
        case COMBAT_MELEE_STAB:  return EQ_STAB_DEF;
        case COMBAT_MELEE_SLASH: return EQ_SLASH_DEF;
        case COMBAT_MELEE_CRUSH: return EQ_CRUSH_DEF;
        case COMBAT_RANGED:      return EQ_RANGED_DEF;
        case COMBAT_MAGIC:       return EQ_MAGIC_DEF;
        default:                 return EQ_CRUSH_DEF;
    }
}

int rc_player_offensive_roll(const RcPlayer *p, RcCombatStyle style) {
    if (!p) return 0;
    switch (style) {
        case COMBAT_MELEE_STAB:
        case COMBAT_MELEE_SLASH:
        case COMBAT_MELEE_CRUSH:
            return rc_player_effective_attack_level(p) *
                   (p->equipment_bonuses[atk_bonus_idx(style)] + 64);
        case COMBAT_RANGED:
            return rc_player_effective_ranged_attack_level(p) *
                   (p->equipment_bonuses[EQ_RANGED_ATK] + 64);
        case COMBAT_MAGIC:
            return rc_player_effective_magic_attack_level(p) *
                   (p->equipment_bonuses[EQ_MAGIC_ATK] + 64);
        default:
            return 0;
    }
}

int rc_player_defensive_roll(const RcPlayer *p, RcCombatStyle style) {
    if (!p) return 0;
    int effective = style == COMBAT_MAGIC
                  ? rc_player_effective_magic_defence_level(p)
                  : rc_player_effective_defence_level(p);
    return effective * (p->equipment_bonuses[def_bonus_idx(style)] + 64);
}

int rc_player_max_hit_melee(const RcPlayer *p) {
    if (!p) return 0;
    int eff = rc_player_effective_strength_level(p);
    int bonus = p->equipment_bonuses[EQ_STR];
    return (eff * (bonus + 64)) / 640 + 1;
}

int rc_player_max_hit_ranged(const RcPlayer *p) {
    if (!p) return 0;
    int eff = rc_player_effective_ranged_strength_level(p);
    int bonus = p->equipment_bonuses[EQ_RANGED_STR];
    return (eff * (bonus + 64)) / 640 + 1;
}

int rc_player_max_hit_magic(const RcPlayer *p, int spell_max_hit) {
    if (!p || spell_max_hit <= 0) return 0;
    int bonus = p->equipment_bonuses[EQ_MAGIC_DMG]
              + rc_prayer_magic_damage_bonus(p->active_prayers);
    return spell_max_hit + (spell_max_hit * bonus) / 100;
}

int rc_npc_offensive_roll(int npc_def_id, RcCombatStyle style) {
    if (npc_def_id < 0 || npc_def_id >= g_npc_def_count) return 0;
    const RcNpcDef *d = &g_npc_defs[npc_def_id];
    int stat = 0;
    switch (style) {
        case COMBAT_RANGED: stat = d->stats[4]; break;
        case COMBAT_MAGIC:  stat = d->stats[5]; break;
        default:            stat = d->stats[0]; break;
    }
    return npc_eff(stat) * 64;
}

int rc_npc_defensive_roll(int npc_def_id, RcCombatStyle style) {
    (void)style;
    if (npc_def_id < 0 || npc_def_id >= g_npc_def_count) return 0;
    const RcNpcDef *d = &g_npc_defs[npc_def_id];
    return npc_eff(d->stats[1]) * 64;
}

// ---- Player vs NPC calc ------------------------------------------------

RcCombatCalc rc_calc_melee(const RcPlayer *atk, int npc_def_id) {
    RcCombatCalc c = {0};
    if (npc_def_id < 0 || npc_def_id >= g_npc_def_count) return c;

    c.attack_roll = rc_player_offensive_roll(atk, atk->combat_style);
    c.defence_roll = rc_npc_defensive_roll(npc_def_id, atk->combat_style);
    c.hit_chance = rc_hit_chance(c.attack_roll, c.defence_roll);
    c.max_hit = rc_player_max_hit_melee(atk);
    return c;
}

RcCombatCalc rc_calc_ranged(const RcPlayer *atk, int npc_def_id) {
    RcCombatCalc c = {0};
    if (npc_def_id < 0 || npc_def_id >= g_npc_def_count) return c;

    c.attack_roll = rc_player_offensive_roll(atk, COMBAT_RANGED);
    c.defence_roll = rc_npc_defensive_roll(npc_def_id, COMBAT_RANGED);
    c.hit_chance = rc_hit_chance(c.attack_roll, c.defence_roll);
    c.max_hit = rc_player_max_hit_ranged(atk);
    return c;
}

RcCombatCalc rc_calc_magic(const RcPlayer *atk, int npc_def_id,
                           int spell_max_hit) {
    RcCombatCalc c = {0};
    if (npc_def_id < 0 || npc_def_id >= g_npc_def_count) return c;

    c.attack_roll = rc_player_offensive_roll(atk, COMBAT_MAGIC);
    c.defence_roll = rc_npc_defensive_roll(npc_def_id, COMBAT_MAGIC);
    c.hit_chance = rc_hit_chance(c.attack_roll, c.defence_roll);
    c.max_hit = rc_player_max_hit_magic(atk, spell_max_hit);
    return c;
}

// ---- NPC vs Player calc ------------------------------------------------

RcCombatStyle rc_combat_npc_preferred_style(int attack_types) {
    if (attack_types & 0x01) return COMBAT_MELEE_STAB;
    if (attack_types & 0x02) return COMBAT_MELEE_SLASH;
    if (attack_types & 0x04) return COMBAT_MELEE_CRUSH;
    if (attack_types & 0x10) return COMBAT_RANGED;
    if (attack_types & 0x08) return COMBAT_MAGIC;
    return COMBAT_MELEE_CRUSH;
}

RcCombatCalc rc_calc_npc_attack_style(int npc_def_id,
                                      const RcPlayer *def,
                                      RcCombatStyle style) {
    RcCombatCalc c = {0};
    if (npc_def_id < 0 || npc_def_id >= g_npc_def_count) return c;
    const RcNpcDef *d = &g_npc_defs[npc_def_id];

    c.attack_roll = rc_npc_offensive_roll(npc_def_id, style);
    c.defence_roll = rc_player_defensive_roll(def, style);
    c.hit_chance = rc_hit_chance(c.attack_roll, c.defence_roll);
    c.max_hit = d->max_hit;
    return c;
}

RcCombatCalc rc_calc_npc_attack(int npc_def_id, const RcPlayer *def) {
    if (npc_def_id < 0 || npc_def_id >= g_npc_def_count) {
        RcCombatCalc c = {0};
        return c;
    }
    RcCombatStyle style =
        rc_combat_npc_preferred_style(g_npc_defs[npc_def_id].attack_types);
    return rc_calc_npc_attack_style(npc_def_id, def, style);
}

// ---- Roll --------------------------------------------------------------

int rc_roll_attack(const RcCombatCalc *calc, uint32_t *rng_state) {
    uint32_t roll = rc_rng_next(rng_state) & 0xFFFF;
    uint32_t threshold = (uint32_t)(calc->hit_chance * 65536.0f);
    if (roll >= threshold) return 0;
    return rc_rng_range(rng_state, calc->max_hit);
}
