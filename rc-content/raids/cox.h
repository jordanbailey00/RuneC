#ifndef RC_CONTENT_COX_H
#define RC_CONTENT_COX_H

// Chambers of Xeric. RuneC has no dynamic instances, so every raid plays one
// fixed layout: copies of the cache's 32x32 room templates composed into a
// column of the map (content/world/composed_maps.toml), each room's exit
// opening into the next room's entry, then Great Olm's own chamber.
// Mechanics, sources and the values chosen where sources conflict:
// rc-content/README.md.

#include "../../rc-core/npc.h"
#include "../../rc-core/types.h"

#include <stdbool.h>
#include <stdint.h>

struct RcWorld;

#define COX_ROOM_SIZE 32
#define COX_MAX_ROOMS 8         // the layout's rooms and Great Olm
#define COX_ROOM_NPCS 16
#define COX_MAX_HAZARDS 48
#define COX_NO_ROOM 0xFF
#define COX_NO_NPC UINT32_MAX

typedef enum {
    COX_ROOM_NONE = 0,
    COX_ROOM_START,
    COX_ROOM_END,
    COX_ROOM_SCAVENGERS,
    COX_ROOM_SCAVENGERS_LARGE,
    COX_ROOM_FARMING_FISH,
    COX_ROOM_FARMING_BATS,
    COX_ROOM_TEKTON,
    COX_ROOM_MUTTADILES,
    COX_ROOM_GUARDIANS,
    COX_ROOM_VESPULA,
    COX_ROOM_SHAMANS,
    COX_ROOM_VASA,
    COX_ROOM_VANGUARDS,
    COX_ROOM_MYSTICS,
    COX_ROOM_ICE_DEMON,
    COX_ROOM_THIEVING,
    COX_ROOM_TIGHTROPE,
    COX_ROOM_CRABS,
    COX_ROOM_OLM,
    COX_ROOM_TYPE_COUNT,
} CoxRoomType;

enum { COX_EXIT_WEST = 0, COX_EXIT_NORTH = 1, COX_EXIT_EAST = 2 };

enum {
    COX_ROOM_ENTERED = 1u << 0,
    COX_ROOM_COMPLETE = 1u << 1,
    COX_ROOM_SPAWNED = 1u << 2,    // room NPCs/locs placed this visit
    COX_ROOM_STARTED = 1u << 3,    // its encounter has begun
};

// One room NPC remembered across visits: the active area drops dynamic NPCs
// far from the player, so a revisited room restores what it lost.
typedef struct CoxStats CoxStats;
typedef struct {
    RcNpcId uid;
    int16_t npc_id;
    uint8_t role;
    bool scripted;
    int16_t x, y;
    int16_t hp;
    const CoxStats *stats;
    // Its state when last seen, restored with it: fights change forms,
    // targetability and combat values (drains, enrage bonuses).
    int16_t form;
    bool untargetable, owner_controlled;
    int16_t live_stats[6];
    int16_t defence_bonus[5];
    uint16_t max_hit_override;
} CoxRoomNpc;

typedef struct {
    RcNpcId uid;
    RcTick next_tick;
} CoxActorTimer;

typedef struct {
    uint8_t type;           // CoxRoomType
    uint8_t variant;        // COX_EXIT_*
    uint8_t flags;          // COX_ROOM_*
    int16_t x, y, plane;    // template block south-west corner
    RcTick started_tick;
    RcTick completed_tick;
    CoxRoomNpc npcs[COX_ROOM_NPCS];
    uint8_t npc_count;
    // Room-specific state: each room module casts this to its own struct
    // (COX_ROOM_STATE checks the size).
    _Alignas(8) uint8_t state[1024];
} CoxRoom;

