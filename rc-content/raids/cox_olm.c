#include "cox.h"

#include "../../rc-core/api.h"
#include "../../rc-core/combat.h"
#include "../../rc-core/combat_hit.h"
#include "../../rc-core/consumables.h"
#include "../../rc-core/interaction.h"
#include "../../rc-core/items.h"
#include "../../rc-core/object_runtime.h"
#include "../../rc-core/prayer.h"

#include <string.h>

// The Great Olm. The head and both claws are owner-controlled NPCs in the
// chamber's east or west wall. The head acts on a 4-step clock (basic,
// null, basic, special) and turns to face the raider's view; each claw
// phase has one power (acid, flame or crystal), the left (melee) claw casts
// the generic specials, and the head's final stand comes after the last
// claw phase. Sources and chosen values: rc-content/README.md.

enum {
    NPC_HEAD = 7554, NPC_HEAD_SPAWNING = 7551,
    NPC_MELEE_CLAW = 7555, NPC_MELEE_CLAW_SPAWNING = 7552, NPC_MELEE_CLAW_DYING = 7557,
    NPC_MAGE_CLAW = 7553, NPC_MAGE_CLAW_SPAWNING = 7550, NPC_MAGE_CLAW_DYING = 7556,
    NPC_FIRE = 7558,
};
enum { ROLE_HEAD = 1, ROLE_MELEE = 2, ROLE_MAGE = 3, ROLE_FIRE = 4 };   // fires 4..23
#define FIRES 20

enum {
    LOC_BARRIER = 29879,
    LOC_EXIT_CRYSTAL = 30018,
    LOC_REWARD_CRYSTAL = 30027,
    LOC_REWARD_CHEST = 30028,
    LOC_ACID_POOL = 30032,
    LOC_CRYSTAL_BOMB = 29766,
    LOC_SEEDLING = 30033,
    LOC_LARGE_CRYSTAL = 30034,
};

enum { ITEM_GOURD_WATER = 20801, ITEM_GOURD_EMPTY = 20800 };

// The arena floor.
#define ARENA_MIN_X 3228
#define ARENA_MAX_X 3237
#define ARENA_MIN_Y 5730
#define ARENA_MAX_Y 5748
#define BARRIER_Y 5729

enum { OLM_WAIT, OLM_RISING, OLM_ACTIVE, OLM_SINKING, OLM_SHOWER, OLM_DONE };
enum { POWER_ACID = 0, POWER_FLAME = 1, POWER_CRYSTAL = 2 };
enum { STEP_BASIC1, STEP_NULL, STEP_BASIC2, STEP_SPECIAL };
enum { SPECIAL_BURST, SPECIAL_LIGHTNING, SPECIAL_TELEPORT, SPECIAL_HEAL };
enum { VIEW_SOUTH, VIEW_MIDDLE, VIEW_NORTH };
enum { ATK_ACID_SPRAY, ATK_ACID_DRIP, ATK_DEEP_BURN, ATK_FIRE_WALL,
       ATK_FALLING_CRYSTALS, ATK_CRYSTAL_BOMBS, ATK_SIPHON, ATK_COUNT };

typedef struct {
    uint8_t stage;
    uint8_t phase;              // 1-based claw phase; claw_phases + 1 = final
    uint8_t claw_phases;
    bool west;
    uint8_t power;
    uint8_t powers_used;        // bitmask of POWER_*
    uint8_t facing;
    uint8_t step;
    uint8_t special;
    bool owed_basic;
    uint8_t auto_style;         // RcCombatStyle
    uint16_t used_attacks;      // power attacks used this phase (bitmask)
    bool melee_down, mage_down;
    RcTick next_step, stage_tick, claw_down_tick;
    RcTick clench_until, null_tick, heal_until;
    // Running mechanics.
    RcTick wall_tick;
    int16_t wall_y;
    uint8_t crystals_left;
    RcTick next_crystal;
    RcTick drip_until;
    int16_t drip_x, drip_y;
    uint8_t burn_hits;
    RcTick next_burn;
    RcTick teleport_tick;
    int16_t portal_x, portal_y;
    struct { int16_t x, y; int8_t dy; bool live; } bolt[6];
    RcTick burst_tick;
    int16_t burst_x, burst_y;
    RcTick siphon_tick;
    int16_t pool_x[2], pool_y[2];
    uint8_t siphon_cooldown;
    RcTick sphere_tick;
    uint8_t sphere_style;
    RcTick next_shower;
    bool opened;
} OlmState;
COX_STATE_FITS(OlmState);

static const CoxStats g_head = {
    .hp = 800, .att = 250, .str = 250, .def = 150, .mag = 250, .rng = 250,
    .attack_bonus = {0, 0, 0, 60, 60}, .defence_bonus = {200, 200, 200, 50, 200},
    .max_hit = 27, .attack_speed = 4, .style = COMBAT_MAGIC,
};
static const CoxStats g_melee_claw = {
    .hp = 600, .att = 250, .str = 250, .def = 175, .mag = 175, .rng = 250,
    .defence_bonus = {50, 50, 50, 200, 200}, .style = COMBAT_NONE,
    .flags = COX_STATS_MAGIC_DEFENSIVE,
};
static const CoxStats g_mage_claw = {
    .hp = 600, .att = 250, .str = 250, .def = 175, .mag = 87, .rng = 250,
    .defence_bonus = {200, 200, 200, 200, 50}, .style = COMBAT_NONE,
    .flags = COX_STATS_MAGIC_DEFENSIVE,
};

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static bool in_arena(int x, int y) {
    return x >= ARENA_MIN_X && x <= ARENA_MAX_X && y >= ARENA_MIN_Y
        && y <= ARENA_MAX_Y;
}

