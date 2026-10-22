#include "../../rc-viewer/actor_motion.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { EAST = 1536, NORTH = 1024, WEST = 512, SOUTH = 0 };

static RuneCActorMotion actor_at(int x, int y, int size, int yaw) {
    RuneCActorMotion motion;
    memset(&motion, 0, sizeof(motion));
    motion.turn_speed = RUNEC_ACTOR_TURN_SPEED;
    motion.rotation_flag = true;
    runec_actor_motion_place(&motion, x, y, size, yaw);
    return motion;
}

static int cycles_until_still(RuneCActorMotion *motion, const int *target,
                              const RuneCActorPrimary *primary) {
    int cycles = 0;
    while (motion->path_length > 0 && cycles < 1000) {
        runec_actor_motion_cycle(motion, target, primary, NULL);
        cycles++;
    }
    return cycles;
}

static void test_walk_takes_32_cycles_a_tile(void) {
    RuneCActorMotion motion = actor_at(10, 10, 1, EAST);
    assert(motion.x == 10 * 128 + 64 && motion.z == 10 * 128 + 64);
    runec_actor_motion_step(&motion, 11, 10, RUNEC_ACTOR_STEP_WALK);
    runec_actor_motion_cycle(&motion, NULL, NULL, NULL);
    assert(motion.x == 10 * 128 + 64 + 4);
    assert(motion.pose == RUNEC_ACTOR_POSE_WALK);
    assert(cycles_until_still(&motion, NULL, NULL) == 31);
    assert(motion.x == 11 * 128 + 64);
    runec_actor_motion_cycle(&motion, NULL, NULL, NULL);
    assert(motion.pose == RUNEC_ACTOR_POSE_IDLE);
}

// Turning away from its step with nothing to face halves the walk: a quarter
// turn (16 cycles at 32) costs 16 cycles at speed 2, then 24 at speed 4.
static void test_turning_start_walks_slowly(void) {
    RuneCActorMotion motion = actor_at(10, 10, 1, SOUTH);
    runec_actor_motion_step(&motion, 11, 10, RUNEC_ACTOR_STEP_WALK);
    runec_actor_motion_cycle(&motion, NULL, NULL, NULL);
    assert(motion.target_yaw == EAST);
    assert(motion.x == 10 * 128 + 64 + 2);
    // Facing south, east is on the actor's left.
    assert(motion.pose == RUNEC_ACTOR_POSE_WALK_LEFT);
    assert(motion.yaw == 2048 - 32);
    assert(cycles_until_still(&motion, NULL, NULL) == 39);
    assert(motion.yaw == EAST);
}

static void test_run_steps_double_speed(void) {
    RuneCActorMotion motion = actor_at(10, 10, 1, NORTH);
    runec_actor_motion_step(&motion, 10, 11, RUNEC_ACTOR_STEP_RUN);
    runec_actor_motion_step(&motion, 10, 12, RUNEC_ACTOR_STEP_RUN);
    RuneCActorPoseSet poses;
    for (int i = 0; i < RUNEC_ACTOR_POSE_COUNT; i++) poses.seq[i] = -1;
    poses.seq[RUNEC_ACTOR_POSE_WALK] = 819;
    poses.seq[RUNEC_ACTOR_POSE_RUN] = 824;
    runec_actor_motion_cycle(&motion, NULL, NULL, &poses);
    assert(motion.z == 10 * 128 + 64 + 8);
    assert(motion.pose == RUNEC_ACTOR_POSE_RUN);
    assert(runec_actor_pose_seq(&poses, motion.pose) == 824);
    assert(cycles_until_still(&motion, NULL, NULL) == 31);
    assert(motion.z == 12 * 128 + 64);
}

