#include "content.h"
#include "consumables.h"
#include "items.h"
#include "raids/cox.h"

// Reviewed B237 identities; no name parsing or arithmetic between dose IDs.
// Evidence, supported families and exclusions: docs/consumables.md.
#define FOOD(id, hp) {.item_id=id, .replacement_id=-1, .heal=hp, .eat_delay=3, .attack_delay=3}
#define BITE(id, next, hp, delay) {.item_id=id, .replacement_id=next, .heal=hp, .eat_delay=delay, .attack_delay=3}
#define POT(id, next, ...) {.item_id=id, .replacement_id=next, .kind=RC_CONSUME_DRINK, .eat_delay=3, __VA_ARGS__}
#define P4(a,b,c,d,...) POT(a,b,__VA_ARGS__), POT(b,c,__VA_ARGS__), POT(c,d,__VA_ARGS__), POT(d,229,__VA_ARGS__)
#define BOOST(s,p,f) [s]={RC_STAT_BOOST,p,f}
#define RESTORE(s,p,f) [s]={RC_STAT_RESTORE,p,f}
#define DRAIN(s,p,f) [s]={RC_STAT_DRAIN,p,f}
#define SUPER_MELEE BOOST(SKILL_ATTACK,15,5), BOOST(SKILL_STRENGTH,15,5), BOOST(SKILL_DEFENCE,15,5)
#define RESTORE_COMBAT(p,f) RESTORE(SKILL_ATTACK,p,f), RESTORE(SKILL_STRENGTH,p,f), RESTORE(SKILL_DEFENCE,p,f), RESTORE(SKILL_RANGED,p,f), RESTORE(SKILL_MAGIC,p,f)
// Chambers of Xeric gourd potions: doses (1)..(4) are consecutive ids and the
// last dose breaks the gourd. Tier boosts are (-) 4 + 10%, regular 5 + 13% and
// (+) 6 + 16% (OSRS Wiki potion pages, RuneLite item stats).
#define GOURD(d1, ...) POT((d1)+3,(d1)+2,__VA_ARGS__), POT((d1)+2,(d1)+1,__VA_ARGS__), \
    POT((d1)+1,(d1),__VA_ARGS__), POT((d1),-1,__VA_ARGS__)
#define TIER_BOOST(s,t) BOOST(s,(t)==0?10:(t)==1?13:16,(t)+4)
#define RESTORE_EVERY(p,f) RESTORE_COMBAT(p,f), RESTORE(SKILL_COOKING,p,f), RESTORE(SKILL_WOODCUTTING,p,f), RESTORE(SKILL_FLETCHING,p,f), RESTORE(SKILL_FISHING,p,f), RESTORE(SKILL_FIREMAKING,p,f), RESTORE(SKILL_CRAFTING,p,f), RESTORE(SKILL_SMITHING,p,f), RESTORE(SKILL_MINING,p,f), RESTORE(SKILL_HERBLORE,p,f), RESTORE(SKILL_AGILITY,p,f), RESTORE(SKILL_THIEVING,p,f), RESTORE(SKILL_SLAYER,p,f), RESTORE(SKILL_FARMING,p,f), RESTORE(SKILL_RUNECRAFT,p,f), RESTORE(SKILL_HUNTER,p,f), RESTORE(SKILL_CONSTRUCTION,p,f)
#define XERICS_AID(hp,hpp,dp,df,rp,rf) .heal=hp, .heal_percent=hpp, .overheal=1, \
    .stats={BOOST(SKILL_DEFENCE,dp,df), DRAIN(SKILL_ATTACK,rp,rf), \
        DRAIN(SKILL_STRENGTH,rp,rf), DRAIN(SKILL_RANGED,rp,rf), DRAIN(SKILL_MAGIC,rp,rf)}
#define COX_TIERS(line, macro) macro(line, 0), macro(line, 1), macro(line, 2)
#define COX_ID(line, tier) (20913 + (line) * 12 + (tier) * 4)
#define ELDER(line, t) GOURD(COX_ID(line, t), .stats={TIER_BOOST(SKILL_ATTACK,t), \
    TIER_BOOST(SKILL_STRENGTH,t), TIER_BOOST(SKILL_DEFENCE,t)})