static bool player_in_arena(const RcWorld *w) {
    return w->player.plane == 0 && in_arena(w->player.x, w->player.y);
}

static void set_form(RcNpc *npc, int npc_id) {
    int idx = rc_npc_def_find(npc_id);
    if (idx >= 0) rc_npc_set_form(npc, idx);
}

static void wall_tiles(bool west, int role, int *x, int *y) {
    // The head sits one tile further back than the claws; facing Olm the
    // melee claw is always on the raider's right.
    if (role == ROLE_HEAD) {
        *x = west ? 3222 : 3239;
        *y = 5738;
    } else {
        bool south = (role == ROLE_MELEE) != west;
        *x = west ? 3223 : 3238;
        *y = south ? 5733 : 5743;
    }
}

static int random_power(RcWorld *w, OlmState *s) {
    // A power does not repeat until all three have been used.
    if ((s->powers_used & 7) == 7) s->powers_used = 0;
    int options[3], n = 0;
    for (int p = 0; p < 3; p++)
        if (!(s->powers_used & (1 << p))) options[n++] = p;
    int p = options[cox_rand(w, n)];
    s->powers_used |= (uint8_t)(1 << p);
    return p;
}

static bool final_phase(const OlmState *s) { return s->phase > s->claw_phases; }
static bool penultimate(const OlmState *s) { return s->phase == s->claw_phases; }

static void rise(RcWorld *w, CoxRaid *raid, CoxRoom *room, OlmState *s) {
    s->stage = OLM_RISING;
    s->stage_tick = w->tick;
    s->melee_down = s->mage_down = false;
    s->used_attacks = 0;
    s->special = SPECIAL_BURST;
    s->step = STEP_BASIC1;
    s->clench_until = s->heal_until = s->claw_down_tick = 0;
    static const char *const names[3] = {"acid", "flame", "crystal"};
    if (!final_phase(s)) {
        s->power = (uint8_t)random_power(w, s);
        cox_message(w, "The Great Olm rises with the power of %s.", names[s->power]);
    } else {
        cox_message(w, "The Great Olm is giving its all. This is its final stand.");
    }
    int x, y;
    wall_tiles(s->west, ROLE_HEAD, &x, &y);
    RcNpc *head = cox_room_npc(w, room, ROLE_HEAD);
    if (!head)
        head = cox_spawn(w, raid, room, NPC_HEAD_SPAWNING, x, y, ROLE_HEAD, &g_head, true);
    if (head) {
        head->x = head->prev_x = head->spawn_x = x;
        head->y = head->prev_y = head->spawn_y = y;
        set_form(head, NPC_HEAD_SPAWNING);
        head->player_untargetable = true;
        // The head's max hit rises in the penultimate and final phases.
        int base = g_head.max_hit;
        head->max_hit_override = (uint16_t)(base + (penultimate(s) ? 1 : 0)
                                            + (final_phase(s) ? 2 : 0));
    }
    if (final_phase(s)) return;
    // Every phase begins with fresh claws (all stats reset).
    static const int roles[2] = {ROLE_MELEE, ROLE_MAGE};
    for (int k = 0; k < 2; k++) {
        RcNpc *old = cox_room_npc(w, room, roles[k]);
        if (old) rc_npc_remove(w, old->uid);
        wall_tiles(s->west, roles[k], &x, &y);
        RcNpc *claw = cox_spawn(w, raid, room, k ? NPC_MAGE_CLAW_SPAWNING
                                : NPC_MELEE_CLAW_SPAWNING, x, y, roles[k],
                                k ? &g_mage_claw : &g_melee_claw, true);
        if (claw) claw->player_untargetable = true;
    }
}

static void olm_enter(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    (void)w;
    (void)raid;
    OlmState *s = COX_ROOM_STATE(OlmState, room);
    if (!s->claw_phases) {
        s->claw_phases = 3;        // 3 + floor(party size / 8)
        s->auto_style = COMBAT_MAGIC;
        s->facing = VIEW_MIDDLE;
    }
}

// The views the head can face: rows of the arena, spanning its width.
static bool in_view(int view, int y) {
    switch (view) {
    case VIEW_SOUTH: return y >= 5730 && y <= 5740;
    case VIEW_MIDDLE: return y >= 5735 && y <= 5745;
    default: return y >= 5740 && y <= 5748;
    }
}

static int best_view(const RcWorld *w) {
    int y = w->player.y;
    if (in_view(VIEW_MIDDLE, y)) return VIEW_MIDDLE;
    return in_view(VIEW_SOUTH, y) ? VIEW_SOUTH : VIEW_NORTH;
}

