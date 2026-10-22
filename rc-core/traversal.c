#include "traversal.h"
#include "api.h"
#include "config.h"
#include "coordinates.h"
#include "interaction.h"
#include "io.h"
#include "pathfinding.h"
#include "player_command.h"
#include "rng.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRAV_MAGIC 0x56415254u
#define TRAV_VERSION 1u
#define NO_TILE 0xFFFFu

RcTraversalEdge *g_rc_traversal_edges = NULL;
int g_rc_traversal_edge_count = 0;

static RcTraversalRange g_traversal_index[RC_TRAVERSAL_KIND_COUNT]
                                          [RC_TRAVERSAL_MAX_SOURCE_ID];
static const RcTraversalEdge *g_active_traversal_edges = NULL;
static const RcTraversalRange (*g_active_traversal_index)
    [RC_TRAVERSAL_MAX_SOURCE_ID] = g_traversal_index;
static int g_active_traversal_edge_count = 0;

static int read_str8(FILE *f, char *out, int cap,
                     const char *path, const char *what) {
    uint8_t len;
    if (!rc_read_exact(f, &len, sizeof(len), 1, path, what)) return 0;
    int keep = len < (uint8_t)(cap - 1) ? (int)len : cap - 1;
    if (keep && !rc_read_exact(f, out, 1, (size_t)keep, path, what)) return 0;
    out[keep] = '\0';
    if (len > (uint8_t)keep &&
            !rc_seek(f, (long)(len - (uint8_t)keep), SEEK_CUR, path, what)) {
        return 0;
    }
    return 1;
}

static int valid_key(int kind, uint32_t source_id) {
    return kind > 0 && kind < RC_TRAVERSAL_KIND_COUNT
        && source_id < RC_TRAVERSAL_MAX_SOURCE_ID;
}

static int valid_edge_coordinates(const RcTraversalEdge *edge) {
    if (!edge || !rc_world_tile_valid(edge->dest_x, edge->dest_y,
                                      edge->dest_plane)) {
        return 0;
    }
    int no_source = edge->start_x == NO_TILE && edge->start_y == NO_TILE
                 && edge->start_plane == UINT8_MAX;
    return no_source || rc_world_tile_valid(edge->start_x, edge->start_y,
                                            edge->start_plane);
}

void rc_traversal_data_init(RcTraversalData *data) {
    if (!data) return;
    data->edges = NULL;
    data->edge_count = 0;
    memset(data->index, 0xFF, sizeof(data->index));
    for (int k = 0; k < RC_TRAVERSAL_KIND_COUNT; k++) {
        for (int i = 0; i < RC_TRAVERSAL_MAX_SOURCE_ID; i++) {
            data->index[k][i].count = 0;
        }
    }
}

void rc_traversal_data_free(RcTraversalData *data) {
    if (!data) return;
    free(data->edges);
    rc_traversal_data_init(data);
}

void rc_traversal_use_data(const RcTraversalData *data) {
    if (!data) {
        g_active_traversal_edges = g_rc_traversal_edges;
        g_active_traversal_edge_count = g_rc_traversal_edge_count;
        g_active_traversal_index = g_traversal_index;
        return;
    }
    g_active_traversal_edges = data->edges;
    g_active_traversal_edge_count = data->edge_count;
    g_active_traversal_index = data->index;
}

void rc_traversal_reset_data_if_active(const RcTraversalData *data) {
    if (data && g_active_traversal_edges == data->edges) {
        rc_traversal_use_data(NULL);
    }
}

