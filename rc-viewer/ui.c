#include "ui.h"
#include "context_menu.h"
#include "ui_reference.h"
#include "../rc-core/storage.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OSRS_ORANGE ((Color){255, 152, 31, 255})
#define OSRS_ITEM_TARGET ((Color){255, 144, 64, 255})
#define OSRS_YELLOW ((Color){255, 255, 0, 255})
#define OSRS_GREEN  ((Color){0, 255, 0, 255})
#define OSRS_RED    ((Color){255, 40, 25, 255})
#define OSRS_BLUE   ((Color){90, 170, 255, 255})
#define OSRS_PANEL  ((Color){31, 25, 18, 232})
#define OSRS_TAB_PRESS_SECONDS 0.12f
#define RUNEC_UI_BANK_COLS 10
#define RUNEC_UI_BANK_ROWS 8
#define RUNEC_UI_BANK_VISIBLE_SLOTS (RUNEC_UI_BANK_COLS * RUNEC_UI_BANK_ROWS)
#define RUNEC_UI_BANK_SLOT_W 36
#define RUNEC_UI_BANK_SLOT_H 32
#define RUNEC_UI_BANK_SLOT_STEP 42
#define RUNEC_UI_COMPONENT_ID(group_id, file_id) \
    ((((uint32_t)(group_id)) << 16) | ((uint32_t)(file_id) & 0xffffu))
#define RUNEC_UI_GROUP_TOPLEVEL 161u
#define RUNEC_UI_GROUP_CHATBOX 162u
#define RUNEC_UI_GROUP_ORBS 160u
#define RUNEC_UI_GROUP_INVENTORY 149u
#define RUNEC_UI_GROUP_MAGIC 218u
#define RUNEC_UI_GROUP_STATS 320u
#define RUNEC_UI_GROUP_WORNITEMS 387u
#define RUNEC_UI_GROUP_PRAYER 541u
#define RUNEC_UI_GROUP_COMBAT 593u
#define RUNEC_UI_TOP_CHAT_CONTAINER RUNEC_UI_COMPONENT_ID(RUNEC_UI_GROUP_TOPLEVEL, 96)
#define RUNEC_UI_TOP_MAP_CONTAINER RUNEC_UI_COMPONENT_ID(RUNEC_UI_GROUP_TOPLEVEL, 95)
#define RUNEC_UI_TOP_SIDE_CONTAINER RUNEC_UI_COMPONENT_ID(RUNEC_UI_GROUP_TOPLEVEL, 73)
#define RUNEC_UI_TOP_MAIN_MODAL RUNEC_UI_COMPONENT_ID(RUNEC_UI_GROUP_TOPLEVEL, 16)
#define RUNEC_UI_TOP_SIDE_MODAL RUNEC_UI_COMPONENT_ID(RUNEC_UI_GROUP_TOPLEVEL, 74)
#define RUNEC_UI_INVENTORY_ITEMS_COMPONENT RUNEC_UI_COMPONENT_ID(RUNEC_UI_GROUP_INVENTORY, 0)
#define RUNEC_UI_MAGIC_FILTER_BUTTON RUNEC_UI_COMPONENT_ID(RUNEC_UI_GROUP_MAGIC, 202)
#define RUNEC_UI_MAGIC_FILTER_CONTAINER RUNEC_UI_COMPONENT_ID(RUNEC_UI_GROUP_MAGIC, 203)
#define RUNEC_UI_WORN_EQUIPMENT_BUTTON RUNEC_UI_COMPONENT_ID(RUNEC_UI_GROUP_WORNITEMS, 1)
#define RUNEC_UI_WORN_PRICE_BUTTON RUNEC_UI_COMPONENT_ID(RUNEC_UI_GROUP_WORNITEMS, 3)
#define RUNEC_UI_WORN_DEATH_BUTTON RUNEC_UI_COMPONENT_ID(RUNEC_UI_GROUP_WORNITEMS, 5)
#define RUNEC_UI_WORN_FOLLOWER_BUTTON RUNEC_UI_COMPONENT_ID(RUNEC_UI_GROUP_WORNITEMS, 7)

typedef struct RuneCUiLayout {
    Rectangle chat;
    Rectangle chat_messages;
    Rectangle chat_controls;
    Rectangle chat_input;
    Rectangle map;
    Rectangle minimap;
    Rectangle compass;
    Rectangle xp_orb;
    Rectangle worldmap_button;
    Rectangle hp_orb;
    Rectangle prayer_orb;
    Rectangle run_orb;
    Rectangle spec_orb;
    Rectangle side;
    Rectangle side_bg;
    Rectangle side_content;
    Rectangle tab[RUNEC_UI_TAB_COUNT];
} RuneCUiLayout;

typedef struct RuneCUiListenerEvent {
    RuneCUiListenerKind kind;
    uint32_t component_id;
    int op;
    Vector2 mouse;
} RuneCUiListenerEvent;

typedef int (*RuneCUiLocalHandlerFn)(RuneCUiState *ui,
                                     const RuneCUiListenerEvent *event,
                                     const RuneCUiHitResult *hit);

typedef struct RuneCUiLocalListenerHandler {
    uint32_t component_id;
    RuneCUiListenerKind kind;
    RuneCUiLocalHandlerFn handler;
} RuneCUiLocalListenerHandler;

static Rectangle open_interface_mount_rect(const RuneCUiState *ui,
                                           const RuneCUiLayout *layout,
                                           const RuneCUiOpenInterface *open,
                                           int screen_w,
                                           int screen_h);

static const char *g_tab_icon[RUNEC_UI_TAB_COUNT] = {
    "side_icon_combat",
    "side_icon_stats",
    "side_icon_quests",
    "side_icon_inventory",
    "side_icon_equipment",
    "side_icon_prayer",
    "side_icon_magic",
    "side_icon_options",
    "side_icon_clan",
};

static const RuneCUiSpellRef g_spell_icons[] = {
#define SPELL_ICON(book, name, sprite) {book, name, #sprite},
#include "spell_icons.inc"
#undef SPELL_ICON
};

static const char *g_spellbook_names[] = {"Standard", "Ancient", "Lunar", "Arceuus"};

static const char *g_skill_names[24] = {
    "Attack", "Hitpoints", "Mining", "Strength", "Agility", "Smithing",
    "Defence", "Herblore", "Fishing", "Ranged", "Thieving", "Cooking",
    "Prayer", "Crafting", "Firemaking", "Magic", "Fletching", "Woodcutting",
    "Runecraft", "Slayer", "Farming", "Construction", "Hunter", "Sailing",
};

static const int g_skill_icon_index[24] = {
    0, 6, 12, 1, 7, 13,
    2, 8, 14, 3, 9, 15,
    4, 10, 16, 5, 11, 17,
    18, 19, 20, 22, 21, 23,
};

static const char *g_equipment_names[RUNEC_UI_EQUIP_SLOT_COUNT] = {
    "Head", "Cape", "Neck", "Weapon", "Body", "Shield", "Ammo",
    "Legs", "Unused", "Hands", "Feet", "Unused", "Ring", "Quiver",
};

static const char *decoded_group_for_tab(RuneCUiTab tab) {
    switch (tab) {
    case RUNEC_UI_TAB_COMBAT: return "combat_interface";
    case RUNEC_UI_TAB_SKILLS: return "stats";
    case RUNEC_UI_TAB_QUESTS: return "questjournal";
    case RUNEC_UI_TAB_INVENTORY: return "inventory";
    case RUNEC_UI_TAB_EQUIPMENT: return "wornitems";
    case RUNEC_UI_TAB_PRAYER: return "prayerbook";
    case RUNEC_UI_TAB_SPELLBOOK: return "magic_spellbook";
    case RUNEC_UI_TAB_SETTINGS: return "settings_side";
    case RUNEC_UI_TAB_CLAN: return NULL;
    default: return NULL;
    }
}

static const RuneCUiOpenInterface *find_open_interface(
    const RuneCUiState *ui,
    RuneCUiOpenMount mount,
    RuneCUiTab tab) {
    if (!ui)
        return NULL;
    for (int i = 0; i < ui->open_interface_count; i++) {
        const RuneCUiOpenInterface *open = &ui->open_interfaces[i];
        if (open->active && open->mount == mount && open->tab == tab)
            return open;
    }
    return NULL;
}

static const char *open_group_for_mount(const RuneCUiState *ui,
                                        RuneCUiOpenMount mount,
                                        RuneCUiTab tab) {
    const RuneCUiOpenInterface *open = find_open_interface(ui, mount, tab);
    return open && open->group[0] ? open->group : NULL;
}

static const char *open_group_for_side_content(const RuneCUiState *ui) {
    const char *group = open_group_for_mount(ui, RUNEC_UI_MOUNT_SIDE_CONTENT,
                                             ui ? ui->active_tab : RUNEC_UI_TAB_NONE);
    if (group)
        return group;
    if (!ui)
        return NULL;
    return decoded_group_for_tab(ui->active_tab);
}

static const Rectangle g_equipment_offsets[RUNEC_UI_EQUIP_SLOT_COUNT] = {
    {77, 4, 36, 36},
    {36, 43, 36, 36},
    {77, 43, 36, 36},
    {21, 82, 36, 36},
    {77, 82, 36, 36},
    {133, 82, 36, 36},
    {-1000, -1000, 0, 0},
    {77, 122, 36, 36},
    {-1000, -1000, 0, 0},
    {21, 162, 36, 36},
    {77, 162, 36, 36},
    {-1000, -1000, 0, 0},
    {133, 162, 36, 36},
    {133, 43, 36, 36},
};

static const char *g_worn_icon_names[RUNEC_UI_EQUIP_SLOT_COUNT] = {
    "wornicons_0", "wornicons_1", "wornicons_2", "wornicons_3",
    "wornicons_4", "wornicons_5", NULL, "wornicons_6",
    NULL, "wornicons_7", "wornicons_8", NULL, "wornicons_9", "wornicons_10",
};

static const int g_worn_slot_component_file[RUNEC_UI_EQUIP_SLOT_COUNT] = {
    15, 16, 17, 18, 19, 20, -1, 21, -1, 22, 23, -1, 24, 25,
};

typedef struct RuneCUiCombatStyleDef {
    int visible;
    int style_index;
    const char *label;
    const char *mode;
    const char *icon_asset;
} RuneCUiCombatStyleDef;

typedef struct RuneCUiCombatProfile {
    int osrs_category;
    const RuneCUiCombatStyleDef styles[RUNEC_UI_COMBAT_STYLE_COUNT];
} RuneCUiCombatProfile;

#define COMBAT_STYLE(style_index, label, mode, icon_asset) \
    {1, style_index, label, mode, icon_asset}
#define COMBAT_STYLE_HIDDEN \
    {0, 0, "", "", ""}

/*
 * Exact b237 combat_interface_weapon_category DB rows from the local
 * Joshua-F dump. Core owns the selected style; the viewer owns this
 * presentation mapping so weapon tabs use the same labels/icons as OSRS.
 */
static const RuneCUiCombatProfile g_combat_profiles[] = {
    {0, {
        COMBAT_STYLE(0, "Punch", "Accurate", "combaticons_14"),
        COMBAT_STYLE(1, "Kick", "Aggressive", "combaticons_15"),
        COMBAT_STYLE(3, "Block", "Defensive", "combaticons_16"),
        COMBAT_STYLE_HIDDEN,
    }},
    {1, {
        COMBAT_STYLE(0, "Chop", "Accurate", "combaticons_1"),
        COMBAT_STYLE(1, "Hack", "Aggressive", "combaticons_2"),
        COMBAT_STYLE(2, "Smash", "Aggressive", "combaticons_3"),
        COMBAT_STYLE(3, "Block", "Defensive", "combaticons_0"),
    }},
    {2, {
        COMBAT_STYLE(0, "Pound", "Accurate", "combaticons2_2"),
        COMBAT_STYLE(1, "Pummel", "Aggressive", "combaticons2_3"),
        COMBAT_STYLE(3, "Block", "Defensive", "combaticons2_0"),
        COMBAT_STYLE_HIDDEN,
    }},
    {3, {
        COMBAT_STYLE(0, "Accurate", "Accurate", "combaticons2_15"),
        COMBAT_STYLE(1, "Rapid", "Rapid", "combaticons2_16"),
        COMBAT_STYLE(3, "Longrange", "Longrange", "combaticons2_17"),
        COMBAT_STYLE_HIDDEN,
    }},
    {4, {
        COMBAT_STYLE(0, "Chop", "Accurate", "combaticons3_6"),
        COMBAT_STYLE(1, "Slash", "Aggressive", "combaticons3_5"),
        COMBAT_STYLE(2, "Lunge", "Controlled", "combaticons3_4"),
        COMBAT_STYLE(3, "Block", "Defensive", "combaticons3_7"),
    }},
    {5, {
        COMBAT_STYLE(0, "Accurate", "Accurate", "combaticons2_5"),
        COMBAT_STYLE(1, "Rapid", "Rapid", "combaticons2_6"),
        COMBAT_STYLE(3, "Longrange", "Longrange", "combaticons2_7"),
        COMBAT_STYLE_HIDDEN,
    }},
    {6, {
        COMBAT_STYLE(0, "Scorch", "Accurate", "combaticons3_16"),
        COMBAT_STYLE(1, "Flare", "Aggressive", "combaticons3_17"),
        COMBAT_STYLE(2, "Blaze", "Defensive", "combaticons3_18"),
        COMBAT_STYLE_HIDDEN,
    }},
    {7, {
        COMBAT_STYLE(0, "Short fuse", "Accurate", "combaticons3_15"),
        COMBAT_STYLE(1, "Medium fuse", "Rapid", "combaticons3_9"),
        COMBAT_STYLE(3, "Long fuse", "Longrange", "combaticons3_8"),
        COMBAT_STYLE_HIDDEN,
    }},
    {8, {
        COMBAT_STYLE(0, "Aim and Fire", "Accurate", "128"),
        COMBAT_STYLE(1, "Kick", "Aggressive", "combaticons_15"),
        COMBAT_STYLE_HIDDEN,
        COMBAT_STYLE_HIDDEN,
    }},
    {9, {
        COMBAT_STYLE(0, "Chop", "Accurate", "combaticons_6"),
        COMBAT_STYLE(1, "Slash", "Aggressive", "combaticons_5"),
        COMBAT_STYLE(2, "Lunge", "Controlled", "combaticons_7"),
        COMBAT_STYLE(3, "Block", "Defensive", "combaticons_4"),
    }},
    {10, {
        COMBAT_STYLE(0, "Chop", "Accurate", "combaticons_6"),
        COMBAT_STYLE(1, "Slash", "Aggressive", "combaticons_5"),
        COMBAT_STYLE(2, "Smash", "Aggressive", "combaticons_5"),
        COMBAT_STYLE(3, "Block", "Defensive", "combaticons_4"),
    }},
    {11, {
        COMBAT_STYLE(0, "Spike", "Accurate", "combaticons3_1"),
        COMBAT_STYLE(1, "Impale", "Aggressive", "combaticons3_3"),
        COMBAT_STYLE(2, "Smash", "Aggressive", "combaticons3_2"),
        COMBAT_STYLE(3, "Block", "Defensive", "combaticons3_0"),
    }},
    {12, {
        COMBAT_STYLE(0, "Jab", "Controlled", "combaticons3_11"),
        COMBAT_STYLE(1, "Swipe", "Aggressive", "combaticons3_12"),
        COMBAT_STYLE(3, "Fend", "Defensive", "combaticons3_10"),
        COMBAT_STYLE_HIDDEN,
    }},
    {13, {
        COMBAT_STYLE(0, "Bash", "Accurate", "combaticons2_13"),
        COMBAT_STYLE(1, "Pound", "Aggressive", "combaticons2_14"),
        COMBAT_STYLE(3, "Block", "Defensive", "combaticons_19"),
        COMBAT_STYLE_HIDDEN,
    }},
    {14, {
        COMBAT_STYLE(0, "Reap", "Accurate", "combaticons2_19"),
        COMBAT_STYLE(1, "Chop", "Aggressive", "combaticons2_9"),
        COMBAT_STYLE(2, "Jab", "Controlled", "combaticons2_18"),
        COMBAT_STYLE(3, "Block", "Defensive", "combaticons2_8"),
    }},
    {15, {
        COMBAT_STYLE(0, "Lunge", "Controlled", "combaticons_8"),
        COMBAT_STYLE(1, "Swipe", "Controlled", "combaticons_18"),
        COMBAT_STYLE(2, "Pound", "Controlled", "combaticons_9"),
        COMBAT_STYLE(3, "Block", "Defensive", "combaticons_17"),
    }},
    {16, {
        COMBAT_STYLE(0, "Pound", "Accurate", "combaticons_13"),
        COMBAT_STYLE(1, "Pummel", "Aggressive", "combaticons_11"),
        COMBAT_STYLE(2, "Spike", "Controlled", "combaticons_12"),
        COMBAT_STYLE(3, "Block", "Defensive", "combaticons_10"),
    }},
    {17, {
        COMBAT_STYLE(0, "Stab", "Accurate", "combaticons_7"),
        COMBAT_STYLE(1, "Lunge", "Aggressive", "combaticons_6"),
        COMBAT_STYLE(2, "Slash", "Controlled", "combaticons_5"),
        COMBAT_STYLE(3, "Block", "Defensive", "combaticons_4"),
    }},
    {18, {
        COMBAT_STYLE(0, "Bash", "Accurate", "combaticons2_13"),
        COMBAT_STYLE(1, "Pound", "Aggressive", "combaticons2_14"),
        COMBAT_STYLE(3, "Focus", "Defensive", "combaticons_19"),
        COMBAT_STYLE_HIDDEN,
    }},
    {19, {
        COMBAT_STYLE(0, "Accurate", "Accurate", "combaticons2_10"),
        COMBAT_STYLE(1, "Rapid", "Rapid", "combaticons2_11"),
        COMBAT_STYLE(3, "Longrange", "Longrange", "combaticons2_12"),
        COMBAT_STYLE_HIDDEN,
    }},
    {20, {
        COMBAT_STYLE(0, "Flick", "Accurate", "combaticons3_13"),
        COMBAT_STYLE(1, "Lash", "Controlled", "combaticons3_14"),
        COMBAT_STYLE(3, "Deflect", "Defensive", "combaticons3_13"),
        COMBAT_STYLE_HIDDEN,
    }},
    {21, {
        COMBAT_STYLE(0, "Jab", "Accurate", "combaticons2_13"),
        COMBAT_STYLE(1, "Swipe", "Aggressive", "combaticons2_14"),
        COMBAT_STYLE(3, "Fend", "Defensive", "combaticons_19"),
        COMBAT_STYLE_HIDDEN,
    }},
    {22, {
        COMBAT_STYLE(0, "Jab", "Accurate", "combaticons2_13"),
        COMBAT_STYLE(1, "Swipe", "Aggressive", "combaticons2_14"),
        COMBAT_STYLE(3, "Fend", "Defensive", "combaticons_19"),
        COMBAT_STYLE_HIDDEN,
    }},
    {24, {
        COMBAT_STYLE(0, "Accurate", "Accurate", "combaticons2_10"),
        COMBAT_STYLE(1, "Accurate", "Accurate", "combaticons2_10"),
        COMBAT_STYLE(3, "Longrange", "Longrange", "combaticons2_12"),
        COMBAT_STYLE_HIDDEN,
    }},
    {25, {
        COMBAT_STYLE(0, "Lunge", "Controlled", "combaticons_8"),
        COMBAT_STYLE(1, "Swipe", "Controlled", "combaticons_18"),
        COMBAT_STYLE(2, "Pound", "Controlled", "combaticons_9"),
        COMBAT_STYLE(3, "Block", "Defensive", "combaticons_17"),
    }},
    {26, {
        COMBAT_STYLE(0, "Jab", "Controlled", "combaticons3_11"),
        COMBAT_STYLE(1, "Swipe", "Aggressive", "combaticons3_12"),
        COMBAT_STYLE(3, "Fend", "Defensive", "combaticons3_10"),
        COMBAT_STYLE_HIDDEN,
    }},
    {27, {
        COMBAT_STYLE(0, "Pound", "Accurate", "combaticons2_2"),
        COMBAT_STYLE(1, "Pummel", "Aggressive", "combaticons2_3"),
        COMBAT_STYLE(2, "Smash", "Aggressive", "combaticons2_0"),
        COMBAT_STYLE_HIDDEN,
    }},
    {28, {
        COMBAT_STYLE(1, "Pummel", "Aggressive", "combaticons2_1"),
        COMBAT_STYLE(3, "Block", "Defensive", "combaticons2_0"),
        COMBAT_STYLE_HIDDEN,
        COMBAT_STYLE_HIDDEN,
    }},
    {29, {
        COMBAT_STYLE(0, "Accurate", "Accurate", "combaticons2_10"),
        COMBAT_STYLE(1, "Accurate", "Accurate", "combaticons2_10"),
        COMBAT_STYLE(3, "Longrange", "Longrange", "combaticons2_12"),
        COMBAT_STYLE_HIDDEN,
    }},
    {30, {
        COMBAT_STYLE(0, "Stab", "Accurate", "combaticons_7"),
        COMBAT_STYLE(1, "Lunge", "Aggressive", "combaticons_6"),
        COMBAT_STYLE(2, "Pound", "Controlled", "combaticons_5"),
        COMBAT_STYLE(3, "Block", "Defensive", "combaticons_4"),
    }},
};

#undef COMBAT_STYLE
#undef COMBAT_STYLE_HIDDEN

static void copy_text(char *dst, size_t cap, const char *src) {
    if (cap == 0)
        return;
    if (!src)
        src = "";
    snprintf(dst, cap, "%s", src);
}

static const RuneCUiCombatProfile *combat_profile_for_osrs_category(int category) {
    int count = (int)(sizeof(g_combat_profiles) / sizeof(g_combat_profiles[0]));
    for (int i = 0; i < count; i++) {
        if (g_combat_profiles[i].osrs_category == category)
            return &g_combat_profiles[i];
    }
    return &g_combat_profiles[0];
}

static int osrs_combat_category_from_core_weapon(int core_weapon_category) {
    switch (core_weapon_category) {
    case 0: return 0;   /* unarmed */
    case 1: return 10;  /* 2h sword */
    case 2: return 1;   /* axe */
    case 4: return 2;   /* blunt */
    case 5: return 27;  /* bludgeon */
    case 6: return 28;  /* bulwark */
    case 7: return 7;   /* chinchompa/grenade */
    case 8: return 4;   /* claw */
    case 9: return 5;   /* crossbow */
    case 10: return 20; /* whip */
    case 11: return 6;  /* fixed device */
    case 12: return 8;  /* gun */
    case 13: return 11; /* pickaxe */
    case 14: return 12; /* polearm */
    case 15: return 13; /* polestaff */
    case 16: return 24; /* powered staff */
    case 17: return 14; /* scythe */
    case 18: return 9;  /* slash sword */
    case 19: return 15; /* spear */
    case 20: return 16; /* spiked */
    case 21: return 17; /* stab sword */
    case 22: return 18; /* staff */
    case 23: return 19; /* thrown */
    case 24: return 10; /* two-handed sword */
    case 25: return 3;  /* bow */
    case 26: return 6;  /* salamander */
    case 27: return 6;  /* multi-style fallback */
    case 28: return 29; /* powered wand */
    case 29: return 21; /* bladed staff */
    case 30: return 30; /* partisan */
    default: return 0;
    }
}

static const RuneCUiCombatStyleOption *combat_style_option_by_index(
    const RuneCUiState *ui,
    int style_index) {
    if (!ui)
        return NULL;
    for (int i = 0; i < RUNEC_UI_COMBAT_STYLE_COUNT; i++) {
        const RuneCUiCombatStyleOption *option = &ui->combat_styles[i];
        if (option->visible && option->style_index == style_index)
            return option;
    }
    return NULL;
}

static const RuneCUiCombatStyleOption *selected_combat_style_option(
    const RuneCUiState *ui) {
    const RuneCUiCombatStyleOption *option =
        combat_style_option_by_index(ui, ui ? ui->selected_combat_style : 0);
    if (option)
        return option;
    if (ui && ui->selected_combat_style == 2)
        option = combat_style_option_by_index(ui, 3);
    if (option)
        return option;
    if (!ui)
        return NULL;
    for (int i = 0; i < RUNEC_UI_COMBAT_STYLE_COUNT; i++) {
        if (ui->combat_styles[i].visible)
            return &ui->combat_styles[i];
    }
    return NULL;
}

static int combat_style_option_selected(const RuneCUiState *ui,
                                        const RuneCUiCombatStyleOption *option) {
    if (!ui || !option || !option->visible || ui->autocast_slot >= 0)
        return 0;
    if (ui->selected_combat_style == option->style_index)
        return 1;
    return ui->selected_combat_style == 2
        && option->style_index == 3
        && combat_style_option_by_index(ui, 2) == NULL;
}

void runec_ui_set_combat_weapon_name(RuneCUiState *ui, const char *name) {
    if (!ui)
        return;
    copy_text(ui->combat_weapon_name, sizeof(ui->combat_weapon_name),
              name && name[0] ? name : "Unarmed");
}

void runec_ui_set_combat_style_profile(RuneCUiState *ui, int core_weapon_category) {
    if (!ui)
        return;
    ui->combat_weapon_category = core_weapon_category;
    int osrs_category = osrs_combat_category_from_core_weapon(core_weapon_category);
    const RuneCUiCombatProfile *profile =
        combat_profile_for_osrs_category(osrs_category);
    for (int i = 0; i < RUNEC_UI_COMBAT_STYLE_COUNT; i++) {
        const RuneCUiCombatStyleDef *src = &profile->styles[i];
        RuneCUiCombatStyleOption *dst = &ui->combat_styles[i];
        dst->visible = src->visible;
        dst->style_index = src->style_index;
        copy_text(dst->label, sizeof(dst->label), src->label);
        copy_text(dst->mode, sizeof(dst->mode), src->mode);
        copy_text(dst->icon_asset, sizeof(dst->icon_asset), src->icon_asset);
    }
    if (!selected_combat_style_option(ui)) {
        for (int i = 0; i < RUNEC_UI_COMBAT_STYLE_COUNT; i++) {
            if (ui->combat_styles[i].visible) {
                ui->selected_combat_style = ui->combat_styles[i].style_index;
                break;
            }
        }
    }
}

static float fmin2(float a, float b) {
    return a < b ? a : b;
}