static void random_arena_tile(RcWorld *w, int *x, int *y) {
    *x = ARENA_MIN_X + cox_rand(w, ARENA_MAX_X - ARENA_MIN_X + 1);
    *y = ARENA_MIN_Y + cox_rand(w, ARENA_MAX_Y - ARENA_MIN_Y + 1);
}

static void olm_strike(RcWorld *w, CoxRaid *raid, RcNpc *src, int x, int y,
                       int radius, int min, int max, int edge_min, int edge_max,
                       int delay, const char *key) {
    CoxHazard h = {.kind = COX_HAZARD_STRIKE, .radius = (uint8_t)radius,
                   .style = COMBAT_NONE, .x = (int16_t)x, .y = (int16_t)y,
                   .plane = 0, .land_tick = w->tick + (RcTick)delay,
                   .min = (uint16_t)min, .max = (uint16_t)max,
                   .edge_min = (uint16_t)edge_min, .edge_max = (uint16_t)edge_max,
                   .source = src ? (RcNpcId)src->uid : COX_NO_NPC, .loc_id = -1};
    cox_add_hazard(raid, &h);
    if (src && key)
        rc_combat_record_encounter_action(w, src, false, x, y, COMBAT_NONE, delay, key);
}

static void acid_pool(RcWorld *w, CoxRaid *raid, RcNpc *head, int x, int y,
                      int ticks) {
    uint64_t key = 0;
    cox_add_loc(w, LOC_ACID_POOL, 22, 0, x, y, 0, &key);
    CoxHazard h = {.kind = COX_HAZARD_POOL, .radius = 0, .style = COMBAT_NONE,
                   .flags = COX_HAZARD_POISON, .x = (int16_t)x, .y = (int16_t)y,
                   .plane = 0, .land_tick = w->tick + 2,
                   .expire_tick = w->tick + 2 + (RcTick)ticks, .min = 3, .max = 6,
                   .source = head ? (RcNpcId)head->uid : COX_NO_NPC,
                   .loc_id = key ? LOC_ACID_POOL : -1, .loc_key = key};
    cox_add_hazard(raid, &h);
}

// ---------------------------------------------------------------------------
// Head attacks.

static void auto_attack(RcWorld *w, OlmState *s, RcNpc *head) {
    // Spheres now and then: the matching prayer saves the raider, and any
    // protection prayer up when one is cast is switched off and sapped.
    RcPlayer *p = &w->player;
    if (!s->sphere_tick && cox_rand(w, 6) == 0) {
        static const uint8_t styles[3] = {COMBAT_MELEE_CRUSH, COMBAT_RANGED,
                                          COMBAT_MAGIC};
        s->sphere_style = styles[cox_rand(w, 3)];
        s->sphere_tick = w->tick + 6;
        uint32_t protect = PRAYER_PROTECT_MELEE | PRAYER_PROTECT_RANGE
                         | PRAYER_PROTECT_MAGIC;
        bool sapped = (p->active_prayers & protect) != 0;
        if (sapped) {
            rc_prayer_disable_all(w, RC_PRAYER_FORCED_OFF);
            p->current_prayer_points /= 2;
        }
        // Red needs Protect from Melee, green Missiles, purple Magic.
        static const char *const keys[3] = {"Sphere melee", "Sphere ranged",
                                            "Sphere magic"};
        static const char *const kinds[3] = {"aggression", "accuracy and dexterity",
                                             "magical power"};
        int c = s->sphere_style == COMBAT_RANGED ? 1 : s->sphere_style == COMBAT_MAGIC ? 2 : 0;
        cox_message(w, sapped ? "The Great Olm fires a sphere of %s your way. "
                                "Your prayers have been sapped."
                              : "The Great Olm fires a sphere of %s your way.",
                    kinds[c]);
        rc_combat_record_encounter_action(w, head, true, p->x, p->y,
                                          (RcCombatStyle)s->sphere_style, 6, keys[c]);
        return;
    }
    // Protection prayer cuts about 75-80% of an auto.
    RcNpcScriptedAttack a = {.style = (RcCombatStyle)s->auto_style,
        .max_hit = rc_npc_max_hit(head, rc_npc_def_for_npc(w, head)),
        .delay = 3, .protected_pct = 22,
        .key = s->auto_style == COMBAT_RANGED ? "Ranged" : "Magic"};
    rc_combat_npc_scripted_attack(w, head, &a);
    // One in five head attacks switches the style.
    if (cox_rand(w, 5) == 0)
        s->auto_style = s->auto_style == COMBAT_RANGED ? COMBAT_MAGIC : COMBAT_RANGED;
}

static bool attack_available(const OlmState *s, int attack) {
    if (final_phase(s)) return attack != ATK_SIPHON || !s->siphon_cooldown;
    bool all = penultimate(s);
    switch (attack) {
    case ATK_ACID_SPRAY: case ATK_ACID_DRIP: return all || s->power == POWER_ACID;
    case ATK_DEEP_BURN: case ATK_FIRE_WALL: return all || s->power == POWER_FLAME;
    case ATK_FALLING_CRYSTALS: case ATK_CRYSTAL_BOMBS:
        return all || s->power == POWER_CRYSTAL;
    default: return false;
    }
}