int rc_load_traversal_edges_into(const char *path, RcTraversalData *data) {
    if (!path || !data) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) return -1;

    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, path, "version")
            || !rc_read_exact(f, &count, sizeof(count), 1, path, "count")
            || magic != TRAV_MAGIC || version != TRAV_VERSION) {
        rc_asset_close(f);
        return -1;
    }

    RcTraversalEdge *rows = calloc(count ? count : 1u, sizeof(*rows));
    if (!rows) {
        rc_asset_close(f);
        return -1;
    }
    memset(data->index, 0xFF, sizeof(data->index));
    for (int k = 0; k < RC_TRAVERSAL_KIND_COUNT; k++)
        for (int j = 0; j < RC_TRAVERSAL_MAX_SOURCE_ID; j++)
            data->index[k][j].count = 0;

    for (uint32_t i = 0; i < count; i++) {
        RcTraversalEdge *row = &rows[i];
        uint16_t planes;
        uint8_t pad[4];
        if (!rc_read_exact(f, &row->kind, sizeof(row->kind), 1, path, "kind")
                || !rc_read_exact(f, &row->option, sizeof(row->option), 1,
                                  path, "option")
                || !rc_read_exact(f, &planes, sizeof(planes), 1, path, "planes")
                || !rc_read_exact(f, &row->source_id, sizeof(row->source_id),
                                  1, path, "source id")
                || !rc_read_exact(f, &row->flags, sizeof(row->flags), 1,
                                  path, "flags")
                || !rc_read_exact(f, &row->start_x, sizeof(row->start_x), 1,
                                  path, "start x")
                || !rc_read_exact(f, &row->start_y, sizeof(row->start_y), 1,
                                  path, "start y")
                || !rc_read_exact(f, &row->dest_x, sizeof(row->dest_x), 1,
                                  path, "dest x")
                || !rc_read_exact(f, &row->dest_y, sizeof(row->dest_y), 1,
                                  path, "dest y")
                || !rc_read_exact(f, pad, sizeof(pad), 1, path, "pad")
                || !read_str8(f, row->action, sizeof(row->action), path,
                              "action")
                || !read_str8(f, row->target, sizeof(row->target), path,
                              "target")) {
            free(rows);
            rc_asset_close(f);
            return -1;
        }
        row->start_plane = (uint8_t)((planes >> 8) & 0xFF);
        row->dest_plane = (uint8_t)(planes & 0xFF);
        if (row->start_x == NO_TILE && row->start_y == NO_TILE
                && row->start_plane == UINT8_MAX) {
            row->source_semantics = RC_TRAVERSAL_SOURCE_NONE;
        } else if (row->flags & 1u) {
            row->source_semantics = RC_TRAVERSAL_SOURCE_INSTANCE_TEMPLATE;
        } else {
            row->source_semantics = RC_TRAVERSAL_SOURCE_PLAYER_TILE;
        }
        if (!valid_key(row->kind, row->source_id)
                || !valid_edge_coordinates(row)) {
            free(rows);
            rc_asset_close(f);
            return -1;
        }
        RcTraversalRange *idx = &data->index[row->kind][row->source_id];
        if (idx->first == UINT32_MAX) idx->first = i;
        idx->count++;
    }

    rc_asset_close(f);
    free(data->edges);
    data->edges = rows;
    data->edge_count = (int)count;
    return data->edge_count;
}

int rc_traversal_mirror_to_globals(const RcTraversalData *data) {
    if (!data) return 0;
    if (data->edge_count == g_rc_traversal_edge_count
            && data->edge_count > 0 && data->edges && g_rc_traversal_edges
            && memcmp(g_rc_traversal_edges, data->edges,
                      (size_t)data->edge_count * sizeof(*data->edges)) == 0) {
        memcpy(g_traversal_index, data->index, sizeof(g_traversal_index));
        return 1;
    }
    RcTraversalEdge *rows = NULL;
    if (data->edge_count > 0) {
        if (!data->edges) return 0;
        rows = malloc((size_t)data->edge_count * sizeof(*rows));
        if (!rows) return 0;
        memcpy(rows, data->edges, (size_t)data->edge_count * sizeof(*rows));
    }
    free(g_rc_traversal_edges);
    g_rc_traversal_edges = rows;
    g_rc_traversal_edge_count = data->edge_count;
    memcpy(g_traversal_index, data->index, sizeof(g_traversal_index));
    return 1;
}

int rc_load_traversal_edges(const char *path) {
    RcTraversalData *data = calloc(1, sizeof(*data));
    if (!data) return -1;
    rc_traversal_data_init(data);
    int loaded = rc_load_traversal_edges_into(path, data);
    if (loaded >= 0 && !rc_traversal_mirror_to_globals(data)) loaded = -1;
    rc_traversal_data_free(data);
    free(data);
    if (loaded >= 0) rc_traversal_use_data(NULL);
    return loaded;
}

const RcTraversalEdge *rc_traversal_edges_for(int kind, int source_id,
                                              int *count) {
    const RcTraversalEdge *edges = g_active_traversal_edges
                                 ? g_active_traversal_edges
                                 : g_rc_traversal_edges;
    const RcTraversalRange (*index)[RC_TRAVERSAL_MAX_SOURCE_ID] =
        g_active_traversal_index ? g_active_traversal_index
                                 : g_traversal_index;
    if (source_id < 0 || !valid_key(kind, (uint32_t)source_id)
            || !edges) {
        if (count) *count = 0;
        return NULL;
    }
    RcTraversalRange idx = index[kind][source_id];
    if (idx.first == UINT32_MAX) {
        if (count) *count = 0;
        return NULL;
    }
    if (count) *count = (int)idx.count;
    return &edges[idx.first];
}

