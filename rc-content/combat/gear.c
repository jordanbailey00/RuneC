// What worn gear does to a player attack: the accuracy and max hit of every
// passive and set effect, in the client's order of operations. Item IDs map
// to families through gear_rows.inc, generated from content/items/gear.tsv.
#include "gear.h"

#include "ammunition.h"
#include "area_flags.h"
#include "combat_formula.h"
#include "interaction.h"
#include "items.h"
#include "npc.h"
#include "prayer.h"
#include "rng.h"
#include "combat_hit.h"
#include "magic.h"
#include "skills.h"
#include "slayer.h"
#include <string.h>


static const RcGearRow g_gear_rows[RC_GEAR_ID_LIMIT] = {
#include "gear_rows.inc"
};
static const RcGearRow g_no_gear;

static const RcNpcCombatRow g_npc_attributes[] = {
#include "../../content/combat/npc_attributes.inc"
};

static int factor(int value, int num, int den) {
    return (int)((int64_t)value * num / den);
}

const RcGearRow *rc_content_gear_row(int item_id) {
    return item_id >= 0 && item_id < RC_GEAR_ID_LIMIT ? &g_gear_rows[item_id] : &g_no_gear;
}

const RcNpcCombatRow *rc_content_npc_row(const RcWorld *world, const RcNpc *npc) {
    const RcNpcDef *def = npc ? rc_npc_def_for_npc(world, npc) : NULL;
    if (!def) return NULL;
    int low = 0, high = (int)(sizeof(g_npc_attributes) / sizeof(g_npc_attributes[0]));
    while (low < high) {
        int mid = low + (high - low) / 2;
        if (g_npc_attributes[mid].id < def->id) low = mid + 1;
        else high = mid;
    }
    return low < (int)(sizeof(g_npc_attributes) / sizeof(g_npc_attributes[0]))
            && g_npc_attributes[low].id == def->id ? &g_npc_attributes[low] : NULL;
}

uint32_t rc_content_npc_attributes(const RcWorld *world, const RcNpc *npc) {
    const RcNpcCombatRow *row = rc_content_npc_row(world, npc);
    return row ? row->attributes : 0;
}

bool rc_content_wearing(const RcPlayer *player, int family) {
    for (int slot = 0; slot < RC_EQUIP_COUNT; slot++)
        if (rc_content_gear_row(player->equipment[slot].item_id)->family == family)
            return true;
    return false;
}

void rc_content_gear_attack(RcGearAttack *a, RcWorld *world, const RcNpc *npc,
                            const RcSpellDef *spell, bool special) {
    memset(a, 0, sizeof(*a));
    RcPlayer *p = &world->player;
    a->world = world;
    a->p = p;
    a->npc = npc;
    a->spell = spell;
    a->special = special;
    a->style = p->combat_style;
    a->npc_row = rc_content_npc_row(world, npc);
    a->attrs = a->npc_row ? a->npc_row->attributes : 0;
    const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
    a->size = def && def->size > 0 ? def->size : 1;
    a->weapon_id = p->equipment[EQUIP_WEAPON].item_id;
    a->weapon = rc_content_gear_row(a->weapon_id);
    for (int slot = 0; slot < RC_EQUIP_COUNT; slot++) {
        const RcGearRow *row = rc_content_gear_row(p->equipment[slot].item_id);
        a->fam[slot] = row->family;
        a->flags |= row->flags;
    }
    a->on_task = (a->attrs & RC_NPC_ATTR_SLAYER)
        && rc_slayer_npc_on_task(world, npc ? npc->def_id : -1);
    a->in_wilderness = rc_wilderness_level_at(p->x, p->y, p->plane) > 0;
    const char *ignored = NULL;
    a->uses_ammo_slot = a->style == COMBAT_RANGED
        && rc_content_ranged_resource_slot(p, &ignored) == EQUIP_AMMO;
}

bool rc_content_special_is_ward(int weapon_id) {
    return rc_content_gear_row(weapon_id)->special == SPECIAL_POWER_OF_DEATH;
}

static bool head(const RcGearAttack *a, int family) { return a->fam[EQUIP_HEAD] == family; }
static bool neck(const RcGearAttack *a, int family) { return a->fam[EQUIP_AMULET] == family; }
static bool weapon(const RcGearAttack *a, int family) { return a->fam[EQUIP_WEAPON] == family; }
static bool attr(const RcGearAttack *a, uint32_t bits) { return (a->attrs & bits) != 0; }

static bool void_robes(const RcGearAttack *a) {
    return (a->fam[EQUIP_BODY] == GEAR_VOID_TOP || a->fam[EQUIP_BODY] == GEAR_ELITE_VOID_TOP)
        && (a->fam[EQUIP_LEGS] == GEAR_VOID_ROBE || a->fam[EQUIP_LEGS] == GEAR_ELITE_VOID_ROBE)
        && a->fam[EQUIP_GLOVES] == GEAR_VOID_GLOVES;
}

static bool elite_void_robes(const RcGearAttack *a) {
    return a->fam[EQUIP_BODY] == GEAR_ELITE_VOID_TOP && a->fam[EQUIP_LEGS] == GEAR_ELITE_VOID_ROBE
        && a->fam[EQUIP_GLOVES] == GEAR_VOID_GLOVES;
}

static bool black_mask(const RcGearAttack *a) {
    return head(a, GEAR_BLACK_MASK) || head(a, GEAR_BLACK_MASK_I)
        || head(a, GEAR_SLAYER_HELMET) || head(a, GEAR_SLAYER_HELMET_I);
}

static bool imbued_black_mask(const RcGearAttack *a) {
    return head(a, GEAR_BLACK_MASK_I) || head(a, GEAR_SLAYER_HELMET_I);
}

static bool obsidian_set(const RcGearAttack *a) {
    return head(a, GEAR_OBSIDIAN_HELMET) && a->fam[EQUIP_BODY] == GEAR_OBSIDIAN_PLATEBODY
        && a->fam[EQUIP_LEGS] == GEAR_OBSIDIAN_PLATELEGS;
}

static bool barrows_set(const RcGearAttack *a, int helm, int body, int legs, int arm) {
    return a->fam[EQUIP_HEAD] == helm && a->fam[EQUIP_BODY] == body
        && a->fam[EQUIP_LEGS] == legs && a->fam[EQUIP_WEAPON] == arm;
}

static bool dharok_set(const RcGearAttack *a) {
    return barrows_set(a, GEAR_DHAROK_HELM, GEAR_DHAROK_PLATEBODY, GEAR_DHAROK_PLATELEGS,
                       GEAR_DHAROK_GREATAXE);
}

static int crystal_pieces(const RcGearAttack *a) {
    return (head(a, GEAR_CRYSTAL_HELM) ? 1 : 0)
        + (a->fam[EQUIP_LEGS] == GEAR_CRYSTAL_LEGS ? 2 : 0)
        + (a->fam[EQUIP_BODY] == GEAR_CRYSTAL_BODY ? 3 : 0);
}

static int inquisitor_percent(const RcGearAttack *a) {
    if (a->style != COMBAT_MELEE_CRUSH) return 0;
    return (head(a, GEAR_INQUISITOR_HELM) ? 1 : 0)
        + (a->fam[EQUIP_BODY] == GEAR_INQUISITOR_HAUBERK ? 2 : 0)
        + (a->fam[EQUIP_LEGS] == GEAR_INQUISITOR_SKIRT ? 2 : 0);
}

// A fuelled revenant weapon in the Wilderness.
static bool revenant_buff(const RcGearAttack *a) {
    if (!a->in_wilderness || !a->p->equipment[EQUIP_WEAPON].state_id) return false;
    unsigned wanted = a->style == COMBAT_MAGIC ? GEARF_REVENANT_MAGIC
        : a->style == COMBAT_RANGED ? GEARF_REVENANT_RANGED : GEARF_REVENANT_MELEE;
    return (a->weapon->flags & wanted) != 0;
}

static int demonbane_vulnerability(const RcGearAttack *a) {
    const RcNpcDef *def = rc_npc_def_for_npc(a->world, a->npc);
    if (!def) return 100;
    if (!strcmp(def->name, "Duke Sucellus")) return 70;
    if (!strcmp(def->name, "Yama")) return 120;
    if (!strcmp(def->name, "Void Flare")) return 200;
    if (!strcmp(def->name, "Ice demon")) return 115;
    return 100;
}

static int add_demonbane(const RcGearAttack *a, int value, int percent) {
    int scaled = percent * demonbane_vulnerability(a) / 100;
    return value + factor(value, scaled, 100);
}

static int max_from_effective(int effective, int bonus) {
    int hit = (int)(((int64_t)effective * (bonus + 64) + 320) / 640);
    return hit > 0 ? hit : 0;
}