static void ui_add_chat(RuneCUiState *ui, const char *text) {
    if (ui->chat_line_count < RUNEC_UI_CHAT_LINES)
        ui->chat_line_count++;
    for (int i = ui->chat_line_count - 1; i > 0; i--)
        copy_text(ui->chat_lines[i], sizeof(ui->chat_lines[i]), ui->chat_lines[i - 1]);
    copy_text(ui->chat_lines[0], sizeof(ui->chat_lines[0]), text);
}

void runec_ui_add_chat_message(RuneCUiState *ui, const char *text) {
    if (!ui || !text || !text[0])
        return;
    ui_add_chat(ui, text);
}

static int decoded_group_available(const RuneCUiState *ui, const char *group) {
    return ui && group && group[0]
        && runec_ui_interface_group(&ui->interfaces, group) != NULL;
}

static int local_open_modal(RuneCUiState *ui, const char *group) {
    if (!decoded_group_available(ui, group))
        return 0;
    return runec_ui_open_subinterface(ui, RUNEC_UI_MOUNT_MODAL,
                                      RUNEC_UI_TAB_NONE,
                                      RUNEC_UI_TOP_MAIN_MODAL, group);
}

static int local_open_side_overlay(RuneCUiState *ui, const char *group) {
    if (!decoded_group_available(ui, group))
        return 0;
    return runec_ui_open_subinterface(ui, RUNEC_UI_MOUNT_OVERLAY,
                                      RUNEC_UI_TAB_NONE,
                                      RUNEC_UI_TOP_SIDE_MODAL, group);
}

static int local_handler_magic_filter(RuneCUiState *ui,
                                      const RuneCUiListenerEvent *event,
                                      const RuneCUiHitResult *hit) {
    (void)hit;
    if (!ui || !event)
        return 0;
    if (event->kind == RUNEC_UI_LISTENER_ON_LOAD)
        ui->magic_filter_open = 0;
    else
        ui->magic_filter_open = !ui->magic_filter_open;
    runec_ui_set_component_hidden(ui, RUNEC_UI_MAGIC_FILTER_CONTAINER,
                                  !ui->magic_filter_open);
    return 1;
}

static int local_handler_equipment_stats(RuneCUiState *ui,
                                         const RuneCUiListenerEvent *event,
                                         const RuneCUiHitResult *hit) {
    (void)event;
    (void)hit;
    return local_open_modal(ui, "equipment");
}

static int local_handler_price_checker(RuneCUiState *ui,
                                       const RuneCUiListenerEvent *event,
                                       const RuneCUiHitResult *hit) {
    (void)event;
    (void)hit;
    return local_open_side_overlay(ui, "ge_pricechecker_side");
}

static int local_handler_death_keep(RuneCUiState *ui,
                                    const RuneCUiListenerEvent *event,
                                    const RuneCUiHitResult *hit) {
    (void)event;
    (void)hit;
    return local_open_modal(ui, "deathkeep");
}

static int local_handler_call_follower(RuneCUiState *ui,
                                       const RuneCUiListenerEvent *event,
                                       const RuneCUiHitResult *hit) {
    (void)event;
    (void)hit;
    ui_add_chat(ui, "You do not have a follower to call.");
    return 1;
}

static const RuneCUiLocalListenerHandler g_local_listener_handlers[] = {
    {RUNEC_UI_MAGIC_FILTER_CONTAINER, RUNEC_UI_LISTENER_ON_LOAD,
     local_handler_magic_filter},
    {RUNEC_UI_MAGIC_FILTER_BUTTON, RUNEC_UI_LISTENER_ON_CLICK,
     local_handler_magic_filter},
    {RUNEC_UI_WORN_EQUIPMENT_BUTTON, RUNEC_UI_LISTENER_ON_OP,
     local_handler_equipment_stats},
    {RUNEC_UI_WORN_PRICE_BUTTON, RUNEC_UI_LISTENER_ON_OP,
     local_handler_price_checker},
    {RUNEC_UI_WORN_DEATH_BUTTON, RUNEC_UI_LISTENER_ON_OP,
     local_handler_death_keep},
    {RUNEC_UI_WORN_FOLLOWER_BUTTON, RUNEC_UI_LISTENER_ON_OP,
     local_handler_call_follower},
};

static int dispatch_local_listener(RuneCUiState *ui,
                                   const RuneCUiListenerEvent *event,
                                   const RuneCUiHitResult *hit) {
    if (!ui || !event || event->component_id == 0)
        return 0;
    int count = (int)(sizeof(g_local_listener_handlers)
                      / sizeof(g_local_listener_handlers[0]));
    for (int i = 0; i < count; i++) {
        const RuneCUiLocalListenerHandler *entry = &g_local_listener_handlers[i];
        if (entry->component_id != event->component_id
                || entry->kind != event->kind || !entry->handler) {
            continue;
        }
        if (entry->handler(ui, event, hit))
            return 1;
    }
    return 0;
}

static int dispatch_local_listener_for_hit(RuneCUiState *ui,
                                           const RuneCUiHitResult *hit,
                                           RuneCUiListenerKind kind,
                                           int op,
                                           Vector2 mouse) {
    if (!hit)
        return 0;
    RuneCUiListenerEvent event = {
        .kind = kind,
        .component_id = hit->component_id,
        .op = op,
        .mouse = mouse,
    };
    return dispatch_local_listener(ui, &event, hit);
}

static void dispatch_local_group_listeners(RuneCUiState *ui,
                                           const char *group_name,
                                           RuneCUiListenerKind kind) {
    if (!ui || !group_name)
        return;
    const RuneCUiInterfaceGroup *group =
        runec_ui_interface_group(&ui->interfaces, group_name);
    if (!group)
        return;
    uint32_t bit = 1u << (unsigned)kind;
    for (int i = 0; i < group->component_count; i++) {
        const RuneCUiComponent *component = &group->components[i];
        if ((component->listener_mask & bit) == 0)
            continue;
        RuneCUiListenerEvent event = {
            .kind = kind,
            .component_id = component->id,
            .op = -1,
            .mouse = {0},
        };
        dispatch_local_listener(ui, &event, NULL);
    }
}

static void dispatch_local_transmit_listeners(RuneCUiState *ui) {
    if (!ui || !ui->decoded_ui_enabled || !ui->decoded_ui_ready)
        return;
    for (int i = 0; i < ui->open_interface_count; i++) {
        const RuneCUiOpenInterface *open = &ui->open_interfaces[i];
        if (!open->active || !open->group[0])
            continue;
        dispatch_local_group_listeners(ui, open->group,
                                       RUNEC_UI_LISTENER_ON_VAR_TRANSMIT);
        dispatch_local_group_listeners(ui, open->group,
                                       RUNEC_UI_LISTENER_ON_INV_TRANSMIT);
        dispatch_local_group_listeners(ui, open->group,
                                       RUNEC_UI_LISTENER_ON_STAT_TRANSMIT);
    }
}

const char *runec_ui_tab_name(RuneCUiTab tab) {
    switch (tab) {
    case RUNEC_UI_TAB_COMBAT: return "Combat";
    case RUNEC_UI_TAB_SKILLS: return "Skills";
    case RUNEC_UI_TAB_QUESTS: return "Quests";
    case RUNEC_UI_TAB_INVENTORY: return "Inventory";
    case RUNEC_UI_TAB_EQUIPMENT: return "Equipment";
    case RUNEC_UI_TAB_PRAYER: return "Prayer";
    case RUNEC_UI_TAB_SPELLBOOK: return "Spellbook";
    case RUNEC_UI_TAB_SETTINGS: return "Settings";
    case RUNEC_UI_TAB_CLAN: return "Clan";
    default: return "Unknown";
    }
}

static void ui_layout(int screen_w, int screen_h, RuneCUiLayout *out) {
    memset(out, 0, sizeof(*out));

    out->chat = (Rectangle){0, (float)screen_h - RUNEC_OSRS_CHAT_H,
                            RUNEC_OSRS_CHAT_W, RUNEC_OSRS_CHAT_H};
    out->chat_messages = (Rectangle){7, out->chat.y + 7, 506, 126};
    out->chat_controls = (Rectangle){0, out->chat.y + 142, 519, 23};
    out->chat_input = (Rectangle){8, out->chat.y + 124, 500, 17};

    out->map = (Rectangle){(float)screen_w - RUNEC_OSRS_MAP_CONTAINER_W, 0,
                           RUNEC_OSRS_MAP_CONTAINER_W, RUNEC_OSRS_MAP_CONTAINER_H};
    out->minimap = (Rectangle){out->map.x + RUNEC_OSRS_MINIMAP_X,
                               out->map.y + RUNEC_OSRS_MINIMAP_Y,
                               RUNEC_OSRS_MINIMAP_W, RUNEC_OSRS_MINIMAP_H};
    out->compass = (Rectangle){out->map.x + RUNEC_OSRS_COMPASS_X,
                               out->map.y + RUNEC_OSRS_COMPASS_Y,
                               RUNEC_OSRS_COMPASS_W, RUNEC_OSRS_COMPASS_H};
    out->xp_orb = (Rectangle){out->map.x + RUNEC_OSRS_ORBS_X + RUNEC_OSRS_XP_X,
                              out->map.y + RUNEC_OSRS_ORBS_Y + RUNEC_OSRS_XP_Y, 27, 27};
    out->hp_orb = (Rectangle){out->map.x + RUNEC_OSRS_ORBS_X + RUNEC_OSRS_HP_X,
                              out->map.y + RUNEC_OSRS_ORBS_Y + RUNEC_OSRS_HP_Y, 57, 34};
    out->prayer_orb = (Rectangle){out->map.x + RUNEC_OSRS_ORBS_X + RUNEC_OSRS_PRAYER_X,
                                  out->map.y + RUNEC_OSRS_ORBS_Y + RUNEC_OSRS_PRAYER_Y, 57, 34};
    out->run_orb = (Rectangle){out->map.x + RUNEC_OSRS_ORBS_X + RUNEC_OSRS_RUN_X,
                               out->map.y + RUNEC_OSRS_ORBS_Y + RUNEC_OSRS_RUN_Y, 57, 34};
    out->spec_orb = (Rectangle){out->map.x + RUNEC_OSRS_ORBS_X + RUNEC_OSRS_SPEC_X,
                                out->map.y + RUNEC_OSRS_ORBS_Y + RUNEC_OSRS_SPEC_Y, 57, 34};
    out->worldmap_button = (Rectangle){out->map.x + RUNEC_OSRS_ORBS_X + RUNEC_OSRS_WORLDMAP_X,
                                       out->map.y + RUNEC_OSRS_ORBS_Y + RUNEC_OSRS_WORLDMAP_Y,
                                       30, 30};
    out->side = (Rectangle){(float)screen_w - RUNEC_OSRS_SIDE_MENU_W,
                            (float)screen_h - RUNEC_OSRS_SIDE_MENU_H,
                            RUNEC_OSRS_SIDE_MENU_W, RUNEC_OSRS_SIDE_MENU_H};
    out->side_bg = out->side;
    out->side_content = (Rectangle){out->side.x + RUNEC_OSRS_SIDE_CONTENT_X,
                                    out->side.y + RUNEC_OSRS_SIDE_CONTENT_Y,
                                    RUNEC_OSRS_SIDE_CONTENT_W,
                                    RUNEC_OSRS_SIDE_CONTENT_H};

    for (int i = 0; i < (int)(sizeof(RUNEC_OSRS_SIDE_STONES) / sizeof(RUNEC_OSRS_SIDE_STONES[0])); i++) {
        const RuneCUiStoneRef *ref = &RUNEC_OSRS_SIDE_STONES[i];
        if (ref->logical_tab < 0 || ref->logical_tab >= RUNEC_UI_TAB_COUNT)
            continue;
        float row_y = out->side.y + (i < 7 ? RUNEC_OSRS_SIDE_TOP_Y : RUNEC_OSRS_SIDE_BOTTOM_Y);
        out->tab[ref->logical_tab] =
            (Rectangle){out->side.x + ref->rect.x, row_y + ref->rect.y,
                        ref->rect.width, ref->rect.height};
    }
}

static void apply_decoded_mounts(RuneCUiState *ui, int screen_w, int screen_h,
                                 RuneCUiLayout *layout) {
    if (!ui->decoded_ui_enabled || !ui->decoded_ui_ready)
        return;
    const char *top_group =
        open_group_for_mount(ui, RUNEC_UI_MOUNT_SCREEN, RUNEC_UI_TAB_NONE);
    if (!top_group)
        top_group = "toplevel_osrs_stretch";
    Rectangle screen = {0, 0, (float)screen_w, (float)screen_h};
    Rectangle rect = {0};
    if (runec_ui_interfaces_component_rect_by_id(&ui->interfaces, top_group,
            RUNEC_UI_TOP_CHAT_CONTAINER, screen, &rect)) {
        layout->chat = rect;
        layout->chat_messages = (Rectangle){rect.x + 7, rect.y + 7, 506, 126};
        layout->chat_controls = (Rectangle){rect.x, rect.y + 142, 519, 23};
        layout->chat_input = (Rectangle){rect.x + 8, rect.y + 124, 500, 17};
    }
    if (runec_ui_interfaces_component_rect_by_id(&ui->interfaces, top_group,
            RUNEC_UI_TOP_MAP_CONTAINER, screen, &rect)) {
        layout->map = rect;
        layout->minimap = (Rectangle){rect.x + RUNEC_OSRS_MINIMAP_X,
                                      rect.y + RUNEC_OSRS_MINIMAP_Y,
                                      RUNEC_OSRS_MINIMAP_W,
                                      RUNEC_OSRS_MINIMAP_H};
        layout->compass = (Rectangle){rect.x + RUNEC_OSRS_COMPASS_X,
                                      rect.y + RUNEC_OSRS_COMPASS_Y,
                                      RUNEC_OSRS_COMPASS_W,
                                      RUNEC_OSRS_COMPASS_H};
        layout->xp_orb = (Rectangle){rect.x + RUNEC_OSRS_ORBS_X + RUNEC_OSRS_XP_X,
                                     rect.y + RUNEC_OSRS_ORBS_Y + RUNEC_OSRS_XP_Y, 27, 27};
        layout->hp_orb = (Rectangle){rect.x + RUNEC_OSRS_ORBS_X + RUNEC_OSRS_HP_X,
                                     rect.y + RUNEC_OSRS_ORBS_Y + RUNEC_OSRS_HP_Y, 57, 34};
        layout->prayer_orb = (Rectangle){rect.x + RUNEC_OSRS_ORBS_X + RUNEC_OSRS_PRAYER_X,
                                         rect.y + RUNEC_OSRS_ORBS_Y + RUNEC_OSRS_PRAYER_Y, 57, 34};
        layout->run_orb = (Rectangle){rect.x + RUNEC_OSRS_ORBS_X + RUNEC_OSRS_RUN_X,
                                      rect.y + RUNEC_OSRS_ORBS_Y + RUNEC_OSRS_RUN_Y, 57, 34};
        layout->spec_orb = (Rectangle){rect.x + RUNEC_OSRS_ORBS_X + RUNEC_OSRS_SPEC_X,
                                       rect.y + RUNEC_OSRS_ORBS_Y + RUNEC_OSRS_SPEC_Y, 57, 34};
        layout->worldmap_button =
            (Rectangle){rect.x + RUNEC_OSRS_ORBS_X + RUNEC_OSRS_WORLDMAP_X,
                        rect.y + RUNEC_OSRS_ORBS_Y + RUNEC_OSRS_WORLDMAP_Y, 30, 30};
    }
    if (runec_ui_interfaces_component_rect(&ui->interfaces, top_group,
            "side_menu", screen, &rect)) {
        layout->side = rect;
        layout->side_bg = rect;
        for (int i = 0; i < (int)(sizeof(RUNEC_OSRS_SIDE_STONES) / sizeof(RUNEC_OSRS_SIDE_STONES[0])); i++) {
            const RuneCUiStoneRef *ref = &RUNEC_OSRS_SIDE_STONES[i];
            if (ref->logical_tab < 0 || ref->logical_tab >= RUNEC_UI_TAB_COUNT)
                continue;
            float row_y = rect.y + (i < 7 ? RUNEC_OSRS_SIDE_TOP_Y : RUNEC_OSRS_SIDE_BOTTOM_Y);
            layout->tab[ref->logical_tab] =
                (Rectangle){rect.x + ref->rect.x, row_y + ref->rect.y,
                            ref->rect.width, ref->rect.height};
        }
    }
    if (runec_ui_interfaces_component_rect_by_id(&ui->interfaces, top_group,
            RUNEC_UI_TOP_SIDE_CONTAINER, screen, &rect)) {
        layout->side_content = rect;
    }
}

static int mouse_over_ui(const RuneCUiLayout *layout, Vector2 mouse) {
    if (CheckCollisionPointRec(mouse, layout->chat)
        || CheckCollisionPointRec(mouse, layout->side)
        || CheckCollisionPointRec(mouse, layout->map))
        return 1;
    for (int i = 0; i < RUNEC_UI_TAB_COUNT; i++) {
        if (CheckCollisionPointRec(mouse, layout->tab[i]))
            return 1;
    }
    return 0;
}

static void clear_intent(RuneCUiState *ui) {
    memset(&ui->last_intent, 0, sizeof(ui->last_intent));
}

static void close_context(RuneCUiState *ui) {
    ui->context_open = 0;
    ui->context_dismissed = 1;
    ui->context_source_kind = RUNEC_UI_CONTEXT_NONE;
    ui->context_source_slot = -1;
    ui->context_source_item_id = 0;
    ui->context_source_component_id = 0;
}

static void set_context(RuneCUiState *ui, Vector2 pos, const char *title,
                        const char **actions, int action_count) {
    ui->context_open = 1;
    ui->context_pos = pos;
    copy_text(ui->context_title, sizeof(ui->context_title), title);
    ui->context_source_kind = RUNEC_UI_CONTEXT_NONE;
    ui->context_source_slot = -1;
    ui->context_source_item_id = 0;
    ui->context_source_component_id = 0;
    if (action_count > RUNEC_UI_CONTEXT_ACTIONS)
        action_count = RUNEC_UI_CONTEXT_ACTIONS;
    ui->context_action_count = action_count;
    for (int i = 0; i < action_count; i++) {
        copy_text(ui->context_actions[i], sizeof(ui->context_actions[i]), actions[i]);
        copy_text(ui->context_targets[i], sizeof(ui->context_targets[i]), title);
        ui->context_target_colors[i] = OSRS_ORANGE;
        ui->context_action_op[i] = i;
    }
}

static void set_context_source(RuneCUiState *ui,
                               RuneCUiContextSourceKind source_kind,
                               int source_slot,
                               uint32_t source_item_id) {
    ui->context_source_kind = source_kind;
    ui->context_source_slot = source_slot;
    ui->context_source_item_id = source_item_id;
    ui->context_source_component_id = 0;
}

static void set_context_source_component(RuneCUiState *ui,
                                         uint32_t component_id) {
    ui->context_source_kind = RUNEC_UI_CONTEXT_COMPONENT;
    ui->context_source_slot = -1;
    ui->context_source_item_id = 0;
    ui->context_source_component_id = component_id;
}

static void set_context_action_op(RuneCUiState *ui, int action_index, int op) {
    if (!ui || action_index < 0 || action_index >= ui->context_action_count
            || action_index >= RUNEC_UI_CONTEXT_ACTIONS)
        return;
    ui->context_action_op[action_index] = op;
}

static int context_action_has_target(const RuneCUiState *ui,
                                     int index) {
    const char *action = ui->context_actions[index];
    return ui->context_targets[index][0] && action[0]
        && strcmp(action, "Cancel") != 0
        && strcmp(action, "Walk here") != 0;
}

static void context_action_text(const RuneCUiState *ui, int action_index,
                                char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!ui || action_index < 0
            || action_index >= ui->context_action_count) return;
    const char *action = ui->context_actions[action_index];
    if (context_action_has_target(ui, action_index)) {
        snprintf(out, out_size, "%s %s", action, ui->context_targets[action_index]);
    } else {
        copy_text(out, out_size, action);
    }
}

static RuneCContextMenuLayout context_menu_layout_for_ui(
    const RuneCUiState *ui) {
    Font font = runec_ui_bold_font(&ui->assets);
    float width = MeasureTextEx(
        font, "Choose Option", RUNEC_CONTEXT_MENU_FONT_SIZE, 0.0f).x;
    for (int i = 0; i < ui->context_action_count; i++) {
        char text[RUNEC_UI_CONTEXT_TEXT_MAX * 2 + 2];
        context_action_text(ui, i, text, sizeof(text));
        float action_width = MeasureTextEx(
            font, text, RUNEC_CONTEXT_MENU_FONT_SIZE, 0.0f).x;
        if (action_width > width) width = action_width;
    }
    return runec_context_menu_layout(
        (int)ui->context_pos.x, (int)ui->context_pos.y,
        GetScreenWidth(), GetScreenHeight(), (int)ceilf(width),
        ui->context_action_count);
}

static void set_item_slot_context(RuneCUiState *ui, Vector2 pos,
                                  const RuneCUiSlot *slot) {
    const char *actions[RUNEC_UI_ITEM_ACTIONS + 1];
    int count = slot ? slot->action_count : 0;
    if (count > RUNEC_UI_ITEM_ACTIONS) count = RUNEC_UI_ITEM_ACTIONS;
    for (int i = 0; i < count; i++) actions[i] = slot->actions[i];
    actions[count] = "Cancel";
    set_context(ui, pos, slot ? slot->label : "Item", actions, count + 1);
    for (int i = 0; slot && i < count; i++)
        set_context_action_op(ui, i, slot->action_ops[i]);
    for (int i = 0; i < count; i++) ui->context_target_colors[i] = OSRS_ITEM_TARGET;
}

enum { BANK_AMOUNT_X = -10, BANK_ALL_BUT_ONE = -11 };
static const struct {
    const char *withdraw, *deposit;
    int amount;
} bank_actions[] = {
    {"Withdraw-1", "Deposit-1", 1},
    {"Withdraw-5", "Deposit-5", 5},
    {"Withdraw-10", "Deposit-10", 10},
    {"Withdraw-X", "Deposit-X", BANK_AMOUNT_X},
    {"Withdraw-All", "Deposit-All", 0},
    {"Withdraw-All-but-1", NULL, BANK_ALL_BUT_ONE},
    {"Examine", "Examine", RUNEC_UI_ITEM_OP_EXAMINE},
};

static const RuneCUiSlot *bank_source_slot(const RuneCUiState *ui,
                                          RuneCUiContextSourceKind kind, int slot) {
    if (!ui->bank_open || slot < 0) return NULL;
    if (kind == RUNEC_UI_CONTEXT_BANK && slot < RUNEC_UI_BANK_SLOT_COUNT)
        return &ui->bank[slot];
    if (kind == RUNEC_UI_CONTEXT_BANK_INVENTORY && slot < RUNEC_UI_INV_SLOT_COUNT)
        return &ui->inventory[slot];
    return NULL;
}

static void set_bank_context(RuneCUiState *ui, Vector2 pos,
                             RuneCUiContextSourceKind kind, int slot) {
    const RuneCUiSlot *item = bank_source_slot(ui, kind, slot);
    if (!item || !item->enabled) return;
    const char *actions[RUNEC_UI_CONTEXT_ACTIONS];
    int ops[RUNEC_UI_CONTEXT_ACTIONS], count = 0;
    for (unsigned i = 0; i < sizeof(bank_actions)/sizeof(bank_actions[0]); i++) {
        const char *action = kind == RUNEC_UI_CONTEXT_BANK
            ? bank_actions[i].withdraw : bank_actions[i].deposit;
        if (action) { actions[count] = action; ops[count++] = bank_actions[i].amount; }
    }
    actions[count] = "Cancel";
    set_context(ui, pos, item->label, actions, count + 1);
    for (int i = 0; i < count; i++) {
        set_context_action_op(ui, i, ops[i]);
        ui->context_target_colors[i] = OSRS_ITEM_TARGET;
    }
    set_context_source(ui, kind, slot, item->item_id);
    ui->context_source_generation = item->generation;
    runec_ui_clear_selected_target(ui);
    ui->drag.active = 0;
}

static void submit_bank_action(RuneCUiState *ui, RuneCUiContextSourceKind kind,
                                int slot, uint32_t id, uint32_t generation, int amount) {
    const RuneCUiSlot *item = bank_source_slot(ui, kind, slot);
    if (!item || !item->enabled || item->item_id != id || item->generation != generation) {
        ui_add_chat(ui, "That item changed. Choose it again.");
        return;
    }
    if (amount == BANK_AMOUNT_X) {
        ui->bank_amount_source = kind;
        ui->bank_amount_slot = slot;
        ui->bank_amount_item_id = id;
        ui->bank_amount_generation = generation;
        ui->bank_amount_text[0] = 0;
        ui->chat_focused = 0;
        return;
    }
    if (amount == RUNEC_UI_ITEM_OP_EXAMINE) {
        ui->last_intent.kind = RUNEC_UI_INTENT_BANK_EXAMINE;
        ui->last_intent.primary = (int)id;
        return;
    }
    int keep_one = amount == BANK_ALL_BUT_ONE;
    if (keep_one && item->quantity <= 1) { ui_add_chat(ui, "There is only one item left."); return; }
    ui->last_intent.kind = kind == RUNEC_UI_CONTEXT_BANK
        ? (keep_one ? RUNEC_UI_INTENT_BANK_WITHDRAW_ALL_BUT_ONE : RUNEC_UI_INTENT_BANK_WITHDRAW)
        : RUNEC_UI_INTENT_BANK_DEPOSIT;
    ui->last_intent.primary = slot;
    ui->last_intent.secondary = keep_one ? 0 : amount;
}

