#ifndef RC_SLAYER_H
#define RC_SLAYER_H

#include <stdint.h>

#define RC_SLAYER_MAX_MASTERS 16
#define RC_SLAYER_MAX_TASKS 128
#define RC_SLAYER_MAX_LOCATIONS 16
#define RC_SLAYER_MAX_BOSS_CANDIDATES 40

enum {
    RC_SLAYER_UNLOCK_LIKE_A_BOSS = 1u << 1,
    RC_SLAYER_UNLOCK_WATCH_THE_BIRDIE = 1u << 2,
    RC_SLAYER_UNLOCK_BASILOCKED = 1u << 3,
    RC_SLAYER_UNLOCK_REPTILE_GOT_RIPPED = 1u << 4,
    RC_SLAYER_UNLOCK_SEEING_RED = 1u << 5,
    RC_SLAYER_UNLOCK_HOT_STUFF = 1u << 6,
    RC_SLAYER_UNLOCK_ACTUAL_VAMPYRE_SLAYER = 1u << 7,
    RC_SLAYER_UNLOCK_WARPED_REALITY = 1u << 8,
    RC_SLAYER_UNLOCK_LURED_IN = 1u << 9,
    RC_SLAYER_UNLOCK_WINGS_SPREAD = 1u << 10,
    RC_SLAYER_UNLOCK_QUEST_GATES = 1u << 29,
    RC_SLAYER_UNLOCK_SAILING_CONTENT = 1u << 30,
};

enum {
    RC_SLAYER_TASK_HAS_LOCATIONS     = 1u << 0,
    RC_SLAYER_TASK_BOSS_SECOND_ROLL  = 1u << 1,
    RC_SLAYER_TASK_HAS_ALTERNATIVES  = 1u << 2,
};

#define RC_SLAYER_PROG_PRIEST_IN_PERIL          (1ull << 0)
#define RC_SLAYER_PROG_DRAGON_SLAYER_I          (1ull << 1)
#define RC_SLAYER_PROG_DRAGON_SLAYER_II         (1ull << 2)
#define RC_SLAYER_PROG_BONE_VOYAGE              (1ull << 3)
#define RC_SLAYER_PROG_ELEMENTAL_WORKSHOP_I     (1ull << 4)
#define RC_SLAYER_PROG_LOST_CITY                (1ull << 5)
#define RC_SLAYER_PROG_OLAFS_QUEST              (1ull << 6)
#define RC_SLAYER_PROG_HORROR_FROM_THE_DEEP     (1ull << 7)
#define RC_SLAYER_PROG_MOURNINGS_END_PART_II    (1ull << 8)
#define RC_SLAYER_PROG_DESERT_TREASURE_I        (1ull << 9)
#define RC_SLAYER_PROG_DESERT_TREASURE_II       (1ull << 10)
#define RC_SLAYER_PROG_SONG_OF_THE_ELVES        (1ull << 11)
#define RC_SLAYER_PROG_REGICIDE                 (1ull << 12)
#define RC_SLAYER_PROG_PERILOUS_MOONS           (1ull << 13)
#define RC_SLAYER_PROG_WATERFALL_QUEST          (1ull << 14)
#define RC_SLAYER_PROG_WATCHTOWER               (1ull << 15)
#define RC_SLAYER_PROG_FAIRYTALE_II             (1ull << 16)
#define RC_SLAYER_PROG_BARBARIAN_TRAINING       (1ull << 17)
#define RC_SLAYER_PROG_THE_FROZEN_DOOR          (1ull << 18)
#define RC_SLAYER_PROG_SECRETS_OF_THE_NORTH     (1ull << 19)
#define RC_SLAYER_PROG_DEATH_PLATEAU            (1ull << 20)
#define RC_SLAYER_PROG_TROUBLED_TORTUGANS       (1ull << 21)
#define RC_SLAYER_PROG_ENTER_THE_ABYSS          (1ull << 22)
#define RC_SLAYER_PROG_CABIN_FEVER              (1ull << 23)
#define RC_SLAYER_PROG_RUM_DEAL                 (1ull << 24)
#define RC_SLAYER_PROG_ERNEST_THE_CHICKEN       (1ull << 25)
#define RC_SLAYER_PROG_SKIPPY_AND_THE_MOGRES    (1ull << 26)
#define RC_SLAYER_PROG_DEATH_TO_THE_DORGESHUUN  (1ull << 27)
#define RC_SLAYER_PROG_CONTACT                  (1ull << 28)
#define RC_SLAYER_PROG_ROYAL_TROUBLE            (1ull << 29)
#define RC_SLAYER_PROG_LEGENDS_QUEST            (1ull << 30)
#define RC_SLAYER_PROG_LUNAR_DIPLOMACY          (1ull << 31)
#define RC_SLAYER_PROG_A_PORCINE_OF_INTEREST    (1ull << 32)
#define RC_SLAYER_PROG_HAUNTED_MINE             (1ull << 33)
#define RC_SLAYER_PROG_SHADOWS_OF_CUSTODIA      (1ull << 34)
#define RC_SLAYER_PROG_FREMENNIK_EXILES         (1ull << 35)