static int melee_attack_bonus(const RcGearAttack *a) {
    int index = a->style == COMBAT_MELEE_STAB ? EQ_STAB_ATK
        : a->style == COMBAT_MELEE_SLASH ? EQ_SLASH_ATK : EQ_CRUSH_ATK;
    int bonus = a->p->equipment_bonuses[index];
    if (weapon(a, GEAR_KERIS_AMASCUT) && !attr(a, RC_NPC_ATTR_TOA) && index == EQ_STAB_ATK)
        bonus -= 50;
    return bonus;
}

static int melee_strength_bonus(const RcGearAttack *a) {
    const int *b = a->p->equipment_bonuses;
    int bonus = b[EQ_STR];
    if (weapon(a, GEAR_KERIS_AMASCUT) && !attr(a, RC_NPC_ATTR_TOA)) bonus -= 22;
    if (a->weapon->family == GEAR_DINHS_BULWARK) {
        int extra = (b[EQ_STAB_DEF] + b[EQ_SLASH_DEF] + b[EQ_CRUSH_DEF] + b[EQ_RANGED_DEF] - 800) / 12 - 38;
        if (extra > 0) bonus += extra;
    }
    return bonus;
}

// Silver melee weapons, silver bolts and the Blisterwood stake.
static bool silver_weapon(const RcGearAttack *a) {
    if (a->style == COMBAT_RANGED)
        return weapon(a, GEAR_BLISTERWOOD_STAKE) || a->fam[EQUIP_AMMO] == GEAR_SILVER_BOLTS;
    return a->style != COMBAT_MAGIC && (a->weapon->flags & GEARF_SILVER_WEAPON);
}

static int melee_attack_roll(RcGearAttack *a) {
    int effective = rc_player_effective_attack_level(a->p);
    if (void_robes(a) && head(a, GEAR_VOID_MELEE_HELM)) effective = factor(effective, 11, 10);
    int base = effective * (melee_attack_bonus(a) + 64);
    int roll = base;
    if (neck(a, GEAR_AMULET_OF_AVARICE) && attr(a, RC_NPC_ATTR_REVENANT)) roll = factor(roll, 24, 20);
    else if ((neck(a, GEAR_SALVE_AMULET_E) || neck(a, GEAR_SALVE_AMULET_EI)) && attr(a, RC_NPC_ATTR_UNDEAD))
        roll = factor(roll, 6, 5);
    else if ((neck(a, GEAR_SALVE_AMULET) || neck(a, GEAR_SALVE_AMULET_I)) && attr(a, RC_NPC_ATTR_UNDEAD))
        roll = factor(roll, 7, 6);
    else if (black_mask(a) && a->on_task) roll = factor(roll, 7, 6);
    if ((a->weapon->flags & GEARF_TZHAAR_WEAPON) && obsidian_set(a)) roll += base / 10;
    if (revenant_buff(a)) roll = factor(roll, 3, 2);
    if (attr(a, RC_NPC_ATTR_DEMON)) {
        if (weapon(a, GEAR_ARCLIGHT) || weapon(a, GEAR_EMBERLIGHT)) roll = add_demonbane(a, roll, 70);
        if (weapon(a, GEAR_SILVERLIGHT)) roll = add_demonbane(a, roll, 60);
        if (weapon(a, GEAR_BURNING_CLAWS)) roll = add_demonbane(a, roll, 5);
    }
    if (attr(a, RC_NPC_ATTR_DRAGON)) {
        if (weapon(a, GEAR_DRAGON_HUNTER_LANCE)) roll = factor(roll, 6, 5);
        else if (weapon(a, GEAR_DRAGON_HUNTER_WAND)) roll = factor(roll, 7, 4);
    }
    if (weapon(a, GEAR_KERIS_BREACHING) && attr(a, RC_NPC_ATTR_KALPHITE)) roll = factor(roll, 133, 100);
    if ((a->weapon->flags & GEARF_KERIS_SUN) && attr(a, RC_NPC_ATTR_TOA)
            && a->npc->current_hp < a->npc->spawn_hp / 4)
        roll = factor(roll, 5, 4);
    if (attr(a, RC_NPC_ATTR_VAMPYRE)) {
        if (weapon(a, GEAR_BLISTERWOOD_FLAIL) || weapon(a, GEAR_BLISTERWOOD_SICKLE)) roll = factor(roll, 105, 100);
        if (weapon(a, GEAR_HALLOWED_FLAIL) || weapon(a, GEAR_SUNSPEAR)) roll = factor(roll, 125, 100);
        if (silver_weapon(a) && a->fam[EQUIP_RING] == GEAR_EFARITAYS_AID)
            roll = factor(roll, 23, 20);
    }
    if (weapon(a, GEAR_GRANITE_HAMMER) && attr(a, RC_NPC_ATTR_GOLEM)) roll = factor(roll, 13, 10);
    int inquisitor = inquisitor_percent(a);
    if (inquisitor) roll = factor(roll, 200 + inquisitor, 200);
    return roll;
}

static int melee_max_hit(RcGearAttack *a) {
    const RcPlayer *p = a->p;
    int effective = rc_player_effective_strength_level(p);
    int stacks = rc_content_soul_stacks(a->world);
    if (weapon(a, GEAR_SOULREAPER_AXE) && !a->special)
        effective += p->skills.boosted_level[SKILL_STRENGTH] * stacks * 6 / 100;
    if (void_robes(a) && head(a, GEAR_VOID_MELEE_HELM)) effective = factor(effective, 11, 10);
    int base = max_from_effective(effective, melee_strength_bonus(a));
    int hit = base;
    if (a->fam[EQUIP_AMMO] == GEAR_CRYSTAL_BLESSING)
        hit = factor(hit, 40 + crystal_pieces(a), 40);
    if (neck(a, GEAR_AMULET_OF_AVARICE) && attr(a, RC_NPC_ATTR_REVENANT)) hit = factor(hit, 24, 20);
    else if ((neck(a, GEAR_SALVE_AMULET_E) || neck(a, GEAR_SALVE_AMULET_EI)) && attr(a, RC_NPC_ATTR_UNDEAD))
        hit = factor(hit, 6, 5);
    else if ((neck(a, GEAR_SALVE_AMULET) || neck(a, GEAR_SALVE_AMULET_I)) && attr(a, RC_NPC_ATTR_UNDEAD))
        hit = factor(hit, 7, 6);
    else if (black_mask(a) && a->on_task) hit = factor(hit, 7, 6);
    if (attr(a, RC_NPC_ATTR_DEMON)) {
        if (weapon(a, GEAR_ARCLIGHT) || weapon(a, GEAR_EMBERLIGHT)) hit = add_demonbane(a, hit, 70);
        if (weapon(a, GEAR_BURNING_CLAWS)) hit = add_demonbane(a, hit, 5);
    }
    if ((a->weapon->flags & GEARF_TZHAAR_WEAPON) && obsidian_set(a)) hit += base / 10;
    if (attr(a, RC_NPC_ATTR_DRAGON)) {
        if (weapon(a, GEAR_DRAGON_HUNTER_LANCE)) hit = factor(hit, 6, 5);
        if (weapon(a, GEAR_DRAGON_HUNTER_WAND)) hit = factor(hit, 7, 5);
    }
    if (attr(a, RC_NPC_ATTR_KALPHITE)) {
        if (weapon(a, GEAR_KERIS_AMASCUT)) hit = factor(hit, 115, 100);
        else if (weapon(a, GEAR_KERIS) || weapon(a, GEAR_KERIS_BREACHING)) hit = factor(hit, 133, 100);
    }
    if (attr(a, RC_NPC_ATTR_GOLEM)) {
        if (weapon(a, GEAR_BARRONITE_MACE)) hit = factor(hit, 23, 20);
        if (weapon(a, GEAR_GRANITE_HAMMER)) hit = factor(hit, 13, 10);
    }
    if (revenant_buff(a)) hit = factor(hit, 3, 2);
    if (weapon(a, GEAR_SILVERLIGHT) && attr(a, RC_NPC_ATTR_DEMON)) hit = add_demonbane(a, hit, 60);
    if (weapon(a, GEAR_LEAF_BLADED_BATTLEAXE) && attr(a, RC_NPC_ATTR_LEAFY)) hit = factor(hit, 47, 40);
    if (weapon(a, GEAR_COLOSSAL_BLADE)) hit += a->size * 2 < 10 ? a->size * 2 : 10;
    if ((a->weapon->flags & GEARF_RAT_BONE_WEAPON) && attr(a, RC_NPC_ATTR_RAT)) hit += 10;
    int inquisitor = inquisitor_percent(a);
    if (inquisitor) hit = factor(hit, 200 + inquisitor, 200);
    if (weapon(a, GEAR_OSMUMTENS_FANG)) {
        int shrink = hit * 3 / 20;
        a->min_hit = shrink;
        if (!a->special) hit -= shrink;
    }
    if (weapon(a, GEAR_NOXIOUS_HALBERD)) a->min_hit = a->p->content_gear[RC_GEAR_STATE_NEXT_DAMAGE_BONUS];
    return hit;
}

