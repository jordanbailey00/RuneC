#include "actor_motion.h"

#include <math.h>
#include <stdlib.h>

// A step farther than this per axis is popped at once (the B235 client).
#define SNAP_DISTANCE 256

static int fine_centre(int tile, int size) {
    return tile * RUNEC_ACTOR_FINE_TILE + size * (RUNEC_ACTOR_FINE_TILE / 2);
}

void runec_actor_motion_place(RuneCActorMotion *motion, int tile_x,
                              int tile_y, int size, int yaw) {
    if (!motion) return;
    motion->size = size > 0 ? size : 1;
    motion->x = motion->prev_x = fine_centre(tile_x, motion->size);
    motion->z = motion->prev_z = fine_centre(tile_y, motion->size);
    motion->yaw = motion->target_yaw = motion->prev_yaw = yaw & 2047;
    motion->path_length = 0;
    motion->path_x[0] = tile_x;
    motion->path_z[0] = tile_y;
    motion->path_speed[0] = RUNEC_ACTOR_STEP_WALK;
    motion->delayed_cycles = 0;
    motion->steps_at_primary = 0;
    motion->face_angle_pending = false;
    motion->pose = RUNEC_ACTOR_POSE_IDLE;
}

void runec_actor_motion_step(RuneCActorMotion *motion, int tile_x,
                             int tile_y, RuneCActorStepSpeed speed) {
    if (!motion) return;
    if (motion->path_length < RUNEC_ACTOR_PATH_CAPACITY - 1)
        motion->path_length++;
    for (int i = motion->path_length; i > 0; i--) {
        motion->path_x[i] = motion->path_x[i - 1];
        motion->path_z[i] = motion->path_z[i - 1];
        motion->path_speed[i] = motion->path_speed[i - 1];
    }
    motion->path_x[0] = tile_x;
    motion->path_z[0] = tile_y;
    motion->path_speed[0] = (uint8_t)speed;
    // A new step drops a face angle the actor never stood still for.
    motion->face_angle_pending = false;
}

void runec_actor_motion_face_angle(RuneCActorMotion *motion, int angle) {
    if (!motion || angle < 0) return;
    motion->face_angle = angle & 2047;
    motion->face_angle_pending = true;
}

void runec_actor_motion_begin_primary(RuneCActorMotion *motion) {
    if (motion) motion->steps_at_primary = motion->path_length;
}

bool runec_actor_motion_primary_waits(const RuneCActorMotion *motion,
                                      const RuneCActorPrimary *primary) {
    return motion && primary && primary->active
        && primary->before_move == RUNEC_ANIM_MOVE_DELAY_ANIM
        && motion->steps_at_primary > 0;
}

int runec_actor_angle_to(int from_x, int from_z, int to_x, int to_z) {
    int dx = from_x - to_x;
    int dz = from_z - to_z;
    if (!dx && !dz) return -1;
    // 1024/pi per radian, rounded like the core's rc_face_angle.
    return (int)lround(atan2((double)dx, (double)dz) * (1024.0 / M_PI))
         & 2047;
}

// The client's yaw toward the next step, or -1 when already on it.
static int step_yaw(const RuneCActorMotion *motion) {
    int i = motion->path_length - 1;
    int dx = fine_centre(motion->path_x[i], motion->size) - motion->x;
    int dz = fine_centre(motion->path_z[i], motion->size) - motion->z;
    if (dx > 0) return dz > 0 ? 1280 : dz < 0 ? 1792 : 1536;
    if (dx < 0) return dz > 0 ? 768 : dz < 0 ? 256 : 512;
    return dz > 0 ? 1024 : dz < 0 ? 0 : -1;
}

static bool movement_blocked(const RuneCActorMotion *motion,
                             const RuneCActorPrimary *primary) {
    if (!primary || !primary->active || motion->path_length == 0)
        return false;
    RuneCAnimMoveMode mode = motion->steps_at_primary > 0
        ? primary->before_move : primary->after_move;
    return mode == RUNEC_ANIM_MOVE_DELAY_MOVE;
}

