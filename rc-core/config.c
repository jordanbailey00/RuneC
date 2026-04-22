#include "config.h"

// Default asset paths. Relative to the process CWD which is expected
// to be the project root; callers can override per-path by copying
// a preset and mutating fields before passing to rc_world_create().

#define DEFAULT_REGIONS     "data/regions"
#define DEFAULT_NPC_DEFS    "data/defs/npc_defs.bin"
#define DEFAULT_SPAWNS      "data/spawns/world.npc-spawns.bin"
#define DEFAULT_VARBITS     "data/defs/varbits.bin"
#define DEFAULT_ITEMS       "data/defs/items.bin"
#define DEFAULT_DROPS       "data/defs/drops.bin"
#define DEFAULT_SKILL_DROPS "data/defs/skill_drops.bin"
#define DEFAULT_RDT         "data/defs/rdt.bin"
#define DEFAULT_GDT         "data/defs/gdt.bin"
#define DEFAULT_RECIPES     "data/defs/recipes.bin"
#define DEFAULT_QUESTS      "data/defs/quests.bin"
#define DEFAULT_DIALOGUE    "data/defs/dialogue.bin"
#define DEFAULT_SHOPS       "data/defs/shops.bin"
#define DEFAULT_SLAYER      "data/defs/slayer.bin"
#define DEFAULT_SPELLS      "data/defs/spells.bin"
#define DEFAULT_ENCOUNTERS  "data/curated/encounters"
#define DEFAULT_ENCOUNT_BIN "data/defs/encounters.bin"

RcWorldConfig rc_preset_full_game(void) {
    return (RcWorldConfig){
        .subsystems = RC_SUB_COMBAT | RC_SUB_PRAYER | RC_SUB_EQUIPMENT
                    | RC_SUB_INVENTORY | RC_SUB_CONSUMABLES | RC_SUB_LOOT
                    | RC_SUB_SKILLS | RC_SUB_QUESTS | RC_SUB_DIALOGUE
                    | RC_SUB_SHOPS | RC_SUB_SLAYER | RC_SUB_ENCOUNTER,
        .seed            = 0,
        .regions_dir     = DEFAULT_REGIONS,
        .npc_defs_path   = DEFAULT_NPC_DEFS,
        .spawns_path     = DEFAULT_SPAWNS,
        .varbits_path    = DEFAULT_VARBITS,
        .items_path      = DEFAULT_ITEMS,
        .drops_path      = DEFAULT_DROPS,
        .skill_drops_path= DEFAULT_SKILL_DROPS,
        .rdt_path        = DEFAULT_RDT,
        .gdt_path        = DEFAULT_GDT,
        .recipes_path    = DEFAULT_RECIPES,
        .quests_path     = DEFAULT_QUESTS,
        .dialogue_path   = DEFAULT_DIALOGUE,
        .shops_path      = DEFAULT_SHOPS,
        .slayer_path     = DEFAULT_SLAYER,
        .spells_path     = DEFAULT_SPELLS,
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
        .varbits_path    = DEFAULT_VARBITS,
        .items_path      = DEFAULT_ITEMS,
        .spells_path     = DEFAULT_SPELLS,
        .encounters_dir  = DEFAULT_ENCOUNTERS,
        .encounters_path = DEFAULT_ENCOUNT_BIN,
    };
}

RcWorldConfig rc_preset_skilling_only(void) {
    return (RcWorldConfig){
        .subsystems = RC_SUB_SKILLS | RC_SUB_INVENTORY | RC_SUB_EQUIPMENT,
        .seed            = 0,
        .regions_dir     = DEFAULT_REGIONS,
        .npc_defs_path   = DEFAULT_NPC_DEFS,
        .spawns_path     = DEFAULT_SPAWNS,
        .varbits_path    = DEFAULT_VARBITS,
        .items_path      = DEFAULT_ITEMS,
        .recipes_path    = DEFAULT_RECIPES,
    };
}

RcWorldConfig rc_preset_base_only(void) {
    return (RcWorldConfig){
        .subsystems      = 0,
        .seed            = 0,
        .regions_dir     = DEFAULT_REGIONS,
        .npc_defs_path   = DEFAULT_NPC_DEFS,
        .spawns_path     = DEFAULT_SPAWNS,
        .varbits_path    = DEFAULT_VARBITS,
    };
}
