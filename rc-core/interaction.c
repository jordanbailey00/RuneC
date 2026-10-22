#include "interaction.h"

#include <string.h>

enum {
    RC_INTERACTION_MAX_APPROACH_RANGE = 64,
};

RcInteractionOp rc_interaction_op_from_option(int option_idx) {
    switch (option_idx) {
    case 0: return RC_INTERACTION_OP1;
    case 1: return RC_INTERACTION_OP2;
    case 2: return RC_INTERACTION_OP3;
    case 3: return RC_INTERACTION_OP4;
    case 4: return RC_INTERACTION_OP5;
    default: return RC_INTERACTION_OP_NONE;
    }
}

int rc_interaction_kind_valid(RcInteractionKind kind) {
    switch (kind) {
    case RC_INTERACTION_NPC:
    case RC_INTERACTION_OBJECT:
    case RC_INTERACTION_GROUND_ITEM:
    case RC_INTERACTION_INVENTORY_ITEM:
    case RC_INTERACTION_EQUIPMENT_ITEM:
    case RC_INTERACTION_WIDGET:
        return 1;
    default:
        return 0;
    }
}

int rc_interaction_op_valid(RcInteractionOp op) {
    return op >= RC_INTERACTION_OP1 && op <= RC_INTERACTION_WIDGET_ACTION;
}

static int valid_footprint(const RcInteractionTarget *target) {
    RcTileBounds bounds;
    return target && rc_tile_bounds_from_origin_size(
        target->tile_x, target->tile_y,
        target->footprint_width, target->footprint_height,
        target->plane, &bounds);
}

static int valid_tile_target(const RcInteractionTarget *target) {
    return valid_footprint(target);
}

static int clamp_face_size(int size) {
    return size < 1 ? 1 : size > UINT8_MAX ? UINT8_MAX : size;
}

int rc_interaction_target_valid(const RcInteractionTarget *target) {
    if (!target || !rc_interaction_kind_valid(target->kind)) return 0;
    switch (target->kind) {
    case RC_INTERACTION_NPC:
        return target->entity_uid >= 0 && target->definition_id >= 0
            && valid_tile_target(target);
    case RC_INTERACTION_OBJECT:
        return target->definition_id >= 0 && valid_tile_target(target);
    case RC_INTERACTION_GROUND_ITEM:
        return target->definition_id >= 0
            && target->ground_item_instance >= 0
            && valid_tile_target(target);
    case RC_INTERACTION_INVENTORY_ITEM:
        return target->definition_id >= 0
            && target->inventory_slot >= 0
            && target->inventory_slot < RC_INVENTORY_SIZE;
    case RC_INTERACTION_EQUIPMENT_ITEM:
        return target->definition_id >= 0
            && target->equipment_slot >= 0
            && target->equipment_slot < RC_EQUIP_COUNT;
    case RC_INTERACTION_WIDGET:
        return target->widget_id >= 0 || target->component_id >= 0;
    default:
        return 0;
    }
}

RcInteractionDispatchKey rc_interaction_dispatch_key_any(void) {
    RcInteractionDispatchKey key;
    key.kind = RC_INTERACTION_NONE;
    key.op = RC_INTERACTION_OP_NONE;
    key.definition_id = RC_INTERACTION_KEY_ANY;
    key.content_group = RC_INTERACTION_KEY_ANY;
    key.source_item_id = RC_INTERACTION_KEY_ANY;
    key.source_spell_id = RC_INTERACTION_KEY_ANY;
    key.widget_id = RC_INTERACTION_KEY_ANY;
    key.component_id = RC_INTERACTION_KEY_ANY;
    return key;
}

RcInteractionDispatchKey rc_interaction_dispatch_key_from_pending(
    const RcPendingInteraction *interaction) {
    RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
    if (!interaction) return key;
    key.kind = interaction->target.kind;
    key.op = interaction->op;
    key.definition_id = interaction->target.definition_id;
    key.content_group = interaction->target.content_group;
    key.source_item_id = interaction->source_item_id;
    key.source_spell_id = interaction->source_spell_id;
    if (interaction->target.kind == RC_INTERACTION_WIDGET) {
        key.widget_id = interaction->target.widget_id;
        key.component_id = interaction->target.component_id;
    } else {
        key.widget_id = interaction->source_widget_id;
        key.component_id = interaction->source_component_id;
    }
    return key;
}

static void result_copy_message(RcInteractionHandlerResult *result,
                                const char *message) {
    result->message[0] = '\0';
    if (!message) return;
    strncpy(result->message, message, RC_INTERACTION_RESULT_MESSAGE_LEN - 1);
    result->message[RC_INTERACTION_RESULT_MESSAGE_LEN - 1] = '\0';
}

