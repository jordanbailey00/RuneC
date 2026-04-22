#include "encounter.h"
#include "types.h"
#include "events.h"
#include "config.h"
#include "npc.h"     // g_npc_defs
#include <stdio.h>
#include <string.h>

// RcPayloadNpcEvent lives in events.h — we just consume it here.

#define ENCT_MAGIC 0x54434E45u     // 'ENCT'

static int find_active_slot(const RcEncounterState *s) {
    for (int i = 0; i < RC_ENC_MAX_ACTIVE; i++) {
        if (!s->active[i].active) return i;
    }
    return -1;
}

static int find_active_by_npc(const RcEncounterState *s, RcNpcId id) {
    for (int i = 0; i < RC_ENC_MAX_ACTIVE; i++) {
        if (s->active[i].active && s->active[i].boss_id == id) return i;
    }
    return -1;
}

// Advance one active encounter. Pass-1 tick: phase HP-threshold
// check + mechanic period countdown. Primitives don't fire yet
// (they're NULL until pass-2 registers them); mechanic timers still
// decrement so the state machine exercises correctly.
static void tick_active(RcWorld *world, RcActiveEncounter *a) {
    RcEncounterState *s = &world->encounter;
    const RcEncounterSpec *spec = &s->registry[a->spec_idx];

    // Find the boss NPC — we reach into the base NPC array, which
    // is part of the always-on base per rc-core/README.md §2.
    RcNpc *boss = NULL;
    for (int i = 0; i < world->npc_count; i++) {
        if (world->npcs[i].active && world->npcs[i].uid == a->boss_id) {
            boss = &world->npcs[i];
            break;
        }
    }
    if (!boss || boss->is_dead) return;   // finish handled by on_died.

    a->ticks_since_start++;

    // Phase transition check — advance to the next phase whose
    // trigger is met. Only 100→lower HP% transitions are live here;
    // hard hp=0 triggers + timed/event-driven "enter_after" phases
    // remain deferred until the phase model grows beyond the pass-1
    // shell.
    for (int p = a->current_phase + 1; p < spec->phase_count; p++) {
        const RcEncounterPhase *ph = &spec->phases[p];
        if (ph->hard_hp_trigger) continue;
        if (ph->enter_at_hp_pct == 0 || ph->enter_at_hp_pct == 100) continue;
        // Pass-1 shortcut: compute hp_pct off the def's hitpoints
        // (the max-hp at spawn). Pass 2 tracks max_hp on the NPC
        // instance so re-heals after phase revert compute correctly.
        int def_hp = g_npc_defs[boss->def_id].hitpoints;
        if (def_hp <= 0) continue;
        int hp_pct = boss->current_hp * 100 / def_hp;
        if (hp_pct < ph->enter_at_hp_pct) {
            uint8_t old_phase = a->current_phase;
            a->current_phase = (uint8_t)p;
            RcPayloadPhaseTransition payload = {
                .npc_id = a->boss_id,
                .old_phase = old_phase,
                .new_phase = (uint8_t)p,
            };
            rc_event_fire(world, RC_EVT_PHASE_TRANSITION, &payload);
        }
    }

    // Mechanic countdown — fire periodic primitives when their
    // countdown elapses. Trigger-bound mechanics are dispatched by
    // the event handlers below.
    for (int m = 0; m < spec->mechanic_count; m++) {
        RcEncounterMechanic *mech =
            &((RcEncounterMechanic *)spec->mechanics)[m];
        if (mech->trigger_type != RC_ENC_TRIGGER_PERIODIC) continue;
        if (mech->period_ticks == 0) continue;
        if (mech->ticks_until_next == 0) {
            if (mech->prim) {
                mech->prim(world, (int)(a - s->active),
                           mech->param_block);
            }
            mech->ticks_until_next = mech->period_ticks;
        } else {
            mech->ticks_until_next--;
        }
    }
}

// ---- Public API --------------------------------------------------------

void rc_encounter_init(RcWorld *world) {
    RcEncounterState *s = &world->encounter;
    memset(s, 0, sizeof(*s));
    rc_event_subscribe(world, RC_EVT_NPC_SPAWNED,
                       rc_encounter_on_npc_spawned, s);
    rc_event_subscribe(world, RC_EVT_NPC_DIED,
                       rc_encounter_on_npc_died, s);
    rc_event_subscribe(world, RC_EVT_PLAYER_DAMAGED,
                       rc_encounter_on_player_damaged, s);
    rc_event_subscribe(world, RC_EVT_PHASE_TRANSITION,
                       rc_encounter_on_phase_transition, s);
}

void rc_encounter_tick(RcWorld *world) {
    if (!(world->enabled & RC_SUB_ENCOUNTER)) return;
    RcEncounterState *s = &world->encounter;
    for (int i = 0; i < RC_ENC_MAX_ACTIVE; i++) {
        if (s->active[i].active) tick_active(world, &s->active[i]);
    }
}