static void fire_wall(RcWorld *w, CoxRaid *raid, CoxRoom *room, OlmState *s,
                      RcNpc *head) {
    int y = w->player.y;
    if (y - 1 < ARENA_MIN_Y || y + 1 > ARENA_MAX_Y) return;
    int f = 0;
    for (int row = -1; row <= 1; row += 2) {
        for (int x = ARENA_MIN_X; x <= ARENA_MAX_X && f < FIRES; x++, f++) {
            // Fires offer Douse; they have no Attack option.
            cox_spawn(w, raid, room, NPC_FIRE, x, y + row, ROLE_FIRE + f, NULL, true);
        }
    }
    s->wall_tick = w->tick + 8;
    s->wall_y = (int16_t)y;
    rc_combat_record_encounter_action(w, head, false, w->player.x, y + 1,
                                      COMBAT_NONE, 2, "Fire wall");
}

static void power_attack(RcWorld *w, CoxRaid *raid, CoxRoom *room, OlmState *s,
                         RcNpc *head) {
    int options[ATK_COUNT], n = 0;
    for (int a = 0; a < ATK_COUNT; a++)
        if (attack_available(s, a) && !(s->used_attacks & (1 << a))) options[n++] = a;
    if (!n) {
        s->used_attacks = 0;
        for (int a = 0; a < ATK_COUNT; a++)
            if (attack_available(s, a)) options[n++] = a;
    }
    if (!n) {
        auto_attack(w, s, head);
        return;
    }
    int attack = options[cox_rand(w, n)];
    s->used_attacks |= (uint16_t)(1 << attack);
    RcPlayer *p = &w->player;
    switch (attack) {
    case ATK_ACID_SPRAY: {
        int pools = 7 + cox_rand(w, 4);
        for (int i = 0; i < pools; i++) {
            int x, y;
            random_arena_tile(w, &x, &y);
            acid_pool(w, raid, head, x, y, 24);
        }
        rc_combat_record_encounter_action(w, head, false, p->x, p->y, COMBAT_NONE,
                                          2, "Acid spray");
        break;
    }
    case ATK_ACID_DRIP:
        cox_message(w, "The Great Olm has smothered you in acid. It starts to drip off slowly.");
        s->drip_until = w->tick + 20;
        s->drip_x = (int16_t)p->x;
        s->drip_y = (int16_t)p->y;
        break;
    case ATK_DEEP_BURN:
        s->burn_hits = 6;
        s->next_burn = w->tick + 3;
        rc_combat_record_encounter_action(w, head, true, p->x, p->y, COMBAT_NONE, 3,
                                          "Deep burn");
        break;
    case ATK_FIRE_WALL:
        fire_wall(w, raid, room, s, head);
        break;
    case ATK_FALLING_CRYSTALS:
        cox_message(w, "The Great Olm sounds a cry...");
        s->crystals_left = 11;
        s->next_crystal = w->tick;
        break;
    case ATK_CRYSTAL_BOMBS:
        for (int i = 0; i < 2; i++) {
            int x, y;
            random_arena_tile(w, &x, &y);
            uint64_t key = 0;
            cox_add_loc(w, LOC_CRYSTAL_BOMB, 10, 0, x, y, 0, &key);
            CoxHazard h = {.kind = COX_HAZARD_STRIKE, .radius = 3, .style = COMBAT_NONE,
                           .x = (int16_t)x, .y = (int16_t)y, .plane = 0,
                           .land_tick = w->tick + 8, .min = 60, .max = 60,
                           .edge_min = 15, .edge_max = 45,
                           .source = (RcNpcId)head->uid,
                           .loc_id = key ? LOC_CRYSTAL_BOMB : -1, .loc_key = key};
            cox_add_hazard(raid, &h);
            rc_combat_record_encounter_action(w, head, false, x, y, COMBAT_NONE, 3,
                                              "Crystal bomb");
        }
        break;
    case ATK_SIPHON:
        for (int i = 0; i < 2; i++) {
            int x, y;
            random_arena_tile(w, &x, &y);
            s->pool_x[i] = (int16_t)x;
            s->pool_y[i] = (int16_t)y;
            rc_combat_record_encounter_action(w, head, false, x, y, COMBAT_NONE, 2,
                                              "Life siphon");
        }
        s->siphon_tick = w->tick + 8;
        s->siphon_cooldown = 10;
        break;
    }
}

// ---------------------------------------------------------------------------
// The left claw's generic specials.

