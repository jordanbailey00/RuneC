#ifndef RC_OBJECT_RUNTIME_H
#define RC_OBJECT_RUNTIME_H

#include "objects.h"
#include "types.h"

typedef enum {
    RC_OBJECT_MUTATION_OK = 1,
    RC_OBJECT_MUTATION_INVALID = -1,
    RC_OBJECT_MUTATION_NOT_FOUND = -2,
    RC_OBJECT_MUTATION_CAPACITY = -3,
    RC_OBJECT_MUTATION_CONFLICT = -4,
} RcObjectMutationResult;

const RcObjectDef *rc_world_object_def_resolve(const RcWorld *world,
                                               int base_obj_id,
                                               int *out_obj_id);
int rc_world_object_resolve_placement(const RcWorld *world,
                                      const RcObjectPlacement *base,
                                      RcObjectPlacement *out);
int rc_world_object_current_placement(const RcWorld *world, int obj_id,
                                      int x, int y, int plane,
                                      uint64_t placement_key,
                                      RcObjectPlacement *out);
int rc_world_object_state_matches(const RcObjectState *state, int obj_id,
                                  int x, int y, int plane);
RcObjectState *rc_world_object_state_find_key(RcWorld *world, uint64_t key);
const RcObjectState *rc_world_object_state_find_key_const(
    const RcWorld *world, uint64_t key);
RcObjectState *rc_world_object_state_find(RcWorld *world, int obj_id,
                                          int x, int y, int plane);
const RcObjectState *rc_world_object_state_find_const(
    const RcWorld *world, int obj_id, int x, int y, int plane);

RcObjectMutationResult rc_world_object_add(
    RcWorld *world, const RcObjectPlacement *object, RcTick duration,
    uint64_t *out_key);
RcObjectMutationResult rc_world_object_delete(
    RcWorld *world, const RcObjectPlacement *base, RcTick duration,
    uint8_t state_flags);
RcObjectMutationResult rc_world_object_replace(
    RcWorld *world, const RcObjectPlacement *base,
    const RcObjectPlacement *replacement, RcTick duration,
    uint8_t state_flags);
RcObjectMutationResult rc_world_object_revert(RcWorld *world, uint64_t key);
// Reverts the changed loc `key` at tick `at` (0 keeps it until reverted).
RcObjectMutationResult rc_world_object_expire(RcWorld *world, uint64_t key,
                                              RcTick at);
void rc_world_objects_tick(RcWorld *world);
int rc_world_objects_replay_collision(RcWorld *world);

int rc_world_object_active_id(const RcWorld *world, int obj_id, int x, int y,
                              int plane);
int rc_world_object_active_state(const RcWorld *world, int obj_id, int x,
                                 int y, int plane, RcObjectState *out);
int rc_world_object_active_state_layer(const RcWorld *world, int obj_id,
                                       int x, int y, int plane, int layer,
                                       RcObjectState *out);
int rc_world_object_active_state_by_key(const RcWorld *world,
                                        uint64_t placement_key,
                                        RcObjectState *out);

#endif
