#include "magic.h"
#include "gear.h"
#include "interaction.h"
#include "items.h"
#include "combat_formula.h"
#include "npc.h"
#include "prayer.h"
#include "ralos.h"
#include "specials.h"
#include "ammunition.h"
#include "rng.h"
#include "slayer.h"
#include <stdio.h>
#include <string.h>

// Max hit: magic level / divisor + offset, or offset alone with divisor 0.
typedef struct {
    int charged, empty, capacity, divisor, offset;
} PoweredStaff;

// Item identities are b241; formulas checked against modern OSRS references.
static const PoweredStaff powered[] = {
    {11905, 11908, 2500, 3, -5},
    {11907, 11908, 2500, 3, -5},
    {12899, 12900, 2500, 3, -2},
    {22288, 22290, 20000, 3, -5},
    {22292, 22294, 20000, 3, -2},
    {22323, 22481, 20000, 3, 0},
    {25731, 25733, 20000, 3, 0},
    {27275, 27277, 20000, 3, 1},
    {28547, 28549, 20000, 3, 1},
    {22555, 22552, 17000, 3, -8},
    {27665, 27662, 17000, 3, -6},
    {28585, 28583, 20000, 37, 96},
    {31113, 31115, 50000, 3, -6},
    {33255, 33257, 10000, 3, -10},  // Lithic sceptre, at least 10
    {22516, -1, 0, 3, -2},
    {33314, 33316, 2500, 3, -2},
    {33318, 33320, 20000, 3, -2},
    {33322, 33434, 2500, 3, -5},
    {33323, 33434, 2500, 3, -5},
    {33326, 33328, 20000, 3, -5},
    {34725, 34727, 20000, 0, 18},   // Zorya's tome
    {22335, -1, 0, 0, 8},           // Starter staves
    {28557, -1, 0, 0, 8},
    {28796, -1, 0, 3, 5},           // Bone staff, +10 ratbane built in
    {23898, -1, 0, 0, 23},          // Gauntlet staves
    {23899, -1, 0, 0, 31},
    {23900, -1, 0, 0, 39},
    {23852, -1, 0, 0, 23},
    {23853, -1, 0, 0, 31},
    {23854, -1, 0, 0, 39},
};

// Weapons spending charges without being powered staves. `empty` replaces
// them at zero; blowpipes keep their dart in the state's high bits.
static const struct { int charged, empty, capacity; } charged_weapons[] = {
    {23983, 23985, 20000}, {24123, 23985, 20000},  // Crystal bow
    {25865, 25862, 20000},                         // Bow of Faerdhinen
    {22550, 22547, 17000}, {27655, 27652, 17000},  // Craw's, Webweaver
    {27610, 27612, 50000}, {30434, 30436, 50000},  // Venator
    {30390, 30392, 5000},                          // Nature's reprisal
    {28945, 28945, 60000},                         // Echo boots
    {12926, 12924, 16383}, {28688, 28687, 16383}, {30374, 30373, 16383},
    {31575, 31577, 16383}, {31579, 31581, 16383}, {31583, 31585, 16383},
    {34715, 34717, 20000},                         // Obligator
};

static const int blowpipe_darts[] = {11230, 25849, 811, 810, 809, 808, 807, 806};

static const PoweredStaff *powered_staff(int id) {
    for (unsigned i = 0; i < sizeof(powered) / sizeof(powered[0]); i++)
        if (powered[i].charged == id) return &powered[i];
    return NULL;
}

static int charged_weapon(int id) {
    for (unsigned i = 0; i < sizeof(charged_weapons) / sizeof(charged_weapons[0]); i++)
        if (charged_weapons[i].charged == id) return (int)i;
    return -1;
}

static uint32_t weapon_charges(const RcInvSlot *slot) {
    return rc_content_gear_row(slot->item_id)->family == GEAR_BLOWPIPE
        ? slot->state_id & 0xFFFFu : slot->state_id;
}