int rc_encounter_register(RcWorld *world, const RcEncounterSpec *spec) {
    RcEncounterState *s = &world->encounter;
    if (s->registry_count >= RC_ENC_REGISTRY_CAP) return -1;
    s->registry[s->registry_count] = *spec;
    return s->registry_count++;
}

int rc_encounter_find_spec(const RcWorld *world, uint32_t npc_id) {
    const RcEncounterState *s = &world->encounter;
    for (int i = 0; i < s->registry_count; i++) {
        const RcEncounterSpec *spec = &s->registry[i];
        for (int j = 0; j < spec->npc_id_count; j++) {
            if (spec->npc_ids[j] == npc_id) return i;
        }
    }
    return -1;
}

void rc_encounter_on_npc_spawned(RcWorld *world, int evt,
                                 const void *payload, void *ctx) {
    (void)evt;
    RcEncounterState *s = (RcEncounterState *)ctx;
    const RcPayloadNpcEvent *p = (const RcPayloadNpcEvent *)payload;
    if (!p) return;

    int spec_idx = rc_encounter_find_spec(world, p->def_id);
    if (spec_idx < 0) return;
    int slot = find_active_slot(s);
    if (slot < 0) return;

    RcActiveEncounter *a = &s->active[slot];
    a->active = true;
    a->spec_idx = (uint16_t)spec_idx;
    a->boss_id = p->npc_id;
    a->current_phase = 0;
    a->ticks_since_start = 0;
    s->started_count++;
}

void rc_encounter_on_npc_died(RcWorld *world, int evt,
                              const void *payload, void *ctx) {
    (void)world; (void)evt;
    RcEncounterState *s = (RcEncounterState *)ctx;
    const RcPayloadNpcEvent *p = (const RcPayloadNpcEvent *)payload;
    if (!p) return;

    int slot = find_active_by_npc(s, p->npc_id);
    if (slot < 0) return;
    s->active[slot].active = false;
    s->finished_count++;
}

// PLAYER_DAMAGED → route to event-driven mechanics.
// Pass-2 wiring covers drain_prayer_on_hit (KQ Barbed Spines). When
// the damaging hit's source NPC is a boss in an active encounter, we
// invoke any drain_prayer_on_hit primitive on that spec. Future
// event-driven primitives (e.g. venom on hit, stat-drain on hit)
// route through the same dispatch.
void rc_encounter_on_player_damaged(RcWorld *world, int evt,
                                    const void *payload, void *ctx) {
    (void)evt;
    RcEncounterState *s = (RcEncounterState *)ctx;
    const RcPayloadPlayerDamaged *p = (const RcPayloadPlayerDamaged *)payload;
    if (!p || p->damage == 0) return;             // mitigated → no drain
    if (p->source_npc_id == 0xFFFFu) return;      // non-NPC source

    int slot = find_active_by_npc(s, p->source_npc_id);
    if (slot < 0) return;                         // not a tracked boss

    RcActiveEncounter *a = &s->active[slot];
    const RcEncounterSpec *spec = &s->registry[a->spec_idx];
    for (int i = 0; i < spec->mechanic_count; i++) {
        const RcEncounterMechanic *m = &spec->mechanics[i];
        if (m->primitive_id == RC_PRIM_DRAIN_PRAYER_ON_HIT && m->prim) {
            m->prim(world, slot, m->param_block);
        }
    }
}

void rc_encounter_on_phase_transition(RcWorld *world, int evt,
                                      const void *payload, void *ctx) {
    (void)evt;
    RcEncounterState *s = (RcEncounterState *)ctx;
    const RcPayloadPhaseTransition *p =
        (const RcPayloadPhaseTransition *)payload;
    if (!p) return;

    int slot = find_active_by_npc(s, p->npc_id);
    if (slot < 0) return;

    RcActiveEncounter *a = &s->active[slot];
    const RcEncounterSpec *spec = &s->registry[a->spec_idx];
    for (int i = 0; i < spec->mechanic_count; i++) {
        const RcEncounterMechanic *m = &spec->mechanics[i];
        if (!m->prim) continue;

        if (m->trigger_type == RC_ENC_TRIGGER_PHASE_ENTER &&
            m->phase_idx == p->new_phase) {
            m->prim(world, slot, m->param_block);
        } else if (m->trigger_type == RC_ENC_TRIGGER_PHASE_EXIT &&
                   m->phase_idx == p->old_phase) {
            m->prim(world, slot, m->param_block);
        }
    }
}

// ---- Binary loader ----------------------------------------------------

// Safe-read helper — returns 0 on short read.
#define RD(ptr, n) (fread((ptr), 1, (n), f) == (size_t)(n))

