#include "cox.h"

#include "../../rc-core/api.h"
#include "../../rc-core/combat.h"
#include "../../rc-core/combat_formula.h"
#include "../../rc-core/combat_hit.h"
#include "../../rc-core/consumables.h"
#include "../../rc-core/interaction.h"
#include "../../rc-core/items.h"
#include "../../rc-core/object_runtime.h"
#include "../../rc-core/pathfinding.h"
#include "../../rc-core/prayer.h"
#include "../../rc-core/spells.h"

#include <stdio.h>
#include <string.h>

// Combat rooms: Tekton, Vanguards, Vasa Nistirio, Guardians, Lizardman
// shamans, Skeletal mystics, Muttadiles and Vespula. Every boss is an
// owner-controlled NPC this module drives: its states, attacks, hazards and
// the room's exit and entrance gates. Stats are the wiki's solo values
// scaled by cox_scale; timings follow the OSRS Wiki and packet-capture
// notes cited in rc-content/README.md.

enum {
    ITEM_STINKHORN = 20910,
    ITEM_JUICE = 20911,
    ITEM_SEED_NOXIFER = 20903,
    ITEM_SEED_GOLPAR = 20906,
    ITEM_SEED_BUCHU = 20909,
    ITEM_BIG_BONES = 532,
    ITEM_BLOSSOM = 20892,
    // (+)(4) potions.
    ITEM_ELDER_P = 20924,
    ITEM_TWISTED_P = 20936,
    ITEM_KODAI_P = 20948,
    ITEM_REVITALISATION_P = 20960,
    ITEM_PRAYER_ENHANCE_P = 20972,
    ITEM_XERICS_AID_P = 20984,
    ITEM_OVERLOAD_P = 20996,
};

enum {
    LOC_PASSAGE = 29789,
    LOC_CRYSTAL_ORANGE = 30017,
    LOC_CRYSTAL_GREEN = 30018,
    LOC_CRYSTAL_PURPLE_SMALL = 30016,
    LOC_INVISIBLE_BLOCK = 26209,
    LOC_MARK_OF_POWER = 29741,
    LOC_BOIL = 30070,
    LOC_TEKTON_FIRE = 30021,
    LOC_VASA_FIRE = 30019,
    LOC_MUTT_TENDRILS = 29767,
    LOC_SHAMAN_TENDRILS = 29768,
    LOC_VESPULA_TENDRILS = 30024,
    LOC_VASA_CRYSTAL_OFF = 29775,
    LOC_VASA_CRYSTAL_ON = 29774,
    LOC_MEAT_TREE_FULL = 30012,
    LOC_MEAT_TREE_EMPTY = 30013,
    LOC_ROOT_FLOWERED = 30068,
    LOC_ROOT_PICKED = 30069,
    LOC_PORTAL_OPEN = 30072,
    LOC_PORTAL_CLOSED = 30073,
    LOC_SACRIFICIAL_TABLE = 29863,
};

// ---------------------------------------------------------------------------
// Shared room helpers.

// The room's exit gate (blocker beside the exit passage, room side) and its
// entrance gate (fire or tendrils raised when the fight starts).
typedef struct {
    int16_t exit_x, exit_y;
    int16_t exit_loc;
    uint8_t exit_size;          // 1: two 1x1 locs side by side, 2: one 2x2
    bool exit_placed, exit_open;
    int16_t entry_loc;
    bool entry_placed;
    uint8_t entry_crossings;
} CoxGate;

static int npc_size(const RcWorld *w, const RcNpc *npc) {
    const RcNpcDef *def = rc_npc_def_for_npc(w, npc);
    return def && def->size > 0 ? def->size : 1;
}

static bool player_adjacent(const RcWorld *w, const RcNpc *npc) {
    int s = npc_size(w, npc), px = w->player.x, py = w->player.y;
    if (w->player.plane != npc->plane) return false;
    bool x_in = px >= npc->x && px < npc->x + s;
    bool y_in = py >= npc->y && py < npc->y + s;
    return (x_in && (py == npc->y - 1 || py == npc->y + s))
        || (y_in && (px == npc->x - 1 || px == npc->x + s));
}

static bool player_under(const RcWorld *w, const RcNpc *npc) {
    int s = npc_size(w, npc), px = w->player.x, py = w->player.y;
    return w->player.plane == npc->plane && px >= npc->x && px < npc->x + s
        && py >= npc->y && py < npc->y + s;
}

static bool has_los(const RcWorld *w, const RcNpc *npc) {
    int s = npc_size(w, npc);
    return rc_has_los_rect(&w->map, npc->x, npc->y, s, s, w->player.x,
                           w->player.y, 1, 1, npc->plane);
}

static void chase(RcWorld *w, RcNpc *npc, int range) {
    RcRouteTarget target = rc_route_target_rectangle(
        w->player.x, w->player.y, 1, 1, 1, range, false, range > 1);
    rc_npc_route_request(w, npc, &target, RC_NPC_ROUTE_CHASE, false);
}

static void walk_to(RcWorld *w, RcNpc *npc, int x, int y) {
    if (npc->x == x && npc->y == y) return;
    if (npc->route_mode == RC_NPC_ROUTE_SCRIPTED && npc->route_len > 0
            && npc->route_target.x == x && npc->route_target.y == y)
        return;
    RcRouteTarget target = rc_route_target_point(x, y);
    rc_npc_route_request(w, npc, &target, RC_NPC_ROUTE_SCRIPTED, true);
}

static void set_form(RcNpc *npc, int npc_id) {
    int idx = rc_npc_def_find(npc_id);
    if (idx >= 0) rc_npc_set_form(npc, idx);
}

static int form_id(const RcWorld *w, const RcNpc *npc) {
    const RcNpcDef *def = rc_npc_def_for_npc(w, npc);
    return def ? def->id : -1;
}

static int attack(RcWorld *w, RcNpc *npc, RcCombatStyle style, int max_hit,
                  int delay, int protected_pct, const char *key) {
    RcNpcScriptedAttack a = {.style = style, .max_hit = max_hit, .delay = delay,
                             .protected_pct = protected_pct, .key = key};
    return rc_combat_npc_scripted_attack(w, npc, &a);
}

// A warned tile strike from `src`, presented as its `key` action.
static void strike(RcWorld *w, CoxRaid *raid, RcNpc *src, int x, int y,
                   int radius, int min, int max, int delay, RcCombatStyle style,
                   uint8_t flags, const char *key) {
    CoxHazard h = {.kind = COX_HAZARD_STRIKE, .radius = (uint8_t)radius,
                   .style = (uint8_t)style, .flags = flags, .x = (int16_t)x,
                   .y = (int16_t)y, .plane = (int16_t)(src ? src->plane
                                                           : w->player.plane),
                   .land_tick = w->tick + (RcTick)delay,
                   .min = (uint16_t)min, .max = (uint16_t)max,
                   .source = src ? (RcNpcId)src->uid : COX_NO_NPC,
                   .loc_id = -1};
    cox_add_hazard(raid, &h);
    if (src && key)
        rc_combat_record_encounter_action(w, src, false, x, y, style, delay, key);
}

static int max_hit_now(const RcWorld *w, const RcNpc *npc) {
    return rc_npc_max_hit(npc, rc_npc_def_for_npc(w, npc));
}

// Drops supplies; unstackable items land one per quantity.
static void drop(RcWorld *w, int x, int y, int plane, const RcGroundGrant *g,
                 int count) {
    RcGroundGrant out[16];
    int n = 0;
    for (int i = 0; i < count; i++) {
        const RcItemDef *def = rc_item_def_get(g[i].item_id);
        int copies = def && !def->stackable ? g[i].quantity : 1;
        for (int k = 0; k < copies && n < 16; k++)
            out[n++] = (RcGroundGrant){g[i].item_id,
                                       def && !def->stackable ? 1 : g[i].quantity, 0};
    }
    if (n > 0)
        rc_ground_grant(w, out, n, x, y, plane, RC_GROUND_OWNER_LOCAL_PLAYER,
                        (RcGroundPolicy){0, 3000, 0, 3000});
}

static void drop_seeds(RcWorld *w, const RcNpc *npc, int min, int max) {
    RcGroundGrant g[4] = {
        {ITEM_BIG_BONES, 1, 0},
        {ITEM_SEED_BUCHU, min + cox_rand(w, max - min + 1), 0},
        {ITEM_SEED_GOLPAR, min + cox_rand(w, max - min + 1), 0},
        {ITEM_SEED_NOXIFER, min + cox_rand(w, max - min + 1), 0},
    };
    drop(w, npc->x, npc->y, npc->plane, g, 4);
}

static bool passage_of(const RcWorld *w, const CoxRoom *room, int *x, int *y,
                       int *rotation) {
    return cox_find_loc(w, room, LOC_PASSAGE, x, y, rotation, 1) == 1;
}

// Places the exit blocker beside the exit passage, on the room side.
static void place_exit(RcWorld *w, CoxRoom *room, CoxGate *g, int loc, int size) {
    if (g->exit_placed || g->exit_open) return;
    int px, py, rot;
    if (!passage_of(w, room, &px, &py, &rot)) return;
    int x, y;
    switch (room->variant) {
    case COX_EXIT_WEST: x = px + 3; y = py; break;
    case COX_EXIT_NORTH: x = px; y = py - size; break;
    default: x = px - size; y = py; break;
    }
    g->exit_x = (int16_t)x;
    g->exit_y = (int16_t)y;
    g->exit_loc = (int16_t)loc;
    g->exit_size = (uint8_t)size;
    g->exit_placed = true;
    if (size == 2) {
        cox_add_loc(w, loc, 10, 0, x, y, room->plane, NULL);
    } else {
        bool along_x = room->variant == COX_EXIT_NORTH;
        cox_add_loc(w, loc, 10, 0, x, y, room->plane, NULL);
        cox_add_loc(w, loc, 10, 0, x + (along_x ? 1 : 0), y + (along_x ? 0 : 1),
                    room->plane, NULL);
    }
}

static void open_exit(RcWorld *w, CoxRoom *room, CoxGate *g) {
    if (g->exit_open || !g->exit_placed) {
        g->exit_open = true;
        return;
    }
    g->exit_open = true;
    int n = g->exit_size == 2 ? 1 : 2;
    bool along_x = room->variant == COX_EXIT_NORTH;
    for (int i = 0; i < n; i++) {
        int x = g->exit_x + (i && along_x ? 1 : 0);
        int y = g->exit_y + (i && !along_x ? 1 : 0);
        cox_remove_loc(w, g->exit_loc, x, y, room->plane);
    }
}

// Entrance gate at local (15..16, 1): a 2x2 fire or a pair of tendrils.
static void place_entry(RcWorld *w, CoxRoom *room, CoxGate *g, int loc) {
    if (g->entry_placed) return;
    g->entry_placed = true;
    g->entry_loc = (int16_t)loc;
    int x = room->x + 15, y = room->y + 1;
    if (loc == LOC_TEKTON_FIRE || loc == LOC_VASA_FIRE) {
        cox_add_loc(w, loc, 10, 0, x, y, room->plane, NULL);
    } else {
        cox_add_loc(w, loc, 10, 0, x, y, room->plane, NULL);
        cox_add_loc(w, loc, 10, 0, x + 1, y, room->plane, NULL);
    }
}

static void remove_entry(RcWorld *w, CoxRoom *room, CoxGate *g) {
    if (!g->entry_placed || !g->entry_loc) return;
    int x = room->x + 15, y = room->y + 1;
    cox_remove_loc(w, g->entry_loc, x, y, room->plane);
    if (g->entry_loc != LOC_TEKTON_FIRE && g->entry_loc != LOC_VASA_FIRE)
        cox_remove_loc(w, g->entry_loc, x + 1, y, room->plane);
    g->entry_loc = 0;
}

static CoxGate *room_gate(CoxRoom *room) {
    // Every combat room state starts with its gate.
    return (CoxGate *)room->state;
}

static void finish_room(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    CoxGate *g = room_gate(room);
    open_exit(w, room, g);
    remove_entry(w, room, g);
    cox_complete_room(w, raid, room);
}

static bool player_inside(const RcWorld *w, const CoxRoom *room, int min_ly) {
    return cox_player_in_room(w, room) && w->player.y - room->y >= min_ly;
}

