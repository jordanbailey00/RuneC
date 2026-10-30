#include "cox.h"

#include "../../rc-core/api.h"
#include "../../rc-core/combat.h"
#include "../../rc-core/combat_hit.h"
#include "../../rc-core/interaction.h"
#include "../../rc-core/items.h"
#include "../../rc-core/object_runtime.h"
#include "../../rc-core/pathfinding.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Puzzle rooms: the Ice demon, the Thieving room (chests, grubs and the
// corrupted scavenger), the Tightrope (deathly guards and the keystone) and
// the Jewelled crabs' orb puzzle.

enum {
    ITEM_KINDLING = 20799,
    ITEM_TINDERBOX = 590,
    ITEM_GRUBS = 20885,
    ITEM_JOURNAL = 20886,
    ITEM_KEYSTONE = 20884,
    ITEM_LOCKPICK = 1523,
    ITEM_HAIR_CLIP = 11769,
    ITEM_PSYKK_BAT = 20883,
    ITEM_STINKHORN = 20910,
    ITEM_JUICE = 20911,
    ITEM_CICELY = 20912,
    ITEM_HAMMER = 2347,
};

enum {
    LOC_PASSAGE = 29789,
    LOC_BRAZIER = 29747,
    LOC_BRAZIER_LIT = 29748,
    LOC_ICE_STORM = 29876,
    LOC_CHEST = 29742,
    LOC_CHEST_OPEN = 29743,
    LOC_CHEST_COCOONS = 29744,
    LOC_CHEST_HATCHED = 29745,
    LOC_TROUGH = 29746,
    LOC_TROUGH_FULL = 29874,
    LOC_KEEPER = 29875,
    LOC_INVISIBLE_BLOCK = 26209,
    LOC_TIGHTROPE = 29750,
    LOC_KEYSTONE = 29751,
    LOC_BARRIER = 29749,
    LOC_RELIEF = 29752,
    LOC_CRYSTAL_BLACK = 29758,
    LOC_CRYSTAL_CYAN = 29759,
    LOC_CRYSTAL_MAGENTA = 29760,
    LOC_CRYSTAL_YELLOW = 29761,
    LOC_CRYSTAL_WHITE = 29762,
    LOC_BIG_CRYSTAL = 29753,          // stages 29753..29757
};

static RcInteractionHandlerResult say(const char *text) {
    return rc_interaction_result_message(text);
}

static CoxRoom *room_of(RcWorld *w, const RcPendingInteraction *pending,
                        int type) {
    CoxRaid *raid = cox_raid(w);
    if (!raid || !raid->in_raid) return NULL;
    CoxRoom *room = cox_room_at(raid, pending->target.tile_x,
                                pending->target.tile_y, pending->target.plane);
    if (!room) {
        RcNpc *npc = pending->target.kind == RC_INTERACTION_NPC
                   ? rc_npc_resolve(w, (RcNpcId)pending->target.entity_uid) : NULL;
        int role;
        room = npc ? cox_room_for_npc(raid, npc->uid, &role) : NULL;
    }
    return room && room->type == type ? room : NULL;
}

// The exit passage's room-side tile row, as the combat rooms gate it.
static bool exit_gate_tile(const RcWorld *w, const CoxRoom *room, int size,
                           int *x, int *y) {
    int px, py, rot;
    if (cox_find_loc(w, room, LOC_PASSAGE, &px, &py, &rot, 1) != 1) return false;
    switch (room->variant) {
    case COX_EXIT_WEST: *x = px + 3; *y = py; break;
    case COX_EXIT_NORTH: *x = px; *y = py - size; break;
    default: *x = px - size; *y = py; break;
    }
    return true;
}

static void gate_pair(RcWorld *w, const CoxRoom *room, int loc, int x, int y,
                      bool add) {
    bool along_x = room->variant == COX_EXIT_NORTH;
    for (int k = 0; k < 2; k++) {
        int gx = x + (k && along_x ? 1 : 0), gy = y + (k && !along_x ? 1 : 0);
        if (add) cox_add_loc(w, loc, 10, 0, gx, gy, room->plane, NULL);
        else cox_remove_loc(w, loc, gx, gy, room->plane);
    }
}

static bool free_tile(const RcWorld *w, const CoxRoom *room, int lx, int ly,
                      int size, int *out_x, int *out_y) {
    for (int r = 0; r < 10; r++)
        for (int dy = -r; dy <= r; dy++)
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
    return false;
}

static int linear_pct(int level, int at1, int at99) {
    if (level < 1) level = 1;
    if (level > 99) level = 99;
    return at1 + (at99 - at1) * (level - 1) / 98;
}

static void drop(RcWorld *w, int x, int y, int plane, const RcGroundGrant *g,
                 int count) {
    rc_ground_grant(w, g, count, x, y, plane, RC_GROUND_OWNER_LOCAL_PLAYER,
                    (RcGroundPolicy){0, 3000, 0, 3000});
}

// ---------------------------------------------------------------------------
// Ice demon: keep the braziers burning with kindling until the demon thaws.

enum { NPC_ICE_DEMON_FROZEN = 7584, NPC_ICE_DEMON = 7585, NPC_ICEFIEND = 7586 };
enum { ICE_FROZEN, ICE_THAWING, ICE_FIGHT };
#define BRAZIERS 4
#define FUEL_PER_KINDLING 10
// Burning brazier-ticks to thaw the demon solo: about 54 kindling (wiki).
#define THAW_SOLO (54 * FUEL_PER_KINDLING)

typedef struct {
    uint8_t phase;
    int16_t bx[BRAZIERS], by[BRAZIERS];
    int16_t fuel[BRAZIERS];
    uint8_t brazier_count;
    int32_t thaw;
    int8_t fiend_brazier;
    RcTick fiend_next, phase_tick, next_attack;
    int16_t storm_x, storm_y;
    bool storm_placed;
} IceState;
COX_STATE_FITS(IceState);

static const CoxStats g_ice_demon = {
    .hp = 140, .att = 1, .str = 1, .def = 160, .mag = 390, .rng = 390,
    .defence_bonus = {70, 70, 110, 140, 40}, .max_hit = 25,
    .attack_speed = 3, .style = COMBAT_MAGIC,
};
static const int8_t g_ice_demon_pos[3][2] = {{7, 21}, {14, 24}, {21, 18}};