const RcTraversalEdge *rc_traversal_edges_all(int *count) {
    const RcTraversalEdge *edges = g_active_traversal_edges
                                 ? g_active_traversal_edges
                                 : g_rc_traversal_edges;
    int edge_count = g_active_traversal_edges ? g_active_traversal_edge_count
                                              : g_rc_traversal_edge_count;
    if (count) *count = edge_count;
    return edge_count > 0 ? edges : NULL;
}

const RcTraversalEdge *rc_traversal_get(int idx) {
    int count = 0;
    const RcTraversalEdge *edges = rc_traversal_edges_all(&count);
    return edges && idx >= 0 && idx < count ? &edges[idx] : NULL;
}

int rc_traversal_index_of(const RcTraversalEdge *edge) {
    int count = 0;
    const RcTraversalEdge *edges = rc_traversal_edges_all(&count);
    if (!edge || !edges || edge < edges || edge >= edges + count) return -1;
    return (int)(edge - edges);
}

const RcTraversalEdge *rc_traversal_find(int kind, int source_id,
                                         int x, int y, int plane, int option) {
    if ((x < -1 || x >= RC_WORLD_SIZE)
            || (y < -1 || y >= RC_WORLD_SIZE)
            || (plane < -1 || plane >= RC_MAX_PLANES)
            || (option < -1 || option > UINT8_MAX)) {
        return NULL;
    }
    int count = 0;
    const RcTraversalEdge *rows = rc_traversal_edges_for(kind, source_id, &count);
    for (int i = 0; rows && i < count; i++) {
        const RcTraversalEdge *row = &rows[i];
        if (row->kind != (uint8_t)kind || row->source_id != (uint32_t)source_id)
            continue;
        if (option >= 0 && row->option != (uint8_t)option) continue;
        if (x >= 0 && row->start_x != x) continue;
        if (y >= 0 && row->start_y != y) continue;
        if (plane >= 0 && row->start_plane != plane) continue;
        return row;
    }
    return NULL;
}

const RcTraversalEdge *rc_traversal_find_target(int kind, const char *target) {
    const RcTraversalEdge *edges = g_active_traversal_edges
                                 ? g_active_traversal_edges
                                 : g_rc_traversal_edges;
    int edge_count = g_active_traversal_edges ? g_active_traversal_edge_count
                                              : g_rc_traversal_edge_count;
    if (!target || !edges || kind <= 0 || kind >= RC_TRAVERSAL_KIND_COUNT)
        return NULL;
    for (int i = 0; i < edge_count; i++) {
        const RcTraversalEdge *row = &edges[i];
        if (row->kind == (uint8_t)kind && strcmp(row->target, target) == 0) {
            return row;
        }
    }
    return NULL;
}

static uint64_t next_nonzero(uint64_t *value) {
    uint64_t next = ++*value;
    if (next == 0) next = ++*value;
    return next;
}

static void publish_event(RcPlayer *player) {
    if (!player || !player->traversal.active) return;
    const RcTraversalState *state = &player->traversal;
    player->traversal_event = (RcTraversalEvent){
        .sequence = next_nonzero(&player->next_traversal_event_sequence),
        .generation = state->generation,
        .interaction_generation = state->interaction_generation,
        .phase = state->phase,
        .presentation = state->presentation,
        .source_kind = state->source_kind,
        .source_id = state->source_id,
        .source_option = state->source_option,
        .source_key = state->source_key,
        .source_x = state->source_x,
        .source_y = state->source_y,
        .source_plane = state->source_plane,
        .approach_x = state->approach_x,
        .approach_y = state->approach_y,
        .approach_plane = state->approach_plane,
        .destination_x = state->destination_x,
        .destination_y = state->destination_y,
        .destination_plane = state->destination_plane,
        .facing_x = state->facing_x,
        .facing_y = state->facing_y,
        .has_facing = state->has_facing,
        .takeoff_ticks = state->takeoff_ticks,
        .transit_ticks = state->transit_ticks,
        .landing_ticks = state->landing_ticks,
    };
}