static bool has_pose(const RuneCActorPoseSet *poses, RuneCActorPose pose) {
    return poses && poses->seq[pose] >= 0;
}

// Walk poses take their run (speed 8 and up) or crawl (2 and under)
// variant when the set has one.
static RuneCActorPose speed_variant(RuneCActorPose pose, int speed,
                                    const RuneCActorPoseSet *poses) {
    int offset = speed >= 8 ? RUNEC_ACTOR_POSE_RUN - RUNEC_ACTOR_POSE_WALK
               : speed <= 2 ? RUNEC_ACTOR_POSE_CRAWL - RUNEC_ACTOR_POSE_WALK
               : 0;
    RuneCActorPose variant = (RuneCActorPose)(pose + offset);
    return offset && has_pose(poses, variant) ? variant : pose;
}

static void pop_step(RuneCActorMotion *motion) {
    motion->path_length--;
    if (motion->steps_at_primary > 0) motion->steps_at_primary--;
}

static void move(RuneCActorMotion *motion, bool has_target, bool blocked,
                 const RuneCActorPoseSet *poses) {
    motion->pose = RUNEC_ACTOR_POSE_IDLE;
    if (motion->path_length == 0) {
        motion->delayed_cycles = 0;
        return;
    }
    if (blocked) {
        motion->delayed_cycles++;
        return;
    }
    int i = motion->path_length - 1;
    int dst_x = fine_centre(motion->path_x[i], motion->size);
    int dst_z = fine_centre(motion->path_z[i], motion->size);
    if (abs(dst_x - motion->x) > SNAP_DISTANCE
            || abs(dst_z - motion->z) > SNAP_DISTANCE) {
        motion->x = dst_x;
        motion->z = dst_z;
        pop_step(motion);
        return;
    }
    int direction = step_yaw(motion);
    if (direction < 0) {
        pop_step(motion);
        return;
    }

    // The pose follows the step measured against the current orientation,
    // so an actor facing its target steps back or sideways.
    int delta = (direction - motion->yaw) & 2047;
    if (delta > 1024) delta -= 2048;
    RuneCActorPose pose = RUNEC_ACTOR_POSE_WALK_BACK;
    if (delta >= -256 && delta <= 256) pose = RUNEC_ACTOR_POSE_WALK;
    else if (delta >= 256 && delta < 768) pose = RUNEC_ACTOR_POSE_WALK_RIGHT;
    else if (delta >= -768 && delta <= -256) pose = RUNEC_ACTOR_POSE_WALK_LEFT;

    int speed = 4;
    if (motion->rotation_flag) {
        if (motion->yaw != motion->target_yaw && !has_target
                && motion->turn_speed != 0)
            speed = 2;
        if (motion->path_length > 2) speed = 6;
        if (motion->path_length > 3) speed = 8;
    } else {
        if (motion->path_length > 1) speed = 6;
        if (motion->path_length > 2) speed = 8;
    }
    if (motion->delayed_cycles > 0 && motion->path_length > 1) {
        speed = 8;
        motion->delayed_cycles--;
    }
    if (motion->path_speed[i] == RUNEC_ACTOR_STEP_RUN) speed <<= 1;
    else if (motion->path_speed[i] == RUNEC_ACTOR_STEP_CRAWL) speed >>= 1;
    motion->pose = speed_variant(pose, speed, poses);

    if (motion->x < dst_x) {
        motion->x += speed;
        if (motion->x > dst_x) motion->x = dst_x;
    } else if (motion->x > dst_x) {
        motion->x -= speed;
        if (motion->x < dst_x) motion->x = dst_x;
    }
    if (motion->z < dst_z) {
        motion->z += speed;
        if (motion->z > dst_z) motion->z = dst_z;
    } else if (motion->z > dst_z) {
        motion->z -= speed;
        if (motion->z < dst_z) motion->z = dst_z;
    }
    if (motion->x == dst_x && motion->z == dst_z) pop_step(motion);
}