static void ice_enter(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    IceState *s = COX_ROOM_STATE(IceState, room);
    if (room->flags & COX_ROOM_COMPLETE) return;
    if (!(room->flags & COX_ROOM_SPAWNED)) {
        room->flags |= COX_ROOM_SPAWNED;
        int xs[BRAZIERS], ys[BRAZIERS];
        int n = cox_find_loc(w, room, LOC_BRAZIER, xs, ys, NULL, BRAZIERS);
        s->brazier_count = (uint8_t)(n > BRAZIERS ? BRAZIERS : n);
        for (int i = 0; i < s->brazier_count; i++) {
            s->bx[i] = (int16_t)xs[i];
            s->by[i] = (int16_t)ys[i];
        }
        int x, y;
        if (free_tile(w, room, g_ice_demon_pos[room->variant][0],
                      g_ice_demon_pos[room->variant][1], 2, &x, &y)) {
            RcNpc *demon = cox_spawn(w, raid, room, NPC_ICE_DEMON_FROZEN, x, y, 1,
                                     &g_ice_demon, true);
            if (demon) demon->player_untargetable = true;
        }
        // One icefiend (per raider, at most four) by a brazier.
        if (s->brazier_count) {
            s->fiend_brazier = (int8_t)cox_rand(w, s->brazier_count);
            int fx = s->bx[s->fiend_brazier], fy = s->by[s->fiend_brazier];
            if (free_tile(w, room, fx - room->x + 1, fy - room->y, 1, &x, &y)) {
                RcNpc *fiend = cox_spawn(w, raid, room, NPC_ICEFIEND, x, y, 2, NULL,
                                         true);
                if (fiend) fiend->player_untargetable = true;
            }
        }
    }
    if (!s->storm_placed) {
        int x, y;
        if (exit_gate_tile(w, room, 2, &x, &y)) {
            s->storm_placed = true;
            s->storm_x = (int16_t)x;
            s->storm_y = (int16_t)y;
            cox_add_loc(w, LOC_ICE_STORM, 10, 0, x, y, room->plane, NULL);
        }
    }
}

static int brazier_index(const IceState *s, int x, int y) {
    for (int i = 0; i < s->brazier_count; i++)
        if (s->bx[i] == x && s->by[i] == y) return i;
    return -1;
}

static void ice_tick(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    IceState *s = COX_ROOM_STATE(IceState, room);
    if (room->flags & COX_ROOM_COMPLETE) return;
    RcTick now = w->tick;
    RcNpc *demon = cox_room_npc(w, room, 1);
    if (s->phase == ICE_FROZEN) {
        // Burning braziers thaw the demon; each burns its fuel down.
        for (int i = 0; i < s->brazier_count; i++) {
            if (s->fuel[i] <= 0) continue;
            s->thaw++;
            if (--s->fuel[i] == 0)
                cox_replace_loc(w, LOC_BRAZIER_LIT, s->bx[i], s->by[i], room->plane,
                                LOC_BRAZIER);
        }
        // The icefiend acts every 3-4 ticks while its brazier burns; 1 in 6
        // takes a kindling's worth of fuel.
        int f = s->fiend_brazier;
        if (f >= 0 && now >= s->fiend_next) {
            s->fiend_next = now + 3 + (RcTick)cox_rand(w, 2);
            if (s->fuel[f] > 0 && cox_rand(w, 6) == 0) {
                s->fuel[f] -= FUEL_PER_KINDLING;
                if (s->fuel[f] <= 0) {
                    s->fuel[f] = 0;
                    cox_replace_loc(w, LOC_BRAZIER_LIT, s->bx[f], s->by[f],
                                    room->plane, LOC_BRAZIER);
                }
            }
        }
        if (s->thaw >= THAW_SOLO && demon) {
            RcNpc *fiend = cox_room_npc(w, room, 2);
            if (fiend) rc_npc_remove(w, fiend->uid);
            s->phase = ICE_THAWING;
            s->phase_tick = now;
            cox_message(w, "The ice demon begins to thaw.");
        }
        return;
    }
    if (!demon) return;
    if (s->phase == ICE_THAWING) {
        if (now >= s->phase_tick + 8) {
            int idx = rc_npc_def_find(NPC_ICE_DEMON);
            if (idx >= 0) rc_npc_set_form(demon, idx);
            demon->player_untargetable = false;
            s->phase = ICE_FIGHT;
            s->next_attack = now + 2;
            room->flags |= COX_ROOM_STARTED;
            room->started_tick = now;
        }
        return;
    }
    if (!cox_player_in_room(w, room) || now < s->next_attack) return;
    s->next_attack = now + 3;
    // It reads the raider's overhead: a boulder at Protect from Missiles,
    // Ice Burst at Protect from Magic; each lands on a 3x3 around them.
    const RcPlayer *p = &w->player;
    bool missiles = (p->active_prayers & PRAYER_PROTECT_RANGE) != 0;
    bool magic = (p->active_prayers & PRAYER_PROTECT_MAGIC) != 0;
    RcCombatStyle style = missiles ? COMBAT_RANGED : magic ? COMBAT_MAGIC
                        : cox_rand(w, 2) ? COMBAT_RANGED : COMBAT_MAGIC;
    int max = rc_npc_max_hit(demon, rc_npc_def_for_npc(w, demon));
    int d = cox_player_distance(w, demon);
    int delay = style == COMBAT_RANGED ? 2 + d / 3 : 1 + (1 + d) / 3;
    CoxHazard h = {.kind = COX_HAZARD_STRIKE, .radius = 1, .style = (uint8_t)style,
                   .flags = COX_HAZARD_PRAYER_HALVES, .x = (int16_t)p->x,
                   .y = (int16_t)p->y, .plane = (int16_t)p->plane,
                   .land_tick = now + (RcTick)delay, .min = 0, .max = (uint16_t)max,
                   .source = (RcNpcId)demon->uid, .loc_id = -1};
    cox_add_hazard(raid, &h);
    rc_combat_record_encounter_action(w, demon, false, p->x, p->y, style, delay,
                                      style == COMBAT_RANGED ? "Boulder" : "Ice burst");
    if (style == COMBAT_MAGIC && cox_rand(w, 4) == 0
            && rc_combat_apply_protection(1, COMBAT_MAGIC, p->active_prayers, true))
        rc_player_apply_freeze(w, 8);
}

static void ice_died(RcWorld *w, CoxRaid *raid, CoxRoom *room, RcNpc *npc,
                     int role) {
    IceState *s = COX_ROOM_STATE(IceState, room);
    if (role != 1) return;
    RcGroundGrant g[3] = {{ITEM_STINKHORN, 7, 0}, {ITEM_JUICE, 7, 0},
                          {ITEM_CICELY, 5, 0}};
    drop(w, npc->x, npc->y, npc->plane, g, 3);
    if (s->storm_placed)
        cox_remove_loc(w, LOC_ICE_STORM, s->storm_x, s->storm_y, room->plane);
    for (int i = 0; i < s->brazier_count; i++)
        if (s->fuel[i] > 0) {
            s->fuel[i] = 0;
            cox_replace_loc(w, LOC_BRAZIER_LIT, s->bx[i], s->by[i], room->plane,
                            LOC_BRAZIER);
        }
    cox_complete_room(w, raid, room);
}

const CoxRoomOps cox_ice_demon_ops = {
    .enter = ice_enter, .tick = ice_tick, .npc_died = ice_died,
};