static int handle_bank_amount_input(RuneCUiState *ui) {
    if (!ui->bank_amount_source) return 0;
    if (!ui->bank_open || IsKeyPressed(KEY_ESCAPE)) {
        ui->bank_amount_source = RUNEC_UI_CONTEXT_NONE;
        return 1;
    }
    int ch;
    while ((ch = GetCharPressed()) > 0) {
        size_t len = strlen(ui->bank_amount_text);
        if (ch >= '0' && ch <= '9' && len + 1 < sizeof(ui->bank_amount_text)) {
            ui->bank_amount_text[len] = (char)ch;
            ui->bank_amount_text[len + 1] = 0;
        }
    }
    size_t len = strlen(ui->bank_amount_text);
    if (IsKeyPressed(KEY_BACKSPACE) && len) ui->bank_amount_text[len - 1] = 0;
    if (IsKeyPressed(KEY_ENTER)) {
        long long amount = strtoll(ui->bank_amount_text, NULL, 10);
        if (amount <= 0 || amount > INT_MAX) {
            ui_add_chat(ui, "Enter an amount from 1 to 2147483647.");
            return 1;
        }
        submit_bank_action(ui, ui->bank_amount_source, ui->bank_amount_slot,
            ui->bank_amount_item_id, ui->bank_amount_generation, (int)amount);
        ui->bank_amount_source = RUNEC_UI_CONTEXT_NONE;
    }
    return 1;
}

void runec_ui_open_context(RuneCUiState *ui, Vector2 pos, const char *title,
                           const char **actions, int action_count) {
    if (!ui || !actions || action_count <= 0)
        return;
    set_context(ui, pos, title, actions, action_count);
}

void runec_ui_open_context_targeted(
    RuneCUiState *ui, Vector2 pos, const char *title, Color target_color,
    const char **actions, int action_count) {
    if (!ui || !actions || action_count <= 0)
        return;
    set_context(ui, pos, title, actions, action_count);
    for (int i = 0; i < ui->context_action_count; i++)
        ui->context_target_colors[i] = target_color;
}

void runec_ui_clear_selected_target(RuneCUiState *ui) {
    if (!ui)
        return;
    memset(&ui->selected_target, 0, sizeof(ui->selected_target));
    ui->selected_target.source_slot = -1;
}

static RuneCUiComponentOverride *component_override_slot(
    RuneCUiState *ui,
    uint32_t component_id,
    int create) {
    if (!ui || component_id == 0)
        return NULL;
    for (int i = 0; i < ui->component_override_count; i++) {
        if (ui->component_overrides[i].component_id == component_id)
            return &ui->component_overrides[i];
    }
    if (!create || ui->component_override_count >= RUNEC_UI_COMPONENT_OVERRIDES)
        return NULL;
    RuneCUiComponentOverride *override =
        &ui->component_overrides[ui->component_override_count++];
    memset(override, 0, sizeof(*override));
    override->component_id = component_id;
    return override;
}

void runec_ui_clear_component_overrides(RuneCUiState *ui) {
    if (!ui)
        return;
    ui->component_override_count = 0;
    ui->item_container_override_count = 0;
}

static RuneCUiItemContainerOverride *item_container_override_slot(
    RuneCUiState *ui,
    uint32_t component_id,
    int create) {
    if (!ui || component_id == 0)
        return NULL;
    for (int i = 0; i < ui->item_container_override_count; i++) {
        if (ui->item_container_overrides[i].component_id == component_id)
            return &ui->item_container_overrides[i];
    }
    if (!create
            || ui->item_container_override_count >= RUNEC_UI_ITEM_CONTAINER_OVERRIDES)
        return NULL;
    RuneCUiItemContainerOverride *override =
        &ui->item_container_overrides[ui->item_container_override_count++];
    memset(override, 0, sizeof(*override));
    override->component_id = component_id;
    override->selected_slot = -1;
    return override;
}

static RuneCUiItemContainerOverride *set_item_container_override(
    RuneCUiState *ui,
    uint32_t component_id,
    int slot_count,
    int columns,
    float x0,
    float y0,
    float step_x,
    float step_y,
    float slot_w,
    float slot_h,
    int selected_slot) {
    RuneCUiItemContainerOverride *override =
        item_container_override_slot(ui, component_id, 1);
    if (!override)
        return NULL;
    if (slot_count > RUNEC_UI_ITEM_CONTAINER_MAX_SLOTS)
        slot_count = RUNEC_UI_ITEM_CONTAINER_MAX_SLOTS;
    memset(override->slots, 0, sizeof(override->slots));
    override->slot_count = slot_count;
    override->columns = columns;
    override->x0 = x0;
    override->y0 = y0;
    override->step_x = step_x;
    override->step_y = step_y;
    override->slot_w = slot_w;
    override->slot_h = slot_h;
    override->selected_slot = selected_slot;
    return override;
}

int runec_ui_set_component_text(RuneCUiState *ui, uint32_t component_id,
                                const char *text) {
    RuneCUiComponentOverride *override =
        component_override_slot(ui, component_id, 1);
    if (!override)
        return 0;
    override->flags |= RUNEC_UI_COMPONENT_OVERRIDE_TEXT;
    copy_text(override->text, sizeof(override->text), text);
    return 1;
}

int runec_ui_set_component_hidden(RuneCUiState *ui, uint32_t component_id,
                                  int hidden) {
    RuneCUiComponentOverride *override =
        component_override_slot(ui, component_id, 1);
    if (!override)
        return 0;
    override->flags |= RUNEC_UI_COMPONENT_OVERRIDE_HIDDEN;
    override->hidden = hidden ? 1 : 0;
    return 1;
}

int runec_ui_set_component_model(RuneCUiState *ui, uint32_t component_id,
                                 int model_type, int model_id) {
    RuneCUiComponentOverride *override =
        component_override_slot(ui, component_id, 1);
    if (!override)
        return 0;
    override->flags |= RUNEC_UI_COMPONENT_OVERRIDE_MODEL;
    override->model_type = model_type;
    override->model_id = model_id;
    return 1;
}

int runec_ui_set_component_item(RuneCUiState *ui, uint32_t component_id,
                                uint32_t item_id, uint32_t icon_item_id,
                                int quantity, int selected) {
    RuneCUiComponentOverride *override =
        component_override_slot(ui, component_id, 1);
    if (!override)
        return 0;
    override->flags |= RUNEC_UI_COMPONENT_OVERRIDE_ITEM;
    override->item_id = (int32_t)item_id;
    override->icon_item_id = (int32_t)(icon_item_id ? icon_item_id : item_id);
    override->item_quantity = quantity > 0 ? quantity : 1;
    override->selected = selected ? 1 : 0;
    return 1;
}

int runec_ui_set_component_animation(RuneCUiState *ui, uint32_t component_id,
                                     int animation_id) {
    RuneCUiComponentOverride *override =
        component_override_slot(ui, component_id, 1);
    if (!override)
        return 0;
    override->flags |= RUNEC_UI_COMPONENT_OVERRIDE_ANIM;
    override->animation_id = animation_id;
    return 1;
}

int runec_ui_set_component_color(RuneCUiState *ui, uint32_t component_id,
                                 int color) {
    RuneCUiComponentOverride *override =
        component_override_slot(ui, component_id, 1);
    if (!override)
        return 0;
    override->flags |= RUNEC_UI_COMPONENT_OVERRIDE_COLOR;
    override->text_color = color;
    return 1;
}

int runec_ui_set_component_scroll(RuneCUiState *ui, uint32_t component_id,
                                  int scroll_x, int scroll_y) {
    RuneCUiComponentOverride *override =
        component_override_slot(ui, component_id, 1);
    if (!override)
        return 0;
    override->flags |= RUNEC_UI_COMPONENT_OVERRIDE_SCROLL;
    override->scroll_x = scroll_x;
    override->scroll_y = scroll_y;
    return 1;
}

static void set_selected_item_target(RuneCUiState *ui, int slot) {
    if (!ui || slot < 0 || slot >= RUNEC_UI_INV_SLOT_COUNT
            || !ui->inventory[slot].enabled)
        return;
    ui->selected_target.kind = RUNEC_UI_SELECTED_ITEM;
    ui->selected_target.source_slot = slot;
    ui->selected_target.source_item_id = ui->inventory[slot].item_id;
    ui->selected_target.source_component_id = 0;
    copy_text(ui->selected_target.label, sizeof(ui->selected_target.label),
              ui->inventory[slot].label);
    copy_text(ui->selected_target.verb, sizeof(ui->selected_target.verb), "Use");
}

static void set_selected_spell_target(RuneCUiState *ui, int slot,
                                      const char *name) {
    if (!ui || slot < 0)
        return;
    ui->selected_target.kind = RUNEC_UI_SELECTED_SPELL;
    ui->selected_target.source_slot = slot;
    ui->selected_target.source_item_id = 0;
    ui->selected_target.source_component_id = 0;
    snprintf(ui->selected_target.label, sizeof(ui->selected_target.label),
             "%.47s", name ? name : "");
    copy_text(ui->selected_target.verb, sizeof(ui->selected_target.verb), "Cast");
}

static void set_component_event_mask(RuneCUiState *ui, uint32_t component_id,
                                     uint32_t mask) {
    if (!ui || component_id == 0 || mask == 0)
        return;
    for (int i = 0; i < ui->event_mask_count; i++) {
        if (ui->event_masks[i].component_id == component_id) {
            ui->event_masks[i].mask = mask;
            return;
        }
    }
    if (ui->event_mask_count >= RUNEC_UI_EVENT_MASKS)
        return;
    ui->event_masks[ui->event_mask_count++] =
        (RuneCUiComponentEventMask){component_id, mask};
}

static void seed_event_masks_for_group(RuneCUiState *ui, const char *group_name) {
    if (!ui || !ui->decoded_ui_ready || !group_name)
        return;
    const RuneCUiInterfaceGroup *group =
        runec_ui_interface_group(&ui->interfaces, group_name);
    if (!group)
        return;
    for (int i = 0; i < group->component_count; i++) {
        const RuneCUiComponent *component = &group->components[i];
        set_component_event_mask(ui, component->id, component->click_mask);
    }
}

static uint32_t event_mask_for_component(const RuneCUiState *ui,
                                         uint32_t component_id,
                                         uint32_t fallback) {
    if (!ui || component_id == 0)
        return fallback;
    for (int i = 0; i < ui->event_mask_count; i++) {
        if (ui->event_masks[i].component_id == component_id)
            return ui->event_masks[i].mask;
    }
    return fallback;
}

static int decoded_component_op_allowed(const RuneCUiState *ui,
                                        const RuneCUiHitResult *hit,
                                        int op) {
    if (!hit)
        return 0;
    uint32_t mask = event_mask_for_component(ui, hit->component_id,
                                             hit->click_mask);
    if (mask == 0)
        return 0;
    if (op < 0)
        return 1;
    if (op < RUNEC_UI_INTERFACE_MAX_ACTIONS
            && (mask & (1u << (unsigned)(op + 1))))
        return 1;
    return hit->action_count == 0 && op == 0;
}

int runec_ui_open_subinterface(RuneCUiState *ui, RuneCUiOpenMount mount,
                               RuneCUiTab tab, uint32_t target_component_id,
                               const char *group) {
    if (!ui || !group || !group[0])
        return 0;
    for (int i = 0; i < ui->open_interface_count; i++) {
        RuneCUiOpenInterface *open = &ui->open_interfaces[i];
        if (open->active && open->mount == mount && open->tab == tab) {
            copy_text(open->group, sizeof(open->group), group);
            open->target_component_id = target_component_id;
            seed_event_masks_for_group(ui, group);
            dispatch_local_group_listeners(ui, group,
                                           RUNEC_UI_LISTENER_ON_LOAD);
            return 1;
        }
    }
    if (ui->open_interface_count >= RUNEC_UI_OPEN_INTERFACES)
        return 0;
    RuneCUiOpenInterface *open = &ui->open_interfaces[ui->open_interface_count++];
    memset(open, 0, sizeof(*open));
    open->active = 1;
    open->mount = mount;
    open->tab = tab;
    open->target_component_id = target_component_id;
    open->z_order = ui->open_interface_count;
    copy_text(open->group, sizeof(open->group), group);
    seed_event_masks_for_group(ui, group);
    dispatch_local_group_listeners(ui, group, RUNEC_UI_LISTENER_ON_LOAD);
    return 1;
}

int runec_ui_open_interface(RuneCUiState *ui, RuneCUiOpenMount mount,
                            RuneCUiTab tab, const char *group) {
    return runec_ui_open_subinterface(ui, mount, tab, 0, group);
}

int runec_ui_open_top_interface(RuneCUiState *ui, const char *group) {
    return runec_ui_open_subinterface(ui, RUNEC_UI_MOUNT_SCREEN,
                                      RUNEC_UI_TAB_NONE, 0, group);
}

int runec_ui_open_overlay(RuneCUiState *ui, const char *group) {
    return runec_ui_open_subinterface(ui, RUNEC_UI_MOUNT_OVERLAY,
                                      RUNEC_UI_TAB_NONE, 0, group);
}

int runec_ui_open_modal(RuneCUiState *ui, const char *group) {
    return runec_ui_open_subinterface(ui, RUNEC_UI_MOUNT_MODAL,
                                      RUNEC_UI_TAB_NONE, 0, group);
}

void runec_ui_set_active_tab(RuneCUiState *ui, RuneCUiTab tab) {
    if (!ui || tab < 0 || tab >= RUNEC_UI_TAB_COUNT)
        return;
    ui->active_tab = tab;
    ui->autocast_picker = 0;
    ui->spell_scroll = 0;
    runec_ui_open_subinterface(ui, RUNEC_UI_MOUNT_SIDE_CONTENT,
                               ui->active_tab,
                               RUNEC_UI_TOP_SIDE_CONTAINER,
                               decoded_group_for_tab(ui->active_tab));
}

void runec_ui_set_dev_transport_options(RuneCUiState *ui,
                                        const char *const *labels,
                                        int count) {
    if (!ui)
        return;
    if (count < 0) count = 0;
    if (count > RUNEC_UI_DEV_TRANSPORT_MAX)
        count = RUNEC_UI_DEV_TRANSPORT_MAX;
    ui->dev_transport_count = count;
    for (int i = 0; i < count; i++) {
        snprintf(ui->dev_transport_labels[i],
                 sizeof(ui->dev_transport_labels[i]), "%.23s",
                 labels && labels[i] ? labels[i] : "");
    }
}

static int clamp_ui_scene_plane(int plane) {
    if (plane < 0) return 0;
    if (plane >= RUNEC_UI_SCENE_PLANE_COUNT)
        return RUNEC_UI_SCENE_PLANE_COUNT - 1;
    return plane;
}

void runec_ui_set_scene_plane_state(RuneCUiState *ui, int scene_plane,
                                    int player_plane, int override_active) {
    if (!ui)
        return;
    ui->scene_plane = clamp_ui_scene_plane(scene_plane);
    ui->player_plane = clamp_ui_scene_plane(player_plane);
    ui->scene_plane_override_active = override_active ? 1 : 0;
}

void runec_ui_set_npc_tiles_overlay(RuneCUiState *ui, int enabled) {
    if (ui) ui->npc_tiles_overlay = enabled ? 1 : 0;
}

int runec_ui_move_interface(RuneCUiState *ui, RuneCUiOpenMount from_mount,
                            RuneCUiTab from_tab, RuneCUiOpenMount to_mount,
                            RuneCUiTab to_tab, uint32_t target_component_id) {
    if (!ui)
        return 0;
    for (int i = 0; i < ui->open_interface_count; i++) {
        RuneCUiOpenInterface *open = &ui->open_interfaces[i];
        if (!open->active || open->mount != from_mount || open->tab != from_tab)
            continue;
        open->mount = to_mount;
        open->tab = to_tab;
        open->target_component_id = target_component_id;
        return 1;
    }
    return 0;
}

void runec_ui_close_interface(RuneCUiState *ui, RuneCUiOpenMount mount,
                              RuneCUiTab tab) {
    if (!ui)
        return;
    for (int i = 0; i < ui->open_interface_count; i++) {
        RuneCUiOpenInterface *open = &ui->open_interfaces[i];
        if (open->active && open->mount == mount && open->tab == tab)
            open->active = 0;
    }
}

void runec_ui_init(RuneCUiState *ui) {
    memset(ui, 0, sizeof(*ui));
    ui->current_spellbook = -1;
    ui->autocast_slot = -1;
    ui->active_tab = RUNEC_UI_TAB_SKILLS;
    ui->selected_inventory_slot = -1;
    ui->selected_equipment_slot = -1;
    ui->context_source_slot = -1;
    ui->selected_target.source_slot = -1;
    ui->drag.source_slot = -1;
    ui->selected_combat_style = 0;
    ui->auto_retaliate = 1;
    ui->special_attack_enabled = 0;
    ui->special_attack_energy = 100;
    ui->active_chat_tab = 0;
    ui->bank_active_tab = 0;
    static const char *bank_tabs[RUNEC_UI_BANK_TAB_COUNT] = {
        "Ranged", "Mage", "Melee", "PvP", "Special",
    };
    for (int i = 0; i < RUNEC_UI_BANK_TAB_COUNT; i++) {
        snprintf(ui->bank_tab_labels[i], sizeof(ui->bank_tab_labels[i]),
                 "%.15s", bank_tabs[i]);
    }
    runec_ui_set_combat_weapon_name(ui, "Abyssal whip");
    runec_ui_set_combat_style_profile(ui, 10);
    ui->hitpoints = 99;
    ui->hitpoints_max = 99;
    ui->prayer_points = 77;
    ui->prayer_points_max = 77;
    ui->run_energy = 100;
    ui->combat_level = 126;
    for (int i = 0; i < RUNEC_UI_SKILL_COUNT; i++) {
        ui->skill_current[i] = 1;
        ui->skill_base[i] = 1;
    }
    ui->skill_current[8] = 10;
    ui->skill_base[8] = 10;
    ui->skill_total = 33;
    const char *start_tab = getenv("RUNEC_UI_START_TAB");
    if (start_tab && start_tab[0]) {
        for (int i = 0; i < RUNEC_UI_TAB_COUNT; i++) {
            if (strcmp(start_tab, runec_ui_tab_name((RuneCUiTab)i)) == 0) {
                ui->active_tab = (RuneCUiTab)i;
                break;
            }
        }
        if (start_tab[0] >= '0' && start_tab[0] <= '9') {
            int tab_index = atoi(start_tab);
            if (tab_index >= 0 && tab_index < RUNEC_UI_TAB_COUNT)
                ui->active_tab = (RuneCUiTab)tab_index;
        }
    }

    runec_ui_assets_load(&ui->assets);
    const char *decoded = getenv("RUNEC_UI_DECODED");
    ui->decoded_ui_enabled = decoded && decoded[0] && strcmp(decoded, "0") != 0;
    if (ui->decoded_ui_enabled) {
        ui->decoded_ui_ready =
            runec_ui_interfaces_load(&ui->interfaces, "data/ui/interfaces.bin");
        if (!ui->decoded_ui_ready) {
            fprintf(stderr,
                    "ui_interfaces: data/ui/interfaces.bin unavailable; using manual UI\n");
        } else {
            runec_ui_open_top_interface(ui, "toplevel_osrs_stretch");
            runec_ui_open_subinterface(ui, RUNEC_UI_MOUNT_CHAT,
                                       RUNEC_UI_TAB_NONE,
                                       RUNEC_UI_TOP_CHAT_CONTAINER, "chatbox");
            runec_ui_open_subinterface(ui, RUNEC_UI_MOUNT_MAP,
                                       RUNEC_UI_TAB_NONE,
                                       RUNEC_UI_TOP_MAP_CONTAINER, "orbs");
            runec_ui_open_subinterface(ui, RUNEC_UI_MOUNT_SIDE_CONTENT,
                                       ui->active_tab,
                                       RUNEC_UI_TOP_SIDE_CONTAINER,
                                       decoded_group_for_tab(ui->active_tab));
            const char *debug_modal = getenv("RUNEC_UI_OPEN_MODAL");
            if (debug_modal && debug_modal[0]) {
                runec_ui_open_subinterface(ui, RUNEC_UI_MOUNT_MODAL,
                                           RUNEC_UI_TAB_NONE,
                                           RUNEC_UI_TOP_MAIN_MODAL,
                                           debug_modal);
            }
            const char *debug_overlay = getenv("RUNEC_UI_OPEN_SIDE_OVERLAY");
            if (debug_overlay && debug_overlay[0]) {
                runec_ui_open_subinterface(ui, RUNEC_UI_MOUNT_OVERLAY,
                                           RUNEC_UI_TAB_NONE,
                                           RUNEC_UI_TOP_SIDE_MODAL,
                                           debug_overlay);
            }
        }
    }
    Image minimap = GenImageColor(152, 152, BLANK);
    ui->minimap_texture = LoadTextureFromImage(minimap);
    UnloadImage(minimap);
    if (ui->minimap_texture.id != 0) {
        SetTextureFilter(ui->minimap_texture, TEXTURE_FILTER_POINT);
        ui->minimap_texture_ready = 1;
    }

    ui->inventory[0] = (RuneCUiSlot){6570, 6570, 1, "Fire cape", 1};
    ui->inventory[1] = (RuneCUiSlot){21295, 21295, 1, "Infernal cape", 1};
    ui->inventory[2] = (RuneCUiSlot){1042, 1042, 1, "Blue partyhat", 1};
    ui->inventory[3] = (RuneCUiSlot){1044, 1044, 1, "Green partyhat", 1};
    ui->inventory[4] = (RuneCUiSlot){1046, 1046, 1, "Purple partyhat", 1};
    ui->inventory[5] = (RuneCUiSlot){1048, 1048, 1, "White partyhat", 1};
    ui->inventory[6] = (RuneCUiSlot){4151, 4151, 1, "Abyssal whip", 1};
    ui->inventory[7] = (RuneCUiSlot){11802, 11802, 1, "Armadyl godsword", 1};
    ui->inventory[8] = (RuneCUiSlot){11832, 11832, 1, "Bandos chestplate", 1};
    ui->inventory[9] = (RuneCUiSlot){11834, 11834, 1, "Bandos tassets", 1};
    ui->inventory[10] = (RuneCUiSlot){26382, 26382, 1, "Torva full helm", 1};
    ui->inventory[11] = (RuneCUiSlot){26384, 26384, 1, "Torva platebody", 1};
    ui->inventory[12] = (RuneCUiSlot){26386, 26386, 1, "Torva platelegs", 1};
    ui->inventory[13] = (RuneCUiSlot){10350, 10350, 1, "3a full helmet", 1};
    ui->inventory[14] = (RuneCUiSlot){10348, 10348, 1, "3a platebody", 1};
    ui->inventory[15] = (RuneCUiSlot){10346, 10346, 1, "3a platelegs", 1};
    ui->inventory[16] = (RuneCUiSlot){10352, 10352, 1, "3a kiteshield", 1};
    ui->inventory[17] = (RuneCUiSlot){995, 1004, 10000000, "Coins", 1};
    ui->equipment[0] = (RuneCUiSlot){11826, 11826, 1, "Helm", 1};
    ui->equipment[3] = (RuneCUiSlot){4151, 4151, 1, "Abyssal whip", 1};
    ui->equipment[4] = (RuneCUiSlot){11828, 11828, 1, "Body", 1};
    ui->equipment[7] = (RuneCUiSlot){11830, 11830, 1, "Legs", 1};

    ui_add_chat(ui, "Welcome to RuneC.");
    ui_add_chat(ui, "OSRS UI shell is cache-sprite backed.");
    ui_add_chat(ui, "Click tabs, inventory, prayers, spells, or the minimap.");
}

void runec_ui_shutdown(RuneCUiState *ui) {
    if (ui->minimap_texture_ready) {
        UnloadTexture(ui->minimap_texture);
        ui->minimap_texture_ready = 0;
    }
    for (int i = 0; i < ui->item_icon_count; i++) {
        if (ui->item_icons[i].ready && ui->item_icons[i].texture.id != 0)
            UnloadTexture(ui->item_icons[i].texture);
    }
    ui->item_icon_count = 0;
    runec_ui_assets_unload(&ui->assets);
    runec_ui_interfaces_unload(&ui->interfaces);
}

void runec_ui_clear_minimap(RuneCUiState *ui) {
    ui->minimap_dot_count = 0;
}

void runec_ui_add_minimap_dot(RuneCUiState *ui, float dx, float dy,
                              RuneCUiMinimapDotKind kind) {
    if (ui->minimap_dot_count >= RUNEC_UI_MINIMAP_DOTS)
        return;
    ui->minimap_dots[ui->minimap_dot_count++] =
        (RuneCUiMinimapDot){dx, dy, kind};
}

void runec_ui_update_minimap(RuneCUiState *ui, const Color *pixels,
                             int width, int height) {
    if (!ui->minimap_texture_ready || !pixels || width != 152 || height != 152)
        return;
    UpdateTexture(ui->minimap_texture, pixels);
}

void runec_ui_set_minimap_rotation(RuneCUiState *ui, float radians) {
    if (ui)
        ui->minimap_rotation = radians;
}

void runec_ui_set_item_icon(RuneCUiState *ui, uint32_t icon_item_id, Texture2D texture) {
    if (!ui || icon_item_id == 0)
        return;
    for (int i = 0; i < ui->item_icon_count; i++) {
        if (ui->item_icons[i].item_id == icon_item_id) {
            if (ui->item_icons[i].ready && ui->item_icons[i].texture.id != 0)
                UnloadTexture(ui->item_icons[i].texture);
            ui->item_icons[i].texture = texture;
            ui->item_icons[i].ready = 1;
            return;
        }
    }
    if (ui->item_icon_count >= RUNEC_UI_ITEM_ICON_CACHE) {
        if (texture.id) UnloadTexture(texture);
        return;
    }
    ui->item_icons[ui->item_icon_count++] =
        (RuneCUiItemIcon){icon_item_id, texture, 1};
}

int runec_ui_set_spellbook(RuneCUiState *ui, int book) {
    if (!ui || book < 0 || book > 3) return 0;
    int count = 0;
    for (size_t i = 0; i < sizeof(g_spell_icons) / sizeof(g_spell_icons[0]); i++) {
        if (g_spell_icons[i].book != book) continue;
        if (count == RUNEC_UI_SPELL_MAX) return 0;
        ui->spells[count] = &g_spell_icons[i];
        ui->spell_icons[count] = runec_ui_asset(&ui->assets, g_spell_icons[i].icon_asset);
        ui->spell_runtime_ids[count] = -1;
        ui->spell_can_autocast[count++] = 0;
    }
    ui->current_spellbook = book;
    ui->spell_count = count;
    ui->spell_scroll = 0;
    ui->spell_sync_tick = UINT64_MAX;
    ui->autocast_slot = -1;
    ui->autocast_picker = 0;
    runec_ui_clear_selected_target(ui);
    close_context(ui);
    return 1;
}

