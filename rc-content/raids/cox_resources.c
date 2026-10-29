#include "cox.h"

#include "../../rc-core/api.h"
#include "../../rc-core/combat.h"
#include "../../rc-core/consumables.h"
#include "../../rc-core/interaction.h"
#include "../../rc-core/items.h"
#include "../../rc-core/object_runtime.h"
#include "../../rc-core/prayer.h"
#include "../../rc-core/skills.h"

#include <stdio.h>
#include <string.h>

// Raid resources: the skilling action loop, scavenger rooms (beasts and
// their shortcut), resource (farming) rooms, weeds, cooking, herblore, raid
// potion effects, tool spawns and storage-unit building.

enum {
    ITEM_KINDLING = 20799,
    ITEM_GOURD_EMPTY = 20800,
    ITEM_GOURD_WATER = 20801,
    ITEM_CAVE_WORMS = 20853,
    ITEM_FISH_BURNT = 20854,
    ITEM_FISH_RAW = 20855,          // + 2 * tier; cooked = raw + 1
    ITEM_BAT_BURNT = 20869,
    ITEM_BAT_RAW = 20870,           // + 2 * tier; cooked = raw + 1
    ITEM_GRIMY_NOXIFER = 20901,
    ITEM_NOXIFER = 20902,
    ITEM_SEED_NOXIFER = 20903,
    ITEM_GRIMY_GOLPAR = 20904,
    ITEM_GOLPAR = 20905,
    ITEM_SEED_GOLPAR = 20906,
    ITEM_GRIMY_BUCHU = 20907,
    ITEM_BUCHU = 20908,
    ITEM_SEED_BUCHU = 20909,
    ITEM_STINKHORN = 20910,
    ITEM_ENDARKENED_JUICE = 20911,
    ITEM_CICELY = 20912,
    ITEM_POTIONS = 20913,           // + line * 12 + tier * 4 + dose - 1
    ITEM_ANTIPOISON = 25754,        // + tier * 4 + dose - 1
    ITEM_PLANK = 21036,
    ITEM_BONES = 526,
    ITEM_RAKE = 5341,
    ITEM_SPADE = 952,
    ITEM_SEED_DIBBER = 5343,
    ITEM_FISHING_ROD = 307,
    ITEM_PEARL_ROD = 22846,
    ITEM_IRON_PICKAXE = 1267,
    ITEM_IRON_AXE = 1349,
    ITEM_BRONZE_AXE = 1351,
    ITEM_NET = 10010,
    ITEM_MAGIC_NET = 11259,
    ITEM_HAMMER = 2347,
    ITEM_TINDERBOX = 590,
    ITEM_LOCKPICK = 1523,
};

enum { LINE_ELDER, LINE_TWISTED, LINE_KODAI, LINE_REVITALISATION,
       LINE_PRAYER_ENHANCE, LINE_XERICS_AID, LINE_OVERLOAD };

enum {
    LOC_SAPLING = 29763,
    LOC_STUMP = 29764,
    LOC_PATCH = 29765,
    LOC_PATCH_STAGE = 29997,        // + stage * 3 + herb (nox, golpar, buchu)
    LOC_STORAGE_TINY = 29769,
    LOC_STORAGE_SMALL = 29770,
    LOC_STORAGE_MEDIUM = 29779,
    LOC_STORAGE_LARGE = 29780,
    LOC_STORAGE_MASSIVE = 37978,
    LOC_OLD_TOOLS = 29771,
    LOC_GOURD_TREE = 29772,
    LOC_WEEDS = 29773,
    LOC_GEYSER = 29878,
    LOC_BUBBLES = 29889,
    LOC_SHORTCUT_SAPLING = 29736,
    LOC_SHORTCUT_STUMP = 29737,
    LOC_SHORTCUT_ROCKS = 29738,
    LOC_SHORTCUT_RUBBLE = 29739,
    LOC_SHORTCUT_BOULDER = 29740,
    LOC_TINDERBOX = 31634,
    LOC_BRONZE_AXE = 31862,
    LOC_HAMMER = 29711,
    LOC_PICKAXE = 41754,
    LOC_FIRE = 26185,
    LOC_TEKTON_FIRE = 30021,
    LOC_VASA_FIRE = 30019,
    LOC_BRAZIER_LIT = 29748,
};

enum { NPC_SCAVENGER = 7548, NPC_BAT = 7587 };   // bats 7587..7593 by tier

static const int g_axes[] = {1351, 1349, 1353, 1361, 1355, 1357, 1359, 6739,
                             13241, 23673, 20011, 25110, 25066, 25378, 28196};
static const int g_pickaxes[] = {1265, 1267, 1269, 12297, 1273, 1271, 1275,
                                 11920, 12797, 13243, 23677, 20014, 25063,
                                 25376, 28217};
static const int g_hammers[] = {2347, 13576, 21003};

// ---------------------------------------------------------------------------
// Shared helpers.

bool cox_skill_roll(RcWorld *world, int level, int low, int high) {
    if (level < 1) level = 1;
    if (level > 99) level = 99;
    int chance = 1 + (low * (99 - level) + high * (level - 1)) / 98;
    return cox_rand(world, 256) < chance;
}

void cox_add_xp(RcWorld *world, int skill, int xp_hundredths) {
    rc_add_xp_hundredths(&world->player.skills, (RcSkill)skill, xp_hundredths);
}

bool cox_give_item(RcWorld *world, int item_id, int quantity) {
    return rc_player_inventory_add(world, item_id, quantity, 0).code
        == RC_ITEM_RESULT_OK;
}

bool cox_take_item(RcWorld *world, int item_id, int quantity) {
    return rc_player_inventory_remove_item(world, item_id, quantity).code
        == RC_ITEM_RESULT_OK;
}

// Swaps up to `max` unstackable `from` items for `to`, slot by slot, in one
// transaction. Returns how many swapped.
int cox_swap_items(RcWorld *world, int from, int to, int max) {
    RcItemTransaction tx;
    if (rc_item_tx_begin(&tx, world).code != RC_ITEM_RESULT_OK) return 0;
    int n = 0;
    for (int i = 0; i < RC_INVENTORY_SIZE && n < max; i++) {
        if (tx.inventory[i].item_id != from) continue;
        tx.inventory[i] = (RcInvSlot){.item_id = to, .quantity = 1};
        n++;
    }
    return n && rc_item_tx_commit(&tx).code == RC_ITEM_RESULT_OK ? n : 0;
}

static int level(const RcWorld *world, int skill) {
    return world->player.skills.boosted_level[skill];
}

static int free_slots(const RcPlayer *p) {
    int n = 0;
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) n += p->inventory[i].item_id < 0;
    return n;
}

static RcInteractionHandlerResult say(const char *text) {
    return rc_interaction_result_message(text);
}

static CoxRaid *raid_or_null(RcWorld *world) {
    CoxRaid *raid = cox_raid(world);
    return raid && raid->in_raid ? raid : NULL;
}

void cox_start_action(RcWorld *world, CoxRaid *raid, int kind,
                      const RcPendingInteraction *pending, int delay,
                      int param) {
    const RcPlayer *p = &world->player;
    CoxAction *a = &raid->action;
    memset(a, 0, sizeof(*a));
    a->kind = (uint8_t)kind;
    if (pending) {
        a->target_x = pending->target.tile_x;
        a->target_y = pending->target.tile_y;
        a->plane = pending->target.plane;
        a->target_key = pending->target.placement_key;
        a->target_id = pending->target.kind == RC_INTERACTION_NPC
                     ? pending->target.entity_uid : pending->target.definition_id;
    }
    a->param = param;
    a->next_tick = world->tick + (RcTick)delay;
    a->started_tick = world->tick;
    a->player_x = p->x;
    a->player_y = p->y;
    a->interaction_generation = p->next_interaction_generation;
}

static bool resource_action(RcWorld *world, CoxRaid *raid, CoxAction *a);