static RcInteractionHandlerResult light_brazier(RcWorld *world, RcPlayer *player,
                                                const RcPendingInteraction *pending,
                                                void *ctx) {
    (void)ctx;
    CoxRoom *room = room_of(world, pending, COX_ROOM_ICE_DEMON);
    if (!room) return say("Nothing interesting happens.");
    if (!cox_has_item(player, ITEM_TINDERBOX))
        return say("You need a tinderbox to light the brazier.");
    if (!cox_has_item(player, ITEM_KINDLING))
        return say("You need some kindling to light the brazier.");
    cox_start_action(world, cox_raid(world), COX_ACT_LIGHT_BRAZIER, pending, 1, 0);
    return rc_interaction_result_complete();
}

static void fuel_brazier(RcWorld *w, CoxRaid *raid, IceState *s, int i, int count) {
    if (!cox_take_item(w, ITEM_KINDLING, count)) return;
    s->fuel[i] = (int16_t)(s->fuel[i] + count * FUEL_PER_KINDLING);
    cox_add_xp(w, SKILL_FIREMAKING, 4800 * count);
    // 36 points per kindling burned (Nov 2023).
    cox_add_points(raid, 36 * count);
}

static RcInteractionHandlerResult fuel_handler(RcWorld *world, RcPlayer *player,
                                               const RcPendingInteraction *pending,
                                               void *ctx) {
    (void)ctx;
    CoxRoom *room = room_of(world, pending, COX_ROOM_ICE_DEMON);
    if (!room) return say("Nothing interesting happens.");
    IceState *s = COX_ROOM_STATE(IceState, room);
    int i = brazier_index(s, pending->target.tile_x, pending->target.tile_y);
    int n = cox_inventory_count(player, ITEM_KINDLING);
    if (i < 0 || n <= 0) return say("You need some kindling to fuel the brazier.");
    fuel_brazier(world, cox_raid(world), s, i, n);
    return say("You add some kindling to the brazier.");
}

static bool light_attempt(RcWorld *w, CoxRaid *raid, CoxAction *a) {
    CoxRoom *room = cox_room_at(raid, a->target_x, a->target_y, a->plane);
    if (!room || room->type != COX_ROOM_ICE_DEMON) return false;
    IceState *s = COX_ROOM_STATE(IceState, room);
    int i = brazier_index(s, a->target_x, a->target_y);
    if (i < 0 || s->fuel[i] > 0 || !cox_has_item(&w->player, ITEM_KINDLING))
        return false;
    a->next_tick = w->tick + 4;
    // Mod Ash: 8% at Firemaking 1 to 78% at 99, retried until it lights.
    int fm = w->player.skills.boosted_level[SKILL_FIREMAKING];
    if (cox_rand(w, 100) >= linear_pct(fm, 8, 78)) return true;
    cox_replace_loc(w, LOC_BRAZIER, s->bx[i], s->by[i], room->plane, LOC_BRAZIER_LIT);
    fuel_brazier(w, raid, s, i, 1);
    cox_add_points(raid, 32);
    cox_message(w, "The brazier catches fire.");
    return false;
}

// ---------------------------------------------------------------------------
// Thieving: feed the corrupted scavenger cavern grubs from the chests.

enum { NPC_SCAVENGER_AWAKE = 7602, NPC_SCAVENGER_ASLEEP = 7603 };
#define THIEVING_CHESTS 80
#define GRUBS_NEEDED_SOLO 30

typedef struct {
    int16_t cx[THIEVING_CHESTS], cy[THIEVING_CHESTS];
    RcTick relock[THIEVING_CHESTS];
    uint8_t chest_count;
    int8_t bat_chest;
    int8_t gas_chest[3];
    bool bats_taken;
    int16_t trough_x, trough_y;
    int16_t trough_grubs;
    int16_t eaten;
    int16_t points_grubs;
    RcTick next_eat, last_fed;
    bool asleep, gate_placed;
    int16_t gate_x, gate_y;
    bool journal_taken;
} ThievingState;
COX_STATE_FITS(ThievingState);

static const int8_t g_scavenger_pos[3][2] = {{7, 14}, {22, 21}, {22, 21}};

static void thieving_enter(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    ThievingState *s = COX_ROOM_STATE(ThievingState, room);
    if (room->flags & COX_ROOM_COMPLETE) return;
    if (!(room->flags & COX_ROOM_SPAWNED)) {
        room->flags |= COX_ROOM_SPAWNED;
        int xs[THIEVING_CHESTS], ys[THIEVING_CHESTS];
        int n = cox_find_loc(w, room, LOC_CHEST, xs, ys, NULL, THIEVING_CHESTS);
        s->chest_count = (uint8_t)(n > THIEVING_CHESTS ? THIEVING_CHESTS : n);
        for (int i = 0; i < s->chest_count; i++) {
            s->cx[i] = (int16_t)xs[i];
            s->cy[i] = (int16_t)ys[i];
        }
        // One chest holds psykk bats; three others release gas.
        int8_t picks[4];
        int picked = 0;
        while (s->chest_count >= 4 && picked < 4) {
            int8_t c = (int8_t)cox_rand(w, s->chest_count);
            bool seen = false;
            for (int k = 0; k < picked; k++) seen |= picks[k] == c;
            if (!seen) picks[picked++] = c;
        }
        s->bat_chest = picked == 4 ? picks[0] : -1;
        for (int k = 0; k < 3; k++) s->gas_chest[k] = picked == 4 ? picks[k + 1] : -1;
        int tx, ty;
        if (cox_find_loc(w, room, LOC_TROUGH, &tx, &ty, NULL, 1) == 1) {
            s->trough_x = (int16_t)tx;
            s->trough_y = (int16_t)ty;
        }
        int x, y;
        if (free_tile(w, room, g_scavenger_pos[room->variant][0],
                      g_scavenger_pos[room->variant][1], 2, &x, &y)) {
            RcNpc *scav = cox_spawn(w, raid, room, NPC_SCAVENGER_AWAKE, x, y, 1, NULL,
                                    true);
            if (scav) scav->player_untargetable = true;
        }
    }
    // The hungry scavenger blocks the way on.
    if (!s->gate_placed && !s->asleep) {
        int x, y;
        if (exit_gate_tile(w, room, 1, &x, &y)) {
            s->gate_placed = true;
            s->gate_x = (int16_t)x;
            s->gate_y = (int16_t)y;
            gate_pair(w, room, LOC_INVISIBLE_BLOCK, x, y, true);
        }
    }
}

static int chest_index(const ThievingState *s, int x, int y) {
    for (int i = 0; i < s->chest_count; i++)
        if (s->cx[i] == x && s->cy[i] == y) return i;
    return -1;
}

