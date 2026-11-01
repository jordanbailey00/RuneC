#ifndef RUNEC_VIEWER_UI_REFERENCE_H
#define RUNEC_VIEWER_UI_REFERENCE_H

#include "raylib.h"

/*
 * OSRS UI reference constants reviewed from b237 interface dumps and tracked
 * here as RuneC-owned layout data. The viewer should use these as layout
 * authority instead of hand-tuned gameframe coordinates.
 */

#define RUNEC_IFACE_TOPLEVEL_OSRS_STRETCH 161
#define RUNEC_IFACE_ORBS 160
#define RUNEC_IFACE_INVENTORY 149
#define RUNEC_IFACE_STATS 320
#define RUNEC_IFACE_WORNITEMS 387
#define RUNEC_IFACE_PRAYERBOOK 541
#define RUNEC_IFACE_MAGIC_SPELLBOOK 218
#define RUNEC_IFACE_COMBAT_INTERFACE 593

#define RUNEC_OSRS_MAP_CONTAINER_W 211.0f
#define RUNEC_OSRS_MAP_CONTAINER_H 207.0f
#define RUNEC_OSRS_MINIMAP_X 53.0f
#define RUNEC_OSRS_MINIMAP_Y 8.0f
#define RUNEC_OSRS_MINIMAP_W 152.0f
#define RUNEC_OSRS_MINIMAP_H 152.0f
#define RUNEC_OSRS_MAP_SURROUND_X 29.0f
#define RUNEC_OSRS_MAP_SURROUND_Y 0.0f
#define RUNEC_OSRS_MAP_SURROUND_W 182.0f
#define RUNEC_OSRS_MAP_SURROUND_H 166.0f
#define RUNEC_OSRS_COMPASS_X 34.0f
#define RUNEC_OSRS_COMPASS_Y 5.0f
#define RUNEC_OSRS_COMPASS_W 35.0f
#define RUNEC_OSRS_COMPASS_H 35.0f

#define RUNEC_OSRS_ORBS_X 0.0f
#define RUNEC_OSRS_ORBS_Y 10.0f
#define RUNEC_OSRS_ORBS_W 207.0f
#define RUNEC_OSRS_ORBS_H 197.0f
#define RUNEC_OSRS_HP_X 0.0f
#define RUNEC_OSRS_HP_Y 37.0f
#define RUNEC_OSRS_PRAYER_X 0.0f
#define RUNEC_OSRS_PRAYER_Y 71.0f
#define RUNEC_OSRS_RUN_X 10.0f
#define RUNEC_OSRS_RUN_Y 103.0f
#define RUNEC_OSRS_SPEC_X 32.0f
#define RUNEC_OSRS_SPEC_Y 128.0f
#define RUNEC_OSRS_WORLDMAP_X 177.0f
#define RUNEC_OSRS_WORLDMAP_Y 137.0f

#define RUNEC_OSRS_CHAT_W 519.0f
#define RUNEC_OSRS_CHAT_H 165.0f
#define RUNEC_OSRS_CHAT_AREA_H 142.0f
#define RUNEC_OSRS_CHAT_DISPLAY_X 7.0f
#define RUNEC_OSRS_CHAT_DISPLAY_Y 6.0f
#define RUNEC_OSRS_CHAT_SCROLL_W 489.0f
#define RUNEC_OSRS_CHAT_SCROLL_H 114.0f
#define RUNEC_OSRS_CHAT_LINE_H 14
#define RUNEC_OSRS_CHAT_TEXT_INSET 3
#define RUNEC_OSRS_SCROLLBAR_W 16.0f
#define RUNEC_OSRS_SIDE_MENU_W 241.0f
#define RUNEC_OSRS_SIDE_MENU_H 335.0f
#define RUNEC_OSRS_SIDE_CONTENT_X 25.0f
#define RUNEC_OSRS_SIDE_CONTENT_Y 37.0f
#define RUNEC_OSRS_SIDE_CONTENT_W 190.0f
#define RUNEC_OSRS_SIDE_CONTENT_H 261.0f
#define RUNEC_OSRS_SIDE_TOP_Y 0.0f
#define RUNEC_OSRS_SIDE_BOTTOM_Y 298.0f

