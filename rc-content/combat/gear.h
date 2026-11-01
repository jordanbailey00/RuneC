#ifndef RC_CONTENT_GEAR_H
#define RC_CONTENT_GEAR_H

#include "combat.h"
#include "rng.h"
#include "spells.h"
#include "gear_enums.inc"

#define RC_GEAR_ID_LIMIT 34800

// Uniform int in [0, bound); rc_rng_range's bound is inclusive.
static inline int rc_content_roll(uint32_t *rng, int bound) {
    return bound > 0 ? (int)rc_rng_bounded(rng, (uint32_t)bound) : 0;
}

// Enchanted bolt procs that take effect when the hit lands.
#define GEAR_HIT_BIND RC_HIT_FLAG_CONTENT_0
#define GEAR_HIT_POISON RC_HIT_FLAG_CONTENT_1
// Melee specials reuse the bits: a hit that burns, and the repeat of a double hit.
#define GEAR_HIT_BURN RC_HIT_FLAG_CONTENT_0
#define GEAR_HIT_REPEAT RC_HIT_FLAG_CONTENT_1

// NPC attributes (content/combat/npc_attributes.inc).
enum {
    RC_NPC_ATTR_UNDEAD = 1u << 0,
    RC_NPC_ATTR_DEMON = 1u << 1,
    RC_NPC_ATTR_DRAGON = 1u << 2,
    RC_NPC_ATTR_KALPHITE = 1u << 3,
    RC_NPC_ATTR_VAMPYRE_1 = 1u << 4,
    RC_NPC_ATTR_VAMPYRE_2 = 1u << 5,
    RC_NPC_ATTR_VAMPYRE_3 = 1u << 6,
    RC_NPC_ATTR_GOLEM = 1u << 7,
    RC_NPC_ATTR_LEAFY = 1u << 8,
    RC_NPC_ATTR_RAT = 1u << 9,
    RC_NPC_ATTR_SHADE = 1u << 10,
    RC_NPC_ATTR_SPECTRAL = 1u << 11,
    RC_NPC_ATTR_FIERY = 1u << 12,
    RC_NPC_ATTR_XERICIAN = 1u << 13,
    RC_NPC_ATTR_PENANCE = 1u << 14,
    RC_NPC_ATTR_FLYING = 1u << 15,
    RC_NPC_ATTR_REVENANT = 1u << 16,
    RC_NPC_ATTR_TOA = 1u << 17,
    RC_NPC_ATTR_SLAYER = 1u << 18,
    RC_NPC_ATTR_BURN_IMMUNE = 1u << 19,
    RC_NPC_ATTR_STRONG_BURN_IMMUNE = 1u << 20,
    RC_NPC_ATTR_UNDERWATER = 1u << 21,
    RC_NPC_ATTR_MAGIC_BY_DEFENCE = 1u << 22,  // Defence level rolls against magic
};
#define RC_NPC_ATTR_VAMPYRE \
    (RC_NPC_ATTR_VAMPYRE_1 | RC_NPC_ATTR_VAMPYRE_2 | RC_NPC_ATTR_VAMPYRE_3)

// Slots of RcPlayer.content_gear; zero is the rest state of each.
enum {
    RC_GEAR_STATE_SOUL_STACKS,          // Soulreaper axe
    RC_GEAR_STATE_MELEE_WARD_UNTIL,     // Power of Death
    RC_GEAR_STATE_FLOW_ATTACKS,         // Zorya's flow buffed attacks left
    RC_GEAR_STATE_FLOW_UNTIL,
    RC_GEAR_STATE_RONDACHE_CHARGES,
    RC_GEAR_STATE_RONDACHE_NEXT_GAIN,
    RC_GEAR_STATE_RONDACHE_DECAY_TICK,
    RC_GEAR_STATE_ASCENSION_SIDE,       // which crossbow fired last
    RC_GEAR_STATE_NEXT_DAMAGE_BONUS,    // Virulence
    RC_GEAR_STATE_IMMUNE_UNTIL,         // Spear Wall
    RC_GEAR_STATE_ATTACK_SPEED_CUT,     // next attack comes this much sooner
    RC_GEAR_STATE_ECHO_OFF,             // Echo boots recoil toggled off
    RC_GEAR_STATE_RONDACHE_ARMED,       // attacked since equipping the shield
    RC_GEAR_STATE_SHATTER_STACKS,       // Lithic sceptre
    RC_GEAR_STATE_SHATTER_TICK,         // tick of the last gained stack
    RC_GEAR_STATE_SOUL_TICK,            // tick of the last gained Soul stack
    RC_GEAR_STATE_WRATH_TARGET,         // uid + 1 under Wrath of Amascut
    RC_GEAR_STATE_WRATH_UNTIL,
    RC_GEAR_STATE_COUNT,
};

