#include "../content.h"
#include "../../rc-core/config.h"
#include "../../rc-core/encounter.h"
#include "../../rc-core/interaction.h"
#include "../../rc-core/types.h"

// Vorkath. Forms, attacks, the special rotation and the acid and spawn
// phases are data (content/encounters/vorkath.toml); content owns the
// sleeping form's Poke option, which wakes the fight.
enum {
    VORKATH_SLEEPING = 8059,
    VORKATH_POKE_OPTION = 0,
};

static RcInteractionHandlerResult poke(RcWorld *world, RcPlayer *player,
                                       const RcPendingInteraction *pending,
                                       void *ctx) {
    (void)player;
    (void)ctx;
    if (!pending || pending->target.kind != RC_INTERACTION_NPC)
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_INVALID_TARGET, "Invalid NPC interaction");
    if (!rc_encounter_wake(world, (RcNpcId)pending->target.entity_uid))
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_TARGET_MISSING, "Vorkath is not asleep.");
    return rc_interaction_result_complete();
}

int rc_content_vorkath_register(struct RcWorld *world) {
    if (!world || !(world->enabled & RC_SUB_ENCOUNTER)) return 1;
    RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
    key.kind = RC_INTERACTION_NPC;
    key.op = rc_interaction_op_from_option(VORKATH_POKE_OPTION);
    key.definition_id = VORKATH_SLEEPING;
    return rc_interaction_register_world_handler(world, &key, poke, NULL) != 0;
}