static void special(RcWorld *w, CoxRaid *raid, OlmState *s, RcNpc *claw) {
    RcPlayer *p = &w->player;
    int kind = s->special;
    int count = penultimate(s) ? 4 : 3;
    s->special = (uint8_t)((s->special + 1) % count);
    switch (kind) {
    case SPECIAL_BURST:
        // A seedling under the raider grows into a crystal 3 ticks later.
        s->burst_tick = w->tick + 3;
        s->burst_x = (int16_t)p->x;
        s->burst_y = (int16_t)p->y;
        cox_add_loc(w, LOC_SEEDLING, 10, 0, p->x, p->y, 0, NULL);
        rc_combat_record_encounter_action(w, claw, false, p->x, p->y, COMBAT_NONE,
                                          3, "Crystal burst");
        break;
    case SPECIAL_LIGHTNING: {
        // Bolts down distinct columns, from either end.
        int bolts = penultimate(s) ? 5 : 4;
        bool used[10] = {0};
        for (int i = 0; i < bolts; i++) {
            int col;
            do col = cox_rand(w, 10); while (used[col]);
            used[col] = true;
            bool north = cox_rand(w, 2);
            s->bolt[i].x = (int16_t)(ARENA_MIN_X + col);
            s->bolt[i].y = (int16_t)(north ? ARENA_MAX_Y : ARENA_MIN_Y);
            s->bolt[i].dy = (int8_t)(north ? -1 : 1);
            s->bolt[i].live = true;
        }
        rc_combat_record_encounter_action(w, claw, false, p->x, p->y, COMBAT_NONE,
                                          1, "Lightning");
        break;
    }
    case SPECIAL_TELEPORT: {
        // Solo: a portal within 10 tiles; the raider is pulled to it.
        int x, y;
        random_arena_tile(w, &x, &y);
        s->portal_x = (int16_t)x;
        s->portal_y = (int16_t)y;
        s->teleport_tick = w->tick + 8;
        rc_combat_record_encounter_action(w, claw, false, x, y, COMBAT_NONE, 8,
                                          "Teleport");
        break;
    }
    default:
        s->heal_until = w->tick + 16;
        rc_combat_record_encounter_action(w, claw, false, claw->x, claw->y,
                                          COMBAT_NONE, 0, "Heal");
        break;
    }
    (void)raid;
}

static void head_step(RcWorld *w, CoxRaid *raid, CoxRoom *room, OlmState *s,
                      RcNpc *head) {
    int step = s->step;
    s->step = (uint8_t)((s->step + 1) % 4);
    if (step == STEP_NULL) s->null_tick = w->tick;
    // It acts only when the raider is in the view it faces; otherwise it
    // turns and loses the step (a skipped basic is owed once).
    int view = best_view(w);
    if (!player_in_arena(w) || !in_view(s->facing, w->player.y)) {
        s->facing = (uint8_t)view;
        if (step == STEP_BASIC1 || step == STEP_BASIC2) s->owed_basic = true;
        if (step == STEP_SPECIAL && !final_phase(s)) {
            int count = penultimate(s) ? 4 : 3;
            s->special = (uint8_t)((s->special + 1) % count);
        }
        return;
    }
    if (s->siphon_cooldown) s->siphon_cooldown--;
    if (step == STEP_BASIC1 || step == STEP_BASIC2 || s->owed_basic) {
        if (s->owed_basic && step != STEP_BASIC1 && step != STEP_BASIC2)
            s->owed_basic = false;
        // A basic is replaced by a power attack 1 time in 3 (1 in 2 in the
        // final phase).
        if (cox_rand(w, final_phase(s) ? 2 : 3) == 0)
            power_attack(w, raid, room, s, head);
        else
            auto_attack(w, s, head);
        if (step == STEP_BASIC1 || step == STEP_BASIC2) s->owed_basic = false;
    }
    if (step == STEP_SPECIAL && !final_phase(s)) {
        RcNpc *claw = cox_room_npc(w, room, ROLE_MELEE);
        if (claw && !s->melee_down && w->tick >= s->clench_until)
            special(w, raid, s, claw);
        else
            s->special = (uint8_t)((s->special + 1) % (penultimate(s) ? 4 : 3));
    }
}

// ---------------------------------------------------------------------------
// Ongoing mechanics.