typedef struct {
    char name[48];
    uint64_t progression_flags;
} RcSlayerLocationDef;

typedef struct {
    char name[48];
    uint64_t progression_flags;
    uint64_t progression_any_flags;
    uint8_t req_slayer;
} RcSlayerBossCandidateDef;

typedef struct {
    char name[64];
    char alternatives[128];
    char requirement_text[192];
    char locations[512];
    char boss_candidates[2048];
    RcSlayerLocationDef location_defs[RC_SLAYER_MAX_LOCATIONS];
    RcSlayerBossCandidateDef boss_candidate_defs[
        RC_SLAYER_MAX_BOSS_CANDIDATES];
    uint16_t weight;
    uint16_t amount_min;
    uint16_t amount_max;
    uint16_t extended_min;
    uint16_t extended_max;
    uint32_t unlock_flags;
    uint64_t progression_flags;
    uint64_t progression_any_flags;
    uint16_t task_flags;
    uint8_t req_slayer;
    uint8_t req_combat;
    uint8_t location_count;
    uint8_t boss_candidate_count;
} RcSlayerTaskDef;

typedef struct {
    char name[48];
    uint16_t task_count;
    uint8_t req_slayer;
    uint8_t req_combat;
    uint32_t total_weight;
    RcSlayerTaskDef tasks[RC_SLAYER_MAX_TASKS];
} RcSlayerMasterDef;

struct RcWorld;

extern RcSlayerMasterDef g_rc_slayer_masters[RC_SLAYER_MAX_MASTERS];
extern int g_rc_slayer_master_count;

int rc_load_slayer(const char *path);
int rc_slayer_find_master(const char *name);
int rc_slayer_find_task(int master_idx, const char *task_name);
int rc_slayer_task_eligible(const struct RcWorld *world, int master_idx,
                            int task_idx);
int rc_slayer_block_task(struct RcWorld *world, const char *task_name);
int rc_slayer_prefer_task(struct RcWorld *world, const char *task_name);
int rc_slayer_task_amount_for_roll(const RcSlayerTaskDef *task,
                                   uint32_t roll, int extended);
int rc_slayer_task_amount_for_world(const struct RcWorld *world,
                                    const RcSlayerTaskDef *task,
                                    uint32_t roll, int extended);
int rc_slayer_task_location_for_roll(const struct RcWorld *world,
                                     const RcSlayerTaskDef *task,
                                     uint32_t roll, char *out, int cap);
int rc_slayer_boss_task_for_roll(const struct RcWorld *world,
                                 int master_idx,
                                 const RcSlayerTaskDef *task,
                                 uint32_t roll, char *out, int cap);
int rc_slayer_start_task(struct RcWorld *world, const char *master_name,
                         const char *task_name, int amount);
int rc_slayer_assign_task(struct RcWorld *world, const char *master_name,
                          uint32_t roll, int amount);
const char *rc_slayer_current_task_name(const struct RcWorld *world);
const char *rc_slayer_current_boss_name(const struct RcWorld *world);
const char *rc_slayer_current_location(const struct RcWorld *world);
void rc_slayer_init(struct RcWorld *world);

#endif
