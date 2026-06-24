#include "config.h"

// Default asset paths. Relative to the process CWD which is expected
// to be the project root; callers can override per-path by copying
// a preset and mutating fields before passing to rc_world_create().

#define DEFAULT_REGIONS     "data/regions"
#define DEFAULT_NPC_DEFS    "data/defs/npc_defs.bin"
#define DEFAULT_SPAWNS      "data/spawns/world.npc-spawns.bin"
#define DEFAULT_VARBITS     "data/defs/varbits.bin"
#define DEFAULT_VARPS       "data/defs/varps.bin"
#define DEFAULT_ITEMS       "data/defs/items.bin"
#define DEFAULT_NORM        "data/defs/normalization.bin"
#define DEFAULT_DROPS       "data/defs/drops.bin"
#define DEFAULT_SKILL_DROPS "data/defs/skill_drops.bin"
#define DEFAULT_RDT         "data/defs/rdt.bin"
#define DEFAULT_GDT         "data/defs/gdt.bin"
#define DEFAULT_MRDT        "data/defs/mrdt.bin"
#define DEFAULT_RECIPES     "data/defs/recipes.bin"
#define DEFAULT_GATHERING   "data/defs/gathering_nodes.bin"
#define DEFAULT_QUESTS      "data/defs/quests.bin"
#define DEFAULT_DIALOGUE    "data/defs/dialogue.bin"
#define DEFAULT_SHOPS       "data/defs/shops.bin"
#define DEFAULT_SLAYER      "data/defs/slayer.bin"
#define DEFAULT_PRAYERS     "data/defs/prayers.bin"
#define DEFAULT_SPELLS      "data/defs/spells.bin"
#define DEFAULT_COMBAT_PROFILES "data/defs/combat_visuals.tsv"
#define DEFAULT_ACTIONS     "data/defs/player_actions.bin"
#define DEFAULT_MON_MECH    "data/defs/regular_npc_mechanics.bin"
#define DEFAULT_ACT_SCHEMA  "data/defs/activity_schemas.bin"
#define DEFAULT_ACT_SPAWNS  "data/defs/activity_spawns.bin"
#define DEFAULT_ACT_MECH    "data/defs/activity_mechanics.bin"
#define DEFAULT_ACT_STATES  "data/defs/activity_states.bin"
#define DEFAULT_OBJ_DEFS    "data/defs/object_defs.bin"
#define DEFAULT_OBJ_PLACES  "data/defs/object_placements.bin"
#define DEFAULT_OBJ_BEH     "data/defs/object_behaviors.bin"
#define DEFAULT_OBJ_TRANS   "data/defs/object_transports.bin"
#define DEFAULT_COLLISION   "data/defs/collision_tiles.bin"
#define DEFAULT_AREA_FLAGS  "data/defs/area_flags.bin"
#define DEFAULT_TRAVERSAL   "data/defs/traversal_edges.bin"
#define DEFAULT_ENCOUNTERS  "data/curated/encounters"
#define DEFAULT_ENCOUNT_BIN "data/defs/encounters.bin"

RcWorldConfig rc_preset_full_game(void) {
    return (RcWorldConfig){
        .subsystems = RC_SUB_COMBAT | RC_SUB_PRAYER | RC_SUB_EQUIPMENT
                    | RC_SUB_INVENTORY | RC_SUB_CONSUMABLES | RC_SUB_LOOT
                    | RC_SUB_SKILLS | RC_SUB_QUESTS | RC_SUB_DIALOGUE
                    | RC_SUB_SHOPS | RC_SUB_SLAYER | RC_SUB_ENCOUNTER
                    | RC_SUB_OBJECTS | RC_SUB_REGIONS | RC_SUB_STORAGE
                    | RC_SUB_TRAVERSAL,
        .seed            = 0,
        .regions_dir     = DEFAULT_REGIONS,
        .npc_defs_path   = DEFAULT_NPC_DEFS,
        .spawns_path     = DEFAULT_SPAWNS,
        .varbits_path    = DEFAULT_VARBITS,
        .varps_path      = DEFAULT_VARPS,
        .items_path      = DEFAULT_ITEMS,
        .normalization_path = DEFAULT_NORM,
        .drops_path      = DEFAULT_DROPS,
        .skill_drops_path= DEFAULT_SKILL_DROPS,
        .rdt_path        = DEFAULT_RDT,
        .gdt_path        = DEFAULT_GDT,
        .mrdt_path       = DEFAULT_MRDT,
        .recipes_path    = DEFAULT_RECIPES,
        .gathering_nodes_path = DEFAULT_GATHERING,
        .quests_path     = DEFAULT_QUESTS,
        .dialogue_path   = DEFAULT_DIALOGUE,
        .shops_path      = DEFAULT_SHOPS,
        .slayer_path     = DEFAULT_SLAYER,
        .prayers_path    = DEFAULT_PRAYERS,
        .spells_path     = DEFAULT_SPELLS,
        .combat_profiles_path = DEFAULT_COMBAT_PROFILES,
        .player_actions_path = DEFAULT_ACTIONS,
        .monster_mechanics_path = DEFAULT_MON_MECH,
        .activity_schemas_path = DEFAULT_ACT_SCHEMA,
        .activity_spawns_path = DEFAULT_ACT_SPAWNS,
        .activity_mechanics_path = DEFAULT_ACT_MECH,
        .activity_states_path = DEFAULT_ACT_STATES,
        .object_defs_path = DEFAULT_OBJ_DEFS,
        .object_placements_path = DEFAULT_OBJ_PLACES,
        .object_behaviors_path = DEFAULT_OBJ_BEH,
        .object_transports_path = DEFAULT_OBJ_TRANS,
        .collision_tiles_path = DEFAULT_COLLISION,
        .area_flags_path = DEFAULT_AREA_FLAGS,
        .traversal_edges_path = DEFAULT_TRAVERSAL,
        .encounters_dir  = DEFAULT_ENCOUNTERS,
        .encounters_path = DEFAULT_ENCOUNT_BIN,
    };
}