int rc_content_blowpipe_dart_strength(const RcInvSlot *slot) {
    if (rc_content_gear_row(slot->item_id)->family != GEAR_BLOWPIPE) return 0;
    unsigned index = slot->state_id >> 16;
    const RcItemDef *dart = rc_item_def_get(
        blowpipe_darts[index < sizeof(blowpipe_darts) / sizeof(blowpipe_darts[0]) ? index : 0]);
    return dart ? dart->ranged_strength : 0;
}

bool rc_content_internal_ammo_ready(const RcInvSlot *slot, const char **failure) {
    if (charged_weapon(slot->item_id) < 0 || weapon_charges(slot) > 0) return true;
    *failure = "Your weapon has no charges left.";
    return false;
}

uint32_t rc_content_magic_charge_capacity(int id) {
    const PoweredStaff *staff = powered_staff(id);
    if (staff) return staff->capacity;
    int charged = charged_weapon(id);
    if (charged >= 0) return charged_weapons[charged].capacity;
    if (id == 1409 || id == 33330) return 120;
    if (id == 12658 || id == 33332) return 2500;
    if (id == 27679 || id == 27788) return 17000;
    return 0;
}

int rc_content_magic_consume_charge(RcWorld *world, int id) {
    int charged = charged_weapon(id);
    if (world->player.combat_style != COMBAT_MAGIC && charged < 0)
        return rc_content_ralos_consume_charge(world, id);
    if (id == 34715 && world->player.attack_style_idx == 2) return 1;  // Smash
    uint32_t capacity = rc_content_magic_charge_capacity(id);
    if (!capacity) return rc_content_ralos_consume_charge(world, id);
    RcItemTransaction tx;
    if (rc_item_tx_begin(&tx, world).code != RC_ITEM_RESULT_OK) return 0;
    RcInvSlot *weapon = &tx.equipment[EQUIP_WEAPON];
    uint32_t charges = weapon->item_id == id ? weapon_charges(weapon) : 0;
    if (!charges || charges > capacity) return charged >= 0 && id == 34715;
    bool blowpipe = rc_content_gear_row(id)->family == GEAR_BLOWPIPE;
    if (!blowpipe || rc_content_roll(&world->rng_state, 3) != 0) {
        weapon->state_id--;
        charges--;
    }
    const PoweredStaff *staff = powered_staff(id);
    if (charges == 0) {
        if (staff) weapon->item_id = staff->empty;
        else if (charged >= 0) weapon->item_id = charged_weapons[charged].empty;
    }
    return rc_item_tx_commit(&tx).code == RC_ITEM_RESULT_OK;
}

static RcInteractionHandlerResult check_charges(RcWorld *world, RcPlayer *p,
                                                const RcPendingInteraction *it, void *ctx) {
    (void)world;
    (void)ctx;
    int slot = it->target.inventory_slot;
    if (slot < 0 || slot >= RC_INVENTORY_SIZE)
        return rc_interaction_result_message("Weapon slot is no longer available.");
    const RcItemDef *def = rc_item_def_get(p->inventory[slot].item_id);
    char message[RC_INTERACTION_RESULT_MESSAGE_LEN];
    snprintf(message, sizeof(message), "%s has %u charges.", def ? def->name : "It",
             weapon_charges(&p->inventory[slot]));
    return rc_interaction_result_message(message);
}

static RcInteractionHandlerResult toggle_echo_boots(RcWorld *world, RcPlayer *p,
                                                    const RcPendingInteraction *it, void *ctx) {
    (void)world;
    (void)it;
    (void)ctx;
    int off = p->content_gear[RC_GEAR_STATE_ECHO_OFF] ^= 1;
    return rc_interaction_result_message(off ? "Your boots will no longer recoil damage."
                                             : "Your boots will now recoil damage.");
}