void cox_tick_action(RcWorld *world, CoxRaid *raid) {
    CoxAction *a = &raid->action;
    if (!a->kind) return;
    const RcPlayer *p = &world->player;
    if (p->is_dead || p->x != a->player_x || p->y != a->player_y
            || p->next_interaction_generation != a->interaction_generation
            || p->attack_target >= 0) {
        a->kind = 0;
        return;
    }
    if (world->tick < a->next_tick) return;
    bool more;
    switch (a->kind) {
    case COX_ACT_LIGHT_BRAZIER:
    case COX_ACT_OPEN_CHEST:
    case COX_ACT_CROSS_ROPE:
        more = cox_puzzle_action(world, raid, a);
        break;
    case COX_ACT_CUT_MEAT_TREE:
        more = cox_combat_action(world, raid, a);
        break;
    default:
        more = resource_action(world, raid, a);
        break;
    }
    if (!more) a->kind = 0;
}

// ---------------------------------------------------------------------------
// Woodcutting: saplings give kindling.

bool cox_chop_sapling(RcWorld *world, CoxRaid *raid, CoxAction *a) {
    (void)raid;
    RcPlayer *p = &world->player;
    if (!cox_has_any(p, g_axes, (int)(sizeof(g_axes) / sizeof(g_axes[0])))) {
        cox_message(world, "You need an axe to chop this sapling.");
        return false;
    }
    if (rc_world_object_active_id(world, LOC_SAPLING, a->target_x, a->target_y,
                                  a->plane) != LOC_SAPLING)
        return false;
    if (cox_inventory_count(p, ITEM_KINDLING) >= 28) {
        cox_message(world, "You can't carry any more kindling.");
        return false;
    }
    a->next_tick = world->tick + 4;
    int wc = level(world, SKILL_WOODCUTTING);
    if (!cox_skill_roll(world, wc, 64, 200)) return true;
    // Mod Ash: 0..floor(level / 12) kindling, a 0 counts as 1.
    int got = cox_rand(world, wc / 12 + 1);
    if (got < 1) got = 1;
    int room = 28 - cox_inventory_count(p, ITEM_KINDLING);
    if (got > room) got = room;
    static const int xp[9] = {0, 3000, 4500, 5500, 6250, 6850, 7350, 7770, 8150};
    if (!cox_give_item(world, ITEM_KINDLING, got)) return false;
    cox_add_xp(world, SKILL_WOODCUTTING, xp[got > 8 ? 8 : got]);
    cox_message(world, "You get some kindling.");
    // The sapling falls 1 in 5 * max(3, 2 * party size) chops.
    if (cox_rand(world, 15) == 0) {
        cox_replace_loc(world, LOC_SAPLING, a->target_x, a->target_y, a->plane,
                        LOC_STUMP);
        rc_world_object_expire(world, a->target_key, world->tick + 50);
        return false;
    }
    return true;
}

static RcInteractionHandlerResult chop_sapling(RcWorld *world, RcPlayer *player,
                                               const RcPendingInteraction *pending,
                                               void *ctx) {
    (void)ctx;
    CoxRaid *raid = raid_or_null(world);
    if (!raid) return say("Nothing interesting happens.");
    if (!cox_has_any(player, g_axes, (int)(sizeof(g_axes) / sizeof(g_axes[0]))))
        return say("You need an axe to chop this sapling.");
    cox_start_action(world, raid, COX_ACT_CHOP, pending, 1, 0);
    return rc_interaction_result_complete();
}

// ---------------------------------------------------------------------------
// Scavenger rooms.

enum { ROLE_BEAST = 1 };

typedef struct {
    RcTick respawn_tick;
    int shortcut_id;       // blocking loc, 0 none (small rooms)
    int shortcut_x, shortcut_y;
    int shortcut_level;
    bool shortcut_placed;
    bool shortcut_cleared;
} ScavState;
COX_STATE_FITS(ScavState);

static const CoxStats g_scavenger = {
    .hp = 30, .att = 120, .str = 120, .def = 45, .mag = 1, .rng = 1,
    .max_hit = 13, .cm_max_hit = 27, .attack_speed = 4,
    .style = COMBAT_MELEE_CRUSH, .flags = COX_STATS_SINGLES,
};

// Shortcut tile per variant (west, north, east exits); unpublished by Jagex,
// from open emulator room tables.
static const int8_t g_shortcut_tile[3][2] = {{8, 6}, {18, 22}, {25, 8}};

static bool random_tile_near(RcWorld *world, const CoxRoom *room, int cx, int cy,
                             int min_d, int max_d, int *out_x, int *out_y) {
    for (int attempt = 0; attempt < 64; attempt++) {
        int dx = cox_rand(world, 2 * max_d + 1) - max_d;
        int dy = cox_rand(world, 2 * max_d + 1) - max_d;
        int d = dx > 0 ? dx : -dx;
        int ady = dy > 0 ? dy : -dy;
        if ((d > ady ? d : ady) < min_d) continue;
        int x = cx + dx, y = cy + dy;
        if (x < room->x + 1 || x > room->x + COX_ROOM_SIZE - 3
                || y < room->y + 1 || y > room->y + COX_ROOM_SIZE - 3)
            continue;
        bool ok = true;
        for (int ty = 0; ty < 2 && ok; ty++)
            for (int tx = 0; tx < 2 && ok; tx++)
                ok = cox_tile_walkable(world, x + tx, y + ty, room->plane);
        if (!ok) continue;
        *out_x = x;
        *out_y = y;
        return true;
    }
    return false;
}

static void spawn_beast(RcWorld *world, CoxRaid *raid, CoxRoom *room) {
    int x, y;
    if (!random_tile_near(world, room, world->player.x, world->player.y, 3, 7,
                          &x, &y))
        return;
    int id = NPC_SCAVENGER + cox_rand(world, 2);
    RcNpc *npc = cox_spawn(world, raid, room, id, x, y, ROLE_BEAST,
                           &g_scavenger, false);
    if (npc) rc_combat_start_npc_vs_player(world, npc->uid, 0);
}

static void scav_enter(RcWorld *world, CoxRaid *raid, CoxRoom *room) {
    ScavState *s = COX_ROOM_STATE(ScavState, room);
    if (room->type == COX_ROOM_SCAVENGERS_LARGE && !s->shortcut_placed) {
        static const int kinds[3] = {LOC_SHORTCUT_ROCKS, LOC_SHORTCUT_SAPLING,
                                     LOC_SHORTCUT_BOULDER};
        s->shortcut_placed = true;
        s->shortcut_id = kinds[cox_rand(world, 3)];
        s->shortcut_x = room->x + g_shortcut_tile[room->variant][0];
        s->shortcut_y = room->y + g_shortcut_tile[room->variant][1];
        int skill = s->shortcut_id == LOC_SHORTCUT_ROCKS ? SKILL_MINING
                  : s->shortcut_id == LOC_SHORTCUT_SAPLING ? SKILL_WOODCUTTING
                  : SKILL_STRENGTH;
        int req = world->player.skills.base_level[skill] - 2 + cox_rand(world, 5);
        s->shortcut_level = req < 1 ? 1 : req > 99 ? 99 : req;
        if (!cox_tile_walkable(world, s->shortcut_x, s->shortcut_y, room->plane)
                || !cox_add_loc(world, s->shortcut_id, 10, 0, s->shortcut_x,
                                s->shortcut_y, room->plane, NULL))
            s->shortcut_id = 0;
    }
    if (raid->started && !cox_room_npc(world, room, ROLE_BEAST)
            && !s->respawn_tick)
        s->respawn_tick = world->tick + 2;
}