static const char *outcome_message(RcTraversalResultCode code) {
    switch (code) {
    case RC_TRAVERSAL_RESULT_CANCELLED: return "Travel cancelled.";
    case RC_TRAVERSAL_RESULT_INVALID_SOURCE:
        return "That travel source is no longer available.";
    case RC_TRAVERSAL_RESULT_INVALID_DESTINATION:
        return "That travel destination is invalid.";
    case RC_TRAVERSAL_RESULT_DESTINATION_UNAVAILABLE:
        return "That travel destination is not available.";
    case RC_TRAVERSAL_RESULT_UNSUPPORTED:
        return "That travel method is not supported yet.";
    case RC_TRAVERSAL_RESULT_SUCCESS:
    case RC_TRAVERSAL_RESULT_NONE:
    default:
        return "";
    }
}

static void publish_outcome(RcPlayer *player, const RcTraversalState *state,
                            RcTraversalResultCode code,
                            RcPlayerActionCancelReason reason) {
    if (!player || !state || state->generation == 0) return;
    RcTraversalOutcome outcome = {
        .sequence = next_nonzero(&player->next_traversal_outcome_sequence),
        .generation = state->generation,
        .code = code,
        .cancel_reason = reason,
        .source_kind = state->source_kind,
        .source_id = state->source_id,
        .destination_x = state->destination_x,
        .destination_y = state->destination_y,
        .destination_plane = state->destination_plane,
    };
    snprintf(outcome.message, sizeof(outcome.message), "%s",
             outcome_message(code));
    player->traversal_outcome = outcome;
}

void rc_traversal_clear(RcPlayer *player) {
    if (!player) return;
    memset(&player->traversal, 0, sizeof(player->traversal));
    player->traversal.source_x = -1;
    player->traversal.source_y = -1;
    player->traversal.source_plane = -1;
    player->traversal.approach_x = -1;
    player->traversal.approach_y = -1;
    player->traversal.approach_plane = -1;
    player->traversal.destination_x = -1;
    player->traversal.destination_y = -1;
    player->traversal.destination_plane = -1;
    player->traversal.facing_x = -1;
    player->traversal.facing_y = -1;
}

int rc_traversal_resolve_destination(
    RcWorld *world, const RcTraversalDestinationSpec *spec,
    const RcTraversalDestination *source, RcTraversalDestination *out) {
    if (!spec || !source || !out
            || !rc_world_tile_valid(source->x, source->y, source->plane)) {
        return 0;
    }
    RcTraversalDestination resolved = {-1, -1, -1};
    switch (spec->policy) {
    case RC_TRAVERSAL_POLICY_EXACT:
        resolved = spec->destination;
        break;
    case RC_TRAVERSAL_POLICY_RELATIVE:
        resolved.x = source->x + spec->delta_x;
        resolved.y = source->y + spec->delta_y;
        resolved.plane = source->plane + spec->delta_plane;
        break;
    case RC_TRAVERSAL_POLICY_RANDOM_AREA: {
        if (!world || spec->area_min_x > spec->area_max_x
                || spec->area_min_y > spec->area_max_y) {
            return 0;
        }
        resolved.x = spec->area_min_x + rc_rng_range(
            &world->rng_state, spec->area_max_x - spec->area_min_x);
        resolved.y = spec->area_min_y + rc_rng_range(
            &world->rng_state, spec->area_max_y - spec->area_min_y);
        resolved.plane = spec->area_plane;
        break;
    }
    case RC_TRAVERSAL_POLICY_CHOICE:
        if (!spec->choices || spec->choice_count <= 0
                || spec->choice_index < 0
                || spec->choice_index >= spec->choice_count) {
            return 0;
        }
        resolved = spec->choices[spec->choice_index];
        break;
    case RC_TRAVERSAL_POLICY_CONTEXTUAL_RETURN:
        if (!spec->context_available) return 0;
        resolved = spec->destination;
        break;
    case RC_TRAVERSAL_POLICY_INSTANCE_MAPPED:
        if (!spec->instance_mapping_available) return 0;
        resolved = spec->destination;
        break;
    default:
        return 0;
    }
    if (!rc_world_tile_valid(resolved.x, resolved.y, resolved.plane))
        return 0;
    *out = resolved;
    return 1;
}