// Entry gates: "Pass" through tendrils or "Cross" a fire, for damage.
static RcInteractionHandlerResult cross_gate(RcWorld *world, RcPlayer *player,
                                             const RcPendingInteraction *pending,
                                             void *ctx) {
    (void)ctx;
    CoxRaid *raid = cox_raid(world);
    CoxRoom *room = raid ? cox_room_at(raid, pending->target.tile_x,
                                       pending->target.tile_y,
                                       pending->target.plane) : NULL;
    if (!room) return rc_interaction_result_message("Nothing interesting happens.");
    CoxGate *g = room_gate(room);
    int loc = pending->target.definition_id;
    int depth = loc == LOC_TEKTON_FIRE || loc == LOC_VASA_FIRE ? 2 : 1;
    int gate_y = room->y + 1;
    int y = player->y < gate_y ? gate_y + depth : gate_y - 1;
    if (!cox_tile_walkable(world, player->x, y, player->plane))
        return rc_interaction_result_message("You can't cross from here.");
    cox_relocate(world, raid, player->x, y, player->plane);
    int damage;
    if (loc == LOC_VASA_FIRE) damage = 22;
    else if (loc == LOC_SHAMAN_TENDRILS && g->entry_crossings == 0) damage = 0;
    else damage = player->skills.base_level[SKILL_HITPOINTS] * 15 / 100;
    g->entry_crossings++;
    if (damage)
        rc_combat_damage_player(world, damage, 0, COMBAT_NONE, RC_HIT_SOURCE_STATUS);
    return rc_interaction_result_complete();
}

// ---------------------------------------------------------------------------
// Tekton.

enum { TEK_WAIT, TEK_RISE, TEK_APPROACH, TEK_FIGHT, TEK_RETURN, TEK_ANVIL };
enum { NPC_TEK_WAITING = 7540, NPC_TEK_WALK = 7541, NPC_TEK_FIGHT = 7542,
       NPC_TEK_WALK_ENRAGED = 7543, NPC_TEK_FIGHT_ENRAGED = 7544,
       NPC_TEK_HAMMERING = 7545 };

typedef struct {
    CoxGate gate;
    uint8_t phase;
    bool repaired, enraged;
    RcTick phase_tick, next_swing, session_end, enrage_end;
    RcTick next_heal, next_spark, anvil_end;
    int16_t work_x, work_y;
} TektonState;
COX_STATE_FITS(TektonState);

static const CoxStats g_tekton = {
    .hp = 300, .att = 390, .str = 390, .def = 205, .mag = 205, .rng = 1,
    .attack_bonus = {64, 64, 64, 0, 0}, .defence_bonus = {155, 165, 105, 0, 0},
    .max_hit = 52, .max_bonus = 20, .attack_speed = 3, .style = COMBAT_MELEE_CRUSH,
    .flags = COX_STATS_MAGIC_DEFENSIVE | COX_STATS_MAX_FORMULA,
};
static const int8_t g_tekton_work[3][2] = {{15, 18}, {11, 14}, {13, 18}};

static void tekton_bonuses(RcNpc *npc, bool enraged) {
    static const int16_t normal[5] = {155, 165, 105, 0, 0};
    static const int16_t raged[5] = {280, 290, 180, 0, 0};
    memcpy(npc->defence_bonus, enraged ? raged : normal, sizeof(normal));
    npc->max_hit_override = (uint16_t)cox_npc_max_hit(npc->stats[2], enraged ? 30 : 20);
}

static void tekton_enter(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    TektonState *s = COX_ROOM_STATE(TektonState, room);
    CoxGate *g = &s->gate;
    if (room->flags & COX_ROOM_COMPLETE) return;
    if (!(room->flags & COX_ROOM_SPAWNED)) {
        room->flags |= COX_ROOM_SPAWNED;
        s->work_x = (int16_t)(room->x + g_tekton_work[room->variant][0]);
        s->work_y = (int16_t)(room->y + g_tekton_work[room->variant][1]);
        cox_spawn(w, raid, room, NPC_TEK_WAITING, s->work_x, s->work_y, 1,
                  &g_tekton, true);
    }
    place_exit(w, room, g, LOC_CRYSTAL_ORANGE, 2);
}

static void tekton_tick(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    TektonState *s = COX_ROOM_STATE(TektonState, room);
    RcNpc *tek = cox_room_npc(w, room, 1);
    if (!tek || (room->flags & COX_ROOM_COMPLETE)) return;
    RcTick now = w->tick;
    switch (s->phase) {
    case TEK_WAIT:
        if (cox_player_in_room(w, room) && cox_player_distance(w, tek) <= 4) {
            s->phase = TEK_RISE;
            s->phase_tick = now;
            room->started_tick = now;
            room->flags |= COX_ROOM_STARTED;
        }
        break;
    case TEK_RISE:
        if (now >= s->phase_tick + 4) {
            set_form(tek, NPC_TEK_WALK);
            place_entry(w, room, &s->gate, LOC_TEKTON_FIRE);
            s->phase = TEK_APPROACH;
        }
        break;
    case TEK_APPROACH:
        if (player_adjacent(w, tek)) {
            rc_npc_route_clear(tek, RC_MOVEMENT_ARRIVED);
            set_form(tek, s->enraged ? NPC_TEK_FIGHT_ENRAGED : NPC_TEK_FIGHT);
            s->next_swing = now + (s->repaired ? 2 : 3);
            s->session_end = now + (s->repaired ? 47 : 27);
            if (s->enraged) s->enrage_end = now + 17;
            s->phase = TEK_FIGHT;
        } else if (cox_player_in_room(w, room)) {
            chase(w, tek, 1);
        }
        break;
    case TEK_FIGHT:
        if (s->enraged && now >= s->enrage_end) {
            s->enraged = false;
            tekton_bonuses(tek, false);
            set_form(tek, NPC_TEK_FIGHT);
        }
        if (now < s->next_swing) break;
        if (now >= s->session_end || !player_adjacent(w, tek)) {
            // Session over, or nobody in reach on the scan: back to the anvil.
            set_form(tek, s->enraged ? NPC_TEK_WALK_ENRAGED : NPC_TEK_WALK);
            walk_to(w, tek, s->work_x, s->work_y);
            s->phase = TEK_RETURN;
            break;
        }
        {
            static const RcCombatStyle styles[3] = {COMBAT_MELEE_STAB,
                COMBAT_MELEE_SLASH, COMBAT_MELEE_CRUSH};
            static const char *const keys[3] = {"Stab", "Slash", "Crush"};
            int k = cox_rand(w, 3);
            // Protect from Melee lets half through; slash lands a tick later.
            attack(w, tek, styles[k], max_hit_now(w, tek), k == 1 ? 2 : 1, 50,
                   keys[k]);
            s->next_swing = now + 3;
        }
        break;
    case TEK_RETURN:
        if (tek->x == s->work_x && tek->y == s->work_y) {
            set_form(tek, NPC_TEK_HAMMERING);
            s->phase = TEK_ANVIL;
            s->repaired = true;
            s->anvil_end = now + 15 + (RcTick)cox_rand(w, 7);
            s->next_heal = now + 3;
            s->next_spark = now + 1 + (RcTick)cox_rand(w, 4);
        } else {
            walk_to(w, tek, s->work_x, s->work_y);
        }
        break;
    case TEK_ANVIL:
        if (now >= s->next_heal) {
            tek->current_hp += tek->spawn_hp / 100 + 1;
            if (tek->current_hp > tek->spawn_hp) tek->current_hp = tek->spawn_hp;
            s->next_heal = now + 3;
        }
        if (now >= s->next_spark && cox_player_in_room(w, room)) {
            // One spark per raider at their tile, 3x3, through prayer.
            int d = cox_chebyshev(tek->x, tek->y, 4, w->player.x, w->player.y, 1);
            int delay = (60 + 5 * d + 15) / 30 + 1;
            strike(w, raid, tek, w->player.x, w->player.y, 1, 10, 20, delay,
                   COMBAT_NONE, 0, "Spark");
            s->next_spark = now + 4;
        }
        if (now >= s->anvil_end) {
            s->enraged = true;
            tekton_bonuses(tek, true);
            set_form(tek, NPC_TEK_WALK_ENRAGED);
            s->phase = TEK_APPROACH;
        }
        break;
    }
}

static void tekton_died(RcWorld *w, CoxRaid *raid, CoxRoom *room, RcNpc *npc,
                        int role) {
    (void)role;
    RcGroundGrant g[4] = {{ITEM_STINKHORN, 5, 0}, {ITEM_OVERLOAD_P, 2, 0},
                          {ITEM_PRAYER_ENHANCE_P, 1, 0},
                          {ITEM_REVITALISATION_P, 1, 0}};
    drop(w, npc->x + 1, npc->y + 1, npc->plane, g, 4);
    finish_room(w, raid, room);
}

const CoxRoomOps cox_tekton_ops = {
    .enter = tekton_enter, .tick = tekton_tick, .npc_died = tekton_died,
};

// ---------------------------------------------------------------------------
// Vanguards.

enum { VAN_DORMANT, VAN_RISING, VAN_FIGHT, VAN_SHUFFLE };
enum { NPC_VAN_DORMANT = 7525, NPC_VAN_WALK = 7526, NPC_VAN_MELEE = 7527,
       NPC_VAN_RANGED = 7528, NPC_VAN_MAGIC = 7529 };

typedef struct {
    CoxGate gate;
    uint8_t phase;
    int8_t dir;
    uint8_t hole[3];            // hole index each Vanguard (melee, ranged, magic) holds
    int16_t hx[3], hy[3];       // hole south-west tiles
    RcTick phase_tick, next_attack, next_shuffle;
    bool dead[3];
} VanguardState;
COX_STATE_FITS(VanguardState);

static const CoxStats g_vanguard[3] = {
    {.hp = 180, .att = 150, .str = 150, .def = 160, .mag = 150, .rng = 150,
     .attack_bonus = {20, 20, 20, 0, 0}, .defence_bonus = {150, 150, 150, 400, 20},
     .max_hit = 18, .max_bonus = 10, .attack_speed = 4,
     .style = COMBAT_MELEE_CRUSH, .flags = COX_STATS_MAX_FORMULA},
    {.hp = 180, .att = 150, .str = 150, .def = 160, .mag = 150, .rng = 150,
     .attack_bonus = {0, 0, 0, 40, 0}, .defence_bonus = {55, 60, 100, 300, 400},
     .max_hit = 22, .max_bonus = 25, .attack_speed = 4,
     .style = COMBAT_RANGED, .flags = COX_STATS_MAX_FORMULA},
    {.hp = 180, .att = 150, .str = 150, .def = 160, .mag = 150, .rng = 150,
     .attack_bonus = {0, 0, 0, 0, 40}, .defence_bonus = {315, 340, 400, 50, 110},
     .max_hit = 22, .max_bonus = 25, .attack_speed = 4,
     .style = COMBAT_MAGIC, .flags = COX_STATS_MAX_FORMULA},
};
static const int g_van_forms[3] = {NPC_VAN_MELEE, NPC_VAN_RANGED, NPC_VAN_MAGIC};
// Hole south-west tiles (THRU and CW from packet captures, CCW mirrored).
static const int8_t g_van_holes[3][3][2] = {
    {{10, 12}, {16, 19}, {20, 12}},
    {{12, 12}, {20, 17}, {13, 21}},
    {{19, 12}, {13, 19}, {9, 12}},
};

static void vanguards_enter(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    VanguardState *s = COX_ROOM_STATE(VanguardState, room);
    if (room->flags & COX_ROOM_COMPLETE) return;
    if (!(room->flags & COX_ROOM_SPAWNED)) {
        room->flags |= COX_ROOM_SPAWNED;
        for (int i = 0; i < 3; i++) {
            s->hx[i] = (int16_t)(room->x + g_van_holes[room->variant][i][0]);
            s->hy[i] = (int16_t)(room->y + g_van_holes[room->variant][i][1]);
            s->hole[i] = (uint8_t)i;
            RcNpc *v = cox_spawn(w, raid, room, NPC_VAN_DORMANT, s->hx[i], s->hy[i],
                                 i + 1, &g_vanguard[i], true);
            if (v) v->player_untargetable = true;
        }
    }
    place_exit(w, room, &s->gate, LOC_CRYSTAL_ORANGE, 2);
}

static void vanguard_emerge(RcWorld *w, VanguardState *s, CoxRoom *room) {
    for (int i = 0; i < 3; i++) {
        RcNpc *v = cox_room_npc(w, room, i + 1);
        if (!v) continue;
        set_form(v, g_van_forms[i]);
        v->player_untargetable = false;
    }
    s->phase = VAN_FIGHT;
    s->next_attack = w->tick + 5;
    s->next_shuffle = w->tick + 20 + (RcTick)cox_rand(w, 17);
}