static void scav_tick(RcWorld *world, CoxRaid *raid, CoxRoom *room) {
    ScavState *s = COX_ROOM_STATE(ScavState, room);
    RcNpc *beast = cox_room_npc(world, room, ROLE_BEAST);
    if (!beast) {
        if (!s->respawn_tick) s->respawn_tick = world->tick + 3 + cox_rand(world, 2);
        if (world->tick >= s->respawn_tick) {
            s->respawn_tick = 0;
            spawn_beast(world, raid, room);
        }
        return;
    }
    // Aggressive within about 5 tiles.
    if (beast->target_uid < 0 && cox_player_distance(world, beast) <= 5)
        rc_combat_start_npc_vs_player(world, beast->uid, 0);
}

static void grant_drops(RcWorld *world, const RcNpc *npc, const RcGroundGrant *g,
                        int count) {
    if (count > 0)
        rc_ground_grant(world, g, count, npc->x, npc->y, npc->plane,
                        RC_GROUND_OWNER_LOCAL_PLAYER,
                        (RcGroundPolicy){0, 3000, 0, 3000});
}

static void scav_died(RcWorld *world, CoxRaid *raid, CoxRoom *room, RcNpc *npc,
                      int role) {
    (void)raid;
    (void)room;
    if (role != ROLE_BEAST) return;
    RcGroundGrant g[8];
    int n = 0;
    g[n++] = (RcGroundGrant){ITEM_BONES, 1, 0};
    for (int roll = 0; roll < 2; roll++) {
        int r = cox_rand(world, 18);
        if (r == 0) g[n++] = (RcGroundGrant){ITEM_FISHING_ROD, 1, 0};
        else if (r == 1) g[n++] = (RcGroundGrant){ITEM_IRON_PICKAXE, 1, 0};
        else if (r == 2) g[n++] = (RcGroundGrant){ITEM_IRON_AXE, 1, 0};
        else if (r == 3) g[n++] = (RcGroundGrant){ITEM_NET, 1, 0};
        else if (r == 4) g[n++] = (RcGroundGrant){ITEM_HAMMER, 1, 0};
        else if (r == 5) g[n++] = (RcGroundGrant){ITEM_TINDERBOX, 1, 0};
        else if (r <= 7) g[n++] = (RcGroundGrant){ITEM_LOCKPICK, 1, 0};
        else if (r <= 9)
            g[n++] = (RcGroundGrant){ITEM_CAVE_WORMS, 30 + cox_rand(world, 21), 0};
        else if (r <= 15) {
            // The secondaries come together.
            if (n + 3 > 8) break;
            g[n++] = (RcGroundGrant){ITEM_ENDARKENED_JUICE, 5 + cox_rand(world, 10), 0};
            g[n++] = (RcGroundGrant){ITEM_STINKHORN, 3 + cox_rand(world, 9), 0};
            g[n++] = (RcGroundGrant){ITEM_CICELY, 3 + cox_rand(world, 4), 0};
        } else {
            g[n++] = (RcGroundGrant){ITEM_PLANK, 2, 0};
        }
        if (n >= 7) break;
    }
    grant_drops(world, npc, g, n);
}

const CoxRoomOps cox_scavengers_ops = {
    .enter = scav_enter, .tick = scav_tick, .npc_died = scav_died,
};

static RcInteractionHandlerResult shortcut(RcWorld *world, RcPlayer *player,
                                           const RcPendingInteraction *pending,
                                           void *ctx) {
    (void)ctx;
    CoxRaid *raid = raid_or_null(world);
    CoxRoom *room = raid ? cox_room_at(raid, pending->target.tile_x,
                                       pending->target.tile_y,
                                       pending->target.plane) : NULL;
    if (!room || room->type != COX_ROOM_SCAVENGERS_LARGE)
        return say("Nothing interesting happens.");
    ScavState *s = COX_ROOM_STATE(ScavState, room);
    int id = pending->target.definition_id;
    if (id == LOC_SHORTCUT_STUMP || id == LOC_SHORTCUT_RUBBLE) {
        // Step over to the far side of the cleared shortcut.
        int x = 2 * pending->target.tile_x - player->x;
        int y = 2 * pending->target.tile_y - player->y;
        if (!cox_tile_walkable(world, x, y, player->plane))
            return say("You can't cross from here.");
        cox_relocate(world, raid, x, y, player->plane);
        return rc_interaction_result_complete();
    }
    int skill = id == LOC_SHORTCUT_ROCKS ? SKILL_MINING
              : id == LOC_SHORTCUT_SAPLING ? SKILL_WOODCUTTING : SKILL_STRENGTH;
    if (player->skills.boosted_level[skill] < s->shortcut_level) {
        static char text[RC_INTERACTION_RESULT_MESSAGE_LEN];
        snprintf(text, sizeof(text), "You need a %s level of %d to clear this.",
                 skill == SKILL_MINING ? "Mining" : skill == SKILL_WOODCUTTING
                 ? "Woodcutting" : "Strength", s->shortcut_level);
        return say(text);
    }
    if (id == LOC_SHORTCUT_ROCKS
            && !cox_has_any(player, g_pickaxes,
                            (int)(sizeof(g_pickaxes) / sizeof(g_pickaxes[0]))))
        return say("You need a pickaxe to mine these rocks.");
    if (id == LOC_SHORTCUT_SAPLING
            && !cox_has_any(player, g_axes, (int)(sizeof(g_axes) / sizeof(g_axes[0]))))
        return say("You need an axe to chop this sapling.");
    cox_start_action(world, raid, COX_ACT_SHORTCUT, pending, 3, 0);
    return rc_interaction_result_complete();
}

static bool clear_shortcut(RcWorld *world, CoxRaid *raid, CoxAction *a) {
    CoxRoom *room = cox_room_at(raid, a->target_x, a->target_y, a->plane);
    if (!room) return false;
    ScavState *s = COX_ROOM_STATE(ScavState, room);
    if (s->shortcut_cleared) return false;
    s->shortcut_cleared = true;
    if (s->shortcut_id == LOC_SHORTCUT_BOULDER) {
        cox_remove_loc(world, s->shortcut_id, s->shortcut_x, s->shortcut_y,
                       room->plane);
        cox_message(world, "You push the boulder out of the way.");
    } else {
        cox_replace_loc(world, s->shortcut_id, s->shortcut_x, s->shortcut_y,
                        room->plane, s->shortcut_id == LOC_SHORTCUT_ROCKS
                        ? LOC_SHORTCUT_RUBBLE : LOC_SHORTCUT_STUMP);
        cox_message(world, s->shortcut_id == LOC_SHORTCUT_ROCKS
                    ? "You clear the rocks." : "You chop down the rotten sapling.");
    }
    cox_add_points(raid, s->shortcut_level * 5);
    return false;
}

// ---------------------------------------------------------------------------
// Resource (farming) rooms.

enum { HERB_NONE = 0, HERB_NOXIFER = 1, HERB_GOLPAR = 2, HERB_BUCHU = 3 };
enum { ROLE_BAT = 1 };                   // bats take roles 1..3
#define FARM_BATS 3
#define PATCH_STAGE_TICKS 13             // 4 stages in about 30 seconds

typedef struct {
    struct {
        int16_t x, y;
        uint8_t herb, stage;             // stage 0 seed .. 4 grown
        uint8_t picks_left;
        RcTick planted_tick;
    } patch[2];
    uint8_t patch_count;
    bool placed;
    RcTick bat_respawn[FARM_BATS];
    int8_t snake_spot;
    int16_t spot_x[3], spot_y[3];
    uint8_t spot_count;
} FarmState;
COX_STATE_FITS(FarmState);

static const int g_seed_level[4] = {0, 55, 27, 39};
static const int g_plant_xp[4] = {0, 1200, 400, 600};
static const int g_harvest_xp[4] = {0, 3000, 1000, 1500};
static const int g_grimy[4] = {0, ITEM_GRIMY_NOXIFER, ITEM_GRIMY_GOLPAR,
                               ITEM_GRIMY_BUCHU};

static int patch_loc(int herb, int stage) {
    // Stage objects run noxifer, golpar, buchu within each stage.
    return LOC_PATCH_STAGE + stage * 3 + (herb - 1);
}

