// test_encounter — verify the encounter subsystem wiring works
// end-to-end through the event bus.
//
// Proves:
//   1. Registering an encounter spec puts it in the registry.
//   2. Spawning an NPC with matching def_id fires RC_EVT_NPC_SPAWNED
//      and the encounter handler creates an active instance.
//   3. Killing that NPC fires RC_EVT_NPC_DIED and finishes the
//      encounter.
//   4. Spawning an NPC whose def_id is NOT registered does nothing
//      (no phantom encounters start).
//   5. Subsystem-gated tick: when RC_SUB_ENCOUNTER is off, active
//      encounters don't advance.

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../rc-core/api.h"
#include "../rc-core/config.h"
#include "../rc-core/encounter.h"
#include "../rc-core/npc.h"
#include "../rc-core/events.h"
#include "../rc-core/combat.h"

// Install two stub NPC defs — one registered as an encounter boss,
// one as a plain NPC.
static void install_stub_defs(void) {
    g_npc_def_count = 2;
    for (int i = 0; i < 2; i++) {
        memset(&g_npc_defs[i], 0, sizeof(g_npc_defs[i]));
    }
    g_npc_defs[0].id = 65001;  // synthetic encounter boss id
    strcpy(g_npc_defs[0].name, "Encounter Boss (stub)");
    g_npc_defs[0].size = 3;
    g_npc_defs[0].hitpoints = 500;
    g_npc_defs[0].stats[1] = 1;
    g_npc_defs[0].stats[3] = 500;

    g_npc_defs[1].id = 12345;  // Non-boss plain NPC
    strcpy(g_npc_defs[1].name, "Plain NPC (stub)");
    g_npc_defs[1].size = 1;
    g_npc_defs[1].hitpoints = 50;
    g_npc_defs[1].stats[3] = 50;
}

// Construct a minimal encounter spec (no mechanics / no phases —
// proves the shell works before primitive registry lands).
static RcEncounterSpec make_stub_spec(void) {
    RcEncounterSpec s = {0};
    strcpy(s.slug, "encounter_stub");
    s.npc_ids[0] = 65001;
    s.npc_id_count = 1;
    return s;
}

int main(void) {
    install_stub_defs();

    RcWorldConfig cfg = rc_preset_combat_only();
    cfg.seed = 42;
    cfg.encounters_path = NULL;
    RcWorld *w = rc_world_create_config(&cfg);
    assert(w != NULL);
    assert((w->enabled & RC_SUB_ENCOUNTER) != 0);

    // ---- 1. Register the encounter ----
    int registry_before = w->encounter.registry_count;
    RcEncounterSpec spec = make_stub_spec();
    int idx = rc_encounter_register(w, &spec);
    assert(idx == registry_before);
    assert(w->encounter.registry_count == registry_before + 1);
    assert(rc_encounter_find_spec(w, 65001) == registry_before);
    assert(rc_encounter_find_spec(w, 99999) == -1);

    // ---- 2. Spawn registered NPC → encounter starts ----
    int before_started = w->encounter.started_count;
    int npc_idx = rc_npc_spawn(w, 0, 3213, 3428, 0);
    assert(npc_idx >= 0);
    assert(w->encounter.started_count == before_started + 1);
    assert(w->encounter.active[0].active);
    assert(w->encounter.active[0].boss_id == w->npcs[npc_idx].uid);

    // ---- 3. Spawn unregistered NPC → no new encounter ----
    int started_after_register = w->encounter.started_count;
    int plain_idx = rc_npc_spawn(w, 1, 3214, 3428, 0);
    assert(plain_idx >= 0);
    assert(w->encounter.started_count == started_after_register);

    // ---- 4. Tick with encounter enabled → ticks_since_start advances ----
    uint32_t before_ticks = w->encounter.active[0].ticks_since_start;
    for (int i = 0; i < 5; i++) rc_world_tick(w);
    assert(w->encounter.active[0].ticks_since_start > before_ticks);

    // ---- 5. Disable the subsystem → encounter stops advancing ----
    w->enabled &= ~RC_SUB_ENCOUNTER;
    uint32_t frozen = w->encounter.active[0].ticks_since_start;
    for (int i = 0; i < 5; i++) rc_world_tick(w);
    assert(w->encounter.active[0].ticks_since_start == frozen);
    w->enabled |= RC_SUB_ENCOUNTER;   // re-enable for death check

    // ---- 6. Kill the boss → encounter finishes ----
    // Simulate death by applying damage-until-dead via the pending
    // hit queue.
    RcNpc *boss = &w->npcs[npc_idx];
    int before_finished = w->encounter.finished_count;
    int guard = 0;
    while (!boss->is_dead && guard++ < 100) {
        rc_queue_hit(boss->pending_hits, &boss->num_pending_hits,
                     100, 0, COMBAT_MELEE_CRUSH, -1, 0u, w->tick);
        rc_world_tick(w);
    }
    assert(boss->is_dead);
    assert(w->encounter.finished_count == before_finished + 1);
    assert(!w->encounter.active[0].active);

    rc_world_destroy(w);

    printf("test_encounter: all encounter-subsystem checks passed.\n");
    return 0;
}