static void vanguard_shuffle(RcWorld *w, VanguardState *s, CoxRoom *room,
                             bool heal) {
    s->phase = VAN_SHUFFLE;
    s->phase_tick = w->tick;
    s->dir = cox_rand(w, 2) ? 1 : 2;
    for (int i = 0; i < 3; i++) {
        RcNpc *v = cox_room_npc(w, room, i + 1);
        if (!v || v->current_hp <= 0) continue;
        set_form(v, NPC_VAN_WALK);
        v->player_untargetable = true;
        // Each gains a hitpoint as it changes to its shell, or heals fully
        // on an HP reset; drained stats come back.
        v->current_hp = heal ? v->spawn_hp
                      : v->current_hp + 1 > v->spawn_hp ? v->spawn_hp
                      : v->current_hp + 1;
        for (int k = 0; k < 6; k++)
            if (v->base_stats[k] > 0) v->stats[k] = v->base_stats[k];
        s->hole[i] = (uint8_t)((s->hole[i] + s->dir) % 3);
        walk_to(w, v, s->hx[s->hole[i]], s->hy[s->hole[i]]);
    }
}

static void vanguard_attacks(RcWorld *w, CoxRaid *raid, VanguardState *s,
                             CoxRoom *room) {
    for (int i = 0; i < 3; i++) {
        RcNpc *v = cox_room_npc(w, room, i + 1);
        if (!v) continue;
        int max = max_hit_now(w, v);
        if (i == 0) {
            if (!player_adjacent(w, v)) {
                chase(w, v, 1);
                continue;
            }
            for (int h = 0; h < 3; h++)
                attack(w, v, COMBAT_MELEE_CRUSH, max, 0, 50, "Melee");
            continue;
        }
        if (cox_player_distance(w, v) > 10 || !has_los(w, v)) continue;
        RcCombatStyle style = i == 1 ? COMBAT_RANGED : COMBAT_MAGIC;
        int delay = i == 1 ? 2 : 3;
        attack(w, v, style, max, delay, 50, i == 1 ? "Ranged" : "Magic");
        // Two more projectiles land on tiles 2 and 4 from the target.
        for (int gap = 2; gap <= 4; gap += 2) {
            int dx = cox_rand(w, 3) - 1, dy = cox_rand(w, 3) - 1;
            if (!dx && !dy) dx = 1;
            strike(w, raid, v, w->player.x + dx * gap, w->player.y + dy * gap, 0,
                   0, max, delay, style, COX_HAZARD_PRAYER_HALVES,
                   i == 1 ? "Ranged:tile" : "Magic:tile");
        }
    }
}

static void vanguards_tick(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    VanguardState *s = COX_ROOM_STATE(VanguardState, room);
    if (room->flags & COX_ROOM_COMPLETE) return;
    RcTick now = w->tick;
    switch (s->phase) {
    case VAN_DORMANT: {
        bool near = false;
        for (int i = 0; i < 3 && !near; i++)
            near = cox_player_in_room(w, room)
                && cox_chebyshev(s->hx[i], s->hy[i], 3, w->player.x,
                                 w->player.y, 1) <= 5;
        if (near) {
            s->phase = VAN_RISING;
            s->phase_tick = now;
            room->flags |= COX_ROOM_STARTED;
            room->started_tick = now;
        }
        break;
    }
    case VAN_RISING:
        if (now >= s->phase_tick + 5) vanguard_emerge(w, s, room);
        break;
    case VAN_FIGHT:
        if (now >= s->next_shuffle) {
            vanguard_shuffle(w, s, room, false);
            break;
        }
        if (now >= s->next_attack) {
            vanguard_attacks(w, raid, s, room);
            s->next_attack = now + 4;
        }
        break;
    case VAN_SHUFFLE: {
        bool arrived = true;
        for (int i = 0; i < 3; i++) {
            RcNpc *v = cox_room_npc(w, room, i + 1);
            if (v && (v->x != s->hx[s->hole[i]] || v->y != s->hy[s->hole[i]]))
                arrived = false;
        }
        if ((arrived && now >= s->phase_tick + 9) || now >= s->phase_tick + 20)
            vanguard_emerge(w, s, room);
        break;
    }
    }
}

static void vanguards_hit(RcWorld *w, CoxRaid *raid, CoxRoom *room, RcNpc *npc,
                          int role, int damage) {
    (void)raid;
    (void)npc;
    (void)role;
    (void)damage;
    VanguardState *s = COX_ROOM_STATE(VanguardState, room);
    if (s->phase != VAN_FIGHT) return;
    // A gap of 40% of max HP between them (a dead one counts as 0) heals
    // and shuffles the living.
    int lo = 1 << 30, hi = -1, max = npc->spawn_hp;
    for (int i = 0; i < 3; i++) {
        RcNpc *v = cox_room_npc(w, room, i + 1);
        int hp = v && !s->dead[i] ? v->current_hp : 0;
        if (hp < 0) hp = 0;
        if (hp < lo) lo = hp;
        if (hp > hi) hi = hp;
    }
    if (hi > 0 && hi - lo >= max * 40 / 100) vanguard_shuffle(w, s, room, true);
}

static void vanguards_died(RcWorld *w, CoxRaid *raid, CoxRoom *room, RcNpc *npc,
                           int role) {
    VanguardState *s = COX_ROOM_STATE(VanguardState, room);
    int i = role - 1;
    if (i < 0 || i > 2) return;
    s->dead[i] = true;
    static const RcGroundGrant drops[3][3] = {
        {{ITEM_XERICS_AID_P, 2, 0}, {ITEM_ELDER_P, 1, 0}, {ITEM_OVERLOAD_P, 1, 0}},
        {{ITEM_XERICS_AID_P, 1, 0}, {ITEM_REVITALISATION_P, 2, 0}, {ITEM_TWISTED_P, 1, 0}},
        {{ITEM_XERICS_AID_P, 1, 0}, {ITEM_PRAYER_ENHANCE_P, 1, 0}, {ITEM_KODAI_P, 1, 0}},
    };
    drop(w, npc->x + 1, npc->y + 1, npc->plane, drops[i], 3);
    if (s->dead[0] && s->dead[1] && s->dead[2]) finish_room(w, raid, room);
}

const CoxRoomOps cox_vanguards_ops = {
    .enter = vanguards_enter, .tick = vanguards_tick,
    .npc_died = vanguards_died, .npc_hit = vanguards_hit,
};

// ---------------------------------------------------------------------------
// Vasa Nistirio.

enum { VASA_DORMANT, VASA_WAKING, VASA_SPECIAL, VASA_WALK, VASA_SIPHON,
       VASA_RETURN };
enum { NPC_VASA_DORMANT = 7565, NPC_VASA = 7566, NPC_VASA_HEALING = 7567,
       NPC_VASA_CRYSTAL = 7568 };
enum { ROLE_VASA = 1, ROLE_CRYSTAL = 2 };   // crystals 2..5

typedef struct {
    CoxGate gate;
    uint8_t phase;
    int8_t crystal;
    bool crystal_dead[4];
    int16_t cx[4], cy[4];
    int16_t home_x, home_y;
    int16_t timer;              // siphon budget, ticks
    int16_t stored_heal;
    RcTick phase_tick, next_boulder, next_siphon;
} VasaState;
COX_STATE_FITS(VasaState);

static const CoxStats g_vasa = {
    .hp = 300, .att = 1, .str = 1, .def = 175, .mag = 230, .rng = 230,
    .attack_bonus = {0, 0, 0, 100, 0}, .defence_bonus = {170, 190, 40, 40, 400},
    .max_hit = 24, .attack_speed = 3, .style = COMBAT_RANGED,
    .flags = COX_STATS_MAX_FORMULA,
};
static const CoxStats g_vasa_crystal = {
    .hp = 120, .att = 1, .str = 1, .def = 100, .mag = 100, .rng = 1,
    .defence_bonus = {-5, 180, 180, 0, 0}, .style = COMBAT_NONE,
};
static const int8_t g_vasa_crystals[4][2] = {{6, 5}, {6, 23}, {23, 23}, {23, 5}};

static void vasa_enter(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    VasaState *s = COX_ROOM_STATE(VasaState, room);
    if (room->flags & COX_ROOM_COMPLETE) return;
    if (!(room->flags & COX_ROOM_SPAWNED)) {
        room->flags |= COX_ROOM_SPAWNED;
        s->home_x = (int16_t)(room->x + 13);
        s->home_y = (int16_t)(room->y + 13);
        s->crystal = -1;
        s->timer = 66;
        RcNpc *vasa = cox_spawn(w, raid, room, NPC_VASA_DORMANT, s->home_x,
                                s->home_y, ROLE_VASA, &g_vasa, true);
        if (vasa) vasa->player_untargetable = true;
        for (int i = 0; i < 4; i++) {
            s->cx[i] = (int16_t)(room->x + g_vasa_crystals[i][0]);
            s->cy[i] = (int16_t)(room->y + g_vasa_crystals[i][1]);
            cox_add_loc(w, LOC_VASA_CRYSTAL_OFF, 10, 0, s->cx[i], s->cy[i],
                        room->plane, NULL);
            RcNpc *c = cox_spawn(w, raid, room, NPC_VASA_CRYSTAL, s->cx[i],
                                 s->cy[i], ROLE_CRYSTAL + i, &g_vasa_crystal, true);
            if (c) c->player_untargetable = true;
        }
    }
    place_exit(w, room, &s->gate, LOC_CRYSTAL_PURPLE_SMALL, 1);
}

static void vasa_pick_crystal(RcWorld *w, VasaState *s, CoxRoom *room,
                              RcNpc *vasa) {
    int alive[4], n = 0;
    for (int i = 0; i < 4; i++) if (!s->crystal_dead[i]) alive[n++] = i;
    if (!n) {
        // With every crystal broken he just keeps fighting.
        s->crystal = -1;
        s->phase = VASA_RETURN;
        s->timer = 1 << 14;
        return;
    }
    s->crystal = (int8_t)alive[cox_rand(w, n)];
    cox_replace_loc(w, LOC_VASA_CRYSTAL_OFF, s->cx[s->crystal], s->cy[s->crystal],
                    room->plane, LOC_VASA_CRYSTAL_ON);
    RcNpc *c = cox_room_npc(w, room, ROLE_CRYSTAL + s->crystal);
    if (c) c->player_untargetable = false;
    set_form(vasa, NPC_VASA);
    s->phase = VASA_WALK;
}

static void vasa_drop_crystal(RcWorld *w, VasaState *s, CoxRoom *room) {
    if (s->crystal < 0) return;
    int i = s->crystal;
    if (!s->crystal_dead[i]) {
        cox_replace_loc(w, LOC_VASA_CRYSTAL_ON, s->cx[i], s->cy[i], room->plane,
                        LOC_VASA_CRYSTAL_OFF);
        RcNpc *c = cox_room_npc(w, room, ROLE_CRYSTAL + i);
        if (c) c->player_untargetable = true;
    }
    s->crystal = -1;
}

static void vasa_special(RcWorld *w, CoxRaid *raid, VasaState *s, CoxRoom *room,
                         RcNpc *vasa) {
    RcTick at = w->tick - s->phase_tick;
    RcPlayer *p = &w->player;
    if (at == 0) {
        // Teleport the raider beside Vasa and stun them; he cannot be hurt
        // until the blast lands.
        static const int d[4][2] = {{-1, 2}, {5, 2}, {2, -1}, {2, 5}};
        for (int k = 0; k < 4; k++) {
            int x = vasa->x + d[k][0], y = vasa->y + d[k][1];
            if (cox_player_in_room(w, room)
                    && cox_tile_walkable(w, x, y, room->plane)) {
                cox_relocate(w, raid, x, y, room->plane);
                break;
            }
        }
        rc_player_apply_freeze(w, 10);
        rc_prayer_disable_all(w, RC_PRAYER_FORCED_OFF);
        rc_combat_record_encounter_action(w, vasa, true, p->x, p->y, COMBAT_MAGIC,
                                          10, "Teleport");
    } else if (at == 10) {
        // The blast takes all but 5 of the stunned raider's HP.
        if (player_adjacent(w, vasa) || player_under(w, vasa)) {
            int hp = p->current_hp / 10 - 5;
            if (hp > 0)
                rc_combat_damage_player(w, hp, 0, COMBAT_MAGIC, (int)vasa->uid);
        }
    } else if (at >= 12) {
        s->timer = 66;
        s->stored_heal = 0;
        vasa_pick_crystal(w, s, room, vasa);
    }
}

static void vasa_begin_special(RcWorld *w, CoxRaid *raid, VasaState *s,
                               CoxRoom *room, RcNpc *vasa) {
    s->phase = VASA_SPECIAL;
    s->phase_tick = w->tick;
    vasa_special(w, raid, s, room, vasa);
}

