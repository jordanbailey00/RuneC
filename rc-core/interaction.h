#ifndef RC_INTERACTION_H
#define RC_INTERACTION_H

#include "types.h"

#define RC_INTERACTION_KEY_ANY -1
#define RC_INTERACTION_CONTENT_GROUP_NPC_ATTACK 1
#define RC_INTERACTION_CONTENT_GROUP_DIALOGUE 2
#define RC_INTERACTION_CONTENT_GROUP_SHOP 3
#define RC_INTERACTION_CONTENT_GROUP_BANK 4
#define RC_INTERACTION_CONTENT_GROUP_SKILLING 5
#define RC_INTERACTION_RESULT_MESSAGE_LEN 96
#define RC_MAX_INTERACTION_HANDLERS 256

typedef enum {
    RC_INTERACTION_SYSTEM_NONE = 0,
    RC_INTERACTION_SYSTEM_DIALOGUE,
    RC_INTERACTION_SYSTEM_SHOP,
    RC_INTERACTION_SYSTEM_BANK,
    RC_INTERACTION_SYSTEM_SKILLING,
} RcInteractionSystemHandoff;

typedef enum {
    RC_INTERACTION_HANDLER_NONE = 0,
    RC_INTERACTION_HANDLER_COMPLETE,
    RC_INTERACTION_HANDLER_CANCEL,
    RC_INTERACTION_HANDLER_CONTINUE_APPROACH,
    RC_INTERACTION_HANDLER_COMBAT_HANDOFF,
    RC_INTERACTION_HANDLER_MESSAGE,
    RC_INTERACTION_HANDLER_FAILURE,
} RcInteractionHandlerCode;

typedef struct {
    RcInteractionKind kind;
    RcInteractionOp op;
    int definition_id;
    int content_group;
    int source_item_id;
    int source_spell_id;
    int widget_id;
    int component_id;
} RcInteractionDispatchKey;

typedef struct {
    RcInteractionHandlerCode code;
    RcInteractionFailure failure;
    int approach_range;
    int combat_target_uid;
    int system_handoff;
    int system_target_id;
    char message[RC_INTERACTION_RESULT_MESSAGE_LEN];
} RcInteractionHandlerResult;

typedef RcInteractionHandlerResult (*RcInteractionHandlerFn)(
    RcWorld *world, RcPlayer *player, const RcPendingInteraction *interaction,
    void *ctx);

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
void rc_interaction_clear_handlers(void);
int  rc_interaction_register_handler(const RcInteractionDispatchKey *key,
                                     RcInteractionHandlerFn fn, void *ctx);
int  rc_interaction_handler_count(void);
int  rc_interaction_find_handler(const RcInteractionDispatchKey *key);
RcInteractionHandlerResult rc_interaction_dispatch(RcWorld *world,
                                                   RcPlayer *player);

#endif