static void tick_mechanics(RcWorld *w, CoxRaid *raid, CoxRoom *room, OlmState *s,
                           RcNpc *head) {
    RcPlayer *p = &w->player;
    RcTick now = w->tick;
    if (s->sphere_tick && now >= s->sphere_tick) {
        s->sphere_tick = 0;
        if (rc_combat_apply_protection(1, s->sphere_style, p->active_prayers, true))
            rc_combat_damage_player(w, p->current_hp / 20, 0,
                                    (RcCombatStyle)s->sphere_style,
                                    head ? (int)head->uid : RC_HIT_SOURCE_STATUS);
    }
    if (s->drip_until) {
        if (now >= s->drip_until) {
            s->drip_until = 0;
        } else if (p->x != s->drip_x || p->y != s->drip_y) {
            acid_pool(w, raid, head, s->drip_x, s->drip_y, 16);
            s->drip_x = (int16_t)p->x;
            s->drip_y = (int16_t)p->y;
        } else {
            rc_combat_damage_player(w, 3 + cox_rand(w, 4), 0, COMBAT_NONE,
                                    RC_HIT_SOURCE_STATUS);
        }
    }
    if (s->burn_hits && now >= s->next_burn) {
        // Deep burn: 5 damage and -2 to every combat stat, every 8 ticks.
        s->burn_hits--;
        s->next_burn = now + 8;
        rc_combat_damage_player(w, 5, 0, COMBAT_NONE, RC_HIT_SOURCE_STATUS);
        static const int stats[5] = {SKILL_ATTACK, SKILL_STRENGTH, SKILL_DEFENCE,
                                     SKILL_RANGED, SKILL_MAGIC};
        for (int i = 0; i < 5; i++) {
            int *lv = &p->skills.boosted_level[stats[i]];
            *lv = *lv > 2 ? *lv - 2 : 1;
        }
    }
    if (s->wall_tick) {
        for (int f = 0; f < FIRES; f++) {
            RcNpc *fire = cox_room_npc(w, room, ROLE_FIRE + f);
            // Status damage: the fires go out with the wall.
            if (fire && fire->x == p->x && fire->y == p->y)
                rc_combat_damage_player(w, 8 + cox_rand(w, 3), 0, COMBAT_NONE,
                                        RC_HIT_SOURCE_STATUS);
        }
        if (now >= s->wall_tick) {
            s->wall_tick = 0;
            if (p->y == s->wall_y && player_in_arena(w))
                rc_combat_damage_player(w, 50 + cox_rand(w, 16), 0, COMBAT_NONE,
                                        head ? (int)head->uid : RC_HIT_SOURCE_STATUS);
            for (int f = 0; f < FIRES; f++) {
                RcNpc *fire = cox_room_npc(w, room, ROLE_FIRE + f);
                if (fire) rc_npc_remove(w, fire->uid);
            }
        }
    }
    if (s->crystals_left && now >= s->next_crystal) {
        // One crystal every 2 ticks at the raider's tile, landing 2 later.
        s->crystals_left--;
        s->next_crystal = now + 2;
        olm_strike(w, raid, head, p->x, p->y, 0, 15, 20, 0, 0, 2, "Falling crystal");
    }
    for (int i = 0; i < 6; i++) {
        if (!s->bolt[i].live) continue;
        if (s->bolt[i].x == p->x && s->bolt[i].y == p->y) {
            rc_combat_damage_player(w, 10 + cox_rand(w, 11), 0, COMBAT_NONE,
                                    RC_HIT_SOURCE_STATUS);
            rc_prayer_disable_all(w, RC_PRAYER_FORCED_OFF);
            rc_player_apply_freeze(w, 3);
            s->bolt[i].live = false;
            continue;
        }
        s->bolt[i].y = (int16_t)(s->bolt[i].y + s->bolt[i].dy);
        if (s->bolt[i].y < ARENA_MIN_Y || s->bolt[i].y > ARENA_MAX_Y)
            s->bolt[i].live = false;
    }
    if (s->burst_tick && now >= s->burst_tick) {
        s->burst_tick = 0;
        cox_remove_loc(w, LOC_SEEDLING, s->burst_x, s->burst_y, 0);
        if (p->x == s->burst_x && p->y == s->burst_y) {
            rc_combat_damage_player(w, 30, 0, COMBAT_NONE, RC_HIT_SOURCE_STATUS);
            int x = p->x + (p->x < 3233 ? 1 : -1);
            if (cox_tile_walkable(w, x, p->y, 0)) cox_relocate(w, raid, x, p->y, 0);
        }
        uint64_t key = 0;
        if (cox_add_loc(w, LOC_LARGE_CRYSTAL, 10, 0, s->burst_x, s->burst_y, 0, &key))
            rc_world_object_expire(w, key, now + 12);
    }
    if (s->teleport_tick && now >= s->teleport_tick) {
        s->teleport_tick = 0;
        if (p->x != s->portal_x || p->y != s->portal_y) {
            int d = cox_chebyshev(p->x, p->y, 1, s->portal_x, s->portal_y, 1);
            cox_relocate(w, raid, s->portal_x, s->portal_y, 0);
            rc_combat_damage_player(w, 1 + 43 * d / 10, 1, COMBAT_NONE,
                                    RC_HIT_SOURCE_STATUS);
        }
    }
    if (s->siphon_tick && now >= s->siphon_tick) {
        // Off the pools at the check: up to 18, healed to Olm five-fold.
        s->siphon_tick = 0;
        bool safe = false;
        for (int i = 0; i < 2; i++)
            safe |= p->x == s->pool_x[i] && p->y == s->pool_y[i];
        if (!safe && head && player_in_arena(w)) {
            int damage = 1 + cox_rand(w, 18);
            rc_combat_damage_player(w, damage, 0, COMBAT_NONE, (int)head->uid);
            head->current_hp = clamp(head->current_hp + 5 * damage, 0, head->spawn_hp);
        }
    }
}

// Between phases crystals shower down: a volley every 4 ticks, one at a
// random tile and one on the raider, 3x3.
static void crystal_shower(RcWorld *w, CoxRaid *raid, OlmState *s, RcNpc *src) {
    if (w->tick < s->next_shower) return;
    s->next_shower = w->tick + 4;
    int x, y;
    random_arena_tile(w, &x, &y);
    olm_strike(w, raid, src, x, y, 1, 16, 25, 10, 16, 2, "Shower");
    if (player_in_arena(w))
        olm_strike(w, raid, src, w->player.x, w->player.y, 1, 16, 25, 10, 16, 4,
                   "Shower");
}