int rc_content_weapon_charges_register(RcWorld *world) {
    for (unsigned i = 0; i < sizeof(charged_weapons) / sizeof(charged_weapons[0]) + sizeof(powered) / sizeof(powered[0]); i++) {
        unsigned count = sizeof(charged_weapons) / sizeof(charged_weapons[0]);
        int id = i < count ? charged_weapons[i].charged : powered[i - count].charged;
        const RcItemDef *def = rc_item_def_get(id);
        if (!def) continue;
        for (int op = 0; op < RC_ITEM_ACTION_COUNT; op++) {
            RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
            key.kind = RC_INTERACTION_INVENTORY_ITEM;
            key.op = RC_INTERACTION_OP1 + op;
            key.definition_id = id;
            if (!strcmp(def->inventory_actions[op], "Toggle-activity")) {
                if (!rc_interaction_register_world_handler(world, &key, toggle_echo_boots, NULL)) return 0;
            }
            if (strcmp(def->inventory_actions[op], "Check")) continue;
            if (!rc_interaction_register_world_handler(world, &key, check_charges, NULL)) return 0;
        }
    }
    return 1;
}

int rc_content_can_autocast(const RcPlayer *p, const RcSpellDef *spell) {
    const RcItemDef *weapon = rc_item_def_get(p->equipment[EQUIP_WEAPON].item_id);
    if (!weapon || !spell || spell->max_hit <= 0 || powered_staff(weapon->id)) return 0;
    const RcGearRow *row = rc_content_gear_row(weapon->id);
    unsigned flags = row->flags;
    bool slayer = flags & GEARF_SLAYERS_STAFF, group = flags & GEARF_SOTD_GROUP;
    if (spell->book == RC_SPELL_BOOK_ANCIENT) {
        if (row->family == GEAR_HARMONISED_STAFF) return 0;
        return (flags & GEARF_ANCIENT_CASTER) || (row->family == GEAR_AHRIM_STAFF
            && rc_content_gear_row(p->equipment[EQUIP_AMULET].item_id)->family == GEAR_AMULET_OF_THE_DAMNED);
    }
    if (spell->book == RC_SPELL_BOOK_ARCEUUS) return (flags & GEARF_ARCEUUS_CASTER) != 0;
    if (spell->book != RC_SPELL_BOOK_STANDARD) return 0;
    if (!strcmp(spell->name, "Iban Blast")) return row->family == GEAR_IBANS_STAFF;
    if (!strcmp(spell->name, "Magic Dart")) return slayer || group;
    if (!strcmp(spell->name, "Saradomin Strike")) return weapon->id == 22296;
    if (!strcmp(spell->name, "Claws of Guthix"))
        return weapon->id == 24144 || row->family == GEAR_VOID_KNIGHT_MACE;
    if (!strcmp(spell->name, "Flames of Zamorak"))
        return (group && weapon->id != 22296 && weapon->id != 24144)
            || row->family == GEAR_ACCURSED_SCEPTRE_A;
    if (!strcmp(spell->name, "Crumble Undead"))
        return slayer || group || row->family == GEAR_VOID_KNIGHT_MACE
            || row->family == GEAR_SKULL_SCEPTRE_I;
    return !strncmp(spell->name, "Wind ", 5) || !strncmp(spell->name, "Water ", 6) ||
        !strncmp(spell->name, "Earth ", 6) || !strncmp(spell->name, "Fire ", 5);
}