static void thieving_tick(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    ThievingState *s = COX_ROOM_STATE(ThievingState, room);
    RcTick now = w->tick;
    // Opened chests lock again 11-15 ticks later.
    for (int i = 0; i < s->chest_count; i++) {
        if (!s->relock[i] || now < s->relock[i]) continue;
        s->relock[i] = 0;
        RcObjectState st;
        if (rc_world_object_active_state(w, LOC_CHEST, s->cx[i], s->cy[i],
                                         room->plane, &st) > 0)
            rc_world_object_revert(w, st.placement_key);
    }
    if (s->asleep || (room->flags & COX_ROOM_COMPLETE)) return;
    // The scavenger eats a grub from the trough every 3 ticks.
    if (s->trough_grubs > 0 && now >= s->next_eat) {
        s->trough_grubs--;
        s->eaten++;
        s->last_fed = now;
        s->next_eat = now + 3;
        if (s->trough_grubs == 0)
            cox_replace_loc(w, LOC_TROUGH_FULL, s->trough_x, s->trough_y,
                            room->plane, LOC_TROUGH);
    }
    // Hunger rises again after 60 seconds without food.
    if (s->eaten > 0 && s->trough_grubs == 0 && now > s->last_fed + 100
            && (now - s->last_fed) % 10 == 0)
        s->eaten--;
    if (s->eaten >= GRUBS_NEEDED_SOLO) {
        s->asleep = true;
        RcNpc *scav = cox_room_npc(w, room, 1);
        if (scav) {
            int idx = rc_npc_def_find(NPC_SCAVENGER_ASLEEP);
            if (idx >= 0) rc_npc_set_form(scav, idx);
            RcGroundGrant g[3] = {{ITEM_STINKHORN, 5, 0}, {ITEM_JUICE, 5, 0},
                                  {ITEM_CICELY, 3, 0}};
            drop(w, scav->x, scav->y, scav->plane, g, 3);
        }
        if (s->gate_placed) gate_pair(w, room, LOC_INVISIBLE_BLOCK, s->gate_x,
                                      s->gate_y, false);
        cox_message(w, "The corrupted scavenger is full and lies down to sleep.");
        cox_complete_room(w, raid, room);
    }
}

const CoxRoomOps cox_thieving_ops = {.enter = thieving_enter, .tick = thieving_tick};

static RcInteractionHandlerResult open_chest(RcWorld *world, RcPlayer *player,
                                             const RcPendingInteraction *pending,
                                             void *ctx) {
    (void)ctx;
    CoxRoom *room = room_of(world, pending, COX_ROOM_THIEVING);
    if (!room) return say("Nothing interesting happens.");
    if (cox_inventory_count(player, ITEM_GRUBS) >= 28)
        return say("You can't carry any more grubs.");
    cox_start_action(world, cox_raid(world), COX_ACT_OPEN_CHEST, pending, 1, 0);
    return rc_interaction_result_complete();
}

static bool chest_attempt(RcWorld *w, CoxRaid *raid, CoxAction *a) {
    CoxRoom *room = cox_room_at(raid, a->target_x, a->target_y, a->plane);
    if (!room || room->type != COX_ROOM_THIEVING) return false;
    ThievingState *s = COX_ROOM_STATE(ThievingState, room);
    int i = chest_index(s, a->target_x, a->target_y);
    if (i < 0 || s->relock[i]) return false;
    RcPlayer *p = &w->player;
    a->next_tick = w->tick + 1;
    // One attempt a tick: 39.2% at level 1 to 60.7% at 99, +21% with a
    // lockpick or hair clip (Nov 2023).
    int lv = p->skills.boosted_level[SKILL_THIEVING];
    int pct = linear_pct(lv, 392, 607);
    if (cox_has_item(p, ITEM_LOCKPICK) || cox_has_item(p, ITEM_HAIR_CLIP)) pct += 215;
    if (cox_rand(w, 1000) >= pct) return true;
    s->relock[i] = w->tick + 11 + (RcTick)cox_rand(w, 5);
    for (int k = 0; k < 3; k++) {
        if (s->gas_chest[k] != i) continue;
        cox_replace_loc(w, LOC_CHEST, s->cx[i], s->cy[i], room->plane, LOC_CHEST_OPEN);
        rc_combat_damage_player(w, 1 + cox_rand(w, 3), 0, COMBAT_NONE,
                                RC_HIT_SOURCE_STATUS);
        cox_message(w, "A cloud of gas bursts out of the chest!");
        return false;
    }
    if (i == s->bat_chest && !s->bats_taken) {
        s->bats_taken = true;
        cox_replace_loc(w, LOC_CHEST, s->cx[i], s->cy[i], room->plane, LOC_CHEST_OPEN);
        // One per raider, at least 5.
        for (int k = 0; k < 5; k++) cox_give_item(w, ITEM_PSYKK_BAT, 1);
        cox_message(w, "You find some bats in the chest.");
        return false;
    }
    // Grubs: 0..floor(level / 25), at least 1 (2 from level 95).
    int got = cox_rand(w, lv / 25 + 1);
    int least = lv >= 95 ? 2 : 1;
    if (got < least) got = least;
    int room_for = 28 - cox_inventory_count(p, ITEM_GRUBS);
    if (got > room_for) got = room_for;
    cox_replace_loc(w, LOC_CHEST, s->cx[i], s->cy[i], room->plane, LOC_CHEST_HATCHED);
    cox_give_item(w, ITEM_GRUBS, got);
    cox_add_xp(w, SKILL_THIEVING, 600 * got);
    cox_message(w, "You find some cavern grubs.");
    return false;
}

static RcInteractionHandlerResult deposit(RcWorld *world, RcPlayer *player,
                                          const RcPendingInteraction *pending,
                                          void *ctx) {
    (void)ctx;
    CoxRoom *room = room_of(world, pending, COX_ROOM_THIEVING);
    if (!room) return say("Nothing interesting happens.");
    ThievingState *s = COX_ROOM_STATE(ThievingState, room);
    int n = cox_inventory_count(player, ITEM_GRUBS);
    if (n <= 0) return say("You don't have any grubs to deposit.");
    if (!cox_take_item(world, ITEM_GRUBS, n)) return say("Nothing interesting happens.");
    if (s->trough_grubs == 0)
        cox_replace_loc(world, LOC_TROUGH, s->trough_x, s->trough_y, room->plane,
                        LOC_TROUGH_FULL);
    s->trough_grubs = (int16_t)(s->trough_grubs + n);
    // 115 points a grub, up to max(4500, floor(total Thieving / 6) * 150).
    int cap_points = (player->skills.base_level[SKILL_THIEVING] / 6) * 150;
    if (cap_points < 4500) cap_points = 4500;
    int cap_grubs = (cap_points + 114) / 115;
    int paid = n;
    if (s->points_grubs + paid > cap_grubs) paid = cap_grubs - s->points_grubs;
    if (paid > 0) {
        s->points_grubs = (int16_t)(s->points_grubs + paid);
        cox_add_points(cox_raid(world), 115 * paid);
    }
    return say("You put the grubs in the trough.");
}