void rc_content_gear_melee(RcGearAttack *a, RcCombatCalc *calc) {
    calc->attack_roll = melee_attack_roll(a);
    calc->max_hit = melee_max_hit(a);
    calc->hit_chance = rc_hit_chance(calc->attack_roll, calc->defence_roll);
}

static int tbow_scaling(int current, int magic, bool accuracy) {
    int bonus_base = accuracy ? 140 : 250, step = accuracy ? 10 : 14;
    int t2 = (3 * magic - step) / 100;
    int t3 = 3 * magic / 10 - 10 * step;
    t3 = t3 * t3 / 100;
    int bonus = bonus_base + t2 - t3;
    if (bonus < 0) bonus = 0;
    if (bonus > bonus_base) bonus = bonus_base;
    return factor(current, bonus, 100);
}

static int tbow_magic(const RcGearAttack *a) {
    const RcNpc *npc = a->npc;
    int magic = npc->stats[5];
    int bonus = npc->attack_bonus[COMBAT_MAGIC - 1];
    if (bonus > magic) magic = bonus;
    int cap = attr(a, RC_NPC_ATTR_XERICIAN) ? 350 : 250;
    return magic < cap ? magic : cap;
}

static int unused_ammo_stat(const RcGearAttack *a, bool strength) {
    if (a->uses_ammo_slot || a->p->equipment[EQUIP_AMMO].quantity <= 0) return 0;
    const RcItemDef *ammo = rc_item_def_get(a->p->equipment[EQUIP_AMMO].item_id);
    if (!ammo) return 0;
    return strength ? ammo->ranged_strength : ammo->attack_ranged;
}

// A charged Dizana's quiver with ammo the weapon fires.
static int quiver_bonus(const RcGearAttack *a, int bonus) {
    return a->fam[EQUIP_CAPE] == GEAR_DIZANAS_QUIVER && a->uses_ammo_slot ? bonus : 0;
}

static int ranged_attack_roll(const RcGearAttack *a) {
    int effective = rc_player_effective_ranged_attack_level(a->p);
    if (void_robes(a) && head(a, GEAR_VOID_RANGER_HELM)) effective = factor(effective, 11, 10);
    int roll = effective * (a->p->equipment_bonuses[EQ_RANGED_ATK] - unused_ammo_stat(a, false)
                            + quiver_bonus(a, 10) + 64);
    if (a->weapon->flags & GEARF_CRYSTAL_BOW) roll = factor(roll, 20 + crystal_pieces(a), 20);
    if (neck(a, GEAR_AMULET_OF_AVARICE) && attr(a, RC_NPC_ATTR_REVENANT)) roll = factor(roll, 24, 20);
    else if (neck(a, GEAR_SALVE_AMULET_EI) && attr(a, RC_NPC_ATTR_UNDEAD)) roll = factor(roll, 6, 5);
    else if (neck(a, GEAR_SALVE_AMULET_I) && attr(a, RC_NPC_ATTR_UNDEAD)) roll = factor(roll, 7, 6);
    else if (imbued_black_mask(a) && a->on_task) roll = factor(roll, 23, 20);
    if (weapon(a, GEAR_TWISTED_BOW)) roll = tbow_scaling(roll, tbow_magic(a), true);
    if (revenant_buff(a)) roll = factor(roll, 3, 2);
    if (weapon(a, GEAR_DRAGON_HUNTER_CROSSBOW) && attr(a, RC_NPC_ATTR_DRAGON)) roll = factor(roll, 13, 10);
    if (weapon(a, GEAR_SCORCHING_BOW) && attr(a, RC_NPC_ATTR_DEMON)) roll = add_demonbane(a, roll, 30);
    return roll;
}

static int holy_water_max_hit(const RcGearAttack *a) {
    if (!attr(a, RC_NPC_ATTR_DEMON)) return 0;
    const RcItemDef *water = rc_item_def_get(a->p->equipment[EQUIP_WEAPON].item_id);
    int hit = max_from_effective(a->p->skills.boosted_level[SKILL_RANGED] + 10,
                                 water ? water->ranged_strength : 0);
    hit = add_demonbane(a, hit, 60);
    const RcNpcDef *def = rc_npc_def_for_npc(a->world, a->npc);
    return def && !strcmp(def->name, "Nezikchened") ? hit + 5 : hit;
}

// Ogre bows hit from the visible level and the ammunition alone.
static int ogre_bow_max_hit(const RcGearAttack *a) {
    const RcItemDef *ammo = rc_item_def_get(a->p->equipment[EQUIP_AMMO].item_id);
    return max_from_effective(a->p->skills.boosted_level[SKILL_RANGED] + 10,
                              ammo ? ammo->ranged_strength : 0);
}

static int ranged_max_hit(RcGearAttack *a) {
    const RcPlayer *p = a->p;
    if (weapon(a, GEAR_HOLY_WATER)) return holy_water_max_hit(a);
    if (weapon(a, GEAR_OGRE_BOW)) return ogre_bow_max_hit(a);
    bool strength_scaled = a->weapon->ammo_kind == AMMO_ATLATL || weapon(a, GEAR_HUNTERS_SPEAR);
    int effective;
    if (strength_scaled) {
        int level = p->skills.boosted_level[SKILL_STRENGTH];
        int prayer = rc_prayer_ranged_strength_bonus(p->active_prayers);
        effective = prayer == 5 && level <= 20 ? level + 1 : level * (100 + prayer) / 100;
        effective += 8 + (p->attack_stance == RC_ATTACK_STANCE_ACCURATE ? 3 : 0);
    } else {
        effective = rc_player_effective_ranged_strength_level(p);
    }
    if (elite_void_robes(a) && head(a, GEAR_VOID_RANGER_HELM)) effective = factor(effective, 9, 8);
    else if (void_robes(a) && head(a, GEAR_VOID_RANGER_HELM)) effective = factor(effective, 11, 10);
    int bonus = strength_scaled ? p->equipment_bonuses[EQ_STR]
        : p->equipment_bonuses[EQ_RANGED_STR] - unused_ammo_stat(a, true)
          + quiver_bonus(a, 1) + rc_content_blowpipe_dart_strength(&p->equipment[EQUIP_WEAPON]);
    int hit = max_from_effective(effective, bonus);
    if (a->weapon->flags & GEARF_CRYSTAL_BOW) hit = factor(hit, 40 + crystal_pieces(a), 40);
    bool revenant = revenant_buff(a);
    bool dragonbane = weapon(a, GEAR_DRAGON_HUNTER_CROSSBOW) && attr(a, RC_NPC_ATTR_DRAGON);
    bool demonbane = weapon(a, GEAR_SCORCHING_BOW) && attr(a, RC_NPC_ATTR_DEMON);
    bool salve_e = neck(a, GEAR_SALVE_AMULET_EI) || (strength_scaled && neck(a, GEAR_SALVE_AMULET_E));
    bool salve = neck(a, GEAR_SALVE_AMULET_I) || (strength_scaled && neck(a, GEAR_SALVE_AMULET));
    if (neck(a, GEAR_AMULET_OF_AVARICE) && attr(a, RC_NPC_ATTR_REVENANT)) hit = factor(hit, 24, 20);
    else if (salve_e && attr(a, RC_NPC_ATTR_UNDEAD)) hit = factor(hit, 6, 5);
    else if (salve && attr(a, RC_NPC_ATTR_UNDEAD)) hit = factor(hit, 7, 6);
    else if (strength_scaled && black_mask(a) && a->on_task) hit = factor(hit, 7, 6);
    else if (imbued_black_mask(a) && a->on_task) {
        // These bonuses are additive with Slayer only.
        int numerator = 23 + (revenant ? 10 : 0) + (dragonbane ? 5 : 0) + (demonbane ? 6 : 0);
        revenant = dragonbane = demonbane = false;
        hit = factor(hit, numerator, 20);
    }
    if (weapon(a, GEAR_TWISTED_BOW)) hit = tbow_scaling(hit, tbow_magic(a), false);
    if (revenant) hit = factor(hit, 3, 2);
    if (dragonbane) hit = factor(hit, 5, 4);
    if (demonbane) hit = add_demonbane(a, hit, 30);
    if ((a->weapon->flags & GEARF_RAT_BONE_WEAPON) && attr(a, RC_NPC_ATTR_RAT)) hit += 10;
    if (weapon(a, GEAR_TONALZTICS) || weapon(a, GEAR_TONALZTICS_UNCHARGED)) hit = factor(hit, 3, 4);
    return hit;
}

// NPC ranged defence depends on the weapon's weight: thrown weapons are light,
// crossbows and chinchompas heavy, salamanders an average of the three.
static void set_ranged_defence(const RcGearAttack *a, RcCombatCalc *calc) {
    const RcNpcCombatRow *row = a->npc_row;
    if (!row || (!row->light && !row->standard && !row->heavy)) return;
    const RcItemDef *item = rc_item_def_get(a->weapon_id);
    int type = item ? item->weapon_type : 0;
    int bonus = type == 23 ? row->light : type == 9 || type == 7 ? row->heavy
        : type == 26 ? (row->light + row->standard + row->heavy) / 3 : row->standard;
    calc->defence_roll = (a->npc->stats[1] + 9) * (bonus + 64);
}