// A room module's view of CoxRoom.state; COX_STATE_FITS checks the size
// once at file scope.
#define COX_ROOM_STATE(type, room) ((type *)(room)->state)
#define COX_STATE_FITS(type) \
    _Static_assert(sizeof(type) <= sizeof(((CoxRoom *)0)->state), \
                   #type " exceeds the room state")

enum {
    COX_HAZARD_STRIKE = 1,   // lands once at land_tick
    COX_HAZARD_POOL = 2,     // damages each tick from land_tick to expire_tick
};

enum {
    COX_HAZARD_POISON = 1u << 0,
    COX_HAZARD_PRAYER_HALVES = 1u << 1,  // protection prayer halves (else ignored)
};

typedef struct {
    uint8_t kind;           // COX_HAZARD_*, 0 = free slot
    uint8_t radius;         // Chebyshev: 0 = the tile, 1 = 3x3, 2 = 5x5
    uint8_t style;          // RcCombatStyle for prayer and hitsplat
    uint8_t flags;
    int16_t x, y, plane;
    RcTick land_tick, expire_tick;
    uint16_t min, max;      // damage on the centre tiles
    uint16_t edge_min, edge_max; // damage beside the centre (radius >= 1)
    RcNpcId source;
    int loc_id;             // pool: loc shown while live, or -1
    uint64_t loc_key;
} CoxHazard;

typedef struct CoxOlm CoxOlm;

// Raid skilling actions (cox_tick_action runs the current one).
enum {
    COX_ACT_NONE = 0,
    COX_ACT_CHOP,           // sapling -> kindling
    COX_ACT_FISH,
    COX_ACT_CATCH_BAT,
    COX_ACT_RAKE,
    COX_ACT_COOK,
    COX_ACT_MIX,            // potion making, one per cycle
    COX_ACT_SHORTCUT,       // mine / chop / push a scavenger-room shortcut
    COX_ACT_PICK,           // grown herb patch
    COX_ACT_LIGHT_BRAZIER,  // cox_puzzle_rooms.c
    COX_ACT_OPEN_CHEST,     // cox_puzzle_rooms.c
    COX_ACT_CROSS_ROPE,     // cox_puzzle_rooms.c
    COX_ACT_CUT_MEAT_TREE,  // cox_combat_rooms.c
};

// Content effects raid consumables carry (RcConsumableDef.effect).
enum {
    COX_EFFECT_OVERLOAD = 1,        // + tier 0..2 ((-), regular, (+))
    COX_EFFECT_PRAYER_ENHANCE = 4,  // + tier 0..2
};

// The raid's skilling action: one at a time, cancelled when the player
// moves or acts.
typedef struct {
    uint8_t kind;
    int target_x, target_y, plane;
    uint64_t target_key;
    int target_id;
    int param;
    RcTick next_tick;
    RcTick started_tick;
    int player_x, player_y;
    uint64_t interaction_generation;
} CoxAction;

typedef struct {
    // Entry setting (raids are solo: no party; Challenge Mode is not
    // implemented).
    bool full_difficulty;          // Captain Rimor: no level scaling

    // Raid state.
    bool in_raid;                  // inside the chambers (lobby included)
    bool started;                  // "The raid has begun!"
    uint8_t room_count;
    uint8_t current_room;
    uint8_t olm_room;
    bool reached_olm;              // dropped into Great Olm's chamber
    CoxRoom rooms[COX_MAX_ROOMS];
    RcTick start_tick;
    RcTick finish_tick;
    bool olm_dead;

    // Scaling inputs fixed at raid start.
    int scale_combat;              // C
    int scale_hp;                  // H'

    // Points.
    int32_t personal_points;
    int32_t total_points;

    // Death handling.
    RcTick respawn_tick;

    CoxHazard hazards[COX_MAX_HAZARDS];
    CoxAction action;

    // Raid potion effects on the player.
    uint8_t overload_tier;
    RcTick overload_until, overload_next_boost, overload_next_damage;
    uint8_t overload_hits_left;
    uint8_t enhance_tier;
    RcTick enhance_until, enhance_next_pulse;
    uint16_t enhance_interval;
    // Storage unit tier built this raid (0 tiny .. 4 massive).
    uint8_t storage_tier;
    CoxOlm *olm;

    // Combat hooks this module wraps (called through for other NPCs).
    void (*prev_on_player_hit_npc)(struct RcWorld *, RcNpc *,
                                   const RcPendingHit *, int);
    void (*prev_on_npc_hit_player)(struct RcWorld *, const RcPendingHit *, int);
} CoxRaid;

typedef struct {
    void (*enter)(struct RcWorld *world, CoxRaid *raid, CoxRoom *room);
    void (*tick)(struct RcWorld *world, CoxRaid *raid, CoxRoom *room);
    // A room NPC died (or was removed by its room script).
    void (*npc_died)(struct RcWorld *world, CoxRaid *raid, CoxRoom *room,
                     RcNpc *npc, int role);
    // A player hit landed on a room NPC.
    void (*npc_hit)(struct RcWorld *world, CoxRaid *raid, CoxRoom *room,
                    RcNpc *npc, int role, int damage);
    // A room NPC's hit landed on the player.
    void (*hit_player)(struct RcWorld *world, CoxRaid *raid, CoxRoom *room,
                       RcNpc *npc, int role, const RcPendingHit *hit,
                       int damage);
} CoxRoomOps;

// Solo base stats (wiki infobox "Normal": a 126-combat, 99-HP player) and
// the bonuses CoX NPCs fight with.
struct CoxStats {
    int16_t hp, att, str, def, mag, rng;
    int16_t attack_bonus[5];       // stab, slash, crush, ranged, magic
    int16_t defence_bonus[5];
    int16_t max_hit;               // solo value; COX_STATS_MAX_FORMULA rescales it
    int16_t max_bonus;             // strength/ranged strength/magic damage bonus
    uint8_t attack_speed;
    uint8_t style;                 // RcCombatStyle of its ordinary attack
    uint8_t flags;                 // COX_STATS_*
};

enum {
    COX_STATS_MAGIC_DEFENSIVE = 1u << 0,  // magic scales as a defence stat
    COX_STATS_SINGLES = 1u << 1,          // no party term (scavengers, soldiers)
    // Max hit from the scaled attacking level and max_bonus with the OSRS
    // NPC formula floor(((level + 9) * (bonus + 64) + 320) / 640).
    COX_STATS_MAX_FORMULA = 1u << 2,
};

// cox_raid.c
CoxRaid *cox_raid(const struct RcWorld *world);
const CoxRoomOps *cox_room_ops(int type);
int cox_room_index(const CoxRaid *raid, const CoxRoom *room);
CoxRoom *cox_room_for_npc(CoxRaid *raid, RcNpcId uid, int *role);
CoxRoom *cox_room_at(CoxRaid *raid, int x, int y, int plane);
void cox_message(struct RcWorld *world, const char *fmt, ...);
void cox_add_points(CoxRaid *raid, int points);
void cox_complete_room(struct RcWorld *world, CoxRaid *raid, CoxRoom *room);
int cox_world_x(const CoxRoom *room, int lx);
int cox_world_y(const CoxRoom *room, int ly);
// Room-local tile of a template-local position for this room's variant.
int cox_room_local_x(const CoxRoom *room, int wx);
int cox_room_local_y(const CoxRoom *room, int wy);
bool cox_tile_walkable(const struct RcWorld *world, int x, int y, int plane);
bool cox_player_in_room(const struct RcWorld *world, const CoxRoom *room);
int cox_chebyshev(int ax, int ay, int aw, int bx, int by, int bw);
int cox_player_distance(const struct RcWorld *world, const RcNpc *npc);
int cox_rand(struct RcWorld *world, int bound);
int cox_scale(const CoxRaid *raid, const CoxStats *base,
              RcNpcCombatOverride *out);
int cox_npc_max_hit(int level, int bonus);
RcNpc *cox_spawn(struct RcWorld *world, CoxRaid *raid, CoxRoom *room,
                 int npc_id, int x, int y, int role, const CoxStats *stats,
                 bool scripted);
RcNpc *cox_room_npc(struct RcWorld *world, CoxRoom *room, int role);
int cox_room_npc_role(const CoxRoom *room, RcNpcId uid);
void cox_remove_room_npcs(struct RcWorld *world, CoxRoom *room);
int cox_find_loc(const struct RcWorld *world, const CoxRoom *room, int loc_id,
                 int *out_x, int *out_y, int *out_rotation, int max);
int cox_add_loc(struct RcWorld *world, int loc_id, int type, int rotation,
                int x, int y, int plane, uint64_t *out_key);
int cox_remove_loc(struct RcWorld *world, int loc_id, int x, int y, int plane);
// Whether `loc_id` stands on the tile now: a live map placement or a
// runtime spawn/replacement.
bool cox_loc_present(const struct RcWorld *world, int loc_id, int x, int y,
                     int plane);
int cox_replace_loc(struct RcWorld *world, int loc_id, int x, int y, int plane,
                    int replacement_id);
int cox_add_hazard(CoxRaid *raid, const CoxHazard *hazard);
void cox_relocate(struct RcWorld *world, CoxRaid *raid, int x, int y, int plane);
// Starts or replaces the raid skilling action.
void cox_start_action(struct RcWorld *world, CoxRaid *raid, int kind,
                      const RcPendingInteraction *pending, int delay, int param);
bool cox_raid_item(int item_id);
int cox_inventory_count(const RcPlayer *p, int item_id);
bool cox_has_item(const RcPlayer *p, int item_id);
bool cox_has_any(const RcPlayer *p, const int *ids, int count);
int cox_register_object(struct RcWorld *world, int obj_id, int option,
                        RcInteractionHandlerFn fn);
int cox_register_npc(struct RcWorld *world, int npc_id, int option,
                     RcInteractionHandlerFn fn);
int cox_register_item_on_object(struct RcWorld *world, int item_id, int obj_id,
                                RcInteractionHandlerFn fn);
int cox_register_item(struct RcWorld *world, int item_id, int option,
                      RcInteractionHandlerFn fn);
int cox_register_item_on_item(struct RcWorld *world, int item_id,
                              int target_id, RcInteractionHandlerFn fn);

// Room modules.
extern const CoxRoomOps cox_tekton_ops, cox_muttadiles_ops, cox_guardians_ops,
    cox_vespula_ops, cox_shamans_ops, cox_vasa_ops, cox_vanguards_ops,
    cox_mystics_ops, cox_ice_demon_ops, cox_thieving_ops, cox_tightrope_ops,
    cox_crabs_ops, cox_scavengers_ops, cox_farming_ops, cox_olm_ops;
int cox_combat_rooms_register(struct RcWorld *world);
int cox_puzzle_rooms_register(struct RcWorld *world);
int cox_resources_register(struct RcWorld *world);
int cox_olm_register(struct RcWorld *world);
int cox_olm_modify_hit(struct RcWorld *world, CoxRoom *room, int role,
                       RcCombatStyle style, int damage);
// Raid skilling actions and potion effects the controller ticks
// (cox_resources.c); the room modules own their action kinds.
void cox_tick_action(struct RcWorld *world, CoxRaid *raid);
void cox_tick_effects(struct RcWorld *world, CoxRaid *raid);
bool cox_puzzle_action(struct RcWorld *world, CoxRaid *raid, CoxAction *a);
bool cox_combat_action(struct RcWorld *world, CoxRaid *raid, CoxAction *a);
// One OSRS skilling success roll: low/high are the chances out of 256 at
// levels 1 and 99.
bool cox_skill_roll(struct RcWorld *world, int level, int low, int high);
// Chops a CoX sapling for kindling (shared by the Ice demon and resource
// rooms); returns false when the action ends.
bool cox_chop_sapling(struct RcWorld *world, CoxRaid *raid, CoxAction *a);
void cox_add_xp(struct RcWorld *world, int skill, int xp_hundredths);
bool cox_give_item(struct RcWorld *world, int item_id, int quantity);
bool cox_take_item(struct RcWorld *world, int item_id, int quantity);
int cox_swap_items(struct RcWorld *world, int from, int to, int max);
int cox_modify_player_hit(struct RcWorld *world, const RcNpc *target,
                          RcCombatStyle style, int spell_idx, int weapon_id,
                          int damage);

#endif