// A growing queue speeds the walk: more than three steps walk at 8, which
// also shows the run pose.
static void test_queue_catches_up(void) {
    RuneCActorMotion motion = actor_at(10, 10, 1, EAST);
    for (int i = 1; i <= 4; i++)
        runec_actor_motion_step(&motion, 10 + i, 10, RUNEC_ACTOR_STEP_WALK);
    RuneCActorPoseSet poses;
    for (int i = 0; i < RUNEC_ACTOR_POSE_COUNT; i++) poses.seq[i] = -1;
    poses.seq[RUNEC_ACTOR_POSE_RUN] = 824;
    runec_actor_motion_cycle(&motion, NULL, NULL, &poses);
    assert(motion.x == 10 * 128 + 64 + 8);
    assert(motion.pose == RUNEC_ACTOR_POSE_RUN);
    // 16 cycles at 8 (the first run above), 22 at 6, then 32 at 4 twice.
    assert(cycles_until_still(&motion, NULL, NULL) == 15 + 22 + 32 + 32);
}

static void test_queue_drops_the_oldest_step(void) {
    RuneCActorMotion motion = actor_at(10, 10, 1, EAST);
    for (int i = 1; i <= 10; i++)
        runec_actor_motion_step(&motion, 10 + i, 10, RUNEC_ACTOR_STEP_WALK);
    assert(motion.path_length == RUNEC_ACTOR_PATH_CAPACITY - 1);
    assert(motion.path_x[motion.path_length - 1] == 12);
    assert(motion.path_x[0] == 20);
}

static void test_far_step_snaps(void) {
    RuneCActorMotion motion = actor_at(10, 10, 1, EAST);
    runec_actor_motion_step(&motion, 13, 10, RUNEC_ACTOR_STEP_WALK);
    runec_actor_motion_cycle(&motion, NULL, NULL, NULL);
    assert(motion.x == 13 * 128 + 64 && motion.path_length == 0);
}

// A step onto the actor's own tile is dropped; a south-west step walks both
// axes down.
static void test_own_tile_and_south_west_steps(void) {
    RuneCActorMotion motion = actor_at(10, 10, 1, 256);
    runec_actor_motion_step(&motion, 10, 10, RUNEC_ACTOR_STEP_WALK);
    runec_actor_motion_cycle(&motion, NULL, NULL, NULL);
    assert(motion.path_length == 0 && motion.pose == RUNEC_ACTOR_POSE_IDLE);
    runec_actor_motion_step(&motion, 9, 9, RUNEC_ACTOR_STEP_WALK);
    runec_actor_motion_cycle(&motion, NULL, NULL, NULL);
    assert(motion.target_yaw == 256 && motion.pose == RUNEC_ACTOR_POSE_WALK);
    assert(motion.x == 10 * 128 + 60 && motion.z == 10 * 128 + 60);
    assert(cycles_until_still(&motion, NULL, NULL) == 31);
    assert(motion.x == 9 * 128 + 64 && motion.z == 9 * 128 + 64);
}

// A face entity steers the orientation every cycle, even while walking, and
// suppresses the slow turn; the pose shows the side step.
static void test_face_entity_while_walking(void) {
    RuneCActorMotion motion = actor_at(10, 10, 1, EAST);
    int target[2] = {20 * 128 + 64, 10 * 128 + 64};
    runec_actor_motion_step(&motion, 10, 11, RUNEC_ACTOR_STEP_WALK);
    runec_actor_motion_cycle(&motion, target, NULL, NULL);
    assert(motion.target_yaw == EAST);
    assert(motion.z == 10 * 128 + 64 + 4);
    // Facing east, north is on the actor's left.
    assert(motion.pose == RUNEC_ACTOR_POSE_WALK_LEFT);
}

// A large NPC is faced at its footprint's centre.
static void test_face_entity_large_centre(void) {
    RuneCActorMotion player = actor_at(9, 10, 1, EAST);
    RuneCActorMotion boss = actor_at(10, 10, 5, SOUTH);
    assert(boss.x == 10 * 128 + 5 * 64);
    int target[2] = {boss.x, boss.z};
    runec_actor_motion_cycle(&player, target, NULL, NULL);
    // From (9.5, 10.5) toward (12.5, 12.5): east-north-east.
    int expected = runec_actor_angle_to(player.x, player.z, boss.x, boss.z);
    assert(player.target_yaw == expected);
    assert(expected > NORTH && expected < EAST);
}