static void vasa_tick(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    VasaState *s = COX_ROOM_STATE(VasaState, room);
    RcNpc *vasa = cox_room_npc(w, room, ROLE_VASA);
    if (!vasa || (room->flags & COX_ROOM_COMPLETE)) return;
    RcTick now = w->tick;
    RcPlayer *p = &w->player;
    int center_x = s->home_x + 2, center_y = s->home_y + 2;
    if (s->phase == VASA_DORMANT) {
        if (cox_player_in_room(w, room)
                && cox_chebyshev(center_x, center_y, 1, p->x, p->y, 1) <= 7) {
            s->phase = VASA_WAKING;
            s->phase_tick = now;
            room->flags |= COX_ROOM_STARTED;
            room->started_tick = now;
            place_entry(w, room, &s->gate, LOC_VASA_FIRE);
        }
        return;
    }
    if (s->phase == VASA_WAKING) {
        if (now >= s->phase_tick + 12) {
            set_form(vasa, NPC_VASA);
            vasa->player_untargetable = false;
            vasa_begin_special(w, raid, s, room, vasa);
        }
        return;
    }
    // Players underneath are stomped every tick.
    if (player_under(w, vasa))
        rc_combat_damage_player(w, 1 + cox_rand(w, 8), 0, COMBAT_MELEE_CRUSH,
                                (int)vasa->uid);
    switch (s->phase) {
    case VASA_SPECIAL:
        vasa_special(w, raid, s, room, vasa);
        return;
    case VASA_WALK: {
        int i = s->crystal;
        if (i < 0) {
            vasa_pick_crystal(w, s, room, vasa);
            return;
        }
        if (cox_chebyshev(vasa->x, vasa->y, 5, s->cx[i], s->cy[i], 4) <= 1) {
            rc_npc_route_clear(vasa, RC_MOVEMENT_ARRIVED);
            set_form(vasa, NPC_VASA_HEALING);
            s->phase = VASA_SIPHON;
            s->next_siphon = now + 2;
            return;
        }
        RcRouteTarget target = rc_route_target_rectangle(s->cx[i], s->cy[i], 4, 4,
                                                         1, 1, false, false);
        if (vasa->route_mode != RC_NPC_ROUTE_SCRIPTED || vasa->route_len == 0)
            rc_npc_route_request(w, vasa, &target, RC_NPC_ROUTE_SCRIPTED, true);
        break;
    }
    case VASA_SIPHON:
        // Mod Ash: every 2 ticks, 1% HP (stored since Nov 2023) and
        // Defence (10% + 1).
        if (now >= s->next_siphon) {
            s->stored_heal += (int16_t)(vasa->spawn_hp / 100);
            int def = vasa->stats[1] + vasa->base_stats[1] / 10 + 1;
            vasa->stats[1] = def > vasa->base_stats[1] ? vasa->base_stats[1] : def;
            s->next_siphon = now + 2;
        }
        if (--s->timer <= 0) {
            vasa->current_hp += s->stored_heal;
            if (vasa->current_hp > vasa->spawn_hp) vasa->current_hp = vasa->spawn_hp;
            s->stored_heal = 0;
            vasa_drop_crystal(w, s, room);
            set_form(vasa, NPC_VASA);
            s->phase = VASA_RETURN;
        }
        return;
    case VASA_RETURN: {
        if (vasa->x != s->home_x || vasa->y != s->home_y) {
            walk_to(w, vasa, s->home_x, s->home_y);
            break;
        }
        if (s->timer <= 0) {
            vasa_begin_special(w, raid, s, room, vasa);
            return;
        }
        bool crystals = false;
        for (int i = 0; i < 4; i++) crystals |= !s->crystal_dead[i];
        if (crystals) {
            vasa_pick_crystal(w, s, room, vasa);
            return;
        }
        // With every crystal broken he stays and keeps throwing boulders.
        break;
    }
    }
    // Boulders at raiders within 8 tiles who are not underneath, 3x3 where
    // they stood; Protect from Missiles cuts the damage to a third.
    if (now >= s->next_boulder && cox_player_in_room(w, room)
            && !player_under(w, vasa) && cox_player_distance(w, vasa) <= 8) {
        int d = cox_player_distance(w, vasa);
        int max = max_hit_now(w, vasa);
        CoxHazard h = {.kind = COX_HAZARD_STRIKE, .radius = 1,
                       .style = COMBAT_RANGED, .x = (int16_t)p->x,
                       .y = (int16_t)p->y, .plane = (int16_t)p->plane,
                       .land_tick = now + 2 + (RcTick)(d / 4),
                       .min = 0, .max = (uint16_t)max, .source = (RcNpcId)vasa->uid,
                       .loc_id = -1};
        if (rc_combat_apply_protection(1, COMBAT_RANGED, p->active_prayers, true) == 0)
            h.max = (uint16_t)(max / 3);
        cox_add_hazard(raid, &h);
        rc_combat_record_encounter_action(w, vasa, false, p->x, p->y, COMBAT_RANGED,
                                          2 + d / 4, "Boulder");
        s->next_boulder = now + 3;
    }
}

static void vasa_died(RcWorld *w, CoxRaid *raid, CoxRoom *room, RcNpc *npc,
                      int role) {
    VasaState *s = COX_ROOM_STATE(VasaState, room);
    if (role == ROLE_VASA) {
        RcGroundGrant g[4] = {{ITEM_OVERLOAD_P, 1, 0}, {ITEM_JUICE, 5, 0},
                              {ITEM_XERICS_AID_P, 2, 0}, {ITEM_TWISTED_P, 2, 0}};
        drop(w, npc->x + 2, npc->y + 2, npc->plane, g, 4);
        for (int i = 0; i < 4; i++) {
            RcNpc *c = cox_room_npc(w, room, ROLE_CRYSTAL + i);
            if (c) rc_npc_remove(w, c->uid);
        }
        finish_room(w, raid, room);
        return;
    }
    // A destroyed crystal discards the healing it stored.
    int i = role - ROLE_CRYSTAL;
    if (i < 0 || i > 3) return;
    s->crystal_dead[i] = true;
    cox_remove_loc(w, i == s->crystal ? LOC_VASA_CRYSTAL_ON : LOC_VASA_CRYSTAL_OFF,
                   s->cx[i], s->cy[i], room->plane);
    if (i == s->crystal) {
        s->crystal = -1;
        s->stored_heal = 0;
        RcNpc *vasa = cox_room_npc(w, room, ROLE_VASA);
        if (vasa) set_form(vasa, NPC_VASA);
        s->phase = VASA_RETURN;
    }
}

const CoxRoomOps cox_vasa_ops = {
    .enter = vasa_enter, .tick = vasa_tick, .npc_died = vasa_died,
};

// ---------------------------------------------------------------------------
// Guardians.

enum { NPC_GUARDIAN_LEFT = 7569, NPC_GUARDIAN_RIGHT = 7570,
       NPC_GUARDIAN_LEFT_DEAD = 7571, NPC_GUARDIAN_RIGHT_DEAD = 7572 };

typedef struct {
    CoxGate gate;
    bool started;
    bool dead[2];
    RcTick next_attack[2];
    RcTick hit_tick[2];
    RcTick statue_tick[2];
    int16_t statue_x[2], statue_y[2];
    CoxStats stats;             // HP follows the party's Mining level
} GuardianState;
COX_STATE_FITS(GuardianState);

static const CoxStats g_guardian = {
    .hp = 250, .att = 140, .str = 140, .def = 100, .mag = 1, .rng = 1,
    .attack_bonus = {0, 0, 0, 0, 0}, .defence_bonus = {80, 180, -10, 0, 0},
    .max_hit = 20, .max_bonus = 20, .attack_speed = 4,
    .style = COMBAT_MELEE_SLASH, .flags = COX_STATS_MAX_FORMULA,
};
static const int8_t g_guardian_pos[3][2][2] = {
    {{12, 17}, {12, 21}}, {{12, 23}, {16, 23}}, {{16, 20}, {16, 24}},
};

static void guardians_enter(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    GuardianState *s = COX_ROOM_STATE(GuardianState, room);
    if (room->flags & COX_ROOM_COMPLETE) return;
    if (!(room->flags & COX_ROOM_SPAWNED)) {
        room->flags |= COX_ROOM_SPAWNED;
        // HP rises with the party's Mining: 151 + average Mining.
        s->stats = g_guardian;
        s->stats.hp = (int16_t)(151 + w->player.skills.base_level[SKILL_MINING]);
        for (int i = 0; i < 2; i++)
            cox_spawn(w, raid, room, i ? NPC_GUARDIAN_RIGHT : NPC_GUARDIAN_LEFT,
                      room->x + g_guardian_pos[room->variant][i][0],
                      room->y + g_guardian_pos[room->variant][i][1], i + 1,
                      &s->stats, true);
    }
    // Invisible blockers hold the passage until both statues are broken.
    place_exit(w, room, &s->gate, LOC_INVISIBLE_BLOCK, 1);
}

static void guardians_tick(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    GuardianState *s = COX_ROOM_STATE(GuardianState, room);
    RcTick now = w->tick;
    // A broken guardian stays as a demolished statue after its death.
    for (int i = 0; i < 2; i++) {
        if (!s->statue_tick[i] || now < s->statue_tick[i]) continue;
        s->statue_tick[i] = 0;
        RcNpc *statue = cox_spawn(w, raid, room, i ? NPC_GUARDIAN_RIGHT_DEAD
                                  : NPC_GUARDIAN_LEFT_DEAD, s->statue_x[i],
                                  s->statue_y[i], 3 + i, NULL, true);
        if (statue) statue->player_untargetable = true;
    }
    if (room->flags & COX_ROOM_COMPLETE) return;
    for (int i = 0; i < 2; i++) {
        RcNpc *g = cox_room_npc(w, room, i + 1);
        if (!g || s->dead[i]) continue;
        if (!s->started) {
            if (!cox_player_in_room(w, room) || cox_player_distance(w, g) > 8) continue;
            s->started = true;
            room->flags |= COX_ROOM_STARTED;
            room->started_tick = now;
            s->next_attack[0] = s->next_attack[1] = now + 2;
        }
        if (now < s->next_attack[i]) continue;
        s->next_attack[i] = now + 4;
        if (!cox_player_in_room(w, room)) continue;
        // In reach: melee or a rockfall, evenly; otherwise only rockfalls.
        if (player_adjacent(w, g) && cox_rand(w, 2)) {
            attack(w, g, COMBAT_MELEE_SLASH, max_hit_now(w, g), 0, 0, "Slash");
        } else if (cox_player_distance(w, g) <= 10) {
            strike(w, raid, g, w->player.x, w->player.y, 1, 0, 15 * max_hit_now(w, g) / 20,
                   2, COMBAT_NONE, 0, "Rockfall");
        }
    }
}

static void guardians_hit(RcWorld *w, CoxRaid *raid, CoxRoom *room, RcNpc *npc,
                          int role, int damage) {
    (void)raid;
    (void)npc;
    (void)damage;
    GuardianState *s = COX_ROOM_STATE(GuardianState, room);
    // Being hit makes both swing back two ticks later (flinching).
    for (int i = 0; i < 2; i++) {
        if (s->next_attack[i] > w->tick + 2 && w->tick >= s->hit_tick[i] + 4) {
            s->next_attack[i] = w->tick + 2;
            s->hit_tick[i] = w->tick;
        }
    }
    (void)role;
}

static void guardians_died(RcWorld *w, CoxRaid *raid, CoxRoom *room, RcNpc *npc,
                           int role) {
    GuardianState *s = COX_ROOM_STATE(GuardianState, room);
    int i = role - 1;
    if (i < 0 || i > 1) return;
    s->dead[i] = true;
    drop_seeds(w, npc, 3, 5);
    s->statue_tick[i] = w->tick + 4;
    s->statue_x[i] = (int16_t)npc->x;
    s->statue_y[i] = (int16_t)npc->y;
    if (s->dead[0] && s->dead[1]) finish_room(w, raid, room);
}

const CoxRoomOps cox_guardians_ops = {
    .enter = guardians_enter, .tick = guardians_tick,
    .npc_died = guardians_died, .npc_hit = guardians_hit,
};

// ---------------------------------------------------------------------------
// Lizardman shamans.

enum { NPC_SHAMAN = 7573, NPC_SPAWN = 6768, NPC_SHAMAN_BLOCKER = 7575 };
enum { ROLE_SHAMAN = 1, ROLE_MINION = 3, ROLE_BLOCKER = 9 };   // minions 3..8
#define SHAMANS 2
#define MINIONS 6

typedef struct {
    CoxGate gate;
    bool started;
    bool dead[SHAMANS];
    RcTick next_attack[SHAMANS], jump_land[SHAMANS], spawn_ready;
    int16_t jump_x[SHAMANS], jump_y[SHAMANS];
    RcTick minion_fuse[MINIONS];
    RcTick open_tick;
} ShamanState;
COX_STATE_FITS(ShamanState);

static const CoxStats g_shaman = {
    .hp = 190, .att = 130, .str = 130, .def = 210, .mag = 130, .rng = 130,
    .attack_bonus = {58, 58, 58, 56, 0}, .defence_bonus = {102, 160, 150, -10, 160},
    .max_hit = 25, .max_bonus = 49, .attack_speed = 4, .style = COMBAT_RANGED,
    .flags = COX_STATS_MAX_FORMULA,
};