int rc_traversal_plan_object(
    RcWorld *world, const RcTraversalEdge *edge, uint64_t source_key,
    uint64_t interaction_generation,
    const RcTraversalDestinationSpec *destination,
    const RcTraversalMotionSpec *motion) {
    if (!world || !edge || !(world->enabled & RC_SUB_TRAVERSAL)
            || edge->kind != RC_TRAVERSAL_OBJECT || edge->flags != 0
            || edge->source_semantics != RC_TRAVERSAL_SOURCE_PLAYER_TILE
            || !valid_edge_coordinates(edge)
            || !rc_world_tile_valid(edge->start_x, edge->start_y,
                                    edge->start_plane)
            || interaction_generation == 0
            || !destination || !motion || motion->takeoff_ticks < 0
            || motion->transit_ticks < 0 || motion->landing_ticks < 0) {
        return 0;
    }
    const RcPendingInteraction *interaction = &world->player.interaction;
    int option = (int)interaction->op - (int)RC_INTERACTION_OP1;
    if (!interaction->active
            || interaction->generation != interaction_generation
            || interaction->target.kind != RC_INTERACTION_OBJECT
            || interaction->target.definition_id != (int)edge->source_id
            || option != (int)edge->option
            || (source_key != 0
                && interaction->target.placement_key != source_key)) {
        return 0;
    }
    if (motion->presentation < RC_TRAVERSAL_PRESENTATION_INSTANT
            || motion->presentation > RC_TRAVERSAL_PRESENTATION_EXACT_MOVE
            || (motion->presentation == RC_TRAVERSAL_PRESENTATION_INSTANT
                && (motion->takeoff_ticks != 0 || motion->transit_ticks != 0
                    || motion->landing_ticks != 0))
            || (motion->presentation == RC_TRAVERSAL_PRESENTATION_EXACT_MOVE
                && motion->transit_ticks <= 0)
            || (motion->presentation != RC_TRAVERSAL_PRESENTATION_EXACT_MOVE
                && motion->transit_ticks != 0)
            || (motion->has_facing
                && (!rc_world_coord_valid(motion->facing_x)
                    || !rc_world_coord_valid(motion->facing_y)))) {
        return 0;
    }
    RcTraversalDestination source = {
        .x = edge->start_x,
        .y = edge->start_y,
        .plane = edge->start_plane,
    };
    RcTraversalDestination resolved;
    if (!rc_traversal_resolve_destination(
            world, destination, &source, &resolved)) {
        return 0;
    }
    rc_traversal_cancel(world, RC_ACTION_CANCEL_REPLACED);
    RcPlayer *player = &world->player;
    player->traversal = (RcTraversalState){
        .active = true,
        .generation = next_nonzero(&player->next_traversal_generation),
        .interaction_generation = interaction_generation,
        .source_kind = edge->kind,
        .source_id = (int)edge->source_id,
        .source_option = edge->option,
        .source_flags = edge->flags,
        .source_key = source_key,
        .source_x = interaction->target.tile_x,
        .source_y = interaction->target.tile_y,
        .source_plane = interaction->target.plane,
        .approach_x = edge->start_x,
        .approach_y = edge->start_y,
        .approach_plane = edge->start_plane,
        .policy = destination->policy,
        .presentation = motion->presentation,
        .phase = RC_TRAVERSAL_PHASE_APPROACH,
        .destination_x = resolved.x,
        .destination_y = resolved.y,
        .destination_plane = resolved.plane,
        .facing_x = motion->facing_x,
        .facing_y = motion->facing_y,
        .has_facing = motion->has_facing,
        .ready_tick = world->tick,
        .takeoff_ticks = motion->takeoff_ticks,
        .transit_ticks = motion->transit_ticks,
        .landing_ticks = motion->landing_ticks,
    };
    publish_event(player);
    return 1;
}

int rc_traversal_start(RcWorld *world, uint64_t interaction_generation) {
    if (!world) return 0;
    RcTraversalState *state = &world->player.traversal;
    if (!state->active || state->phase != RC_TRAVERSAL_PHASE_APPROACH
            || state->interaction_generation != interaction_generation) {
        return 0;
    }
    state->phase = RC_TRAVERSAL_PHASE_TAKEOFF;
    state->ready_tick = world->tick + (RcTick)state->takeoff_ticks;
    rc_player_route_clear(&world->player, RC_MOVEMENT_NONE);
    if (state->has_facing) {
        world->player.facing_entity = -1;
        world->player.facing_x = state->facing_x;
        world->player.facing_y = state->facing_y;
        world->player.facing_width = 1;
        world->player.facing_length = 1;
    }
    world->player_action = (RcPlayerActionState){
        .active = true,
        .owner = RC_ACTION_OWNER_TRAVERSAL,
        .category = RC_ACTION_CATEGORY_STRONG,
        .started_tick = world->tick,
        .ready_tick = state->ready_tick
                    + (RcTick)state->transit_ticks
                    + (RcTick)state->landing_ticks + 1,
    };
    publish_event(&world->player);
    return 1;
}

