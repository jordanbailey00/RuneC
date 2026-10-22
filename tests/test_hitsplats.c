#include "../rc-viewer/hitsplats.h"
#include "../rc-viewer/actor_projection.h"
#include "../rc-core/combat_hit.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static RcCombatRecentHit recent_hit(int damage, int max_hit,
                                    uint8_t hit_type, int source_uid,
                                    uint64_t sequence) {
    return (RcCombatRecentHit){
        .damage = damage,
        .max_hit = max_hit,
        .source_uid = source_uid,
        .timer = 4,
        .sequence = sequence,
        .hit_type = hit_type,
    };
}

static void test_b237_catalog(void) {
    RuneCHitsplatCatalog catalog;
    assert(runec_hitsplat_catalog_load(
        &catalog, "data/defs/hitsplats.bin") == 49);
    assert(runec_hitsplat_catalog_validate_assets(
        &catalog, "data/sprites/ui"));

    const RuneCHitsplatDef *zero = runec_hitsplat_def(&catalog, 26);
    const RuneCHitsplatDef *damage = runec_hitsplat_def(&catalog, 28);
    const RuneCHitsplatDef *maximum = runec_hitsplat_def(&catalog, 48);
    const RuneCHitsplatDef *poison = runec_hitsplat_def(&catalog, 68);
    const RuneCHitsplatDef *burn = runec_hitsplat_def(&catalog, 74);
    assert(zero && zero->sprite_id == 1358 && zero->display_cycles == 50);
    assert(damage && damage->sprite_id == 1359
           && damage->display_cycles == 50);
    assert(maximum && maximum->sprite_id == 3571
           && maximum->display_cycles == 50);
    assert(poison && poison->sprite_id == 1360
           && poison->display_cycles == 50);
    assert(burn && burn->sprite_id == 4767 && burn->display_cycles == 30);
    assert(!runec_hitsplat_def(&catalog, 2));
}

static void test_basic_selection(void) {
    RcCombatRecentHit miss = recent_hit(
        0, 10, RC_HIT_TYPE_MISS, RC_HIT_SOURCE_PLAYER, 1);
    RcCombatRecentHit damage = recent_hit(
        4, 10, RC_HIT_TYPE_NORMAL, RC_HIT_SOURCE_PLAYER, 2);
    RcCombatRecentHit maximum = recent_hit(
        10, 10, RC_HIT_TYPE_MAX, RC_HIT_SOURCE_PLAYER, 3);
    RcCombatRecentHit other = recent_hit(
        4, 10, RC_HIT_TYPE_NORMAL, 100, 4);
    RcCombatRecentHit poison = recent_hit(
        3, 3, RC_HIT_TYPE_MAX, RC_HIT_SOURCE_STATUS, 5);
    assert(runec_hitsplat_basic_definition(&miss, 0) == 26);
    assert(runec_hitsplat_basic_definition(&damage, 0) == 28);
    assert(runec_hitsplat_basic_definition(&maximum, 0) == 48);
    assert(runec_hitsplat_basic_definition(&other, 0) == 29);
    assert(runec_hitsplat_basic_definition(&other, 1) == 28);
    assert(runec_hitsplat_basic_definition(&poison, 0) == 68);
}