static void spawn_bat(RcWorld *world, CoxRaid *raid, CoxRoom *room, int slot) {
    int x, y;
    if (!random_tile_near(world, room, room->x + 16, room->y + 16, 0, 10, &x, &y))
        return;
    int hunter = level(world, SKILL_HUNTER);
    int max_tier = hunter >= 90 ? 6 : hunter >= 75 ? 5 : hunter >= 60 ? 4
                 : hunter >= 45 ? 3 : hunter >= 30 ? 2 : hunter >= 15 ? 1 : 0;
    int tier = cox_rand(world, 3) ? max_tier : cox_rand(world, max_tier + 1);
    RcNpc *bat = cox_spawn(world, raid, room, NPC_BAT + tier, x, y,
                           ROLE_BAT + slot, NULL, false);
    if (bat) {
        bat->disable_wander = false;
        bat->spawn_wander_range = 4;
    }
}

static void farm_enter(RcWorld *world, CoxRaid *raid, CoxRoom *room) {
    FarmState *s = COX_ROOM_STATE(FarmState, room);
    if (!s->placed) {
        s->placed = true;
        int xs[2], ys[2];
        int n = cox_find_loc(world, room, LOC_PATCH, xs, ys, NULL, 2);
        s->patch_count = (uint8_t)(n > 2 ? 2 : n);
        for (int i = 0; i < s->patch_count; i++) {
            s->patch[i].x = (int16_t)xs[i];
            s->patch[i].y = (int16_t)ys[i];
        }
        int sx[3], sy[3];
        n = cox_find_loc(world, room, LOC_BUBBLES, sx, sy, NULL, 3);
        s->spot_count = (uint8_t)(n > 3 ? 3 : n);
        for (int i = 0; i < s->spot_count; i++) {
            s->spot_x[i] = (int16_t)sx[i];
            s->spot_y[i] = (int16_t)sy[i];
        }
        s->snake_spot = s->spot_count ? (int8_t)cox_rand(world, s->spot_count) : -1;
    }
    if (room->type == COX_ROOM_FARMING_BATS) {
        for (int i = 0; i < FARM_BATS; i++)
            if (!cox_room_npc(world, room, ROLE_BAT + i) && !s->bat_respawn[i])
                spawn_bat(world, raid, room, i);
    }
}

static void grow_patches(RcWorld *world, CoxRoom *room) {
    FarmState *s = COX_ROOM_STATE(FarmState, room);
    for (int i = 0; i < s->patch_count; i++) {
        if (s->patch[i].herb == HERB_NONE || s->patch[i].stage >= 4) continue;
        int stage = (int)((world->tick - s->patch[i].planted_tick)
                          / PATCH_STAGE_TICKS);
        if (stage > 4) stage = 4;
        if (stage == s->patch[i].stage) continue;
        int from = patch_loc(s->patch[i].herb, s->patch[i].stage);
        cox_replace_loc(world, from, s->patch[i].x, s->patch[i].y, room->plane,
                        patch_loc(s->patch[i].herb, stage));
        s->patch[i].stage = (uint8_t)stage;
    }
}

static void farm_tick(RcWorld *world, CoxRaid *raid, CoxRoom *room) {
    FarmState *s = COX_ROOM_STATE(FarmState, room);
    if (room->type != COX_ROOM_FARMING_BATS) return;
    for (int i = 0; i < FARM_BATS; i++) {
        if (cox_room_npc(world, room, ROLE_BAT + i)) continue;
        if (!s->bat_respawn[i]) s->bat_respawn[i] = world->tick + 10 + cox_rand(world, 11);
        if (world->tick >= s->bat_respawn[i]) {
            s->bat_respawn[i] = 0;
            spawn_bat(world, raid, room, i);
        }
    }
}

const CoxRoomOps cox_farming_ops = {.enter = farm_enter, .tick = farm_tick};

// Patches grow on wall-clock time whether or not the player is in the room.
static void grow_all_patches(RcWorld *world, CoxRaid *raid) {
    for (int i = 0; i < raid->room_count; i++) {
        CoxRoom *room = &raid->rooms[i];
        if ((room->type == COX_ROOM_FARMING_FISH
                || room->type == COX_ROOM_FARMING_BATS)
                && COX_ROOM_STATE(FarmState, room)->placed)
            grow_patches(world, room);
    }
}

static int patch_index(FarmState *s, int x, int y) {
    for (int i = 0; i < s->patch_count; i++)
        if (s->patch[i].x == x && s->patch[i].y == y) return i;
    return -1;
}

static RcInteractionHandlerResult patch_op(RcWorld *world, RcPlayer *player,
                                           const RcPendingInteraction *pending,
                                           void *ctx) {
    (void)ctx;
    (void)player;
    CoxRaid *raid = raid_or_null(world);
    CoxRoom *room = raid ? cox_room_at(raid, pending->target.tile_x,
                                       pending->target.tile_y,
                                       pending->target.plane) : NULL;
    if (!room) return say("Nothing interesting happens.");
    FarmState *s = COX_ROOM_STATE(FarmState, room);
    int i = patch_index(s, pending->target.tile_x, pending->target.tile_y);
    if (i < 0) return say("Nothing interesting happens.");
    static const char *const names[4] = {"", "noxifer", "golpar", "buchu leaf"};
    static char text[RC_INTERACTION_RESULT_MESSAGE_LEN];
    switch (pending->op) {
    case RC_INTERACTION_OP1:          // Pick
        if (s->patch[i].stage < 4) return say("The herb isn't ready yet.");
        cox_start_action(world, raid, COX_ACT_PICK, pending, 1, i);
        return rc_interaction_result_complete();
    case RC_INTERACTION_OP3:          // Clear
        rc_world_object_revert(world, pending->target.placement_key);
        s->patch[i].herb = HERB_NONE;
        s->patch[i].stage = 0;
        return say("You clear the patch.");
    default:                          // Inspect
        if (s->patch[i].herb == HERB_NONE)
            return say("This patch is empty. Plant a seed here with a seed dibber.");
        snprintf(text, sizeof(text), s->patch[i].stage >= 4
                 ? "This patch has fully grown %s." : "This patch is growing %s.",
                 names[s->patch[i].herb]);
        return say(text);
    }
}

static RcInteractionHandlerResult plant_seed(RcWorld *world, RcPlayer *player,
                                             const RcPendingInteraction *pending,
                                             void *ctx) {
    (void)ctx;
    CoxRaid *raid = raid_or_null(world);
    CoxRoom *room = raid ? cox_room_at(raid, pending->target.tile_x,
                                       pending->target.tile_y,
                                       pending->target.plane) : NULL;
    if (!room) return say("Nothing interesting happens.");
    FarmState *s = COX_ROOM_STATE(FarmState, room);
    int i = patch_index(s, pending->target.tile_x, pending->target.tile_y);
    if (i < 0) return say("Nothing interesting happens.");
    if (s->patch[i].herb != HERB_NONE) return say("This patch is already in use.");
    int seed = pending->source_item_id;
    int herb = seed == ITEM_SEED_NOXIFER ? HERB_NOXIFER
             : seed == ITEM_SEED_GOLPAR ? HERB_GOLPAR : HERB_BUCHU;
    if (!cox_has_item(player, ITEM_SEED_DIBBER))
        return say("You need a seed dibber to plant seeds.");
    if (player->skills.boosted_level[SKILL_FARMING] < g_seed_level[herb]) {
        static char text[RC_INTERACTION_RESULT_MESSAGE_LEN];
        snprintf(text, sizeof(text), "You need a Farming level of %d to plant that.",
                 g_seed_level[herb]);
        return say(text);
    }
    if (!cox_take_item(world, seed, 1)) return say("Nothing interesting happens.");
    s->patch[i].herb = (uint8_t)herb;
    s->patch[i].stage = 0;
    s->patch[i].planted_tick = world->tick;
    // Yield rises with Farming level; Jagex never published the formula.
    s->patch[i].picks_left = (uint8_t)(3 + player->skills.boosted_level[SKILL_FARMING] / 33);
    cox_replace_loc(world, LOC_PATCH, s->patch[i].x, s->patch[i].y, room->plane,
                    patch_loc(herb, 0));
    cox_add_xp(world, SKILL_FARMING, g_plant_xp[herb]);
    return say("You plant the seed in the patch.");
}