RcInteractionHandlerResult rc_interaction_result_complete(void) {
    RcInteractionHandlerResult result;
    memset(&result, 0, sizeof(result));
    result.code = RC_INTERACTION_HANDLER_COMPLETE;
    return result;
}

RcInteractionHandlerResult rc_interaction_result_cancel(
    RcInteractionFailure reason) {
    RcInteractionHandlerResult result;
    memset(&result, 0, sizeof(result));
    result.code = RC_INTERACTION_HANDLER_CANCEL;
    result.failure = reason;
    return result;
}

RcInteractionHandlerResult rc_interaction_result_continue_approach(
    int approach_range) {
    RcInteractionHandlerResult result;
    memset(&result, 0, sizeof(result));
    result.code = RC_INTERACTION_HANDLER_CONTINUE_APPROACH;
    result.approach_range = approach_range;
    return result;
}

RcInteractionHandlerResult rc_interaction_result_message(const char *message) {
    RcInteractionHandlerResult result;
    memset(&result, 0, sizeof(result));
    result.code = RC_INTERACTION_HANDLER_MESSAGE;
    result_copy_message(&result, message);
    return result;
}

RcInteractionHandlerResult rc_interaction_result_failure(
    RcInteractionFailure reason, const char *message) {
    RcInteractionHandlerResult result;
    memset(&result, 0, sizeof(result));
    result.code = RC_INTERACTION_HANDLER_FAILURE;
    result.failure = reason;
    result_copy_message(&result, message);
    return result;
}

RcInteractionHandlerResult rc_interaction_result_handoff(void) {
    RcInteractionHandlerResult result;
    memset(&result, 0, sizeof(result));
    result.code = RC_INTERACTION_HANDLER_HANDOFF;
    return result;
}

static void copy_option_text(char dst[RC_INTERACTION_OPTION_TEXT_LEN],
                             const char *src) {
    dst[0] = '\0';
    if (!src) return;
    strncpy(dst, src, RC_INTERACTION_OPTION_TEXT_LEN - 1);
    dst[RC_INTERACTION_OPTION_TEXT_LEN - 1] = '\0';
}

static const char *failure_message(RcInteractionFailure reason) {
    switch (reason) {
    case RC_INTERACTION_FAIL_TARGET_MISSING: return "That target is no longer there.";
    case RC_INTERACTION_FAIL_TARGET_VERSION_CHANGED: return "That target has changed.";
    case RC_INTERACTION_FAIL_TARGET_DEAD: return "That target is no longer available.";
    case RC_INTERACTION_FAIL_OPTION_UNAVAILABLE: return "That option is not available.";
    case RC_INTERACTION_FAIL_NO_HANDLER: return "Nothing interesting happens.";
    case RC_INTERACTION_FAIL_CANNOT_REACH: return "I can't reach that.";
    case RC_INTERACTION_FAIL_LOS_BLOCKED: return "I can't see that.";
    case RC_INTERACTION_FAIL_ACTOR_BUSY: return "You are too busy to do that.";
    case RC_INTERACTION_FAIL_ACTOR_DEAD: return "You can't do that right now.";
    case RC_INTERACTION_FAIL_INVALID_SOURCE: return "That source is no longer available.";
    case RC_INTERACTION_FAIL_INVALID_TARGET: return "That target is not valid.";
    case RC_INTERACTION_FAIL_CANCELLED: return "";
    case RC_INTERACTION_FAIL_NONE:
    default:
        return "";
    }
}

static void publish_outcome(RcPlayer *player, uint64_t generation,
                            RcInteractionHandlerCode code,
                            RcInteractionFailure failure,
                            const char *message) {
    if (!player) return;
    uint64_t sequence = ++player->next_interaction_outcome_sequence;
    if (sequence == 0)
        sequence = ++player->next_interaction_outcome_sequence;
    RcInteractionOutcome outcome;
    memset(&outcome, 0, sizeof(outcome));
    outcome.sequence = sequence;
    outcome.interaction_generation = generation;
    outcome.code = code;
    outcome.failure = failure;
    const char *text = message && message[0] ? message : failure_message(failure);
    if (text) {
        strncpy(outcome.message, text, sizeof(outcome.message) - 1);
        outcome.message[sizeof(outcome.message) - 1] = '\0';
    }
    player->interaction_outcome = outcome;
}

