#ifndef RC_CONFIG_H
#define RC_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Subsystem bitmask — drives which tick functions run and which
// binaries get loaded at rc_world_create() time. See rc-core/README.md
// §2 for the subsystem list and §3 for the config-driven bring-up
// contract.
//
// Base (always on, not in this mask): tiles, pathfinding, player
// position, NPC position, tick counter, varbit state.
enum {
    RC_SUB_COMBAT       = 1u << 0,
    RC_SUB_PRAYER       = 1u << 1,
    RC_SUB_EQUIPMENT    = 1u << 2,
    RC_SUB_INVENTORY    = 1u << 3,
    RC_SUB_CONSUMABLES  = 1u << 4,    // food / potion
    RC_SUB_LOOT         = 1u << 5,
    RC_SUB_SKILLS       = 1u << 6,
    RC_SUB_QUESTS       = 1u << 7,
    RC_SUB_DIALOGUE     = 1u << 8,
    RC_SUB_SHOPS        = 1u << 9,
    RC_SUB_SLAYER       = 1u << 10,
    RC_SUB_ENCOUNTER    = 1u << 11,   // boss scripts + mechanics
    RC_SUB_OBJECTS      = 1u << 12,   // object defs/placements/actions
    RC_SUB_REGIONS      = 1u << 13,   // collision/area datasets
    RC_SUB_STORAGE      = 1u << 14,   // banks/deposit boxes/storage
    RC_SUB_TRAVERSAL    = 1u << 15,   // traversal edges/teleports
    RC_SUB_ALL          = (1u << 16) - 1u,
};

enum {
    RC_WORLD_NPC_CAPACITY_BASE = 64,
    RC_WORLD_NPC_CAPACITY_SIM = 1024,
    RC_WORLD_NPC_CAPACITY_FULL = 4096,
};

enum {
    RC_WORLD_STREAMING_DEFAULT_ACTIVE_RADIUS = 2,
    RC_WORLD_STREAMING_DEFAULT_MAX_CACHED_REGIONS = 64,
};

typedef struct {
    int active_radius_regions;
    int max_cached_regions;
} RcWorldStreamingConfig;

// World configuration — consumed exactly once at rc_world_create().
// After that, no config-driven branching on the tick path; only the
// base tick dispatcher does a cache-resident bitmask-AND.
typedef struct {
    uint32_t subsystems;             // bitmask of RC_SUB_*
    uint32_t seed;                   // deterministic RNG seed
    bool members_world;              // gameplay availability policy
    int npc_capacity;                // dense active NPC slots for this world
    RcWorldStreamingConfig streaming;

    // Asset paths. rc_world_create_config() loads each path only when
    // its owning subsystem/shared user group is enabled. Disabled
    // subsystems ignore non-NULL paths so presets can carry defaults
    // without forcing unused data into a sim.
    const char *regions_dir;         // base: region *.terrain/.objects/.cmap
    const char *npc_defs_path;       // base: npc_defs.bin
    const char *spawns_path;         // active area: indexed world NPC spawns
    const char *ground_item_spawns_path; // RC_SUB_LOOT: indexed static items
    const char *varbits_path;        // explicit state: varbits.bin
    const char *varps_path;          // explicit state: varps.bin
    const char *items_path;          // RC_SUB_EQUIPMENT / _INVENTORY
    const char *normalization_path;  // shared: normalization.bin
    const char *drops_path;          // RC_SUB_LOOT: drops.bin
    const char *skill_drops_path;    // RC_SUB_LOOT/_SKILLS: skill_drops.bin
    const char *rdt_path;            // RC_SUB_LOOT: rdt.bin
    const char *gdt_path;            // RC_SUB_LOOT: gdt.bin
    const char *mrdt_path;           // RC_SUB_LOOT: mrdt.bin
    const char *recipes_path;        // RC_SUB_SKILLS: recipes.bin
    const char *gathering_nodes_path; // RC_SUB_SKILLS: gathering_nodes.bin
    const char *quests_path;         // RC_SUB_QUESTS: quests.bin
    const char *dialogue_path;       // RC_SUB_DIALOGUE: dialogue.bin
    const char *shops_path;          // RC_SUB_SHOPS: shops.bin
    const char *slayer_path;         // RC_SUB_SLAYER: slayer.bin
    const char *prayers_path;        // RC_SUB_PRAYER: prayers.bin
    const char *spells_path;         // RC_SUB_COMBAT magic casts
    const char *combat_profiles_path; // RC_SUB_COMBAT hit-delay profile rows
    const char *player_actions_path; // base input action gates
    const char *monster_mechanics_path; // RC_SUB_COMBAT/_SLAYER: regular_npc_mechanics.bin
    const char *activity_schemas_path; // RC_SUB_ENCOUNTER: activity_schemas.bin
    const char *activity_spawns_path; // RC_SUB_ENCOUNTER: activity_spawns.bin
    const char *activity_mechanics_path; // RC_SUB_ENCOUNTER: activity_mechanics.bin
    const char *activity_states_path; // RC_SUB_ENCOUNTER: activity_states.bin
    const char *object_defs_path;     // RC_SUB_OBJECTS: object_defs.bin
    const char *object_placements_path; // RC_SUB_OBJECTS: indexed placements
    const char *object_behaviors_path; // RC_SUB_OBJECTS: object_behaviors.bin
    const char *collision_tiles_path; // RC_SUB_REGIONS: indexed collision
    const char *area_flags_path;     // RC_SUB_REGIONS: area_flags.bin
    const char *traversal_edges_path; // RC_SUB_TRAVERSAL: traversal_edges.bin
    const char *encounters_path;     // RC_SUB_ENCOUNTER: encounters.bin
} RcWorldConfig;

RcWorldStreamingConfig rc_world_streaming_config_default(void);
void rc_world_streaming_config_sanitize(RcWorldStreamingConfig *config);
int rc_world_config_validate(const RcWorldConfig *config,
                             char *message, size_t message_capacity);

// Config presets. Use these instead of zero-initialising; the
// presets set sane defaults for the asset paths you typically want.

// Full game: every subsystem + every asset.
RcWorldConfig rc_preset_full_game(void);

// Combat-only sim: base + combat + prayer + equipment + inventory +
// consumables + encounter. No loot / quests / dialogue / shops /
// skills. Target for Colosseum / Inferno / raid RL simulators.
RcWorldConfig rc_preset_combat_only(void);

// Skilling-only sim: base + skills + inventory + equipment (for
// tool use). No combat / prayer / encounter.
RcWorldConfig rc_preset_skilling_only(void);

// Bare base: pathfinding + tiles + player position only. For
// locomotion benchmarks or minimal-state RL.
RcWorldConfig rc_preset_base_only(void);

#endif
