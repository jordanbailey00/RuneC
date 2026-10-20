#include "dragonfire.h"
#include "../../rc-core/prayer.h"
#include "../../rc-core/types.h"

// Encounter dragonfire tables (OSRS Wiki "Dragonfire", fixed revisions in
// docs/osrs_encounter_oracle.md). Rows: no potion, antifire, super
// antifire. Columns: none, shield, Protect from Magic, both. Vorkath has a
// separate maximum when the player resists (wins the magic accuracy roll).
static const uint8_t KBD_FIERY[3][4] = {
    {65, 15, 20, 15}, {50, 0, 5, 0}, {0, 0, 0, 0},
};
static const uint8_t KBD_SPECIAL[3][4] = {
    {50, 10, 15, 10}, {50, 10, 15, 10}, {50, 10, 15, 10},
};
static const uint8_t VORKATH_BURNT[3][4] = {
    {80, 20, 30, 20}, {70, 10, 20, 10}, {60, 0, 10, 0},
};
static const uint8_t VORKATH_RESISTED[3][4] = {
    {50, 20, 30, 20}, {40, 10, 20, 10}, {30, 0, 10, 0},
};

static bool dragonfire_shield(const RcPlayer *p) {
    switch (p->equipment[EQUIP_SHIELD].item_id) {
        case 1540: case 8282:     // Anti-dragon shield
        case 11283: case 11284:   // Dragonfire shield
        case 22002: case 22003:   // Dragonfire ward
        case 21633: case 21634:   // Ancient wyvern shield
            return true;
        default:
            return false;
    }
}

int rc_content_dragonfire_max_hit(const RcWorld *world, const RcNpc *npc,
                                  uint8_t breath, bool resisted,
                                  bool *shielded) {
    (void)npc;
    if (!world) return -1;
    const RcPlayer *p = &world->player;
    bool shield = dragonfire_shield(p);
    bool prayer = (p->active_prayers & PRAYER_PROTECT_MAGIC) != 0;
    int potion = world->tick < p->super_antifire_until ? 2
               : world->tick < p->antifire_until ? 1 : 0;
    int column = (shield ? 1 : 0) + (prayer ? 2 : 0);
    if (shielded) *shielded = shield || prayer;
    switch (breath) {
        case RC_DRAGONFIRE_KBD_FIERY:
            return KBD_FIERY[potion][column];
        case RC_DRAGONFIRE_KBD_SPECIAL:
            return KBD_SPECIAL[potion][column];
        case RC_DRAGONFIRE_VORKATH:
        case RC_DRAGONFIRE_VORKATH_COLOURED: {
            int max = resisted ? VORKATH_RESISTED[potion][column]
                               : VORKATH_BURNT[potion][column];
            // Venomous and corrupting breaths hit 5 lower.
            if (breath == RC_DRAGONFIRE_VORKATH_COLOURED)
                max = max > 5 ? max - 5 : 0;
            return max;
        }
        default:
            return -1;
    }
}