static RcInteractionHandlerResult search_keeper(RcWorld *world, RcPlayer *player,
                                                const RcPendingInteraction *pending,
                                                void *ctx) {
    (void)player;
    (void)ctx;
    CoxRoom *room = room_of(world, pending, COX_ROOM_THIEVING);
    if (!room) return say("Nothing interesting happens.");
    ThievingState *s = COX_ROOM_STATE(ThievingState, room);
    if (s->journal_taken || !cox_give_item(world, ITEM_JOURNAL, 1))
        return say("You find nothing of interest.");
    s->journal_taken = true;
    return say("You find a journal on the creature keeper.");
}

// ---------------------------------------------------------------------------
// Tightrope: the deathly guards stand on their platforms beside the rope and
// fight a raider who attacks them or walks the rope. Cross under fire, take
// the keystone on the far side, cross back and dispel the barrier in front of
// the exit. Tiles, timings and attacks follow the RSProx recordings (RT rows;
// README Sources).

enum { NPC_DEATHLY_RANGER = 7559, NPC_DEATHLY_MAGE = 7560 };
#define GUARDS 4
#define GUARD_ATTACK_TICKS 4
#define GUARD_RANGE 10
#define CROSS_FACING_TICKS 2

typedef struct {
    int16_t rope_x[2], rope_y[2];
    bool barrier_placed, dispelled;
    int16_t barrier_x, barrier_y;
    // A guard fights once it is hit or a raider walks the rope.
    bool engaged[GUARDS];
    RcTick next_guard_shot[GUARDS];
    // The crossing in progress: the end it started from, and its direction.
    int8_t cross_from, cross_dx, cross_dy;
} RopeState;
COX_STATE_FITS(RopeState);

static const CoxStats g_deathly[2] = {
    {.hp = 120, .att = 1, .str = 1, .def = 155, .mag = 155, .rng = 210,
     .attack_bonus = {0, 0, 0, 120, 0}, .max_hit = 34,
     .attack_speed = 4, .style = COMBAT_RANGED, .flags = COX_STATS_MAGIC_DEFENSIVE},
    {.hp = 120, .att = 1, .str = 1, .def = 155, .mag = 210, .rng = 1,
     .attack_bonus = {0, 0, 0, 0, 120}, .max_hit = 16,
     .attack_speed = 4, .style = COMBAT_MAGIC},
};
// Template tiles per exit variant (west, north, east): the two rangers share
// one platform beside the rope and the two mages the other (the solo count;
// NR spawn tiles, RT-S1), and the barrier's south-west tile and rotation one
// tile inside the exit passage.
static const int8_t g_guard_post[3][GUARDS][2] = {
    {{20, 26}, {22, 26}, {21, 19}, {23, 19}},
    {{20, 26}, {22, 26}, {21, 19}, {23, 19}},
    {{5, 20}, {5, 22}, {12, 21}, {12, 23}},
};
static const int8_t g_barrier_pos[3][3] = {{6, 16, 0}, {13, 24, 1}, {27, 15, 2}};

