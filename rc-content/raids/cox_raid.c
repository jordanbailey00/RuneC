#include "cox.h"

#include "../content.h"
#include "../../rc-core/api.h"
#include "../../rc-core/collision.h"
#include "../../rc-core/combat.h"
#include "../../rc-core/combat_hit.h"
#include "../../rc-core/consumables.h"
#include "../../rc-core/coordinates.h"
#include "../../rc-core/dialogue.h"
#include "../../rc-core/drops.h"
#include "../../rc-core/events.h"
#include "../../rc-core/interaction.h"
#include "../../rc-core/items.h"
#include "../../rc-core/objects.h"
#include "../../rc-core/object_runtime.h"
#include "../../rc-core/pathfinding.h"
#include "../../rc-core/rng.h"
#include "../../rc-core/skills.h"
#include "../../rc-core/varbits.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Raid controller: entry and difficulty on Mount Quidamortem, the layout,
// the way down to Great Olm, timer and points, death and leaving. Room
// behaviour lives in the room modules (cox_combat_rooms.c,
// cox_puzzle_rooms.c, cox_resources.c, cox_olm.c).

enum {
    LOC_ENTRANCE = 29777,           // Mount Quidamortem "Chambers of Xeric"
    LOC_EXIT_STEPS = 29778,         // lobby (non-leader) and Olm chamber
    LOC_LOBBY_STEPS_LEADER = 50000, // lobby steps for the leader (one layout: no Reload)
    LOC_PASSAGE = 29789,
    LOC_HOLE_OLM = 29735,           // raids_bossentrance
    LOC_ROPE_OLM = 29996,           // raids_bossexit
    LOC_ENERGY_WELL = 30066,
    LOC_BANK_CHEST = 47420,
    LOC_BANK_CHEST_CLOSED = 47419,
    NPC_RIMOR = 7595,
};

// The raid's client variables. The points overlay reads the scores and the
// timer; progress 5 ("Get out!") stops the timer.
enum {
    VARBIT_PARTYSIZE = 5424,
    VARBIT_ISLEADER = 5423,
    VARBIT_PROGRESS = 5425,
    VARBIT_PARTYSCORE = 5431,
    VARBIT_INDUNGEON = 5432,
    VARBIT_TIMER = 6386,            // ticks, 16 bits
    VARP_PLAYERSCORE = 4609,
    PROGRESS_STARTED = 1,
    PROGRESS_OLM = 2,
    PROGRESS_DONE = 5,
};

// Mount Quidamortem tile the raid exits to, beside the entrance.
#define COX_LOBBY_EXIT_X 1233
#define COX_LOBBY_EXIT_Y 3571
// The layout's south-west corner (content/world/composed_maps.toml), and
// Great Olm's chamber, which is its own template.
#define COX_LAYOUT_X 3472
#define COX_LAYOUT_Y 5440
#define COX_OLM_X 3200
#define COX_OLM_Y 5696
// The end room's hole lands in the chamber's antechamber; the chamber's
// rope climbs back to the end room beside the hole.
#define COX_OLM_ARRIVAL_X 3232
#define COX_OLM_ARRIVAL_Y 5721
#define COX_END_RETURN_LX 12
#define COX_END_RETURN_LY 21
#define COX_TICKS_PER_SECOND_X10 6   // 0.6 s per tick

// ---------------------------------------------------------------------------
// The layout: one chain, the same every raid, rooms easiest to hardest. Each
// room is the north-exit variant of its template, copied by the map
// pipeline into a column north of the start room so every room's exit opens
// into the next room's entry.

static const uint8_t g_layout[] = {
    COX_ROOM_START, COX_ROOM_TIGHTROPE, COX_ROOM_CRABS, COX_ROOM_SHAMANS,
    COX_ROOM_MUTTADILES, COX_ROOM_TEKTON, COX_ROOM_END,
};
_Static_assert(sizeof(g_layout) < COX_MAX_ROOMS, "the layout and Olm fit the raid");

// ---------------------------------------------------------------------------
// State and small helpers.

static void cox_tick(RcWorld *world, void *ctx);

CoxRaid *cox_raid(const RcWorld *world) {
    return rc_world_content_ctx(world, cox_tick);
}

const CoxRoomOps *cox_room_ops(int type) {
    switch (type) {
    case COX_ROOM_TEKTON: return &cox_tekton_ops;
    case COX_ROOM_MUTTADILES: return &cox_muttadiles_ops;
    case COX_ROOM_GUARDIANS: return &cox_guardians_ops;
    case COX_ROOM_VESPULA: return &cox_vespula_ops;
    case COX_ROOM_SHAMANS: return &cox_shamans_ops;
    case COX_ROOM_VASA: return &cox_vasa_ops;
    case COX_ROOM_VANGUARDS: return &cox_vanguards_ops;
    case COX_ROOM_MYSTICS: return &cox_mystics_ops;
    case COX_ROOM_ICE_DEMON: return &cox_ice_demon_ops;
    case COX_ROOM_THIEVING: return &cox_thieving_ops;
    case COX_ROOM_TIGHTROPE: return &cox_tightrope_ops;
    case COX_ROOM_CRABS: return &cox_crabs_ops;
    case COX_ROOM_SCAVENGERS:
    case COX_ROOM_SCAVENGERS_LARGE: return &cox_scavengers_ops;
    case COX_ROOM_FARMING_FISH:
    case COX_ROOM_FARMING_BATS: return &cox_farming_ops;
    case COX_ROOM_OLM: return &cox_olm_ops;
    default: return NULL;
    }
}

int cox_room_index(const CoxRaid *raid, const CoxRoom *room) {
    return (int)(room - raid->rooms);
}

static int room_size(const CoxRoom *room) {
    return room->type == COX_ROOM_OLM ? 64 : COX_ROOM_SIZE;
}

CoxRoom *cox_room_at(CoxRaid *raid, int x, int y, int plane) {
    for (int i = 0; raid && i < raid->room_count; i++) {
        CoxRoom *room = &raid->rooms[i];
        int size = room_size(room);
        if (plane == room->plane && x >= room->x && x < room->x + size
                && y >= room->y && y < room->y + size)
            return room;
    }
    return NULL;
}