static bool pick_herb(RcWorld *world, CoxRaid *raid, CoxAction *a) {
    CoxRoom *room = cox_room_at(raid, a->target_x, a->target_y, a->plane);
    if (!room) return false;
    FarmState *s = COX_ROOM_STATE(FarmState, room);
    int i = a->param;
    if (i < 0 || i >= s->patch_count || s->patch[i].stage < 4) return false;
    int herb = s->patch[i].herb;
    bool ok = free_slots(&world->player) > 0
        ? cox_give_item(world, g_grimy[herb], 1)
        : rc_ground_item_spawn(world, g_grimy[herb], 1, world->player.x,
                               world->player.y, world->player.plane, 0) >= 0;
    if (!ok) return false;
    cox_add_xp(world, SKILL_FARMING, g_harvest_xp[herb]);
    a->next_tick = world->tick + 2;
    if (--s->patch[i].picks_left == 0) {
        rc_world_object_revert(world, a->target_key);
        s->patch[i].herb = HERB_NONE;
        s->patch[i].stage = 0;
        cox_message(world, "The patch is now empty.");
        return false;
    }
    return true;
}

static RcInteractionHandlerResult old_tools(RcWorld *world, RcPlayer *player,
                                            const RcPendingInteraction *pending,
                                            void *ctx) {
    (void)ctx;
    static const int tools[3] = {ITEM_RAKE, ITEM_SPADE, ITEM_SEED_DIBBER};
    int first = 0, last = 2;
    if (pending->op >= RC_INTERACTION_OP2 && pending->op <= RC_INTERACTION_OP4)
        first = last = (int)pending->op - (int)RC_INTERACTION_OP2;
    bool took = false;
    for (int t = first; t <= last; t++) {
        if (cox_has_item(player, tools[t])) continue;
        if (!cox_give_item(world, tools[t], 1))
            return say("You don't have enough inventory space.");
        took = true;
    }
    return say(took ? "You take some tools." : "You already have those tools.");
}

static RcInteractionHandlerResult gourd_tree(RcWorld *world, RcPlayer *player,
                                             const RcPendingInteraction *pending,
                                             void *ctx) {
    (void)ctx;
    int n = pending->op == RC_INTERACTION_OP2 ? free_slots(player) : 1;
    if (free_slots(player) <= 0) return say("You don't have enough inventory space.");
    for (int i = 0; i < n; i++) cox_give_item(world, ITEM_GOURD_EMPTY, 1);
    return say(n > 1 ? "You pick some gourds from the tree."
                     : "You pick a gourd from the tree.");
}

static RcInteractionHandlerResult geyser(RcWorld *world, RcPlayer *player,
                                         const RcPendingInteraction *pending,
                                         void *ctx) {
    (void)pending;
    (void)ctx;
    if (!cox_swap_items(world, ITEM_GOURD_EMPTY, ITEM_GOURD_WATER, RC_INVENTORY_SIZE))
        return say("You need an empty gourd vial to fill.");
    (void)player;
    return say("You fill the gourd vials with water from the geyser.");
}

// Fishing: a rod, cave worms, and any spot gives any fish your level allows.
static int fish_tier(const RcWorld *world, int skill) {
    int lv = level(world, skill);
    return lv >= 90 ? 6 : lv >= 75 ? 5 : lv >= 60 ? 4 : lv >= 45 ? 3
         : lv >= 30 ? 2 : lv >= 15 ? 1 : 0;
}

static RcInteractionHandlerResult fish(RcWorld *world, RcPlayer *player,
                                       const RcPendingInteraction *pending,
                                       void *ctx) {
    (void)ctx;
    CoxRaid *raid = raid_or_null(world);
    if (!raid) return say("Nothing interesting happens.");
    if (!cox_has_item(player, ITEM_FISHING_ROD) && !cox_has_item(player, ITEM_PEARL_ROD))
        return say("You need a fishing rod to fish here.");
    if (!cox_has_item(player, ITEM_CAVE_WORMS))
        return say("You need some cave worms to fish here.");
    cox_start_action(world, raid, COX_ACT_FISH, pending, 2, 0);
    return rc_interaction_result_complete();
}

static bool fish_tick(RcWorld *world, CoxRaid *raid, CoxAction *a) {
    RcPlayer *p = &world->player;
    if (free_slots(p) <= 0) {
        cox_message(world, "You don't have enough inventory space.");
        return false;
    }
    if (!cox_has_item(p, ITEM_CAVE_WORMS)) {
        cox_message(world, "You have run out of cave worms.");
        return false;
    }
    a->next_tick = world->tick + 5;
    CoxRoom *room = cox_room_at(raid, a->target_x, a->target_y, a->plane);
    if (room) {
        FarmState *s = COX_ROOM_STATE(FarmState, room);
        if (s->snake_spot >= 0 && s->spot_x[s->snake_spot] == a->target_x
                && s->spot_y[s->snake_spot] == a->target_y && cox_rand(world, 4) == 0) {
            rc_combat_damage_player(world, 3 + cox_rand(world, 4), 0, COMBAT_NONE,
                                    RC_HIT_SOURCE_STATUS);
            cox_message(world, "A cave snake bites you!");
            return true;
        }
    }
    if (!cox_skill_roll(world, level(world, SKILL_FISHING), 48, 160)) return true;
    int max_tier = fish_tier(world, SKILL_FISHING);
    int tier = cox_rand(world, 4) ? max_tier : cox_rand(world, max_tier + 1);
    cox_take_item(world, ITEM_CAVE_WORMS, 1);
    cox_give_item(world, ITEM_FISH_RAW + 2 * tier, 1);
    cox_add_xp(world, SKILL_FISHING, (20 + 3 * tier) * 100);
    return true;
}

static RcInteractionHandlerResult catch_bat(RcWorld *world, RcPlayer *player,
                                            const RcPendingInteraction *pending,
                                            void *ctx) {
    (void)ctx;
    CoxRaid *raid = raid_or_null(world);
    if (!raid) return say("Nothing interesting happens.");
    bool net = cox_has_any(player, (const int[]){ITEM_NET, ITEM_MAGIC_NET}, 2);
    if (!net && player->skills.boosted_level[SKILL_HUNTER] < 99)
        return say("You need a butterfly net to catch bats.");
    if (free_slots(player) <= 0) return say("You don't have enough inventory space.");
    RcNpc *bat = rc_npc_resolve(world, (RcNpcId)pending->target.entity_uid);
    const RcNpcDef *def = bat ? rc_npc_base_def_for_npc(bat) : NULL;
    if (!def) return say("Nothing interesting happens.");
    int tier = def->id - NPC_BAT;
    static const int req[7] = {1, 15, 30, 45, 60, 75, 90};
    if (tier < 0 || tier > 6) return say("Nothing interesting happens.");
    if (player->skills.boosted_level[SKILL_HUNTER] < req[tier])
        return say("Your Hunter level isn't high enough to catch that bat.");
    if (!cox_skill_roll(world, player->skills.boosted_level[SKILL_HUNTER] - req[tier] + 1,
                        96, 256))
        return say("The bat flutters out of your reach.");
    cox_give_item(world, ITEM_BAT_RAW + 2 * tier, 1);
    cox_add_xp(world, SKILL_HUNTER, (5 + 4 * tier) * 100);
    // Removing the bat ends this interaction, so the message goes out first.
    cox_message(world, "You catch the bat.");
    rc_npc_remove(world, bat->uid);
    return rc_interaction_result_complete();
}

