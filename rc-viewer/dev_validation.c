#include "dev_validation.h"

#include "../rc-core/api.h"
#include "../rc-core/combat.h"
#include "../rc-core/items.h"
#include "../rc-core/npc.h"
#include "../rc-core/pathfinding.h"
#include "../rc-core/spells.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static int env_bool_local(const char *key, int fallback) {
    const char *value = getenv(key);
    if (!value || !value[0])
        return fallback;
    return strcmp(value, "0") != 0 && strcmp(value, "false") != 0;
}

int runec_dev_validation_enabled(void) {
    return env_bool_local("RUNEC_DEV_VALIDATION", 1);
}

static const RuneCDevTransport g_dev_transports[] = {
    {"varrock",  "Varrock",   RUNEC_DEV_VARROCK_BANK_X, RUNEC_DEV_VARROCK_BANK_Y, 0,   -1, 1},
    {"graardor", "Graardor",  2872, 5358, 2, 2215, 4},
    {"kbd",      "KBD",       2269, 4697, 0, 2266, 5},
    {"vorkath",  "Vorkath",   2269, 4062, 0, 8061, 7},
    {"jad",      "Jad",       2400, 5088, 0, 3127, 5},
};

static const RuneCDevEncounterNpc g_graardor_encounter[] = {
    {2215, 2872, 5358, 2, 4}, /* General Graardor */
    {2216, 2866, 5358, 2, 1}, /* Sergeant Strongstack */
    {2217, 2872, 5352, 2, 1}, /* Sergeant Steelwill */
    {2218, 2868, 5362, 2, 1}, /* Sergeant Grimspike */
};

static const RuneCDevEncounterNpc g_kbd_encounter[] = {
    {2266, 2269, 4697, 0, 5},
};

static const RuneCDevEncounterNpc g_vorkath_encounter[] = {
    {8061, 2269, 4062, 0, 7},
};

static const RuneCDevEncounterNpc g_jad_encounter[] = {
    {3127, 2400, 5088, 0, 5},
};

const RuneCDevTransport *runec_dev_validation_transports(int *count) {
    if (count)
        *count = runec_dev_validation_enabled() ? ARRAY_COUNT(g_dev_transports) : 0;
    return runec_dev_validation_enabled() ? g_dev_transports : NULL;
}

static void normalize_lookup_name(const char *src, char *dst, size_t cap) {
    size_t out = 0;
    if (!dst || cap == 0)
        return;
    for (size_t i = 0; src && src[i] && out + 1 < cap; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '\'' || c == ' ' || c == '-' || c == '_')
            continue;
        dst[out++] = (char)tolower(c);
    }
    dst[out] = '\0';
}

static int item_name_matches(const char *a, const char *b) {
    char na[96], nb[96];
    normalize_lookup_name(a, na, sizeof(na));
    normalize_lookup_name(b, nb, sizeof(nb));
    return strcmp(na, nb) == 0;
}

const RuneCDevTransport *runec_dev_validation_find_transport(const char *key) {
    int count = 0;
    const RuneCDevTransport *transports =
        runec_dev_validation_transports(&count);
    if (!key || !key[0] || !transports)
        return NULL;
    for (int i = 0; i < count; i++) {
        const RuneCDevTransport *d = &transports[i];
        if (strcmp(key, d->key) == 0 || strcmp(key, d->label) == 0)
            return d;
    }
    return NULL;
}

const RuneCDevEncounterNpc *runec_dev_validation_encounter_npcs(
    const RuneCDevTransport *transport, int *count) {
    if (count)
        *count = 0;
    if (!transport || !runec_dev_validation_enabled())
        return NULL;
    const RuneCDevEncounterNpc *rows = NULL;
    int row_count = 0;
    if (strcmp(transport->key, "graardor") == 0) {
        rows = g_graardor_encounter;
        row_count = ARRAY_COUNT(g_graardor_encounter);
    } else if (strcmp(transport->key, "kbd") == 0) {
        rows = g_kbd_encounter;
        row_count = ARRAY_COUNT(g_kbd_encounter);
    } else if (strcmp(transport->key, "vorkath") == 0) {
        rows = g_vorkath_encounter;
        row_count = ARRAY_COUNT(g_vorkath_encounter);
    } else if (strcmp(transport->key, "jad") == 0) {
        rows = g_jad_encounter;
        row_count = ARRAY_COUNT(g_jad_encounter);
    }
    if (count)
        *count = row_count;
    return rows;
}