static void test_slots_timing_and_replacement(void) {
    RuneCHitsplatDef definition = {
        .loaded = 1,
        .id = 28,
        .display_cycles = 50,
        .replacement_mode = -1,
    };
    RuneCHitsplatState state = {0};
    assert(runec_hitsplat_state_add(&state, &definition, 1, 1) == 0);
    assert(runec_hitsplat_state_add(&state, &definition, 2, 2) == 1);
    assert(runec_hitsplat_state_add(&state, &definition, 3, 3) == 2);
    assert(runec_hitsplat_state_add(&state, &definition, 4, 4) == 3);
    assert(runec_hitsplat_state_add(&state, &definition, 5, 5) == -1);

    runec_hitsplat_state_advance(&state, 49.0f);
    assert(state.slots[0].active);
    runec_hitsplat_state_advance(&state, 1.0f);
    for (int i = 0; i < RUNEC_HITSPLAT_SLOT_COUNT; i++)
        assert(!state.slots[i].active);
    assert(runec_hitsplat_state_add(&state, &definition, 6, 6) == 0);

    runec_hitsplat_state_reset(&state);
    definition.replacement_mode = 0;
    for (int i = 0; i < RUNEC_HITSPLAT_SLOT_COUNT; i++)
        assert(runec_hitsplat_state_add(
            &state, &definition, i + 1, (uint64_t)i + 1) == i);
    state.slots[2].remaining_cycles = 1.0f;
    assert(runec_hitsplat_state_add(&state, &definition, 9, 9) == 2);

    definition.replacement_mode = 1;
    state.slots[0].amount = 8;
    state.slots[1].amount = 2;
    state.slots[2].amount = 6;
    state.slots[3].amount = 4;
    assert(runec_hitsplat_state_add(&state, &definition, 2, 10) == -1);
    assert(runec_hitsplat_state_add(&state, &definition, 3, 11) == 1);

    int x = 0;
    int y = 0;
    const int expected[4][2] = {
        {0, 0}, {0, -20}, {-15, -10}, {15, -10},
    };
    for (int i = 0; i < RUNEC_HITSPLAT_SLOT_COUNT; i++) {
        runec_hitsplat_slot_offset(i, &x, &y);
        assert(x == expected[i][0] && y == expected[i][1]);
    }

    definition.scroll_x = 20;
    definition.scroll_y = 10;
    definition.fade_start_cycle = 40;
    RuneCHitsplatSlot visual = {
        .remaining_cycles = 25.0f,
        .total_cycles = 50.0f,
    };
    float visual_x = 0.0f;
    float visual_y = 0.0f;
    unsigned char alpha = 0;
    runec_hitsplat_visual_offset(&definition, &visual, &visual_x, &visual_y,
                                 &alpha);
    assert(visual_x == 10.0f && visual_y == -5.0f && alpha == 255);
    visual.remaining_cycles = 5.0f;
    runec_hitsplat_visual_offset(&definition, &visual, &visual_x, &visual_y,
                                 &alpha);
    assert(visual_x == 18.0f && visual_y == -9.0f && alpha == 127);
}

static void test_event_identity_and_sync(void) {
    RcCombatActorState actor = {0};
    rc_combat_actor_record_hit(&actor, 4, 10, COMBAT_MELEE_STAB,
                               RC_HIT_SOURCE_PLAYER,
                               RC_HIT_TYPE_NORMAL, 0, 4);
    rc_combat_actor_record_hit(&actor, 4, 10, COMBAT_MELEE_STAB,
                               RC_HIT_SOURCE_PLAYER,
                               RC_HIT_TYPE_NORMAL, 0, 4);
    assert(actor.recent_hit_count == 2);
    assert(actor.recent_hits[0].sequence == 1);
    assert(actor.recent_hits[1].sequence == 2);

    RuneCHitsplatCatalog catalog;
    assert(runec_hitsplat_catalog_load(
        &catalog, "data/defs/hitsplats.bin") == 49);
    RuneCHitsplatState state = {0};
    runec_hitsplat_state_sync(&state, &catalog, actor.recent_hits,
                              actor.recent_hit_count, 0);
    assert(state.slots[0].active && state.slots[0].sequence == 1);
    assert(state.slots[1].active && state.slots[1].sequence == 2);
    runec_hitsplat_state_sync(&state, &catalog, actor.recent_hits,
                              actor.recent_hit_count, 0);
    assert(!state.slots[2].active);

    rc_combat_actor_tick_recent_hits(&actor);
    assert(actor.recent_hits[0].sequence == 1);
    assert(actor.recent_hits[1].sequence == 2);
}

static void test_actor_projection(void) {
    assert(runec_actor_model_midpoint(0.0f, 2.0f, 1.0f) == 1.0f);
    assert(runec_actor_model_midpoint(2.0f, 0.0f, 0.5f) == 0.5f);

    const float vertices[] = {
        0.0f, 0.25f, 0.0f,
        1.0f, 2.50f, 0.0f,
        0.0f, -0.50f, 1.0f,
    };
    float min_y = 0.0f;
    float max_y = 0.0f;
    assert(runec_actor_model_vertical_bounds(
        vertices, 3, &min_y, &max_y));
    assert(min_y == -0.50f && max_y == 2.50f);
}

int main(void) {
    test_b237_catalog();
    test_basic_selection();
    test_slots_timing_and_replacement();
    test_event_identity_and_sync();
    test_actor_projection();
    puts("hitsplat tests passed");
    return 0;
}