#define RUNEC_OSRS_INVENTORY_SLOT_COLS 4
#define RUNEC_OSRS_INVENTORY_SLOT_COUNT 28
#define RUNEC_OSRS_INVENTORY_SLOT_X 16.0f
#define RUNEC_OSRS_INVENTORY_SLOT_Y 8.0f
#define RUNEC_OSRS_INVENTORY_SLOT_STEP_X 42.0f
#define RUNEC_OSRS_INVENTORY_SLOT_STEP_Y 36.0f
#define RUNEC_OSRS_INVENTORY_SLOT_W 36.0f
#define RUNEC_OSRS_INVENTORY_SLOT_H 32.0f

// icon_asset is NULL on tabs the viewer does not use: they stay blank and
// take no clicks.
typedef struct RuneCUiStoneRef {
    int logical_tab;
    const char *stone_asset;
    const char *icon_asset;
    Rectangle rect;
    Rectangle icon_rect;
} RuneCUiStoneRef;

static const RuneCUiStoneRef RUNEC_OSRS_SIDE_STONES[] = {
    {0, "side_stone_highlights_0", "side_icon_combat",    {0,   0, 38, 36}, {4,   0, 33, 36}},
    {1, "side_stone_highlights_4", "side_icon_stats",     {38,  0, 33, 36}, {38,  0, 33, 36}},
    {-1, NULL, NULL,                                      {71,  0, 33, 36}, {71,  0, 33, 36}},
    {2, "side_stone_highlights_4", "side_icon_inventory", {104, 0, 33, 36}, {104, 0, 33, 36}},
    {3, "side_stone_highlights_4", "side_icon_equipment", {137, 0, 33, 36}, {137, 0, 33, 36}},
    {4, "side_stone_highlights_4", "side_icon_prayer",    {170, 0, 33, 36}, {170, 0, 33, 36}},
    {5, "side_stone_highlights_1", "side_icon_magic",     {203, 0, 38, 36}, {204, 0, 33, 36}},
    {6, "side_stone_highlights_2", "side_icon_clan",      {0,   0, 38, 36}, {4,   0, 33, 36}},
    {-1, NULL, NULL,                                      {38,  0, 33, 36}, {38,  0, 33, 36}},
    {-1, NULL, NULL,                                      {71,  0, 33, 36}, {71,  0, 33, 36}},
    {-1, NULL, NULL,                                      {104, 0, 33, 36}, {104, 0, 33, 36}},
    {-1, NULL, NULL,                                      {137, 0, 33, 36}, {137, 0, 33, 36}},
    {-1, NULL, NULL,                                      {170, 0, 33, 36}, {170, 0, 33, 36}},
    {-1, NULL, NULL,                                      {203, 0, 38, 36}, {204, 0, 33, 36}},
};

// icon is the skill's sprite (enum 255); icon_x its offset in script 393.
typedef struct RuneCUiSkillRef {
    const char *name;
    const char *icon;
    int icon_x;
    Rectangle rect;
} RuneCUiSkillRef;

static const RuneCUiSkillRef RUNEC_OSRS_SKILLS[] = {
    {"Attack",       "197", 0,  {1,   1,   62, 30}},
    {"Strength",     "198", 0,  {1,   31,  62, 30}},
    {"Defence",      "199", 1,  {1,   61,  62, 30}},
    {"Ranged",       "200", 1,  {1,   91,  62, 30}},
    {"Prayer",       "201", 1,  {1,   121, 62, 30}},
    {"Magic",        "202", 1,  {1,   151, 62, 30}},
    {"Runecraft",    "215", 1,  {1,   181, 62, 30}},
    {"Construction", "221", 0,  {1,   211, 62, 32}},
    {"Hitpoints",    "203", 1,  {64,  1,   62, 30}},
    {"Agility",      "204", 1,  {64,  31,  62, 30}},
    {"Herblore",     "205", 1,  {64,  61,  62, 30}},
    {"Thieving",     "206", 2,  {64,  91,  62, 30}},
    {"Crafting",     "207", 2,  {64,  121, 62, 30}},
    {"Fletching",    "208", 2,  {64,  151, 62, 30}},
    {"Slayer",       "216", -1, {64,  181, 62, 30}},
    {"Hunter",       "220", 0,  {64,  211, 62, 32}},
    {"Mining",       "209", 2,  {127, 1,   62, 30}},
    {"Smithing",     "210", 2,  {127, 31,  62, 30}},
    {"Fishing",      "211", 1,  {127, 61,  62, 30}},
    {"Cooking",      "212", 3,  {127, 91,  62, 30}},
    {"Firemaking",   "213", 2,  {127, 121, 62, 30}},
    {"Woodcutting",  "214", 2,  {127, 151, 62, 30}},
    {"Farming",      "217", 1,  {127, 181, 62, 30}},
    {"Sailing",      "228", 1,  {127, 211, 62, 32}},
};

