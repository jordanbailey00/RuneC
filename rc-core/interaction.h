#ifndef RC_INTERACTION_H
#define RC_INTERACTION_H

#include "types.h"

RcInteractionOp rc_interaction_op_from_option(int option_idx);
int  rc_interaction_kind_valid(RcInteractionKind kind);
int  rc_interaction_op_valid(RcInteractionOp op);
int  rc_interaction_target_valid(const RcInteractionTarget *target);
RcInteractionDispatchKey rc_interaction_dispatch_key_any(void);
RcInteractionDispatchKey rc_interaction_dispatch_key_from_pending(
    const RcPendingInteraction *interaction);
RcInteractionHandlerResult rc_interaction_result_complete(void);
RcInteractionHandlerResult rc_interaction_result_cancel(
    RcInteractionFailure reason);
RcInteractionHandlerResult rc_interaction_result_continue_approach(
    int approach_range);
RcInteractionHandlerResult rc_interaction_result_combat_handoff(
    int target_uid);
RcInteractionHandlerResult rc_interaction_result_system_handoff(
    int system_handoff, int target_id);
RcInteractionHandlerResult rc_interaction_result_message(const char *message);
RcInteractionHandlerResult rc_interaction_result_failure(
    RcInteractionFailure reason, const char *message);
int  rc_interaction_begin(RcPlayer *player, int source_actor_uid,
                          RcInteractionOp op, const char *option_text,
                          const RcInteractionTarget *target,
                          int approach_range);
int  rc_interaction_begin_with_source(
    RcPlayer *player, int source_actor_uid, RcInteractionOp op,
    const char *option_text, const RcInteractionTarget *target,
    int approach_range, int source_item_id, int source_spell_id,
    int source_widget_id, int source_component_id);
void rc_interaction_cancel(RcPlayer *player, RcInteractionFailure reason);
void rc_interaction_clear(RcPlayer *player);
int  rc_interaction_is_active(const RcPlayer *player);
const RcPendingInteraction *rc_interaction_get(const RcPlayer *player);
RcInteractionKind rc_interaction_target_kind(const RcPlayer *player);
void rc_interaction_clear_world_handlers(RcWorld *world);
int  rc_interaction_register_world_handler(
    RcWorld *world, const RcInteractionDispatchKey *key,
    RcInteractionHandlerFn fn, void *ctx);
int  rc_interaction_world_handler_count(const RcWorld *world);
int  rc_interaction_find_world_handler(
    const RcWorld *world, const RcInteractionDispatchKey *key);
RcInteractionHandlerResult rc_interaction_dispatch(RcWorld *world,
                                                   RcPlayer *player);

#endif