int rc_content_prepare_magic(RcWorld *world, const RcNpc *target,
                              const RcSpellDef *spell, RcCombatCalc *calc,
                              int *speed, const char **failure) {
    const RcPlayer *p = &world->player;
    const RcInvSlot *held = &p->equipment[EQUIP_WEAPON];
    const RcItemDef *weapon = rc_item_def_get(held->item_id);
    int level = p->skills.boosted_level[SKILL_MAGIC];
    int max_hit;
    if (spell) {
        const RcNpcDef *npc = rc_npc_def_for_npc(world, target);
        unsigned required = !strcmp(spell->name, "Crumble Undead") ? 1
            : strstr(spell->name, "Demonbane") ? 2 : 0;
        uint32_t attrs = rc_content_npc_attributes(world, target);
        if (required && (!npc || !(attrs & (required == 1 ? RC_NPC_ATTR_UNDEAD : RC_NPC_ATTR_DEMON)))) {
            *failure = required == 1 ? "This target is not a reviewed undead spell target."
                                     : "This target is not a reviewed demon spell target.";
            return 0;
        }
        if (level < spell->level) {
            *failure = "Your Magic level is too low for this spell.";
            return 0;
        }
        if (p->manual_spell_cast < 0 && !rc_content_can_autocast(p, spell)) {
            *failure = "This weapon cannot autocast that spell.";
            return 0;
        }
        int god_staff = !strcmp(spell->name, "Saradomin Strike") ? 2415 :
            !strcmp(spell->name, "Claws of Guthix") ? 2416 :
            !strcmp(spell->name, "Flames of Zamorak") ? 2417 : -1;
        if (god_staff >= 0 && held->item_id != god_staff && !rc_content_can_autocast(p, spell)) {
            *failure = "This god spell requires its corresponding staff.";
            return 0;
        }
        max_hit = spell->max_hit;
        // Elemental spells scale to the strongest unlocked element in their tier.
        const char *tiers[] = {"Strike", "Bolt", "Blast", "Wave", "Surge"};
        const int levels[][4] = {{1,5,9,13}, {17,23,29,35}, {41,47,53,59},
                                {62,65,70,75}, {81,85,90,95}};
        const int hits[][4] = {{2,4,6,8}, {9,10,11,12}, {13,14,15,16},
                              {17,18,19,20}, {21,22,23,24}};
        if (!strncmp(spell->name, "Wind ", 5) || !strncmp(spell->name, "Water ", 6) ||
                !strncmp(spell->name, "Earth ", 6) || !strncmp(spell->name, "Fire ", 5)) {
            const char *tier = strchr(spell->name, ' ') + 1;
            for (int i = 0; i < 5; i++) if (!strcmp(tier, tiers[i]))
                for (int j = 0; j < 4; j++) if (level >= levels[i][j]) max_hit = hits[i][j];
        }
        if (!strcmp(spell->name, "Magic Dart")) {
            if (!weapon || !(rc_content_gear_row(weapon->id)->flags
                             & (GEARF_SLAYERS_STAFF | GEARF_SOTD_GROUP))) {
                *failure = "Magic Dart requires a compatible Slayer staff.";
                return 0;
            }
            max_hit = held->item_id == 21255 && rc_slayer_npc_on_task(world, target->def_id)
                ? 13 + level / 6 : 10 + level / 10;  // Slayer's staff (e)
        }
        if (!strcmp(spell->name, "Iban Blast") &&
                (!weapon || rc_content_gear_row(weapon->id)->family != GEAR_IBANS_STAFF || !held->state_id ||
                 held->state_id > rc_content_magic_charge_capacity(held->item_id))) {
            *failure = "Iban Blast requires an Iban's staff with charges.";
            return 0;
        }
        if (weapon && weapon->id == 24423 && spell->book == RC_SPELL_BOOK_STANDARD
                && p->manual_spell_cast < 0) *speed = 4;
        if (weapon && rc_content_gear_row(weapon->id)->family == GEAR_TWINFLAME_STAFF) *speed = 6;
    } else {
        if (weapon && rc_content_gear_row(weapon->id)->family == GEAR_NATURES_REPRISAL) {
            if (!held->state_id) {
                *failure = "Your weapon has no charges left.";
                return 0;
            }
            max_hit = (level - 15) / 3 + 3;
            *calc = rc_calc_magic(p, target, max_hit);
            RcGearAttack reprisal;
            rc_content_gear_attack(&reprisal, world, target, NULL, p->combat.special_pending);
            rc_content_gear_magic(&reprisal, calc, max_hit);
            return 1;
        }
        if (weapon && weapon->weapon_type == 26) {
            static const int strength[] = {0, 56, 59, 77, 92, 104};
            int tier = rc_content_gear_row(weapon->id)->ammo_tier;
            if (tier >= 1 && tier <= 5) {
                if (rc_content_ranged_resource_slot(p, failure) != EQUIP_AMMO) return 0;
                max_hit = (level * (strength[tier] + 64) + 320) / 640;
                *calc = rc_calc_magic(p, target, max_hit);
                RcGearAttack salamander;
                rc_content_gear_attack(&salamander, world, target, NULL, p->combat.special_pending);
                rc_content_gear_magic(&salamander, calc, max_hit);
                return 1;
            }
        }
        const PoweredStaff *staff = powered_staff(held->item_id);
        if (!staff || (staff->capacity &&
                (!held->state_id || held->state_id > (uint32_t)staff->capacity))) {
            *failure = "This powered weapon has no valid charges. Use a charged version.";
            return 0;
        }
        max_hit = staff->divisor == 0 ? staff->offset
            : staff->divisor == 37 ? (8 * level + staff->offset) / 37
            : level / staff->divisor + staff->offset;
        if (held->item_id == 33255 && max_hit < 10) max_hit = 10;
        if (max_hit < 1) max_hit = 1;
        if (weapon->id == 34725 && p->content_gear[RC_GEAR_STATE_FLOW_ATTACKS] > 0
                && world->tick < (RcTick)p->content_gear[RC_GEAR_STATE_FLOW_UNTIL]) {
            RcPlayer *caster = &world->player;
            caster->content_gear[RC_GEAR_STATE_FLOW_ATTACKS]--;
            *speed = 2;
        }
    }
    *calc = rc_calc_magic(p, target, max_hit);
    RcGearAttack attack;
    rc_content_gear_attack(&attack, world, target, spell, p->combat.special_pending);
    rc_content_gear_magic(&attack, calc, max_hit);
    return 1;
}