void rc_content_gear_ranged(RcGearAttack *a, RcCombatCalc *calc) {
    set_ranged_defence(a, calc);
    int attack_roll = ranged_attack_roll(a);
    // rc_content_ranged_calc scaled the base roll for chinchompa distance.
    int plain = rc_player_effective_ranged_attack_level(a->p)
        * (a->p->equipment_bonuses[EQ_RANGED_ATK] - unused_ammo_stat(a, false) + 64);
    if (a->p->equipment[EQUIP_WEAPON].item_id >= 0 && plain > 0 && calc->attack_roll < plain)
        attack_roll = factor(attack_roll, calc->attack_roll, plain);
    calc->attack_roll = attack_roll;
    calc->max_hit = ranged_max_hit(a);
    calc->hit_chance = rc_hit_chance(calc->attack_roll, calc->defence_roll);
}

// Bonus percent when a spell's element is the target's elemental weakness.
static int spell_weakness(const RcGearAttack *a) {
    if (!a->spell || !a->npc_row || !a->npc_row->weakness) return 0;
    const char *name = a->spell->name;
    int element = !strncmp(name, "Fire ", 5) ? RC_WEAKNESS_FIRE
        : !strncmp(name, "Water ", 6) ? RC_WEAKNESS_WATER
        : !strncmp(name, "Earth ", 6) ? RC_WEAKNESS_EARTH
        : !strncmp(name, "Wind ", 5) ? RC_WEAKNESS_AIR : RC_WEAKNESS_NONE;
    return element == a->npc_row->weakness ? a->npc_row->severity : 0;
}

void rc_content_gear_magic(RcGearAttack *a, RcCombatCalc *calc, int base) {
    const RcPlayer *p = a->p;
    bool shadow = weapon(a, GEAR_TUMEKENS_SHADOW) && !a->spell;
    int shadow_factor = !shadow ? 1 : attr(a, RC_NPC_ATTR_TOA) ? 4 : 3;
    int effective = rc_player_effective_magic_attack_level(p);
    if (void_robes(a) && head(a, GEAR_VOID_MAGE_HELM)) effective = factor(effective, 29, 20);
    int roll_base = effective * (p->equipment_bonuses[EQ_MAGIC_ATK] * shadow_factor + 64);
    int roll = roll_base;
    int additive = 0;
    bool mask = false;
    bool standard = a->spell && a->spell->book == RC_SPELL_BOOK_STANDARD;
    if (neck(a, GEAR_AMULET_OF_AVARICE) && attr(a, RC_NPC_ATTR_REVENANT)) additive = 20;
    else if (neck(a, GEAR_SALVE_AMULET_EI) && attr(a, RC_NPC_ATTR_UNDEAD)) additive = 20;
    else if (neck(a, GEAR_SALVE_AMULET_I) && attr(a, RC_NPC_ATTR_UNDEAD)) additive = 15;
    else if (imbued_black_mask(a) && a->on_task) mask = true;
    if ((a->weapon->flags & GEARF_SILVER_WEAPON) && a->fam[EQUIP_RING] == GEAR_EFARITAYS_AID
            && attr(a, RC_NPC_ATTR_VAMPYRE)) additive += 15;
    if ((a->weapon->flags & GEARF_SMOKE_BATTLESTAFF) && standard) additive += 10;
    if (additive) roll = factor(roll, 100 + additive, 100);
    if (attr(a, RC_NPC_ATTR_DRAGON)) {
        if (weapon(a, GEAR_DRAGON_HUNTER_CROSSBOW)) roll = factor(roll, 13, 10);
        else if (weapon(a, GEAR_DRAGON_HUNTER_LANCE)) roll = factor(roll, 6, 5);
        else if (weapon(a, GEAR_DRAGON_HUNTER_WAND)) roll = factor(roll, 7, 4);
    }
    if (mask) roll = factor(roll, 23, 20);
    bool demonbane_spell = a->spell && strstr(a->spell->name, "Demonbane") != NULL;
    if (demonbane_spell && attr(a, RC_NPC_ATTR_DEMON))
        roll = add_demonbane(a, roll, weapon(a, GEAR_PURGING_STAFF) ? 40 : 20);
    if (revenant_buff(a)) roll = factor(roll, 3, 2);
    bool water = a->spell && !strncmp(a->spell->name, "Water ", 6);
    bool bind = a->spell && (!strcmp(a->spell->name, "Bind") || !strcmp(a->spell->name, "Snare")
                             || !strcmp(a->spell->name, "Entangle"));
    if (a->fam[EQUIP_SHIELD] == GEAR_TOME_OF_WATER && (water || bind)) roll = factor(roll, 6, 5);
    int weakness = spell_weakness(a);
    roll += factor(roll_base, weakness, 100);
    calc->attack_roll = roll;
    if (attr(a, RC_NPC_ATTR_MAGIC_BY_DEFENCE)) {
        int magic = a->npc->stats[5] + 9;
        calc->defence_roll = rc_npc_defensive_roll(a->npc, COMBAT_MAGIC) / magic * (a->npc->stats[1] + 9);
    }

    int hit = base;
    if (a->special && a->weapon->special == SPECIAL_SOUL_REND) hit = factor(hit, 13, 10);
    int base_hit = hit;
    if (a->fam[EQUIP_GLOVES] == GEAR_CHAOS_GAUNTLETS && a->spell
            && strstr(a->spell->name, "Bolt")) hit += 3;
    int damage = p->equipment_bonuses[EQ_MAGIC_DMG];
    if (shadow) {
        damage *= shadow_factor;
        if (damage > 1000) damage = 1000;
    }
    if ((a->weapon->flags & GEARF_SMOKE_BATTLESTAFF) && standard) damage += 100;
    if (a->spell && a->spell->book == RC_SPELL_BOOK_ANCIENT) {
        damage += 30 * (head(a, GEAR_VIRTUS_MASK) + (a->fam[EQUIP_BODY] == GEAR_VIRTUS_ROBE_TOP)
                        + (a->fam[EQUIP_LEGS] == GEAR_VIRTUS_ROBE_BOTTOM));
    }
    if (elite_void_robes(a) && head(a, GEAR_VOID_MAGE_HELM)) damage += 50;
    mask = false;
    if (neck(a, GEAR_SALVE_AMULET_EI) && attr(a, RC_NPC_ATTR_UNDEAD)) damage += 200;
    else if (neck(a, GEAR_SALVE_AMULET_I) && attr(a, RC_NPC_ATTR_UNDEAD)) damage += 150;
    else if (neck(a, GEAR_AMULET_OF_AVARICE) && attr(a, RC_NPC_ATTR_REVENANT)) damage += 200;
    else if (imbued_black_mask(a) && a->on_task) mask = true;
    damage += 10 * rc_prayer_magic_damage_bonus(p->active_prayers);
    hit += factor(hit, damage, 1000);
    if (mask) hit = factor(hit, 23, 20);
    if (weapon(a, GEAR_DAWNBRINGER)) {
        hit /= 2;
        if (hit < 2) hit = 2;
    }
    if (attr(a, RC_NPC_ATTR_DRAGON)) {
        if (weapon(a, GEAR_DRAGON_HUNTER_LANCE)) hit = factor(hit, 6, 5);
        else if (weapon(a, GEAR_DRAGON_HUNTER_WAND)) hit = factor(hit, 7, 5);
        else if (weapon(a, GEAR_DRAGON_HUNTER_CROSSBOW)) hit = factor(hit, 5, 4);
    }
    if (revenant_buff(a)) hit = factor(hit, 3, 2);
    hit += factor(base_hit, weakness, 100);
    bool charged_tome = p->equipment[EQUIP_SHIELD].quantity > 0 && p->equipment[EQUIP_SHIELD].state_id > 0;
    if (charged_tome && a->spell
            && ((a->fam[EQUIP_SHIELD] == GEAR_TOME_OF_FIRE && !strncmp(a->spell->name, "Fire ", 5))
             || (a->fam[EQUIP_SHIELD] == GEAR_TOME_OF_WATER && water)
             || (a->fam[EQUIP_SHIELD] == GEAR_TOME_OF_EARTH && !strncmp(a->spell->name, "Earth ", 6))))
        hit = factor(hit, 11, 10);
    calc->max_hit = hit;
    if (a->fam[EQUIP_RING] == GEAR_BRIMSTONE_RING && rc_content_roll(&a->world->rng_state, 4) == 0)
        calc->defence_roll = factor(calc->defence_roll, 9, 10);
    calc->hit_chance = rc_hit_chance(calc->attack_roll, calc->defence_roll);
}