int runec_dev_validation_prepare_encounter(RcWorld *world,
                                           const RuneCDevTransport *transport) {
    if (!world || !transport || !runec_dev_validation_enabled())
        return 0;
    int count = 0;
    const RuneCDevEncounterNpc *rows =
        runec_dev_validation_encounter_npcs(transport, &count);
    if (!rows || count <= 0)
        return 0;

    rc_combat_set_multi_combat(world, count > 1);
    int prepared = 0;
    for (int i = 0; i < count; i++) {
        const RuneCDevEncounterNpc *row = &rows[i];
        RcNpcEnsureResult result;
        int idx = rc_world_ensure_npc_near(world, row->npc_id, row->x,
                                           row->y, row->plane, 2, &result);
        if (idx < 0 || idx >= world->npc_count) {
            fprintf(stderr,
                    "dev transport: missing or unspawnable NPC %d for %s\n",
                    row->npc_id, transport->label);
            continue;
        }
        RcNpc *npc = &world->npcs[idx];
        npc->x = row->x;
        npc->y = row->y;
        npc->prev_x = row->x;
        npc->prev_y = row->y;
        npc->plane = row->plane;
        npc->spawn_x = row->x;
        npc->spawn_y = row->y;
        npc->disable_wander = true;
        npc->player_untargetable = false;
        npc->is_dead = false;
        npc->death_timer = 0;
        npc->respawn_timer = 0;
        if (npc->def_id >= 0 && npc->def_id < g_npc_def_count) {
            npc->current_hp = g_npc_defs[npc->def_id].hitpoints;
        }
        if (env_bool_local("RUNEC_DEV_BOSS_ATTACKS", 1)) {
            rc_combat_start_npc_vs_player(world, npc->uid, 0);
            npc->attack_timer = 0;
        }
        prepared++;
    }
    return prepared;
}

static int find_unnoted_item_id_by_name(const char *name) {
    static const struct {
        const char *query;
        const char *actual;
        int forced_id;
    } aliases[] = {
        {"Rune arrows", "Rune arrow", -1},
        {"Dragon arrows", "Dragon arrow", -1},
        {"Amethyst arrows", "Amethyst arrow", -1},
        {"Dragon darts", "Dragon dart", -1},
        {"Amethyst darts", "Amethyst dart", -1},
        {"Sunfire splinter", "Sunfire splinters", -1},
        {"Granite maul (ornate handle)", NULL, 12848},
    };
    for (int i = 0; i < ARRAY_COUNT(aliases); i++) {
        if (!item_name_matches(name, aliases[i].query))
            continue;
        if (aliases[i].forced_id >= 0) {
            const RcItemDef *forced = rc_item_def_get(aliases[i].forced_id);
            if (forced && !forced->noted && !forced->placeholder)
                return forced->id;
        }
        if (aliases[i].actual)
            name = aliases[i].actual;
        break;
    }
    int fallback = -1;
    for (int i = 0; i < RC_MAX_ITEM_DEFS; i++) {
        const RcItemDef *def = rc_item_def_get(i);
        if (!def || !def->name[0] || !item_name_matches(def->name, name))
            continue;
        if (!def->noted && !def->placeholder)
            return def->id;
        if (fallback < 0 && def->noted && def->linked_id_item >= 0) {
            const RcItemDef *linked = rc_item_def_get(def->linked_id_item);
            if (linked && !linked->noted && !linked->placeholder)
                fallback = linked->id;
        }
    }
    return fallback;
}

static int validation_quantity(int item_id, int requested) {
    const RcItemDef *def = rc_item_def_get(item_id);
    if (def && def->stackable && requested < 1000)
        return 1000;
    if (requested < 2)
        return 2;
    return requested;
}