// A free square of `size` tiles near a room-local point.
static bool free_square(const RcWorld *w, const CoxRoom *room, int lx, int ly,
                        int size, int *out_x, int *out_y) {
    for (int r = 0; r < 8; r++) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                int x = room->x + lx + dx, y = room->y + ly + dy;
                bool ok = true;
                for (int ty = 0; ty < size && ok; ty++)
                    for (int tx = 0; tx < size && ok; tx++)
                        ok = cox_tile_walkable(w, x + tx, y + ty, room->plane);
                if (ok) {
                    *out_x = x;
                    *out_y = y;
                    return true;
                }
            }
        }
    }
    return false;
}

static void shamans_enter(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    ShamanState *s = COX_ROOM_STATE(ShamanState, room);
    if (room->flags & COX_ROOM_COMPLETE) return;
    if (!(room->flags & COX_ROOM_SPAWNED)) {
        room->flags |= COX_ROOM_SPAWNED;
        static const int8_t at[SHAMANS][2] = {{11, 16}, {19, 16}};
        for (int i = 0; i < SHAMANS; i++) {
            int x, y;
            if (free_square(w, room, at[i][0], at[i][1], 3, &x, &y))
                cox_spawn(w, raid, room, NPC_SHAMAN + (i & 1), x, y,
                          ROLE_SHAMAN + i, &g_shaman, true);
        }
    }
    place_exit(w, room, &s->gate, LOC_INVISIBLE_BLOCK, 1);
    if (!cox_room_npc(w, room, ROLE_BLOCKER) && s->gate.exit_placed) {
        bool along_x = room->variant == COX_EXIT_NORTH;
        for (int k = 0; k < 2; k++)
            cox_spawn(w, raid, room, NPC_SHAMAN_BLOCKER,
                      s->gate.exit_x + (k && along_x ? 1 : 0),
                      s->gate.exit_y + (k && !along_x ? 1 : 0), ROLE_BLOCKER + k,
                      NULL, true);
    }
}

static void shaman_spawn_minions(RcWorld *w, CoxRaid *raid, ShamanState *s,
                                 CoxRoom *room) {
    static const int d[3][2] = {{0, 0}, {-1, 0}, {1, 0}};
    int placed = 0;
    for (int m = 0; m < MINIONS && placed < 3; m++) {
        if (cox_room_npc(w, room, ROLE_MINION + m)) continue;
        int x = w->player.x + d[placed][0], y = w->player.y + d[placed][1];
        if (!cox_tile_walkable(w, x, y, room->plane)) {
            x = w->player.x;
            y = w->player.y;
        }
        if (cox_spawn(w, raid, room, NPC_SPAWN, x, y, ROLE_MINION + m, NULL, true))
            s->minion_fuse[m] = w->tick + 10 + (RcTick)cox_rand(w, 11) + (RcTick)placed;
        placed++;
    }
}

static void shaman_attack(RcWorld *w, CoxRaid *raid, ShamanState *s, int i,
                          RcNpc *sh, CoxRoom *room) {
    RcPlayer *p = &w->player;
    bool adjacent = player_adjacent(w, sh);
    int roll = cox_rand(w, adjacent ? 7 : 6);
    if (roll == 4) {
        // Acid: 5x5 at the raider's tile, through prayer, may poison.
        strike(w, raid, sh, p->x, p->y, 2, 20, 35, 3, COMBAT_NONE,
               cox_rand(w, 4) == 0 ? COX_HAZARD_POISON : 0, "Acid");
        s->next_attack[i] = w->tick + 4;
    } else if (roll == 5) {
        // Jump onto where the raider stood; lands 4 ticks later.
        int x = p->x - 1, y = p->y - 1;
        bool ok = true;
        for (int ty = 0; ty < 3 && ok; ty++)
            for (int tx = 0; tx < 3 && ok; tx++)
                ok = cox_tile_walkable(w, x + tx, y + ty, room->plane);
        if (!ok) {
            attack(w, sh, COMBAT_RANGED, max_hit_now(w, sh), 2, 0, "Spit");
            s->next_attack[i] = w->tick + 4;
            return;
        }
        s->jump_land[i] = w->tick + 4;
        s->jump_x[i] = (int16_t)x;
        s->jump_y[i] = (int16_t)y;
        sh->player_untargetable = true;
        rc_combat_record_encounter_action(w, sh, false, p->x, p->y, COMBAT_NONE, 4,
                                          "Jump");
        strike(w, raid, NULL, p->x, p->y, 1, 20, 25, 5, COMBAT_NONE, 0, NULL);
        s->next_attack[i] = w->tick + 8;
    } else if (roll == 6) {
        attack(w, sh, COMBAT_MELEE_CRUSH,
               cox_npc_max_hit(sh->stats[2], 52), 0, 0, "Melee");
        s->next_attack[i] = w->tick + 4;
    } else {
        attack(w, sh, COMBAT_RANGED, max_hit_now(w, sh), 2, 0, "Spit");
        s->next_attack[i] = w->tick + 4;
    }
}

static void shamans_tick(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    ShamanState *s = COX_ROOM_STATE(ShamanState, room);
    RcTick now = w->tick;
    if (room->flags & COX_ROOM_COMPLETE) return;
    if (s->open_tick) {
        if (now >= s->open_tick) {
            for (int k = 0; k < 2; k++) {
                RcNpc *b = cox_room_npc(w, room, ROLE_BLOCKER + k);
                if (b) rc_npc_remove(w, b->uid);
            }
            finish_room(w, raid, room);
        }
        return;
    }
    if (!s->started) {
        if (!player_inside(w, room, 3)) return;
        s->started = true;
        room->flags |= COX_ROOM_STARTED;
        room->started_tick = now;
        place_entry(w, room, &s->gate, LOC_SHAMAN_TENDRILS);
        s->next_attack[0] = now + 2;
        s->next_attack[1] = now + 4;
        s->spawn_ready = now + 10;
    }
    for (int i = 0; i < SHAMANS; i++) {
        RcNpc *sh = cox_room_npc(w, room, ROLE_SHAMAN + i);
        if (!sh) continue;
        if (s->jump_land[i]) {
            if (now >= s->jump_land[i]) {
                s->jump_land[i] = 0;
                sh->x = sh->prev_x = s->jump_x[i];
                sh->y = sh->prev_y = s->jump_y[i];
                sh->player_untargetable = false;
            }
            continue;
        }
        if (!cox_player_in_room(w, room)) continue;
        if (cox_player_distance(w, sh) > 8 || !has_los(w, sh)) {
            chase(w, sh, 8);
            continue;
        }
        if (now >= s->next_attack[i]) shaman_attack(w, raid, s, i, sh, room);
    }
    // Spawns: a 1 in 61 roll each tick, at most once every 20 seconds.
    if (now >= s->spawn_ready && cox_rand(w, 61) == 0 && cox_player_in_room(w, room)) {
        shaman_spawn_minions(w, raid, s, room);
        s->spawn_ready = now + 33;
    }
    for (int m = 0; m < MINIONS; m++) {
        RcNpc *minion = cox_room_npc(w, room, ROLE_MINION + m);
        if (!minion) continue;
        if (now >= s->minion_fuse[m]) {
            strike(w, raid, NULL, minion->x, minion->y, 2, 5, 10, 0, COMBAT_NONE,
                   0, NULL);
            rc_combat_record_encounter_action(w, minion, false, minion->x,
                                              minion->y, COMBAT_NONE, 0, "Explode");
            rc_npc_remove(w, minion->uid);
        } else {
            chase(w, minion, 1);
        }
    }
}

static void shamans_died(RcWorld *w, CoxRaid *raid, CoxRoom *room, RcNpc *npc,
                         int role) {
    (void)raid;
    ShamanState *s = COX_ROOM_STATE(ShamanState, room);
    int i = role - ROLE_SHAMAN;
    if (i < 0 || i >= SHAMANS) return;
    s->dead[i] = true;
    drop_seeds(w, npc, 5, 10);
    bool all = true;
    for (int k = 0; k < SHAMANS; k++) all &= s->dead[k];
    // The blocking spawns burst six ticks after the last shaman falls.
    if (all) s->open_tick = w->tick + 6;
}

const CoxRoomOps cox_shamans_ops = {
    .enter = shamans_enter, .tick = shamans_tick, .npc_died = shamans_died,
};

// ---------------------------------------------------------------------------
// Skeletal mystics.

enum { NPC_MYSTIC = 7604 };
#define MYSTICS 3

typedef struct {
    CoxGate gate;
    bool started;
    bool dead[MYSTICS];
    bool melee[MYSTICS];
    RcTick next_attack[MYSTICS], melee_since[MYSTICS];
} MysticState;
COX_STATE_FITS(MysticState);

static const CoxStats g_mystic = {
    .hp = 160, .att = 140, .str = 140, .def = 187, .mag = 140, .rng = 1,
    .attack_bonus = {85, 85, 85, 0, 40}, .defence_bonus = {155, 155, 75, 115, 140},
    .max_hit = 24, .max_bonus = 38, .attack_speed = 4, .style = COMBAT_MAGIC,
    .flags = COX_STATS_MAX_FORMULA,
};

static void mystics_enter(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    MysticState *s = COX_ROOM_STATE(MysticState, room);
    if (room->flags & COX_ROOM_COMPLETE) return;
    if (!(room->flags & COX_ROOM_SPAWNED)) {
        room->flags |= COX_ROOM_SPAWNED;
        int tx[9], ty[9];
        int n = cox_find_loc(w, room, LOC_SACRIFICIAL_TABLE, tx, ty, NULL, 9);
        if (n > 9) n = 9;
        for (int i = 0; i < MYSTICS; i++) {
            int lx = 16, ly = 16;
            if (n > 0) {
                int k = (i * n) / MYSTICS;
                lx = tx[k] - room->x + 2;
                ly = ty[k] - room->y;
            }
            int x, y;
            if (free_square(w, room, lx, ly, 2, &x, &y))
                cox_spawn(w, raid, room, NPC_MYSTIC + i, x, y, i + 1, &g_mystic, true);
        }
    }
    place_exit(w, room, &s->gate, LOC_MARK_OF_POWER, 2);
}

static void mystics_tick(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    (void)raid;
    MysticState *s = COX_ROOM_STATE(MysticState, room);
    if (room->flags & COX_ROOM_COMPLETE) return;
    RcTick now = w->tick;
    RcPlayer *p = &w->player;
    for (int i = 0; i < MYSTICS; i++) {
        RcNpc *m = cox_room_npc(w, room, i + 1);
        if (!m || !cox_player_in_room(w, room)) continue;
        if (!s->started) {
            if (cox_player_distance(w, m) > 10 || !has_los(w, m)) continue;
            s->started = true;
            room->flags |= COX_ROOM_STARTED;
            room->started_tick = now;
        }
        if (s->melee[i]) {
            if (player_adjacent(w, m)) {
                s->melee_since[i] = now;
            } else {
                chase(w, m, 1);
                // A mystic that cannot reach its target for 8 ticks casts again.
                if (now > s->melee_since[i] + 8) s->melee[i] = false;
                continue;
            }
        } else if (cox_player_distance(w, m) > 10 || !has_los(w, m)) {
            chase(w, m, 10);
            continue;
        }
        if (now < s->next_attack[i]) continue;
        s->next_attack[i] = now + 4;
        bool praying_magic = (p->active_prayers & PRAYER_PROTECT_MAGIC) != 0;
        if (s->melee[i] || (praying_magic && player_adjacent(w, m))) {
            attack(w, m, COMBAT_MELEE_CRUSH, cox_npc_max_hit(m->stats[2], 50), 0, 50,
                   "Melee");
        } else if (cox_rand(w, 5) == 0) {
            // Vulnerability: no damage; a successful cast drains Defence.
            RcNpcScriptedAttack spell = {.style = COMBAT_MAGIC, .delay = 3,
                .flags = RC_NPC_ATTACK_ALWAYS_ACCURATE, .key = "Vulnerability"};
            rc_combat_npc_scripted_attack(w, m, &spell);
            RcCombatCalc calc = rc_calc_npc_attack_style(
                m, rc_npc_def_for_npc(w, m), p, COMBAT_MAGIC);
            if (rc_roll_attack(&calc, &w->rng_state, false).accurate) {
                int drain = 4 + cox_rand(w, 5);
                int *def = &p->skills.boosted_level[SKILL_DEFENCE];
                *def = *def - drain < 1 ? 1 : *def - drain;
            }
        } else {
            attack(w, m, COMBAT_MAGIC, max_hit_now(w, m), 3, 50, "Fire blast");
        }
        // After each attack, half the time a mystic that can reach switches to
        // melee.
        if (!s->melee[i] && cox_rand(w, 2) == 0 && cox_player_distance(w, m) <= 3) {
            s->melee[i] = true;
            s->melee_since[i] = now;
        }
    }
}