// Sunfire runes floor a successful fire spell at a tenth of its max hit.
void rc_content_gear_spell_floor(RcGearAttack *a, const RcCombatCalc *calc) {
    if (a->spell && !strncmp(a->spell->name, "Fire ", 5)
            && rc_content_has_sunfire_runes(a->p, a->spell))
        // Measured before a tome's 10%.
        a->min_hit = (a->fam[EQUIP_SHIELD] == GEAR_TOME_OF_FIRE && a->p->equipment[EQUIP_SHIELD].state_id
                      ? (calc->max_hit * 10 + 10) / 11 : calc->max_hit) / 10;
}

// Weapons that hit vampyres of that tier for full damage.
static bool vampyrebane(const RcGearAttack *a, bool tier_three) {
    bool melee = a->style == COMBAT_MELEE_STAB || a->style == COMBAT_MELEE_SLASH
        || a->style == COMBAT_MELEE_CRUSH;
    if (weapon(a, GEAR_BLISTERWOOD_STAKE) && a->style == COMBAT_RANGED) return true;
    return (!tier_three || melee)
        && ((!tier_three && weapon(a, GEAR_ROD_OF_IVANDIS)) || weapon(a, GEAR_IVANDIS_FLAIL)
            || weapon(a, GEAR_BLISTERWOOD_FLAIL) || weapon(a, GEAR_BLISTERWOOD_SICKLE)
            || weapon(a, GEAR_HALLOWED_FLAIL) || weapon(a, GEAR_SUNSPEAR));
}

bool rc_content_gear_blocked(const RcGearAttack *a) {
    if (attr(a, RC_NPC_ATTR_VAMPYRE_3) && !vampyrebane(a, true)) return true;
    if (attr(a, RC_NPC_ATTR_VAMPYRE_2) && !vampyrebane(a, false)
            && a->fam[EQUIP_RING] != GEAR_EFARITAYS_AID && !silver_weapon(a)) return true;
    if ((a->weapon->flags & GEARF_RAT_BONE_WEAPON) && !attr(a, RC_NPC_ATTR_RAT)) return true;
    return false;
}

int rc_content_gear_after_roll(const RcGearAttack *a, const RcCombatCalc *calc,
                               int damage, bool accurate) {
    (void)calc;
    if (!accurate || damage <= 0) return damage;
    if (a->fam[EQUIP_AMMO] == GEAR_SEEKING_ARROW && a->uses_ammo_slot && damage < 3) damage = 3;
    bool melee = a->style != COMBAT_RANGED && a->style != COMBAT_MAGIC;
    if (attr(a, RC_NPC_ATTR_VAMPYRE)) {
        bool efaritay = a->fam[EQUIP_RING] == GEAR_EFARITAYS_AID;
        int percent = 100;
        if (weapon(a, GEAR_BLISTERWOOD_FLAIL) || weapon(a, GEAR_HALLOWED_FLAIL)
                || weapon(a, GEAR_BLISTERWOOD_STAKE)) percent = 125;
        else if (weapon(a, GEAR_SUNSPEAR)) percent = 150;
        else if (weapon(a, GEAR_BLISTERWOOD_SICKLE)) percent = 115;
        else if (weapon(a, GEAR_IVANDIS_FLAIL)) percent = 120;
        else if (weapon(a, GEAR_ROD_OF_IVANDIS) && !attr(a, RC_NPC_ATTR_VAMPYRE_3)) percent = 110;
        else if (silver_weapon(a) && attr(a, RC_NPC_ATTR_VAMPYRE_1)) percent = 110;
        if (percent != 100) {
            if (efaritay) damage = factor(damage, 11, 10);
            damage = factor(damage, percent, 100);
        }
        if (attr(a, RC_NPC_ATTR_VAMPYRE_2) && !vampyrebane(a, false)) {
            if (efaritay) damage /= 2;
            else if (silver_weapon(a) && damage > 10) damage = 10;
        }
    }
    if (melee && weapon(a, GEAR_GADDERHAMMER) && attr(a, RC_NPC_ATTR_SHADE))
        damage = rc_content_roll(&a->world->rng_state, 20) ? factor(damage, 5, 4) : damage * 2;
    if (a->npc_row && a->style != COMBAT_MAGIC && a->npc_row->flat_armour) {
        damage -= a->npc_row->flat_armour;
        if (damage < 0) damage = 0;
    }
    if (melee && (a->weapon->flags & GEARF_TZHAAR_WEAPON) && neck(a, GEAR_BERSERKER_NECKLACE))
        damage = factor(damage, 6, 5);
    const RcNpcDef *def = rc_npc_def_for_npc(a->world, a->npc);
    if (def && !strcmp(def->name, "Corporeal Beast")) {
        bool corpbane = a->style == COMBAT_MAGIC
            || (a->style == COMBAT_MELEE_STAB && (a->weapon->flags & GEARF_CORP_SPEAR));
        if (!corpbane) damage /= 2;
    }
    if (melee && dharok_set(a)) {
        int max_hp = a->p->max_hp / 10;
        int current = a->p->current_hp / 10;
        damage = (int)((int64_t)damage * (10000 + (max_hp - current) * max_hp) / 10000);
    }
    return damage;
}

// Chance when the attacker keeps the better of two attack rolls (osrs-dps-calc).
static float double_roll_chance(int atk, int def) {
    double a = atk, d = def;
    if (atk > def) return (float)(1.0 - (d + 2) * (2 * d + 3) / (a + 1) / (a + 1) / 6);
    return (float)(a * (4 * a + 5) / 6 / (a + 1) / (d + 1));
}

RcCombatRoll rc_content_gear_roll(RcGearAttack *a, const RcCombatCalc *calc) {
    RcCombatCalc use = *calc;
    if (weapon(a, GEAR_OSMUMTENS_FANG) && a->style == COMBAT_MELEE_STAB) {
        float one = use.hit_chance;
        use.hit_chance = attr(a, RC_NPC_ATTR_TOA) ? 1.0f - (1.0f - one) * (1.0f - one)
            : double_roll_chance(use.attack_roll, use.defence_roll);
    } else if (a->fam[EQUIP_GLOVES] == GEAR_CONFLICTION_GAUNTLETS && a->style == COMBAT_MAGIC
            && (rc_item_def_get(a->weapon_id) == NULL
                || (rc_item_def_get(a->weapon_id)->equip_conflicts & (1u << EQUIP_SHIELD)) == 0)) {
        float one = use.hit_chance;
        float two = double_roll_chance(use.attack_roll, use.defence_roll);
        use.hit_chance = two / (1.0f + two - one);
    }
    RcCombatRoll roll = rc_roll_attack(&use, &a->world->rng_state, true);
    if (a->npc->force_player_max_hit) roll = (RcCombatRoll){true, calc->max_hit};
    else if (roll.accurate && a->min_hit > 0)
        roll.damage = calc->max_hit > a->min_hit
            ? a->min_hit + rc_content_roll(&a->world->rng_state, calc->max_hit - a->min_hit + 1)
            : a->min_hit;
    return roll;
}

static void put_hit(RcPendingHit *hit, const RcCombatCalc *calc, int damage, bool accurate) {
    *hit = (RcPendingHit){.damage = damage, .max_hit = calc->max_hit, .accurate = accurate};
}

// Two hits splitting the max hit; the second can only land after the first.
int rc_content_gear_split_hits(RcGearAttack *a, RcCombatCalc *calc, RcPendingHit *hits, bool chained) {
    RcCombatCalc first = *calc, second = *calc;
    first.max_hit = calc->max_hit / 2;
    second.max_hit = calc->max_hit - first.max_hit;
    RcCombatRoll one = rc_content_gear_roll(a, &first);
    put_hit(&hits[0], &first, one.damage, one.accurate);
    if (chained && !one.accurate) {
        put_hit(&hits[1], &second, 0, false);
        return 2;
    }
    RcCombatRoll two = rc_content_gear_roll(a, &second);
    put_hit(&hits[1], &second, two.damage, two.accurate);
    return 2;
}

// Chance per thousand and effect of an enchanted bolt. The Armadyl crossbow's
// special doubles the chance; the Zaryte crossbow's guarantees it on a hit.
static int bolt_chance(const RcGearAttack *a) {
    int chance;
    switch (a->fam[EQUIP_AMMO]) {
    case GEAR_OPAL_BOLTS_E: chance = 50; break;
    case GEAR_JADE_BOLTS_E: case GEAR_PEARL_BOLTS_E: case GEAR_RUBY_BOLTS_E:
    case GEAR_DRAGONSTONE_BOLTS_E: chance = 60; break;
    case GEAR_DIAMOND_BOLTS_E: chance = 100; break;
    case GEAR_ONYX_BOLTS_E: chance = 110; break;
    case GEAR_EMERALD_BOLTS_E: chance = 550; break;
    default: return 0;
    }
    if (a->special && a->weapon->special == SPECIAL_ARMADYL_EYE) chance *= 2;
    if (weapon(a, GEAR_ASCENSION_CROSSBOWS)) chance /= 2;
    return chance;
}

static int rolled_up_to(const RcGearAttack *a, int max) {
    return max > 0 ? rc_content_roll(&a->world->rng_state, max + 1) : 0;
}