void runec_ui_sync_status(RuneCUiState *ui, int world_x, int world_y,
                          int local_x, int local_y, uint32_t tick,
                          int running, int paused) {
    ui->world_x = world_x;
    ui->world_y = world_y;
    ui->local_x = local_x;
    ui->local_y = local_y;
    ui->tick = tick;
    ui->running = running;
    ui->paused = paused;
}

static void sync_decoded_component_overrides(RuneCUiState *ui) {
    if (!ui)
        return;
    if (!ui->decoded_ui_enabled || !ui->decoded_ui_ready)
        return;

    RuneCUiItemContainerOverride *inventory =
        set_item_container_override(
            ui, RUNEC_UI_INVENTORY_ITEMS_COMPONENT,
            RUNEC_UI_INV_SLOT_COUNT, 4,
            RUNEC_OSRS_INVENTORY_SLOT_X, RUNEC_OSRS_INVENTORY_SLOT_Y,
            RUNEC_OSRS_INVENTORY_SLOT_STEP_X, RUNEC_OSRS_INVENTORY_SLOT_STEP_Y,
            RUNEC_OSRS_INVENTORY_SLOT_W, RUNEC_OSRS_INVENTORY_SLOT_H,
            ui->selected_inventory_slot);
    if (inventory) {
        for (int i = 0; i < RUNEC_UI_INV_SLOT_COUNT; i++) {
            inventory->slots[i] = (RuneCUiItemContainerSlot){
                ui->inventory[i].item_id,
                ui->inventory[i].icon_item_id,
                ui->inventory[i].quantity,
                ui->inventory[i].enabled,
            };
        }
    }

    for (int i = 0; i < RUNEC_UI_EQUIP_SLOT_COUNT; i++) {
        if (g_worn_slot_component_file[i] < 0)
            continue;
        uint32_t component_id = RUNEC_UI_COMPONENT_ID(
            RUNEC_UI_GROUP_WORNITEMS, g_worn_slot_component_file[i]);
        const RuneCUiSlot *slot = &ui->equipment[i];
        runec_ui_set_component_item(ui, component_id,
                                    slot->enabled ? slot->item_id : 0,
                                    slot->enabled ? slot->icon_item_id : 0,
                                    slot->enabled ? slot->quantity : 1,
                                    ui->selected_equipment_slot == i);
    }

    if (ui->active_tab == RUNEC_UI_TAB_SKILLS) {
        char text[64];
        snprintf(text, sizeof(text), "Total level: %d",
                 ui->skill_total > 0 ? ui->skill_total : 0);
        runec_ui_set_component_text(
            ui, RUNEC_UI_COMPONENT_ID(RUNEC_UI_GROUP_STATS, 32), text);
    }
    dispatch_local_transmit_listeners(ui);
}

static int handle_context_click(RuneCUiState *ui, Vector2 mouse) {
    if (!ui->context_open)
        return 0;

    RuneCContextMenuLayout box = context_menu_layout_for_ui(ui);
    if (!runec_context_menu_contains(&box, (int)mouse.x, (int)mouse.y)) {
        close_context(ui);
        return 1;
    }

    int clicked_action = runec_context_menu_action_at(
        &box, ui->context_action_count, (int)mouse.x, (int)mouse.y);
    if (clicked_action >= 0) {
            int i = clicked_action;
            const char *action = ui->context_actions[i];
            int op = ui->context_action_op[i];
            if (strcmp(action, "Cancel") == 0) {
                close_context(ui);
                return 1;
            }
            if (ui->context_source_kind == RUNEC_UI_CONTEXT_BANK
                    || ui->context_source_kind == RUNEC_UI_CONTEXT_BANK_INVENTORY) {
                submit_bank_action(ui, ui->context_source_kind, ui->context_source_slot,
                    ui->context_source_item_id, ui->context_source_generation, op);
            } else if (ui->context_source_kind == RUNEC_UI_CONTEXT_INVENTORY) {
                if (strcmp(action, "Use") == 0) {
                    set_selected_item_target(ui, ui->context_source_slot);
                    ui->last_intent.kind = RUNEC_UI_INTENT_SELECTED_ITEM;
                    ui->last_intent.primary = ui->context_source_slot;
                    ui->last_intent.secondary = (int)ui->context_source_item_id;
                } else {
                    ui->last_intent.kind = RUNEC_UI_INTENT_INVENTORY_ACTION;
                    ui->last_intent.primary = ui->context_source_slot;
                    ui->last_intent.secondary = op;
                }
            } else if (ui->context_source_kind == RUNEC_UI_CONTEXT_EQUIPMENT) {
                ui->last_intent.kind = RUNEC_UI_INTENT_EQUIPMENT_ACTION;
                ui->last_intent.primary = ui->context_source_slot;
                ui->last_intent.secondary = op;
            } else if (ui->context_source_kind == RUNEC_UI_CONTEXT_PRAYER) {
                if (strcmp(action, "Setup quick-prayers") == 0) {
                    ui->quick_prayer_setup = 1;
                    runec_ui_set_active_tab(ui, RUNEC_UI_TAB_PRAYER);
                } else if (ui->context_source_slot == -1) {
                    ui->last_intent.kind = RUNEC_UI_INTENT_QUICK_PRAYER_TOGGLE;
                } else if (strcmp(action, "Activate") == 0 || strcmp(action, "Deactivate") == 0) {
                    ui->last_intent.kind = RUNEC_UI_INTENT_PRAYER_SLOT;
                    ui->last_intent.primary = ui->context_source_slot;
                    copy_text(ui->last_intent.text,
                              sizeof(ui->last_intent.text),
                              ui->context_title);
                } else if (strcmp(action, "Quick-prayer") == 0) {
                    ui->quick_prayer_setup = 1;
                    ui->last_intent.kind = RUNEC_UI_INTENT_QUICK_PRAYER_SLOT;
                    ui->last_intent.primary = ui->context_source_slot;
                    copy_text(ui->last_intent.text,
                              sizeof(ui->last_intent.text),
                              ui->context_title);
                } else {
                    ui->last_intent.kind = RUNEC_UI_INTENT_CONTEXT_ACTION;
                    ui->last_intent.primary = i;
                    ui->last_intent.secondary = ui->context_source_slot;
                }
            } else if (ui->context_source_kind == RUNEC_UI_CONTEXT_SPELL
                    && strcmp(action, "Cast") == 0) {
                set_selected_spell_target(ui, ui->context_source_slot,
                                          ui->context_title);
                ui->last_intent.kind = RUNEC_UI_INTENT_SELECTED_SPELL;
                ui->last_intent.primary = ui->context_source_slot;
                ui->last_intent.secondary = 0;
                copy_text(ui->last_intent.text, sizeof(ui->last_intent.text),
                          ui->context_title);
            } else if (ui->context_source_kind == RUNEC_UI_CONTEXT_SPELL
                    && (strcmp(action, "Autocast") == 0 || strcmp(action, "Defensive autocast") == 0)) {
                runec_ui_clear_selected_target(ui);
                ui->last_intent.kind = RUNEC_UI_INTENT_AUTOCAST_SPELL;
                ui->last_intent.primary = ui->context_source_slot;
                ui->last_intent.secondary = strcmp(action, "Defensive autocast") == 0;
                copy_text(ui->last_intent.text,
                          sizeof(ui->last_intent.text),
                          ui->context_title);
                ui->autocast_picker = 0;
                ui->spell_scroll = 0;
            } else if (ui->context_source_kind == RUNEC_UI_CONTEXT_COMPONENT) {
                RuneCUiListenerEvent event = {
                    .kind = RUNEC_UI_LISTENER_ON_OP,
                    .component_id = ui->context_source_component_id,
                    .op = op,
                    .mouse = mouse,
                };
                if (dispatch_local_listener(ui, &event, NULL)) {
                    close_context(ui);
                    return 1;
                }
                ui->last_intent.kind = RUNEC_UI_INTENT_COMPONENT_ACTION;
                ui->last_intent.primary = (int)ui->context_source_component_id;
                ui->last_intent.secondary = op;
            } else {
                ui->last_intent.kind = RUNEC_UI_INTENT_CONTEXT_ACTION;
                ui->last_intent.primary = i;
                ui->last_intent.secondary = ui->context_source_slot;
            }
            ui->last_intent.position = mouse;
            if (!ui->last_intent.text[0])
                copy_text(ui->last_intent.text, sizeof(ui->last_intent.text),
                          action);
            close_context(ui);
            return 1;
    }

    return 1;
}

static Rectangle inv_slot_rect(const RuneCUiLayout *layout, int slot) {
    int col = slot % 4;
    int row = slot / 4;
    return (Rectangle){
        layout->side_content.x + RUNEC_OSRS_INVENTORY_SLOT_X + col * RUNEC_OSRS_INVENTORY_SLOT_STEP_X,
        layout->side_content.y + RUNEC_OSRS_INVENTORY_SLOT_Y + row * RUNEC_OSRS_INVENTORY_SLOT_STEP_Y,
        RUNEC_OSRS_INVENTORY_SLOT_W,
        RUNEC_OSRS_INVENTORY_SLOT_H,
    };
}

static int inv_slot_at(const RuneCUiLayout *layout, Vector2 mouse) {
    for (int i = 0; i < RUNEC_UI_INV_SLOT_COUNT; i++) {
        if (CheckCollisionPointRec(mouse, inv_slot_rect(layout, i)))
            return i;
    }
    return -1;
}

static Rectangle equip_slot_rect(const RuneCUiLayout *layout, int slot) {
    if (slot < 0 || slot >= RUNEC_UI_EQUIP_SLOT_COUNT)
        return (Rectangle){0, 0, 0, 0};
    Rectangle off = g_equipment_offsets[slot];
    if (off.width <= 0 || off.height <= 0)
        return off;
    return (Rectangle){
        layout->side_content.x + off.x,
        layout->side_content.y + off.y,
        off.width,
        off.height,
    };
}

static int equipment_slot_at(const RuneCUiLayout *layout, Vector2 mouse) {
    for (int i = 0; i < RUNEC_UI_EQUIP_SLOT_COUNT; i++) {
        Rectangle r = equip_slot_rect(layout, i);
        if (r.width > 0 && CheckCollisionPointRec(mouse, r))
            return i;
    }
    return -1;
}

static const RuneCUiItemContainerOverride *find_item_container_for_component(
    const RuneCUiState *ui,
    uint32_t component_id) {
    if (!ui)
        return NULL;
    for (int i = 0; i < ui->item_container_override_count; i++) {
        const RuneCUiItemContainerOverride *override =
            &ui->item_container_overrides[i];
        if (override->component_id == component_id)
            return override;
    }
    return NULL;
}

static int decoded_item_container_slot_at(const RuneCUiState *ui,
                                          const RuneCUiLayout *layout,
                                          const char *group,
                                          uint32_t component_id,
                                          Vector2 mouse) {
    if (!ui || !ui->decoded_ui_enabled || !ui->decoded_ui_ready || !group)
        return -1;
    const RuneCUiItemContainerOverride *override =
        find_item_container_for_component(ui, component_id);
    if (!override || override->slot_count <= 0 || override->columns <= 0)
        return -1;
    Rectangle container = {0};
    if (!runec_ui_interfaces_component_rect_by_id(
            &ui->interfaces, group, component_id, layout->side_content,
            &container))
        return -1;
    int count = override->slot_count;
    if (count > RUNEC_UI_ITEM_CONTAINER_MAX_SLOTS)
        count = RUNEC_UI_ITEM_CONTAINER_MAX_SLOTS;
    for (int i = 0; i < count; i++) {
        int col = i % override->columns;
        int row = i / override->columns;
        Rectangle r = {
            container.x + override->x0 + (float)col * override->step_x,
            container.y + override->y0 + (float)row * override->step_y,
            override->slot_w,
            override->slot_h,
        };
        if (CheckCollisionPointRec(mouse, r))
            return i;
    }
    return -1;
}

static int ui_inventory_slot_at(const RuneCUiState *ui,
                                const RuneCUiLayout *layout,
                                Vector2 mouse) {
    if (ui && ui->decoded_ui_enabled && ui->decoded_ui_ready
            && ui->active_tab == RUNEC_UI_TAB_INVENTORY) {
        int slot = decoded_item_container_slot_at(
            ui, layout, open_group_for_side_content(ui),
            RUNEC_UI_INVENTORY_ITEMS_COMPONENT, mouse);
        if (slot >= 0)
            return slot;
    }
    return inv_slot_at(layout, mouse);
}

static Rectangle bank_panel_rect(int screen_w, int screen_h,
                                 const RuneCUiLayout *layout) {
    float right_limit = layout->side.x > 0.0f ? layout->side.x : (float)screen_w;
    float bottom_limit = layout->chat.y > 0.0f ? layout->chat.y : (float)screen_h;
    float w = right_limit - 64.0f;
    if (w > 760.0f) w = 760.0f;
    if (w < 470.0f) w = right_limit - 24.0f;
    float h = 420.0f;
    if (h > bottom_limit - 76.0f) h = bottom_limit - 76.0f;
    if (h < 300.0f) h = 300.0f;
    return (Rectangle){32.0f, 58.0f, w, h};
}

static Rectangle bank_close_rect(Rectangle panel) {
    return (Rectangle){panel.x + panel.width - 70.0f, panel.y + 8.0f,
                       56.0f, 22.0f};
}

static Rectangle bank_mode_rect(Rectangle panel, int noted) {
    return (Rectangle){panel.x + 8 + noted * 58, panel.y + 7, 54, 22};
}

static Rectangle bank_deposit_all_rect(Rectangle panel, int equipment) {
    return (Rectangle){panel.x + panel.width - 146 + equipment * 38, panel.y + 4, 32, 26};
}

static int bank_slot_visible_in_tab(const RuneCUiState *ui, int slot) {
    if (ui && ui->bank_kind == RC_STORAGE_DEPOSIT_BOX)
        return slot >= 0 && slot < RC_INVENTORY_SIZE && ui->inventory[slot].enabled;
    return ui && slot >= 0 && slot < RUNEC_UI_BANK_SLOT_COUNT
        && ui->bank[slot].enabled
        && ui->bank[slot].category == ui->bank_active_tab;
}

static int bank_tab_item_count(const RuneCUiState *ui) {
    int count = 0;
    if (!ui)
        return 0;
    for (int i = 0; i < RUNEC_UI_BANK_SLOT_COUNT; i++)
        if (bank_slot_visible_in_tab(ui, i))
            count++;
    return count;
}

static int bank_scroll_max(const RuneCUiState *ui) {
    int max = bank_tab_item_count(ui) - RUNEC_UI_BANK_VISIBLE_SLOTS;
    return max > 0 ? max : 0;
}

static void clamp_bank_scroll(RuneCUiState *ui) {
    int max = bank_scroll_max(ui);
    if (ui->bank_scroll < 0) ui->bank_scroll = 0;
    if (ui->bank_scroll > max) ui->bank_scroll = max;
}

static int bank_slot_for_visible_index(const RuneCUiState *ui,
                                       int visible_index) {
    if (!ui || visible_index < 0)
        return -1;
    int seen = 0;
    for (int i = 0; i < RUNEC_UI_BANK_SLOT_COUNT; i++) {
        if (!bank_slot_visible_in_tab(ui, i))
            continue;
        if (seen == visible_index)
            return i;
        seen++;
    }
    return -1;
}

static Rectangle bank_tab_rect(Rectangle panel, int tab) {
    float w = 76.0f;
    return (Rectangle){
        panel.x + 12.0f + (float)tab * (w + 4.0f),
        panel.y + 35.0f,
        w,
        22.0f,
    };
}

static Rectangle bank_slot_rect(Rectangle panel, int visible_slot) {
    int col = visible_slot % RUNEC_UI_BANK_COLS;
    int row = visible_slot / RUNEC_UI_BANK_COLS;
    return (Rectangle){
        panel.x + 18.0f + col * RUNEC_UI_BANK_SLOT_STEP,
        panel.y + 70.0f + row * RUNEC_UI_BANK_SLOT_STEP,
        RUNEC_UI_BANK_SLOT_W,
        RUNEC_UI_BANK_SLOT_H,
    };
}

static int bank_slot_at(RuneCUiState *ui, Rectangle panel, Vector2 mouse) {
    clamp_bank_scroll(ui);
    for (int i = 0; i < RUNEC_UI_BANK_VISIBLE_SLOTS; i++) {
        if (CheckCollisionPointRec(mouse, bank_slot_rect(panel, i)))
            return bank_slot_for_visible_index(ui, ui->bank_scroll + i);
    }
    return -1;
}

static int ui_equipment_slot_at(const RuneCUiState *ui,
                                const RuneCUiLayout *layout,
                                Vector2 mouse) {
    if (ui && ui->decoded_ui_enabled && ui->decoded_ui_ready
            && ui->active_tab == RUNEC_UI_TAB_EQUIPMENT) {
        const char *group = open_group_for_side_content(ui);
        for (int i = 0; i < RUNEC_UI_EQUIP_SLOT_COUNT; i++) {
            if (g_worn_slot_component_file[i] < 0)
                continue;
            Rectangle r = {0};
            uint32_t component_id = RUNEC_UI_COMPONENT_ID(
                RUNEC_UI_GROUP_WORNITEMS, g_worn_slot_component_file[i]);
            if (runec_ui_interfaces_component_rect_by_id(
                    &ui->interfaces, group, component_id, layout->side_content,
                    &r)
                    && r.width > 0
                    && CheckCollisionPointRec(mouse, r)) {
                return i;
            }
        }
    }
    return equipment_slot_at(layout, mouse);
}

static Rectangle skill_slot_rect(const RuneCUiLayout *layout, int slot) {
    if (slot < 0 || slot >= (int)(sizeof(RUNEC_OSRS_SKILLS) / sizeof(RUNEC_OSRS_SKILLS[0])))
        return (Rectangle){0, 0, 0, 0};
    Rectangle r = RUNEC_OSRS_SKILLS[slot].rect;
    return (Rectangle){layout->side_content.x + r.x, layout->side_content.y + r.y,
                       r.width, r.height};
}

static int skill_slot_at(const RuneCUiLayout *layout, Vector2 mouse) {
    int count = (int)(sizeof(RUNEC_OSRS_SKILLS) / sizeof(RUNEC_OSRS_SKILLS[0]));
    for (int i = 0; i < count; i++) {
        if (CheckCollisionPointRec(mouse, skill_slot_rect(layout, i)))
            return i;
    }
    Rectangle total = {layout->side_content.x + RUNEC_OSRS_STATS_TOTAL.x,
                       layout->side_content.y + RUNEC_OSRS_STATS_TOTAL.y,
                       RUNEC_OSRS_STATS_TOTAL.width, RUNEC_OSRS_STATS_TOTAL.height};
    if (CheckCollisionPointRec(mouse, total))
        return count;
    return -1;
}

static Rectangle side_ref_rect(const RuneCUiLayout *layout, Rectangle ref) {
    return (Rectangle){layout->side_content.x + ref.x, layout->side_content.y + ref.y,
                       ref.width, ref.height};
}

static Rectangle combat_style_rect(const RuneCUiState *ui, const RuneCUiLayout *layout,
                                    int visible_slot) {
    if (ui->autocast_available)
        return side_ref_rect(layout, (Rectangle){20, 45 + visible_slot * 36, 68, 32});
    return side_ref_rect(layout, RUNEC_OSRS_COMBAT_STYLES[visible_slot].rect);
}

static const RuneCUiCombatStyleOption *combat_style_at(
    const RuneCUiState *ui,
    const RuneCUiLayout *layout,
    Vector2 mouse) {
    if (!ui)
        return NULL;
    int visible_slot = 0;
    int layout_count = (int)(sizeof(RUNEC_OSRS_COMBAT_STYLES)
        / sizeof(RUNEC_OSRS_COMBAT_STYLES[0]));
    for (int i = 0; i < RUNEC_UI_COMBAT_STYLE_COUNT && visible_slot < layout_count; i++) {
        const RuneCUiCombatStyleOption *option = &ui->combat_styles[i];
        if (!option->visible)
            continue;
        if (CheckCollisionPointRec(mouse, combat_style_rect(ui, layout, visible_slot++)))
            return option;
    }
    return NULL;
}

static Rectangle grid_cell_rect(const RuneCUiLayout *layout, int index,
                                int cols, float x0, float y0,
                                float step_x, float step_y,
                                float w, float h) {
    int col = index % cols;
    int row = index / cols;
    return (Rectangle){
        layout->side_content.x + x0 + col * step_x,
        layout->side_content.y + y0 + row * step_y,
        w,
        h,
    };
}

static int grid_index_at(const RuneCUiLayout *layout, Vector2 mouse,
                         int count, int cols, float x0, float y0,
                         float step_x, float step_y, float w, float h) {
    for (int i = 0; i < count; i++) {
        if (CheckCollisionPointRec(mouse,
                grid_cell_rect(layout, i, cols, x0, y0, step_x, step_y, w, h)))
            return i;
    }
    return -1;
}

static RuneCUiComponentOverrides decoded_overrides(const RuneCUiState *ui) {
    return (RuneCUiComponentOverrides){
        ui ? ui->component_overrides : NULL,
        ui ? ui->component_override_count : 0,
        ui ? ui->item_container_overrides : NULL,
        ui ? ui->item_container_override_count : 0,
    };
}

static int decoded_side_hit(const RuneCUiState *ui, const RuneCUiLayout *layout,
                            Vector2 mouse, RuneCUiHitResult *hit) {
    if (!ui || !ui->decoded_ui_enabled || !ui->decoded_ui_ready
            || !CheckCollisionPointRec(mouse, layout->side_content))
        return 0;
    if (ui->active_tab == RUNEC_UI_TAB_COMBAT || ui->active_tab == RUNEC_UI_TAB_PRAYER)
        return 0;
    const char *group = open_group_for_side_content(ui);
    if (!group)
        return 0;
    RuneCUiComponentOverrides overrides = decoded_overrides(ui);
    return runec_ui_interfaces_hit_test_ex(&ui->interfaces, group,
                                           layout->side_content, mouse, hit,
                                           &overrides);
}

static int decoded_modal_hit(const RuneCUiState *ui,
                             const RuneCUiLayout *layout,
                             int screen_w,
                             int screen_h,
                             Vector2 mouse,
                             RuneCUiHitResult *hit) {
    if (!ui || !ui->decoded_ui_enabled || !ui->decoded_ui_ready)
        return 0;
    RuneCUiComponentOverrides overrides = decoded_overrides(ui);
    for (int i = ui->open_interface_count - 1; i >= 0; i--) {
        const RuneCUiOpenInterface *open = &ui->open_interfaces[i];
        if (!open->active || !open->group[0])
            continue;
        if (open->mount != RUNEC_UI_MOUNT_MODAL
                && open->mount != RUNEC_UI_MOUNT_OVERLAY)
            continue;
        Rectangle mount =
            open_interface_mount_rect(ui, layout, open, screen_w, screen_h);
        if (!CheckCollisionPointRec(mouse, mount))
            continue;
        if (runec_ui_interfaces_hit_test_ex(&ui->interfaces, open->group,
                                            mount, mouse, hit, &overrides)) {
            return 1;
        }
        return 1;
    }
    return 0;
}

static int close_modal_interfaces(RuneCUiState *ui) {
    if (!ui)
        return 0;
    int closed = 0;
    for (int i = 0; i < ui->open_interface_count; i++) {
        RuneCUiOpenInterface *open = &ui->open_interfaces[i];
        if (!open->active)
            continue;
        if (open->mount == RUNEC_UI_MOUNT_MODAL
                || open->mount == RUNEC_UI_MOUNT_OVERLAY) {
            open->active = 0;
            closed = 1;
        }
    }
    return closed;
}

static int ui_selftest_fail(char *error, size_t error_cap, const char *message) {
    if (error && error_cap > 0) {
        snprintf(error, error_cap, "%s", message ? message : "unknown failure");
    }
    return 0;
}

static int open_group_active(const RuneCUiState *ui, RuneCUiOpenMount mount,
                             RuneCUiTab tab, const char *group) {
    const RuneCUiOpenInterface *open = find_open_interface(ui, mount, tab);
    return open && group && strcmp(open->group, group) == 0;
}

static const RuneCUiComponentOverride *find_component_override(
    const RuneCUiState *ui, uint32_t component_id) {
    if (!ui || component_id == 0)
        return NULL;
    for (int i = 0; i < ui->component_override_count; i++) {
        if (ui->component_overrides[i].component_id == component_id)
            return &ui->component_overrides[i];
    }
    return NULL;
}

static int any_modal_or_overlay_active(const RuneCUiState *ui) {
    if (!ui)
        return 0;
    for (int i = 0; i < ui->open_interface_count; i++) {
        const RuneCUiOpenInterface *open = &ui->open_interfaces[i];
        if (!open->active)
            continue;
        if (open->mount == RUNEC_UI_MOUNT_MODAL
                || open->mount == RUNEC_UI_MOUNT_OVERLAY)
            return 1;
    }
    return 0;
}

static int store_has_listener_and_trigger(const RuneCUiInterfaceStore *store) {
    int saw_listener = 0;
    int saw_trigger = 0;
    if (!store || !store->loaded)
        return 0;
    for (int g = 0; g < store->group_count; g++) {
        const RuneCUiInterfaceGroup *group = &store->groups[g];
        for (int c = 0; c < group->component_count; c++) {
            const RuneCUiComponent *component = &group->components[c];
            if (component->listener_count > 0)
                saw_listener = 1;
            if (component->trigger_count > 0)
                saw_trigger = 1;
            if (saw_listener && saw_trigger)
                return 1;
        }
    }
    return 0;
}

