#include "../content.h"
#include "../../rc-core/interaction.h"
#include "../../rc-core/traversal.h"
#include "../../rc-core/types.h"

#include <stdio.h>

// Raid entrances and exits. Entering moves the player into the raid's static
// map template, which serves as the single player's instance. The scraped
// traversal rows for these objects point at live instance coordinates, so
// rc-core rejects them and these handlers take over. Chambers of Xeric owns
// its own entry and exit (raids/cox_raid.c).
typedef struct {
    int obj_id;
    int option;     // 0-based object option; the first option on all of these
    int x, y, plane;
} RaidTransport;

static const RaidTransport g_raid_transports[] = {
    // Entrances.
    {32653, 0, 3220, 4458, 0}, /* Theatre of Blood -> entrance corridor */
    {46089, 0, 3551, 5153, 0}, /* Tombs of Amascut lobby Entry -> Nexus */
    {62768, 0, 4127, 5573, 0}, /* Fractured Archive door (b241) -> arrival room */
    // Exits back to the lobby or hub.
    {32996, 0, 3676, 3219, 0}, /* ToB treasure-room crystal -> Ver Sinhaza */
    {45128, 0, 3359, 9114, 0}, /* ToA Exit (Leave) -> lobby */
    {62765, 0, 4127, 5590, 0}, /* Archive boss-room Passageway -> central hall */
};

#define RAID_TRANSPORT_COUNT \
    ((int)(sizeof(g_raid_transports) / sizeof(g_raid_transports[0])))

static const RaidTransport *raid_transport_for(int obj_id, int option) {
    for (int i = 0; i < RAID_TRANSPORT_COUNT; i++) {
        if (g_raid_transports[i].obj_id == obj_id
                && g_raid_transports[i].option == option)
            return &g_raid_transports[i];
    }
    return NULL;
}

static RcInteractionHandlerResult use_raid_transport(
    struct RcWorld *world, RcPlayer *player,
    const RcPendingInteraction *pending, void *ctx) {
    (void)ctx;
    int option = (int)pending->op - (int)RC_INTERACTION_OP1;
    const RaidTransport *transport =
        raid_transport_for(pending->target.definition_id, option);
    if (!transport)
        return rc_interaction_result_failure(RC_INTERACTION_FAIL_NO_HANDLER,
                                             "Nothing interesting happens.");
    // The player has arrived beside the object; travel from that tile.
    RcTraversalEdge edge = {
        .kind = RC_TRAVERSAL_OBJECT,
        .option = (uint8_t)option,
        .source_semantics = RC_TRAVERSAL_SOURCE_PLAYER_TILE,
        .start_plane = (uint8_t)player->plane,
        .dest_plane = (uint8_t)transport->plane,
        .source_id = (uint32_t)transport->obj_id,
        .start_x = (uint16_t)player->x,
        .start_y = (uint16_t)player->y,
        .dest_x = (uint16_t)transport->x,
        .dest_y = (uint16_t)transport->y,
    };
    RcTraversalDestinationSpec destination = {
        .policy = RC_TRAVERSAL_POLICY_EXACT,
        .destination = {transport->x, transport->y, transport->plane},
    };
    RcTraversalMotionSpec motion = {
        .presentation = RC_TRAVERSAL_PRESENTATION_INSTANT,
    };
    uint64_t generation = player->interaction.generation;
    if (!rc_traversal_plan_object(world, &edge, pending->target.placement_key,
                                  generation, &destination, &motion)
            || !rc_traversal_start(world, generation)) {
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_INVALID_SOURCE, "You can't go that way.");
    }
    return rc_interaction_result_complete();
}

int rc_content_raid_transports_register(struct RcWorld *world) {
    for (int i = 0; i < RAID_TRANSPORT_COUNT; i++) {
        RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
        key.kind = RC_INTERACTION_OBJECT;
        key.op = rc_interaction_op_from_option(g_raid_transports[i].option);
        key.definition_id = g_raid_transports[i].obj_id;
        if (!rc_interaction_register_world_handler(world, &key,
                                                   use_raid_transport, NULL)) {
            fprintf(stderr, "raid transports: could not register %d\n",
                    g_raid_transports[i].obj_id);
            return 0;
        }
    }
    return 1;
}