void rc_interaction_reject(RcPlayer *player, RcInteractionFailure reason,
                           const char *message) {
    if (!player) return;
    if (!player->interaction.active)
        player->interaction.last_failure = reason;
    publish_outcome(player, 0, RC_INTERACTION_HANDLER_FAILURE, reason, message);
}

void rc_interaction_publish_message(RcPlayer *player, const char *message) {
    publish_outcome(player, 0, RC_INTERACTION_HANDLER_MESSAGE,
                    RC_INTERACTION_FAIL_NONE, message);
}

int rc_interaction_begin(RcPlayer *player, int source_actor_uid,
                         RcInteractionOp op, const char *option_text,
                         const RcInteractionTarget *target,
                         int approach_range) {
    return rc_interaction_begin_with_source(
        player, source_actor_uid, op, option_text, target, approach_range,
        RC_INTERACTION_KEY_ANY, RC_INTERACTION_KEY_ANY,
        RC_INTERACTION_KEY_ANY, RC_INTERACTION_KEY_ANY);
}

int rc_interaction_begin_with_source(
    RcPlayer *player, int source_actor_uid, RcInteractionOp op,
    const char *option_text, const RcInteractionTarget *target,
    int approach_range, int source_item_id, int source_spell_id,
    int source_widget_id, int source_component_id) {
    if (!player) return 0;
    if (source_actor_uid < 0 || !rc_interaction_op_valid(op)
            || !rc_interaction_target_valid(target)
            || approach_range < 0
            || approach_range > RC_INTERACTION_MAX_APPROACH_RANGE
            || source_item_id < RC_INTERACTION_KEY_ANY
            || source_spell_id < RC_INTERACTION_KEY_ANY
            || source_widget_id < RC_INTERACTION_KEY_ANY
            || source_component_id < RC_INTERACTION_KEY_ANY) {
        rc_interaction_reject(player, RC_INTERACTION_FAIL_INVALID_TARGET,
                              "Invalid interaction request");
        return 0;
    }
    RcPendingInteraction next;
    memset(&next, 0, sizeof(next));
    next.active = true;
    next.generation = ++player->next_interaction_generation;
    if (next.generation == 0)
        next.generation = ++player->next_interaction_generation;
    next.source_actor_uid = source_actor_uid;
    next.op = op;
    copy_option_text(next.option_text, option_text);
    next.target = *target;
    next.source_item_id = source_item_id;
    next.source_inventory_slot = -1;
    next.source_spell_id = source_spell_id;
    next.source_widget_id = source_widget_id;
    next.source_component_id = source_component_id;
    next.approach_range = approach_range;
    next.flags = RC_INTERACTION_STARTED;
    next.last_failure = RC_INTERACTION_FAIL_NONE;
    player->interaction = next;
    // Facing starts with the click (rsmod OpNpc/OpLoc/OpObj handlers): an
    // NPC becomes the face entity; a loc or obj resets it and pends its
    // footprint as the face square.
    if (target->kind == RC_INTERACTION_NPC) {
        player->facing_entity = target->entity_uid;
    } else if (target->kind == RC_INTERACTION_OBJECT
            || target->kind == RC_INTERACTION_GROUND_ITEM) {
        player->facing_entity = -1;
        player->facing_x = target->tile_x;
        player->facing_y = target->tile_y;
        player->facing_width = (uint8_t)clamp_face_size(target->footprint_width);
        player->facing_length = (uint8_t)clamp_face_size(target->footprint_height);
    }
    return 1;
}

void rc_interaction_cancel(RcPlayer *player, RcInteractionFailure reason) {
    if (!player || !player->interaction.active) return;
    RcInteractionHandlerResult result = rc_interaction_result_cancel(reason);
    (void)rc_interaction_apply_result(player, player->interaction.generation,
                                      result);
}

void rc_interaction_clear(RcPlayer *player) {
    if (!player) return;
    memset(&player->interaction, 0, sizeof(player->interaction));
}

int rc_interaction_is_active(const RcPlayer *player) {
    return player && player->interaction.active;
}

const RcPendingInteraction *rc_interaction_get(const RcPlayer *player) {
    return player ? &player->interaction : NULL;
}

const RcInteractionOutcome *rc_interaction_last_outcome(
    const RcPlayer *player) {
    return player ? &player->interaction_outcome : NULL;
}

RcInteractionKind rc_interaction_target_kind(const RcPlayer *player) {
    if (!rc_interaction_is_active(player)) return RC_INTERACTION_NONE;
    return player->interaction.target.kind;
}