static int read_pstr(FILE *f, char *out, int cap) {
    uint8_t len;
    if (!RD(&len, 1)) return 0;
    if (len >= cap) return 0;
    if (len && !RD(out, len)) return 0;
    out[len] = '\0';
    return 1;
}

int rc_encounter_load(RcWorld *world, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "encounter_load: can't open %s\n", path);
        return -1;
    }
    uint32_t magic, version, count;
    if (!RD(&magic, 4) || !RD(&version, 4) || !RD(&count, 4)) {
        fprintf(stderr, "encounter_load: bad header\n"); fclose(f);
        return -1;
    }
    if (magic != ENCT_MAGIC) {
        fprintf(stderr, "encounter_load: bad magic %08x\n", magic);
        fclose(f); return -1;
    }
    if (version == 0 || version > 2) {
        fprintf(stderr, "encounter_load: unsupported version %u\n", version);
        fclose(f); return -1;
    }

    int loaded = 0;
    for (uint32_t i = 0; i < count; i++) {
        RcEncounterSpec s;
        memset(&s, 0, sizeof(s));

        if (!read_pstr(f, s.slug, sizeof(s.slug))) break;

        uint8_t nid_count;
        if (!RD(&nid_count, 1)) break;
        s.npc_id_count = nid_count > RC_ENC_MAX_NPC_IDS
                         ? RC_ENC_MAX_NPC_IDS : nid_count;
        for (uint8_t j = 0; j < nid_count; j++) {
            uint32_t nid;
            if (!RD(&nid, 4)) { fclose(f); return loaded; }
            if (j < RC_ENC_MAX_NPC_IDS) s.npc_ids[j] = nid;
        }

        uint8_t atk_count;
        if (!RD(&atk_count, 1)) break;
        s.attack_count = atk_count > RC_ENC_MAX_ATTACKS
                         ? RC_ENC_MAX_ATTACKS : atk_count;
        for (uint8_t j = 0; j < atk_count; j++) {
            RcEncounterAttack a; memset(&a, 0, sizeof(a));
            if (!read_pstr(f, a.name, sizeof(a.name))) { fclose(f); return loaded; }
            uint8_t style, warn;
            uint16_t maxhit;
            if (!RD(&style, 1) || !RD(&maxhit, 2) || !RD(&warn, 1)) {
                fclose(f); return loaded;
            }
            a.style = style;
            a.max_hit = maxhit;
            a.warning_ticks = warn;
            if (j < RC_ENC_MAX_ATTACKS) s.attacks[j] = a;
        }

        uint8_t ph_count;
        if (!RD(&ph_count, 1)) break;
        s.phase_count = ph_count > RC_ENC_MAX_PHASES
                        ? RC_ENC_MAX_PHASES : ph_count;
        for (uint8_t j = 0; j < ph_count; j++) {
            RcEncounterPhase p; memset(&p, 0, sizeof(p));
            if (!read_pstr(f, p.id, sizeof(p.id))) { fclose(f); return loaded; }
            uint8_t pct, hard;
            if (!RD(&pct, 1) || !RD(&hard, 1)) { fclose(f); return loaded; }
            p.enter_at_hp_pct = pct;
            p.hard_hp_trigger = (bool)hard;
            if (j < RC_ENC_MAX_PHASES) s.phases[j] = p;
        }

        uint8_t mech_count;
        if (!RD(&mech_count, 1)) break;
        s.mechanic_count = mech_count > RC_ENC_MAX_MECHANICS
                           ? RC_ENC_MAX_MECHANICS : mech_count;
        for (uint8_t j = 0; j < mech_count; j++) {
            RcEncounterMechanic m; memset(&m, 0, sizeof(m));
            if (!read_pstr(f, m.name, sizeof(m.name))) { fclose(f); return loaded; }
            uint8_t prim;
            uint16_t period;
            if (!RD(&prim, 1) || !RD(&period, 2)) { fclose(f); return loaded; }
            if (version >= 2) {
                if (!RD(&m.trigger_type, 1) || !RD(&m.phase_idx, 1)) {
                    fclose(f); return loaded;
                }
            } else {
                m.trigger_type = RC_ENC_TRIGGER_PERIODIC;
                m.phase_idx = 0xFFu;
            }
            if (!RD(m.param_block, sizeof(m.param_block))) {
                fclose(f); return loaded;
            }
            m.primitive_id = prim;
            m.prim = rc_encounter_prim_lookup(prim);
            m.period_ticks = period;
            m.ticks_until_next =
                (m.trigger_type == RC_ENC_TRIGGER_PERIODIC) ? period : 0;
            if (j < RC_ENC_MAX_MECHANICS) s.mechanics[j] = m;
        }

        if (rc_encounter_register(world, &s) >= 0) loaded++;
    }

    fclose(f);
    fprintf(stderr, "encounter_load: loaded %d / %u encounters from %s\n",
            loaded, count, path);
    return loaded;
}

#undef RD