static const Rectangle RUNEC_OSRS_STATS_TOTAL = {0, 241, 190, 19};

typedef struct RuneCUiCombatStyleRef {
    Rectangle rect;
    Rectangle icon_rect;
    Rectangle text_rect;
} RuneCUiCombatStyleRef;

// Interface 593 as script 7593 lays it out on b241.
static const RuneCUiCombatStyleRef RUNEC_OSRS_COMBAT_STYLES[] = {
    {{20, 45, 71, 47}, {38,  50,  34, 24}, {21,  75,  68, 13}},
    {{99, 45, 71, 47}, {117, 50,  34, 24}, {100, 75,  68, 13}},
    {{20, 99, 71, 47}, {38,  104, 34, 24}, {21,  129, 68, 13}},
    {{99, 99, 71, 47}, {117, 104, 34, 24}, {100, 129, 68, 13}},
};

// Weapons that autocast list their styles in one column beside the spell boxes.
static const Rectangle RUNEC_OSRS_COMBAT_STAFF_STYLE = {20, 45, 71, 32};
#define RUNEC_OSRS_COMBAT_STAFF_STYLE_STEP 36.0f
static const Rectangle RUNEC_OSRS_COMBAT_AUTOCAST_DEFENSIVE = {99, 45, 71, 50};
static const Rectangle RUNEC_OSRS_COMBAT_AUTOCAST_NORMAL = {99, 99, 71, 50};

static const Rectangle RUNEC_OSRS_COMBAT_TITLE = {20, 0, 169, 30};
static const Rectangle RUNEC_OSRS_COMBAT_LEVEL = {20, 26, 169, 12};
static const Rectangle RUNEC_OSRS_COMBAT_RETALIATE = {20, 153, 150, 44};
static const Rectangle RUNEC_OSRS_COMBAT_RETALIATE_ICON = {27, 156, 26, 39};
static const Rectangle RUNEC_OSRS_COMBAT_RETALIATE_TEXT = {56, 154, 112, 42};
static const Rectangle RUNEC_OSRS_COMBAT_SPECIAL_BAR = {20, 204, 150, 28};
static const Rectangle RUNEC_OSRS_COMBAT_CATEGORY = {0, 231, 190, 28};

// Script 187: with a special in both hands the bar splits, the weapon's half
// on the left, each half with its orb at the outer end.
typedef struct RuneCUiSpecialHalfRef {
    Rectangle rect;
    Rectangle stone;
    Rectangle bar;
    Rectangle text;
    Rectangle ring;
    Rectangle orb;
} RuneCUiSpecialHalfRef;

static const RuneCUiSpecialHalfRef RUNEC_OSRS_COMBAT_SPECIAL_HALVES[2] = {
    {{10, 204, 83, 28}, {22, 205, 71, 26}, {33, 212, 58, 12},
     {31, 205, 58, 28}, {10, 204, 28, 28}, {11, 205, 26, 26}},
    {{97, 204, 83, 28}, {97, 205, 71, 26}, {99, 212, 58, 12},
     {101, 205, 58, 28}, {152, 204, 28, 28}, {153, 205, 26, 26}},
};

typedef struct RuneCUiWornButtonRef {
    const char *asset;
    Rectangle rect;
    Rectangle icon_rect;
} RuneCUiWornButtonRef;

static const RuneCUiWornButtonRef RUNEC_OSRS_WORN_BUTTONS[] = {
    {"675",  {7,   208, 40, 40}, {10,  210, 32, 32}},
    {"1090", {52,  208, 40, 40}, {56,  212, 32, 32}},
    {"912",  {97,  208, 40, 40}, {99,  211, 34, 34}},
    {"1343", {142, 208, 40, 40}, {145, 211, 32, 32}},
};

#endif