#define TWISTED(line, t) GOURD(COX_ID(line, t), .stats={TIER_BOOST(SKILL_RANGED,t)})
#define KODAI(line, t) GOURD(COX_ID(line, t), .stats={TIER_BOOST(SKILL_MAGIC,t)})
// Revitalisation (+) restores 11 + 30% (wiki); the lower tiers are
// unpublished (taken as 9 + 20% and 10 + 25%).
#define REVITALISATION(line, t) GOURD(COX_ID(line, t), .prayer_flat=9+(t), \
    .prayer_percent=20+5*(t), .stats={RESTORE_EVERY(20+5*(t), 9+(t))})
#define PRAYER_ENHANCE(line, t) GOURD(COX_ID(line, t), \
    .effect=COX_EFFECT_PRAYER_ENHANCE+(t))
#define OVERLOAD(line, t) GOURD(COX_ID(line, t), .effect=COX_EFFECT_OVERLOAD+(t), \
    .min_hp=50)
#define RESTORE_ALL RESTORE_COMBAT(25,8), RESTORE(SKILL_COOKING,25,8), RESTORE(SKILL_WOODCUTTING,25,8), RESTORE(SKILL_FLETCHING,25,8), RESTORE(SKILL_FISHING,25,8), RESTORE(SKILL_FIREMAKING,25,8), RESTORE(SKILL_CRAFTING,25,8), RESTORE(SKILL_SMITHING,25,8), RESTORE(SKILL_MINING,25,8), RESTORE(SKILL_HERBLORE,25,8), RESTORE(SKILL_AGILITY,25,8), RESTORE(SKILL_THIEVING,25,8), RESTORE(SKILL_SLAYER,25,8), RESTORE(SKILL_FARMING,25,8), RESTORE(SKILL_RUNECRAFT,25,8), RESTORE(SKILL_HUNTER,25,8), RESTORE(SKILL_CONSTRUCTION,25,8)