static void bolt_effect(RcGearAttack *a, const RcCombatCalc *calc, RcPendingHit *hit) {
    int chance = bolt_chance(a);
    bool zcb = a->weapon->special == SPECIAL_EVOKE;
    bool sure = zcb && a->special && hit->accurate;
    if (!chance || (!sure && rc_content_roll(&a->world->rng_state, 1000) >= chance)) return;
    int level = a->p->skills.boosted_level[SKILL_RANGED];
    bool fiery = attr(a, RC_NPC_ATTR_FIERY);
    switch (a->fam[EQUIP_AMMO]) {
    case GEAR_OPAL_BOLTS_E:
        hit->damage += level / (zcb ? 9 : 10);
        a->p->attack_ammo_effect = true;
        break;
    case GEAR_JADE_BOLTS_E:
        hit->flags |= GEAR_HIT_BIND;
        a->p->attack_ammo_effect = true;
        break;
    case GEAR_EMERALD_BOLTS_E:
        if (!hit->accurate) return;
        hit->flags |= GEAR_HIT_POISON;
        a->p->attack_ammo_effect = true;
        break;
    case GEAR_PEARL_BOLTS_E: {
        int divisor = fiery ? 15 : 20;
        hit->damage += level / (zcb ? divisor - 2 : divisor);
        a->p->attack_ammo_effect = true;
        break;
    }
    case GEAR_DIAMOND_BOLTS_E:
        hit->damage = rolled_up_to(a, factor(calc->max_hit, zcb ? 126 : 115, 100));
        hit->accurate = true;
        a->p->attack_ammo_effect = true;
        break;
    case GEAR_DRAGONSTONE_BOLTS_E:
        if (!hit->accurate || fiery || attr(a, RC_NPC_ATTR_DRAGON)) return;
        hit->damage += level * 2 / (zcb ? 9 : 10);
        a->p->attack_ammo_effect = true;
        break;
    case GEAR_ONYX_BOLTS_E: {
        if (!hit->accurate || attr(a, RC_NPC_ATTR_UNDEAD)) return;
        hit->damage = rolled_up_to(a, factor(calc->max_hit, zcb ? 132 : 120, 100));
        int healed = hit->damage / 4;
        a->p->current_hp += healed * 10;
        if (a->p->current_hp > a->p->max_hp) a->p->current_hp = a->p->max_hp;
        a->p->attack_ammo_effect = true;
        break;
    }
    case GEAR_RUBY_BOLTS_E: {
        if (a->p->current_hp < 100) return;
        int cap = zcb ? 110 : 100;
        int damage = factor(a->npc->current_hp, zcb ? 22 : 20, 100);
        hit->damage = damage < cap ? damage : cap;
        hit->accurate = true;
        a->p->current_hp -= a->p->current_hp / 10;
        a->p->attack_ammo_effect = true;
        break;
    }
    default:
        break;
    }
}

void rc_content_gear_after_hits(RcGearAttack *a, const RcCombatCalc *calc,
                                RcPendingHit *hits, int count) {
    RcPlayer *p = a->p;
    p->attack_ammo_effect = false;
    if (a->style == COMBAT_RANGED && a->uses_ammo_slot
            && (a->weapon->ammo_kind == AMMO_BOLT || a->weapon->ammo_kind == AMMO_ASCENSION)
            && !(a->special && a->weapon->special == SPECIAL_ANNIHILATE))
        for (int i = 0; i < count; i++) bolt_effect(a, calc, &hits[i]);
    for (int i = 0; i < count; i++)
        hits[i].damage = rc_content_gear_after_roll(a, calc, hits[i].damage, hits[i].accurate);
    if (p->content_gear[RC_GEAR_STATE_WRATH_TARGET] == a->npc->uid + 1
            && a->world->tick < (RcTick)p->content_gear[RC_GEAR_STATE_WRATH_UNTIL])
        for (int i = 0; i < count; i++) hits[i].damage = factor(hits[i].damage, 5, 4);
    p->content_gear[RC_GEAR_STATE_RONDACHE_ARMED] = a->fam[EQUIP_SHIELD] == GEAR_RONDACHE;
    p->attack_alternate = false;
    if (weapon(a, GEAR_ASCENSION_CROSSBOWS)) {
        p->content_gear[RC_GEAR_STATE_ASCENSION_SIDE] ^= 1;
        p->attack_alternate = p->content_gear[RC_GEAR_STATE_ASCENSION_SIDE] != 0;
    }
    if (weapon(a, GEAR_NOXIOUS_HALBERD) && count > 0 && hits[0].accurate)
        p->content_gear[RC_GEAR_STATE_NEXT_DAMAGE_BONUS] = 0;
    if (weapon(a, GEAR_DUAL_MACUAHUITL) && head(a, GEAR_BLOOD_MOON_HELM)
            && a->fam[EQUIP_BODY] == GEAR_BLOOD_MOON_CHESTPLATE
            && a->fam[EQUIP_LEGS] == GEAR_BLOOD_MOON_TASSETS) {
        // A third of accurate hits proc, in order; the special's hit always does.
        bool infusion = a->weapon->special == SPECIAL_BLOOD_INFUSION && a->special;
        bool proc = false;
        for (int i = 0; i < count && (infusion || hits[i].accurate) && !proc; i++)
            proc = hits[i].accurate && (infusion || rc_content_roll(&a->world->rng_state, 100) < 33);
        p->content_gear[RC_GEAR_STATE_ATTACK_SPEED_CUT] = proc ? 1 : 0;
    }
    if (weapon(a, GEAR_SOULREAPER_AXE) && !a->special) {
        int stacks = rc_content_soul_stacks(a->world);
        p->content_gear[RC_GEAR_STATE_SOUL_STACKS] = stacks < 5 ? stacks + 1 : 5;
        p->content_gear[RC_GEAR_STATE_SOUL_TICK] = (int)a->world->tick;
    }
}

// Extra targets of cleaving and bouncing weapons, in multi-combat.
static void hit_neighbours(RcGearAttack *a, const RcCombatCalc *calc, int x, int y,
                           int radius, int targets, int max_hit, int delay) {
    RcWorld *w = a->world;
    if (!rc_combat_is_multi_combat(w)) return;
    for (int i = 0; i < w->npc_count && targets > 0; i++) {
        RcNpc *npc = &w->npcs[i];
        if (!npc->active || npc->is_dead || npc->current_hp <= 0 || npc == a->npc
                || npc->plane != a->npc->plane || npc->player_untargetable) continue;
        int dx = npc->x - x, dy = npc->y - y;
        if (dx < -radius || dx > radius || dy < -radius || dy > radius) continue;
        const RcNpcDef *def = rc_npc_def_for_npc(w, npc);
        if (!def || def->attack_speed <= 0) continue;
        RcCombatCalc other = *calc;
        other.max_hit = max_hit;
        other.defence_roll = rc_npc_defensive_roll(npc, a->style);
        other.hit_chance = rc_hit_chance(other.attack_roll, other.defence_roll);
        RcCombatRoll roll = rc_roll_attack(&other, &w->rng_state, true);
        if (rc_queue_hit_meta(npc->pending_hits, &npc->num_pending_hits, roll.damage, delay,
                              a->style, RC_HIT_SOURCE_PLAYER, 0, w->tick, 0, max_hit)) {
            RcPendingHit *hit = &npc->pending_hits[npc->num_pending_hits - 1];
            hit->accurate = roll.accurate;
            hit->weapon_id = a->weapon_id > 0 ? (uint16_t)a->weapon_id : 0;
        }
        targets--;
    }
}

// Obligator: charged attacks take the best of two rolls, three if the target
// guards crush best, and gain size damage; Thrust pierces two tiles behind.
static int obligator_hit(RcGearAttack *a, RcCombatCalc *calc, RcPendingHit *hits) {
    const RcInvSlot *held = &a->p->equipment[EQUIP_WEAPON];
    bool thrust = a->p->attack_style_idx != 2;
    if (held->state_id > 0) {
        if (a->size >= 3) calc->max_hit = factor(calc->max_hit, 140, 100);
        else if (a->size == 2) calc->max_hit = factor(calc->max_hit, 120, 100);
        const int16_t *def = a->npc->defence_bonus;
        int rolls = def[COMBAT_MELEE_STAB - 1] > def[COMBAT_MELEE_CRUSH - 1]
            || def[COMBAT_MELEE_SLASH - 1] > def[COMBAT_MELEE_CRUSH - 1] ? 3 : 2;
        RcCombatRoll best = rc_content_gear_roll(a, calc);
        for (int i = 1; i < rolls; i++) {
            RcCombatRoll next = rc_content_gear_roll(a, calc);
            if (next.damage > best.damage) best = next;
        }
        put_hit(&hits[0], calc, best.damage, best.accurate);
    } else {
        RcCombatRoll roll = rc_content_gear_roll(a, calc);
        put_hit(&hits[0], calc, roll.damage, roll.accurate);
    }
    if (thrust) hit_neighbours(a, calc, a->npc->x, a->npc->y, 2, 2, calc->max_hit, 0);
    return 1;
}