static void mystics_died(RcWorld *w, CoxRaid *raid, CoxRoom *room, RcNpc *npc,
                         int role) {
    MysticState *s = COX_ROOM_STATE(MysticState, room);
    int i = role - 1;
    if (i < 0 || i >= MYSTICS) return;
    s->dead[i] = true;
    drop_seeds(w, npc, 5, 10);
    bool all = true;
    for (int k = 0; k < MYSTICS; k++) all &= s->dead[k];
    if (all) finish_room(w, raid, room);
}

const CoxRoomOps cox_mystics_ops = {
    .enter = mystics_enter, .tick = mystics_tick, .npc_died = mystics_died,
};

// ---------------------------------------------------------------------------
// Muttadiles.

enum { MUT_SMALL, MUT_EMERGING, MUT_LARGE };
enum { NPC_MUT_SUBMERGED = 7561, NPC_MUT_SMALL = 7562, NPC_MUT_LARGE = 7563,
       NPC_MEAT_TREE = 7564 };
enum { ROLE_MUT_SMALL = 1, ROLE_MUT_LARGE = 2, ROLE_TREE = 3 };

typedef struct {
    CoxGate gate;
    uint8_t phase;
    bool started;
    RcTick next_attack[2], next_sweep, emerge_tick;
    uint8_t meals[2], bites[2];
    bool to_tree[2];
    RcTick tree_walk_start[2];
    int16_t tree_x, tree_y;
    int32_t tree_hp;
    bool tree_down;
    uint8_t large_style, large_run;
} MuttState;
COX_STATE_FITS(MuttState);

static const CoxStats g_mutt_small = {
    .hp = 250, .att = 150, .str = 150, .def = 138, .mag = 1, .rng = 150,
    .attack_bonus = {71, 71, 71, 83, 0}, .defence_bonus = {-5, 72, 50, 0, 60},
    .max_hit = 30, .max_bonus = 56, .attack_speed = 4, .style = COMBAT_RANGED,
    .flags = COX_STATS_MAX_FORMULA,
};
static const CoxStats g_mutt_large = {
    .hp = 250, .att = 250, .str = 250, .def = 220, .mag = 250, .rng = 250,
    .attack_bonus = {88, 88, 88, 82, 0}, .defence_bonus = {-5, 82, 60, 0, 75},
    .max_hit = 45, .max_bonus = 47, .attack_speed = 4, .style = COMBAT_RANGED,
    .flags = COX_STATS_MAX_FORMULA,
};
static const int8_t g_tree_pos[3][2] = {{5, 8}, {5, 8}, {22, 5}};
static const int8_t g_mutt_large_pos[3][2] = {{18, 20}, {18, 18}, {8, 18}};
// The small one's spawn: the north- and east-exit rooms' are the recorded
// tiles (RSProx), the west's NR's.
static const int8_t g_mutt_small_pos[3][2] = {{12, 16}, {15, 5}, {15, 6}};

static void mutt_enter(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    MuttState *s = COX_ROOM_STATE(MuttState, room);
    if (room->flags & COX_ROOM_COMPLETE) return;
    if (!(room->flags & COX_ROOM_SPAWNED)) {
        room->flags |= COX_ROOM_SPAWNED;
        int v = room->variant;
        s->tree_x = (int16_t)(room->x + g_tree_pos[v][0]);
        s->tree_y = (int16_t)(room->y + g_tree_pos[v][1]);
        // Mod Ash: tree HP is 5 x the team's total Woodcutting, at least 100.
        s->tree_hp = 5 * w->player.skills.base_level[SKILL_WOODCUTTING];
        if (s->tree_hp < 100) s->tree_hp = 100;
        cox_add_loc(w, LOC_MEAT_TREE_FULL, 10, 0, s->tree_x, s->tree_y, room->plane,
                    NULL);
        // An NPC with Cut (no Attack option).
        cox_spawn(w, raid, room, NPC_MEAT_TREE, s->tree_x, s->tree_y, ROLE_TREE, NULL,
                  true);
        cox_spawn(w, raid, room, NPC_MUT_SMALL, room->x + g_mutt_small_pos[v][0],
                  room->y + g_mutt_small_pos[v][1], ROLE_MUT_SMALL, &g_mutt_small,
                  true);
        RcNpc *large = cox_spawn(w, raid, room, NPC_MUT_SUBMERGED,
                                 room->x + g_mutt_large_pos[v][0],
                                 room->y + g_mutt_large_pos[v][1], ROLE_MUT_LARGE,
                                 &g_mutt_large, true);
        if (large) large->player_untargetable = true;
    }
    place_exit(w, room, &s->gate, LOC_CRYSTAL_GREEN, 2);
}

// Below half HP a Muttadile walks to the meat tree and eats (3 meals at
// most, 3 bites of 15% each), giving up if it cannot reach the tree.
static bool mutt_heal(RcWorld *w, MuttState *s, int i, RcNpc *m) {
    if (s->tree_down || s->meals[i] >= 3) return false;
    if (!s->to_tree[i]) {
        if (m->current_hp * 2 >= m->spawn_hp) return false;
        s->to_tree[i] = true;
        s->bites[i] = 0;
        s->tree_walk_start[i] = w->tick;
    }
    if (cox_chebyshev(m->x, m->y, npc_size(w, m), s->tree_x, s->tree_y, 2) <= 1) {
        rc_npc_route_clear(m, RC_MOVEMENT_ARRIVED);
        m->current_hp += m->spawn_hp * 15 / 100;
        if (m->current_hp > m->spawn_hp) m->current_hp = m->spawn_hp;
        rc_combat_record_encounter_action(w, m, false, s->tree_x, s->tree_y,
                                          COMBAT_NONE, 0, "Eat");
        if (++s->bites[i] >= 3) {
            s->to_tree[i] = false;
            s->meals[i]++;
        }
        return true;
    }
    if (w->tick > s->tree_walk_start[i] + 25) {
        s->to_tree[i] = false;
        s->meals[i]++;
        return false;
    }
    RcRouteTarget target = rc_route_target_rectangle(s->tree_x, s->tree_y, 2, 2,
                                                     1, 1, false, false);
    if (m->route_mode != RC_NPC_ROUTE_SCRIPTED || m->route_len == 0)
        rc_npc_route_request(w, m, &target, RC_NPC_ROUTE_SCRIPTED, true);
    return true;
}

static void mutt_small_attack(RcWorld *w, MuttState *s, RcNpc *m) {
    if (player_adjacent(w, m) && cox_rand(w, 10) < 9) {
        attack(w, m, COMBAT_MELEE_STAB, cox_npc_max_hit(m->stats[2], 48), 0, 0,
               "Bite");
    } else {
        attack(w, m, COMBAT_RANGED, max_hit_now(w, m), 2, 50, "Spit");
    }
    s->next_attack[0] = w->tick + 4;
}

static void mutt_large_attack(RcWorld *w, MuttState *s, RcNpc *m) {
    s->next_attack[1] = w->tick + 4;
    if (player_adjacent(w, m)) {
        int melee = cox_npc_max_hit(m->stats[2], 55);
        if (cox_rand(w, 4) == 0)
            attack(w, m, COMBAT_MELEE_CRUSH, melee * 3 / 2, 0, 50, "Stomp");
        else
            attack(w, m, COMBAT_MELEE_STAB, melee, 0, 50, "Bite");
        return;
    }
    // At range: runs of at least three attacks of one style (Nov 2023).
    if (s->large_run >= 3 && cox_rand(w, 2) == 0) {
        s->large_style ^= 1;
        s->large_run = 0;
    }
    s->large_run++;
    if (s->large_style)
        attack(w, m, COMBAT_MAGIC, cox_npc_max_hit(m->stats[5], -8), 3, 50, "Magic");
    else
        attack(w, m, COMBAT_RANGED, max_hit_now(w, m), 2, 50, "Spit");
}

static void mutt_tick(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    (void)raid;
    MuttState *s = COX_ROOM_STATE(MuttState, room);
    if (room->flags & COX_ROOM_COMPLETE) return;
    RcTick now = w->tick;
    if (!s->started) {
        if (!player_inside(w, room, 3)) return;
        s->started = true;
        room->flags |= COX_ROOM_STARTED;
        room->started_tick = now;
        place_entry(w, room, &s->gate, LOC_MUTT_TENDRILS);
        s->next_attack[0] = now + 2;
        s->next_sweep = now + 6;
    }
    bool here = cox_player_in_room(w, room);
    if (s->phase == MUT_SMALL) {
        RcNpc *small = cox_room_npc(w, room, ROLE_MUT_SMALL);
        RcNpc *large = cox_room_npc(w, room, ROLE_MUT_LARGE);
        if (small && here && !mutt_heal(w, s, 0, small)) {
            if (cox_player_distance(w, small) > 7 || !has_los(w, small))
                chase(w, small, 7);
            else if (now >= s->next_attack[0])
                mutt_small_attack(w, s, small);
        }
        // Submerged, the large one sweeps the room: each raider in sight is
        // hit 1 time in 3.
        if (large && here && now >= s->next_sweep) {
            s->next_sweep = now + 6;
            if (has_los(w, large) && cox_rand(w, 3) == 0)
                attack(w, large, COMBAT_MAGIC, cox_npc_max_hit(large->stats[5], -8),
                       3, 50, "Sweep");
        }
        return;
    }
    RcNpc *large = cox_room_npc(w, room, ROLE_MUT_LARGE);
    if (!large) return;
    if (s->phase == MUT_EMERGING) {
        if (now >= s->emerge_tick) {
            set_form(large, NPC_MUT_LARGE);
            large->player_untargetable = false;
            s->phase = MUT_LARGE;
            s->next_attack[1] = now;
        }
        return;
    }
    if (!here || mutt_heal(w, s, 1, large)) return;
    if (!player_adjacent(w, large) && (cox_player_distance(w, large) > 8
                                       || !has_los(w, large))) {
        chase(w, large, 8);
        return;
    }
    if (now >= s->next_attack[1]) mutt_large_attack(w, s, large);
}

static void mutt_died(RcWorld *w, CoxRaid *raid, CoxRoom *room, RcNpc *npc,
                      int role) {
    MuttState *s = COX_ROOM_STATE(MuttState, room);
    if (role == ROLE_MUT_SMALL) {
        RcGroundGrant g[4] = {{ITEM_BIG_BONES, 1, 0}, {ITEM_OVERLOAD_P, 1, 0},
                              {ITEM_PRAYER_ENHANCE_P, 1, 0},
                              {ITEM_STINKHORN, 1 + cox_rand(w, 3), 0}};
        drop(w, npc->x + 1, npc->y + 1, npc->plane, g, 4);
        s->phase = MUT_EMERGING;
        s->emerge_tick = w->tick + 8;
        return;
    }
    if (role == ROLE_MUT_LARGE) {
        RcGroundGrant g[5] = {{ITEM_BIG_BONES, 1, 0}, {ITEM_OVERLOAD_P, 1, 0},
                              {ITEM_PRAYER_ENHANCE_P, 1, 0},
                              {ITEM_XERICS_AID_P, 1, 0},
                              {ITEM_REVITALISATION_P, 1, 0}};
        drop(w, npc->x + 2, npc->y + 2, npc->plane, g, 5);
        RcNpc *tree = cox_room_npc(w, room, ROLE_TREE);
        if (tree) rc_npc_remove(w, tree->uid);
        finish_room(w, raid, room);
    }
}

const CoxRoomOps cox_muttadiles_ops = {
    .enter = mutt_enter, .tick = mutt_tick, .npc_died = mutt_died,
};

static RcInteractionHandlerResult cut_meat_tree(RcWorld *world, RcPlayer *player,
                                                const RcPendingInteraction *pending,
                                                void *ctx) {
    (void)ctx;
    static const int axes[] = {1351, 1349, 1353, 1361, 1355, 1357, 1359, 6739,
                               13241, 23673, 20011, 25110, 25066, 25378, 28196};
    CoxRaid *raid = cox_raid(world);
    if (!raid || !raid->in_raid)
        return rc_interaction_result_message("Nothing interesting happens.");
    if (!cox_has_any(player, axes, (int)(sizeof(axes) / sizeof(axes[0]))))
        return rc_interaction_result_message("You need an axe to cut the meat tree.");
    cox_start_action(world, raid, COX_ACT_CUT_MEAT_TREE, pending, 1, 0);
    return rc_interaction_result_complete();
}

