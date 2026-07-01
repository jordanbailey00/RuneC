#ifndef RC_ACTIVITY_MECHANICS_H
#define RC_ACTIVITY_MECHANICS_H

#include "types.h"

#include <stdbool.h>
#include <stdint.h>

#define RC_ACTIVITY_MECH_MAX_ROWS 128
#define RC_ACTIVITY_MECH_MAX_NPC_IDS 64
#define RC_ACTIVITY_MECH_MAX_SECTIONS 16

enum {
    RC_ACTIVITY_MECH_STATUS_ENCOUNTER_BACKED = 1,
    RC_ACTIVITY_MECH_STATUS_ACTIVITY_INDEXED = 2,
    RC_ACTIVITY_MECH_STATUS_OWNER_MAPPED = 3,
    RC_ACTIVITY_MECH_STATUS_OWNER_EXECUTABLE = 4,
};

enum {
    RC_ACTIVITY_MECH_HAS_SECTIONS = 1u << 0,
    RC_ACTIVITY_MECH_MISSING_NPC_IDS = 1u << 1,
    RC_ACTIVITY_MECH_HAS_ENCOUNTER = 1u << 2,
    RC_ACTIVITY_MECH_BEHAVIOR_INCOMPLETE = 1u << 3,
    RC_ACTIVITY_MECH_HAS_OWNER = 1u << 4,
    RC_ACTIVITY_MECH_OWNER_ONLY = 1u << 5,
};

enum {
    RC_ACTIVITY_BEHAVIOR_STAT_DRAIN = 1ull << 0,
    RC_ACTIVITY_BEHAVIOR_PRAYER_DRAIN = 1ull << 1,
    RC_ACTIVITY_BEHAVIOR_HEAL_ON_HIT = 1ull << 2,
    RC_ACTIVITY_BEHAVIOR_POISON = 1ull << 3,
    RC_ACTIVITY_BEHAVIOR_VENOM = 1ull << 4,
    RC_ACTIVITY_BEHAVIOR_ENRAGE = 1ull << 5,
    RC_ACTIVITY_BEHAVIOR_AREA_PRESSURE = 1ull << 6,
    RC_ACTIVITY_BEHAVIOR_DAMAGE_GATE = 1ull << 7,
    RC_ACTIVITY_BEHAVIOR_PROTECTION_PIERCE = 1ull << 8,
    RC_ACTIVITY_BEHAVIOR_FORM_ROTATION = 1ull << 9,
    RC_ACTIVITY_BEHAVIOR_WAVE_BOSS = 1ull << 10,
    RC_ACTIVITY_BEHAVIOR_DRAGONFIRE = 1ull << 11,
    RC_ACTIVITY_BEHAVIOR_TELEBLOCK = 1ull << 12,
    RC_ACTIVITY_BEHAVIOR_FREEZE = 1ull << 13,
};

enum {
    RC_ACTIVITY_PROFILE_NONE = 0,
    RC_ACTIVITY_PROFILE_BARROWS_AHRIM = 1,
    RC_ACTIVITY_PROFILE_BARROWS_DHAROK = 2,
    RC_ACTIVITY_PROFILE_BARROWS_GUTHAN = 3,
    RC_ACTIVITY_PROFILE_BARROWS_KARIL = 4,
    RC_ACTIVITY_PROFILE_BARROWS_TORAG = 5,
    RC_ACTIVITY_PROFILE_BARROWS_VERAC = 6,
    RC_ACTIVITY_PROFILE_ARAXXOR = 7,
    RC_ACTIVITY_PROFILE_BLOOD_MOON = 8,
    RC_ACTIVITY_PROFILE_BLUE_MOON = 9,
    RC_ACTIVITY_PROFILE_ECLIPSE_MOON = 10,
    RC_ACTIVITY_PROFILE_REVENANT_MALEDICTUS = 11,
    RC_ACTIVITY_PROFILE_SHELLBANE_GRYPHON = 12,
    RC_ACTIVITY_PROFILE_TZTOK_JAD = 13,
    RC_ACTIVITY_PROFILE_DAWN = 14,
    RC_ACTIVITY_PROFILE_DUSK = 15,
    RC_ACTIVITY_PROFILE_GROTESQUE_GUARDIANS = 16,
    RC_ACTIVITY_PROFILE_BRUTUS = 17,
    RC_ACTIVITY_PROFILE_DEMONIC_BRUTUS = 18,
    RC_ACTIVITY_PROFILE_ROYAL_TITANS = 19,
    RC_ACTIVITY_PROFILE_MOONS_OF_PERIL = 20,
};

typedef struct {
    char title[96];
    uint32_t text_hash;
    uint32_t text_len;
} RcActivityMechanicSection;

typedef struct {
    char slug[64];
    char name[64];
    char source_pages[192];
    char encounter_slug[64];
    uint8_t status;
    uint8_t flags;
    uint16_t npc_count;
    uint16_t section_count;
    uint64_t behavior_bits;
    uint16_t profile_id;
    uint32_t npc_ids[RC_ACTIVITY_MECH_MAX_NPC_IDS];
    RcActivityMechanicSection sections[RC_ACTIVITY_MECH_MAX_SECTIONS];
} RcActivityMechanic;

typedef struct {
    RcActivityMechanic rows[RC_ACTIVITY_MECH_MAX_ROWS];
    int count;
    uint64_t behavior_by_npc[RC_MAX_NPC_ID];
    uint16_t profile_by_npc[RC_MAX_NPC_ID];
    int index_built;
} RcActivityMechanicData;

extern RcActivityMechanic
    g_rc_activity_mechanics[RC_ACTIVITY_MECH_MAX_ROWS];
extern int g_rc_activity_mechanic_count;

int rc_load_activity_mechanics(const char *path);
void rc_activity_mechanic_data_init(RcActivityMechanicData *data);
void rc_activity_mechanic_data_free(RcActivityMechanicData *data);
int rc_activity_mechanic_data_import_globals(RcActivityMechanicData *data);
int rc_load_activity_mechanics_into(const char *path,
                                    RcActivityMechanicData *data);
int rc_activity_mechanics_mirror_to_globals(
    const RcActivityMechanicData *data);
void rc_activity_mechanics_use_data(const RcActivityMechanicData *data);
void rc_activity_mechanics_reset_data_if_active(
    const RcActivityMechanicData *data);
void rc_activity_mechanics_rebuild_index(void);
int rc_activity_mechanics_find_slug(const char *slug);
uint64_t rc_activity_mechanics_behavior_for_npc(uint32_t npc_id);
uint64_t rc_activity_mechanics_behavior_for_owner(const char *owner);
uint16_t rc_activity_mechanics_profile_for_npc(uint32_t npc_id);
bool rc_activity_mechanics_has_npc(int idx, uint32_t npc_id);

#endif