int rc_content_player_hit_delay(const RcWorld *world, const RcNpc *target,
                                const RcSpellDef *spell, int hit_index) {
    const RcPlayer *p = &world->player;
    const RcNpcDef *npc = rc_npc_def_for_npc(world, target);
    int size = npc && npc->size > 0 ? npc->size : 1;
    // Barrages measure to the south-west tile, the rest to the nearest edge.
    bool barrage = spell && strstr(spell->name, "Barrage");
    int distance = rc_content_edge_distance(p, target->x, target->y, barrage ? 1 : size);
    const RcItemDef *weapon = rc_item_def_get(p->equipment[EQUIP_WEAPON].item_id);
    const RcGearRow *row = rc_content_gear_row(weapon ? weapon->id : -1);
    // Discrete server arrival rules, independent of any projectile assets. Each
    // is the wiki's value plus the tick NPCs add by processing before players.
    if (p->combat.special_pending && weapon) {
        int fixed = rc_content_special_hit_delay(weapon->id, distance, hit_index);
        if (fixed) return fixed;
    }
    if (p->combat_style == COMBAT_MAGIC) {
        if (spell && !strcmp(spell->name, "Crumble Undead")) return 2 + (distance + 3) / 6;
        if (spell && strstr(spell->name, "Demonbane")) return 3;
        if (spell && strstr(spell->name, "Grasp")) return 2;
        if (!spell && row->family == GEAR_TUMEKENS_SHADOW) return 3 + (distance + 1) / 3;
        if (!spell && weapon && weapon->id == 31113) return distance <= 2 ? 2 : 3;  // Eye of Ayak
        return 2 + (distance + 1) / 3;
    }
    if (p->combat_style != COMBAT_RANGED || (weapon && weapon->weapon_type == 26)) return 1;
    if (row->special == SPECIAL_CONCENTRATED_SHOT) return distance <= 4 ? 3 : 4;
    if (row->family == GEAR_TONALZTICS || row->family == GEAR_TONALZTICS_UNCHARGED) return 3;
    if (row->family == GEAR_DARK_BOW && hit_index) return 2 + (distance + 2) / 3;
    if (weapon && (weapon->weapon_type == 23 || weapon->weapon_type == 7)
            && row->ammo_kind != AMMO_ATLATL)
        return 2 + distance / 6;
    return 2 + (distance + 3) / 6;
}