bool cox_combat_action(RcWorld *world, CoxRaid *raid, CoxAction *a) {
    if (a->kind != COX_ACT_CUT_MEAT_TREE) return false;
    RcNpc *tree = rc_npc_resolve(world, (RcNpcId)a->target_id);
    int role = 0;
    CoxRoom *room = tree ? cox_room_for_npc(raid, tree->uid, &role) : NULL;
    if (!room || role != ROLE_TREE) return false;
    MuttState *s = COX_ROOM_STATE(MuttState, room);
    a->next_tick = world->tick + 5;
    // Mod Ash: 20% at level 1 to 78% at 99; floor(level / 2) - 7 to + 3.
    int wc = world->player.skills.boosted_level[SKILL_WOODCUTTING];
    if (cox_rand(world, 100) >= 20 + 58 * (wc - 1) / 98) return true;
    int damage = wc / 2 - 7 + cox_rand(world, 11);
    if (damage < 1) damage = 1;
    s->tree_hp -= damage;
    cox_add_xp(world, SKILL_WOODCUTTING, 4500);
    cox_add_points(raid, 3);
    if (s->tree_hp > 0) return true;
    s->tree_down = true;
    cox_replace_loc(world, LOC_MEAT_TREE_FULL, s->tree_x, s->tree_y, room->plane,
                    LOC_MEAT_TREE_EMPTY);
    rc_npc_remove(world, tree->uid);
    cox_message(world, "You chop down the meat tree.");
    return false;
}

// ---------------------------------------------------------------------------
// Vespula.

enum { NPC_VESPULA = 7530, NPC_VESPULA_ENRAGED = 7531, NPC_VESPULA_GROUNDED = 7532,
       NPC_PORTAL = 7533, NPC_GRUB = 7534, NPC_GRUB_SICKLY = 7535,
       NPC_GRUB_INFECTED = 7536, NPC_GRUB_DEAD = 7537,
       NPC_SOLDIER = 7538, NPC_SOLDIER_GROUNDED = 7539 };
enum { ROLE_VESPULA = 1, ROLE_PORTAL = 2, ROLE_GRUB = 3, ROLE_SOLDIER = 7 };
#define GRUBS 4

typedef struct {
    CoxGate gate;
    bool started, grounded, provoked;
    int8_t target_grub;
    RcTick phase_tick, next_attack, enrage_until, next_drain, roots_tick;
    int16_t drain_x, drain_y;
    struct {
        bool infected, hatched;
        RcTick next_drain, regrow_tick;
    } grub[GRUBS];
    RcTick soldier_heal[GRUBS], soldier_fuse[GRUBS];
    int16_t root_x[6], root_y[6];
    RcTick root_regrow[6];
    uint8_t root_count;
    bool roots_flowered;
} VespState;
COX_STATE_FITS(VespState);

static const CoxStats g_vespula = {
    .hp = 200, .att = 150, .str = 150, .def = 88, .mag = 88, .rng = 150,
    .defence_bonus = {0, 0, 0, 60, 70}, .max_hit = 14, .attack_speed = 3,
    .style = COMBAT_RANGED, .flags = COX_STATS_MAGIC_DEFENSIVE,
};
static const CoxStats g_portal = {
    .hp = 250, .att = 1, .str = 1, .def = 176, .mag = 176, .rng = 1,
    .defence_bonus = {0, 0, 0, 140, 60}, .attack_speed = 2, .style = COMBAT_NONE,
    .flags = COX_STATS_MAGIC_DEFENSIVE,
};
static const CoxStats g_grub = {.hp = 125, .att = 1, .str = 1, .def = 1, .mag = 1,
                                .rng = 1, .style = COMBAT_NONE,
                                .flags = COX_STATS_SINGLES};
static const CoxStats g_soldier = {
    .hp = 100, .att = 150, .str = 150, .def = 40, .mag = 40, .rng = 1,
    .defence_bonus = {0, 0, 0, 0, 30}, .max_hit = 14,
    .attack_speed = 4, .style = COMBAT_MELEE_CRUSH,
    .flags = COX_STATS_SINGLES | COX_STATS_MAGIC_DEFENSIVE,
};
static const int8_t g_vesp_pos[3][2] = {{15, 8}, {10, 10}, {11, 8}};
static const int8_t g_portal_pos[3][2] = {{17, 20}, {22, 11}, {11, 20}};
static const int8_t g_grub_pos[3][GRUBS][2] = {
    {{12, 5}, {22, 8}, {20, 13}, {13, 13}},
    {{15, 8}, {7, 10}, {15, 16}, {7, 16}},
    {{8, 13}, {15, 13}, {8, 5}, {5, 9}},
};

static void vespula_enter(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    VespState *s = COX_ROOM_STATE(VespState, room);
    if (room->flags & COX_ROOM_COMPLETE) return;
    if (!(room->flags & COX_ROOM_SPAWNED)) {
        room->flags |= COX_ROOM_SPAWNED;
        int v = room->variant;
        s->target_grub = -1;
        cox_spawn(w, raid, room, NPC_VESPULA, room->x + g_vesp_pos[v][0],
                  room->y + g_vesp_pos[v][1], ROLE_VESPULA, &g_vespula, true);
        cox_spawn(w, raid, room, NPC_PORTAL, room->x + g_portal_pos[v][0],
                  room->y + g_portal_pos[v][1], ROLE_PORTAL, &g_portal, true);
        // Grubs have no Attack option; stung ones offer Feed.
        for (int i = 0; i < GRUBS; i++)
            cox_spawn(w, raid, room, NPC_GRUB, room->x + g_grub_pos[v][i][0],
                      room->y + g_grub_pos[v][i][1], ROLE_GRUB + i, &g_grub, true);
        int xs[6], ys[6];
        int n = cox_find_loc(w, room, LOC_ROOT_PICKED, xs, ys, NULL, 6);
        s->root_count = (uint8_t)(n > 6 ? 6 : n);
        for (int i = 0; i < s->root_count; i++) {
            s->root_x[i] = (int16_t)xs[i];
            s->root_y[i] = (int16_t)ys[i];
        }
    }
    place_exit(w, room, &s->gate, LOC_BOIL, 1);
}

static void vespula_hatch(RcWorld *w, CoxRaid *raid, VespState *s, CoxRoom *room,
                          int i, RcNpc *grub) {
    s->grub[i].infected = false;
    s->grub[i].hatched = true;
    s->grub[i].regrow_tick = w->tick + 37;
    set_form(grub, NPC_GRUB_DEAD);
    if (!cox_room_npc(w, room, ROLE_SOLDIER + i)) {
        RcNpc *soldier = cox_spawn(w, raid, room, NPC_SOLDIER, grub->x, grub->y,
                                   ROLE_SOLDIER + i, &g_soldier, false);
        if (soldier) {
            rc_combat_start_npc_vs_player(w, soldier->uid, 0);
            s->soldier_heal[i] = w->tick + 9;
            s->soldier_fuse[i] = 0;
        }
    }
}

static void vespula_grubs(RcWorld *w, CoxRaid *raid, VespState *s, CoxRoom *room,
                          RcNpc *vesp) {
    RcTick now = w->tick;
    for (int i = 0; i < GRUBS; i++) {
        RcNpc *grub = cox_room_npc(w, room, ROLE_GRUB + i);
        if (!grub) continue;
        if (s->grub[i].hatched) {
            if (now >= s->grub[i].regrow_tick) {
                s->grub[i].hatched = false;
                grub->current_hp = grub->spawn_hp;
                set_form(grub, NPC_GRUB);
            }
            continue;
        }
        if (s->grub[i].infected && now >= s->grub[i].next_drain) {
            s->grub[i].next_drain = now + 5;
            if (--grub->current_hp <= 0) vespula_hatch(w, raid, s, room, i, grub);
        }
    }
    // She flies to an unstung grub and stings it: it drops to about 56%
    // and drains a hitpoint every 5 ticks until a soldier hatches.
    if (s->grounded || s->enrage_until > now) return;
    if (s->target_grub < 0) {
        int pick[GRUBS], n = 0;
        for (int i = 0; i < GRUBS; i++)
            if (!s->grub[i].infected && !s->grub[i].hatched
                    && cox_room_npc(w, room, ROLE_GRUB + i))
                pick[n++] = i;
        if (n) s->target_grub = (int8_t)pick[cox_rand(w, n)];
        return;
    }
    RcNpc *grub = cox_room_npc(w, room, ROLE_GRUB + s->target_grub);
    if (!grub) {
        s->target_grub = -1;
        return;
    }
    if (cox_chebyshev(vesp->x, vesp->y, 5, grub->x, grub->y, 3) <= 1) {
        int i = s->target_grub;
        rc_combat_record_encounter_action(w, vesp, false, grub->x, grub->y,
                                          COMBAT_NONE, 1, "Sting");
        grub->current_hp = grub->current_hp * 56 / 100;
        set_form(grub, NPC_GRUB_INFECTED);
        s->grub[i].infected = true;
        s->grub[i].next_drain = now + 5;
        s->target_grub = -1;
    } else {
        RcRouteTarget target = rc_route_target_rectangle(grub->x, grub->y, 3, 3, 1,
                                                         1, false, false);
        // No way closer (her five tiles do not fit, or the route ends short
        // of the grub): she tries another one.
        if ((vesp->route_mode != RC_NPC_ROUTE_SCRIPTED || vesp->route_len == 0)
                && (!rc_npc_route_request(w, vesp, &target, RC_NPC_ROUTE_SCRIPTED,
                                          true)
                    || vesp->route_len == 0))
            s->target_grub = -1;
    }
}

static void vespula_soldiers(RcWorld *w, CoxRaid *raid, VespState *s,
                             CoxRoom *room) {
    RcTick now = w->tick;
    RcNpc *portal = cox_room_npc(w, room, ROLE_PORTAL);
    for (int i = 0; i < GRUBS; i++) {
        RcNpc *soldier = cox_room_npc(w, room, ROLE_SOLDIER + i);
        if (!soldier) continue;
        if (s->soldier_fuse[i]) {
            // Grounded at low HP; it bursts 21 ticks later.
            if (now >= s->soldier_fuse[i]) {
                strike(w, raid, NULL, soldier->x + 1, soldier->y + 1, 2, 15, 15, 0,
                       COMBAT_NONE, 0, NULL);
                rc_combat_record_encounter_action(w, soldier, false, soldier->x,
                                                  soldier->y, COMBAT_NONE, 0,
                                                  "Explode");
                rc_npc_remove(w, soldier->uid);
            }
            continue;
        }
        if (soldier->current_hp <= 6) {
            set_form(soldier, NPC_SOLDIER_GROUNDED);
            soldier->owner_controlled = true;
            s->soldier_fuse[i] = now + 21;
            continue;
        }
        // Living soldiers heal the portal 3% every 5 ticks.
        if (portal && now >= s->soldier_heal[i]) {
            portal->current_hp += portal->spawn_hp * 3 / 100;
            if (portal->current_hp > portal->spawn_hp)
                portal->current_hp = portal->spawn_hp;
            s->soldier_heal[i] = now + 5;
        }
    }
}

