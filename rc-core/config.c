#include "config.h"

#include "types.h"

#include <stdio.h>

// Default asset paths. Relative to the process CWD which is expected
// to be the project root; callers can override per-path by copying
// a preset and mutating fields before passing to rc_world_create().

#define DEFAULT_REGIONS     "data/regions"
#define DEFAULT_NPC_DEFS    "data/defs/npc_defs.bin"
#define DEFAULT_SPAWNS      "data/spawns/world.npc-spawns.indexed.bin"
#define DEFAULT_GROUND_SPAWNS "data/spawns/world.ground-items.indexed.bin"
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
#define DEFAULT_OBJ_PLACES  "data/regions/world.object-placements.indexed.bin"
#define DEFAULT_OBJ_BEH     "data/defs/object_behaviors.bin"
#define DEFAULT_COLLISION   "data/regions/world.collision-tiles.indexed.bin"
#define DEFAULT_AREA_FLAGS  "data/defs/area_flags.bin"
#define DEFAULT_TRAVERSAL   "data/defs/traversal_edges.bin"
#define DEFAULT_ENCOUNT_BIN "data/defs/encounters.bin"

RcWorldStreamingConfig rc_world_streaming_config_default(void) {
    return (RcWorldStreamingConfig){
        .active_radius_regions = RC_WORLD_STREAMING_DEFAULT_ACTIVE_RADIUS,
        .max_cached_regions = RC_WORLD_STREAMING_DEFAULT_MAX_CACHED_REGIONS,
    };
}

void rc_world_streaming_config_sanitize(RcWorldStreamingConfig *config) {
    if (!config) return;
    RcWorldStreamingConfig defaults = rc_world_streaming_config_default();
    if (config->active_radius_regions < 0)
        config->active_radius_regions = defaults.active_radius_regions;
    if (config->active_radius_regions > 2)
        config->active_radius_regions = 2;
    if (config->max_cached_regions <= 0)
        config->max_cached_regions = defaults.max_cached_regions;
    int64_t side = (int64_t)config->active_radius_regions * 2 + 1;
    int64_t active_regions = side >= 256 ? 65536 : side * side;
    if (config->max_cached_regions < active_regions)
        config->max_cached_regions = (int)active_regions;
}