int rc_content_gear_weapon_hits(RcGearAttack *a, RcCombatCalc *calc,
                                RcPendingHit *hits, int capacity) {
    bool melee = a->style != COMBAT_RANGED && a->style != COMBAT_MAGIC;
    if (melee && weapon(a, GEAR_OBLIGATOR)) return obligator_hit(a, calc, hits);
    if (melee && weapon(a, GEAR_HALLOWFELL)) {
        RcCombatRoll roll = rc_content_gear_roll(a, calc);
        put_hit(&hits[0], calc, roll.damage, roll.accurate);
        hit_neighbours(a, calc, a->p->x, a->p->y, 1, 2, calc->max_hit / 2, 0);
        return 1;
    }
    if (a->style == COMBAT_RANGED && weapon(a, GEAR_SAGES_AXE)
            && a->npc->current_hp * 5 < a->npc->spawn_hp) {
        put_hit(&hits[0], calc, a->npc->current_hp, true);  // executes the wounded
        return 1;
    }
    if (a->style == COMBAT_RANGED && weapon(a, GEAR_VENATOR_BOW)) {
        RcCombatRoll roll = rc_content_gear_roll(a, calc);
        put_hit(&hits[0], calc, roll.damage, roll.accurate);
        hit_neighbours(a, calc, a->npc->x, a->npc->y, 2, 2, calc->max_hit * 2 / 3, 2);
        return 1;
    }
    if (melee && weapon(a, GEAR_SCYTHE_OF_VITUR) && capacity >= 3) {
        int count = a->size < 3 ? a->size : 3;
        for (int i = 0; i < count; i++) {
            RcCombatCalc splash = *calc;
            splash.max_hit = calc->max_hit >> i;
            RcCombatRoll roll = rc_content_gear_roll(a, &splash);
            put_hit(&hits[i], &splash, roll.damage, roll.accurate);
        }
        return count;
    }
    if (a->style == COMBAT_RANGED && weapon(a, GEAR_DARK_BOW) && capacity >= 2) {
        int count = a->p->equipment[EQUIP_AMMO].quantity > 1 ? 2 : 1;
        for (int i = 0; i < count; i++) {
            RcCombatRoll roll = rc_content_gear_roll(a, calc);
            put_hit(&hits[i], calc, roll.damage, roll.accurate);
        }
        return count;
    }
    if (melee && weapon(a, GEAR_DUAL_MACUAHUITL) && capacity >= 2)
        return rc_content_gear_split_hits(a, calc, hits, true);
    if (melee && (a->weapon->flags & GEARF_TWO_HIT_MELEE) && capacity >= 2)
        return rc_content_gear_split_hits(a, calc, hits, false);
    if (melee && (weapon(a, GEAR_KERIS) || weapon(a, GEAR_KERIS_BREACHING)
                  || weapon(a, GEAR_KERIS_AMASCUT)) && attr(a, RC_NPC_ATTR_KALPHITE)
            && rc_content_roll(&a->world->rng_state, 51) == 0) {
        RcCombatRoll roll = rc_content_gear_roll(a, calc);
        put_hit(&hits[0], calc, roll.damage * 3, roll.accurate);
        return 1;
    }
    if (melee && barrows_set(a, GEAR_VERAC_HELM, GEAR_VERAC_BRASSARD, GEAR_VERAC_PLATESKIRT,
                             GEAR_VERAC_FLAIL) && rc_content_roll(&a->world->rng_state, 4) == 0) {
        int damage = 1 + rc_content_roll(&a->world->rng_state, calc->max_hit + 1);
        put_hit(&hits[0], calc, damage, true);
        return 1;
    }
    if (a->style == COMBAT_RANGED && barrows_set(a, GEAR_KARIL_COIF, GEAR_KARIL_LEATHERTOP,
            GEAR_KARIL_LEATHERSKIRT, GEAR_KARIL_CROSSBOW) && neck(a, GEAR_AMULET_OF_THE_DAMNED)
            && capacity >= 2) {
        RcCombatRoll roll = rc_content_gear_roll(a, calc);
        put_hit(&hits[0], calc, roll.damage, roll.accurate);
        if (!roll.accurate || rc_content_roll(&a->world->rng_state, 4) != 0) return 1;
        put_hit(&hits[1], calc, roll.damage / 2, true);
        return 2;
    }
    if (a->style == COMBAT_MAGIC && a->spell && weapon(a, GEAR_TWINFLAME_STAFF) && capacity >= 2
            && a->spell->book == RC_SPELL_BOOK_STANDARD
            && (strstr(a->spell->name, "Bolt") || strstr(a->spell->name, "Blast")
                || strstr(a->spell->name, "Wave"))) {
        RcCombatRoll roll = rc_content_gear_roll(a, calc);
        put_hit(&hits[0], calc, roll.damage, roll.accurate);
        put_hit(&hits[1], calc, roll.damage * 4 / 10, roll.accurate);
        return 2;
    }
    if (a->style == COMBAT_MAGIC && !a->spell && (weapon(a, GEAR_SANGUINESTI_STAFF))) {
        RcCombatRoll roll = rc_content_gear_roll(a, calc);
        if (roll.accurate && rc_content_roll(&a->world->rng_state, 5) == 0) roll.damage += 8;
        put_hit(&hits[0], calc, roll.damage, roll.accurate);
        return 1;
    }
    if (a->style == COMBAT_MAGIC && barrows_set(a, GEAR_AHRIM_HOOD, GEAR_AHRIM_ROBETOP,
            GEAR_AHRIM_ROBESKIRT, GEAR_AHRIM_STAFF) && neck(a, GEAR_AMULET_OF_THE_DAMNED)) {
        RcCombatRoll roll = rc_content_gear_roll(a, calc);
        if (rc_content_roll(&a->world->rng_state, 4) == 0) roll.damage = roll.damage * 13 / 10;
        put_hit(&hits[0], calc, roll.damage, roll.accurate);
        return 1;
    }
    return 0;
}

void rc_content_gear_bind(RcWorld *world, RcNpc *npc, int ticks) {
    if (world->tick < npc->immobilize_immune_until) return;
    npc->immobilized_until = world->tick + ticks;
    npc->immobilize_immune_until = npc->immobilized_until + 5;
}

int rc_content_edge_distance(const RcPlayer *player, int x, int y, int size) {
    int dx = player->x < x ? x - player->x : player->x - (x + size - 1);
    int dy = player->y < y ? y - player->y : player->y - (y + size - 1);
    if (dx < 0) dx = 0;
    if (dy < 0) dy = 0;
    return dx > dy ? dx : dy;
}

// Charges after decaying one per 30 seconds, even in combat; negative without
// the shield.
int rc_content_rondache_charges(const RcWorld *world) {
    const RcPlayer *p = &world->player;
    if (!rc_content_wearing(p, GEAR_RONDACHE)) return -1;
    int lost = (int)(world->tick - (RcTick)p->content_gear[RC_GEAR_STATE_RONDACHE_DECAY_TICK]) / 50;
    int charges = p->content_gear[RC_GEAR_STATE_RONDACHE_CHARGES];
    return lost >= charges ? 0 : charges - lost;
}

// Soul stacks start decaying 30 seconds after the last attack, one per 12 seconds.
int rc_content_soul_stacks(const RcWorld *world) {
    const RcPlayer *p = &world->player;
    int idle = (int)(world->tick - (RcTick)p->content_gear[RC_GEAR_STATE_SOUL_TICK]) - 50;
    int stacks = p->content_gear[RC_GEAR_STATE_SOUL_STACKS];
    int lost = idle > 0 ? idle / 20 + 1 : 0;
    return lost >= stacks ? 0 : stacks - lost;
}

// One Shatter stack decays per 30 seconds without a new landed hit.
int rc_content_shatter_stacks(const RcWorld *world) {
    const RcPlayer *p = &world->player;
    int lost = (int)(world->tick - (RcTick)p->content_gear[RC_GEAR_STATE_SHATTER_TICK]) / 50;
    int stacks = p->content_gear[RC_GEAR_STATE_SHATTER_STACKS];
    return lost >= stacks ? 0 : stacks - lost;
}

// Flat armour after protection prayers. What it stops feeds Shield Slam, at most
// once every 2 ticks however many enemies are hitting.
static int rondache_mitigate(RcWorld *world, int damage) {
    RcPlayer *p = &world->player;
    if (!p->content_gear[RC_GEAR_STATE_RONDACHE_ARMED]) return damage;
    int mitigated = damage > 2 ? 2 : damage - 1;
    if (mitigated <= 0) return damage;
    if (world->tick >= (RcTick)p->content_gear[RC_GEAR_STATE_RONDACHE_NEXT_GAIN]) {
        int *decay = &p->content_gear[RC_GEAR_STATE_RONDACHE_DECAY_TICK];
        int before = rc_content_rondache_charges(world);
        int lost = (int)(world->tick - (RcTick)*decay) / 50;
        *decay = before > 0 ? *decay + lost * 50 : (int)world->tick;
        int after = before + mitigated < 20 ? before + mitigated : 20;
        p->content_gear[RC_GEAR_STATE_RONDACHE_CHARGES] = after;
        p->content_gear[RC_GEAR_STATE_RONDACHE_NEXT_GAIN] = (int)(world->tick + 2);
        if (after == 20 && before < 20)
            rc_interaction_publish_message(p, "Your rondache is fully charged.");
    }
    return damage - mitigated;
}

