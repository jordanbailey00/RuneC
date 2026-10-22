#ifndef RUNEC_ACTOR_MOTION_H
#define RUNEC_ACTOR_MOTION_H

#include <stdbool.h>
#include <stdint.h>

// Client-side actor movement, run each 20 ms client cycle as the OSRS client
// does (Client3 updateMovement/updateFacingDirection, 317 Game.method99 and
// method100, checked against the B235 client). Server steps queue up and the
// actor walks toward the oldest one, 4 fine units a cycle (128 to a tile),
// catching up at 6 or 8 when the queue grows and doubling for run steps. It
// turns toward its target orientation 32 units a cycle (2048 to a turn,
// 0 = south, clockwise) and takes its pose from the step direction measured
// against where it faces.

#define RUNEC_ACTOR_PATH_CAPACITY 10
#define RUNEC_ACTOR_FINE_TILE 128
#define RUNEC_ACTOR_CYCLE_SECONDS 0.02f
#define RUNEC_ACTOR_TURN_SPEED 32

typedef enum {
    RUNEC_ACTOR_STEP_CRAWL = 0,
    RUNEC_ACTOR_STEP_WALK = 1,
    RUNEC_ACTOR_STEP_RUN = 2,
} RuneCActorStepSpeed;

typedef enum {
    RUNEC_ACTOR_POSE_IDLE = 0,
    RUNEC_ACTOR_POSE_TURN_LEFT,
    RUNEC_ACTOR_POSE_TURN_RIGHT,
    RUNEC_ACTOR_POSE_WALK,
    RUNEC_ACTOR_POSE_WALK_BACK,
    RUNEC_ACTOR_POSE_WALK_LEFT,
    RUNEC_ACTOR_POSE_WALK_RIGHT,
    RUNEC_ACTOR_POSE_RUN,
    RUNEC_ACTOR_POSE_RUN_BACK,
    RUNEC_ACTOR_POSE_RUN_LEFT,
    RUNEC_ACTOR_POSE_RUN_RIGHT,
    RUNEC_ACTOR_POSE_CRAWL,
    RUNEC_ACTOR_POSE_CRAWL_BACK,
    RUNEC_ACTOR_POSE_CRAWL_LEFT,
    RUNEC_ACTOR_POSE_CRAWL_RIGHT,
    RUNEC_ACTOR_POSE_COUNT,
} RuneCActorPose;

// An actor's base animation set: a sequence per pose, -1 when it has none.
typedef struct {
    int seq[RUNEC_ACTOR_POSE_COUNT];
} RuneCActorPoseSet;

// How a primary (action) sequence and movement share the actor: sequence
// opcode 9 (preanim_move) for steps queued before it began, opcode 10
// (postanim_move) for later ones. Both default to merge when the sequence
// has walkmerge labels, else to delaying the move.
typedef enum {
    RUNEC_ANIM_MOVE_DELAY_MOVE = 0,
    RUNEC_ANIM_MOVE_DELAY_ANIM = 1,  // opcode 9: the sequence waits
    RUNEC_ANIM_MOVE_ABORT_ANIM = 1,  // opcode 10: a new step cancels it
    RUNEC_ANIM_MOVE_MERGE = 2,
} RuneCAnimMoveMode;

typedef struct {
    bool active;                    // a primary sequence is playing
    RuneCAnimMoveMode before_move;  // opcode 9
    RuneCAnimMoveMode after_move;   // opcode 10
} RuneCActorPrimary;

typedef struct {
    // Fine position of the footprint centre: tile * 128 + size * 64.
    int x, z;
    int size;
    int yaw, target_yaw;            // current and target orientation
    int turn_speed;                 // units a cycle; 0 never turns
    bool rotation_flag;             // NPC opcode 109; off skips the slow turn
    // Queued steps as tiles: [0] newest, [path_length - 1] walked next.
    int path_length;
    int path_x[RUNEC_ACTOR_PATH_CAPACITY];
    int path_z[RUNEC_ACTOR_PATH_CAPACITY];
    uint8_t path_speed[RUNEC_ACTOR_PATH_CAPACITY];
    int delayed_cycles;             // cycles a primary sequence held a move
    int steps_at_primary;           // steps queued when it began
    bool face_angle_pending;        // a server face angle not yet applied
    int face_angle;
    RuneCActorPose pose;
    int prev_x, prev_z, prev_yaw;   // before the last cycle
} RuneCActorMotion;

// Puts the actor on a tile with nothing queued (spawn or teleport).
void runec_actor_motion_place(RuneCActorMotion *motion, int tile_x,
                              int tile_y, int size, int yaw);
// Queues one server step. With nine pending the oldest is dropped, as the
// client's ten-entry path drops it.
void runec_actor_motion_step(RuneCActorMotion *motion, int tile_x,
                             int tile_y, RuneCActorStepSpeed speed);
// A server face angle; it turns the actor once it is standing.
void runec_actor_motion_face_angle(RuneCActorMotion *motion, int angle);
// Records the steps queued as a primary sequence starts.
void runec_actor_motion_begin_primary(RuneCActorMotion *motion);
// Whether a primary sequence holds its frames until the steps queued before
// it are walked (opcode 9 delayanim).
bool runec_actor_motion_primary_waits(const RuneCActorMotion *motion,
                                      const RuneCActorPrimary *primary);
// Runs one client cycle. `target` is the fine centre of the face entity, or
// NULL; `primary` and `poses` may be NULL.
void runec_actor_motion_cycle(RuneCActorMotion *motion, const int *target,
                              const RuneCActorPrimary *primary,
                              const RuneCActorPoseSet *poses);
// The pose's sequence, falling back to walk and then idle as the client
// does; -1 when the set has neither.
int runec_actor_pose_seq(const RuneCActorPoseSet *poses, RuneCActorPose pose);
// The angle the client turns toward from one fine point to another, or -1
// when they coincide.
int runec_actor_angle_to(int from_x, int from_z, int to_x, int to_z);
// Fine position between the last two cycles, as tiles of the footprint's
// south-west corner; `t` is the share of the next cycle elapsed.
void runec_actor_motion_render_tile(const RuneCActorMotion *motion, float t,
                                    float *tile_x, float *tile_y);
// Orientation between the last two cycles in draw degrees (0 south,
// 90 east, 180 north).
float runec_actor_motion_render_degrees(const RuneCActorMotion *motion,
                                        float t);

#endif