int runec_ui_runtime_selftest(RuneCUiState *ui, char *error, size_t error_cap) {
    if (!ui)
        return ui_selftest_fail(error, error_cap, "ui state is null");
    if (!ui->decoded_ui_enabled || !ui->decoded_ui_ready)
        return ui_selftest_fail(error, error_cap, "decoded UI is not ready");

    const char *required_groups[] = {
        "toplevel_osrs_stretch", "chatbox", "orbs", "inventory",
        "wornitems", "stats", "prayerbook", "magic_spellbook",
        "combat_interface", "equipment", "ge_pricechecker_side", "deathkeep",
    };
    for (int i = 0; i < (int)(sizeof(required_groups) / sizeof(required_groups[0])); i++) {
        if (!decoded_group_available(ui, required_groups[i]))
            return ui_selftest_fail(error, error_cap, "required UI group missing");
    }
    if (!store_has_listener_and_trigger(&ui->interfaces))
        return ui_selftest_fail(error, error_cap, "listener/trigger metadata missing");

    if (!open_group_active(ui, RUNEC_UI_MOUNT_SCREEN, RUNEC_UI_TAB_NONE,
                           "toplevel_osrs_stretch"))
        return ui_selftest_fail(error, error_cap, "top-level interface not open");
    if (!open_group_active(ui, RUNEC_UI_MOUNT_CHAT, RUNEC_UI_TAB_NONE,
                           "chatbox"))
        return ui_selftest_fail(error, error_cap, "chatbox interface not open");
    if (!open_group_active(ui, RUNEC_UI_MOUNT_MAP, RUNEC_UI_TAB_NONE,
                           "orbs"))
        return ui_selftest_fail(error, error_cap, "orbs interface not open");

    for (int tab = 0; tab < RUNEC_UI_TAB_COUNT; tab++) {
        const char *group = decoded_group_for_tab((RuneCUiTab)tab);
        if (!group || !decoded_group_available(ui, group))
            continue;
        ui->active_tab = (RuneCUiTab)tab;
        if (!runec_ui_open_subinterface(ui, RUNEC_UI_MOUNT_SIDE_CONTENT,
                ui->active_tab, RUNEC_UI_TOP_SIDE_CONTAINER, group)) {
            return ui_selftest_fail(error, error_cap, "failed to open side tab");
        }
        if (!open_group_active(ui, RUNEC_UI_MOUNT_SIDE_CONTENT,
                               ui->active_tab, group)) {
            return ui_selftest_fail(error, error_cap, "side tab group mismatch");
        }
    }

    ui->active_tab = RUNEC_UI_TAB_INVENTORY;
    sync_decoded_component_overrides(ui);
    const RuneCUiItemContainerOverride *inventory = NULL;
    for (int i = 0; i < ui->item_container_override_count; i++) {
        if (ui->item_container_overrides[i].component_id
                == RUNEC_UI_INVENTORY_ITEMS_COMPONENT) {
            inventory = &ui->item_container_overrides[i];
            break;
        }
    }
    if (!inventory || inventory->slot_count != RUNEC_UI_INV_SLOT_COUNT
            || !inventory->slots[0].enabled)
        return ui_selftest_fail(error, error_cap, "inventory override missing");

    const RuneCUiComponentOverride *weapon = find_component_override(
        ui, RUNEC_UI_COMPONENT_ID(RUNEC_UI_GROUP_WORNITEMS, 15));
    if (!weapon || !(weapon->flags & RUNEC_UI_COMPONENT_OVERRIDE_ITEM))
        return ui_selftest_fail(error, error_cap, "equipment item override missing");

    const char *actions[] = {"Use", "Examine", "Drop"};
    runec_ui_open_context(ui, (Vector2){32, 32}, "Coins", actions, 3);
    if (!ui->context_open || ui->context_action_count != 3)
        return ui_selftest_fail(error, error_cap, "context menu smoke failed");

    set_selected_item_target(ui, 0);
    if (ui->selected_target.kind != RUNEC_UI_SELECTED_ITEM)
        return ui_selftest_fail(error, error_cap, "selected item target failed");
    runec_ui_clear_selected_target(ui);
    set_selected_spell_target(ui, 5, "Wind Strike");
    if (ui->selected_target.kind != RUNEC_UI_SELECTED_SPELL)
        return ui_selftest_fail(error, error_cap, "selected spell target failed");
    runec_ui_clear_selected_target(ui);

    RuneCUiListenerEvent event = {
        .kind = RUNEC_UI_LISTENER_ON_LOAD,
        .component_id = RUNEC_UI_MAGIC_FILTER_CONTAINER,
        .op = -1,
        .mouse = {0},
    };
    if (!dispatch_local_listener(ui, &event, NULL) || ui->magic_filter_open)
        return ui_selftest_fail(error, error_cap, "magic filter load failed");
    const RuneCUiComponentOverride *filter = find_component_override(
        ui, RUNEC_UI_MAGIC_FILTER_CONTAINER);
    if (!filter || !(filter->flags & RUNEC_UI_COMPONENT_OVERRIDE_HIDDEN)
            || !filter->hidden)
        return ui_selftest_fail(error, error_cap, "magic filter hidden override failed");

    event.kind = RUNEC_UI_LISTENER_ON_CLICK;
    event.component_id = RUNEC_UI_MAGIC_FILTER_BUTTON;
    if (!dispatch_local_listener(ui, &event, NULL) || !ui->magic_filter_open)
        return ui_selftest_fail(error, error_cap, "magic filter click failed");

    event.kind = RUNEC_UI_LISTENER_ON_OP;
    event.component_id = RUNEC_UI_WORN_EQUIPMENT_BUTTON;
    if (!dispatch_local_listener(ui, &event, NULL)
            || !open_group_active(ui, RUNEC_UI_MOUNT_MODAL,
                                  RUNEC_UI_TAB_NONE, "equipment")) {
        return ui_selftest_fail(error, error_cap, "equipment modal failed");
    }
    event.component_id = RUNEC_UI_WORN_PRICE_BUTTON;
    if (!dispatch_local_listener(ui, &event, NULL)
            || !open_group_active(ui, RUNEC_UI_MOUNT_OVERLAY,
                                  RUNEC_UI_TAB_NONE, "ge_pricechecker_side")) {
        return ui_selftest_fail(error, error_cap, "price checker overlay failed");
    }
    event.component_id = RUNEC_UI_WORN_DEATH_BUTTON;
    if (!dispatch_local_listener(ui, &event, NULL)
            || !open_group_active(ui, RUNEC_UI_MOUNT_MODAL,
                                  RUNEC_UI_TAB_NONE, "deathkeep")) {
        return ui_selftest_fail(error, error_cap, "deathkeep modal failed");
    }

    int previous_lines = ui->chat_line_count;
    event.component_id = RUNEC_UI_WORN_FOLLOWER_BUTTON;
    if (!dispatch_local_listener(ui, &event, NULL)
            || ui->chat_line_count < previous_lines
            || strcmp(ui->chat_lines[0], "You do not have a follower to call.") != 0) {
        return ui_selftest_fail(error, error_cap, "follower hook failed");
    }

    dispatch_local_transmit_listeners(ui);
    if (!close_modal_interfaces(ui) || any_modal_or_overlay_active(ui))
        return ui_selftest_fail(error, error_cap, "modal close smoke failed");
    return 1;
}

static const char *decoded_hit_title(const RuneCUiHitResult *hit) {
    if (hit->name[0])
        return hit->name;
    if (hit->text[0])
        return hit->text;
    if (hit->target_verb[0])
        return hit->target_verb;
    return "Component";
}

static const char *decoded_action_for_op(const RuneCUiHitResult *hit, int op) {
    if (!hit)
        return NULL;
    if (op >= 0 && op < hit->action_count
            && op < RUNEC_UI_INTERFACE_MAX_ACTIONS
            && hit->actions[op][0])
        return hit->actions[op];
    if (op == 0 && hit->target_verb[0])
        return hit->target_verb;
    return "Select";
}

static int decoded_primary_op(const RuneCUiState *ui,
                              const RuneCUiHitResult *hit) {
    if (!hit)
        return -1;
    for (int i = 0; i < hit->action_count
            && i < RUNEC_UI_INTERFACE_MAX_ACTIONS; i++) {
        if (hit->actions[i][0] && decoded_component_op_allowed(ui, hit, i))
            return i;
    }
    if (hit->target_verb[0] && decoded_component_op_allowed(ui, hit, 0))
        return 0;
    if (hit->click_mask != 0 && decoded_component_op_allowed(ui, hit, 0))
        return 0;
    if (hit->listener_mask & ((1u << RUNEC_UI_LISTENER_ON_OP)
            | (1u << RUNEC_UI_LISTENER_ON_CLICK))) {
        return 0;
    }
    return -1;
}

static int parse_int_suffix(const char *text, const char *prefix) {
    size_t prefix_len = strlen(prefix);
    if (!text || strncmp(text, prefix, prefix_len) != 0)
        return -1;
    const char *p = text + prefix_len;
    if (!*p)
        return -1;
    int value = 0;
    while (*p) {
        if (*p < '0' || *p > '9')
            return -1;
        value = value * 10 + (*p - '0');
        p++;
    }
    return value;
}

static int component_name_matches(const char *component_name,
                                  const char *display_name) {
    if (!component_name || !display_name)
        return 0;
    while (*component_name && *display_name) {
        char a = *component_name == '_' ? ' ' : *component_name;
        char b = *display_name;
        if (tolower((unsigned char)a) != tolower((unsigned char)b))
            return 0;
        component_name++;
        display_name++;
    }
    return *component_name == '\0' && *display_name == '\0';
}

static int skill_slot_from_component(const RuneCUiHitResult *hit) {
    if (!hit || hit->group_id != RUNEC_UI_GROUP_STATS)
        return -1;
    int count = (int)(sizeof(RUNEC_OSRS_SKILLS) / sizeof(RUNEC_OSRS_SKILLS[0]));
    for (int i = 0; i < count; i++) {
        if (component_name_matches(hit->name, RUNEC_OSRS_SKILLS[i].name))
            return i;
    }
    if (strcmp(hit->name, "total") == 0)
        return count;
    return -1;
}

static void title_from_component_name(const char *name, char *dst, size_t cap) {
    if (!dst || cap == 0)
        return;
    if (!name)
        name = "";
    size_t out = 0;
    int new_word = 1;
    for (size_t i = 0; name[i] && out + 1 < cap; i++) {
        char ch = name[i] == '_' ? ' ' : name[i];
        if (ch == ' ') {
            dst[out++] = ch;
            new_word = 1;
            continue;
        }
        if (new_word)
            ch = (char)toupper((unsigned char)ch);
        dst[out++] = ch;
        new_word = 0;
    }
    dst[out] = '\0';
}

static void spell_title_from_component(const RuneCUiHitResult *hit,
                                       char *dst, size_t cap) {
    if (!hit || !hit->name[0]) {
        copy_text(dst, cap, "Spell");
        return;
    }
    title_from_component_name(hit->name, dst, cap);
}

static int decoded_left_click(RuneCUiState *ui,
                              const RuneCUiHitResult *hit,
                              Vector2 mouse) {
    if (!ui || !hit)
        return 0;
    int op = decoded_primary_op(ui, hit);
    if (op < 0)
        return 0;

    if (ui->selected_target.kind == RUNEC_UI_SELECTED_ITEM) {
        ui->last_intent.kind = RUNEC_UI_INTENT_SELECTED_ITEM_ON_COMPONENT;
        ui->last_intent.primary = ui->selected_target.source_slot;
        ui->last_intent.secondary = (int)hit->component_id;
        ui->last_intent.position = mouse;
        snprintf(ui->last_intent.text, sizeof(ui->last_intent.text),
                 "%s -> %s", ui->selected_target.label,
                 decoded_hit_title(hit));
        return 1;
    }
    if (ui->selected_target.kind == RUNEC_UI_SELECTED_SPELL) {
        ui->last_intent.kind = RUNEC_UI_INTENT_SELECTED_SPELL_ON_COMPONENT;
        ui->last_intent.primary = ui->selected_target.source_slot;
        ui->last_intent.secondary = (int)hit->component_id;
        ui->last_intent.position = mouse;
        snprintf(ui->last_intent.text, sizeof(ui->last_intent.text),
                 "%s -> %s", ui->selected_target.label,
                 decoded_hit_title(hit));
        return 1;
    }

    if ((hit->listener_mask & (1u << RUNEC_UI_LISTENER_ON_CLICK))
            && dispatch_local_listener_for_hit(
                ui, hit, RUNEC_UI_LISTENER_ON_CLICK, op, mouse)) {
        return 1;
    }
    if (dispatch_local_listener_for_hit(
            ui, hit, RUNEC_UI_LISTENER_ON_OP, op, mouse)) {
        return 1;
    }

    if (hit->group_id == RUNEC_UI_GROUP_COMBAT) {
        int style = parse_int_suffix(hit->name, "");
        if (style >= 0 && style < 4) {
            ui->selected_combat_style = style;
            ui->last_intent.kind = RUNEC_UI_INTENT_COMBAT_STYLE;
            ui->last_intent.primary = style;
            ui->last_intent.position = mouse;
            const RuneCUiCombatStyleOption *option =
                combat_style_option_by_index(ui, style);
            copy_text(ui->last_intent.text, sizeof(ui->last_intent.text),
                      option ? option->label : "Attack style");
            return 1;
        }
        if (strcmp(hit->name, "retaliate") == 0) {
            ui->auto_retaliate = !ui->auto_retaliate;
            ui->last_intent.kind = RUNEC_UI_INTENT_AUTO_RETALIATE;
            ui->last_intent.primary = ui->auto_retaliate;
            ui->last_intent.position = mouse;
            return 1;
        }
        if (strcmp(hit->name, "special_attack") == 0) {
            ui->special_attack_enabled = !ui->special_attack_enabled;
            ui->last_intent.kind = RUNEC_UI_INTENT_SPECIAL_ATTACK;
            ui->last_intent.primary = ui->special_attack_enabled;
            ui->last_intent.secondary = ui->special_attack_energy;
            ui->last_intent.position = mouse;
            return 1;
        }
    }

    if (hit->group_id == RUNEC_UI_GROUP_WORNITEMS) {
        int slot = parse_int_suffix(hit->name, "slot");
        if (slot >= 0 && slot < RUNEC_UI_EQUIP_SLOT_COUNT) {
            ui->selected_equipment_slot = slot;
            ui->last_intent.kind = RUNEC_UI_INTENT_EQUIPMENT_SLOT;
            ui->last_intent.primary = slot;
            ui->last_intent.position = mouse;
            return 1;
        }
    }

    if (hit->group_id == RUNEC_UI_GROUP_MAGIC && hit->file_id >= 5
            && hit->file_id <= 200) {
        char name[64];
        spell_title_from_component(hit, name, sizeof(name));
        set_selected_spell_target(ui, (int)hit->file_id, name);
        ui->selected_target.source_component_id = hit->component_id;
        ui->last_intent.kind = RUNEC_UI_INTENT_SELECTED_SPELL;
        ui->last_intent.primary = (int)hit->file_id;
        ui->last_intent.secondary = (int)hit->component_id;
        ui->last_intent.position = mouse;
        copy_text(ui->last_intent.text, sizeof(ui->last_intent.text), name);
        return 1;
    }

    int skill = skill_slot_from_component(hit);
    if (skill >= 0) {
        ui->last_intent.kind = RUNEC_UI_INTENT_SKILL_SLOT;
        ui->last_intent.primary = skill;
        ui->last_intent.position = mouse;
        copy_text(ui->last_intent.text, sizeof(ui->last_intent.text),
                  skill < (int)(sizeof(RUNEC_OSRS_SKILLS) / sizeof(RUNEC_OSRS_SKILLS[0]))
                      ? RUNEC_OSRS_SKILLS[skill].name
                      : "Total level");
        return 1;
    }

    const char *action = decoded_action_for_op(hit, op);
    if (action) {
        ui->last_intent.kind = RUNEC_UI_INTENT_COMPONENT_ACTION;
        ui->last_intent.primary = (int)hit->component_id;
        ui->last_intent.secondary = op;
        ui->last_intent.position = mouse;
        copy_text(ui->last_intent.text, sizeof(ui->last_intent.text), action);
        return 1;
    }
    return 0;
}

static int open_decoded_context(RuneCUiState *ui, Vector2 mouse,
                                const RuneCUiHitResult *hit) {
    char action_buf[RUNEC_UI_CONTEXT_ACTIONS][RUNEC_UI_CONTEXT_TEXT_MAX];
    const char *actions[RUNEC_UI_CONTEXT_ACTIONS];
    int ops[RUNEC_UI_CONTEXT_ACTIONS];
    int count = 0;
    if (hit->target_verb[0] && count < RUNEC_UI_CONTEXT_ACTIONS
            && decoded_component_op_allowed(ui, hit, 0)) {
        copy_text(action_buf[count], sizeof(action_buf[count]), hit->target_verb);
        actions[count] = action_buf[count];
        ops[count] = 0;
        count++;
    }
    for (int i = 0; i < hit->action_count && count < RUNEC_UI_CONTEXT_ACTIONS; i++) {
        if (!hit->actions[i][0])
            continue;
        if (!decoded_component_op_allowed(ui, hit, i))
            continue;
        copy_text(action_buf[count], sizeof(action_buf[count]), hit->actions[i]);
        actions[count] = action_buf[count];
        ops[count] = i;
        count++;
    }
    if (count == 0 && hit->click_mask != 0
            && decoded_component_op_allowed(ui, hit, 0)) {
        actions[count] = "Select";
        ops[count] = 0;
        count++;
    }
    if (count == 0)
        return 0;
    set_context(ui, mouse, decoded_hit_title(hit), actions, count);
    for (int i = 0; i < count; i++)
        set_context_action_op(ui, i, ops[i]);
    set_context_source_component(ui, hit->component_id);
    return 1;
}

static int handle_bank_input(RuneCUiState *ui, const RuneCUiLayout *layout,
                             int screen_w, int screen_h, Vector2 mouse) {
    if (!ui->bank_open)
        return 0;

    Rectangle panel = bank_panel_rect(screen_w, screen_h, layout);
    Rectangle close = bank_close_rect(panel);
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f && CheckCollisionPointRec(mouse, panel)) {
        ui->bank_scroll -= (int)(wheel * RUNEC_UI_BANK_COLS);
        clamp_bank_scroll(ui);
        return 1;
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        ui->last_intent.kind = RUNEC_UI_INTENT_BANK_CLOSE;
        ui->last_intent.position = mouse;
        return 1;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        int slot = bank_slot_at(ui, panel, mouse);
        if (slot >= 0) {
            set_bank_context(ui, mouse, ui->bank_kind == RC_STORAGE_DEPOSIT_BOX
                ? RUNEC_UI_CONTEXT_BANK_INVENTORY : RUNEC_UI_CONTEXT_BANK, slot);
            return 1;
        }
        if (ui->active_tab == RUNEC_UI_TAB_INVENTORY) {
            slot = ui_inventory_slot_at(ui, layout, mouse);
            if (slot >= 0) {
                set_bank_context(ui, mouse, RUNEC_UI_CONTEXT_BANK_INVENTORY, slot);
                return 1;
            }
        }
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (CheckCollisionPointRec(mouse, close)) {
            ui->last_intent.kind = RUNEC_UI_INTENT_BANK_CLOSE;
            ui->last_intent.position = mouse;
            return 1;
        }

        for (int i = 0; ui->bank_kind != RC_STORAGE_DEPOSIT_BOX && i < 2; i++) {
            if (CheckCollisionPointRec(mouse, bank_mode_rect(panel, i))) {
                ui->bank_noted = i;
                return 1;
            }
        }
        for (int i = 0; i < 2; i++) {
            if (CheckCollisionPointRec(mouse, bank_deposit_all_rect(panel, i))) {
                ui->last_intent.kind = RUNEC_UI_INTENT_BANK_DEPOSIT_ALL;
                ui->last_intent.primary = i;
                return 1;
            }
        }
        for (int i = 0; ui->bank_kind != RC_STORAGE_DEPOSIT_BOX && i < RUNEC_UI_BANK_TAB_COUNT; i++) {
            if (CheckCollisionPointRec(mouse, bank_tab_rect(panel, i))) {
                if (ui->bank_active_tab != i) {
                    ui->bank_active_tab = i;
                    ui->bank_scroll = 0;
                }
                return 1;
            }
        }

        int bank_slot = bank_slot_at(ui, panel, mouse);
        if (bank_slot >= 0) {
            int deposit = ui->bank_kind == RC_STORAGE_DEPOSIT_BOX;
            const RuneCUiSlot *item = deposit ? &ui->inventory[bank_slot] : &ui->bank[bank_slot];
            if (item->enabled) {
                ui->last_intent.kind = deposit ? RUNEC_UI_INTENT_BANK_DEPOSIT : RUNEC_UI_INTENT_BANK_WITHDRAW;
                ui->last_intent.primary = bank_slot;
                ui->last_intent.secondary =
                    (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
                        ? 0 : 1;
                ui->last_intent.position = mouse;
            }
            return 1;
        }

        if (ui->active_tab == RUNEC_UI_TAB_INVENTORY) {
            int inv_slot = ui_inventory_slot_at(ui, layout, mouse);
            if (inv_slot >= 0) {
                if (ui->inventory[inv_slot].enabled) {
                    ui->last_intent.kind = RUNEC_UI_INTENT_BANK_DEPOSIT;
                    ui->last_intent.primary = inv_slot;
                    ui->last_intent.secondary =
                        (IsKeyDown(KEY_LEFT_SHIFT) ||
                         IsKeyDown(KEY_RIGHT_SHIFT)) ? 0 : 1;
                    ui->last_intent.position = mouse;
                }
                return 1;
            }
        }

        if (CheckCollisionPointRec(mouse, panel))
            return 1;
    }

    if ((IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) ||
         IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) &&
            CheckCollisionPointRec(mouse, panel)) {
        return 1;
    }
    return CheckCollisionPointRec(mouse, panel);
}

static Rectangle side_clan_plane_follow_rect(const RuneCUiLayout *layout) {
    return (Rectangle){
        layout->side_content.x + 8.0f,
        layout->side_content.y + 38.0f,
        58.0f,
        22.0f,
    };
}

static Rectangle side_clan_plane_button_rect(const RuneCUiLayout *layout,
                                             int plane) {
    return (Rectangle){
        layout->side_content.x + 72.0f + (float)plane * 28.0f,
        layout->side_content.y + 38.0f,
        24.0f,
        22.0f,
    };
}

// Beside the View/Player line: toggles the NPC occupied-tile highlight.
static Rectangle side_clan_npc_tiles_rect(const RuneCUiLayout *layout) {
    return (Rectangle){
        layout->side_content.x + 112.0f,
        layout->side_content.y + 62.0f,
        70.0f,
        20.0f,
    };
}

static Rectangle side_clan_dev_transport_rect(const RuneCUiLayout *layout,
                                              int idx) {
    int col = idx % 2;
    int row = idx / 2;
    return (Rectangle){
        layout->side_content.x + 8.0f + (float)col * 88.0f,
        layout->side_content.y + 100.0f + (float)row * 23.0f,
        82.0f,
        20.0f,
    };
}

static Rectangle dev_spellbook_button(const RuneCUiLayout *layout, int book) {
    return (Rectangle){layout->side_content.x + 8 + (book % 2) * 88,
        layout->side_content.y + 212 + (book / 2) * 25, 82, 22};
}

static int spell_grid_visible(const RuneCUiState *ui) {
    return ui->active_tab == RUNEC_UI_TAB_SPELLBOOK
        || (ui->active_tab == RUNEC_UI_TAB_COMBAT && ui->autocast_picker);
}

static int spell_grid_columns(const RuneCUiState *ui) {
    return ui->autocast_picker || ui->current_spellbook == 1 ? 4
        : ui->current_spellbook == 0 ? 8 : 6;
}

static int spell_grid_rows(const RuneCUiState *ui) {
    return ui->autocast_picker ? 9 : 10;
}

static int spell_display_slots(const RuneCUiState *ui, int *slots) {
    int count = 0;
    for (int i = 0; i < ui->spell_count; i++)
        if (!ui->autocast_picker || ui->spell_can_autocast[i]) slots[count++] = i;
    return count;
}

static Rectangle spell_grid_cell(const RuneCUiState *ui,
                                  const RuneCUiLayout *layout, int index) {
    int cols = spell_grid_columns(ui);
    int size = cols == 8 ? 22 : 24;
    int step = 184 / cols;
    return (Rectangle){layout->side_content.x + 3 + (index % cols) * step + (step - size) / 2,
        layout->side_content.y + (ui->autocast_picker ? 24 : 0) + (index / cols) * 24, size, size};
}

static void scroll_spell_grid(RuneCUiState *ui, int rows) {
    int slots[RUNEC_UI_SPELL_MAX];
    int count = spell_display_slots(ui, slots);
    int cols = spell_grid_columns(ui);
    int limit = (count + cols - 1) / cols - spell_grid_rows(ui);
    if (limit < 0) limit = 0;
    ui->spell_scroll += rows;
    if (ui->spell_scroll < 0) ui->spell_scroll = 0;
    if (ui->spell_scroll > limit) ui->spell_scroll = limit;
}

static int handle_spell_click(RuneCUiState *ui, const RuneCUiLayout *layout,
                              Vector2 mouse, int right_click) {
    if (!spell_grid_visible(ui) || !CheckCollisionPointRec(mouse, layout->side_content))
        return 0;
    if (ui->autocast_picker && mouse.y >= layout->side_content.y + 241) {
        if (!right_click && mouse.x < layout->side_content.x + 70) {
            ui->last_intent.kind = RUNEC_UI_INTENT_AUTOCAST_SPELL;
            ui->last_intent.primary = -1;
        }
        ui->autocast_picker = 0;
        ui->spell_scroll = 0;
        return 1;
    }
    int slots[RUNEC_UI_SPELL_MAX];
    int count = spell_display_slots(ui, slots);
    int cols = spell_grid_columns(ui);
    for (int i = 0; i < cols * spell_grid_rows(ui); i++) {
        int display = i + ui->spell_scroll * cols;
        if (display >= count) break;
        int slot = slots[display];
        if (!CheckCollisionPointRec(mouse, spell_grid_cell(ui, layout, i))) continue;
        const char *name = ui->spells[slot]->name;
        if (right_click) {
            const char *actions[4] = {"Cast"};
            int n = 1;
            if (ui->spell_can_autocast[slot]) {
                actions[n++] = "Autocast";
                actions[n++] = "Defensive autocast";
            }
            actions[n++] = "Examine";
            set_context(ui, mouse, name, actions, n);
            set_context_source(ui, RUNEC_UI_CONTEXT_SPELL, slot, 0);
        } else {
            int picker = ui->autocast_picker;
            if (!picker) set_selected_spell_target(ui, slot, name);
            ui->last_intent.kind = picker ? RUNEC_UI_INTENT_AUTOCAST_SPELL : RUNEC_UI_INTENT_SELECTED_SPELL;
            ui->last_intent.primary = slot;
            ui->last_intent.secondary = picker && ui->autocast_picker_defensive;
            ui->last_intent.position = mouse;
            copy_text(ui->last_intent.text, sizeof(ui->last_intent.text), name);
            if (picker) {
                ui->autocast_picker = 0;
                ui->spell_scroll = 0;
            }
        }
        return 1;
    }
    return 1;
}