int rc_content_gear_incoming(RcWorld *world, const RcPendingHit *hit, int damage) {
    RcPlayer *p = &world->player;
    bool melee = hit->attack_style == COMBAT_MELEE_STAB || hit->attack_style == COMBAT_MELEE_SLASH
        || hit->attack_style == COMBAT_MELEE_CRUSH;
    bool ranged = hit->attack_style == COMBAT_RANGED;
    int weapon_id = p->equipment[EQUIP_WEAPON].item_id;
    if (world->tick < (RcTick)p->content_gear[RC_GEAR_STATE_IMMUNE_UNTIL]
            && (melee || (ranged && rc_content_gear_row(weapon_id)->special_variant)))
        return 0;
    if (damage <= 0) return damage;
    if (melee && world->tick < (RcTick)p->content_gear[RC_GEAR_STATE_MELEE_WARD_UNTIL]
            && rc_content_special_is_ward(weapon_id))
        damage /= 2;
    if (rc_content_wearing(p, GEAR_ELYSIAN_SPIRIT_SHIELD) && rc_content_roll(&world->rng_state, 10) < 7)
        damage -= damage / 4 > 1 ? damage / 4 : 1;
    if (rc_content_gear_row(weapon_id)->family == GEAR_DINHS_BULWARK && p->attack_style_idx == 1)
        damage = damage * 4 / 5;
    if (rc_content_wearing(p, GEAR_JUSTICIAR_FACEGUARD) && rc_content_wearing(p, GEAR_JUSTICIAR_CHESTGUARD)
            && rc_content_wearing(p, GEAR_JUSTICIAR_LEGGUARDS)) {
        int index = hit->attack_style == COMBAT_MELEE_STAB ? EQ_STAB_DEF
            : hit->attack_style == COMBAT_MELEE_SLASH ? EQ_SLASH_DEF
            : hit->attack_style == COMBAT_MELEE_CRUSH ? EQ_CRUSH_DEF
            : ranged ? EQ_RANGED_DEF : EQ_MAGIC_DEF;
        int bonus = p->equipment_bonuses[index];
        if (bonus > 0) damage = (int)((int64_t)damage * (3000 - bonus) / 3000);
    }
    return rc_content_wearing(p, GEAR_RONDACHE) ? rondache_mitigate(world, damage) : damage;
}

void rc_content_gear_after_hit_taken(RcWorld *world, const RcPendingHit *hit, int damage) {
    RcPlayer *p = &world->player;
    if (damage <= 0) return;
    RcNpc *source = hit->source_idx >= 0 ? rc_npc_resolve(world, hit->source_idx) : NULL;
    if (!source || source->is_dead) return;
    bool suffering = rc_content_wearing(p, GEAR_RING_OF_SUFFERING);
    if (suffering || rc_content_wearing(p, GEAR_RING_OF_RECOIL)) {
        int recoil = 1 + damage / 10;
        rc_queue_hit_meta(source->pending_hits, &source->num_pending_hits, recoil, 0,
                          COMBAT_NONE, RC_HIT_SOURCE_STATUS, 0, world->tick, 0, recoil);
    }
    const RcInvSlot *boots = &p->equipment[EQUIP_BOOTS];
    const RcNpcDef *def = rc_npc_def_for_npc(world, source);
    if (rc_content_gear_row(boots->item_id)->family == GEAR_ECHO_BOOTS && damage > 0
            && boots->state_id > 0 && !p->content_gear[RC_GEAR_STATE_ECHO_OFF]
            && rc_content_edge_distance(p, source->x, source->y, def ? def->size : 1) <= 1) {
        RcItemTransaction tx;
        if (rc_item_tx_begin(&tx, world).code != RC_ITEM_RESULT_OK) return;
        tx.equipment[EQUIP_BOOTS].state_id--;
        if (rc_item_tx_commit(&tx).code == RC_ITEM_RESULT_OK)
            rc_queue_hit_meta(source->pending_hits, &source->num_pending_hits, 1, 0,
                              COMBAT_NONE, RC_HIT_SOURCE_STATUS, 0, world->tick, 0, 1);
    }
}

void rc_content_gear_landed(RcWorld *world, RcNpc *npc, const RcPendingHit *hit, int damage) {
    RcPlayer *p = &world->player;
    uint32_t *rng = &world->rng_state;
    const RcGearRow *row = rc_content_gear_row(hit->weapon_id);
    bool melee = hit->attack_style == COMBAT_MELEE_STAB || hit->attack_style == COMBAT_MELEE_SLASH
        || hit->attack_style == COMBAT_MELEE_CRUSH;
    bool ranged = hit->attack_style == COMBAT_RANGED;
    if (ranged && (hit->flags & GEAR_HIT_BIND)) rc_content_gear_bind(world, npc, 8);
    if (ranged && (hit->flags & GEAR_HIT_POISON)) rc_npc_apply_poison(world, npc, 25);
    if (row->special == SPECIAL_EXPLOSIVE_SHATTER && hit->accurate
            && !(hit->flags & RC_HIT_FLAG_SPECIAL)) {
        int stacks = rc_content_shatter_stacks(world);
        p->content_gear[RC_GEAR_STATE_SHATTER_STACKS] = stacks < 14 ? stacks + 1 : 14;
        p->content_gear[RC_GEAR_STATE_SHATTER_TICK] = (int)world->tick;
    }
    if (damage <= 0) return;
    if (melee && rc_content_wearing(p, GEAR_AMULET_OF_BLOOD_FURY) && rc_content_roll(rng, 5) == 0) {
        p->current_hp += damage * 3;
        if (p->current_hp > p->max_hp) p->current_hp = p->max_hp;
    }
    if (melee && row->family == GEAR_GUTHAN_WARSPEAR && rc_content_wearing(p, GEAR_GUTHAN_HELM)
            && rc_content_wearing(p, GEAR_GUTHAN_PLATEBODY) && rc_content_wearing(p, GEAR_GUTHAN_CHAINSKIRT)
            && rc_content_roll(rng, 4) == 0) {
        p->current_hp += damage * 10;
        if (p->current_hp > p->max_hp) p->current_hp = p->max_hp;
    }
    uint32_t flags = row->flags;
    if (ranged) flags |= rc_content_gear_row(p->equipment[EQUIP_AMMO].item_id)->flags;
    int tier = flags & GEARF_WEAPON_POISON_3 ? 3 : flags & GEARF_WEAPON_POISON_2 ? 2
        : flags & GEARF_WEAPON_POISON_1 ? 1 : 0;
    // A Serpentine helm turns the weapon's chance, or a melee hit, into venom.
    bool serpentine = rc_content_wearing(p, GEAR_SERPENTINE_HELM);
    int venom_odds = flags & GEARF_VENOM_WEAPON ? (serpentine ? 1 : 4)
        : row->family == GEAR_NOXIOUS_HALBERD ? (serpentine ? 2 : 3)
        : serpentine && melee ? (tier ? 2 : 6) : 0;
    if (venom_odds && rc_content_roll(rng, venom_odds) == 0) rc_npc_apply_venom(world, npc);
    else if (tier && rc_content_roll(rng, melee ? 4 : 8) == 0)
        rc_npc_apply_poison(world, npc, 15 + 5 * tier - (melee ? 0 : 14));
    if (row->family == GEAR_HOLY_WATER) {
        const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
        int defence = npc->base_stats[1] > 0 ? npc->base_stats[1] : def ? def->stats[1] : 0;
        if (npc->stats[1] >= defence) npc->stats[1] -= defence * 5 / 100;
    }
    if (row->ammo_kind == AMMO_ATLATL && rc_content_wearing(p, GEAR_ECLIPSE_MOON_HELM)
            && rc_content_wearing(p, GEAR_ECLIPSE_MOON_CHESTPLATE)
            && rc_content_wearing(p, GEAR_ECLIPSE_MOON_TASSETS) && rc_content_roll(rng, 5) == 0) {
        uint32_t attrs = rc_content_npc_attributes(world, npc);
        if (!(attrs & (RC_NPC_ATTR_BURN_IMMUNE | RC_NPC_ATTR_STRONG_BURN_IMMUNE))) {
            if (npc->burn_remaining <= 0) npc->burn_next_tick = world->tick + 4;
            if (npc->burn_remaining <= 40) npc->burn_remaining = (int16_t)(npc->burn_remaining + 10);
        }
    }
}