void cox_message(RcWorld *world, const char *fmt, ...) {
    char text[RC_INTERACTION_RESULT_MESSAGE_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    rc_interaction_publish_message(&world->player, text);
}

void cox_add_points(CoxRaid *raid, int points) {
    if (!raid || !raid->started || points <= 0) return;
    raid->personal_points += points;
    raid->total_points += points;
}

int cox_world_x(const CoxRoom *room, int lx) { return room->x + lx; }
int cox_world_y(const CoxRoom *room, int ly) { return room->y + ly; }
int cox_room_local_x(const CoxRoom *room, int wx) { return wx - room->x; }
int cox_room_local_y(const CoxRoom *room, int wy) { return wy - room->y; }

// The cache's collision, loaded or not; runtime locs (gates, crystals,
// spawned rocks) are not in it.
static bool static_walkable(int x, int y, int plane) {
    int found = 0;
    uint32_t flags = rc_collision_flags_at(x, y, plane, &found);
    return found && !(flags & (COL_BLOCK_WALK | COL_LOC));
}

bool cox_tile_walkable(const RcWorld *world, int x, int y, int plane) {
    if (world && rc_map_tile_loaded(&world->map, x, y, plane))
        return !(rc_get_flags(&world->map, x, y, plane) & (COL_BLOCK_WALK | COL_LOC));
    return static_walkable(x, y, plane);
}

bool cox_player_in_room(const RcWorld *world, const CoxRoom *room) {
    const RcPlayer *p = &world->player;
    int size = room_size(room);
    return p->plane == room->plane && p->x >= room->x && p->x < room->x + size
        && p->y >= room->y && p->y < room->y + size;
}

// Chebyshev distance between two square footprints (0 = touching/overlap).
int cox_chebyshev(int ax, int ay, int aw, int bx, int by, int bw) {
    int dx = bx > ax + aw - 1 ? bx - (ax + aw - 1)
           : ax > bx + bw - 1 ? ax - (bx + bw - 1) : 0;
    int dy = by > ay + aw - 1 ? by - (ay + aw - 1)
           : ay > by + bw - 1 ? ay - (by + bw - 1) : 0;
    return dx > dy ? dx : dy;
}

int cox_player_distance(const RcWorld *world, const RcNpc *npc) {
    const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
    int size = def && def->size > 0 ? def->size : 1;
    return cox_chebyshev(npc->x, npc->y, size, world->player.x,
                         world->player.y, 1);
}

int cox_rand(RcWorld *world, int bound) {
    return bound > 1 ? (int)rc_rng_bounded(&world->rng_state, (uint32_t)bound) : 0;
}

static int isqrt_int(int v) {
    int r = 0;
    while ((r + 1) * (r + 1) <= v) r++;
    return r;
}

static int clamp_int(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

int cox_npc_max_hit(int level, int bonus) {
    return ((level + 9) * (bonus + 64) + 320) / 640;
}

// OSRS Wiki DPS calculator's CoX scaling (party of one).
int cox_scale(const CoxRaid *raid, const CoxStats *base,
              RcNpcCombatOverride *out) {
    memset(out, 0, sizeof(*out));
    const int n = 1;
    int c = raid->scale_combat, h = raid->scale_hp;
    bool singles = base->flags & COX_STATS_SINGLES;
    int hp = base->hp * c / 126;
    if (singles) {
        if (hp < 5) hp = 5;
    } else {
        hp += hp * (n / 2);
    }
    int off_mul = 100 + 7 * isqrt_int(n - 1) + (n - 1);
    int def_mul = 100 + isqrt_int(n - 1) + 7 * (n - 1) / 10;
    int16_t levels[6] = {base->att, base->def, base->str, base->hp, base->rng,
                         base->mag};
    for (int i = 0; i < 6; i++) {
        if (i == 3) continue;
        int v = levels[i];
        if (v <= 1) {
            out->stats[i] = 1;
            continue;
        }
        bool defensive = i == 1 || (i == 5 && (base->flags & COX_STATS_MAGIC_DEFENSIVE));
        if (singles) {
            v = v * clamp_int(h, 55, 99) / 99;
            out->stats[i] = v > 1 ? v : 1;
            continue;
        }
        v = v * h / 99;
        v = v * (defensive ? def_mul : off_mul) / 100;
        out->stats[i] = clamp_int(v, 50, defensive ? 20000 : 5000);
    }
    out->stats[3] = clamp_int(hp, singles ? 5 : 50, 30000);
    out->hitpoints = out->stats[3];
    out->max_hit = base->max_hit;
    if (base->flags & COX_STATS_MAX_FORMULA) {
        int level = base->style == COMBAT_RANGED ? out->stats[4]
                  : base->style == COMBAT_MAGIC ? out->stats[5] : out->stats[2];
        out->max_hit = cox_npc_max_hit(level, base->max_bonus);
    }
    out->attack_speed = base->attack_speed;
    out->attack_style = base->style;
    memcpy(out->attack_bonus, base->attack_bonus, sizeof(out->attack_bonus));
    memcpy(out->defence_bonus, base->defence_bonus, sizeof(out->defence_bonus));
    return out->hitpoints;
}

static void remember_npc(const RcWorld *world, CoxRoomNpc *rn, const RcNpc *npc) {
    const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
    rn->x = (int16_t)npc->x;
    rn->y = (int16_t)npc->y;
    rn->hp = (int16_t)(npc->is_dead ? 0 : npc->current_hp);
    rn->form = (int16_t)(def ? def->id : rn->npc_id);
    rn->untargetable = npc->player_untargetable;
    rn->owner_controlled = npc->owner_controlled;
    for (int i = 0; i < 6; i++) rn->live_stats[i] = (int16_t)npc->stats[i];
    memcpy(rn->defence_bonus, npc->defence_bonus, sizeof(rn->defence_bonus));
    rn->max_hit_override = npc->max_hit_override;
}

// Respawns a room NPC the active area dropped, as it was when last seen.
static void restore_npc(RcWorld *world, CoxRaid *raid, CoxRoom *room,
                        CoxRoomNpc *rn) {
    int def_idx = rc_npc_def_find(rn->npc_id);
    if (def_idx < 0) return;
    RcNpcSpawnConfig config = {.wander_range = 0};
    RcNpcSpawnResult r = rc_npc_spawn_ex(world, def_idx, rn->x, rn->y,
                                         room->plane, &config);
    if (r.status != RC_NPC_SPAWN_CREATED) return;
    RcNpc *npc = &world->npcs[r.slot];
    rn->uid = r.uid;
    if (rn->stats) {
        RcNpcCombatOverride o;
        cox_scale(raid, rn->stats, &o);
        rc_npc_apply_combat_override(npc, &o);
    }
    if (rn->form != rn->npc_id) {
        int form = rc_npc_def_find(rn->form);
        if (form >= 0) rc_npc_set_form(npc, form);
    }
    npc->current_hp = rn->hp;
    for (int i = 0; i < 6; i++) npc->stats[i] = rn->live_stats[i];
    memcpy(npc->defence_bonus, rn->defence_bonus, sizeof(npc->defence_bonus));
    npc->max_hit_override = rn->max_hit_override;
    npc->disable_wander = true;
    npc->player_untargetable = rn->untargetable;
    npc->owner_controlled = rn->owner_controlled;
    // Simple NPCs pick their fight back up.
    if (!rn->owner_controlled) rc_combat_start_npc_vs_player(world, npc->uid, 0);
}

RcNpc *cox_spawn(RcWorld *world, CoxRaid *raid, CoxRoom *room, int npc_id,
                 int x, int y, int role, const CoxStats *stats, bool scripted) {
    int def_idx = rc_npc_def_find(npc_id);
    if (def_idx < 0) return NULL;
    RcNpcSpawnConfig config = {.wander_range = 0};
    RcNpcSpawnResult r = rc_npc_spawn_ex(world, def_idx, x, y, room->plane,
                                         &config);
    if (r.status != RC_NPC_SPAWN_CREATED) return NULL;
    RcNpc *npc = &world->npcs[r.slot];
    npc->disable_wander = true;
    npc->owner_controlled = scripted;
    if (stats) {
        RcNpcCombatOverride o;
        cox_scale(raid, stats, &o);
        rc_npc_apply_combat_override(npc, &o);
    }
    CoxRoomNpc *slot = NULL;
    for (int i = 0; i < room->npc_count; i++)
        if (room->npcs[i].role == role && role) slot = &room->npcs[i];
    if (!slot && room->npc_count < COX_ROOM_NPCS)
        slot = &room->npcs[room->npc_count++];
    if (slot) {
        slot->uid = npc->uid;
        slot->npc_id = (int16_t)npc_id;
        slot->role = (uint8_t)role;
        slot->stats = stats;
        slot->scripted = scripted;
        remember_npc(world, slot, npc);
    }
    return npc;
}

RcNpc *cox_room_npc(RcWorld *world, CoxRoom *room, int role) {
    for (int i = 0; i < room->npc_count; i++) {
        if (room->npcs[i].role != role) continue;
        RcNpc *npc = rc_npc_resolve(world, room->npcs[i].uid);
        if (npc && npc->active && !npc->is_dead) return npc;
    }
    return NULL;
}

int cox_room_npc_role(const CoxRoom *room, RcNpcId uid) {
    for (int i = 0; i < room->npc_count; i++)
        if (room->npcs[i].uid == uid) return room->npcs[i].role;
    return 0;
}

void cox_remove_room_npcs(RcWorld *world, CoxRoom *room) {
    for (int i = 0; i < room->npc_count; i++) {
        if (rc_npc_resolve(world, room->npcs[i].uid))
            rc_npc_remove(world, room->npcs[i].uid);
    }
    room->npc_count = 0;
}

// Placements of `loc_id` in a room block, from the cache map data.
int cox_find_loc(const RcWorld *world, const CoxRoom *room, int loc_id,
                 int *out_x, int *out_y, int *out_rotation, int max) {
    (void)world;
    int found = 0;
    int size = room_size(room);
    for (int my = room->y / 64; my <= (room->y + size - 1) / 64; my++) {
        for (int mx = room->x / 64; mx <= (room->x + size - 1) / 64; mx++) {
            uint16_t ms;
            if (!rc_world_to_mapsquare(mx * 64, my * 64, &ms, NULL, NULL))
                continue;
            int count = 0;
            const RcObjectPlacement *rows = rc_object_region_placements(ms, &count);
            for (int i = 0; rows && i < count; i++) {
                const RcObjectPlacement *p = &rows[i];
                if ((int)p->obj_id != loc_id || p->plane != room->plane
                        || p->x < room->x || p->x >= room->x + size
                        || p->y < room->y || p->y >= room->y + size)
                    continue;
                if (found < max) {
                    if (out_x) out_x[found] = p->x;
                    if (out_y) out_y[found] = p->y;
                    if (out_rotation) out_rotation[found] = p->rotation;
                }
                found++;
            }
        }
    }
    return found;
}

int cox_add_loc(RcWorld *world, int loc_id, int type, int rotation,
                int x, int y, int plane, uint64_t *out_key) {
    uint16_t ms;
    if (!rc_world_to_mapsquare(x, y, &ms, NULL, NULL)) return 0;
    RcObjectPlacement p = {.obj_id = (uint32_t)loc_id, .x = (uint16_t)x,
                           .y = (uint16_t)y, .mapsquare = ms,
                           .plane = (uint8_t)plane, .type = (uint8_t)type,
                           .rotation = (uint8_t)rotation};
    return rc_world_object_add(world, &p, 0, out_key) == RC_OBJECT_MUTATION_OK;
}

static int find_placement(const RcWorld *world, int loc_id, int x, int y,
                          int plane, RcObjectPlacement *out) {
    RcObjectPlacement rows[8];
    int n = rc_object_placements_at(x, y, plane, rows, 8);
    for (int i = 0; i < n; i++) {
        if ((int)rows[i].obj_id == loc_id) {
            *out = rows[i];
            return 1;
        }
    }
    // A spawned or replaced loc lives in the runtime object states.
    for (int i = 0; i < world->object_state_count; i++) {
        const RcObjectState *s = &world->object_states[i];
        if (s->active_obj_id == loc_id && s->active_x == x && s->active_y == y
                && s->active_plane == plane) {
            uint16_t ms;
            if (!rc_world_to_mapsquare(x, y, &ms, NULL, NULL)) return 0;
            *out = (RcObjectPlacement){.obj_id = (uint32_t)loc_id,
                .key = s->placement_key, .x = (uint16_t)x, .y = (uint16_t)y,
                .mapsquare = ms, .plane = (uint8_t)plane,
                .type = s->active_type, .rotation = s->active_rotation};
            return 2;
        }
    }
    return 0;
}

bool cox_loc_present(const RcWorld *world, int loc_id, int x, int y, int plane) {
    for (int i = 0; i < world->object_state_count; i++) {
        const RcObjectState *s = &world->object_states[i];
        if (s->active_obj_id == loc_id && s->active_x == x && s->active_y == y
                && s->active_plane == plane)
            return true;
    }
    RcObjectPlacement rows[8];
    int n = rc_object_placements_at(x, y, plane, rows, 8);
    for (int i = 0; i < n; i++)
        if ((int)rows[i].obj_id == loc_id
                && !rc_world_object_state_find_key_const(world, rows[i].key))
            return true;
    return false;
}

int cox_remove_loc(RcWorld *world, int loc_id, int x, int y, int plane) {
    RcObjectPlacement p;
    int found = find_placement(world, loc_id, x, y, plane, &p);
    if (found == 2) {
        const RcObjectState *s = rc_world_object_state_find_key(world, p.key);
        if (s && s->base_obj_id < 0)
            return rc_world_object_revert(world, p.key) == RC_OBJECT_MUTATION_OK;
        RcObjectPlacement base = p;
        base.obj_id = (uint32_t)s->base_obj_id;
        base.x = (uint16_t)s->x;
        base.y = (uint16_t)s->y;
        base.type = s->base_type;
        base.rotation = s->base_rotation;
        return rc_world_object_delete(world, &base, 0, 0) == RC_OBJECT_MUTATION_OK;
    }
    return found && rc_world_object_delete(world, &p, 0, 0) == RC_OBJECT_MUTATION_OK;
}

int cox_replace_loc(RcWorld *world, int loc_id, int x, int y, int plane,
                    int replacement_id) {
    RcObjectPlacement p;
    int found = find_placement(world, loc_id, x, y, plane, &p);
    if (!found) return 0;
    RcObjectPlacement base = p;
    if (found == 2) {
        const RcObjectState *s = rc_world_object_state_find_key(world, p.key);
        if (!s) return 0;
        if (s->base_obj_id < 0) {
            // Spawned loc: swap the spawn.
            int type = s->active_type, rotation = s->active_rotation;
            rc_world_object_revert(world, p.key);
            return cox_add_loc(world, replacement_id, type, rotation, x, y,
                               plane, NULL);
        }
        base.obj_id = (uint32_t)s->base_obj_id;
        base.x = (uint16_t)s->x;
        base.y = (uint16_t)s->y;
        base.type = s->base_type;
        base.rotation = s->base_rotation;
    }
    RcObjectPlacement replacement = p;
    replacement.obj_id = (uint32_t)replacement_id;
    return rc_world_object_replace(world, &base, &replacement, 0, 0)
        == RC_OBJECT_MUTATION_OK;
}

int cox_add_hazard(CoxRaid *raid, const CoxHazard *hazard) {
    for (int i = 0; i < COX_MAX_HAZARDS; i++) {
        if (raid->hazards[i].kind) continue;
        raid->hazards[i] = *hazard;
        return i;
    }
    return -1;
}

static void tick_hazards(RcWorld *world, CoxRaid *raid) {
    RcPlayer *p = &world->player;
    for (int i = 0; i < COX_MAX_HAZARDS; i++) {
        CoxHazard *h = &raid->hazards[i];
        if (!h->kind) continue;
        bool live = world->tick >= h->land_tick
            && (h->kind == COX_HAZARD_STRIKE ? world->tick == h->land_tick
                                             : world->tick < h->expire_tick);
        if (live && !p->is_dead && p->plane == h->plane) {
            int d = cox_chebyshev(h->x, h->y, 1, p->x, p->y, 1);
            if (d <= h->radius) {
                int lo = h->min, hi = h->max;
                if (d > 0 && h->edge_max) {
                    lo = h->edge_min;
                    hi = h->edge_max;
                }
                int damage = lo + cox_rand(world, hi - lo + 1);
                if ((h->flags & COX_HAZARD_PRAYER_HALVES)
                        && rc_combat_apply_protection(1, h->style,
                                                      p->active_prayers, true) == 0)
                    damage /= 2;
                rc_combat_damage_player(world, damage, 0, (RcCombatStyle)h->style,
                                        (int)h->source);
                if ((h->flags & COX_HAZARD_POISON) && damage > 0)
                    rc_player_apply_toxin(world, h->max > 6 ? 6 : 2, false);
            }
        }
        bool done = h->kind == COX_HAZARD_STRIKE ? world->tick >= h->land_tick
                                                 : world->tick + 1 >= h->expire_tick;
        if (done) {
            if (h->loc_id >= 0 && h->loc_key)
                rc_world_object_revert(world, h->loc_key);
            memset(h, 0, sizeof(*h));
        }
    }
}

static void set_current_room(RcWorld *world, CoxRaid *raid, CoxRoom *room) {
    raid->current_room = (uint8_t)cox_room_index(raid, room);
    // Restore room NPCs the active area dropped while the player was away.
    for (int i = 0; i < room->npc_count; i++) {
        CoxRoomNpc *rn = &room->npcs[i];
        if (rn->hp > 0 && !rc_npc_resolve(world, rn->uid))
            restore_npc(world, raid, room, rn);
    }
    if (!(room->flags & COX_ROOM_ENTERED)) room->flags |= COX_ROOM_ENTERED;
    const CoxRoomOps *ops = cox_room_ops(room->type);
    if (ops && ops->enter) ops->enter(world, raid, room);
}

void cox_relocate(RcWorld *world, CoxRaid *raid, int x, int y, int plane) {
    if (!rc_world_relocate_player(world, x, y, plane)) return;
    raid->action.kind = 0;
    CoxRoom *room = cox_room_at(raid, x, y, plane);
    if (room) set_current_room(world, raid, room);
}

bool cox_raid_item(int item_id) {
    switch (item_id) {
    case 20886: case 20888: case 20890: case 20893: case 20895: case 20897:
    case 20899:
        return false;   // lore books leave with the player
    default:
        break;
    }
    return (item_id >= 20799 && item_id <= 20801)
        || (item_id >= 20853 && item_id <= 20996)
        || item_id == 21036
        || (item_id >= 25754 && item_id <= 25765);
}

int cox_inventory_count(const RcPlayer *p, int item_id) {
    int n = 0;
    for (int i = 0; i < RC_INVENTORY_SIZE; i++)
        if (p->inventory[i].item_id == item_id) n += p->inventory[i].quantity;
    return n;
}

bool cox_has_item(const RcPlayer *p, int item_id) {
    return cox_inventory_count(p, item_id) > 0;
}

bool cox_has_any(const RcPlayer *p, const int *ids, int count) {
    for (int i = 0; i < count; i++) {
        if (cox_has_item(p, ids[i])) return true;
        for (int e = 0; e < RC_EQUIP_COUNT; e++)
            if (p->equipment[e].item_id == ids[i]) return true;
    }
    return false;
}

static int register_key(RcWorld *world, RcInteractionKind kind,
                        RcInteractionOp op, int definition_id, int item_id,
                        RcInteractionHandlerFn fn) {
    RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
    key.kind = kind;
    key.op = op;
    key.definition_id = definition_id;
    if (item_id >= 0) key.source_item_id = item_id;
    return rc_interaction_register_world_handler(world, &key, fn, NULL) != 0;
}

int cox_register_object(RcWorld *world, int obj_id, int option,
                        RcInteractionHandlerFn fn) {
    return register_key(world, RC_INTERACTION_OBJECT,
                        rc_interaction_op_from_option(option), obj_id, -1, fn);
}

int cox_register_npc(RcWorld *world, int npc_id, int option,
                     RcInteractionHandlerFn fn) {
    return register_key(world, RC_INTERACTION_NPC,
                        rc_interaction_op_from_option(option), npc_id, -1, fn);
}

int cox_register_item_on_object(RcWorld *world, int item_id, int obj_id,
                                RcInteractionHandlerFn fn) {
    return register_key(world, RC_INTERACTION_OBJECT, RC_INTERACTION_USE_ON,
                        obj_id, item_id, fn);
}

int cox_register_item(RcWorld *world, int item_id, int option,
                      RcInteractionHandlerFn fn) {
    return register_key(world, RC_INTERACTION_INVENTORY_ITEM,
                        rc_interaction_op_from_option(option), item_id, -1, fn);
}

int cox_register_item_on_item(RcWorld *world, int item_id, int target_id,
                              RcInteractionHandlerFn fn) {
    return register_key(world, RC_INTERACTION_INVENTORY_ITEM,
                        RC_INTERACTION_USE_ON, target_id, item_id, fn);
}

// ---------------------------------------------------------------------------
// Layout and the way down.

static void add_room(CoxRaid *raid, int type, int x, int y) {
    CoxRoom *room = &raid->rooms[raid->room_count++];
    memset(room, 0, sizeof(*room));
    room->type = (uint8_t)type;
    room->variant = COX_EXIT_NORTH;
    room->x = (int16_t)x;
    room->y = (int16_t)y;
}

static void build_layout(CoxRaid *raid) {
    raid->room_count = 0;
    for (int i = 0; i < (int)sizeof(g_layout); i++)
        add_room(raid, g_layout[i], COX_LAYOUT_X, COX_LAYOUT_Y + COX_ROOM_SIZE * i);
    raid->olm_room = raid->room_count;
    add_room(raid, COX_ROOM_OLM, COX_OLM_X, COX_OLM_Y);
}

// The end room, last before Great Olm, and the tile beside its hole.
static void end_return(const CoxRaid *raid, int *x, int *y, int *plane) {
    const CoxRoom *end = &raid->rooms[raid->olm_room - 1];
    *x = end->x + COX_END_RETURN_LX;
    *y = end->y + COX_END_RETURN_LY;
    *plane = end->plane;
}

static void format_duration(char *dst, size_t cap, RcTick ticks) {
    unsigned long long s = (unsigned long long)ticks * COX_TICKS_PER_SECOND_X10 / 10;
    if (s >= 3600)
        snprintf(dst, cap, "%llu:%02llu:%02llu", s / 3600, s / 60 % 60, s % 60);
    else
        snprintf(dst, cap, "%llu:%02llu", s / 60, s % 60);
}

// Dropping into Great Olm's chamber completes the level above it, which
// OSRS always names the lower level.
static void enter_olm_level(RcWorld *world, CoxRaid *raid) {
    if (raid->reached_olm) return;
    char duration[24];
    format_duration(duration, sizeof(duration), world->tick - raid->start_tick);
    cox_message(world, "Lower level complete! Duration: %s", duration);
    raid->reached_olm = true;
    rc_varbit_set(world, VARBIT_PROGRESS, PROGRESS_OLM);
}

void cox_complete_room(RcWorld *world, CoxRaid *raid, CoxRoom *room) {
    (void)raid;
    if (room->flags & COX_ROOM_COMPLETE) return;
    room->flags |= COX_ROOM_COMPLETE;
    room->completed_tick = world->tick;
}

// ---------------------------------------------------------------------------
// Entering, starting and leaving.

static void reset_raid(RcWorld *world, CoxRaid *raid) {
    for (int i = 0; i < raid->room_count; i++)
        cox_remove_room_npcs(world, &raid->rooms[i]);
    // Revert every loc the raid changed in its rooms.
    for (int i = world->object_state_count - 1; i >= 0; i--) {
        const RcObjectState *s = &world->object_states[i];
        if (cox_room_at(raid, s->x, s->y, s->plane))
            rc_world_object_revert(world, s->placement_key);
    }
    CoxOlm *olm = raid->olm;
    bool full = raid->full_difficulty;
    void (*prev_hit)(RcWorld *, RcNpc *, const RcPendingHit *, int) =
        raid->prev_on_player_hit_npc;
    void (*prev_hit_player)(RcWorld *, const RcPendingHit *, int) =
        raid->prev_on_npc_hit_player;
    memset(raid, 0, sizeof(*raid));
    raid->olm = olm;
    raid->full_difficulty = full;
    raid->prev_on_player_hit_npc = prev_hit;
    raid->prev_on_npc_hit_player = prev_hit_player;
    raid->current_room = COX_NO_ROOM;
    rc_varbit_set(world, VARBIT_PARTYSCORE, 0);
    rc_varbit_set(world, VARBIT_TIMER, 0);
    rc_varp_set(world, VARP_PLAYERSCORE, 0);
}

// Inside the lobby, up from its steps.
static void lobby_arrival(const CoxRoom *lobby, int *x, int *y) {
    *x = lobby->x + 2;
    *y = lobby->y + 4;
}

static void enter_raid(RcWorld *world, CoxRaid *raid) {
    reset_raid(world, raid);
    raid->in_raid = true;
    build_layout(raid);
    const RcPlayer *p = &world->player;
    int combat = rc_combat_level(&p->skills);
    int hp = p->skills.base_level[SKILL_HITPOINTS];
    raid->scale_combat = raid->full_difficulty ? 126 : clamp_int(combat, 60, 126);
    raid->scale_hp = raid->full_difficulty ? 99
                   : clamp_int(55 + 44 * hp / 99, 55, 99);
    rc_varbit_set(world, VARBIT_ISLEADER, 1);
    rc_varbit_set(world, VARBIT_INDUNGEON, 1);
    rc_varbit_set(world, VARBIT_PARTYSIZE, 1);
    rc_varbit_set(world, VARBIT_PROGRESS, 0);
    int x, y;
    lobby_arrival(&raid->rooms[0], &x, &y);
    cox_relocate(world, raid, x, y, 0);
    world->player.run_energy = 10000;
}

static void purge_raid_items(RcWorld *world, bool drop, int x, int y, int plane) {
    RcItemTransaction tx;
    if (rc_item_tx_begin(&tx, world).code != RC_ITEM_RESULT_OK) return;
    RcGroundGrant grants[RC_INVENTORY_SIZE];
    int grant_count = 0;
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        const RcInvSlot *slot = &world->player.inventory[i];
        if (slot->item_id < 0 || !cox_raid_item(slot->item_id)) continue;
        if (drop && grant_count < RC_INVENTORY_SIZE)
            grants[grant_count++] = (RcGroundGrant){slot->item_id, slot->quantity, 0};
        rc_item_tx_remove_slot(&tx, i, slot->quantity, slot->generation);
    }
    if (drop && grant_count)
        rc_item_tx_commit_ground(&tx, grants, grant_count, x, y, plane,
                                 RC_GROUND_OWNER_LOCAL_PLAYER,
                                 (RcGroundPolicy){0, 3000, 0, 3000});
    else
        rc_item_tx_commit(&tx);
}

// Ends the raid: its items go, its state and locs reset, the overlay hides.
static void end_raid(RcWorld *world, CoxRaid *raid) {
    purge_raid_items(world, false, 0, 0, 0);
    reset_raid(world, raid);
    rc_varbit_set(world, VARBIT_ISLEADER, 0);
    rc_varbit_set(world, VARBIT_INDUNGEON, 0);
    rc_varbit_set(world, VARBIT_PROGRESS, 0);
    rc_world_close_choice(world);
}

static void leave_raid(RcWorld *world, CoxRaid *raid) {
    end_raid(world, raid);
    rc_world_relocate_player(world, COX_LOBBY_EXIT_X, COX_LOBBY_EXIT_Y, 0);
    world->player.run_energy = 10000;
}

static void start_raid(RcWorld *world, CoxRaid *raid) {
    if (raid->started) return;
    raid->started = true;
    raid->start_tick = world->tick;
    rc_varbit_set(world, VARBIT_PROGRESS, PROGRESS_STARTED);
    CoxRoom *lobby = &raid->rooms[0];
    int x[2], y[2];
    if (cox_find_loc(world, lobby, LOC_BANK_CHEST, x, y, NULL, 1) > 0)
        cox_replace_loc(world, LOC_BANK_CHEST, x[0], y[0], lobby->plane,
                        LOC_BANK_CHEST_CLOSED);
    cox_message(world, "The raid has begun!");
}

// ---------------------------------------------------------------------------
// Entry and difficulty (Mount Quidamortem). RuneC raids are solo, so there is
// no party to form at a recruiting board: the steps start a normal raid.
// Challenge Mode is not implemented.

static RcInteractionHandlerResult rimor(RcWorld *world, RcPlayer *player,
                                        const RcPendingInteraction *pending,
                                        void *ctx) {
    (void)player;
    (void)ctx;
    CoxRaid *raid = cox_raid(world);
    if (!raid) return rc_interaction_result_cancel(RC_INTERACTION_FAIL_NO_HANDLER);
    // Options: Talk-to, Claim-cape (third), Set-difficulty (fourth).
    switch (pending->op) {
    case RC_INTERACTION_OP4:
        raid->full_difficulty = !raid->full_difficulty;
        return rc_interaction_result_message(raid->full_difficulty
            ? "Captain Rimor: Your raids will no longer scale to your combat level."
            : "Captain Rimor: Your raids will scale to your combat level again.");
    case RC_INTERACTION_OP3:
        return rc_interaction_result_message("Captain Rimor: I have no cape for you.");
    default:
        return rc_interaction_result_message(
            "Captain Rimor: The chambers lie through the steps.");
    }
}

static RcInteractionHandlerResult enter_chambers(RcWorld *world, RcPlayer *player,
                                                 const RcPendingInteraction *pending,
                                                 void *ctx) {
    (void)player;
    (void)pending;
    (void)ctx;
    CoxRaid *raid = cox_raid(world);
    if (!raid) return rc_interaction_result_cancel(RC_INTERACTION_FAIL_NO_HANDLER);
    if (!raid->in_raid) enter_raid(world, raid);
    return rc_interaction_result_complete();
}

// ---------------------------------------------------------------------------
// Object handlers inside the raid.

static void start_choice(RcWorld *world, int option, void *ctx) {
    (void)ctx;
    CoxRaid *raid = cox_raid(world);
    if (raid && raid->in_raid && option == 0) start_raid(world, raid);
}

static RcInteractionHandlerResult passage(RcWorld *world, RcPlayer *player,
                                          const RcPendingInteraction *pending,
                                          void *ctx) {
    (void)ctx;
    CoxRaid *raid = cox_raid(world);
    if (!raid || !raid->in_raid)
        return rc_interaction_result_failure(RC_INTERACTION_FAIL_INVALID_TARGET,
                                             "Nothing interesting happens.");
    if (!raid->started) {
        const char *options[2] = {"Yes, start the raid.", "No."};
        rc_world_open_choice(world, "Start the raid?", options, 2,
                             start_choice, NULL);
        return rc_interaction_result_complete();
    }
    RcObjectPlacement placement;
    if (!rc_world_object_current_placement(world, pending->target.definition_id,
            pending->target.tile_x, pending->target.tile_y,
            pending->target.plane, pending->target.placement_key, &placement))
        return rc_interaction_result_failure(RC_INTERACTION_FAIL_TARGET_MISSING,
                                             "You can't go that way.");
    // Crossing moves 4 tiles through the passage's long axis.
    int x = player->x, y = player->y;
    if (placement.rotation % 2 == 0)
        x += player->x < placement.x ? 4 : -4;
    else
        y += player->y < placement.y ? 4 : -4;
    if (!cox_tile_walkable(world, x, y, player->plane))
        return rc_interaction_result_failure(RC_INTERACTION_FAIL_CANNOT_REACH,
                                             "You can't go that way.");
    cox_relocate(world, raid, x, y, player->plane);
    return rc_interaction_result_complete();
}

static RcInteractionHandlerResult hole(RcWorld *world, RcPlayer *player,
                                       const RcPendingInteraction *pending,
                                       void *ctx) {
    (void)player;
    (void)ctx;
    CoxRaid *raid = cox_raid(world);
    if (!raid || !raid->in_raid || !raid->started)
        return rc_interaction_result_failure(RC_INTERACTION_FAIL_INVALID_TARGET,
                                             "Nothing interesting happens.");
    CoxRoom *room = cox_room_at(raid, pending->target.tile_x,
                                pending->target.tile_y, pending->target.plane);
    if (!room || room->type != COX_ROOM_END)
        return rc_interaction_result_failure(RC_INTERACTION_FAIL_INVALID_TARGET,
                                             "Nothing interesting happens.");
    enter_olm_level(world, raid);
    cox_relocate(world, raid, COX_OLM_ARRIVAL_X, COX_OLM_ARRIVAL_Y, 0);
    return rc_interaction_result_complete();
}

static RcInteractionHandlerResult rope(RcWorld *world, RcPlayer *player,
                                       const RcPendingInteraction *pending,
                                       void *ctx) {
    (void)player;
    (void)ctx;
    CoxRaid *raid = cox_raid(world);
    if (!raid || !raid->in_raid)
        return rc_interaction_result_failure(RC_INTERACTION_FAIL_INVALID_TARGET,
                                             "Nothing interesting happens.");
    CoxRoom *room = cox_room_at(raid, pending->target.tile_x,
                                pending->target.tile_y, pending->target.plane);
    if (!room || room->type != COX_ROOM_OLM)
        return rc_interaction_result_failure(RC_INTERACTION_FAIL_INVALID_TARGET,
                                             "Nothing interesting happens.");
    int x, y, plane;
    end_return(raid, &x, &y, &plane);
    cox_relocate(world, raid, x, y, plane);
    return rc_interaction_result_complete();
}

static RcInteractionHandlerResult exit_steps(RcWorld *world, RcPlayer *player,
                                             const RcPendingInteraction *pending,
                                             void *ctx) {
    (void)player;
    (void)pending;
    (void)ctx;
    CoxRaid *raid = cox_raid(world);
    if (!raid || !raid->in_raid)
        return rc_interaction_result_failure(RC_INTERACTION_FAIL_INVALID_TARGET,
                                             "Nothing interesting happens.");
    leave_raid(world, raid);
    return rc_interaction_result_complete();
}

static RcInteractionHandlerResult energy_well(RcWorld *world, RcPlayer *player,
                                              const RcPendingInteraction *pending,
                                              void *ctx) {
    (void)world;
    (void)pending;
    (void)ctx;
    player->run_energy = 10000;
    return rc_interaction_result_message("You feel refreshed.");
}

// ---------------------------------------------------------------------------
// Death and the points overlay.

static void on_player_died(RcWorld *world, int evt, const void *payload,
                           void *ctx) {
    (void)evt;
    (void)payload;
    (void)ctx;
    CoxRaid *raid = cox_raid(world);
    if (!raid || !raid->in_raid) return;
    const RcPlayer *p = &world->player;
    purge_raid_items(world, true, p->x, p->y, p->plane);
    int lost = raid->personal_points * 40 / 100;
    raid->personal_points -= lost;
    raid->total_points -= lost;
    raid->respawn_tick = world->tick + 5;
    raid->action.kind = 0;
}

// A raider who dies respawns in the lobby, or past the level beside the
// end room's hole.
static void respawn(RcWorld *world, CoxRaid *raid) {
    raid->respawn_tick = 0;
    int x, y, plane = raid->rooms[0].plane;
    if (raid->reached_olm)
        end_return(raid, &x, &y, &plane);
    else
        lobby_arrival(&raid->rooms[0], &x, &y);
    if (!rc_world_respawn_player(world, x, y, plane)) return;
    CoxRoom *room = cox_room_at(raid, x, y, plane);
    if (room) raid->current_room = (uint8_t)cox_room_index(raid, room);
    cox_message(world, "Oh dear, you are dead!");
}

// The overlay's values, sent as the server does: the client formats them.
static void update_overlay(RcWorld *world, CoxRaid *raid) {
    rc_varbit_set(world, VARBIT_PARTYSCORE,
                  (uint32_t)(raid->total_points > 0 ? raid->total_points : 0));
    rc_varp_set(world, VARP_PLAYERSCORE,
                raid->personal_points > 0 ? raid->personal_points : 0);
    RcTick end = raid->finish_tick ? raid->finish_tick : world->tick;
    RcTick ticks = raid->started ? end - raid->start_tick : 0;
    rc_varbit_set(world, VARBIT_TIMER, (uint32_t)(ticks > 0xFFFF ? 0xFFFF : ticks));
    if (raid->finish_tick) rc_varbit_set(world, VARBIT_PROGRESS, PROGRESS_DONE);
}

// Raid NPCs drop only the supplies their rooms give, never their generic
// table.
static RcLootPolicy raid_loot(RcWorld *world, const RcNpc *npc,
                              RcGroundGrant *out, int capacity, int *count,
                              void *ctx) {
    (void)out;
    (void)capacity;
    (void)count;
    (void)ctx;
    const RcNpcDef *def = rc_npc_base_def_for_npc(npc);
    CoxRaid *raid = cox_raid(world);
    if (raid && raid->in_raid && def && def->id >= 7525 && def->id <= 7606)
        return RC_LOOT_SUPPRESS;
    return RC_LOOT_GENERIC;
}

CoxRoom *cox_room_for_npc(CoxRaid *raid, RcNpcId uid, int *role) {
    for (int i = 0; i < raid->room_count; i++) {
        int r = cox_room_npc_role(&raid->rooms[i], uid);
        if (r) {
            *role = r;
            return &raid->rooms[i];
        }
    }
    return NULL;
}

static void on_npc_died(RcWorld *world, int evt, const void *payload,
                        void *ctx) {
    (void)evt;
    (void)ctx;
    const RcPayloadNpcEvent *e = payload;
    CoxRaid *raid = cox_raid(world);
    if (!raid || !raid->in_raid) return;
    int role = 0;
    CoxRoom *room = cox_room_for_npc(raid, e->npc_id, &role);
    if (!room) return;
    for (int i = 0; i < room->npc_count; i++)
        if (room->npcs[i].uid == e->npc_id) room->npcs[i].hp = 0;
    RcNpc *npc = rc_npc_resolve(world, e->npc_id);
    const CoxRoomOps *ops = cox_room_ops(room->type);
    if (npc && ops && ops->npc_died) ops->npc_died(world, raid, room, npc, role);
}

static void on_player_hit_npc(RcWorld *world, RcNpc *npc,
                              const RcPendingHit *hit, int damage) {
    CoxRaid *raid = cox_raid(world);
    if (raid && raid->prev_on_player_hit_npc)
        raid->prev_on_player_hit_npc(world, npc, hit, damage);
    if (!raid || !raid->in_raid || !npc) return;
    int role = 0;
    CoxRoom *room = cox_room_for_npc(raid, (RcNpcId)npc->uid, &role);
    if (!room) return;
    // Damage points scale with combat level (5 per HP at 126 combat).
    cox_add_points(raid, damage * 5 * raid->scale_combat / 126);
    const CoxRoomOps *ops = cox_room_ops(room->type);
    if (ops && ops->npc_hit) ops->npc_hit(world, raid, room, npc, role, damage);
}

// Raid NPCs follow their room's rules (Vespula's poison, Olm's acid), not
// the regular poisonous-monster tags their wiki infoboxes give them.
static void on_npc_hit_player(RcWorld *world, const RcPendingHit *hit,
                              int damage) {
    CoxRaid *raid = cox_raid(world);
    int role = 0;
    CoxRoom *room = raid && raid->in_raid && hit && hit->source_idx >= 0
        ? cox_room_for_npc(raid, (RcNpcId)hit->source_idx, &role) : NULL;
    if (!room) {
        if (raid && raid->prev_on_npc_hit_player)
            raid->prev_on_npc_hit_player(world, hit, damage);
        return;
    }
    RcNpc *npc = rc_npc_resolve(world, hit->source_idx);
    const CoxRoomOps *ops = cox_room_ops(room->type);
    if (npc && ops && ops->hit_player)
        ops->hit_player(world, raid, room, npc, role, hit, damage);
}

static void track_room_npcs(RcWorld *world, CoxRoom *room) {
    for (int i = 0; i < room->npc_count; i++) {
        CoxRoomNpc *rn = &room->npcs[i];
        const RcNpc *npc = rc_npc_resolve(world, rn->uid);
        if (npc && npc->active) remember_npc(world, rn, npc);
    }
}

static void cox_tick(RcWorld *world, void *ctx) {
    CoxRaid *raid = ctx;
    if (!raid->in_raid) return;
    RcPlayer *p = &world->player;
    if (raid->respawn_tick && world->tick >= raid->respawn_tick && p->is_dead)
        respawn(world, raid);
    // Leaving the raid's rooms by any other means ends the raid.
    CoxRoom *here = cox_room_at(raid, p->x, p->y, p->plane);
    if (!p->is_dead && !here) {
        end_raid(world, raid);
        return;
    }
    // Walking into a room (or any move into one) enters it.
    if (here && cox_room_index(raid, here) != raid->current_room)
        set_current_room(world, raid, here);
    if (raid->current_room < raid->room_count) {
        CoxRoom *room = &raid->rooms[raid->current_room];
        const CoxRoomOps *ops = cox_room_ops(room->type);
        if (raid->started && ops && ops->tick) ops->tick(world, raid, room);
        track_room_npcs(world, room);
    }
    tick_hazards(world, raid);
    cox_tick_action(world, raid);
    cox_tick_effects(world, raid);
    update_overlay(world, raid);
}

static void release_raid(void *ctx) {
    CoxRaid *raid = ctx;
    if (!raid) return;
    free(raid->olm);
    free(raid);
}

int rc_content_chambers_of_xeric_register(RcWorld *world) {
    if (!world) return 0;
    CoxRaid *raid = calloc(1, sizeof(*raid));
    if (!raid) return 0;
    raid->current_room = COX_NO_ROOM;
    if (!rc_world_add_content_tick(world, cox_tick, raid, release_raid)) {
        free(raid);
        return 0;
    }
    // Wrap the regular combat hooks: raid damage rules and point tracking.
    RcCombatContentHooks hooks = world->combat_hooks;
    raid->prev_on_player_hit_npc = hooks.on_player_hit_npc;
    raid->prev_on_npc_hit_player = hooks.on_npc_hit_player;
    hooks.on_player_hit_npc = on_player_hit_npc;
    hooks.on_npc_hit_player = on_npc_hit_player;
    hooks.modify_player_hit = cox_modify_player_hit;
    rc_combat_register_content_hooks(world, &hooks);
    rc_set_npc_loot_hook(world, raid_loot, NULL);
    int ok = rc_event_subscribe(world, RC_EVT_PLAYER_DIED, on_player_died, NULL) >= 0
        && rc_event_subscribe(world, RC_EVT_NPC_DIED, on_npc_died, NULL) >= 0;
    ok &= cox_register_object(world, LOC_ENTRANCE, 0, enter_chambers);
    ok &= cox_register_object(world, LOC_EXIT_STEPS, 0, exit_steps);
    ok &= cox_register_object(world, LOC_LOBBY_STEPS_LEADER, 0, exit_steps);
    ok &= cox_register_object(world, LOC_PASSAGE, 0, passage);
    ok &= cox_register_object(world, LOC_HOLE_OLM, 0, hole);
    ok &= cox_register_object(world, LOC_ROPE_OLM, 0, rope);
    ok &= cox_register_object(world, LOC_ENERGY_WELL, 0, energy_well);
    static const int rimor_ops[3] = {0, 2, 3};
    for (int i = 0; i < 3; i++)
        ok &= cox_register_npc(world, NPC_RIMOR, rimor_ops[i], rimor);
    ok &= cox_combat_rooms_register(world);
    ok &= cox_puzzle_rooms_register(world);
    ok &= cox_resources_register(world);
    ok &= cox_olm_register(world);
    return ok;
}