static int sign_of(int v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

static int rope_length(const RopeState *s) {
    int dx = abs(s->rope_x[1] - s->rope_x[0]), dy = abs(s->rope_y[1] - s->rope_y[0]);
    return dx > dy ? dx : dy;
}

static void rope_enter(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    RopeState *s = COX_ROOM_STATE(RopeState, room);
    if (room->flags & COX_ROOM_COMPLETE) return;
    // A raider coming back finds the guards idle again.
    memset(s->engaged, 0, sizeof(s->engaged));
    if (!(room->flags & COX_ROOM_SPAWNED)) {
        room->flags |= COX_ROOM_SPAWNED;
        int xs[2], ys[2];
        if (cox_find_loc(w, room, LOC_TIGHTROPE, xs, ys, NULL, 2) == 2) {
            for (int i = 0; i < 2; i++) {
                s->rope_x[i] = (int16_t)xs[i];
                s->rope_y[i] = (int16_t)ys[i];
            }
        }
        for (int g = 0; g < GUARDS; g++) {
            const int8_t *post = g_guard_post[room->variant][g];
            cox_spawn(w, raid, room, g < 2 ? NPC_DEATHLY_RANGER : NPC_DEATHLY_MAGE,
                      room->x + post[0], room->y + post[1], g + 1,
                      &g_deathly[g < 2 ? 0 : 1], true);
        }
    }
    if (!s->barrier_placed && !s->dispelled) {
        const int8_t *pos = g_barrier_pos[room->variant];
        s->barrier_placed = true;
        s->barrier_x = (int16_t)(room->x + pos[0]);
        s->barrier_y = (int16_t)(room->y + pos[1]);
        cox_add_loc(w, LOC_BARRIER, 10, pos[2], s->barrier_x, s->barrier_y,
                    room->plane, NULL);
    }
}

// One guard's attack: an arrow or a fire blast whose hit lands with the
// projectile, 46 + 5 (arrow) or 10 (fire blast) client cycles per tile of the
// gap after the swing (RT-A1..A3).
static void guard_attack(RcWorld *w, RcNpc *guard, bool ranger, int gap) {
    RcNpcScriptedAttack shot = {
        .style = ranger ? COMBAT_RANGED : COMBAT_MAGIC,
        .max_hit = rc_npc_max_hit(guard, rc_npc_def_for_npc(w, guard)),
        .delay = (46 + (ranger ? 5 : 10) * gap) / 30,
        .protected_pct = 50, .key = ranger ? "Ranged" : "Magic"};
    rc_combat_npc_scripted_attack(w, guard, &shot);
}

static void rope_tick(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    (void)raid;
    RopeState *s = COX_ROOM_STATE(RopeState, room);
    if (room->flags & COX_ROOM_COMPLETE) return;
    // An engaged guard attacks every 4 ticks while the raider is in reach
    // (RT-A5), on the rope or off it.
    RcTick now = w->tick;
    for (int g = 0; g < GUARDS; g++) {
        RcNpc *guard = cox_room_npc(w, room, g + 1);
        if (!guard || !s->engaged[g] || now < s->next_guard_shot[g]) continue;
        int gap = cox_player_distance(w, guard);
        if (w->player.is_dead || gap > GUARD_RANGE) continue;
        guard_attack(w, guard, g < 2, gap);
        s->next_guard_shot[g] = now + GUARD_ATTACK_TICKS;
    }
}

// A guard that is hit turns on the raider and attacks 2 ticks later (RT-W1).
static void rope_npc_hit(RcWorld *w, CoxRaid *raid, CoxRoom *room, RcNpc *npc,
                         int role, int damage) {
    (void)raid;
    (void)npc;
    (void)damage;
    RopeState *s = COX_ROOM_STATE(RopeState, room);
    int g = role - 1;
    if (g < 0 || g >= GUARDS || s->engaged[g]) return;
    s->engaged[g] = true;
    s->next_guard_shot[g] = w->tick + 2;
}

const CoxRoomOps cox_tightrope_ops = {
    .enter = rope_enter, .tick = rope_tick, .npc_hit = rope_npc_hit};

// Cross: the raider faces along the rope for two ticks, then walks it to the
// tile past the far end, two tiles on the first tick and one a tick after,
// unable to act until off it (RT-X1).
static RcInteractionHandlerResult cross_rope(RcWorld *world, RcPlayer *player,
                                             const RcPendingInteraction *pending,
                                             void *ctx) {
    (void)ctx;
    CoxRoom *room = room_of(world, pending, COX_ROOM_TIGHTROPE);
    if (!room) return say("Nothing interesting happens.");
    RopeState *s = COX_ROOM_STATE(RopeState, room);
    int from = s->rope_x[0] == pending->target.tile_x
            && s->rope_y[0] == pending->target.tile_y ? 0 : 1;
    int to = 1 - from;
    s->cross_from = (int8_t)from;
    s->cross_dx = (int8_t)sign_of(s->rope_x[to] - s->rope_x[from]);
    s->cross_dy = (int8_t)sign_of(s->rope_y[to] - s->rope_y[from]);
    player->facing_entity = -1;
    player->facing_x = s->rope_x[to];
    player->facing_y = s->rope_y[to];
    player->facing_width = 1;
    player->facing_length = 1;
    rc_world_delay_player(world, CROSS_FACING_TICKS + rope_length(s) + 1);
    cox_start_action(world, cox_raid(world), COX_ACT_CROSS_ROPE, pending,
                     CROSS_FACING_TICKS, 0);
    cox_add_xp(world, SKILL_AGILITY, 2000);
    return rc_interaction_result_complete();
}

// One tick of the walk: step k puts the raider k + 1 tiles past the near end,
// and the last lands one past the far end. Idle guards open on the raider the
// tick after they step on (RT-X3).
static bool rope_step(RcWorld *w, CoxRaid *raid, CoxAction *a) {
    CoxRoom *room = cox_room_at(raid, a->target_x, a->target_y, a->plane);
    if (!room) return false;
    RopeState *s = COX_ROOM_STATE(RopeState, room);
    int k = a->param;
    int x = s->rope_x[s->cross_from] + s->cross_dx * (k + 1);
    int y = s->rope_y[s->cross_from] + s->cross_dy * (k + 1);
    if (!rc_world_force_move_player(w, x, y)) return false;
    a->player_x = x;
    a->player_y = y;
    if (k == 0) {
        for (int g = 0; g < GUARDS; g++) {
            if (s->engaged[g]) continue;
            s->engaged[g] = true;
            s->next_guard_shot[g] = w->tick + 1;
        }
    }
    if (k >= rope_length(s)) return false;
    a->param = k + 1;
    a->next_tick = w->tick + 1;
    return true;
}

static RcInteractionHandlerResult take_keystone(RcWorld *world, RcPlayer *player,
                                                const RcPendingInteraction *pending,
                                                void *ctx) {
    (void)ctx;
    if (!room_of(world, pending, COX_ROOM_TIGHTROPE))
        return say("Nothing interesting happens.");
    if (cox_has_item(player, ITEM_KEYSTONE)) return say("You already have a keystone.");
    if (!cox_give_item(world, ITEM_KEYSTONE, 1))
        return say("You don't have enough inventory space.");
    return rc_interaction_result_complete();
}

// Dispel, or the keystone used on the barrier: the keystone goes, the barrier
// disperses and every surviving guard dies with it (RT-K2, RT-K3).
static RcInteractionHandlerResult dispel(RcWorld *world, RcPlayer *player,
                                         const RcPendingInteraction *pending,
                                         void *ctx) {
    (void)ctx;
    CoxRoom *room = room_of(world, pending, COX_ROOM_TIGHTROPE);
    if (!room) return say("Nothing interesting happens.");
    RopeState *s = COX_ROOM_STATE(RopeState, room);
    if (!cox_take_item(world, ITEM_KEYSTONE, 1))
        return say("The barrier needs a keystone crystal to dispel it.");
    s->dispelled = true;
    cox_remove_loc(world, LOC_BARRIER, s->barrier_x, s->barrier_y, room->plane);
    for (int g = 0; g < GUARDS; g++) {
        RcNpc *guard = cox_room_npc(world, room, g + 1);
        if (guard) rc_npc_remove(world, guard->uid);
    }
    CoxRaid *raid = cox_raid(world);
    cox_add_points(raid, 2000);
    cox_complete_room(world, raid, room);
    (void)player;
    return say("Your keystone glows as it is absorbed into the barrier, which disperses.");
}

// ---------------------------------------------------------------------------
// Jewelled crabs: steer the orb into each crystal in its colour.

enum { NPC_CRAB_DIM = 7576, NPC_CRAB_RED = 7577, NPC_CRAB_GREEN = 7578,
       NPC_CRAB_BLUE = 7579, NPC_ORB_WHITE = 7580 };   // orbs white, red, green, blue
enum { COLOUR_WHITE = 0, COLOUR_RED = 1, COLOUR_GREEN = 2, COLOUR_BLUE = 3,
       COLOUR_NONE = 4 };
enum { ROLE_ORB = 1, ROLE_CRAB = 2 };    // crabs 2..5
#define CRABS 3

typedef struct {
    int16_t relief_x, relief_y;
    int16_t crystal_x[4], crystal_y[4];
    bool lit[4];
    uint8_t lit_count;
    int8_t orb_dir, orb_colour;
    RcTick next_orb, orb_born;
    uint8_t crab_colour[CRABS];
    RcTick crab_colour_until[CRABS], crab_rooted_until[CRABS], crab_next_swing[CRABS];
    RcTick crab_next_step[CRABS];
    bool gate_placed, open;
    int16_t gate_x, gate_y;
    RcTick open_tick;
} CrabState;
COX_STATE_FITS(CrabState);

static const CoxStats g_crab = {
    .hp = 100, .att = 150, .str = 150, .def = 1, .mag = 1, .rng = 1,
    .max_hit = 12, .attack_speed = 4, .style = COMBAT_MELEE_CRUSH,
};
// Crystal colours: black takes a white orb, cyan red, magenta green,
// yellow blue.
static const int g_crystal_locs[4] = {LOC_CRYSTAL_BLACK, LOC_CRYSTAL_CYAN,
                                      LOC_CRYSTAL_MAGENTA, LOC_CRYSTAL_YELLOW};
static const int g_crystal_needs[4] = {COLOUR_WHITE, COLOUR_RED, COLOUR_GREEN,
                                       COLOUR_BLUE};
static const int g_dir[4][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};   // N E S W

static void crabs_enter(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    CrabState *s = COX_ROOM_STATE(CrabState, room);
    if (room->flags & COX_ROOM_COMPLETE) return;
    if (!(room->flags & COX_ROOM_SPAWNED)) {
        room->flags |= COX_ROOM_SPAWNED;
        int x, y;
        if (cox_find_loc(w, room, LOC_RELIEF, &x, &y, NULL, 1) == 1) {
            s->relief_x = (int16_t)x;
            s->relief_y = (int16_t)y;
        }
        for (int i = 0; i < 4; i++)
            if (cox_find_loc(w, room, g_crystal_locs[i], &x, &y, NULL, 1) == 1) {
                s->crystal_x[i] = (int16_t)x;
                s->crystal_y[i] = (int16_t)y;
            }
        // The first three of the variant's four crab slots (solo; NR's tiles).
        static const int8_t at[3][CRABS][2] = {
            {{22, 10}, {16, 14}, {17, 21}},
            {{10, 14}, {15, 9}, {17, 17}},
            {{9, 12}, {7, 18}, {13, 22}},
        };
        for (int c = 0; c < CRABS; c++)
            if (free_tile(w, room, at[room->variant][c][0], at[room->variant][c][1],
                          1, &x, &y))
                cox_spawn(w, raid, room, NPC_CRAB_DIM, x, y, ROLE_CRAB + c, &g_crab,
                          true);
        s->next_orb = w->tick + 5;
        s->orb_colour = COLOUR_NONE;
    }
    if (!s->gate_placed && !s->open) {
        int gx, gy;
        if (exit_gate_tile(w, room, 2, &gx, &gy)) {
            s->gate_placed = true;
            s->gate_x = (int16_t)gx;
            s->gate_y = (int16_t)gy;
            cox_add_loc(w, LOC_BIG_CRYSTAL, 10, 0, gx, gy, room->plane, NULL);
        }
    }
}

static void crab_set_colour(RcWorld *w, CrabState *s, CoxRoom *room, int c,
                            int colour, int ticks) {
    RcNpc *crab = cox_room_npc(w, room, ROLE_CRAB + c);
    if (!crab) return;
    static const int ids[5] = {NPC_CRAB_DIM, NPC_CRAB_RED, NPC_CRAB_GREEN,
                               NPC_CRAB_BLUE, NPC_CRAB_DIM};
    s->crab_colour[c] = (uint8_t)colour;
    s->crab_colour_until[c] = colour == COLOUR_NONE ? 0 : w->tick + (RcTick)ticks;
    int idx = rc_npc_def_find(ids[colour == COLOUR_NONE ? 0 : colour]);
    if (idx >= 0) rc_npc_set_form(crab, idx);
}

static void orb_end(RcWorld *w, CrabState *s, CoxRoom *room, RcNpc *orb) {
    if (orb) rc_npc_remove(w, orb->uid);
    s->orb_colour = COLOUR_NONE;
    // The next orb leaves the carving as soon as the last one ends.
    s->next_orb = w->tick + 1;
    (void)room;
}

static void light_crystal(RcWorld *w, CoxRaid *raid, CrabState *s, CoxRoom *room,
                          int i) {
    s->lit[i] = true;
    cox_replace_loc(w, g_crystal_locs[i], s->crystal_x[i], s->crystal_y[i],
                    room->plane, LOC_CRYSTAL_WHITE);
    s->lit_count++;
    cox_add_points(raid, 500);
    if (s->gate_placed)
        cox_replace_loc(w, LOC_BIG_CRYSTAL + s->lit_count - 1, s->gate_x, s->gate_y,
                        room->plane, LOC_BIG_CRYSTAL + s->lit_count);
    if (s->lit_count == 4) {
        // The crabs vanish and the big crystal shatters.
        for (int c = 0; c < CRABS; c++) {
            RcNpc *crab = cox_room_npc(w, room, ROLE_CRAB + c);
            if (crab) rc_npc_remove(w, crab->uid);
        }
        s->open_tick = w->tick + 7;
    }
}

static void move_orb(RcWorld *w, CoxRaid *raid, CrabState *s, CoxRoom *room,
                     RcNpc *orb) {
    RcPlayer *p = &w->player;
    int dx = g_dir[s->orb_dir][0], dy = g_dir[s->orb_dir][1];
    int nx = orb->x + dx, ny = orb->y + dy;
    // A crystal in its path: the right colour lights it.
    for (int i = 0; i < 4; i++) {
        if (s->lit[i] || s->crystal_x[i] != nx || s->crystal_y[i] != ny) continue;
        if (s->orb_colour == g_crystal_needs[i]) light_crystal(w, raid, s, room, i);
        orb_end(w, s, room, orb);
        return;
    }
    // A crab turns it right; a white orb takes the crab's colour, a coloured
    // one meeting another colour fizzles out.
    for (int c = 0; c < CRABS; c++) {
        RcNpc *crab = cox_room_npc(w, room, ROLE_CRAB + c);
        if (!crab || crab->x != nx || crab->y != ny) continue;
        int colour = s->crab_colour[c];
        if (colour != COLOUR_NONE && s->orb_colour != COLOUR_WHITE
                && s->orb_colour != colour) {
            orb_end(w, s, room, orb);
            return;
        }
        if (s->orb_colour == COLOUR_WHITE && colour != COLOUR_NONE) {
            s->orb_colour = (int8_t)colour;
            int idx = rc_npc_def_find(NPC_ORB_WHITE + colour);
            if (idx >= 0) rc_npc_set_form(orb, idx);
        }
        s->orb_dir = (int8_t)((s->orb_dir + 1) % 4);
        return;
    }
    if (p->x == nx && p->y == ny && p->plane == room->plane) {
        // Status damage: the spent orb is removed this tick, which would
        // cancel a hit it was credited with.
        rc_combat_damage_player(w, 22, 0, COMBAT_NONE, RC_HIT_SOURCE_STATUS);
        orb_end(w, s, room, orb);
        return;
    }
    if (!rc_can_move(&w->map, orb->x, orb->y, dx, dy, room->plane)) {
        orb_end(w, s, room, orb);
        return;
    }
    orb->prev_x = orb->x;
    orb->prev_y = orb->y;
    orb->x = nx;
    orb->y = ny;
}

static void crabs_tick(RcWorld *w, CoxRaid *raid, CoxRoom *room) {
    CrabState *s = COX_ROOM_STATE(CrabState, room);
    RcTick now = w->tick;
    if (s->open_tick && now >= s->open_tick && !s->open) {
        s->open = true;
        if (s->gate_placed)
            cox_remove_loc(w, LOC_BIG_CRYSTAL + 4, s->gate_x, s->gate_y, room->plane);
        RcNpc *orb = cox_room_npc(w, room, ROLE_ORB);
        if (orb) rc_npc_remove(w, orb->uid);
        cox_complete_room(w, raid, room);
    }
    if (room->flags & COX_ROOM_COMPLETE || s->lit_count == 4) return;
    RcNpc *orb = cox_room_npc(w, room, ROLE_ORB);
    if (!orb && now >= s->next_orb) {
        // Out of the carving onto the open floor beside it (the side nearest
        // the room's middle if more than one is open).
        int x = s->relief_x, y = s->relief_y;
        int cx = room->x + 16, cy = room->y + 16;
        int dir = -1, best = 0;
        for (int d = 0; d < 4; d++) {
            int nx = x + g_dir[d][0], ny = y + g_dir[d][1];
            int dist = abs(cx - nx) + abs(cy - ny);
            if (cox_tile_walkable(w, nx, ny, room->plane) && (dir < 0 || dist < best)) {
                dir = d;
                best = dist;
            }
        }
        orb = dir < 0 ? NULL : cox_spawn(w, raid, room, NPC_ORB_WHITE,
                                          x + g_dir[dir][0], y + g_dir[dir][1],
                                          ROLE_ORB, NULL, true);
        if (orb) {
            orb->player_untargetable = true;
            s->orb_dir = (int8_t)dir;
            s->orb_colour = COLOUR_WHITE;
            s->orb_born = now;
        }
    } else if (orb && now > s->orb_born) {
        move_orb(w, raid, s, room, orb);
    }
    for (int c = 0; c < CRABS; c++) {
        RcNpc *crab = cox_room_npc(w, room, ROLE_CRAB + c);
        if (!crab) continue;
        crab->current_hp = crab->spawn_hp;   // crabs cannot be hurt
        if (s->crab_colour[c] != COLOUR_NONE && now >= s->crab_colour_until[c])
            crab_set_colour(w, s, room, c, COLOUR_NONE, 0);
        if (now < s->crab_rooted_until[c]) {
            if (now >= s->crab_next_swing[c] && cox_player_distance(w, crab) <= 1) {
                RcNpcScriptedAttack a = {.style = COMBAT_MELEE_CRUSH,
                    .max_hit = rc_npc_max_hit(crab, rc_npc_def_for_npc(w, crab)),
                    .protected_pct = 0, .key = "Melee"};
                rc_combat_npc_scripted_attack(w, crab, &a);
                s->crab_next_swing[c] = now + 4;
            }
            continue;
        }
        // Short aggression range: a crab follows a raider within 3 tiles.
        if (cox_player_in_room(w, room) && cox_player_distance(w, crab) <= 3) {
            if (cox_player_distance(w, crab) <= 1) {
                if (now >= s->crab_next_swing[c]) {
                    RcNpcScriptedAttack a = {.style = COMBAT_MELEE_CRUSH,
                        .max_hit = rc_npc_max_hit(crab, rc_npc_def_for_npc(w, crab)),
                        .protected_pct = 0, .key = "Melee"};
                    rc_combat_npc_scripted_attack(w, crab, &a);
                    s->crab_next_swing[c] = now + 4;
                }
            } else {
                RcRouteTarget t = rc_route_target_rectangle(w->player.x, w->player.y,
                                                            1, 1, 1, 1, false, false);
                rc_npc_route_request(w, crab, &t, RC_NPC_ROUTE_CHASE, false);
            }
        }
    }
}

static void crabs_hit(RcWorld *w, CoxRaid *raid, CoxRoom *room, RcNpc *npc,
                      int role, int damage) {
    (void)raid;
    (void)damage;
    (void)npc;
    CrabState *s = COX_ROOM_STATE(CrabState, room);
    int c = role - ROLE_CRAB;
    if (c < 0 || c >= CRABS) return;
    // The attack's style colours the crab: melee red, ranged green, magic
    // blue.
    RcCombatStyle style = w->player.combat_style;
    if (w->player.manual_spell_cast >= 0 || w->player.autocast_spell >= 0)
        style = COMBAT_MAGIC;
    int colour = style == COMBAT_RANGED ? COLOUR_GREEN
               : style == COMBAT_MAGIC ? COLOUR_BLUE : COLOUR_RED;
    crab_set_colour(w, s, room, c, colour, 5 + cox_rand(w, 10));
}

const CoxRoomOps cox_crabs_ops = {
    .enter = crabs_enter, .tick = crabs_tick, .npc_hit = crabs_hit,
};

static RcInteractionHandlerResult smash_crab(RcWorld *world, RcPlayer *player,
                                             const RcPendingInteraction *pending,
                                             void *ctx) {
    (void)ctx;
    CoxRoom *room = room_of(world, pending, COX_ROOM_CRABS);
    if (!room) return say("Nothing interesting happens.");
    static const int blunt[] = {ITEM_HAMMER, 13576, 21003, 4153, 24225};
    if (!cox_has_any(player, blunt, (int)(sizeof(blunt) / sizeof(blunt[0]))))
        return say("You need a hammer to smash the crab.");
    CrabState *s = COX_ROOM_STATE(CrabState, room);
    RcNpc *crab = rc_npc_resolve(world, (RcNpcId)pending->target.entity_uid);
    int c = crab ? cox_room_npc_role(room, crab->uid) - ROLE_CRAB : -1;
    if (c < 0 || c >= CRABS) return say("Nothing interesting happens.");
    // Smashing roots the crab (50-60 ticks solo) and turns it red.
    s->crab_rooted_until[c] = world->tick + 50 + (RcTick)cox_rand(world, 11);
    rc_npc_route_clear(crab, RC_MOVEMENT_ARRIVED);
    crab_set_colour(world, s, room, c, COLOUR_RED, 50);
    return rc_interaction_result_complete();
}

// ---------------------------------------------------------------------------

bool cox_puzzle_action(RcWorld *world, CoxRaid *raid, CoxAction *a) {
    switch (a->kind) {
    case COX_ACT_LIGHT_BRAZIER: return light_attempt(world, raid, a);
    case COX_ACT_OPEN_CHEST: return chest_attempt(world, raid, a);
    case COX_ACT_CROSS_ROPE: return rope_step(world, raid, a);
    default: return false;
    }
}

int cox_puzzle_rooms_register(RcWorld *world) {
    int ok = 1;
    ok &= cox_register_object(world, LOC_BRAZIER, 0, light_brazier);
    ok &= cox_register_object(world, LOC_BRAZIER_LIT, 0, fuel_handler);
    ok &= cox_register_item_on_object(world, ITEM_KINDLING, LOC_BRAZIER_LIT, fuel_handler);
    ok &= cox_register_object(world, LOC_CHEST, 0, open_chest);
    ok &= cox_register_object(world, LOC_TROUGH, 0, deposit);
    ok &= cox_register_object(world, LOC_TROUGH_FULL, 0, deposit);
    ok &= cox_register_object(world, LOC_KEEPER, 4, search_keeper);   // Search
    ok &= cox_register_object(world, LOC_TIGHTROPE, 0, cross_rope);
    ok &= cox_register_object(world, LOC_KEYSTONE, 0, take_keystone);
    ok &= cox_register_object(world, LOC_BARRIER, 0, dispel);
    ok &= cox_register_item_on_object(world, ITEM_KEYSTONE, LOC_BARRIER, dispel);
    // Crabs: Attack (second option) fights them; Smash is the third.
    for (int c = 0; c < 4; c++)
        ok &= cox_register_npc(world, NPC_CRAB_DIM + c, 2, smash_crab);
    return ok;
}