void rc_traversal_cancel(RcWorld *world,
                         RcPlayerActionCancelReason reason) {
    if (!world || !world->player.traversal.active) return;
    RcTraversalState state = world->player.traversal;
    rc_traversal_clear(&world->player);
    if (world->player_action.active
            && world->player_action.owner == RC_ACTION_OWNER_TRAVERSAL) {
        world->player_action.active = false;
        world->player_action.owner = RC_ACTION_OWNER_NONE;
        world->player_action.category = RC_ACTION_CATEGORY_SOFT;
        world->player_action.ready_tick = world->tick;
        world->player_action.last_cancel_reason = reason;
    }
    publish_outcome(&world->player, &state, RC_TRAVERSAL_RESULT_CANCELLED,
                    reason);
}

void rc_traversal_tick(RcWorld *world) {
    if (!world || !world->player.traversal.active) return;
    RcPlayer *player = &world->player;
    RcTraversalState *state = &player->traversal;
    if (state->phase == RC_TRAVERSAL_PHASE_APPROACH) {
        if (!player->interaction.active
                || player->interaction.generation
                    != state->interaction_generation) {
            rc_traversal_cancel(world, RC_ACTION_CANCEL_REPLACED);
        }
        return;
    }
    if (world->tick < state->ready_tick) return;
    if (state->phase == RC_TRAVERSAL_PHASE_LANDING) {
        RcTraversalState finished = *state;
        rc_traversal_clear(player);
        publish_outcome(player, &finished, RC_TRAVERSAL_RESULT_SUCCESS,
                        RC_ACTION_CANCEL_NONE);
        return;
    }
    if (state->phase == RC_TRAVERSAL_PHASE_TAKEOFF
            && state->presentation == RC_TRAVERSAL_PRESENTATION_EXACT_MOVE) {
        state->phase = RC_TRAVERSAL_PHASE_TRANSIT;
        state->ready_tick = world->tick + (RcTick)state->transit_ticks;
        world->player_action.ready_tick = state->ready_tick
                                        + (RcTick)state->landing_ticks + 1;
        publish_event(player);
        return;
    }
    if (state->phase != RC_TRAVERSAL_PHASE_TAKEOFF
            && state->phase != RC_TRAVERSAL_PHASE_TRANSIT) {
        return;
    }

    state->phase = RC_TRAVERSAL_PHASE_RELOCATE;
    publish_event(player);
    RcTraversalState admitted = *state;
    rc_traversal_clear(player);
    if (!rc_world_relocate_player(world, admitted.destination_x,
                                  admitted.destination_y,
                                  admitted.destination_plane)) {
        publish_outcome(player, &admitted,
                        RC_TRAVERSAL_RESULT_DESTINATION_UNAVAILABLE,
                        RC_ACTION_CANCEL_NONE);
        rc_player_action_refresh(world);
        return;
    }
    if (admitted.landing_ticks > 0) {
        admitted.active = true;
        admitted.phase = RC_TRAVERSAL_PHASE_LANDING;
        admitted.ready_tick = world->tick + (RcTick)admitted.landing_ticks;
        player->traversal = admitted;
        world->player_action = (RcPlayerActionState){
            .active = true,
            .owner = RC_ACTION_OWNER_TRAVERSAL,
            .category = RC_ACTION_CATEGORY_STRONG,
            .started_tick = world->tick,
            .ready_tick = admitted.ready_tick + 1,
        };
        publish_event(player);
        return;
    }
    publish_outcome(player, &admitted, RC_TRAVERSAL_RESULT_SUCCESS,
                    RC_ACTION_CANCEL_NONE);
}

const RcTraversalState *rc_traversal_state(const RcPlayer *player) {
    return player ? &player->traversal : NULL;
}

const RcTraversalEvent *rc_traversal_last_event(const RcPlayer *player) {
    return player ? &player->traversal_event : NULL;
}

const RcTraversalOutcome *rc_traversal_last_outcome(const RcPlayer *player) {
    return player ? &player->traversal_outcome : NULL;
}