static void claw_died(RcWorld *w, CoxRaid *raid, CoxRoom *room, OlmState *s,
                      int role) {
    (void)raid;
    (void)room;
    if (role == ROLE_MELEE) s->melee_down = true;
    if (role == ROLE_MAGE) {
        s->mage_down = true;
        s->clench_until = 0;   // no more clenching this phase
    }
    if (!s->claw_down_tick) s->claw_down_tick = w->tick;
    if (s->melee_down && s->mage_down) {
        s->stage = OLM_SINKING;
        s->stage_tick = w->tick;
    }
}

static void olm_tick(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    OlmState *s = COX_ROOM_STATE(OlmState, room);
    RcTick now = w->tick;
    RcNpc *head = cox_room_npc(w, room, ROLE_HEAD);
    switch (s->stage) {
    case OLM_WAIT:
        return;
    case OLM_RISING:
        if (now >= s->stage_tick + 5) {
            if (head) {
                set_form(head, NPC_HEAD);
                head->player_untargetable = !final_phase(s);
            }
            RcNpc *melee = cox_room_npc(w, room, ROLE_MELEE);
            RcNpc *mage = cox_room_npc(w, room, ROLE_MAGE);
            if (melee && !final_phase(s)) {
                set_form(melee, NPC_MELEE_CLAW);
                melee->player_untargetable = false;
            }
            if (mage && !final_phase(s)) {
                set_form(mage, NPC_MAGE_CLAW);
                mage->player_untargetable = false;
            }
            s->stage = OLM_ACTIVE;
            s->next_step = now + (s->phase == 1 ? 4 : 8);
        }
        return;
    case OLM_ACTIVE:
        if (!head) return;
        tick_mechanics(w, raid, room, s, head);
        if (now >= s->next_step) {
            s->next_step = now + 4;
            head_step(w, raid, room, s, head);
        }
        // Penultimate phase: a claw left down for about 30 seconds returns
        // at full health.
        if (penultimate(s) && s->claw_down_tick && !(s->melee_down && s->mage_down)
                && now >= s->claw_down_tick + 50) {
            int role = s->melee_down ? ROLE_MELEE : ROLE_MAGE;
            int x, y;
            wall_tiles(s->west, role, &x, &y);
            RcNpc *claw = cox_spawn(w, raid, room, role == ROLE_MELEE ? NPC_MELEE_CLAW
                                    : NPC_MAGE_CLAW, x, y, role,
                                    role == ROLE_MELEE ? &g_melee_claw : &g_mage_claw,
                                    true);
            (void)claw;
            if (role == ROLE_MELEE) s->melee_down = false;
            else s->mage_down = false;
            s->claw_down_tick = 0;
        }
        return;
    case OLM_SINKING:
        // The head sinks a few ticks after the last claw, then the shower.
        tick_mechanics(w, raid, room, s, head);
        if (now >= s->stage_tick + 4) {
            if (head) {
                set_form(head, NPC_HEAD_SPAWNING);
                head->player_untargetable = true;
            }
            s->stage = OLM_SHOWER;
            s->stage_tick = now;
            s->next_shower = now + 6;
        }
        return;
    case OLM_SHOWER:
        tick_mechanics(w, raid, room, s, head);
        if (now <= s->stage_tick + 34) crystal_shower(w, raid, s, head);
        if (now >= s->stage_tick + 34) {
            s->phase++;
            // It switches walls after each claw phase; the final stand stays
            // on the penultimate phase's wall.
            if (!final_phase(s)) s->west = !s->west;
            rise(w, raid, room, s);
        }
        return;
    default:
        return;
    }
}

static void olm_died(RcWorld *w, CoxRaid *raid, CoxRoom *room, RcNpc *npc,
                     int role) {
    (void)npc;
    OlmState *s = COX_ROOM_STATE(OlmState, room);
    if (role == ROLE_MELEE || role == ROLE_MAGE) {
        claw_died(w, raid, room, s, role);
        return;
    }
    if (role != ROLE_HEAD || !final_phase(s)) return;
    s->stage = OLM_DONE;
    raid->olm_dead = true;
    raid->finish_tick = w->tick;
    cox_remove_loc(w, LOC_EXIT_CRYSTAL, 3232, 5749, 0);
    cox_replace_loc(w, LOC_REWARD_CRYSTAL, 3233, 5751, 0, LOC_REWARD_CHEST);
    for (int i = 0; i < COX_MAX_HAZARDS; i++) memset(&raid->hazards[i], 0,
                                                    sizeof(raid->hazards[i]));
    cox_message(w, "As the Great Olm collapses, the crystal blocking your exit has been shattered.");
    unsigned long long secs = (unsigned long long)(w->tick - raid->start_tick) * 6 / 10;
    cox_message(w, "Congratulations - your raid is complete! Team size: Solo Duration: %llu:%02llu",
                secs / 60, secs % 60);
    cox_complete_room(w, raid, room);
}