static const RcConsumableDef definitions[] = {
    FOOD(315,3), FOOD(319,1), FOOD(325,4), FOOD(329,9), FOOD(333,7),
    FOOD(339,7), FOOD(347,5), FOOD(351,8), FOOD(355,6), FOOD(361,10),
    FOOD(365,13), FOOD(373,14), FOOD(379,12), FOOD(385,20), FOOD(391,22),
    FOOD(397,21), FOOD(2140,3), FOOD(2142,3), FOOD(2149,14), FOOD(2309,5),
    FOOD(1965,1), FOOD(1963,2), FOOD(1942,1), FOOD(1957,2), FOOD(1985,1),
    FOOD(1961,1), FOOD(2343,14), FOOD(2878,10), FOOD(3228,5),
    FOOD(6701,16), FOOD(6703,20), FOOD(7060,22), FOOD(7946,16),
    FOOD(11936,22), FOOD(6883,8),
    {.item_id=3144, .replacement_id=-1, .kind=RC_CONSUME_COMBO,
        .eat_delay=3, .attack_delay=2, .heal=18},
    {.item_id=13441, .replacement_id=-1, .eat_delay=3, .attack_delay=3,
        .angler=1, .overheal=1},
    BITE(1891,1893,4,2), BITE(1893,1895,4,2), BITE(1895,-1,4,3),
    BITE(1897,1899,5,2), BITE(1899,1901,5,2), BITE(1901,-1,5,3),
    BITE(2289,2291,7,1), BITE(2291,-1,7,2),
    BITE(2293,2295,8,1), BITE(2295,-1,8,2),
    BITE(2297,2299,9,1), BITE(2299,-1,9,2),
    BITE(2301,2303,11,1), BITE(2303,-1,11,2),
    BITE(2323,2335,7,1), BITE(2335,2313,7,2),
    BITE(2325,2333,5,1), BITE(2333,2313,5,2),
    BITE(2327,2331,6,1), BITE(2331,2313,6,2),
    {.item_id=7218, .replacement_id=7220, .heal=11, .eat_delay=1, .attack_delay=3,
        .energy=10, .stats={BOOST(SKILL_AGILITY,0,5)}},
    {.item_id=7220, .replacement_id=2313, .heal=11, .eat_delay=1, .attack_delay=3,
        .energy=10, .stats={BOOST(SKILL_AGILITY,0,5)}},
    {.item_id=7208, .replacement_id=7210, .heal=11, .eat_delay=1, .attack_delay=3,
        .stats={BOOST(SKILL_SLAYER,0,5), BOOST(SKILL_RANGED,0,4)}},
    {.item_id=7210, .replacement_id=2313, .heal=11, .eat_delay=1, .attack_delay=3,
        .stats={BOOST(SKILL_SLAYER,0,5), BOOST(SKILL_RANGED,0,4)}},
    {.item_id=29140, .replacement_id=-1, .heal=12, .eat_delay=3,
        .attack_delay=3, .delayed_heal=9},
    {.item_id=29134, .replacement_id=-1, .heal=13, .eat_delay=3,
        .attack_delay=3, .delayed_heal=10, .delayed_energy=10},
    {.item_id=29143, .replacement_id=-1, .heal=14, .eat_delay=3,
        .attack_delay=3, .delayed_heal=12, .delayed_cure=1},
    P4(2428,121,123,125, .stats={BOOST(SKILL_ATTACK,10,3)}),
    P4(113,115,117,119, .stats={BOOST(SKILL_STRENGTH,10,3)}),
    P4(2432,133,135,137, .stats={BOOST(SKILL_DEFENCE,10,3)}),
    P4(2436,145,147,149, .stats={BOOST(SKILL_ATTACK,15,5)}),
    P4(2440,157,159,161, .stats={BOOST(SKILL_STRENGTH,15,5)}),
    P4(2442,163,165,167, .stats={BOOST(SKILL_DEFENCE,15,5)}),
    P4(2444,169,171,173, .stats={BOOST(SKILL_RANGED,10,4)}),
    P4(3040,3042,3044,3046, .stats={BOOST(SKILL_MAGIC,0,4)}),
    P4(2438,151,153,155, .stats={BOOST(SKILL_FISHING,0,3)}),
    P4(3032,3034,3036,3038, .stats={BOOST(SKILL_AGILITY,0,3)}),
    P4(9998,10000,10002,10004, .stats={BOOST(SKILL_HUNTER,0,3)}),
    P4(9739,9741,9743,9745, .stats={BOOST(SKILL_ATTACK,10,3), BOOST(SKILL_STRENGTH,10,3)}),
    P4(12695,12697,12699,12701, .stats={SUPER_MELEE}),
    P4(22449,22452,22455,22458, .stats={BOOST(SKILL_MAGIC,0,4), BOOST(SKILL_DEFENCE,15,5)}),
    P4(22461,22464,22467,22470, .stats={BOOST(SKILL_RANGED,10,4), BOOST(SKILL_DEFENCE,15,5)}),
    P4(2430,127,129,131, .stats={RESTORE_COMBAT(30,10)}),
    P4(3024,3026,3028,3030, .prayer_flat=8, .prayer_percent=25, .stats={RESTORE_ALL}),
    P4(2434,139,141,143, .prayer_flat=7, .prayer_percent=25),
    P4(6685,6687,6689,6691, .heal=2, .heal_percent=15, .overheal=1,
        .stats={BOOST(SKILL_DEFENCE,20,2), DRAIN(SKILL_ATTACK,10,2),
            DRAIN(SKILL_STRENGTH,10,2), DRAIN(SKILL_RANGED,10,2), DRAIN(SKILL_MAGIC,10,2)}),
    P4(3008,3010,3012,3014, .energy=10),
    P4(3016,3018,3020,3022, .energy=20),
    P4(12625,12627,12629,12631, .energy=20, .stamina_ticks=200),
    P4(2446,175,177,179, .cure=1, .poison_immunity=150),
    P4(2448,181,183,185, .cure=1, .poison_immunity=600),
    P4(12913,12915,12917,12919, .cure=2, .poison_immunity=1500, .venom_immunity=360),
    P4(2452,2454,2456,2458, .antifire_ticks=600),
    P4(11951,11953,11955,11957, .antifire_ticks=1200),
    P4(21978,21981,21984,21987, .super_antifire_ticks=300),
    P4(22209,22212,22215,22218, .super_antifire_ticks=600),
    P4(23685,23688,23691,23694, .divine_ticks=500, .stats={SUPER_MELEE}),
    P4(23697,23700,23703,23706, .divine_ticks=500, .stats={BOOST(SKILL_ATTACK,15,5)}),
    P4(23709,23712,23715,23718, .divine_ticks=500, .stats={BOOST(SKILL_STRENGTH,15,5)}),
    P4(23721,23724,23727,23730, .divine_ticks=500, .stats={BOOST(SKILL_DEFENCE,15,5)}),
    P4(23733,23736,23739,23742, .divine_ticks=500, .stats={BOOST(SKILL_RANGED,10,4)}),
    P4(23745,23748,23751,23754, .divine_ticks=500, .stats={BOOST(SKILL_MAGIC,0,4)}),
    P4(24623,24626,24629,24632, .divine_ticks=500,
        .stats={BOOST(SKILL_MAGIC,0,4), BOOST(SKILL_DEFENCE,15,5)}),
    P4(24635,24638,24641,24644, .divine_ticks=500,
        .stats={BOOST(SKILL_RANGED,10,4), BOOST(SKILL_DEFENCE,15,5)}),
    // Chambers of Xeric raid food: cooked tiers 0..6 heal 5 + 3 per tier.
    FOOD(20856,5), FOOD(20858,8), FOOD(20860,11), FOOD(20862,14),
    FOOD(20864,17), FOOD(20866,20), FOOD(20868,23),
    FOOD(20871,5), FOOD(20873,8), FOOD(20875,11), FOOD(20877,14),
    FOOD(20879,17), FOOD(20881,20), FOOD(20883,23),
    COX_TIERS(0, ELDER), COX_TIERS(1, TWISTED), COX_TIERS(2, KODAI),
    COX_TIERS(3, REVITALISATION), COX_TIERS(4, PRAYER_ENHANCE),
    GOURD(COX_ID(5, 0), XERICS_AID(1, 7, 14, 1, 7, 1)),
    GOURD(COX_ID(5, 1), XERICS_AID(2, 12, 18, 2, 9, 2)),
    GOURD(COX_ID(5, 2), XERICS_AID(5, 15, 20, 5, 10, 4)),
    COX_TIERS(6, OVERLOAD),
    GOURD(25754, .cure=1, .poison_immunity=150),
    GOURD(25758, .cure=1, .poison_immunity=600),
    GOURD(25762, .cure=1, .poison_immunity=900),
};

static int prayer_cape(int id) {
    return id == 9759 || id == 9760 || id == 13280 || id == 13342;
}

static int prayer_bonus(const RcPlayer *p) {
    int ring = p->equipment[EQUIP_RING].item_id;
    if (ring == 13202 || ring == 25252 || ring == 26764
            || prayer_cape(p->equipment[EQUIP_CAPE].item_id)) return 2;
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        if (p->inventory[i].quantity > 0 && (p->inventory[i].item_id == 6714
                || prayer_cape(p->inventory[i].item_id))) return 2;
    }
    return 0;
}

int rc_content_consumables_register(RcWorld *world) {
    if (!world) return 0;
    if (!(world->enabled & RC_SUB_CONSUMABLES)) return 1;
    world->consumable_prayer_bonus = prayer_bonus;
    if (!rc_consumables_register(world, definitions,
            (int)(sizeof(definitions) / sizeof(definitions[0])))) {
        world->consumables = NULL;
        world->consumable_count = 0;
        return 0;
    }
    return 1;
}