static void seed_bank_names(RcWorld *world, int tab,
                            const char *const *names, int count,
                            int quantity) {
    for (int i = 0; i < count; i++) {
        int item_id = find_unnoted_item_id_by_name(names[i]);
        if (item_id < 0) {
            fprintf(stderr, "combat bank: missing item '%s'\n", names[i]);
            continue;
        }
        rc_bank_add_item_tab(world, item_id,
                             validation_quantity(item_id, quantity), tab);
    }
}

void runec_dev_validation_seed_bank(RcWorld *world) {
    if (!world || !runec_dev_validation_enabled())
        return;

    static const char *const ranged_items[] = {
        "Twisted bow", "Bow of faerdhinen", "Bow of faerdhinen (c)",
        "Crystal bow", "Toxic blowpipe", "Toxic blowpipe (empty)",
        "Zaryte crossbow", "Armadyl crossbow", "Dragon hunter crossbow",
        "Dragon crossbow", "Scorching bow", "Webweaver bow", "Craw's bow",
        "Venator bow", "Tonalztics of ralos", "Eclipse atlatl",
        "Hunters' sunlight crossbow", "Dark bow", "Magic shortbow (i)",
        "Heavy ballista", "Light ballista", "Dragon knife",
        "Dragon knife(p++)", "Dragon thrownaxe", "Morrigan's javelin",
        "Morrigan's throwing axe", "Black chinchompa", "Red chinchompa",
        "Masori mask (f)", "Masori body (f)", "Masori chaps (f)",
        "Masori mask", "Masori body", "Masori chaps", "Crystal helm",
        "Crystal body", "Crystal legs", "Armadyl helmet",
        "Armadyl chestplate", "Armadyl chainskirt", "Karil's coif",
        "Karil's leathertop", "Karil's leatherskirt", "Eclipse moon helm",
        "Eclipse moon chestplate", "Eclipse moon tassets", "Elite void top",
        "Elite void robe", "Void knight gloves", "Void ranger helm",
        "Dizana's quiver", "Blessed dizana's quiver", "Ava's assembler",
        "Dragonfire ward", "Twisted buckler", "Necklace of anguish",
        "Zaryte vambraces", "Pegasian boots", "Avernic treads",
        "Venator ring", "Archers ring (i)",
    };
    static const char *const ranged_ammo[] = {
        "Rune arrow", "Rune arrows", "Runite bolts", "Dragon arrows",
        "Amethyst arrows", "Dragon darts", "Amethyst darts",
        "Dragon javelin", "Atlatl dart", "Sunlight antler bolts",
        "Moonlight antler bolts", "Dragon bolts", "Opal dragon bolts (e)",
        "Jade dragon bolts (e)", "Pearl dragon bolts (e)",
        "Topaz dragon bolts (e)", "Sapphire dragon bolts (e)",
        "Emerald dragon bolts (e)", "Ruby dragon bolts (e)",
        "Diamond dragon bolts (e)", "Dragonstone dragon bolts (e)",
        "Onyx dragon bolts (e)",
    };
    static const char *const mage_items[] = {
        "Tumeken's shadow", "Eye of ayak", "Eye of ayak (uncharged)",
        "Sanguinesti staff", "Sanguinesti staff (uncharged)",
        "Harmonised nightmare staff", "Eldritch nightmare staff",
        "Volatile nightmare staff", "Nightmare staff", "Kodai wand",
        "Staff of fire", "Ancient staff", "Trident of the swamp",
        "Uncharged toxic trident", "Trident of the seas",
        "Thammaron's sceptre", "Accursed sceptre", "Accursed sceptre (a)",
        "Staff of the dead", "Toxic staff of the dead", "Staff of light",
        "Staff of balance", "Purging staff", "Dragon hunter wand",
        "Twinflame staff", "Ancient sceptre", "Blood ancient sceptre",
        "Ice ancient sceptre", "Smoke ancient sceptre",
        "Shadow ancient sceptre", "Zuriel's staff", "Ahrim's staff",
        "Blue moon spear", "Dawnbringer", "Ancestral hat",
        "Ancestral robe top", "Ancestral robe bottom", "Virtus mask",
        "Virtus robe top", "Virtus robe bottom", "Ahrim's hood",
        "Ahrim's robetop", "Ahrim's robeskirt", "Blue moon helm",
        "Blue moon chestplate", "Blue moon tassets", "Bloodbark helm",
        "Bloodbark body", "Bloodbark legs", "Bloodbark boots",
        "Bloodbark gauntlets", "Swampbark helm", "Swampbark body",
        "Swampbark legs", "Swampbark boots", "Swampbark gauntlets",
        "Elidinis' ward", "Elidinis' ward (f)", "Arcane spirit shield",
        "Ancient wyvern shield", "Malediction ward", "Mage's book",
        "Tome of fire", "Tome of fire (empty)", "Tome of water",
        "Tome of water (empty)", "Tome of earth", "Tome of earth (empty)",
        "Occult necklace", "Tormented bracelet", "Confliction gauntlets",
        "Eternal boots", "Avernic treads", "Magus ring", "Seers ring (i)",
        "Brimstone ring", "Imbued saradomin cape", "Imbued zamorak cape",
        "Imbued guthix cape",
    };
    static const char *const melee_items[] = {
        "Scythe of vitur", "Scythe of vitur (uncharged)", "Soulreaper axe",
        "Abyssal whip", "Dragon scimitar", "Osmumten's fang",
        "Ghrazi rapier", "Blade of saeldor", "Blade of saeldor (c)",
        "Inquisitor's mace", "Noxious halberd", "Dragon hunter lance",
        "Emberlight", "Arclight", "Voidwaker", "Dragon claws",
        "Burning claws", "Elder maul", "Dragon warhammer",
        "Bandos godsword", "Armadyl godsword", "Saradomin godsword",
        "Zamorak godsword", "Ancient godsword", "Abyssal tentacle",
        "Abyssal dagger", "Abyssal dagger(p++)", "Abyssal bludgeon",
        "Granite maul", "Granite maul (ornate handle)", "Crystal halberd",
        "Dragon halberd", "Dragon spear", "Dragon spear(p++)",
        "Zamorakian spear", "Zamorakian hasta", "Ursine chainmace",
        "Viggora's chainmace", "Keris", "Keris partisan",
        "Keris partisan of breaching", "Keris partisan of corruption",
        "Keris partisan of the sun", "Colossal blade", "Dharok's greataxe",
        "Guthan's warspear", "Verac's flail", "Torag's hammers",
        "Dual macuahuitl", "Arkan blade", "Soulflame horn",
        "Torva full helm", "Torva platebody", "Torva platelegs",
        "Oathplate helm", "Oathplate chest", "Oathplate legs",
        "Bandos chestplate", "Bandos tassets", "Inquisitor's great helm",
        "Inquisitor's hauberk", "Inquisitor's plateskirt",
        "Justiciar faceguard", "Justiciar chestguard", "Justiciar legguards",
        "Neitiznot faceguard", "Serpentine helm", "Magma helm",
        "Tanzanite helm", "Blood moon helm", "Blood moon chestplate",
        "Blood moon tassets", "Dharok's helm", "Dharok's platebody",
        "Dharok's platelegs", "Guthan's helm", "Guthan's platebody",
        "Guthan's chainskirt", "Verac's helm", "Verac's brassard",
        "Verac's plateskirt", "Torag's helm", "Torag's platebody",
        "Torag's platelegs", "Helm of neitiznot", "3rd age full helmet",
        "3rd age platebody", "3rd age platelegs", "3rd age kiteshield",
        "Obsidian helmet", "Obsidian platebody", "Obsidian platelegs",
        "Elite void top", "Elite void robe", "Void knight gloves",
        "Void melee helm", "Avernic defender", "Dragon defender",
        "Dinh's bulwark", "Dragonfire shield", "Elysian spirit shield",
        "Spectral spirit shield", "Arcane spirit shield",
        "Amulet of rancour", "Amulet of torture", "Amulet of blood fury",
        "Amulet of fury", "Berserker necklace", "Ferocious gloves",
        "Barrows gloves", "Primordial boots", "Echo boots",
        "Avernic treads", "Ultor ring", "Bellator ring",
        "Berserker ring (i)", "Tyrannical ring (i)", "Treasonous ring (i)",
        "Lightbearer", "Infernal cape", "Fire cape", "Mythical cape",
        "Toktz-xil-ak", "Toktz-xil-ek", "Tzhaar-ket-em", "Toktz-mej-tal",
    };
    static const char *const pvp_items[] = {
        "Voidwaker", "Dragon claws", "Burning claws", "Armadyl godsword",
        "Ancient godsword", "Bandos godsword", "Zamorak godsword",
        "Saradomin godsword", "Granite maul", "Granite maul (ornate handle)",
        "Dragon dagger(p++)", "Abyssal dagger(p++)", "Elder maul",
        "Dragon warhammer", "Statius's warhammer", "Vesta's longsword",
        "Vesta's blighted longsword", "Vesta's spear", "Morrigan's javelin",
        "Morrigan's throwing axe", "Dark bow", "Magic shortbow (i)",
        "Heavy ballista", "Dragon knife(p++)", "Dragon thrownaxe",
        "Webweaver bow", "Ursine chainmace", "Accursed sceptre",
        "Accursed sceptre (a)", "Volatile nightmare staff",
        "Eldritch nightmare staff", "Toxic staff of the dead",
        "Staff of the dead", "Staff of light", "Staff of balance",
        "Saradomin's blessed sword", "Noxious halberd", "Arkan blade",
        "Soulflame horn", "Vesta's chainbody", "Vesta's plateskirt",
        "Statius's full helm", "Statius's platebody", "Statius's platelegs",
        "Morrigan's coif", "Morrigan's leather body",
        "Morrigan's leather chaps", "Zuriel's hood", "Zuriel's robe top",
        "Zuriel's robe bottom", "Elite void top", "Elite void robe",
        "Void knight gloves", "Void melee helm", "Void ranger helm",
        "Void mage helm", "Torva full helm", "Torva platebody",
        "Torva platelegs", "Oathplate helm", "Oathplate chest",
        "Oathplate legs", "Masori mask (f)", "Masori body (f)",
        "Masori chaps (f)", "Ancestral hat", "Ancestral robe top",
        "Ancestral robe bottom", "Virtus mask", "Virtus robe top",
        "Virtus robe bottom", "Crystal helm", "Crystal body",
        "Crystal legs", "Karil's coif", "Karil's leathertop",
        "Karil's leatherskirt", "Ahrim's hood", "Ahrim's robetop",
        "Ahrim's robeskirt", "Amulet of rancour", "Amulet of torture",
        "Necklace of anguish", "Occult necklace", "Amulet of blood fury",
        "Amulet of the damned", "Tormented bracelet", "Ferocious gloves",
        "Zaryte vambraces", "Barrows gloves", "Lightbearer",
        "Ring of suffering (i)", "Ring of recoil", "Phoenix necklace",
        "Ring of life", "Berserker ring (i)", "Archers ring (i)",
        "Seers ring (i)", "Ultor ring", "Bellator ring", "Magus ring",
        "Venator ring", "Elysian spirit shield", "Spectral spirit shield",
        "Arcane spirit shield", "Dragonfire shield", "Dragonfire ward",
        "Ancient wyvern shield", "Dinh's bulwark",
    };
    static const char *const special_items[] = {
        "Lightbearer", "Surge potion(4)", "Surge potion(3)",
        "Surge potion(2)", "Surge potion(1)", "Soulflame horn",
        "Rite of vile transference", "Deadeye prayer scroll",
        "Mystic vigour prayer scroll", "Dexterous prayer scroll",
        "Arcane prayer scroll", "Zulrah's scales", "Revenant ether",
        "Crystal shard", "Sunfire splinter", "Ancient essence",
        "Vial of blood", "Echo crystal", "Aether rune", "Aether catalyst",
        "Demon tear", "Demonic tallow", "Barrel of demonic tallow",
        "Burnt page", "Soaked page", "Soiled page", "Desiccated page",
        "Divine rune pouch", "Rune pouch", "Amulet of the damned",
        "Berserker necklace", "Amulet of avarice", "Salve amulet(ei)",
        "Slayer helmet (i)", "Black mask (i)", "Ring of recoil",
        "Ring of suffering", "Ring of suffering (i)", "Phoenix necklace",
        "Ring of life", "Brimstone ring", "Elysian spirit shield",
        "Spectral spirit shield", "Arcane spirit shield", "Dinh's bulwark",
        "Dragonfire shield", "Dragonfire ward", "Ancient wyvern shield",
        "Serpentine helm", "Magma helm", "Tanzanite helm", "Echo boots",
        "Weapon poison(+)", "Weapon poison(++)", "Anti-venom+(4)",
        "Shark", "Anglerfish", "Prayer potion(4)", "Ranging potion(4)",
        "Magic potion(4)", "Super combat potion(4)", "Coins",
        "Crystal helm", "Crystal body", "Crystal legs",
        "Bow of faerdhinen", "Bow of faerdhinen (c)",
        "Elite void top", "Elite void robe", "Void knight gloves",
        "Void melee helm", "Void ranger helm", "Void mage helm",
        "Justiciar faceguard", "Justiciar chestguard", "Justiciar legguards",
        "Inquisitor's great helm", "Inquisitor's hauberk",
        "Inquisitor's plateskirt", "Virtus mask", "Virtus robe top",
        "Virtus robe bottom", "Obsidian helmet", "Obsidian platebody",
        "Obsidian platelegs", "Toktz-xil-ak", "Toktz-xil-ek",
        "Tzhaar-ket-em", "Toktz-mej-tal", "Dharok's helm",
        "Dharok's platebody", "Dharok's platelegs", "Dharok's greataxe",
        "Guthan's helm", "Guthan's platebody", "Guthan's chainskirt",
        "Guthan's warspear", "Verac's helm", "Verac's brassard",
        "Verac's plateskirt", "Verac's flail", "Karil's coif",
        "Karil's leathertop", "Karil's leatherskirt", "Ahrim's hood",
        "Ahrim's robetop", "Ahrim's robeskirt", "Ahrim's staff",
        "Eclipse moon helm", "Eclipse moon chestplate",
        "Eclipse moon tassets", "Eclipse atlatl", "Blue moon helm",
        "Blue moon chestplate", "Blue moon tassets", "Blue moon spear",
        "Blood moon helm", "Blood moon chestplate", "Blood moon tassets",
        "Dual macuahuitl", "Torva full helm", "Torva platebody",
        "Torva platelegs", "Oathplate helm", "Oathplate chest",
        "Oathplate legs",
    };
    static const char *const special_stacks[] = {
        "Air rune", "Water rune", "Earth rune", "Fire rune", "Mind rune",
        "Chaos rune", "Death rune", "Blood rune", "Soul rune",
        "Wrath rune", "Cosmic rune", "Law rune", "Astral rune",
        "Nature rune", "Dragon arrows", "Amethyst arrows", "Dragon darts",
        "Amethyst darts", "Dragon javelin", "Atlatl dart",
        "Atlatl dart shaft", "Atlatl dart tips", "Sunlight antler bolts",
        "Moonlight antler bolts", "Dragon bolts", "Opal dragon bolts (e)",
        "Jade dragon bolts (e)", "Pearl dragon bolts (e)",
        "Topaz dragon bolts (e)", "Sapphire dragon bolts (e)",
        "Emerald dragon bolts (e)", "Ruby dragon bolts (e)",
        "Diamond dragon bolts (e)", "Dragonstone dragon bolts (e)",
        "Onyx dragon bolts (e)", "Zulrah's scales", "Revenant ether",
        "Crystal shard", "Sunfire splinter", "Ancient essence",
    };

    seed_bank_names(world, RUNEC_DEV_BANK_TAB_RANGED, ranged_items,
                    ARRAY_COUNT(ranged_items), 2);
    seed_bank_names(world, RUNEC_DEV_BANK_TAB_RANGED, ranged_ammo,
                    ARRAY_COUNT(ranged_ammo), 10000);
    seed_bank_names(world, RUNEC_DEV_BANK_TAB_MAGE, mage_items,
                    ARRAY_COUNT(mage_items), 2);
    seed_bank_names(world, RUNEC_DEV_BANK_TAB_MELEE, melee_items,
                    ARRAY_COUNT(melee_items), 2);
    seed_bank_names(world, RUNEC_DEV_BANK_TAB_PVP, pvp_items,
                    ARRAY_COUNT(pvp_items), 2);
    seed_bank_names(world, RUNEC_DEV_BANK_TAB_SPECIAL, special_items,
                    ARRAY_COUNT(special_items), 2);
    seed_bank_names(world, RUNEC_DEV_BANK_TAB_SPECIAL, special_stacks,
                    ARRAY_COUNT(special_stacks), 100000);

    int fire_blast = rc_spell_find("Fire Blast");
    if (fire_blast >= 0)
        world->player.selected_spell = fire_blast;
    rc_recalc_bonuses(&world->player);
    rc_refresh_player_combat_style(&world->player);
}