// Weeds: rake for 1-3 seeds of one kind, never the same kind twice running.
static RcInteractionHandlerResult rake_weeds(RcWorld *world, RcPlayer *player,
                                             const RcPendingInteraction *pending,
                                             void *ctx) {
    (void)ctx;
    CoxRaid *raid = raid_or_null(world);
    if (!raid) return say("Nothing interesting happens.");
    if (!cox_has_item(player, ITEM_RAKE)) return say("You need a rake to rake the weeds.");
    cox_start_action(world, raid, COX_ACT_RAKE, pending, 3, -1);
    return rc_interaction_result_complete();
}

static bool rake_tick(RcWorld *world, CoxRaid *raid, CoxAction *a) {
    (void)raid;
    static const int seeds[3] = {ITEM_SEED_GOLPAR, ITEM_SEED_BUCHU, ITEM_SEED_NOXIFER};
    int kind = cox_rand(world, 3);
    if (kind == a->param) kind = (kind + 1 + cox_rand(world, 2)) % 3;
    a->param = kind;
    a->next_tick = world->tick + 4;
    if (!cox_give_item(world, seeds[kind], 1 + cox_rand(world, 3))) {
        cox_message(world, "You don't have enough inventory space.");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Cooking and fires.

static bool is_fire(int loc_id) {
    return loc_id == LOC_FIRE || loc_id == LOC_TEKTON_FIRE
        || loc_id == LOC_VASA_FIRE || loc_id == LOC_BRAZIER_LIT;
}

static int raw_tier(int item_id, bool *bat) {
    if (item_id >= ITEM_FISH_RAW && item_id < ITEM_FISH_RAW + 14
            && (item_id - ITEM_FISH_RAW) % 2 == 0) {
        *bat = false;
        return (item_id - ITEM_FISH_RAW) / 2;
    }
    if (item_id >= ITEM_BAT_RAW && item_id < ITEM_BAT_RAW + 14
            && (item_id - ITEM_BAT_RAW) % 2 == 0) {
        *bat = true;
        return (item_id - ITEM_BAT_RAW) / 2;
    }
    return -1;
}

static RcInteractionHandlerResult cook(RcWorld *world, RcPlayer *player,
                                       const RcPendingInteraction *pending,
                                       void *ctx) {
    (void)ctx;
    CoxRaid *raid = raid_or_null(world);
    if (!raid) return say("Nothing interesting happens.");
    int fire = rc_world_object_active_id(world, pending->target.definition_id,
                                         pending->target.tile_x,
                                         pending->target.tile_y,
                                         pending->target.plane);
    if (!is_fire(fire)) return say("Nothing interesting happens.");
    bool bat = false;
    int tier = raw_tier(pending->source_item_id, &bat);
    static const int req[7] = {1, 15, 30, 45, 60, 75, 90};
    if (tier < 0) return say("Nothing interesting happens.");
    if (player->skills.boosted_level[SKILL_COOKING] < req[tier])
        return say("Your Cooking level isn't high enough to cook that.");
    cox_start_action(world, raid, COX_ACT_COOK, pending, 1, pending->source_item_id);
    return rc_interaction_result_complete();
}

static bool cook_tick(RcWorld *world, CoxRaid *raid, CoxAction *a) {
    RcPlayer *p = &world->player;
    bool bat = false;
    int tier = raw_tier(a->param, &bat);
    if (tier < 0 || !cox_has_item(p, a->param)) return false;
    a->next_tick = world->tick + 4;
    static const int req[7] = {1, 15, 30, 45, 60, 75, 90};
    // Burn rates are unpublished: half burn at the requirement, none 30
    // levels above it.
    int margin = level(world, SKILL_COOKING) - req[tier];
    bool burnt = margin < 30 && cox_rand(world, 60) >= 30 + margin;
    if (!cox_swap_items(world, a->param, burnt ? (bat ? ITEM_BAT_BURNT
                        : ITEM_FISH_BURNT) : a->param + 1, 1))
        return false;
    if (burnt) {
        cox_message(world, "You accidentally burn it.");
    } else {
        cox_add_xp(world, SKILL_COOKING, (10 + 3 * tier) * 100);
        cox_add_points(raid, 4 + 8 * tier);
    }
    return cox_has_item(p, a->param);
}

static RcInteractionHandlerResult light_kindling(RcWorld *world, RcPlayer *player,
                                                 const RcPendingInteraction *pending,
                                                 void *ctx) {
    (void)pending;
    (void)ctx;
    CoxRaid *raid = raid_or_null(world);
    if (!raid) return say("Nothing interesting happens.");
    if (!cox_take_item(world, ITEM_KINDLING, 1)) return say("Nothing interesting happens.");
    uint64_t key = 0;
    if (!cox_add_loc(world, LOC_FIRE, 10, 0, player->x, player->y, player->plane,
                     &key)) {
        cox_give_item(world, ITEM_KINDLING, 1);
        return say("You can't light a fire here.");
    }
    // Ground fires burn for about a minute, then return to the floor.
    rc_world_object_expire(world, key, world->tick + 100);
    cox_add_xp(world, SKILL_FIREMAKING, 4800);
    int x = player->x - 1, y = player->y;
    if (cox_tile_walkable(world, x, y, player->plane))
        cox_relocate(world, raid, x, y, player->plane);
    return say("The fire catches and the kindling begins to burn.");
}

// ---------------------------------------------------------------------------
// Herblore.

typedef struct {
    int herb;          // clean herb item
    int secondary;     // item consumed with it; 0 = overload
    int line;          // LINE_*, or -1 antipoison
    int level[3];      // (-), regular, (+)
    int xp[3];         // hundredths
} CoxRecipe;

static const CoxRecipe g_recipes[] = {
    {ITEM_GOLPAR, ITEM_STINKHORN, LINE_ELDER, {47, 59, 70}, {650, 1000, 1300}},
    {ITEM_GOLPAR, ITEM_CICELY, LINE_TWISTED, {47, 59, 70}, {650, 1000, 1300}},
    {ITEM_GOLPAR, ITEM_ENDARKENED_JUICE, LINE_KODAI, {47, 59, 70}, {650, 1000, 1300}},
    {ITEM_BUCHU, ITEM_STINKHORN, LINE_REVITALISATION, {52, 65, 78}, {1350, 2000, 2650}},
    {ITEM_BUCHU, ITEM_CICELY, LINE_PRAYER_ENHANCE, {52, 65, 78}, {1350, 2000, 2650}},
    {ITEM_BUCHU, ITEM_ENDARKENED_JUICE, LINE_XERICS_AID, {52, 65, 78}, {1350, 2000, 2650}},
    {ITEM_NOXIFER, 0, LINE_OVERLOAD, {60, 75, 90}, {3350, 5000, 6650}},
    {ITEM_NOXIFER, ITEM_CICELY, -1, {60, 75, 90}, {1350, 2000, 2650}},
};

static int potion_id(int line, int tier, int dose) {
    if (line < 0) return ITEM_ANTIPOISON + tier * 4 + dose - 1;
    return ITEM_POTIONS + line * 12 + tier * 4 + dose - 1;
}

// Highest tier of a 4-dose potion line carried, or -1.
static int carried_tier(const RcPlayer *p, int line) {
    for (int tier = 2; tier >= 0; tier--)
        if (cox_has_item(p, potion_id(line, tier, 4))) return tier;
    return -1;
}

static const CoxRecipe *recipe_for(const RcPlayer *p, int herb) {
    for (unsigned i = 0; i < sizeof(g_recipes) / sizeof(g_recipes[0]); i++) {
        const CoxRecipe *r = &g_recipes[i];
        if (r->herb != herb) continue;
        if (r->secondary ? cox_has_item(p, r->secondary)
                : carried_tier(p, LINE_ELDER) >= 0 && carried_tier(p, LINE_TWISTED) >= 0
                  && carried_tier(p, LINE_KODAI) >= 0)
            return r;
    }
    return NULL;
}

static int recipe_tier(const RcWorld *world, const CoxRecipe *r) {
    int lv = level(world, SKILL_HERBLORE);
    int tier = lv >= r->level[2] ? 2 : lv >= r->level[1] ? 1 : lv >= r->level[0] ? 0 : -1;
    if (r->line == LINE_OVERLOAD && tier >= 0) {
        // (+) overloads need (+) inputs.
        int inputs = carried_tier(&world->player, LINE_ELDER);
        int t = carried_tier(&world->player, LINE_TWISTED);
        if (t < inputs) inputs = t;
        t = carried_tier(&world->player, LINE_KODAI);
        if (t < inputs) inputs = t;
        if (inputs < tier) tier = inputs;
    }
    return tier;
}

static bool mix_once(RcWorld *world, CoxRaid *raid, int herb) {
    RcPlayer *p = &world->player;
    const CoxRecipe *r = recipe_for(p, herb);
    if (!r || !cox_has_item(p, herb) || !cox_has_item(p, ITEM_GOURD_WATER))
        return false;
    int tier = recipe_tier(world, r);
    if (tier < 0) {
        cox_message(world, "You need a higher Herblore level to make this potion.");
        return false;
    }
    if (!cox_take_item(world, herb, 1) || !cox_take_item(world, ITEM_GOURD_WATER, 1))
        return false;
    if (r->secondary) {
        cox_take_item(world, r->secondary, 1);
    } else {
        cox_take_item(world, potion_id(LINE_ELDER, carried_tier(p, LINE_ELDER), 4), 1);
        cox_take_item(world, potion_id(LINE_TWISTED, carried_tier(p, LINE_TWISTED), 4), 1);
        cox_take_item(world, potion_id(LINE_KODAI, carried_tier(p, LINE_KODAI), 4), 1);
    }
    cox_give_item(world, potion_id(r->line, tier, 4), 1);
    cox_add_xp(world, SKILL_HERBLORE, r->xp[tier]);
    (void)raid;
    return true;
}

static RcInteractionHandlerResult mix(RcWorld *world, RcPlayer *player,
                                      const RcPendingInteraction *pending,
                                      void *ctx) {
    (void)ctx;
    CoxRaid *raid = raid_or_null(world);
    if (!raid) return say("Nothing interesting happens.");
    int a = pending->source_item_id, b = pending->target.definition_id;
    int herb = a == ITEM_GOURD_WATER ? b : a;
    const CoxRecipe *r = recipe_for(player, herb);
    if (!r) return say("You need a secondary ingredient to make a potion.");
    if (recipe_tier(world, r) < 0)
        return say("You need a higher Herblore level to make this potion.");
    cox_start_action(world, raid, COX_ACT_MIX, NULL, 0, herb);
    return rc_interaction_result_complete();
}

static RcInteractionHandlerResult clean_herb(RcWorld *world, RcPlayer *player,
                                             const RcPendingInteraction *pending,
                                             void *ctx) {
    (void)ctx;
    int grimy = pending->target.definition_id;
    int clean = grimy + 1;
    int req = grimy == ITEM_GRIMY_GOLPAR ? 47 : grimy == ITEM_GRIMY_BUCHU ? 52 : 60;
    int xp = grimy == ITEM_GRIMY_GOLPAR ? 100 : grimy == ITEM_GRIMY_BUCHU ? 200 : 300;
    if (player->skills.boosted_level[SKILL_HERBLORE] < req) {
        static char text[RC_INTERACTION_RESULT_MESSAGE_LEN];
        snprintf(text, sizeof(text), "You need a Herblore level of %d to clean this herb.",
                 req);
        return say(text);
    }
    if (!cox_swap_items(world, grimy, clean, 1))
        return rc_interaction_result_cancel(RC_INTERACTION_FAIL_TARGET_MISSING);
    cox_add_xp(world, SKILL_HERBLORE, xp);
    return rc_interaction_result_complete();
}

// ---------------------------------------------------------------------------
// Raid potion effects the consumables table cannot express.

static int tier_boost(int base, int tier) {
    static const int flat[3] = {4, 5, 6}, pct[3] = {10, 13, 16};
    return flat[tier] + base * pct[tier] / 100;
}

static void overload_boost(RcPlayer *p, int tier) {
    static const int skills[5] = {SKILL_ATTACK, SKILL_STRENGTH, SKILL_DEFENCE,
                                  SKILL_RANGED, SKILL_MAGIC};
    for (int i = 0; i < 5; i++) {
        int s = skills[i], base = p->skills.base_level[s];
        int boosted = base + tier_boost(base, tier);
        if (p->skills.boosted_level[s] < boosted) p->skills.boosted_level[s] = boosted;
    }
}

static void consumable_effect(RcWorld *world, const RcConsumableDef *def) {
    CoxRaid *raid = cox_raid(world);
    if (!raid) return;
    RcPlayer *p = &world->player;
    int e = def->effect;
    if (e >= COX_EFFECT_OVERLOAD && e < COX_EFFECT_OVERLOAD + 3) {
        int tier = e - COX_EFFECT_OVERLOAD;
        // A dose while one runs renews it without the damage.
        bool active = raid->overload_until > world->tick;
        raid->overload_tier = (uint8_t)tier;
        raid->overload_until = world->tick + 500;
        raid->overload_next_boost = world->tick + 25;
        if (!active) {
            raid->overload_hits_left = 5;
            raid->overload_next_damage = world->tick + 1;
        }
        overload_boost(p, tier);
    } else if (e >= COX_EFFECT_PRAYER_ENHANCE && e < COX_EFFECT_PRAYER_ENHANCE + 3) {
        int tier = e - COX_EFFECT_PRAYER_ENHANCE;
        // Mod Ash, (+): floor(Prayer / 2 + 31) pulses at min(63, 500 / pulses)
        // ticks. Lower tiers are unpublished: 8 fewer pulses per tier.
        int pulses = p->skills.base_level[SKILL_PRAYER] / 2 + 31 - 8 * (2 - tier);
        if (pulses < 1) pulses = 1;
        int interval = 500 / pulses;
        raid->enhance_tier = (uint8_t)tier;
        raid->enhance_interval = (uint16_t)(interval > 63 ? 63 : interval < 1 ? 1 : interval);
        raid->enhance_until = world->tick + (RcTick)pulses * raid->enhance_interval;
        raid->enhance_next_pulse = world->tick + raid->enhance_interval;
    }
}

void cox_tick_effects(RcWorld *world, CoxRaid *raid) {
    RcPlayer *p = &world->player;
    grow_all_patches(world, raid);
    if (raid->overload_hits_left && world->tick >= raid->overload_next_damage
            && !p->is_dead) {
        rc_combat_damage_player(world, 10, 0, COMBAT_NONE, RC_HIT_SOURCE_STATUS);
        raid->overload_hits_left--;
        raid->overload_next_damage = world->tick + 2;
    }
    if (raid->overload_until) {
        if (world->tick >= raid->overload_until) {
            raid->overload_until = 0;
            if (!p->is_dead) {
                int hp = p->current_hp + 500;
                p->current_hp = hp > p->max_hp ? p->max_hp : hp;
                cox_message(world, "The effects of overload have worn off, and you feel normal again.");
            }
        } else if (world->tick >= raid->overload_next_boost) {
            overload_boost(p, raid->overload_tier);
            raid->overload_next_boost = world->tick + 25;
        }
    }
    if (raid->enhance_until) {
        if (world->tick >= raid->enhance_until) {
            raid->enhance_until = 0;
        } else if (world->tick >= raid->enhance_next_pulse) {
            rc_prayer_restore_points(p, 1);
            raid->enhance_next_pulse = world->tick + raid->enhance_interval;
        }
    }
}

// ---------------------------------------------------------------------------
// Tool spawns and storage units.

static RcInteractionHandlerResult take_tool(RcWorld *world, RcPlayer *player,
                                            const RcPendingInteraction *pending,
                                            void *ctx) {
    (void)ctx;
    int loc = pending->target.definition_id;
    int item = loc == LOC_TINDERBOX ? ITEM_TINDERBOX
             : loc == LOC_BRONZE_AXE ? ITEM_BRONZE_AXE
             : loc == LOC_HAMMER ? ITEM_HAMMER : ITEM_IRON_PICKAXE;
    if (cox_has_item(player, item)) return say("You already have one of those.");
    if (!cox_give_item(world, item, 1)) return say("You don't have enough inventory space.");
    return rc_interaction_result_complete();
}

static const int g_storage_tiers[5] = {LOC_STORAGE_TINY, LOC_STORAGE_SMALL,
                                       LOC_STORAGE_MEDIUM, LOC_STORAGE_LARGE,
                                       LOC_STORAGE_MASSIVE};

static RcInteractionHandlerResult storage(RcWorld *world, RcPlayer *player,
                                          const RcPendingInteraction *pending,
                                          void *ctx) {
    (void)ctx;
    CoxRaid *raid = raid_or_null(world);
    if (!raid) return say("Nothing interesting happens.");
    // Options: Shared, Private, and Build (tiny) or Upgrade, fifth.
    if (pending->op != RC_INTERACTION_OP5)
        return say("Storage units can't hold items in RuneC yet.");
    int next = raid->storage_tier + 1;
    if (next > 4) return say("This storage unit can't be upgraded any further.");
    static const int req[5] = {0, 30, 60, 90, 99};
    if (player->skills.boosted_level[SKILL_CONSTRUCTION] < req[next]) {
        static char text[RC_INTERACTION_RESULT_MESSAGE_LEN];
        snprintf(text, sizeof(text), "You need a Construction level of %d to build that.",
                 req[next]);
        return say(text);
    }
    if (!cox_has_any(player, g_hammers, 3)) return say("You need a hammer to build here.");
    if (cox_inventory_count(player, ITEM_PLANK) < 2)
        return say("You need 2 mallignum root planks to build that.");
    cox_take_item(world, ITEM_PLANK, 2);
    // One build upgrades every hotspot of the raid.
    int from = g_storage_tiers[raid->storage_tier], to = g_storage_tiers[next];
    for (int i = 0; i < raid->room_count; i++) {
        int xs[4], ys[4];
        int n = cox_find_loc(world, &raid->rooms[i], LOC_STORAGE_TINY, xs, ys, NULL, 4);
        for (int k = 0; k < n && k < 4; k++)
            cox_replace_loc(world, from, xs[k], ys[k], raid->rooms[i].plane, to);
    }
    raid->storage_tier = (uint8_t)next;
    cox_add_xp(world, SKILL_CONSTRUCTION, 15000);
    cox_add_points(raid, 100);
    return say("You build a storage unit.");
}

// ---------------------------------------------------------------------------

static bool resource_action(RcWorld *world, CoxRaid *raid, CoxAction *a) {
    switch (a->kind) {
    case COX_ACT_CHOP: return cox_chop_sapling(world, raid, a);
    case COX_ACT_FISH: return fish_tick(world, raid, a);
    case COX_ACT_RAKE: return rake_tick(world, raid, a);
    case COX_ACT_COOK: return cook_tick(world, raid, a);
    case COX_ACT_SHORTCUT: return clear_shortcut(world, raid, a);
    case COX_ACT_PICK: return pick_herb(world, raid, a);
    case COX_ACT_MIX:
        a->next_tick = world->tick + 2;
        return mix_once(world, raid, a->param) && cox_has_item(&world->player, a->param);
    default: return false;
    }
}

int cox_resources_register(RcWorld *world) {
    int ok = 1;
    world->consumable_effect = consumable_effect;
    ok &= cox_register_object(world, LOC_SAPLING, 0, chop_sapling);
    ok &= cox_register_object(world, LOC_SHORTCUT_SAPLING, 0, shortcut);
    ok &= cox_register_object(world, LOC_SHORTCUT_ROCKS, 0, shortcut);
    ok &= cox_register_object(world, LOC_SHORTCUT_BOULDER, 0, shortcut);
    ok &= cox_register_object(world, LOC_SHORTCUT_STUMP, 0, shortcut);
    ok &= cox_register_object(world, LOC_SHORTCUT_RUBBLE, 0, shortcut);
    ok &= cox_register_object(world, LOC_PATCH, 1, patch_op);
    for (int herb = 0; herb < 3; herb++) {
        for (int stage = 0; stage < 5; stage++)
            ok &= cox_register_object(world, LOC_PATCH_STAGE + stage * 3 + herb, 1,
                                      patch_op);
        int grown = LOC_PATCH_STAGE + 12 + herb;
        ok &= cox_register_object(world, grown, 0, patch_op);
        ok &= cox_register_object(world, grown, 2, patch_op);
    }
    static const int seeds[3] = {ITEM_SEED_NOXIFER, ITEM_SEED_GOLPAR, ITEM_SEED_BUCHU};
    for (int i = 0; i < 3; i++)
        ok &= cox_register_item_on_object(world, seeds[i], LOC_PATCH, plant_seed);
    for (int op = 0; op < 4; op++)
        ok &= cox_register_object(world, LOC_OLD_TOOLS, op, old_tools);
    ok &= cox_register_object(world, LOC_GOURD_TREE, 0, gourd_tree);
    ok &= cox_register_object(world, LOC_GOURD_TREE, 1, gourd_tree);
    ok &= cox_register_object(world, LOC_GEYSER, 0, geyser);
    ok &= cox_register_item_on_object(world, ITEM_GOURD_EMPTY, LOC_GEYSER, geyser);
    ok &= cox_register_object(world, LOC_BUBBLES, 0, fish);
    for (int tier = 0; tier < 7; tier++)
        ok &= cox_register_npc(world, NPC_BAT + tier, 0, catch_bat);
    ok &= cox_register_object(world, LOC_WEEDS, 0, rake_weeds);
    for (int tier = 0; tier < 7; tier++) {
        ok &= cox_register_item_on_object(world, ITEM_FISH_RAW + 2 * tier,
                                          RC_INTERACTION_KEY_ANY, cook);
        ok &= cox_register_item_on_object(world, ITEM_BAT_RAW + 2 * tier,
                                          RC_INTERACTION_KEY_ANY, cook);
    }
    ok &= cox_register_item_on_item(world, ITEM_TINDERBOX, ITEM_KINDLING, light_kindling);
    ok &= cox_register_item_on_item(world, ITEM_KINDLING, ITEM_TINDERBOX, light_kindling);
    static const int herbs[3] = {ITEM_GOLPAR, ITEM_BUCHU, ITEM_NOXIFER};
    for (int i = 0; i < 3; i++) {
        ok &= cox_register_item_on_item(world, herbs[i], ITEM_GOURD_WATER, mix);
        ok &= cox_register_item_on_item(world, ITEM_GOURD_WATER, herbs[i], mix);
    }
    static const int grimy[3] = {ITEM_GRIMY_GOLPAR, ITEM_GRIMY_BUCHU, ITEM_GRIMY_NOXIFER};
    for (int i = 0; i < 3; i++)
        ok &= cox_register_item(world, grimy[i], 0, clean_herb);
    ok &= cox_register_object(world, LOC_TINDERBOX, 0, take_tool);
    ok &= cox_register_object(world, LOC_BRONZE_AXE, 0, take_tool);
    ok &= cox_register_object(world, LOC_HAMMER, 0, take_tool);
    ok &= cox_register_object(world, LOC_PICKAXE, 0, take_tool);
    ok &= cox_register_object(world, LOC_STORAGE_TINY, 1, storage);
    ok &= cox_register_object(world, LOC_STORAGE_TINY, 4, storage);
    for (int t = 1; t < 5; t++) {
        ok &= cox_register_object(world, g_storage_tiers[t], 0, storage);
        ok &= cox_register_object(world, g_storage_tiers[t], 1, storage);
        if (t < 4) ok &= cox_register_object(world, g_storage_tiers[t], 4, storage);
    }
    return ok;
}