static Rectangle autocast_button(const RuneCUiLayout *layout, int defensive) {
    return side_ref_rect(layout, (Rectangle){102, defensive ? 46 : 99, 68, 47});
}

static int handle_autocast_button(RuneCUiState *ui, const RuneCUiLayout *layout, Vector2 mouse) {
    if (ui->active_tab != RUNEC_UI_TAB_COMBAT || !ui->autocast_available) return 0;
    for (int defensive = 0; defensive <= 1; defensive++) {
        if (!CheckCollisionPointRec(mouse, autocast_button(layout, defensive))) continue;
        ui->autocast_picker = 1;
        ui->autocast_picker_defensive = defensive;
        ui->spell_scroll = 0;
        runec_ui_clear_selected_target(ui);
        close_context(ui);
        return 1;
    }
    return 0;
}

static int handle_side_clan_input(RuneCUiState *ui,
                                  const RuneCUiLayout *layout,
                                  Vector2 mouse) {
    if (!ui || !layout || ui->active_tab != RUNEC_UI_TAB_CLAN)
        return 0;
    if (ui->dev_spellbooks_enabled) {
        for (int book = 0; book < 4; book++) {
            if (CheckCollisionPointRec(mouse, dev_spellbook_button(layout, book))) {
                ui->last_intent.kind = RUNEC_UI_INTENT_DEV_SPELLBOOK;
                ui->last_intent.primary = book;
                ui->last_intent.position = mouse;
                return 1;
            }
        }
    }
    if (CheckCollisionPointRec(mouse, side_clan_plane_follow_rect(layout))) {
        ui->last_intent.kind = RUNEC_UI_INTENT_SCENE_PLANE;
        ui->last_intent.primary = -1;
        ui->last_intent.position = mouse;
        copy_text(ui->last_intent.text, sizeof(ui->last_intent.text),
                  "Follow plane");
        return 1;
    }
    for (int plane = 0; plane < RUNEC_UI_SCENE_PLANE_COUNT; plane++) {
        if (CheckCollisionPointRec(mouse,
                                   side_clan_plane_button_rect(layout,
                                                               plane))) {
            ui->last_intent.kind = RUNEC_UI_INTENT_SCENE_PLANE;
            ui->last_intent.primary = plane;
            ui->last_intent.position = mouse;
            snprintf(ui->last_intent.text, sizeof(ui->last_intent.text),
                     "Plane %d", plane);
            return 1;
        }
    }
    if (CheckCollisionPointRec(mouse, side_clan_npc_tiles_rect(layout))) {
        ui->last_intent.kind = RUNEC_UI_INTENT_DEV_NPC_TILES;
        ui->last_intent.primary = !ui->npc_tiles_overlay;
        ui->last_intent.position = mouse;
        return 1;
    }
    for (int i = 0; i < ui->dev_transport_count; i++) {
        if (CheckCollisionPointRec(mouse,
                                   side_clan_dev_transport_rect(layout, i))) {
            ui->last_intent.kind = RUNEC_UI_INTENT_DEV_TRANSPORT;
            ui->last_intent.primary = i;
            ui->last_intent.position = mouse;
            return 1;
        }
    }
    return CheckCollisionPointRec(mouse, layout->side_content);
}

static int handle_chat_tab_input(RuneCUiState *ui, const RuneCUiLayout *layout,
                                 Vector2 mouse) {
    if (!ui || !layout)
        return 0;
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        return 0;
    for (int i = 0; i < RUNEC_UI_CHAT_TAB_COUNT; i++) {
        Rectangle r = {
            layout->chat.x + 6 + i * 72.0f,
            layout->chat_controls.y + 1,
            68,
            21,
        };
        if (CheckCollisionPointRec(mouse, r)) {
            ui->active_chat_tab = i;
            return 1;
        }
    }
    return 0;
}

int runec_ui_handle_input(RuneCUiState *ui, int screen_w, int screen_h) {
    RuneCUiLayout layout;
    ui_layout(screen_w, screen_h, &layout);
    apply_decoded_mounts(ui, screen_w, screen_h, &layout);
    sync_decoded_component_overrides(ui);
    Vector2 mouse = GetMousePosition();
    float dt = GetFrameTime();
    clear_intent(ui);
    ui->context_dismissed = 0;

    if (ui->context_open && !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        RuneCContextMenuLayout context = context_menu_layout_for_ui(ui);
        if (!runec_context_menu_contains_margin(
                &context, (int)mouse.x, (int)mouse.y,
                RUNEC_CONTEXT_MENU_DISMISS_MARGIN)) {
            close_context(ui);
        }
    }

    for (int i = 0; i < RUNEC_UI_TAB_COUNT; i++) {
        if (ui->tab_press_timer[i] <= 0.0f)
            continue;
        ui->tab_press_timer[i] -= dt;
        if (ui->tab_press_timer[i] < 0.0f)
            ui->tab_press_timer[i] = 0.0f;
    }

    if (handle_bank_amount_input(ui)) return 1;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && handle_context_click(ui, mouse))
        return 1;
    if (ui->context_open) {
        if (IsKeyPressed(KEY_ESCAPE)) close_context(ui);
        return 1;
    }

    if (ui->chat_focused) {
        int ch = GetCharPressed();
        while (ch > 0) {
            size_t len = strlen(ui->chat_input);
            if (ch >= 32 && ch <= 126 && len + 1 < sizeof(ui->chat_input)) {
                ui->chat_input[len] = (char)ch;
                ui->chat_input[len + 1] = '\0';
            }
            ch = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE)) {
            size_t len = strlen(ui->chat_input);
            if (len > 0)
                ui->chat_input[len - 1] = '\0';
        }
        if (IsKeyPressed(KEY_ENTER)) {
            if (ui->chat_input[0]) {
                ui->last_intent.kind = RUNEC_UI_INTENT_CHAT_SEND;
                copy_text(ui->last_intent.text, sizeof(ui->last_intent.text),
                          ui->chat_input);
                char line[96];
                snprintf(line, sizeof(line), "You: %.88s", ui->chat_input);
                ui_add_chat(ui, line);
                ui->chat_input[0] = '\0';
            }
            ui->chat_focused = 0;
        }
        if (IsKeyPressed(KEY_ESCAPE))
            ui->chat_focused = 0;
    }
    if (!ui->chat_focused && ui->selected_target.kind != RUNEC_UI_SELECTED_NONE
            && IsKeyPressed(KEY_ESCAPE)) {
        runec_ui_clear_selected_target(ui);
        ui->last_intent.kind = RUNEC_UI_INTENT_SELECTED_TARGET_CANCEL;
        return 1;
    }
    if (!ui->chat_focused && IsKeyPressed(KEY_ESCAPE)
            && close_modal_interfaces(ui)) {
        return 1;
    }

    if (handle_chat_tab_input(ui, &layout, mouse))
        return 1;

    if (handle_bank_input(ui, &layout, screen_w, screen_h, mouse))
        return 1;

    if (spell_grid_visible(ui) && CheckCollisionPointRec(mouse, layout.side_content))
        scroll_spell_grid(ui, -(int)GetMouseWheelMove());
    if (ui->autocast_picker && IsKeyPressed(KEY_ESCAPE)) {
        ui->autocast_picker = 0;
        ui->spell_scroll = 0;
        return 1;
    }

    if (ui->drag.active && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        RuneCUiDragState drag = ui->drag;
        ui->drag.active = 0;
        ui->drag.source_kind = RUNEC_UI_CONTEXT_NONE;
        ui->drag.source_slot = -1;

        if (drag.source_kind == RUNEC_UI_CONTEXT_INVENTORY) {
            int target = ui_inventory_slot_at(ui, &layout, mouse);
            float dx = mouse.x - drag.start.x;
            float dy = mouse.y - drag.start.y;
            int moved = fabsf(dx) > 3.0f || fabsf(dy) > 3.0f;
            if (target >= 0 && moved) {
                ui->last_intent.kind = RUNEC_UI_INTENT_INVENTORY_DRAG;
                ui->last_intent.primary = drag.source_slot;
                ui->last_intent.secondary = target;
                ui->last_intent.position = mouse;
                return 1;
            }
            int previous = ui->selected_inventory_slot;
            ui->selected_inventory_slot = drag.source_slot;
            ui->last_intent.kind = RUNEC_UI_INTENT_INVENTORY_SLOT;
            ui->last_intent.primary = drag.source_slot;
            ui->last_intent.secondary = previous;
            ui->last_intent.position = mouse;
            return 1;
        }

        if (drag.source_kind == RUNEC_UI_CONTEXT_EQUIPMENT) {
            ui->selected_equipment_slot = drag.source_slot;
            ui->last_intent.kind = RUNEC_UI_INTENT_EQUIPMENT_SLOT;
            ui->last_intent.primary = drag.source_slot;
            ui->last_intent.position = mouse;
            return 1;
        }
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        RuneCUiHitResult modal_hit = {0};
        if (decoded_modal_hit(ui, &layout, screen_w, screen_h, mouse,
                              &modal_hit)) {
            if (modal_hit.component_id != 0)
                decoded_left_click(ui, &modal_hit, mouse);
            return 1;
        }

        if (CheckCollisionPointRec(mouse, layout.chat_input)) {
            ui->chat_focused = 1;
            ui->last_intent.kind = RUNEC_UI_INTENT_CHAT_FOCUS;
            ui->last_intent.position = mouse;
            return 1;
        }

        for (int i = 0; i < RUNEC_UI_TAB_COUNT; i++) {
            if (CheckCollisionPointRec(mouse, layout.tab[i])) {
                runec_ui_set_active_tab(ui, (RuneCUiTab)i);
                ui->tab_press_timer[i] = OSRS_TAB_PRESS_SECONDS;
                ui->last_intent.kind = RUNEC_UI_INTENT_TAB;
                ui->last_intent.primary = i;
                ui->last_intent.position = mouse;
                return 1;
            }
        }

        if (CheckCollisionPointRec(mouse, layout.run_orb)) {
            ui->last_intent.kind = RUNEC_UI_INTENT_RUN_TOGGLE;
            ui->last_intent.primary = !ui->running;
            ui->last_intent.position = mouse;
            return 1;
        }

        if (CheckCollisionPointRec(mouse, layout.prayer_orb)) {
            ui->last_intent.kind = RUNEC_UI_INTENT_QUICK_PRAYER_TOGGLE;
            ui->last_intent.primary = 1;
            ui->last_intent.position = mouse;
            copy_text(ui->last_intent.text, sizeof(ui->last_intent.text),
                      "Quick-prayer");
            return 1;
        }

        if (CheckCollisionPointRec(mouse, layout.spec_orb)) {
            ui->special_attack_enabled = !ui->special_attack_enabled;
            ui->last_intent.kind = RUNEC_UI_INTENT_SPECIAL_ATTACK;
            ui->last_intent.primary = ui->special_attack_enabled;
            ui->last_intent.secondary = ui->special_attack_energy;
            ui->last_intent.position = mouse;
            return 1;
        }

        if (CheckCollisionPointRec(mouse, layout.minimap)) {
            ui->last_intent.kind = RUNEC_UI_INTENT_MINIMAP_CLICK;
            ui->last_intent.primary = (int)(mouse.x - layout.minimap.x);
            ui->last_intent.secondary = (int)(mouse.y - layout.minimap.y);
            ui->last_intent.position = mouse;
            return 1;
        }

        if (handle_side_clan_input(ui, &layout, mouse))
            return 1;
        if (handle_spell_click(ui, &layout, mouse, 0))
            return 1;
        if (handle_autocast_button(ui, &layout, mouse))
            return 1;

        RuneCUiHitResult decoded_hit = {0};
        if (decoded_side_hit(ui, &layout, mouse, &decoded_hit)
                && decoded_left_click(ui, &decoded_hit, mouse)) {
            return 1;
        }

        if (ui->active_tab == RUNEC_UI_TAB_COMBAT) {
            const RuneCUiCombatStyleOption *style =
                combat_style_at(ui, &layout, mouse);
            if (style) {
                ui->selected_combat_style = style->style_index;
                ui->last_intent.kind = RUNEC_UI_INTENT_COMBAT_STYLE;
                ui->last_intent.primary = style->style_index;
                ui->last_intent.position = mouse;
                copy_text(ui->last_intent.text, sizeof(ui->last_intent.text),
                          style->label);
                return 1;
            }
            if (CheckCollisionPointRec(mouse, side_ref_rect(&layout, RUNEC_OSRS_COMBAT_RETALIATE))) {
                ui->auto_retaliate = !ui->auto_retaliate;
                ui->last_intent.kind = RUNEC_UI_INTENT_AUTO_RETALIATE;
                ui->last_intent.primary = ui->auto_retaliate;
                ui->last_intent.position = mouse;
                return 1;
            }
            if (CheckCollisionPointRec(mouse, side_ref_rect(&layout, RUNEC_OSRS_COMBAT_SPECIAL_BAR))) {
                ui->special_attack_enabled = !ui->special_attack_enabled;
                ui->last_intent.kind = RUNEC_UI_INTENT_SPECIAL_ATTACK;
                ui->last_intent.primary = ui->special_attack_enabled;
                ui->last_intent.secondary = ui->special_attack_energy;
                ui->last_intent.position = mouse;
                return 1;
            }
        } else if (ui->active_tab == RUNEC_UI_TAB_INVENTORY) {
            int slot = ui_inventory_slot_at(ui, &layout, mouse);
            if (slot >= 0) {
                if (ui->selected_target.kind == RUNEC_UI_SELECTED_ITEM) {
                    ui->last_intent.kind = RUNEC_UI_INTENT_SELECTED_ITEM_ON_ITEM;
                    ui->last_intent.primary = ui->selected_target.source_slot;
                    ui->last_intent.secondary = slot;
                    ui->last_intent.position = mouse;
                    snprintf(ui->last_intent.text, sizeof(ui->last_intent.text),
                             "%s -> %s", ui->selected_target.label,
                             ui->inventory[slot].enabled
                                ? ui->inventory[slot].label : "slot");
                    return 1;
                }
                if (ui->selected_target.kind == RUNEC_UI_SELECTED_SPELL) {
                    ui->last_intent.kind = RUNEC_UI_INTENT_SELECTED_SPELL_ON_ITEM;
                    ui->last_intent.primary = ui->selected_target.source_slot;
                    ui->last_intent.secondary = slot;
                    ui->last_intent.position = mouse;
                    snprintf(ui->last_intent.text, sizeof(ui->last_intent.text),
                             "%s -> %s", ui->selected_target.label,
                             ui->inventory[slot].enabled
                                ? ui->inventory[slot].label : "slot");
                    return 1;
                }
                ui->drag.active = 1;
                ui->drag.source_kind = RUNEC_UI_CONTEXT_INVENTORY;
                ui->drag.source_slot = slot;
                ui->drag.start = mouse;
                return 1;
            }
        } else if (ui->active_tab == RUNEC_UI_TAB_EQUIPMENT) {
            int slot = ui_equipment_slot_at(ui, &layout, mouse);
            if (slot >= 0) {
                ui->drag.active = 1;
                ui->drag.source_kind = RUNEC_UI_CONTEXT_EQUIPMENT;
                ui->drag.source_slot = slot;
                ui->drag.start = mouse;
                return 1;
            }
        } else if (ui->active_tab == RUNEC_UI_TAB_PRAYER) {
            if (ui->quick_prayer_setup && CheckCollisionPointRec(mouse,
                    side_ref_rect(&layout, (Rectangle){4, 234, 182, 24}))) {
                ui->quick_prayer_setup = 0;
                return 1;
            }
            int slot = grid_index_at(&layout, mouse, ui->prayers.count, 5, 8, 8, 36, 36, 34, 34);
            if (slot >= 0) {
                int id = ui->prayers.ids[slot];
                ui->last_intent.kind = ui->quick_prayer_setup
                    ? RUNEC_UI_INTENT_QUICK_PRAYER_SLOT : RUNEC_UI_INTENT_PRAYER_SLOT;
                ui->last_intent.primary = id;
                ui->last_intent.position = mouse;
                copy_text(ui->last_intent.text, sizeof(ui->last_intent.text),
                          runec_prayer_ui_ref(id)->name);
                return 1;
            }
        } else if (ui->active_tab == RUNEC_UI_TAB_SKILLS) {
            int slot = skill_slot_at(&layout, mouse);
            if (slot >= 0) {
                ui->last_intent.kind = RUNEC_UI_INTENT_SKILL_SLOT;
                ui->last_intent.primary = slot;
                ui->last_intent.position = mouse;
                copy_text(ui->last_intent.text, sizeof(ui->last_intent.text),
                          slot < (int)(sizeof(RUNEC_OSRS_SKILLS) / sizeof(RUNEC_OSRS_SKILLS[0]))
                              ? RUNEC_OSRS_SKILLS[slot].name
                              : "Total level");
                return 1;
            }
        }

        if (decoded_hit.component_id != 0)
            return 1;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        RuneCUiHitResult modal_hit = {0};
        if (decoded_modal_hit(ui, &layout, screen_w, screen_h, mouse,
                              &modal_hit)) {
            if (modal_hit.component_id != 0)
                open_decoded_context(ui, mouse, &modal_hit);
            return 1;
        }
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && mouse_over_ui(&layout, mouse)) {
        if (CheckCollisionPointRec(mouse, layout.prayer_orb)) {
            const char *actions[] = {
                ui->prayers.quick_active ? "Deactivate quick-prayers" : "Activate quick-prayers",
                "Setup quick-prayers"};
            set_context(ui, mouse, "Quick-prayers", actions, 2);
            set_context_source(ui, RUNEC_UI_CONTEXT_PRAYER, -1, 0);
            return 1;
        }
        if (handle_spell_click(ui, &layout, mouse, 1))
            return 1;
        if (ui->active_tab == RUNEC_UI_TAB_COMBAT) {
            const RuneCUiCombatStyleOption *style =
                combat_style_at(ui, &layout, mouse);
            if (style) {
                static const char *actions[] = {"Select", "Examine"};
                set_context(ui, mouse, style->label, actions, 2);
                return 1;
            }
            if (CheckCollisionPointRec(mouse, side_ref_rect(&layout, RUNEC_OSRS_COMBAT_RETALIATE))) {
                static const char *actions[] = {"Toggle", "Examine"};
                set_context(ui, mouse, "Auto Retaliate", actions, 2);
                return 1;
            }
            if (CheckCollisionPointRec(mouse, side_ref_rect(&layout, RUNEC_OSRS_COMBAT_SPECIAL_BAR))) {
                static const char *actions[] = {"Use", "Examine"};
                set_context(ui, mouse, "Special Attack", actions, 2);
                return 1;
            }
        }
        if (ui->active_tab == RUNEC_UI_TAB_INVENTORY) {
            int slot = ui_inventory_slot_at(ui, &layout, mouse);
            if (slot >= 0) {
                static const char *empty_actions[] = {"Cancel"};
                const char *title = ui->inventory[slot].enabled
                    ? ui->inventory[slot].label
                    : "Empty inventory slot";
                if (ui->inventory[slot].enabled) {
                    set_item_slot_context(ui, mouse, &ui->inventory[slot]);
                    set_context_source(ui, RUNEC_UI_CONTEXT_INVENTORY, slot,
                                       ui->inventory[slot].item_id);
                } else {
                    set_context(ui, mouse, title, empty_actions, 1);
                }
                return 1;
            }
        }
        if (ui->active_tab == RUNEC_UI_TAB_EQUIPMENT) {
            int slot = ui_equipment_slot_at(ui, &layout, mouse);
            if (slot >= 0) {
                if (ui->equipment[slot].enabled)
                    set_item_slot_context(ui, mouse, &ui->equipment[slot]);
                else {
                    static const char *empty_actions[] = {"Cancel"};
                    set_context(ui, mouse, g_equipment_names[slot],
                                empty_actions, 1);
                }
                set_context_source(ui, RUNEC_UI_CONTEXT_EQUIPMENT, slot,
                                   ui->equipment[slot].item_id);
                return 1;
            }
        }
        if (ui->active_tab == RUNEC_UI_TAB_PRAYER) {
            int slot = grid_index_at(&layout, mouse, ui->prayers.count, 5, 8, 8, 36, 36, 34, 34);
            if (slot >= 0) {
                int id = ui->prayers.ids[slot];
                const char *actions[] = {
                    ui->prayers.active & (1u << id) ? "Deactivate" : "Activate", "Quick-prayer"};
                set_context(ui, mouse, runec_prayer_ui_ref(id)->name, actions, 2);
                set_context_source(ui, RUNEC_UI_CONTEXT_PRAYER, id, 0);
                return 1;
            }
        }
        RuneCUiHitResult hit = {0};
        if (decoded_side_hit(ui, &layout, mouse, &hit)
                && open_decoded_context(ui, mouse, &hit)) {
            return 1;
        }
        static const char *actions[] = {"Cancel"};
        set_context(ui, mouse, "RuneC", actions, 1);
        return 1;
    }

    RuneCUiHitResult capture_hit = {0};
    if (decoded_modal_hit(ui, &layout, screen_w, screen_h, mouse,
                          &capture_hit)) {
        return 1;
    }
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !ui->context_open)
        return 0;
    return mouse_over_ui(&layout, mouse) || ui->chat_focused;
}

static void draw_text_shadow(const RuneCUiState *ui, const char *text,
                             float x, float y, float size, Color color) {
    runec_ui_draw_text_shadow(&ui->assets, text, x, y, size, color);
}

static void draw_centered_text(const RuneCUiState *ui, const char *text,
                               Rectangle rect, float size, Color color) {
    if (size < 12.0f)
        size = 12.0f;
    Font font = runec_ui_font_for_size(&ui->assets, size);
    Vector2 m = MeasureTextEx(font, text, size, 0);
    draw_text_shadow(ui, text, rect.x + (rect.width - m.x) * 0.5f,
                     rect.y + (rect.height - m.y) * 0.5f, size, color);
}

static int draw_asset_centered(const RuneCUiState *ui, const char *name,
                               Rectangle rect, float max_w, float max_h, Color tint) {
    const Texture2D *tex = runec_ui_asset(&ui->assets, name);
    if (!tex)
        return 0;
    float scale = fmin2(max_w / (float)tex->width, max_h / (float)tex->height);
    if (scale > 1.0f)
        scale = 1.0f;
    Rectangle dst = {
        rect.x + (rect.width - tex->width * scale) * 0.5f,
        rect.y + (rect.height - tex->height * scale) * 0.5f,
        tex->width * scale,
        tex->height * scale,
    };
    runec_ui_draw_asset(&ui->assets, name, dst, tint);
    return 1;
}

static void draw_slot_box(Rectangle r, int selected) {
    DrawRectangleRec(r, (Color){20, 17, 12, selected ? 220 : 150});
    DrawRectangleLinesEx(r, selected ? 2.0f : 1.0f,
                         selected ? OSRS_YELLOW : (Color){87, 70, 48, 210});
}

static void draw_asset_tiled(const RuneCUiState *ui, const char *name,
                             Rectangle dst, Color tint) {
    const Texture2D *tex = runec_ui_asset(&ui->assets, name);
    if (!tex) {
        DrawRectangleRec(dst, OSRS_PANEL);
        return;
    }
    for (float y = dst.y; y < dst.y + dst.height; y += (float)tex->height) {
        for (float x = dst.x; x < dst.x + dst.width; x += (float)tex->width) {
            float w = fmin2((float)tex->width, dst.x + dst.width - x);
            float h = fmin2((float)tex->height, dst.y + dst.height - y);
            DrawTexturePro(*tex, (Rectangle){0, 0, w, h},
                           (Rectangle){x, y, w, h}, (Vector2){0, 0}, 0, tint);
        }
    }
}

static void draw_side_chrome(const RuneCUiState *ui, const RuneCUiLayout *layout) {
    Rectangle backing = {layout->side.x + 20, layout->side.y + 27, 200, 281};
    draw_asset_tiled(ui, "tradebacking_dark", backing, WHITE);
    if (!runec_ui_asset_ready(&ui->assets, "tradebacking_dark"))
        DrawRectangleRec(backing, OSRS_PANEL);

    runec_ui_draw_asset(&ui->assets, "osrs_stretch_side_topbottom_0",
                        (Rectangle){layout->side.x, layout->side.y + RUNEC_OSRS_SIDE_TOP_Y,
                                    241, 37}, WHITE);
    runec_ui_draw_asset(&ui->assets, "osrs_stretch_side_topbottom_1",
                        (Rectangle){layout->side.x, layout->side.y + RUNEC_OSRS_SIDE_BOTTOM_Y,
                                    241, 37}, WHITE);
    runec_ui_draw_asset(&ui->assets, "osrs_stretch_side_columns_0",
                        (Rectangle){layout->side.x + 2, layout->side.y + 37, 26, 261}, WHITE);
    runec_ui_draw_asset(&ui->assets, "osrs_stretch_side_columns_1",
                        (Rectangle){layout->side.x + 212, layout->side.y + 37, 26, 261}, WHITE);

    for (int i = 0; i < (int)(sizeof(RUNEC_OSRS_SIDE_STONES) / sizeof(RUNEC_OSRS_SIDE_STONES[0])); i++) {
        const RuneCUiStoneRef *ref = &RUNEC_OSRS_SIDE_STONES[i];
        if (ref->logical_tab != ui->active_tab)
            continue;
        float row_y = layout->side.y + (i < 7 ? RUNEC_OSRS_SIDE_TOP_Y : RUNEC_OSRS_SIDE_BOTTOM_Y);
        float pressed = ui->tab_press_timer[ui->active_tab] > 0.0f ? 1.0f : 0.0f;
        Rectangle stone = {layout->side.x + ref->rect.x,
                           row_y + ref->rect.y + pressed,
                           ref->rect.width, ref->rect.height};
        draw_asset_tiled(ui, ref->stone_asset, stone, WHITE);
        DrawRectangleRec(stone, (Color){145, 22, 18, pressed > 0.0f ? 72 : 44});
        break;
    }

    for (int i = 0; i < (int)(sizeof(RUNEC_OSRS_SIDE_STONES) / sizeof(RUNEC_OSRS_SIDE_STONES[0])); i++) {
        const RuneCUiStoneRef *ref = &RUNEC_OSRS_SIDE_STONES[i];
        float row_y = layout->side.y + (i < 7 ? RUNEC_OSRS_SIDE_TOP_Y : RUNEC_OSRS_SIDE_BOTTOM_Y);
        float pressed = ref->logical_tab == ui->active_tab
            && ui->tab_press_timer[ui->active_tab] > 0.0f ? 1.0f : 0.0f;
        Rectangle icon = {layout->side.x + ref->icon_rect.x, row_y + ref->icon_rect.y,
                          ref->icon_rect.width, ref->icon_rect.height};
        icon.y += pressed;
        runec_ui_draw_asset(&ui->assets, ref->icon_asset, icon, WHITE);
    }
}