int rc_world_config_validate(const RcWorldConfig *config,
                             char *message, size_t message_capacity) {
    if (message && message_capacity > 0) message[0] = '\0';
    if (!config) {
        if (message && message_capacity > 0)
            snprintf(message, message_capacity, "world config is null");
        return 0;
    }
    uint32_t unknown = config->subsystems & ~RC_SUB_ALL;
    if (unknown != 0) {
        if (message && message_capacity > 0)
            snprintf(message, message_capacity,
                     "world config has unknown subsystem bits 0x%08x", unknown);
        return 0;
    }
    if (config->npc_capacity <= 0 || config->npc_capacity > RC_MAX_NPCS) {
        if (message && message_capacity > 0)
            snprintf(message, message_capacity,
                     "world NPC capacity %d is outside 1..%d",
                     config->npc_capacity, RC_MAX_NPCS);
        return 0;
    }
#define RC_REQUIRE_PATH(bits, field, label) \
    do { \
        if ((config->subsystems & (bits)) != 0 \
                && (!config->field || !config->field[0])) { \
            if (message && message_capacity > 0) \
                snprintf(message, message_capacity, \
                         "%s requires %s", label, #field); \
            return 0; \
        } \
    } while (0)
    RC_REQUIRE_PATH(RC_SUB_COMBAT | RC_SUB_DIALOGUE | RC_SUB_SHOPS
                    | RC_SUB_SLAYER | RC_SUB_ENCOUNTER | RC_SUB_STORAGE,
                    npc_defs_path, "NPC-backed subsystem");
    RC_REQUIRE_PATH(RC_SUB_EQUIPMENT | RC_SUB_INVENTORY | RC_SUB_CONSUMABLES
                    | RC_SUB_SHOPS | RC_SUB_STORAGE | RC_SUB_LOOT,
                    items_path, "item-backed subsystem");
    RC_REQUIRE_PATH(RC_SUB_PRAYER, prayers_path, "prayer subsystem");
    RC_REQUIRE_PATH(RC_SUB_QUESTS, quests_path, "quest subsystem");
    RC_REQUIRE_PATH(RC_SUB_DIALOGUE, dialogue_path, "dialogue subsystem");
    RC_REQUIRE_PATH(RC_SUB_SHOPS, shops_path, "shop subsystem");
    RC_REQUIRE_PATH(RC_SUB_SLAYER, slayer_path, "slayer subsystem");
    RC_REQUIRE_PATH(RC_SUB_TRAVERSAL, traversal_edges_path,
                    "traversal subsystem");
    RC_REQUIRE_PATH(RC_SUB_LOOT, ground_item_spawns_path,
                    "loot subsystem active-area state");
    if ((config->subsystems & RC_SUB_OBJECTS)
            && ((!config->object_defs_path || !config->object_defs_path[0])
                || (!config->object_behaviors_path
                    || !config->object_behaviors_path[0]))) {
        if (message && message_capacity > 0)
            snprintf(message, message_capacity,
                     "object subsystem requires definitions and behaviors");
        return 0;
    }
    if ((config->subsystems & RC_SUB_REGIONS)
            && ((!config->collision_tiles_path
                 || !config->collision_tiles_path[0])
                && (!config->area_flags_path
                    || !config->area_flags_path[0]))) {
        if (message && message_capacity > 0)
            snprintf(message, message_capacity,
                     "region subsystem requires collision or area data");
        return 0;
    }
#undef RC_REQUIRE_PATH
    return 1;
}

RcWorldConfig rc_preset_full_game(void) {
    return (RcWorldConfig){
        .members_world = true,
        .subsystems = RC_SUB_COMBAT | RC_SUB_PRAYER | RC_SUB_EQUIPMENT
                    | RC_SUB_INVENTORY | RC_SUB_CONSUMABLES | RC_SUB_LOOT
                    | RC_SUB_SKILLS | RC_SUB_QUESTS | RC_SUB_DIALOGUE
                    | RC_SUB_SHOPS | RC_SUB_SLAYER | RC_SUB_ENCOUNTER
                    | RC_SUB_OBJECTS | RC_SUB_REGIONS | RC_SUB_STORAGE
                    | RC_SUB_TRAVERSAL,
        .seed            = 0,
        .npc_capacity    = RC_WORLD_NPC_CAPACITY_FULL,
        .streaming       = rc_world_streaming_config_default(),
        .regions_dir     = DEFAULT_REGIONS,
        .npc_defs_path   = DEFAULT_NPC_DEFS,
        .spawns_path     = DEFAULT_SPAWNS,
        .ground_item_spawns_path = DEFAULT_GROUND_SPAWNS,
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
        .collision_tiles_path = DEFAULT_COLLISION,
        .area_flags_path = DEFAULT_AREA_FLAGS,
        .traversal_edges_path = DEFAULT_TRAVERSAL,
        .encounters_path = DEFAULT_ENCOUNT_BIN,
    };
}

RcWorldConfig rc_preset_combat_only(void) {
    return (RcWorldConfig){
        .members_world = true,
        .subsystems = RC_SUB_COMBAT | RC_SUB_PRAYER | RC_SUB_EQUIPMENT
                    | RC_SUB_INVENTORY | RC_SUB_CONSUMABLES
                    | RC_SUB_ENCOUNTER,
        .seed            = 0,
        .npc_capacity    = RC_WORLD_NPC_CAPACITY_SIM,
        .streaming       = rc_world_streaming_config_default(),
        .regions_dir     = DEFAULT_REGIONS,
        .npc_defs_path   = DEFAULT_NPC_DEFS,
        .spawns_path     = DEFAULT_SPAWNS,
        .ground_item_spawns_path = DEFAULT_GROUND_SPAWNS,
        .varbits_path    = DEFAULT_VARBITS,
        .varps_path      = DEFAULT_VARPS,
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
        .encounters_path = DEFAULT_ENCOUNT_BIN,
    };
}

RcWorldConfig rc_preset_skilling_only(void) {
    return (RcWorldConfig){
        .members_world = true,
        .subsystems = RC_SUB_SKILLS | RC_SUB_INVENTORY | RC_SUB_EQUIPMENT
                    | RC_SUB_OBJECTS | RC_SUB_REGIONS | RC_SUB_TRAVERSAL,
        .seed            = 0,
        .npc_capacity    = RC_WORLD_NPC_CAPACITY_SIM,
        .streaming       = rc_world_streaming_config_default(),
        .regions_dir     = DEFAULT_REGIONS,
        .npc_defs_path   = DEFAULT_NPC_DEFS,
        .spawns_path     = DEFAULT_SPAWNS,
        .ground_item_spawns_path = DEFAULT_GROUND_SPAWNS,
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
        .collision_tiles_path = DEFAULT_COLLISION,
        .area_flags_path = DEFAULT_AREA_FLAGS,
        .traversal_edges_path = DEFAULT_TRAVERSAL,
    };
}

RcWorldConfig rc_preset_base_only(void) {
    return (RcWorldConfig){
        .members_world = true,
        .subsystems      = 0,
        .seed            = 0,
        .npc_capacity    = RC_WORLD_NPC_CAPACITY_BASE,
        .streaming       = rc_world_streaming_config_default(),
        .regions_dir     = DEFAULT_REGIONS,
        .npc_defs_path   = DEFAULT_NPC_DEFS,
        .spawns_path     = DEFAULT_SPAWNS,
        .ground_item_spawns_path = DEFAULT_GROUND_SPAWNS,
        .player_actions_path = DEFAULT_ACTIONS,
    };
}