int rc_interaction_apply_result(RcPlayer *player, uint64_t generation,
                                RcInteractionHandlerResult result) {
    if (!player || !player->interaction.active
            || player->interaction.generation != generation) {
        return 0;
    }
    if (result.code == RC_INTERACTION_HANDLER_CONTINUE_APPROACH) {
        if (result.approach_range < 0
                || result.approach_range > RC_INTERACTION_MAX_APPROACH_RANGE) {
            result = rc_interaction_result_failure(
                RC_INTERACTION_FAIL_INVALID_TARGET,
                "Invalid interaction approach range");
        } else {
            player->interaction.approach_range = result.approach_range;
            return 1;
        }
    }
    if (result.code == RC_INTERACTION_HANDLER_NONE) {
        result = rc_interaction_result_failure(
            RC_INTERACTION_FAIL_NO_HANDLER, "No interaction result");
    }
    if (result.code != RC_INTERACTION_HANDLER_COMPLETE
            && result.code != RC_INTERACTION_HANDLER_CANCEL
            && result.code != RC_INTERACTION_HANDLER_MESSAGE
            && result.code != RC_INTERACTION_HANDLER_FAILURE
            && result.code != RC_INTERACTION_HANDLER_HANDOFF) {
        result = rc_interaction_result_failure(
            RC_INTERACTION_FAIL_INVALID_TARGET,
            "Invalid interaction result");
    }

    RcInteractionFailure failure = RC_INTERACTION_FAIL_NONE;
    if (result.code == RC_INTERACTION_HANDLER_CANCEL
            || result.code == RC_INTERACTION_HANDLER_FAILURE) {
        failure = result.failure != RC_INTERACTION_FAIL_NONE
                ? result.failure : RC_INTERACTION_FAIL_CANCELLED;
        player->interaction.flags |= RC_INTERACTION_CANCELLED;
    } else {
        player->interaction.flags |= RC_INTERACTION_INTERACTED
                                  | RC_INTERACTION_COMPLETED;
    }
    player->interaction.active = false;
    player->interaction.last_failure = failure;
    publish_outcome(player, generation, result.code, failure, result.message);
    return 1;
}

static uint32_t key_specificity(const RcInteractionDispatchKey *key) {
    uint32_t specificity = 0;
    specificity |= key->definition_id != RC_INTERACTION_KEY_ANY ? 1u << 5 : 0;
    specificity |= key->content_group != RC_INTERACTION_KEY_ANY ? 1u << 4 : 0;
    specificity |= key->source_item_id != RC_INTERACTION_KEY_ANY ? 1u << 3 : 0;
    specificity |= key->source_spell_id != RC_INTERACTION_KEY_ANY ? 1u << 2 : 0;
    specificity |= key->widget_id != RC_INTERACTION_KEY_ANY ? 1u << 1 : 0;
    specificity |= key->component_id != RC_INTERACTION_KEY_ANY ? 1u : 0;
    return specificity;
}

static int wildcard_match(int registered, int actual) {
    return registered == RC_INTERACTION_KEY_ANY || registered == actual;
}

static int key_matches(const RcInteractionDispatchKey *registered,
                       const RcInteractionDispatchKey *actual) {
    if (!registered || !actual) return 0;
    if (registered->kind != actual->kind || registered->op != actual->op) {
        return 0;
    }
    return wildcard_match(registered->definition_id, actual->definition_id)
        && wildcard_match(registered->content_group, actual->content_group)
        && wildcard_match(registered->source_item_id, actual->source_item_id)
        && wildcard_match(registered->source_spell_id, actual->source_spell_id)
        && wildcard_match(registered->widget_id, actual->widget_id)
        && wildcard_match(registered->component_id, actual->component_id);
}

static int keys_equal(const RcInteractionDispatchKey *a,
                      const RcInteractionDispatchKey *b) {
    return a && b && a->kind == b->kind && a->op == b->op
        && a->definition_id == b->definition_id
        && a->content_group == b->content_group
        && a->source_item_id == b->source_item_id
        && a->source_spell_id == b->source_spell_id
        && a->widget_id == b->widget_id
        && a->component_id == b->component_id;
}

static int dispatch_key_valid(const RcInteractionDispatchKey *key) {
    if (!key || !rc_interaction_kind_valid(key->kind)
            || !rc_interaction_op_valid(key->op)) {
        return 0;
    }
    return key->definition_id >= RC_INTERACTION_KEY_ANY
        && key->content_group >= RC_INTERACTION_KEY_ANY
        && key->source_item_id >= RC_INTERACTION_KEY_ANY
        && key->source_spell_id >= RC_INTERACTION_KEY_ANY
        && key->widget_id >= RC_INTERACTION_KEY_ANY
        && key->component_id >= RC_INTERACTION_KEY_ANY;
}