static void vespula_tick(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    VespState *s = COX_ROOM_STATE(VespState, room);
    if (room->flags & COX_ROOM_COMPLETE) return;
    RcNpc *vesp = cox_room_npc(w, room, ROLE_VESPULA);
    RcTick now = w->tick;
    RcPlayer *p = &w->player;
    if (!s->started) {
        if (!player_inside(w, room, 3)) return;
        s->started = true;
        room->flags |= COX_ROOM_STARTED;
        room->started_tick = now;
        place_entry(w, room, &s->gate, LOC_VESPULA_TENDRILS);
        s->roots_tick = now + 10 + (RcTick)cox_rand(w, 6);
        for (int i = 0; i < GRUBS; i++) s->grub[i].next_drain = now + 10;
    }
    if (!s->roots_flowered && now >= s->roots_tick) {
        s->roots_flowered = true;
        for (int i = 0; i < s->root_count; i++)
            cox_replace_loc(w, LOC_ROOT_PICKED, s->root_x[i], s->root_y[i],
                            room->plane, LOC_ROOT_FLOWERED);
    }
    for (int i = 0; i < s->root_count; i++) {
        if (!s->root_regrow[i] || now < s->root_regrow[i]) continue;
        s->root_regrow[i] = 0;
        cox_replace_loc(w, LOC_ROOT_PICKED, s->root_x[i], s->root_y[i], room->plane,
                        LOC_ROOT_FLOWERED);
    }
    if (!vesp) return;
    vespula_grubs(w, raid, s, room, vesp);
    vespula_soldiers(w, raid, s, room);
    if (s->grounded) {
        if (now >= s->phase_tick + 15) {
            // She takes off and regains most of her health.
            s->grounded = false;
            set_form(vesp, NPC_VESPULA);
            vesp->current_hp = vesp->spawn_hp * (89 + cox_rand(w, 12)) / 100;
            if (vesp->current_hp > vesp->spawn_hp) vesp->current_hp = vesp->spawn_hp;
        }
        return;
    }
    // The portal drains its attacker's prayer while they keep attacking it
    // from the same tile.
    if (s->next_drain && now >= s->next_drain) {
        if (p->x != s->drain_x || p->y != s->drain_y) {
            s->next_drain = 0;
        } else {
            RcNpc *portal = cox_room_npc(w, room, ROLE_PORTAL);
            if (portal) {
                if (p->current_prayer_points >= 30) {
                    p->current_prayer_points -= 30;
                } else {
                    p->current_prayer_points = 0;
                    rc_combat_damage_player(w, 3, 0, COMBAT_NONE, (int)portal->uid);
                }
                rc_combat_record_encounter_action(w, portal, true, p->x, p->y,
                                                  COMBAT_MAGIC, 2, "Drain");
            }
            s->next_drain = now + 2;
        }
    }
    if (s->enrage_until > now) {
        // Enraged: stings raiders in reach and stomps those beneath.
        if (player_under(w, vesp))
            rc_combat_damage_player(w, 2 + cox_rand(w, 7), 0, COMBAT_NONE,
                                    (int)vesp->uid);
        if (now >= s->next_attack && player_adjacent(w, vesp)) {
            RcNpcScriptedAttack sting = {.style = COMBAT_NONE, .min_hit = 5,
                .max_hit = 20, .delay = 0, .protected_pct = 100,
                .flags = RC_NPC_ATTACK_ALWAYS_ACCURATE, .key = "Sting"};
            rc_combat_npc_scripted_attack(w, vesp, &sting);
            s->next_attack = now + 3;
        }
        return;
    }
    if (s->enrage_until && now >= s->enrage_until) {
        s->enrage_until = 0;
        set_form(vesp, NPC_VESPULA);
    }
    // She shoots back at whoever attacked her.
    if (s->provoked && now >= s->next_attack && cox_player_in_room(w, room)) {
        if (cox_player_distance(w, vesp) <= 10 && has_los(w, vesp)) {
            attack(w, vesp, COMBAT_RANGED, max_hit_now(w, vesp), 2, 50, "Ranged");
            s->next_attack = now + 3;
        }
    }
}

static void vespula_hit(RcWorld *w, CoxRaid *raid, CoxRoom *room, RcNpc *npc,
                        int role, int damage) {
    (void)raid;
    (void)damage;
    VespState *s = COX_ROOM_STATE(VespState, room);
    RcNpc *vesp = cox_room_npc(w, room, ROLE_VESPULA);
    if (role == ROLE_VESPULA) {
        s->provoked = true;
        // At about 23% she lands for 15 ticks.
        if (!s->grounded && npc->current_hp * 100 <= npc->spawn_hp * 23) {
            s->grounded = true;
            s->phase_tick = w->tick;
            s->enrage_until = 0;
            set_form(npc, NPC_VESPULA_GROUNDED);
        }
    } else if (role == ROLE_PORTAL) {
        if (!s->next_drain) {
            s->next_drain = w->tick + 1;
            s->drain_x = (int16_t)w->player.x;
            s->drain_y = (int16_t)w->player.y;
        }
        if (vesp && !s->grounded) {
            s->enrage_until = w->tick + 20;
            set_form(vesp, NPC_VESPULA_ENRAGED);
        }
    }
}

static void vespula_died(RcWorld *w, CoxRaid *raid, CoxRoom *room, RcNpc *npc,
                         int role) {
    VespState *s = COX_ROOM_STATE(VespState, room);
    if (role >= ROLE_SOLDIER && role < ROLE_SOLDIER + GRUBS) return;
    if (role != ROLE_PORTAL) return;
    (void)s;
    // Destroying the portal kills Vespula, her soldiers and the grubs.
    RcNpc *vesp = cox_room_npc(w, room, ROLE_VESPULA);
    int dx = vesp ? vesp->x + 2 : npc->x, dy = vesp ? vesp->y + 4 : npc->y;
    for (int r = 1; r <= ROLE_SOLDIER + GRUBS; r++) {
        if (r == ROLE_PORTAL) continue;
        RcNpc *n = cox_room_npc(w, room, r);
        if (n) rc_npc_remove(w, n->uid);
    }
    RcGroundGrant g[4] = {{ITEM_XERICS_AID_P, 2, 0}, {ITEM_REVITALISATION_P, 1, 0},
                          {ITEM_PRAYER_ENHANCE_P, 1, 0}, {ITEM_OVERLOAD_P, 1, 0}};
    drop(w, dx, dy, room->plane, g, 4);
    finish_room(w, raid, room);
}

// Her ranged attacks poison from 20 and her soldiers' melee from 15 (wiki).
// The wiki gives no chance; 1 in 4 damaging hits is an estimate.
static void vespula_hit_player(RcWorld *w, CoxRaid *raid, CoxRoom *room,
                               RcNpc *npc, int role, const RcPendingHit *hit,
                               int damage) {
    (void)raid;
    (void)room;
    if (damage <= 0 || cox_rand(w, 4) != 0) return;
    if (role == ROLE_VESPULA && hit->attack_style == COMBAT_RANGED)
        rc_player_apply_toxin(w, 20, false);
    else if (role >= ROLE_SOLDIER && role < ROLE_SOLDIER + GRUBS
             && rc_npc_def_for_npc(w, npc)->id == NPC_SOLDIER)
        rc_player_apply_toxin(w, 15, false);
}

const CoxRoomOps cox_vespula_ops = {
    .enter = vespula_enter, .tick = vespula_tick, .npc_died = vespula_died,
    .npc_hit = vespula_hit, .hit_player = vespula_hit_player,
};

static RcInteractionHandlerResult pick_root(RcWorld *world, RcPlayer *player,
                                            const RcPendingInteraction *pending,
                                            void *ctx) {
    (void)player;
    (void)ctx;
    CoxRaid *raid = cox_raid(world);
    int x = pending->target.tile_x, y = pending->target.tile_y;
    CoxRoom *room = raid ? cox_room_at(raid, x, y, pending->target.plane) : NULL;
    if (!room || room->type != COX_ROOM_VESPULA)
        return rc_interaction_result_message("Nothing interesting happens.");
    if (!cox_give_item(world, ITEM_BLOSSOM, 1))
        return rc_interaction_result_message("You don't have enough inventory space.");
    cox_replace_loc(world, LOC_ROOT_FLOWERED, x, y, room->plane, LOC_ROOT_PICKED);
    // It flowers again a little later (30 ticks: an estimate).
    VespState *s = COX_ROOM_STATE(VespState, room);
    for (int i = 0; i < s->root_count; i++)
        if (s->root_x[i] == x && s->root_y[i] == y) s->root_regrow[i] = world->tick + 30;
    return rc_interaction_result_complete();
}

static RcInteractionHandlerResult feed_grub(RcWorld *world, RcPlayer *player,
                                            const RcPendingInteraction *pending,
                                            void *ctx) {
    (void)player;
    (void)ctx;
    CoxRaid *raid = cox_raid(world);
    RcNpc *grub = rc_npc_resolve(world, (RcNpcId)pending->target.entity_uid);
    int role = 0;
    CoxRoom *room = raid && grub ? cox_room_for_npc(raid, grub->uid, &role) : NULL;
    if (!room || role < ROLE_GRUB || role >= ROLE_GRUB + GRUBS)
        return rc_interaction_result_message("Nothing interesting happens.");
    VespState *s = COX_ROOM_STATE(VespState, room);
    int i = role - ROLE_GRUB;
    if (s->grub[i].hatched)
        return rc_interaction_result_message("This grub has already hatched.");
    if (!cox_take_item(world, ITEM_BLOSSOM, 1))
        return rc_interaction_result_message("You need a medivaemia blossom to feed it.");
    grub->current_hp += 35;
    if (grub->current_hp > grub->spawn_hp) grub->current_hp = grub->spawn_hp;
    return rc_interaction_result_message("You feed the blossom to the grub.");
}

// ---------------------------------------------------------------------------
// Damage rules for the raid's NPCs.

static int pickaxe_requirement(int weapon_id) {
    switch (weapon_id) {
    case 1265: case 1267: return 1;     // bronze, iron
    case 1269: return 6;                // steel
    case 12297: return 11;              // black
    case 1273: return 21;               // mithril
    case 1271: return 31;               // adamant
    case 1275: case 23276: return 41;   // rune, gilded
    case 11920: case 12797: case 13243: case 23677: case 20014: case 25063:
    case 25376: case 28217:
        return 61;                      // dragon and better
    default: return -1;
    }
}

int cox_modify_player_hit(RcWorld *world, const RcNpc *target, RcCombatStyle style,
                          int spell_idx, int weapon_id, int damage) {
    (void)spell_idx;
    CoxRaid *raid = cox_raid(world);
    if (!raid || !raid->in_raid || !target || damage <= 0) return damage;
    int role = 0;
    CoxRoom *room = cox_room_for_npc(raid, (RcNpcId)target->uid, &role);
    if (!room) return damage;
    bool melee = style >= COMBAT_MELEE_STAB && style <= COMBAT_MELEE_CRUSH;
    int id = form_id(world, target);
    switch (room->type) {
    case COX_ROOM_TEKTON: {
        TektonState *s = COX_ROOM_STATE(TektonState, room);
        if (s->phase == TEK_ANVIL || style == COMBAT_RANGED) return 0;
        return style == COMBAT_MAGIC ? damage / 5 : damage;
    }
    case COX_ROOM_VASA: {
        VasaState *s = COX_ROOM_STATE(VasaState, room);
        if (role == ROLE_VASA) {
            bool immune = s->phase == VASA_SIPHON || s->phase == VASA_WAKING
                || s->phase == VASA_DORMANT
                || (s->phase == VASA_SPECIAL && world->tick < s->phase_tick + 10);
            return immune ? 0 : damage;
        }
        // Glowing crystals: no ranged damage, a third of magic.
        if (style == COMBAT_RANGED) return 0;
        return style == COMBAT_MAGIC ? damage / 3 : damage;
    }
    case COX_ROOM_GUARDIANS: {
        int req = melee ? pickaxe_requirement(weapon_id) : -1;
        if (req < 0) return 0;
        int mining = world->player.skills.boosted_level[SKILL_MINING];
        return damage * (50 + mining + req) / 150;
    }
    case COX_ROOM_ICE_DEMON: {
        // A third of most damage; fire spells deal 2.5 times and demonbane
        // 115% (since Jul 2026).
        const RcSpellDef *spell = spell_idx >= 0 ? rc_spell_def_get(spell_idx) : NULL;
        if (spell && strncmp(spell->name, "Fire", 4) == 0) return damage * 5 / 2;
        if (weapon_id == 19675 || weapon_id == 6746 || weapon_id == 2402
                || weapon_id == 29589)
            return damage * 115 / 100;
        return damage * 33 / 100;
    }
    case COX_ROOM_CRABS:
        return 0;   // the crabs cannot be hurt
    case COX_ROOM_OLM:
        return cox_olm_modify_hit(world, room, role, style, damage);
    case COX_ROOM_VESPULA:
        if (role == ROLE_PORTAL && melee) return 0;
        if (role == ROLE_VESPULA && melee && id != NPC_VESPULA_GROUNDED) return 0;
        if (role >= ROLE_SOLDIER && id == NPC_SOLDIER && melee) return 0;
        if (role >= ROLE_SOLDIER && id == NPC_SOLDIER_GROUNDED
                && style == COMBAT_RANGED) return 0;
        return damage;
    default:
        return damage;
    }
}

int cox_combat_rooms_register(RcWorld *world) {
    int ok = 1;
    static const int gates[5] = {LOC_TEKTON_FIRE, LOC_VASA_FIRE, LOC_MUTT_TENDRILS,
                                 LOC_SHAMAN_TENDRILS, LOC_VESPULA_TENDRILS};
    for (int i = 0; i < 5; i++) ok &= cox_register_object(world, gates[i], 0, cross_gate);
    ok &= cox_register_npc(world, NPC_MEAT_TREE, 0, cut_meat_tree);
    ok &= cox_register_object(world, LOC_ROOT_FLOWERED, 0, pick_root);
    ok &= cox_register_npc(world, NPC_GRUB_SICKLY, 0, feed_grub);
    ok &= cox_register_npc(world, NPC_GRUB_INFECTED, 0, feed_grub);
    for (int g = 0; g < 4; g++) {
        RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
        key.kind = RC_INTERACTION_NPC;
        key.op = RC_INTERACTION_USE_ON;
        key.definition_id = NPC_GRUB + g;
        key.source_item_id = ITEM_BLOSSOM;
        ok &= rc_interaction_register_world_handler(world, &key, feed_grub, NULL) != 0;
    }
    return ok;
}