static void draw_orb(const RuneCUiState *ui, Rectangle rect, const char *filler,
                     const char *icon, int value, int max_value, Color color) {
    runec_ui_draw_asset(&ui->assets, "orb_frame_0", rect, WHITE);
    if (!runec_ui_asset_ready(&ui->assets, "orb_frame_0"))
        DrawRectangleRounded((Rectangle){rect.x + 0, rect.y + 7, 34, 20}, 0.22f, 5,
                             (Color){50, 46, 37, 235});
    Rectangle fill = {rect.x + 27, rect.y + 4, 26, 26};
    runec_ui_draw_asset(&ui->assets, "orb_filler_0", fill, WHITE);
    runec_ui_draw_asset(&ui->assets, filler, fill, WHITE);
    if (!runec_ui_asset_ready(&ui->assets, filler))
        DrawCircle((int)(fill.x + 14), (int)(fill.y + 14), 12, color);
    draw_asset_centered(ui, icon, fill, 22, 22, WHITE);

    char text[16];
    snprintf(text, sizeof(text), "%d", value);
    Color text_color = max_value > 0 && value < max_value / 3 ? OSRS_RED : OSRS_GREEN;
    draw_centered_text(ui, text, (Rectangle){rect.x + 3, rect.y + 14, 24, 13}, 12, text_color);
}

static void draw_minimap(const RuneCUiState *ui, const RuneCUiLayout *layout) {
    Vector2 center = {layout->minimap.x + layout->minimap.width * 0.5f,
                      layout->minimap.y + layout->minimap.height * 0.5f};
    if (ui->minimap_texture_ready) {
        DrawTexturePro(ui->minimap_texture,
                       (Rectangle){0, 0, 152, 152},
                       layout->minimap, (Vector2){0, 0}, 0, WHITE);
    } else {
        DrawCircle((int)center.x, (int)center.y, 72.0f, (Color){85, 124, 52, 255});
    }

    for (int i = 0; i < ui->minimap_dot_count; i++) {
        const RuneCUiMinimapDot *dot = &ui->minimap_dots[i];
        float px = center.x + dot->dx * 4.0f;
        float py = center.y - dot->dy * 4.0f;
        float dx = px - center.x;
        float dy = py - center.y;
        if (dx * dx + dy * dy > 68.0f * 68.0f)
            continue;
        Color c = OSRS_YELLOW;
        float radius = 2.0f;
        if (dot->kind == RUNEC_UI_MINIMAP_DOT_PLAYER) {
            c = WHITE;
            radius = 3.0f;
        } else if (dot->kind == RUNEC_UI_MINIMAP_DOT_DESTINATION) {
            c = OSRS_RED;
            radius = 3.0f;
        }
        DrawCircle((int)px, (int)py, radius, c);
    }

    Rectangle cover = {layout->map.x + RUNEC_OSRS_MAP_SURROUND_X,
                       layout->map.y + RUNEC_OSRS_MAP_SURROUND_Y,
                       RUNEC_OSRS_MAP_SURROUND_W, RUNEC_OSRS_MAP_SURROUND_H};
    runec_ui_draw_asset(&ui->assets, "osrs_stretch_mapsurround", cover, WHITE);
    if (!runec_ui_asset_ready(&ui->assets, "osrs_stretch_mapsurround")) {
        DrawCircleLines((int)center.x, (int)center.y, 73.0f, (Color){51, 48, 35, 255});
        DrawCircleLines((int)center.x, (int)center.y, 70.0f, (Color){180, 166, 104, 255});
    }

    const Texture2D *compass = runec_ui_asset(&ui->assets, "compass");
    if (compass) {
        Rectangle source = {0, 0, (float)compass->width,
                            (float)compass->height};
        Rectangle destination = {
            layout->compass.x + layout->compass.width * 0.5f,
            layout->compass.y + layout->compass.height * 0.5f,
            layout->compass.width,
            layout->compass.height,
        };
        Vector2 origin = {layout->compass.width * 0.5f,
                          layout->compass.height * 0.5f};
        DrawTexturePro(*compass, source, destination, origin,
                       ui->minimap_rotation
                           * (180.0f / 3.14159265358979323846f),
                       WHITE);
    } else {
        runec_ui_draw_asset(&ui->assets, "resize_compass_mask", layout->compass, WHITE);
        draw_text_shadow(ui, "N", layout->compass.x + 16, layout->compass.y + 12, 14, OSRS_ORANGE);
    }

    runec_ui_draw_asset(&ui->assets, "tli_button01_orb01_34x34_0", layout->xp_orb, WHITE);
    if (!runec_ui_asset_ready(&ui->assets, "tli_button01_orb01_34x34_0"))
        runec_ui_draw_asset(&ui->assets, "ring_34_0", layout->xp_orb, WHITE);
    draw_asset_centered(ui, "orb_xp_0", layout->xp_orb, 24, 24, WHITE);
    if (!runec_ui_asset_ready(&ui->assets, "orb_xp_0"))
        draw_centered_text(ui, "XP", layout->xp_orb, 12, OSRS_YELLOW);

    draw_orb(ui, layout->hp_orb, "orb_filler_1", "orb_icon_0",
             ui->hitpoints, ui->hitpoints_max, OSRS_RED);
    draw_orb(ui, layout->prayer_orb, "orb_filler_4", "orb_icon_1",
             ui->prayer_points, ui->prayer_points_max, OSRS_BLUE);
    draw_orb(ui, layout->run_orb, "orb_filler_5", "orb_icon_2",
             ui->run_energy, 100, OSRS_GREEN);
    draw_orb(ui, layout->spec_orb, "orb_filler_9", "orb_icon_6",
             ui->special_attack_energy, 100, OSRS_YELLOW);

    runec_ui_draw_asset(&ui->assets, "ring_30", layout->worldmap_button, WHITE);
    draw_asset_centered(ui, "worldmap_icon_0", layout->worldmap_button, 22, 22, WHITE);
    if (!runec_ui_asset_ready(&ui->assets, "worldmap_icon_0"))
        draw_centered_text(ui, "?", layout->worldmap_button, 14, OSRS_YELLOW);
}

static void draw_chat(RuneCUiState *ui, const RuneCUiLayout *layout) {
    int decoded_chat = 0;
    const char *decoded_chat_env = getenv("RUNEC_UI_DECODED_CHAT");
    int decoded_chat_enabled = decoded_chat_env && decoded_chat_env[0]
        && strcmp(decoded_chat_env, "0") != 0;
    if (decoded_chat_enabled && ui->decoded_ui_enabled && ui->decoded_ui_ready) {
        const char *group =
            open_group_for_mount(ui, RUNEC_UI_MOUNT_CHAT, RUNEC_UI_TAB_NONE);
        RuneCUiComponentOverrides overrides = decoded_overrides(ui);
        decoded_chat = runec_ui_interfaces_draw_group_ex(
            &ui->interfaces, &ui->assets, group ? group : "chatbox",
            layout->chat, &overrides);
    }
    if (!decoded_chat)
        runec_ui_draw_asset(&ui->assets, "chatbox_bg", layout->chat, WHITE);
    if (!decoded_chat && !runec_ui_asset_ready(&ui->assets, "chatbox_bg"))
        DrawRectangleRec(layout->chat, (Color){0, 0, 0, 115});
    DrawRectangleRec(layout->chat_messages, (Color){0, 0, 0, 84});

    for (int i = ui->chat_line_count - 1; i >= 0; i--) {
        int row = ui->chat_line_count - 1 - i;
        draw_text_shadow(ui, ui->chat_lines[i],
                         layout->chat_messages.x + 3,
                         layout->chat_messages.y + row * 17.0f, 14,
                         OSRS_ORANGE);
    }

    const char *prompt = ui->chat_focused ? ">" : "Click here to chat";
    char input[160];
    snprintf(input, sizeof(input), "%s %s", prompt, ui->chat_input);
    if (ui->bank_amount_source)
        snprintf(input, sizeof(input), "Enter amount: %s*", ui->bank_amount_text);
    draw_text_shadow(ui, input, layout->chat_input.x, layout->chat_input.y, 14, WHITE);

    runec_ui_draw_asset(&ui->assets, "main_stones_bottom", layout->chat_controls, WHITE);
    if (!decoded_chat && !runec_ui_asset_ready(&ui->assets, "main_stones_bottom"))
        DrawRectangleRec(layout->chat_controls, (Color){74, 62, 48, 235});

    const char *tabs[] = {"All", "Game", "Public", "Private", "Channel", "Clan", "Trade"};
    for (int i = 0; i < 7; i++) {
        Rectangle r = {layout->chat.x + 6 + i * 72.0f, layout->chat_controls.y + 1, 68, 21};
        if (!decoded_chat)
            runec_ui_draw_asset(&ui->assets, "chat_tab_button_0", r, WHITE);
        if (!decoded_chat && !runec_ui_asset_ready(&ui->assets, "chat_tab_button_0"))
            DrawRectangleRec(r, (Color){52, 44, 35, 230});
        draw_centered_text(ui, tabs[i], r, 12,
                           i == ui->active_chat_tab ? OSRS_YELLOW : OSRS_ORANGE);
    }
}


static Color stack_text_color(int quantity) {
    if (quantity >= 10000000)
        return OSRS_GREEN;
    if (quantity >= 100000)
        return WHITE;
    return OSRS_YELLOW;
}

static void format_stack_quantity(int quantity, char *dst, size_t cap) {
    if (quantity >= 10000000) {
        snprintf(dst, cap, "%dM", quantity / 1000000);
    } else if (quantity >= 100000) {
        snprintf(dst, cap, "%dK", quantity / 1000);
    } else {
        snprintf(dst, cap, "%d", quantity);
    }
}

static const Texture2D *ui_item_icon_texture(const RuneCUiState *ui,
                                             uint32_t icon_item_id) {
    for (int i = 0; i < ui->item_icon_count; i++) {
        if (ui->item_icons[i].ready && ui->item_icons[i].item_id == icon_item_id)
            return &ui->item_icons[i].texture;
    }
    return NULL;
}

static void draw_inventory_item(const RuneCUiState *ui, const RuneCUiSlot *slot, Rectangle r) {
    uint32_t icon_item_id = slot->icon_item_id ? slot->icon_item_id : slot->item_id;
    const Texture2D *icon = ui_item_icon_texture(ui, icon_item_id);
    if (icon && icon->id) {
        // Inventory sprites are authored at 36x32; never auto-fit or resample.
        DrawTexture(*icon, (int)floorf(r.x + (r.width - icon->width) * 0.5f),
                     (int)floorf(r.y + (r.height - icon->height) * 0.5f), WHITE);
        return;
    }
    draw_centered_text(ui, "?", r, 12, RED);
}

static void draw_inventory(const RuneCUiState *ui, const RuneCUiLayout *layout) {
    for (int i = 0; i < RUNEC_UI_INV_SLOT_COUNT; i++) {
        Rectangle r = inv_slot_rect(layout, i);
        if (ui->selected_inventory_slot == i)
            DrawRectangleLinesEx((Rectangle){r.x - 1, r.y - 1, r.width + 2, r.height + 2},
                                 2.0f, OSRS_YELLOW);
        if (ui->inventory[i].enabled) {
            draw_inventory_item(ui, &ui->inventory[i], r);
            if (ui->inventory[i].quantity > 1) {
                char q[16];
                format_stack_quantity(ui->inventory[i].quantity, q, sizeof(q));
                draw_text_shadow(ui, q, r.x + 1, r.y - 1, 10,
                                 stack_text_color(ui->inventory[i].quantity));
            }
        }
    }
}

static void draw_bank(RuneCUiState *ui, int screen_w, int screen_h,
                      const RuneCUiLayout *layout) {
    if (!ui->bank_open)
        return;
    clamp_bank_scroll(ui);
    Rectangle panel = bank_panel_rect(screen_w, screen_h, layout);
    DrawRectangleRec(panel, (Color){34, 27, 19, 246});
    DrawRectangleLinesEx(panel, 2.0f, (Color){134, 105, 62, 255});
    DrawRectangleRec((Rectangle){panel.x + 2, panel.y + 2,
                                 panel.width - 4, 30},
                     (Color){64, 48, 31, 245});
    int deposit = ui->bank_kind == RC_STORAGE_DEPOSIT_BOX;
    draw_centered_text(ui, deposit ? "Bank deposit box" : "Bank", (Rectangle){panel.x, panel.y + 6,
                                               panel.width, 18},
                       14, OSRS_YELLOW);

    Rectangle close = bank_close_rect(panel);
    DrawRectangleRec(close, (Color){78, 56, 34, 245});
    DrawRectangleLinesEx(close, 1.0f, (Color){170, 130, 76, 255});
    draw_centered_text(ui, "Close", close, 12, OSRS_ORANGE);

    for (int i = 0; i < 2; i++) {
        Rectangle r = bank_deposit_all_rect(panel, i);
        int hovered = CheckCollisionPointRec(GetMousePosition(), r);
        DrawRectangleRec(r, hovered ? (Color){92,67,39,255} : (Color){49,38,26,245});
        DrawRectangleLinesEx(r, 1, hovered ? OSRS_YELLOW : OSRS_ORANGE);
        draw_asset_centered(ui, g_tab_icon[i ? RUNEC_UI_TAB_EQUIPMENT : RUNEC_UI_TAB_INVENTORY],
                            r, 24, 24, WHITE);
    }
    for (int i = 0; !deposit && i < 2; i++) {
        Rectangle r = bank_mode_rect(panel, i);
        DrawRectangleRec(r, i == ui->bank_noted ? (Color){92,67,39,255} : (Color){49,38,26,245});
        DrawRectangleLinesEx(r, 1, OSRS_ORANGE);
        draw_centered_text(ui, i ? "Note" : "Item", r, 12,
                           i == ui->bank_noted ? OSRS_YELLOW : OSRS_ORANGE);
    }
    for (int i = 0; !deposit && i < RUNEC_UI_BANK_TAB_COUNT; i++) {
        Rectangle r = bank_tab_rect(panel, i);
        Color fill = i == ui->bank_active_tab
            ? (Color){92, 67, 39, 255}
            : (Color){49, 38, 26, 245};
        DrawRectangleRec(r, fill);
        DrawRectangleLinesEx(r, 1.0f, (Color){150, 112, 61, 255});
        draw_centered_text(ui, ui->bank_tab_labels[i], r, 11,
                           i == ui->bank_active_tab ? OSRS_YELLOW : OSRS_ORANGE);
    }

    for (int i = 0; i < RUNEC_UI_BANK_VISIBLE_SLOTS; i++) {
        Rectangle r = bank_slot_rect(panel, i);
        DrawRectangleRec(r, (Color){13, 11, 9, 150});
        DrawRectangleLinesEx(r, 1.0f, (Color){74, 60, 41, 255});
        int slot = bank_slot_for_visible_index(ui, ui->bank_scroll + i);
        if (slot < 0)
            continue;
        const RuneCUiSlot *item = deposit ? &ui->inventory[slot] : &ui->bank[slot];
        if (!item->enabled)
            continue;
        draw_inventory_item(ui, item, r);
        if (item->quantity > 1) {
            char q[16];
            format_stack_quantity(item->quantity, q, sizeof(q));
            draw_text_shadow(ui, q, r.x + 1, r.y - 1, 10,
                             stack_text_color(item->quantity));
        }
    }

    int scroll_max = bank_scroll_max(ui);
    if (scroll_max > 0) {
        Rectangle track = {panel.x + panel.width - 15.0f, panel.y + 70.0f,
                           5.0f, panel.height - 84.0f};
        DrawRectangleRec(track, (Color){14, 11, 8, 220});
        float frac = (float)ui->bank_scroll / (float)scroll_max;
        int tab_count = bank_tab_item_count(ui);
        if (tab_count < RUNEC_UI_BANK_VISIBLE_SLOTS)
            tab_count = RUNEC_UI_BANK_VISIBLE_SLOTS;
        float thumb_h = track.height *
            ((float)RUNEC_UI_BANK_VISIBLE_SLOTS / (float)tab_count);
        if (thumb_h < 24.0f) thumb_h = 24.0f;
        Rectangle thumb = {track.x, track.y + frac * (track.height - thumb_h),
                           track.width, thumb_h};
        DrawRectangleRec(thumb, (Color){166, 127, 69, 255});
    }
}

static void draw_equipment(const RuneCUiState *ui, const RuneCUiLayout *layout) {
    draw_asset_tiled(ui, "miscgraphics_2",
                     side_ref_rect(layout, (Rectangle){77, 39, 36, 124}), WHITE);
    draw_asset_tiled(ui, "miscgraphics_2",
                     side_ref_rect(layout, (Rectangle){21, 118, 36, 45}), WHITE);
    draw_asset_tiled(ui, "miscgraphics_2",
                     side_ref_rect(layout, (Rectangle){133, 118, 36, 45}), WHITE);
    draw_asset_tiled(ui, "miscgraphics_3",
                     side_ref_rect(layout, (Rectangle){56, 81, 78, 36}), WHITE);
    draw_asset_tiled(ui, "miscgraphics_3",
                     side_ref_rect(layout, (Rectangle){71, 42, 48, 36}), WHITE);

    for (int i = 0; i < RUNEC_UI_EQUIP_SLOT_COUNT; i++) {
        Rectangle r = equip_slot_rect(layout, i);
        if (r.width <= 0)
            continue;
        if (ui->equipment[i].enabled) {
            draw_inventory_item(ui, &ui->equipment[i], r);
            if (ui->equipment[i].quantity > 1) {
                char q[16];
                format_stack_quantity(ui->equipment[i].quantity, q, sizeof(q));
                draw_text_shadow(ui, q, r.x + 1, r.y - 1, 10,
                                 stack_text_color(ui->equipment[i].quantity));
            }
        } else if (g_worn_icon_names[i]) {
            draw_asset_centered(ui, g_worn_icon_names[i], r, 28, 28, (Color){190, 178, 150, 175});
        }
        if (ui->selected_equipment_slot == i) {
            DrawRectangleLinesEx((Rectangle){r.x - 1, r.y - 1, r.width + 2, r.height + 2},
                                 2.0f, OSRS_YELLOW);
        }
    }

    for (int i = 0; i < (int)(sizeof(RUNEC_OSRS_WORN_BUTTONS) / sizeof(RUNEC_OSRS_WORN_BUTTONS[0])); i++) {
        const RuneCUiWornButtonRef *ref = &RUNEC_OSRS_WORN_BUTTONS[i];
        Rectangle b = {layout->side_content.x + ref->rect.x,
                       layout->side_content.y + ref->rect.y,
                       ref->rect.width, ref->rect.height};
        Rectangle icon = {layout->side_content.x + ref->icon_rect.x,
                          layout->side_content.y + ref->icon_rect.y,
                          ref->icon_rect.width, ref->icon_rect.height};
        runec_ui_draw_asset(&ui->assets, "combatboxes_0", b, WHITE);
        if (!runec_ui_asset_ready(&ui->assets, "combatboxes_0")) {
            DrawRectangleRounded(b, 0.18f, 5, (Color){54, 46, 35, 235});
            DrawRectangleLinesEx(b, 1, (Color){119, 99, 68, 255});
        }
        draw_asset_centered(ui, ref->asset, icon, icon.width, icon.height, WHITE);
    }
}

static void draw_prayer(const RuneCUiState *ui, const RuneCUiLayout *layout) {
    Vector2 mouse = GetMousePosition();
    int hovered = -1;
    for (int i = 0; i < ui->prayers.count; i++) {
        int id = ui->prayers.ids[i];
        const RuneCPrayerUiRef *ref = runec_prayer_ui_ref(id);
        Rectangle r = grid_cell_rect(layout, i, 5, 8, 8, 36, 36, 34, 34);
        uint32_t bit = 1u << id;
        uint32_t selected = ui->quick_prayer_setup ? ui->prayers.quick : ui->prayers.active;
        if (selected & bit) draw_asset_centered(ui, "prayer_glow", r, 34, 34, WHITE);
        const char *asset = ui->prayers.available & bit ? ref->available_asset : ref->locked_asset;
        const Texture2D *tex = runec_ui_asset(&ui->assets, asset);
        if (tex) draw_asset_centered(ui, asset, r, tex->width, tex->height, WHITE);
        if (CheckCollisionPointRec(mouse, r)) hovered = id;
    }
    if (ui->quick_prayer_setup) {
        Rectangle done = side_ref_rect(layout, (Rectangle){4, 234, 182, 24});
        draw_centered_text(ui, "Done", done, 14, OSRS_ORANGE);
    } else {
        draw_centered_text(ui, TextFormat("Prayer: %d / %d", ui->prayer_points, ui->prayer_points_max),
            side_ref_rect(layout, (Rectangle){4, 234, 182, 24}), 14, OSRS_ORANGE);
    }
    if (hovered >= 0 && !ui->context_open) {
        const char *name = runec_prayer_ui_ref(hovered)->name;
        const char *reason = ui->prayers.reasons[hovered];
        char title[96];
        snprintf(title, sizeof(title), "%s (level %d)", name, ui->prayers.levels[hovered]);
        Font font = runec_ui_font_for_size(&ui->assets, 12);
        float width = fmaxf(MeasureTextEx(font, title, 12, 0).x,
                            MeasureTextEx(font, reason, 12, 0).x) + 10;
        float height = reason[0] ? 36 : 20;
        Rectangle tip = {fmaxf(2, fminf(mouse.x + 12, GetScreenWidth() - width - 2)),
                         fminf(mouse.y + 22, GetScreenHeight() - height - 2), width, height};
        DrawRectangleRec(tip, (Color){28, 23, 17, 245});
        DrawRectangleLinesEx(tip, 1, OSRS_ORANGE);
        draw_text_shadow(ui, title, tip.x + 5, tip.y + 3, 12, OSRS_YELLOW);
        if (reason[0]) draw_text_shadow(ui, reason, tip.x + 5, tip.y + 19, 12, OSRS_ORANGE);
    }
}

static void draw_spellbook(const RuneCUiState *ui, const RuneCUiLayout *layout) {
    int slots[RUNEC_UI_SPELL_MAX];
    int count = spell_display_slots(ui, slots);
    int cols = spell_grid_columns(ui);
    Vector2 mouse = GetMousePosition();
    const char *hover_name = NULL;
    for (int i = 0; i < cols * spell_grid_rows(ui); i++) {
        int display = i + ui->spell_scroll * cols;
        if (display >= count) break;
        int slot = slots[display];
        Rectangle r = spell_grid_cell(ui, layout, i);
        int selected = (ui->selected_target.kind == RUNEC_UI_SELECTED_SPELL
            && ui->selected_target.source_slot == slot) || ui->autocast_slot == slot;
        if (selected) DrawRectangleLinesEx(r, 1, OSRS_YELLOW);
        const Texture2D *icon = ui->spell_icons[slot];
        if (icon)
            DrawTexturePro(*icon, (Rectangle){0, 0, icon->width, icon->height},
                           r, (Vector2){0}, 0, ui->spell_available[slot] ? WHITE : GRAY);
        if (CheckCollisionPointRec(mouse, r)) {
            hover_name = ui->spells[slot]->name;
            DrawRectangleLinesEx(r, 1, OSRS_ORANGE);
        }
    }
    if (ui->autocast_picker) {
        draw_centered_text(ui, ui->autocast_picker_defensive ? "Defensive Autocast" : "Autocast",
            side_ref_rect(layout, (Rectangle){0, 0, 190, 20}), 12, OSRS_ORANGE);
        if (!count)
            draw_centered_text(ui, "No compatible spells", side_ref_rect(layout,
                (Rectangle){0, 100, 190, 20}), 12, OSRS_ORANGE);
        draw_centered_text(ui, "Clear", side_ref_rect(layout, (Rectangle){4, 242, 60, 18}), 12, OSRS_ORANGE);
        draw_centered_text(ui, "Cancel", side_ref_rect(layout, (Rectangle){126, 242, 60, 18}), 12, OSRS_ORANGE);
    } else {
        draw_centered_text(ui, g_spellbook_names[ui->current_spellbook],
            side_ref_rect(layout, (Rectangle){0, 242, 190, 18}), 12, OSRS_ORANGE);
    }
    if (hover_name && !ui->context_open && ui->selected_target.kind == RUNEC_UI_SELECTED_NONE) {
        Font font = runec_ui_font_for_size(&ui->assets, 12);
        float width = MeasureTextEx(font, hover_name, 12, 0).x + 10;
        Rectangle tip = {fminf(mouse.x + 12, GetScreenWidth() - width - 2),
                         fminf(mouse.y + 22, GetScreenHeight() - 22), width, 20};
        DrawRectangleRec(tip, (Color){28, 23, 17, 245});
        DrawRectangleLinesEx(tip, 1, OSRS_ORANGE);
        draw_text_shadow(ui, hover_name, tip.x + 5, tip.y + 3, 12, OSRS_YELLOW);
    }
}