static void clear_handler_table(RcInteractionHandlerEntry *entries,
                                int *count) {
    if (!entries || !count) return;
    memset(entries, 0,
           (size_t)RC_MAX_INTERACTION_HANDLERS * sizeof(entries[0]));
    *count = 0;
}

static int register_handler_in_table(RcInteractionHandlerEntry *entries,
                                     int *count,
                                     const RcInteractionDispatchKey *key,
                                     RcInteractionHandlerFn fn, void *ctx) {
    if (!entries || !count) return 0;
    if (!dispatch_key_valid(key) || !fn) return 0;
    for (int i = 0; i < *count; i++) {
        if (keys_equal(&entries[i].key, key)) {
            entries[i].fn = fn;
            entries[i].ctx = ctx;
            return 1;
        }
    }
    if (*count >= RC_MAX_INTERACTION_HANDLERS) {
        return 0;
    }
    RcInteractionHandlerEntry *entry = &entries[(*count)++];
    entry->key = *key;
    entry->fn = fn;
    entry->ctx = ctx;
    return 1;
}

static int find_handler_in_table(const RcInteractionHandlerEntry *entries,
                                 int count,
                                 const RcInteractionDispatchKey *key) {
    if (!entries || count <= 0) return -1;
    if (!dispatch_key_valid(key)) return -1;
    int best = -1;
    uint32_t best_specificity = 0;
    for (int i = 0; i < count; i++) {
        const RcInteractionHandlerEntry *entry = &entries[i];
        if (!key_matches(&entry->key, key)) continue;
        uint32_t specificity = key_specificity(&entry->key);
        if (best < 0 || specificity > best_specificity) {
            best = i;
            best_specificity = specificity;
        }
    }
    return best;
}

void rc_interaction_clear_world_handlers(RcWorld *world) {
    if (!world) return;
    clear_handler_table(world->interaction_handlers,
                        &world->interaction_handler_count);
}

int rc_interaction_register_world_handler(
    RcWorld *world, const RcInteractionDispatchKey *key,
    RcInteractionHandlerFn fn, void *ctx) {
    if (!world) return 0;
    return register_handler_in_table(world->interaction_handlers,
                                     &world->interaction_handler_count,
                                     key, fn, ctx);
}

int rc_interaction_world_handler_count(const RcWorld *world) {
    return world ? world->interaction_handler_count : 0;
}

int rc_interaction_find_world_handler(const RcWorld *world,
                                      const RcInteractionDispatchKey *key) {
    if (!world) return -1;
    return find_handler_in_table(world->interaction_handlers,
                                 world->interaction_handler_count, key);
}

int rc_interaction_has_specific_world_handler(
    const RcWorld *world, const RcInteractionDispatchKey *key) {
    int index = rc_interaction_find_world_handler(world, key);
    return index >= 0
        && key_specificity(&world->interaction_handlers[index].key) > 0;
}

static RcInteractionHandlerResult no_handler_result(RcPlayer *player) {
    if (player) {
        player->interaction.last_failure = RC_INTERACTION_FAIL_NO_HANDLER;
    }
    return rc_interaction_result_failure(
        RC_INTERACTION_FAIL_NO_HANDLER, "Nothing interesting happens.");
}

RcInteractionHandlerResult rc_interaction_dispatch(RcWorld *world,
                                                    RcPlayer *player) {
    if (!player || !player->interaction.active) {
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_INVALID_SOURCE, "No active interaction");
    }
    if (!rc_interaction_target_valid(&player->interaction.target)
            || !rc_interaction_op_valid(player->interaction.op)) {
        player->interaction.last_failure = RC_INTERACTION_FAIL_INVALID_TARGET;
        return rc_interaction_result_failure(
            RC_INTERACTION_FAIL_INVALID_TARGET, "Invalid interaction target");
    }
    RcInteractionDispatchKey key =
        rc_interaction_dispatch_key_from_pending(&player->interaction);
    int handler_idx = world ? rc_interaction_find_world_handler(world, &key)
                            : -1;
    if (handler_idx < 0) return no_handler_result(player);

    RcPendingInteraction pending = player->interaction;
    const RcInteractionHandlerEntry *entry =
        &world->interaction_handlers[handler_idx];
    RcInteractionHandlerResult result =
        entry->fn(world, player, &pending, entry->ctx);
    if (result.code == RC_INTERACTION_HANDLER_CANCEL ||
            result.code == RC_INTERACTION_HANDLER_FAILURE) {
        if (player->interaction.active
                && player->interaction.generation == pending.generation) {
            player->interaction.last_failure = result.failure;
        }
    }
    return result;
}