static void turn(RuneCActorMotion *motion) {
    int remaining = (motion->target_yaw - motion->yaw) & 2047;
    if (!remaining || motion->turn_speed <= 0) return;
    if (remaining < motion->turn_speed
            || remaining > 2048 - motion->turn_speed)
        motion->yaw = motion->target_yaw;
    else if (remaining > 1024)
        motion->yaw -= motion->turn_speed;
    else
        motion->yaw += motion->turn_speed;
    motion->yaw &= 2047;
    // Standing actors turn on the spot: counter-clockwise is to their left.
    if (motion->pose == RUNEC_ACTOR_POSE_IDLE
            && motion->yaw != motion->target_yaw) {
        motion->pose = remaining > 1024 ? RUNEC_ACTOR_POSE_TURN_LEFT
                                        : RUNEC_ACTOR_POSE_TURN_RIGHT;
    }
}

void runec_actor_motion_cycle(RuneCActorMotion *motion, const int *target,
                              const RuneCActorPrimary *primary,
                              const RuneCActorPoseSet *poses) {
    if (!motion) return;
    motion->prev_x = motion->x;
    motion->prev_z = motion->z;
    motion->prev_yaw = motion->yaw;
    bool blocked = movement_blocked(motion, primary);

    // The face entity sets the target orientation every cycle; a face angle
    // waits until the actor stands (or a sequence holds its move); else a
    // moving actor faces its next step.
    int facing = -1;
    if (target) {
        facing = runec_actor_angle_to(motion->x, motion->z,
                                      target[0], target[1]);
    } else if (motion->face_angle_pending
            && (motion->path_length == 0 || motion->delayed_cycles > 0)) {
        facing = motion->face_angle;
        motion->face_angle_pending = false;
    } else if (motion->path_length > 0 && !blocked) {
        facing = step_yaw(motion);
    }
    if (facing >= 0) motion->target_yaw = facing;

    move(motion, target != NULL, blocked, poses);
    turn(motion);
}

int runec_actor_pose_seq(const RuneCActorPoseSet *poses,
                         RuneCActorPose pose) {
    if (!poses || pose < 0 || pose >= RUNEC_ACTOR_POSE_COUNT) return -1;
    if (poses->seq[pose] >= 0) return poses->seq[pose];
    if (pose != RUNEC_ACTOR_POSE_IDLE
            && poses->seq[RUNEC_ACTOR_POSE_WALK] >= 0)
        return poses->seq[RUNEC_ACTOR_POSE_WALK];
    return poses->seq[RUNEC_ACTOR_POSE_IDLE];
}

void runec_actor_motion_render_tile(const RuneCActorMotion *motion, float t,
                                    float *tile_x, float *tile_y) {
    if (!motion || !tile_x || !tile_y) return;
    float corner = (float)motion->size * (RUNEC_ACTOR_FINE_TILE * 0.5f);
    float x = (float)motion->prev_x + (float)(motion->x - motion->prev_x) * t;
    float z = (float)motion->prev_z + (float)(motion->z - motion->prev_z) * t;
    *tile_x = (x - corner) / (float)RUNEC_ACTOR_FINE_TILE;
    *tile_y = (z - corner) / (float)RUNEC_ACTOR_FINE_TILE;
}

float runec_actor_motion_render_degrees(const RuneCActorMotion *motion,
                                        float t) {
    if (!motion) return 0.0f;
    int arc = ((motion->yaw - motion->prev_yaw + 1024) & 2047) - 1024;
    float yaw = fmodf((float)motion->prev_yaw + (float)arc * t, 2048.0f);
    if (yaw < 0.0f) yaw += 2048.0f;
    // Yaw runs clockwise from south; draw angles run the other way.
    return -yaw * (360.0f / 2048.0f);
}