static void draw_skills(const RuneCUiState *ui, const RuneCUiLayout *layout) {
    int count = (int)(sizeof(RUNEC_OSRS_SKILLS) / sizeof(RUNEC_OSRS_SKILLS[0]));
    for (int i = 0; i < count; i++) {
        Rectangle r = skill_slot_rect(layout, i);
        DrawRectangleRec(r, (Color){72, 70, 60, 232});
        DrawLineEx((Vector2){r.x, r.y}, (Vector2){r.x + r.width - 1, r.y}, 1,
                   (Color){139, 130, 104, 255});
        DrawLineEx((Vector2){r.x, r.y}, (Vector2){r.x, r.y + r.height - 1}, 1,
                   (Color){139, 130, 104, 255});
        DrawLineEx((Vector2){r.x, r.y + r.height - 1},
                   (Vector2){r.x + r.width - 1, r.y + r.height - 1}, 1,
                   (Color){28, 25, 21, 255});
        DrawLineEx((Vector2){r.x + r.width - 1, r.y},
                   (Vector2){r.x + r.width - 1, r.y + r.height - 1}, 1,
                   (Color){28, 25, 21, 255});
        char icon[32];
        snprintf(icon, sizeof(icon), "skill_icon_%d", RUNEC_OSRS_SKILLS[i].icon_index);
        draw_asset_centered(ui, icon, (Rectangle){r.x + 3, r.y + 3, 24, 24}, 24, 24, WHITE);
        int current_level = i < RUNEC_UI_SKILL_COUNT && ui->skill_current[i] > 0
                          ? ui->skill_current[i] : 1;
        int base_level = i < RUNEC_UI_SKILL_COUNT && ui->skill_base[i] > 0
                       ? ui->skill_base[i] : current_level;
        char cur[8];
        char base[8];
        snprintf(cur, sizeof(cur), "%d", current_level);
        snprintf(base, sizeof(base), "%d", base_level);
        Color cur_color = current_level < base_level ? (Color){220, 45, 31, 255}
                        : current_level > base_level ? OSRS_GREEN : OSRS_YELLOW;
        draw_text_shadow(ui, cur, r.x + 39, r.y + 2, 10, cur_color);
        draw_text_shadow(ui, base, r.x + 39, r.y + 17, 9, OSRS_GREEN);
    }

    Rectangle total = {layout->side_content.x + RUNEC_OSRS_STATS_TOTAL.x,
                       layout->side_content.y + RUNEC_OSRS_STATS_TOTAL.y,
                       RUNEC_OSRS_STATS_TOTAL.width, RUNEC_OSRS_STATS_TOTAL.height};
    DrawRectangleRec(total, (Color){7, 7, 7, 238});
    DrawRectangleLinesEx(total, 1, (Color){99, 91, 68, 255});
    char total_text[32];
    snprintf(total_text, sizeof(total_text), "Total level: %d",
             ui->skill_total > 0 ? ui->skill_total : 0);
    draw_centered_text(ui, total_text, total, 10, OSRS_YELLOW);
}

static void draw_combat_box(const RuneCUiState *ui, Rectangle r, int selected) {
    const char *asset = selected ? "combatboxes_1" : "combatboxes_0";
    runec_ui_draw_asset(&ui->assets, asset, r, WHITE);
    if (!runec_ui_asset_ready(&ui->assets, asset)) {
        DrawRectangleRec(r, selected ? (Color){83, 61, 43, 245} : (Color){45, 39, 31, 235});
        DrawRectangleLinesEx(r, 1, selected ? OSRS_YELLOW : (Color){103, 89, 63, 255});
    }
    if (selected)
        DrawRectangleRec(r, (Color){120, 27, 20, 54});
}

static void draw_combat(const RuneCUiState *ui, const RuneCUiLayout *layout) {
    Rectangle header = side_ref_rect(layout, RUNEC_OSRS_COMBAT_HEADER);
    const char *weapon_name = ui->combat_weapon_name[0]
        ? ui->combat_weapon_name : "Unarmed";
    draw_centered_text(ui, weapon_name, side_ref_rect(layout, RUNEC_OSRS_COMBAT_TITLE),
                       13, OSRS_ORANGE);
    char level[32];
    snprintf(level, sizeof(level), "Combat Lvl: %d", ui->combat_level);
    draw_centered_text(ui, level, side_ref_rect(layout, RUNEC_OSRS_COMBAT_LEVEL),
                       12, OSRS_ORANGE);
    DrawLineEx((Vector2){header.x + 10, header.y + 42},
               (Vector2){header.x + header.width - 10, header.y + 42}, 1,
               (Color){75, 64, 45, 180});

    int visible_slot = 0;
    int layout_count = (int)(sizeof(RUNEC_OSRS_COMBAT_STYLES)
        / sizeof(RUNEC_OSRS_COMBAT_STYLES[0]));
    for (int i = 0; i < RUNEC_UI_COMBAT_STYLE_COUNT && visible_slot < layout_count; i++) {
        const RuneCUiCombatStyleOption *option = &ui->combat_styles[i];
        if (!option->visible)
            continue;
        const RuneCUiCombatStyleRef *style =
            &RUNEC_OSRS_COMBAT_STYLES[visible_slot];
        int selected = combat_style_option_selected(ui, option);
        Rectangle button = combat_style_rect(ui, layout, visible_slot++);
        draw_combat_box(ui, button, selected);
        Rectangle icon = ui->autocast_available
            ? (Rectangle){button.x + 22, button.y, 24, 18} : side_ref_rect(layout, style->icon_rect);
        Rectangle label = ui->autocast_available
            ? (Rectangle){button.x, button.y + 17, button.width, 14} : side_ref_rect(layout, style->text_rect);
        draw_asset_centered(ui, option->icon_asset, icon, icon.width, icon.height, WHITE);
        draw_centered_text(ui, option->label, label,
                           10, selected ? OSRS_YELLOW : OSRS_ORANGE);
    }

    if (ui->autocast_available) {
        for (int defensive = 0; defensive <= 1; defensive++) {
            Rectangle r = autocast_button(layout, defensive);
            int selected = ui->autocast_slot >= 0 && ui->defensive_autocast == defensive;
            draw_combat_box(ui, r, selected);
            const Texture2D *icon = ui->autocast_slot >= 0 ? ui->spell_icons[ui->autocast_slot]
                : runec_ui_asset(&ui->assets, "sideicons_interface_6");
            if (icon) DrawTexturePro(*icon, (Rectangle){0, 0, icon->width, icon->height},
                (Rectangle){r.x + (defensive ? 32 : 22), r.y + 3, 24, 24}, (Vector2){0}, 0, WHITE);
            if (defensive) draw_asset_centered(ui, "combat_shield",
                (Rectangle){r.x + 7, r.y + 3, 22, 24}, 22, 24, WHITE);
            draw_centered_text(ui, "Spell", (Rectangle){r.x, r.y + 29, r.width, 15}, 12, OSRS_ORANGE);
        }
    }

    Rectangle retaliate = side_ref_rect(layout, RUNEC_OSRS_COMBAT_RETALIATE);
    draw_combat_box(ui, retaliate, ui->auto_retaliate);
    draw_asset_centered(ui, "combat_shield", side_ref_rect(layout, RUNEC_OSRS_COMBAT_RETALIATE_ICON),
                        26, 39, WHITE);
    draw_centered_text(ui, ui->auto_retaliate ? "Auto Retaliate" : "Retaliate Off",
                       side_ref_rect(layout, RUNEC_OSRS_COMBAT_RETALIATE_TEXT), 11, OSRS_ORANGE);

    Rectangle spec = side_ref_rect(layout, RUNEC_OSRS_COMBAT_SPECIAL_BAR);
    runec_ui_draw_asset(&ui->assets, "combatboxes_special_attack", spec, WHITE);
    if (!runec_ui_asset_ready(&ui->assets, "combatboxes_special_attack"))
        DrawRectangleRec(spec, (Color){32, 28, 22, 235});
    Rectangle empty = {spec.x + 2, spec.y + 7, spec.width - 4, 12};
    DrawRectangleRec(empty, (Color){115, 6, 6, 255});
    Rectangle fill = empty;
    fill.width *= (float)ui->special_attack_energy / 100.0f;
    DrawRectangleRec(fill, ui->special_attack_enabled ? OSRS_GREEN : (Color){57, 125, 59, 255});
    DrawRectangleLinesEx((Rectangle){spec.x + 2, spec.y + 6, spec.width - 4, 14}, 1,
                         (Color){44, 42, 35, 255});
    char spec_text[32];
    snprintf(spec_text, sizeof(spec_text), "Special Attack: %d%%", ui->special_attack_energy);
    draw_centered_text(ui, spec_text, spec, 10, OSRS_YELLOW);

    const RuneCUiCombatStyleOption *selected_style =
        selected_combat_style_option(ui);
    const char *mode = selected_style ? selected_style->mode : "Accurate";
    if (ui->autocast_slot >= 0) mode = ui->defensive_autocast ? "Defensive cast" : "Autocast";
    char category[64];
    snprintf(category, sizeof(category), "Attack style: %s", mode);
    draw_centered_text(ui, category, side_ref_rect(layout, RUNEC_OSRS_COMBAT_CATEGORY),
                       12, OSRS_ORANGE);
}

static void draw_placeholder_tab(const RuneCUiState *ui, const RuneCUiLayout *layout,
                                 const char *title, const char *body) {
    draw_centered_text(ui, title, (Rectangle){layout->side_content.x, layout->side_content.y + 26, 190, 20},
                       15, OSRS_YELLOW);
    draw_centered_text(ui, body, (Rectangle){layout->side_content.x + 10, layout->side_content.y + 105, 170, 36},
                       12, OSRS_ORANGE);
}

static void draw_clan_validation_tab(const RuneCUiState *ui,
                                     const RuneCUiLayout *layout) {
    Vector2 mouse = GetMousePosition();
    draw_centered_text(ui, "Validation", (Rectangle){layout->side_content.x,
                                                     layout->side_content.y + 10,
                                                     190, 18},
                       14, OSRS_YELLOW);
    draw_text_shadow(ui, "Scene level", layout->side_content.x + 8.0f,
                     layout->side_content.y + 24.0f, 12, OSRS_ORANGE);

    Rectangle follow = side_clan_plane_follow_rect(layout);
    Color follow_fill = !ui->scene_plane_override_active
        ? (Color){92, 70, 38, 245}
        : (CheckCollisionPointRec(mouse, follow)
           ? (Color){72, 62, 47, 240}
           : (Color){42, 34, 25, 230});
    DrawRectangleRec(follow, follow_fill);
    DrawRectangleLinesEx(follow, 1.0f, (Color){155, 122, 66, 255});
    draw_centered_text(ui, "Follow", follow, 12,
                       !ui->scene_plane_override_active
                       ? OSRS_YELLOW : OSRS_ORANGE);

    for (int plane = 0; plane < RUNEC_UI_SCENE_PLANE_COUNT; plane++) {
        Rectangle r = side_clan_plane_button_rect(layout, plane);
        int active = ui->scene_plane_override_active
                  && ui->scene_plane == plane;
        Color fill = active
            ? (Color){92, 70, 38, 245}
            : (CheckCollisionPointRec(mouse, r)
               ? (Color){72, 62, 47, 240}
               : (Color){42, 34, 25, 230});
        DrawRectangleRec(r, fill);
        DrawRectangleLinesEx(r, 1.0f, (Color){155, 122, 66, 255});
        char label[4];
        snprintf(label, sizeof(label), "%d", plane);
        draw_centered_text(ui, label, r, 12,
                           active ? OSRS_YELLOW : OSRS_ORANGE);
    }

    char plane_label[48];
    snprintf(plane_label, sizeof(plane_label), "View %d  Player %d",
             ui->scene_plane, ui->player_plane);
    draw_centered_text(ui, plane_label,
                       (Rectangle){layout->side_content.x + 4.0f,
                                   layout->side_content.y + 64.0f,
                                   106.0f, 18.0f},
                       12, OSRS_ORANGE);

    Rectangle tiles = side_clan_npc_tiles_rect(layout);
    Color tiles_fill = ui->npc_tiles_overlay
        ? (Color){92, 70, 38, 245}
        : (CheckCollisionPointRec(mouse, tiles)
           ? (Color){72, 62, 47, 240}
           : (Color){42, 34, 25, 230});
    DrawRectangleRec(tiles, tiles_fill);
    DrawRectangleLinesEx(tiles, 1.0f, (Color){155, 122, 66, 255});
    draw_centered_text(ui, "NPC tiles", tiles, 12,
                       ui->npc_tiles_overlay ? OSRS_YELLOW : OSRS_ORANGE);

    draw_text_shadow(ui, "Teleports", layout->side_content.x + 8.0f,
                     layout->side_content.y + 86.0f, 12, OSRS_ORANGE);
    for (int i = 0; i < ui->dev_transport_count; i++) {
        Rectangle r = side_clan_dev_transport_rect(layout, i);
        Color fill = CheckCollisionPointRec(mouse, r)
            ? (Color){72, 62, 47, 240}
            : (Color){42, 34, 25, 230};
        DrawRectangleRec(r, fill);
        DrawRectangleLinesEx(r, 1.0f, (Color){155, 122, 66, 255});
        draw_centered_text(ui, ui->dev_transport_labels[i], r, 12,
                           OSRS_ORANGE);
    }
    if (ui->dev_spellbooks_enabled) {
        draw_text_shadow(ui, "Spellbook", layout->side_content.x + 8,
                         layout->side_content.y + 195, 12, OSRS_ORANGE);
        for (int book = 0; book < 4; book++) {
            Rectangle r = dev_spellbook_button(layout, book);
            DrawRectangleRec(r, (Color){42, 34, 25, 230});
            DrawRectangleLinesEx(r, 1, (Color){155, 122, 66, 255});
            draw_centered_text(ui, g_spellbook_names[book], r, 12,
                book == ui->current_spellbook ? OSRS_YELLOW : OSRS_ORANGE);
        }
    }
}

static void draw_decoded_dynamic_tab_overlay(const RuneCUiState *ui,
                                             const RuneCUiLayout *layout) {
    switch (ui->active_tab) {
    case RUNEC_UI_TAB_INVENTORY:
        break;
    case RUNEC_UI_TAB_EQUIPMENT:
        break;
    case RUNEC_UI_TAB_SKILLS:
        draw_skills(ui, layout);
        break;
    case RUNEC_UI_TAB_COMBAT:
        draw_combat(ui, layout);
        break;
    case RUNEC_UI_TAB_CLAN:
        draw_clan_validation_tab(ui, layout);
        break;
    default:
        break;
    }
}

static void draw_side(RuneCUiState *ui, const RuneCUiLayout *layout) {
    draw_side_chrome(ui, layout);

    if (spell_grid_visible(ui)) {
        draw_spellbook(ui, layout);
        return;
    }

    if (ui->active_tab == RUNEC_UI_TAB_COMBAT) {
        draw_combat(ui, layout);
        return;
    }
    if (ui->active_tab == RUNEC_UI_TAB_PRAYER) {
        draw_prayer(ui, layout);
        return;
    }

    if (ui->decoded_ui_enabled && ui->decoded_ui_ready) {
        const char *group = open_group_for_side_content(ui);
        RuneCUiComponentOverrides overrides = decoded_overrides(ui);
        if (group && runec_ui_interfaces_draw_group_ex(
                &ui->interfaces, &ui->assets, group, layout->side_content,
                &overrides)) {
            draw_decoded_dynamic_tab_overlay(ui, layout);
            return;
        }
    }

    switch (ui->active_tab) {
    case RUNEC_UI_TAB_INVENTORY:
        draw_inventory(ui, layout);
        break;
    case RUNEC_UI_TAB_EQUIPMENT:
        draw_equipment(ui, layout);
        break;
    case RUNEC_UI_TAB_SKILLS:
        draw_skills(ui, layout);
        break;
    case RUNEC_UI_TAB_COMBAT:
        draw_combat(ui, layout);
        break;
    case RUNEC_UI_TAB_QUESTS:
        draw_placeholder_tab(ui, layout, "Quest List", "Quest journal surface.");
        break;
    case RUNEC_UI_TAB_SETTINGS:
        draw_placeholder_tab(ui, layout, "Settings", "Viewer options surface.");
        break;
    case RUNEC_UI_TAB_CLAN:
        draw_clan_validation_tab(ui, layout);
        break;
    default:
        break;
    }
}

typedef struct {
    char action[RUNEC_UI_ITEM_ACTION_LEN];
    char target[160];
    int more_options;
} RuneCUiItemHover;

static int item_hover_text(RuneCUiState *ui, const RuneCUiLayout *layout,
                           int width, int height, Vector2 mouse, int shift,
                           RuneCUiItemHover *hover) {
    memset(hover, 0, sizeof(*hover));
    if (ui->context_open || ui->drag.active || ui->bank_amount_source) return 0;
    const RuneCUiSlot *item = NULL;
    int withdraw = 0;
    if (ui->bank_open) {
        Rectangle panel = bank_panel_rect(width, height, layout);
        if (CheckCollisionPointRec(mouse, panel)) {
            for (int i = 0; i < 2; i++) {
                if (CheckCollisionPointRec(mouse, bank_deposit_all_rect(panel, i))) {
                    copy_text(hover->action, sizeof(hover->action),
                              i ? "Deposit worn items" : "Deposit inventory");
                    return 1;
                }
            }
            int slot = bank_slot_at(ui, panel, mouse);
            if (slot < 0) return 0;
            withdraw = ui->bank_kind != RC_STORAGE_DEPOSIT_BOX;
            item = withdraw ? &ui->bank[slot] : &ui->inventory[slot];
        }
    }
    if (!item && ui->active_tab == RUNEC_UI_TAB_INVENTORY) {
        int slot = ui_inventory_slot_at(ui, layout, mouse);
        if (slot >= 0) item = &ui->inventory[slot];
    }
    if (!item || !item->enabled) return 0;
    copy_text(hover->target, sizeof(hover->target), item->label);
    if (ui->bank_open) {
        int primary = shift ? 4 : 0;
        copy_text(hover->action, sizeof(hover->action), withdraw
            ? bank_actions[primary].withdraw : bank_actions[primary].deposit);
        for (unsigned i = 0; i < sizeof(bank_actions)/sizeof(bank_actions[0]); i++)
            if (withdraw || bank_actions[i].deposit) hover->more_options++;
        hover->more_options--;
    } else if (ui->selected_target.kind != RUNEC_UI_SELECTED_NONE) {
        copy_text(hover->action, sizeof(hover->action), ui->selected_target.verb);
        snprintf(hover->target, sizeof(hover->target), "%s -> %s",
            ui->selected_target.label, item->label);
    } else if (item->action_count) {
        copy_text(hover->action, sizeof(hover->action), item->actions[0]);
        hover->more_options = item->action_count - 1;
    } else return 0;
    return 1;
}

static void draw_item_hover(RuneCUiState *ui, const RuneCUiLayout *layout,
                            int width, int height) {
    RuneCUiItemHover hover;
    if (!item_hover_text(ui, layout, width, height, GetMousePosition(),
            IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT), &hover)) return;
    char target[sizeof(hover.target) + 2], suffix[40] = "";
    snprintf(target, sizeof(target), " %s", hover.target);
    if (hover.more_options > 0)
        snprintf(suffix, sizeof(suffix), " / %d more options", hover.more_options);
    const char *parts[] = {hover.action, target, suffix};
    const Color colors[] = {WHITE, OSRS_ITEM_TARGET, WHITE};
    Font font = runec_ui_bold_font(&ui->assets);
    float x = 4;
    for (int i = 0; i < 3; i++) {
        DrawTextEx(font, parts[i], (Vector2){x + 1, 5}, RUNEC_CONTEXT_MENU_FONT_SIZE, 0, BLACK);
        DrawTextEx(font, parts[i], (Vector2){x, 4}, RUNEC_CONTEXT_MENU_FONT_SIZE, 0, colors[i]);
        x += MeasureTextEx(font, parts[i], RUNEC_CONTEXT_MENU_FONT_SIZE, 0).x;
    }
}

static void draw_context(const RuneCUiState *ui) {
    if (!ui->context_open)
        return;
    RuneCContextMenuLayout layout = context_menu_layout_for_ui(ui);
    Color menu_color = (Color){93, 84, 71, 255};
    Rectangle box = {(float)layout.x, (float)layout.y,
                     (float)layout.width, (float)layout.height};
    DrawRectangleRec(box, menu_color);
    DrawRectangle(layout.x + 1, layout.y + 1, layout.width - 2,
                  RUNEC_CONTEXT_MENU_HEADER_HEIGHT - 3, BLACK);
    DrawRectangleLinesEx(
        (Rectangle){(float)layout.x + 1.0f,
                    (float)(layout.y + RUNEC_CONTEXT_MENU_HEADER_HEIGHT - 1),
                    (float)layout.width - 2.0f,
                    (float)(layout.height
                        - RUNEC_CONTEXT_MENU_HEADER_HEIGHT)},
        1.0f, BLACK);

    Font font = runec_ui_bold_font(&ui->assets);
    DrawTextEx(font, "Choose Option",
               (Vector2){(float)layout.x + 4.0f, (float)layout.y + 2.0f},
               RUNEC_CONTEXT_MENU_FONT_SIZE, 0.0f, menu_color);
    Vector2 mouse = GetMousePosition();
    for (int i = 0; i < ui->context_action_count; i++) {
        float x = (float)layout.x + 4.0f;
        float y = (float)(layout.y + RUNEC_CONTEXT_MENU_HEADER_HEIGHT
                        + i * RUNEC_CONTEXT_MENU_ROW_HEIGHT + 1);
        int hovered = runec_context_menu_action_at(
            &layout, ui->context_action_count,
            (int)mouse.x, (int)mouse.y) == i;
        const char *action = ui->context_actions[i];
        Color action_color = hovered ? OSRS_YELLOW : WHITE;
        DrawTextEx(font, action, (Vector2){x + 1.0f, y + 1.0f},
                   RUNEC_CONTEXT_MENU_FONT_SIZE, 0.0f, BLACK);
        DrawTextEx(font, action, (Vector2){x, y},
                   RUNEC_CONTEXT_MENU_FONT_SIZE, 0.0f, action_color);
        if (context_action_has_target(ui, i)) {
            float action_width = MeasureTextEx(
                font, action, RUNEC_CONTEXT_MENU_FONT_SIZE, 0.0f).x;
            char target[RUNEC_UI_CONTEXT_TEXT_MAX + 2];
            snprintf(target, sizeof(target), " %s", ui->context_targets[i]);
            x += action_width;
            DrawTextEx(font, target, (Vector2){x + 1.0f, y + 1.0f},
                       RUNEC_CONTEXT_MENU_FONT_SIZE, 0.0f, BLACK);
            DrawTextEx(font, target, (Vector2){x, y},
                       RUNEC_CONTEXT_MENU_FONT_SIZE, 0.0f,
                       ui->context_target_colors[i]);
        }
    }
}

static void draw_selected_target(const RuneCUiState *ui) {
    if (ui->drag.active && ui->drag.source_kind == RUNEC_UI_CONTEXT_INVENTORY
            && ui->drag.source_slot >= 0
            && ui->drag.source_slot < RUNEC_UI_INV_SLOT_COUNT
            && ui->inventory[ui->drag.source_slot].enabled) {
        Vector2 mouse = GetMousePosition();
        Rectangle r = {mouse.x - 16, mouse.y - 16,
                       RUNEC_OSRS_INVENTORY_SLOT_W,
                       RUNEC_OSRS_INVENTORY_SLOT_H};
        DrawRectangleRec(r, (Color){0, 0, 0, 80});
        draw_inventory_item(ui, &ui->inventory[ui->drag.source_slot], r);
    }
    if (ui->selected_target.kind == RUNEC_UI_SELECTED_NONE)
        return;
    Vector2 mouse = GetMousePosition();
    char text[96];
    snprintf(text, sizeof(text), "%s %s ->", ui->selected_target.verb,
             ui->selected_target.label);
    int width = MeasureText(text, 12) + 10;
    Rectangle box = {mouse.x + 12, mouse.y + 12, (float)width, 20};
    DrawRectangleRec(box, (Color){28, 23, 17, 230});
    DrawRectangleLinesEx(box, 1, (Color){170, 137, 72, 255});
    draw_text_shadow(ui, text, box.x + 5, box.y + 4, 11, OSRS_YELLOW);
}

static Rectangle open_interface_mount_rect(const RuneCUiState *ui,
                                           const RuneCUiLayout *layout,
                                           const RuneCUiOpenInterface *open,
                                           int screen_w,
                                           int screen_h) {
    Rectangle screen = {0, 0, (float)screen_w, (float)screen_h};
    if (ui && open && open->target_component_id != 0) {
        const char *top_group =
            open_group_for_mount(ui, RUNEC_UI_MOUNT_SCREEN, RUNEC_UI_TAB_NONE);
        Rectangle target = {0};
        if (top_group && runec_ui_interfaces_component_rect_by_id(
                &ui->interfaces, top_group, open->target_component_id, screen,
                &target)) {
            return target;
        }
    }

    switch (open ? open->mount : RUNEC_UI_MOUNT_SCREEN) {
    case RUNEC_UI_MOUNT_CHAT:
        return layout->chat;
    case RUNEC_UI_MOUNT_MAP:
        return layout->map;
    case RUNEC_UI_MOUNT_SIDE:
        return layout->side;
    case RUNEC_UI_MOUNT_SIDE_CONTENT:
        return layout->side_content;
    case RUNEC_UI_MOUNT_OVERLAY:
    case RUNEC_UI_MOUNT_MODAL:
    case RUNEC_UI_MOUNT_SCREEN:
    default:
        return screen;
    }
}

static void draw_open_overlay_interfaces(RuneCUiState *ui,
                                         const RuneCUiLayout *layout,
                                         int screen_w,
                                         int screen_h) {
    if (!ui || !ui->decoded_ui_enabled || !ui->decoded_ui_ready)
        return;
    RuneCUiComponentOverrides overrides = decoded_overrides(ui);
    for (int i = 0; i < ui->open_interface_count; i++) {
        RuneCUiOpenInterface *open = &ui->open_interfaces[i];
        if (!open->active || !open->group[0])
            continue;
        if (open->mount != RUNEC_UI_MOUNT_OVERLAY
                && open->mount != RUNEC_UI_MOUNT_MODAL)
            continue;
        Rectangle mount =
            open_interface_mount_rect(ui, layout, open, screen_w, screen_h);
        runec_ui_interfaces_draw_group_ex(&ui->interfaces, &ui->assets,
                                          open->group, mount, &overrides);
    }
}

void runec_ui_draw(RuneCUiState *ui, int screen_w, int screen_h) {
    RuneCUiLayout layout;
    ui_layout(screen_w, screen_h, &layout);
    apply_decoded_mounts(ui, screen_w, screen_h, &layout);
    sync_decoded_component_overrides(ui);

    draw_minimap(ui, &layout);
    draw_chat(ui, &layout);
    draw_side(ui, &layout);
    draw_open_overlay_interfaces(ui, &layout, screen_w, screen_h);
    draw_bank(ui, screen_w, screen_h, &layout);
    draw_selected_target(ui);
    draw_context(ui);
    draw_item_hover(ui, &layout, screen_w, screen_h);
}