enum { RC_WEAKNESS_NONE, RC_WEAKNESS_FIRE, RC_WEAKNESS_WATER, RC_WEAKNESS_EARTH, RC_WEAKNESS_AIR };

// content/combat/npc_attributes.inc. Ranged bonuses are all zero when they
// don't differ from the NPC's ordinary ranged defence.
typedef struct {
    int id;
    uint32_t attributes;
    int8_t weakness;       // RC_WEAKNESS_*
    uint8_t severity;      // percent
    int16_t light, standard, heavy;
    int16_t flat_armour;
    int16_t defence_floor;
} RcNpcCombatRow;

typedef struct {
    uint16_t family;
    uint32_t flags;
    uint8_t ammo_kind;
    uint8_t ammo_tier;
    uint8_t special;
    uint8_t special_cost;   // percent of the bar
    uint8_t special_variant; // 1 = Bounty Hunter rules
} RcGearRow;

const RcGearRow *rc_content_gear_row(int item_id);
const RcNpcCombatRow *rc_content_npc_row(const RcWorld *world, const RcNpc *npc);
uint32_t rc_content_npc_attributes(const RcWorld *world, const RcNpc *npc);
bool rc_content_wearing(const RcPlayer *player, int family);

// Everything an attack needs to know about what the player wears and hits.
typedef struct {
    RcWorld *world;
    RcPlayer *p;
    const RcNpc *npc;
    const RcSpellDef *spell;
    uint32_t attrs;
    const RcNpcCombatRow *npc_row;  // NULL when the NPC has no row
    int size;
    RcCombatStyle style;
    bool special;
    bool on_task;
    bool in_wilderness;
    bool uses_ammo_slot;
    int weapon_id;
    const RcGearRow *weapon;
    uint16_t fam[RC_EQUIP_COUNT];
    unsigned flags;
    int min_hit;
} RcGearAttack;

void rc_content_gear_attack(RcGearAttack *a, RcWorld *world, const RcNpc *npc,
                            const RcSpellDef *spell, bool special);
// Replace the rolls of a melee or ranged attack with the full formula.
void rc_content_gear_melee(RcGearAttack *a, RcCombatCalc *calc);
void rc_content_gear_ranged(RcGearAttack *a, RcCombatCalc *calc);
// `base` is the spell or powered staff max hit before gear.
void rc_content_gear_magic(RcGearAttack *a, RcCombatCalc *calc, int base);
// Style immunity from the target's attributes (vampyre tiers, leafy, rats).
void rc_content_gear_spell_floor(RcGearAttack *a, const RcCombatCalc *calc);
bool rc_content_gear_blocked(const RcGearAttack *a);
// Damage rules that act on each rolled hit.
int rc_content_gear_after_roll(const RcGearAttack *a, const RcCombatCalc *calc,
                               int damage, bool accurate);
// Multi-hit passives: Scythe, Dual macuahuitl, Torag's hammers, Dark bow and
// the rest. Returns the hit count, 0 for one ordinary roll.
int rc_content_gear_weapon_hits(RcGearAttack *a, RcCombatCalc *calc,
                                RcPendingHit *hits, int capacity);
RcCombatRoll rc_content_gear_roll(RcGearAttack *a, const RcCombatCalc *calc);
// Bolt effects and per-attack state, once every hit of the attack is rolled.
void rc_content_gear_after_hits(RcGearAttack *a, const RcCombatCalc *calc,
                                RcPendingHit *hits, int count);
// Damage the player takes after protection prayers, and what taking it does.
void rc_content_gear_bind(RcWorld *world, RcNpc *npc, int ticks);
int rc_content_edge_distance(const RcPlayer *player, int x, int y, int size);
int rc_content_rondache_charges(const RcWorld *world);
int rc_content_shatter_stacks(const RcWorld *world);
int rc_content_soul_stacks(const RcWorld *world);
int rc_content_gear_split_hits(RcGearAttack *a, RcCombatCalc *calc, RcPendingHit *hits, bool chained);
int rc_content_gear_incoming(RcWorld *world, const RcPendingHit *hit, int damage);
void rc_content_gear_after_hit_taken(RcWorld *world, const RcPendingHit *hit, int damage);
// Effects of gear when a player hit lands.
void rc_content_gear_landed(RcWorld *world, RcNpc *target, const RcPendingHit *hit, int damage);
bool rc_content_special_is_ward(int weapon_id);

#endif