void rc_content_magic_hit(RcWorld *world, RcNpc *target,
                          const RcPendingHit *hit, int damage) {
    if (!hit->accurate || hit->attack_style != COMBAT_MAGIC) return;
    const RcSpellDef *spell = hit->spell_key ? rc_spell_def_get(hit->spell_key - 1) : NULL;
    int heal = 0;
    if (spell) {
        if (spell->effect_flags & RC_SPELL_EFFECT_HEAL) heal = damage / 4;
        if ((spell->effect_flags & RC_SPELL_EFFECT_POISON)
                && rc_content_roll(&world->rng_state, 8) == 0
                && rc_npc_apply_poison(world, target, strstr(spell->name, "Blitz") ||
                    strstr(spell->name, "Barrage") ? 20 : 10))
            target->player_loot_credit = true;
        if (spell->effect_flags & RC_SPELL_EFFECT_FREEZE) {
            int ticks = !strcmp(spell->name, "Bind") ? 8 :
                !strcmp(spell->name, "Snare") ? 16 :
                !strcmp(spell->name, "Entangle") ? 24 :
                !strcmp(spell->name, "Ice Rush") ? 8 :
                !strcmp(spell->name, "Ice Burst") ? 16 :
                !strcmp(spell->name, "Ice Blitz") ? 24 :
                !strcmp(spell->name, "Ice Barrage") ? 32 :
                !strcmp(spell->name, "Undead Grasp") ? 4 :
                !strcmp(spell->name, "Skeletal Grasp") ? 2 : 1;
            bool grasp = strstr(spell->name, "Grasp") != NULL;
            if (world->tick >= target->immobilize_immune_until &&
                    (!grasp || rc_rng_range(&world->rng_state, 2) == 0)) {
                target->immobilized_until = world->tick + ticks;
                target->immobilize_immune_until = target->immobilized_until + 5;
            }
        }
        if (spell->effect_flags & RC_SPELL_EFFECT_DRAIN) {
            int stat = strstr(spell->name, "Shadow") || !strcmp(spell->name, "Confuse") ||
                !strcmp(spell->name, "Stun") ? 0 :
                !strcmp(spell->name, "Weaken") || !strcmp(spell->name, "Enfeeble") ? 2 :
                !strcmp(spell->name, "Flames of Zamorak") ? 5 : 1;
            int percent = !strcmp(spell->name, "Confuse") || !strcmp(spell->name, "Weaken") ||
                !strcmp(spell->name, "Curse") ? 5 : 10;
            if (!strcmp(spell->name, "Shadow Blitz") || !strcmp(spell->name, "Shadow Barrage"))
                percent = 15;
            const RcNpcDef *def = rc_npc_def_for_npc(world, target);
            int floor = def ? def->stats[stat] * (100 - percent) / 100 : 0;
            if (target->stats[stat] > floor) target->stats[stat] = floor;
        }
    } else if (rc_content_gear_row(hit->weapon_id)->family == GEAR_SANGUINESTI_STAFF &&
            rc_rng_range(&world->rng_state, 5) == 0) heal = damage / 2;
    if (heal > 0 && world->player.current_hp < world->player.max_hp) {
        RcPlayer *p = &world->player;
        p->current_hp += heal * 10;
        if (p->current_hp > p->max_hp) p->current_hp = p->max_hp;
    }
}