RcWorldConfig rc_preset_combat_only(void) {
    return (RcWorldConfig){
        .subsystems = RC_SUB_COMBAT | RC_SUB_PRAYER | RC_SUB_EQUIPMENT
                    | RC_SUB_INVENTORY | RC_SUB_CONSUMABLES
                    | RC_SUB_ENCOUNTER,
        .seed            = 0,
        .regions_dir     = DEFAULT_REGIONS,
        .npc_defs_path   = DEFAULT_NPC_DEFS,
        .spawns_path     = DEFAULT_SPAWNS,
        .items_path      = DEFAULT_ITEMS,
        .normalization_path = DEFAULT_NORM,
        .prayers_path    = DEFAULT_PRAYERS,
        .spells_path     = DEFAULT_SPELLS,
        .combat_profiles_path = DEFAULT_COMBAT_PROFILES,
        .player_actions_path = DEFAULT_ACTIONS,
        .monster_mechanics_path = DEFAULT_MON_MECH,
        .activity_schemas_path = DEFAULT_ACT_SCHEMA,
        .activity_spawns_path = DEFAULT_ACT_SPAWNS,
        .activity_mechanics_path = DEFAULT_ACT_MECH,
        .activity_states_path = DEFAULT_ACT_STATES,
        .encounters_dir  = DEFAULT_ENCOUNTERS,
        .encounters_path = DEFAULT_ENCOUNT_BIN,
    };
}

RcWorldConfig rc_preset_skilling_only(void) {
    return (RcWorldConfig){
        .subsystems = RC_SUB_SKILLS | RC_SUB_INVENTORY | RC_SUB_EQUIPMENT
                    | RC_SUB_OBJECTS | RC_SUB_REGIONS | RC_SUB_TRAVERSAL,
        .seed            = 0,
        .regions_dir     = DEFAULT_REGIONS,
        .npc_defs_path   = DEFAULT_NPC_DEFS,
        .spawns_path     = DEFAULT_SPAWNS,
        .varbits_path    = DEFAULT_VARBITS,
        .varps_path      = DEFAULT_VARPS,
        .items_path      = DEFAULT_ITEMS,
        .normalization_path = DEFAULT_NORM,
        .skill_drops_path= DEFAULT_SKILL_DROPS,
        .recipes_path    = DEFAULT_RECIPES,
        .gathering_nodes_path = DEFAULT_GATHERING,
        .player_actions_path = DEFAULT_ACTIONS,
        .object_defs_path = DEFAULT_OBJ_DEFS,
        .object_placements_path = DEFAULT_OBJ_PLACES,
        .object_behaviors_path = DEFAULT_OBJ_BEH,
        .object_transports_path = DEFAULT_OBJ_TRANS,
        .collision_tiles_path = DEFAULT_COLLISION,
        .area_flags_path = DEFAULT_AREA_FLAGS,
        .traversal_edges_path = DEFAULT_TRAVERSAL,
    };
}

RcWorldConfig rc_preset_base_only(void) {
    return (RcWorldConfig){
        .subsystems      = 0,
        .seed            = 0,
        .regions_dir     = DEFAULT_REGIONS,
        .npc_defs_path   = DEFAULT_NPC_DEFS,
        .spawns_path     = DEFAULT_SPAWNS,
        .player_actions_path = DEFAULT_ACTIONS,
    };
}