// A face angle waits until the actor stands, then turns it on the spot.
static void test_face_angle_waits_for_standing(void) {
    RuneCActorMotion motion = actor_at(10, 10, 1, EAST);
    runec_actor_motion_step(&motion, 11, 10, RUNEC_ACTOR_STEP_WALK);
    runec_actor_motion_face_angle(&motion, WEST);
    assert(motion.face_angle_pending);
    runec_actor_motion_cycle(&motion, NULL, NULL, NULL);
    assert(motion.target_yaw == EAST);
    cycles_until_still(&motion, NULL, NULL);
    runec_actor_motion_cycle(&motion, NULL, NULL, NULL);
    assert(motion.target_yaw == WEST && !motion.face_angle_pending);
    assert(motion.pose == RUNEC_ACTOR_POSE_TURN_RIGHT
           || motion.pose == RUNEC_ACTOR_POSE_TURN_LEFT);
    int cycles = 1;
    while (motion.yaw != WEST && cycles < 100) {
        runec_actor_motion_cycle(&motion, NULL, NULL, NULL);
        cycles++;
    }
    // Half a turn at 32 a cycle.
    assert(cycles == 32);
    assert(motion.pose == RUNEC_ACTOR_POSE_IDLE);

    // A new step drops a face angle not yet applied.
    runec_actor_motion_step(&motion, 12, 10, RUNEC_ACTOR_STEP_WALK);
    runec_actor_motion_face_angle(&motion, NORTH);
    runec_actor_motion_step(&motion, 13, 10, RUNEC_ACTOR_STEP_WALK);
    assert(!motion.face_angle_pending);
}

static void test_turn_on_spot_direction(void) {
    RuneCActorMotion motion = actor_at(10, 10, 1, SOUTH);
    runec_actor_motion_face_angle(&motion, WEST);
    runec_actor_motion_cycle(&motion, NULL, NULL, NULL);
    // South to west is clockwise: to the actor's right.
    assert(motion.pose == RUNEC_ACTOR_POSE_TURN_RIGHT);
    assert(motion.yaw == 32);
    motion = actor_at(10, 10, 1, SOUTH);
    runec_actor_motion_face_angle(&motion, EAST);
    runec_actor_motion_cycle(&motion, NULL, NULL, NULL);
    assert(motion.pose == RUNEC_ACTOR_POSE_TURN_LEFT);
    assert(motion.yaw == 2048 - 32);
}

// A primary sequence that delays movement holds queued steps; once it ends
// the actor catches up at 8 while more than one step is queued.
static void test_primary_delays_then_catches_up(void) {
    RuneCActorMotion motion = actor_at(10, 10, 1, EAST);
    runec_actor_motion_step(&motion, 11, 10, RUNEC_ACTOR_STEP_WALK);
    RuneCActorPrimary primary = {
        .active = true,
        .before_move = RUNEC_ANIM_MOVE_DELAY_MOVE,
        .after_move = RUNEC_ANIM_MOVE_DELAY_MOVE,
    };
    runec_actor_motion_begin_primary(&motion);
    assert(motion.steps_at_primary == 1);
    for (int i = 0; i < 10; i++)
        runec_actor_motion_cycle(&motion, NULL, &primary, NULL);
    assert(motion.x == 10 * 128 + 64 && motion.delayed_cycles == 10);
    runec_actor_motion_step(&motion, 12, 10, RUNEC_ACTOR_STEP_WALK);
    primary.active = false;
    runec_actor_motion_cycle(&motion, NULL, &primary, NULL);
    assert(motion.x == 10 * 128 + 64 + 8 && motion.delayed_cycles == 9);

    // A merging sequence lets the actor walk on.
    motion = actor_at(10, 10, 1, EAST);
    runec_actor_motion_step(&motion, 11, 10, RUNEC_ACTOR_STEP_WALK);
    primary = (RuneCActorPrimary){
        .active = true,
        .before_move = RUNEC_ANIM_MOVE_MERGE,
        .after_move = RUNEC_ANIM_MOVE_MERGE,
    };
    runec_actor_motion_begin_primary(&motion);
    runec_actor_motion_cycle(&motion, NULL, &primary, NULL);
    assert(motion.x == 10 * 128 + 64 + 4);

    // A delayanim sequence waits for the steps queued before it.
    primary.before_move = RUNEC_ANIM_MOVE_DELAY_ANIM;
    assert(runec_actor_motion_primary_waits(&motion, &primary));
    cycles_until_still(&motion, NULL, &primary);
    assert(!runec_actor_motion_primary_waits(&motion, &primary));
}