int runec_dev_validation_spawn_varrock_bank_dummy(RcWorld *world) {
    if (!world || !runec_dev_validation_enabled()
            || !env_bool_local("RUNEC_DEV_BANK_DUMMY", 1)) {
        return -1;
    }
    enum { DUMMY_NPC_ID = 2668 };
    static const int tiles[][2] = {
        {3182, 3443}, {3181, 3443}, {3182, 3437}, {3181, 3437},
        {3183, 3440}, {3183, 3441}, {3184, 3442},
    };
    int dummy_x = tiles[0][0];
    int dummy_y = tiles[0][1];
    int found_tile = 0;
    for (int i = 0; i < ARRAY_COUNT(tiles); i++) {
        int x = tiles[i][0];
        int y = tiles[i][1];
        if (rc_tile_blocked(&world->map, x, y, 0))
            continue;
        dummy_x = x;
        dummy_y = y;
        found_tile = 1;
        break;
    }
    if (!found_tile)
        return -1;

    int existing = rc_world_find_npc_near(world, DUMMY_NPC_ID,
                                          3183, 3440, 0, 8);
    int idx = existing;
    if (idx < 0) {
        int def_idx = rc_npc_def_find(DUMMY_NPC_ID);
        if (def_idx < 0) {
            fprintf(stderr, "combat bank: missing combat dummy NPC %d\n",
                    DUMMY_NPC_ID);
            return -1;
        }
        idx = rc_npc_spawn(world, def_idx, dummy_x, dummy_y, 0);
    }
    if (idx < 0 || idx >= world->npc_count)
        return -1;
    RcNpc *dummy = &world->npcs[idx];
    dummy->x = dummy_x;
    dummy->y = dummy_y;
    dummy->prev_x = dummy->x;
    dummy->prev_y = dummy->y;
    dummy->plane = 0;
    dummy->force_player_max_hit = true;
    dummy->disable_wander = true;
    dummy->current_hp = 100000000;
    dummy->target_uid = -1;
    dummy->attack_timer = 0;
    dummy->wander_timer = 0;
    dummy->spawn_x = dummy->x;
    dummy->spawn_y = dummy->y;
    fprintf(stderr, "combat bank: dummy ready at %d,%d,%d uid=%d\n",
            dummy->x, dummy->y, dummy->plane, dummy->uid);
    return idx;
}

int runec_dev_validation_bank_withdraw_quantity(const RcWorld *world,
                                                int bank_slot) {
    if (!world || !runec_dev_validation_enabled()
            || bank_slot < 0 || bank_slot >= RC_BANK_SIZE) {
        return -1;
    }
    const RcInvSlot *slot = &world->player.bank[bank_slot];
    if (slot->item_id < 0 || slot->quantity <= 1)
        return 0;
    return slot->quantity - 1;
}
