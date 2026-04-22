#ifndef RC_CONFIG_H
#define RC_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

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
};

// World configuration — consumed exactly once at rc_world_create().
// After that, no config-driven branching on the tick path; only the
// base tick dispatcher does a cache-resident bitmask-AND.
typedef struct {
    uint32_t subsystems;             // bitmask of RC_SUB_*
    uint32_t seed;                   // deterministic RNG seed

    // Per-subsystem asset paths. NULL entries must match a zero bit
    // in `subsystems`; mismatches are a config error (checked in
    // rc_world_create).
    const char *regions_dir;         // base: region *.terrain/.objects/.cmap
    const char *npc_defs_path;       // base: npc_defs.bin
    const char *spawns_path;         // base: world.npc-spawns.bin
    const char *varbits_path;        // base: varbits.bin
    const char *items_path;          // RC_SUB_EQUIPMENT / _INVENTORY
    const char *drops_path;          // RC_SUB_LOOT: drops.bin
    const char *skill_drops_path;    // RC_SUB_LOOT: skill_drops.bin
    const char *rdt_path;            // RC_SUB_LOOT: rdt.bin
    const char *gdt_path;            // RC_SUB_LOOT: gdt.bin
    const char *recipes_path;        // RC_SUB_SKILLS: recipes.bin
    const char *quests_path;         // RC_SUB_QUESTS: quests.bin
    const char *dialogue_path;       // RC_SUB_DIALOGUE: dialogue.bin
    const char *shops_path;          // RC_SUB_SHOPS: shops.bin
    const char *slayer_path;         // RC_SUB_SLAYER: slayer.bin
    const char *spells_path;         // RC_SUB_COMBAT magic casts
    const char *encounters_dir;      // RC_SUB_ENCOUNTER: TOML dir (pass 2)
    const char *encounters_path;     // RC_SUB_ENCOUNTER: encounters.bin
} RcWorldConfig;

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

// Convenience: check if a subsystem is enabled on a world.
static inline bool rc_subsystem_enabled(const void *world_enabled_ptr,
                                        uint32_t sub) {
    return (*(const uint32_t *)world_enabled_ptr & sub) != 0;
}

#endif