static void test_no_rotation_flag_speeds(void) {
    RuneCActorMotion motion = actor_at(10, 10, 1, SOUTH);
    motion.rotation_flag = false;
    runec_actor_motion_step(&motion, 11, 10, RUNEC_ACTOR_STEP_WALK);
    runec_actor_motion_step(&motion, 12, 10, RUNEC_ACTOR_STEP_WALK);
    runec_actor_motion_cycle(&motion, NULL, NULL, NULL);
    // No slow turn; two queued steps already walk at 6.
    assert(motion.x == 10 * 128 + 64 + 6);
}

static void test_pose_fallbacks(void) {
    RuneCActorPoseSet poses;
    for (int i = 0; i < RUNEC_ACTOR_POSE_COUNT; i++) poses.seq[i] = -1;
    poses.seq[RUNEC_ACTOR_POSE_IDLE] = 808;
    assert(runec_actor_pose_seq(&poses, RUNEC_ACTOR_POSE_WALK_BACK) == 808);
    poses.seq[RUNEC_ACTOR_POSE_WALK] = 819;
    assert(runec_actor_pose_seq(&poses, RUNEC_ACTOR_POSE_TURN_LEFT) == 819);
    assert(runec_actor_pose_seq(&poses, RUNEC_ACTOR_POSE_WALK_BACK) == 819);
    assert(runec_actor_pose_seq(&poses, RUNEC_ACTOR_POSE_IDLE) == 808);
    poses.seq[RUNEC_ACTOR_POSE_WALK_BACK] = 820;
    assert(runec_actor_pose_seq(&poses, RUNEC_ACTOR_POSE_WALK_BACK) == 820);
    assert(runec_actor_pose_seq(NULL, RUNEC_ACTOR_POSE_WALK) == -1);
}

static void test_render_interpolation(void) {
    RuneCActorMotion motion = actor_at(10, 10, 2, EAST);
    runec_actor_motion_step(&motion, 11, 10, RUNEC_ACTOR_STEP_WALK);
    runec_actor_motion_cycle(&motion, NULL, NULL, NULL);
    float x, y;
    runec_actor_motion_render_tile(&motion, 0.5f, &x, &y);
    assert(fabsf(x - (10.0f + 2.0f / 128.0f)) < 1e-4f);
    assert(fabsf(y - 10.0f) < 1e-4f);
    assert(fabsf(runec_actor_motion_render_degrees(&motion, 1.0f) + 270.0f)
           < 1e-3f);
    // The draw angle turns the short way across south.
    motion.prev_yaw = 2048 - 32;
    motion.yaw = 32;
    assert(fabsf(runec_actor_motion_render_degrees(&motion, 0.5f)) < 1e-3f);
}

int main(void) {
    test_walk_takes_32_cycles_a_tile();
    test_turning_start_walks_slowly();
    test_run_steps_double_speed();
    test_queue_catches_up();
    test_queue_drops_the_oldest_step();
    test_far_step_snaps();
    test_own_tile_and_south_west_steps();
    test_face_entity_while_walking();
    test_face_entity_large_centre();
    test_face_angle_waits_for_standing();
    test_turn_on_spot_direction();
    test_primary_delays_then_catches_up();
    test_no_rotation_flag_speeds();
    test_pose_fallbacks();
    test_render_interpolation();
    printf("test_actor_motion: client cycle movement, facing and poses.\n");
    return 0;
}
