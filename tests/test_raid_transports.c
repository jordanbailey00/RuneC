#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "content.h"
#include "interaction.h"
#include "objects.h"
#include "player_command.h"
#include "traversal.h"
#include "world_test_fixture.h"

#define ODEF_PATH RC_TEST_SOURCE_DIR "/data/defs/object_defs.bin"
#define OPLI_PATH \
    RC_TEST_SOURCE_DIR "/data/regions/world.object-placements.indexed.bin"
#define OBHV_PATH RC_TEST_SOURCE_DIR "/data/defs/object_behaviors.bin"
#define TRAV_PATH RC_TEST_SOURCE_DIR "/data/defs/traversal_edges.bin"

typedef struct {
    const char *name;
    int obj_id;
    const char *option;
    int anchor_x, anchor_y;   // placement tile of the object
    int approach_x, approach_y;
    int dest_x, dest_y;
} RaidTransportCase;

static const RaidTransportCase g_cases[] = {
    {"CoX entrance", 29777, "Enter", 1232, 3573, 1233, 3572, 3278, 5190},
    {"ToB entrance", 32653, "Enter", 3678, 3216, 3677, 3219, 3220, 4458},
    {"ToA entrance", 46089, "Enter", 3357, 9112, 3359, 9113, 3551, 5153},
    {"Archive entrance", 62768, "Enter", 4123, 5562, 4127, 5561, 4127, 5573},
    {"CoX exit", 29778, "Climb", 3231, 5755, 3231, 5754, 1233, 3571},
    {"ToB exit", 32996, "Use", 3246, 4315, 3246, 4314, 3676, 3219},
    {"ToA exit", 45128, "Leave", 3549, 5163, 3551, 5162, 3359, 9114},
    {"Archive exit", 62765, "Exit", 4085, 5533, 4087, 5535, 4127, 5590},
};

static RcWorld *create_world(int with_content) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_OBJECTS | RC_SUB_TRAVERSAL;
    cfg.object_defs_path = ODEF_PATH;
    cfg.object_placements_path = OPLI_PATH;
    cfg.object_behaviors_path = OBHV_PATH;
    cfg.traversal_edges_path = TRAV_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    if (with_content)
        assert(rc_world_install_content(world, rc_content_raid_transports_register));
    return world;
}

static void place_player(RcWorld *world, int x, int y) {
    rc_test_open_mapsquare(world, x, y, 0);
    world->player.x = world->player.prev_x = x;
    world->player.y = world->player.prev_y = y;
    world->player.plane = 0;
}

int main(void) {
    // Without the content module, the scraped instance rows hide CoX's Enter.
    RcWorld *plain = create_world(0);
    place_player(plain, 1233, 3572);
    assert(!rc_world_object_option_supported(plain, 29777, 1232, 3573, 0, 0, 0));
    rc_world_destroy(plain);

    RcWorld *world = create_world(1);
    int checked = 0;
    for (unsigned i = 0; i < sizeof(g_cases) / sizeof(g_cases[0]); i++) {
        const RaidTransportCase *c = &g_cases[i];
        // The Fractured Archive exists only in b241 data.
        if (!rc_object_def_get(c->obj_id)) {
            fprintf(stderr, "raid transports: skip %s (no definition)\n", c->name);
            continue;
        }
        place_player(world, c->approach_x, c->approach_y);
        assert(rc_world_object_option_supported(world, c->obj_id, c->anchor_x,
                                                c->anchor_y, 0, 0, 0));
        assert(strcmp(rc_world_object_option_label(world, c->obj_id, c->anchor_x,
                                                   c->anchor_y, 0, 0, 0),
                      c->option) == 0);
        assert(rc_player_interact_object_at(world, c->obj_id, c->anchor_x,
                                            c->anchor_y, 0, 0));
        for (int t = 0; t < 8 && (world->player.x != c->dest_x
                                  || world->player.y != c->dest_y); t++)
            rc_world_tick(world);
        if (world->player.x != c->dest_x || world->player.y != c->dest_y) {
            const RcInteractionOutcome *io = rc_interaction_last_outcome(&world->player);
            fprintf(stderr, "raid transports: %s left the player at %d,%d,%d "
                    "('%s')\n", c->name, world->player.x, world->player.y,
                    world->player.plane, io ? io->message : "");
        }
        assert(world->player.x == c->dest_x && world->player.y == c->dest_y);
        assert(world->player.plane == 0);
        checked++;
    }
    assert(checked >= 6);
    rc_world_destroy(world);
    return 0;
}