// Pass the barrier to start the fight; there is no way back until Olm dies.
static RcInteractionHandlerResult pass_barrier(RcWorld *world, RcPlayer *player,
                                               const RcPendingInteraction *pending,
                                               void *ctx) {
    (void)pending;
    (void)ctx;
    CoxRaid *raid = cox_raid(world);
    if (!raid || !raid->in_raid || raid->olm_room >= raid->room_count)
        return rc_interaction_result_message("Nothing interesting happens.");
    CoxRoom *room = &raid->rooms[raid->olm_room];
    OlmState *s = COX_ROOM_STATE(OlmState, room);
    if (player->y > BARRIER_Y) {
        if (!raid->olm_dead)
            return rc_interaction_result_message(
                "You can't leave the chamber while the Great Olm lives.");
        cox_relocate(world, raid, player->x, BARRIER_Y - 1, 0);
        return rc_interaction_result_complete();
    }
    int x = player->x < 3231 ? 3231 : player->x > 3234 ? 3234 : player->x;
    cox_relocate(world, raid, x, BARRIER_Y + 1, 0);
    if (s->stage == OLM_WAIT && !raid->olm_dead) {
        if (!s->claw_phases) olm_enter(world, raid, room);
        s->phase = 1;
        s->west = cox_rand(world, 2) == 1;
        room->flags |= COX_ROOM_STARTED;
        room->started_tick = world->tick;
        rise(world, raid, room, s);
    }
    return rc_interaction_result_complete();
}

static RcInteractionHandlerResult search_chest(RcWorld *world, RcPlayer *player,
                                               const RcPendingInteraction *pending,
                                               void *ctx) {
    (void)world;
    (void)player;
    (void)pending;
    (void)ctx;
    return rc_interaction_result_message("The chest is empty.");
}

static RcInteractionHandlerResult douse(RcWorld *world, RcPlayer *player,
                                        const RcPendingInteraction *pending,
                                        void *ctx) {
    (void)player;
    (void)ctx;
    RcNpc *fire = rc_npc_resolve(world, (RcNpcId)pending->target.entity_uid);
    if (!fire) return rc_interaction_result_message("Nothing interesting happens.");
    if (pending->op != RC_INTERACTION_USE_ON && !cox_has_item(player, ITEM_GOURD_WATER))
        return rc_interaction_result_message("You need some water to douse the fire.");
    if (!cox_swap_items(world, ITEM_GOURD_WATER, ITEM_GOURD_EMPTY, 1))
        return rc_interaction_result_message("You need some water to douse the fire.");
    rc_npc_remove(world, fire->uid);
    return rc_interaction_result_complete();
}

// Head and claws take a third from everything but their weakness; a
// clenched left claw takes nothing.
int cox_olm_modify_hit(RcWorld *world, CoxRoom *room, int role,
                       RcCombatStyle style, int damage) {
    OlmState *s = COX_ROOM_STATE(OlmState, room);
    bool melee = style >= COMBAT_MELEE_STAB && style <= COMBAT_MELEE_CRUSH;
    switch (role) {
    case ROLE_HEAD: return style == COMBAT_RANGED ? damage : damage / 3;
    case ROLE_MAGE: return style == COMBAT_MAGIC ? damage : damage / 3;
    case ROLE_MELEE:
        if (world->tick < s->clench_until) return 0;
        return melee ? damage : damage / 3;
    default: return 0;
    }
}

static void olm_hit(RcWorld *w, CoxRaid *raid, CoxRoom *room, RcNpc *npc,
                    int role, int damage) {
    (void)raid;
    OlmState *s = COX_ROOM_STATE(OlmState, room);
    if (role != ROLE_MELEE || s->stage != OLM_ACTIVE) return;
    // Penultimate heal: damage to the left claw heals it for two attacks.
    if (w->tick < s->heal_until) {
        npc->current_hp = clamp(npc->current_hp + 2 * damage, 1, npc->spawn_hp);
        return;
    }
    // A single hit of at least 1/20 of its base HP between the null and the
    // special makes it clench for about 45 ticks (not in the penultimate
    // phase, and never once the mage claw has fallen).
    if (!penultimate(s) && !s->mage_down && damage * 20 >= 600
            && s->null_tick && w->tick <= s->null_tick + 8 && w->tick >= s->clench_until) {
        s->clench_until = w->tick + 45;
        cox_message(w, "The Great Olm's left claw clenches to protect itself temporarily.");
    }
}

const CoxRoomOps cox_olm_ops = {
    .enter = olm_enter, .tick = olm_tick, .npc_died = olm_died, .npc_hit = olm_hit,
};

int cox_olm_register(RcWorld *world) {
    int ok = 1;
    ok &= cox_register_object(world, LOC_BARRIER, 0, pass_barrier);
    ok &= cox_register_object(world, LOC_BARRIER, 1, pass_barrier);
    ok &= cox_register_object(world, LOC_REWARD_CHEST, 0, search_chest);
    ok &= cox_register_npc(world, NPC_FIRE, 0, douse);
    RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
    key.kind = RC_INTERACTION_NPC;
    key.op = RC_INTERACTION_USE_ON;
    key.definition_id = NPC_FIRE;
    key.source_item_id = ITEM_GOURD_WATER;
    ok &= rc_interaction_register_world_handler(world, &key, douse, NULL) != 0;
    return ok;
}
