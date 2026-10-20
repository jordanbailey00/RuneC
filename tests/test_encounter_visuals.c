// test_encounter_visuals — presentation coverage for the admitted encounters
// on installed data.
//
// The core names every encounter action it records (RC_COMBAT_ACTION_ENCOUNTER
// events): attacks by their name, mechanic parts as "<mechanic>[:<part>]"
// (rc_encounter_prim_parts) and form changes as RC_ENC_FORM_LEAVE_KEY /
// RC_ENC_FORM_ENTER_KEY. For every form that can record each action:
//   1. combat_visuals.tsv holds a row keyed by that name for the form's NPC;
//   2. every spotanim the row names is in spotanims.bin with a model;
//   3. its sequence is in all.anims and animates the form's own skeleton (it
//      shares the stand sequence's framebase), so a graphic's sequence can
//      never stand in for a body animation;
//   4. every object role has a spawn template for each rotation, and each
//      template's model is in spawn_objects.models;
//   5. every projectile the row launches from the NPC starts on its body:
//      the start point (the NPC's centre, `progress` toward the target,
//      `start height` up, plus the travel model's own lift) lies within half
//      a tile of the form's model posed with the row's sequence at the
//      row's start delay. Rows without a sequence (mechanic parts) are posed
//      with their mechanic's sequence at any moment of it.

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../rc-core/encounter.h"
#include "../rc-viewer/anims.h"
#include "../rc-viewer/combat_visuals.h"
#include "../rc-viewer/npc_render_defs.h"
#include "../rc-viewer/spotanims.h"

#define DATA RC_TEST_SOURCE_DIR "/data/"

static RcEncounterSpec g_specs[RC_ENC_REGISTRY_CAP];
static RuneCNpcRenderDefs g_render;
static AnimCache *g_anims;
static SpotAnimSet *g_spots;
static int g_failures;
static int g_origins;   // projectile starts measured on a body

static void fail(const char *what, int npc_id, const char *key, int id) {
    fprintf(stderr, "encounter visuals: npc %d '%s': %s (%d)\n", npc_id, key,
            what, id);
    g_failures++;
}

static int skeleton_of(int seq_id) {
    AnimSequence *seq = seq_id >= 0 && seq_id <= 0xFFFF
                      ? anim_get_sequence(g_anims, (uint16_t)seq_id) : NULL;
    return seq && seq->frame_count ? (int)seq->frames[0].frame.framebase_id : -1;
}

static void check_spotanim(int npc_id, const char *key, int id) {
    if (id < 0) return;
    const SpotAnimDef *spot = spotanim_find(g_spots, id);
    if (!spot || spot->model_id < 0) fail("spotanim missing or modelless", npc_id, key, id);
}

static void check_origin(int npc_id, const char *key, const RcCombatVisualDef *row);

static void check_action(int npc_id, const char *key) {
    assert(strlen(key) < sizeof(((RcCombatAttackEvent *)0)->action_key_name));
    const RcCombatVisualDef *row = rc_combat_visual_for_npc_action(npc_id, key);
    if (!row) {
        fail("no combat visual row", npc_id, key, -1);
        return;
    }
    check_spotanim(npc_id, key, row->launch_spotanim_id);
    check_spotanim(npc_id, key, row->travel_spotanim_id);
    check_spotanim(npc_id, key, row->impact_spotanim_id);
    check_origin(npc_id, key, row);
    if (row->attack_anim_id < 0) return;
    const RuneCNpcRenderDef *render = runec_npc_render_find(&g_render, npc_id);
    int body = render ? skeleton_of(render->stand_anim) : -1;
    int attack = skeleton_of(row->attack_anim_id);
    if (attack < 0) fail("sequence not in all.anims", npc_id, key, row->attack_anim_id);
    else if (body < 0 || attack != body)
        fail("sequence animates another skeleton", npc_id, key, row->attack_anim_id);
}

static bool form_leaves(const RcEncounterForm *form) {
    return form->dormant || form->on_lethal == RC_ENC_LETHAL_TRANSFORM;
}

// Forms another form's transition, revert or a reset can bring in.
static bool form_entered(const RcEncounterSpec *s, int f) {
    for (int g = 0; g < s->form_count; g++) {
        const RcEncounterForm *o = &s->forms[g];
        if ((form_leaves(o) && o->next_form == f)
                || (o->revert_after_ticks && o->revert_form == f))
            return true;
    }
    return f == 0 && s->form_count > 1 && s->disengage == RC_ENC_DISENGAGE_RESET;
}

static void check_spec(const RcEncounterSpec *s) {
    for (int f = 0; f < s->form_count; f++) {
        const RcEncounterForm *form = &s->forms[f];
        if (form_leaves(form)) check_action((int)form->npc_id, RC_ENC_FORM_LEAVE_KEY);
        if (form_entered(s, f)) check_action((int)form->npc_id, RC_ENC_FORM_ENTER_KEY);
        if (form->dormant) continue;   // a dormant form only changes form
        int npc = (int)form->npc_id;
        for (int a = 0; a < s->attack_count; a++) {
            if (s->attacks[a].npc_id == 0 || s->attacks[a].npc_id == form->npc_id)
                check_action(npc, s->attacks[a].name);
        }
        for (int m = 0; m < s->mechanic_count; m++) {
            const RcEncounterMechanic *mech = &s->mechanics[m];
            const char *const *parts = rc_encounter_prim_parts(mech->primitive_id);
            for (int p = 0; parts && parts[p]; p++) {
                char key[64];
                if (parts[p][0])
                    snprintf(key, sizeof(key), "%s:%s", mech->name, parts[p]);
                else
                    snprintf(key, sizeof(key), "%s", mech->name);
                check_action(npc, key);
            }
        }
    }
}

// ---- Model poses (npcs.models / projectiles.models, MDL2/MDL3) ----------

typedef struct {
    int16_t *base;
    uint8_t *skins;
    int count;
} PoseModel;

static int read_model(const char *path, uint32_t model_id, PoseModel *out) {
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "rb");
    assert(f && "installed model pack missing");
    uint32_t magic = 0, count = 0;
    assert(fread(&magic, 4, 1, f) == 1 && fread(&count, 4, 1, f) == 1);
    uint32_t *offsets = malloc((size_t)count * 4);
    assert(offsets && fread(offsets, 4, count, f) == count);
    int found = 0;
    for (uint32_t m = 0; m < count && !found; m++) {
        assert(fseek(f, (long)offsets[m], SEEK_SET) == 0);
        uint32_t id; uint16_t evc, fc, bvc;
        assert(fread(&id, 4, 1, f) == 1 && fread(&evc, 2, 1, f) == 1
               && fread(&fc, 2, 1, f) == 1 && fread(&bvc, 2, 1, f) == 1);
        if (id != model_id) continue;
        long skip = (long)evc * (12 + 4 + (magic == 0x4D444C33u ? 8 : 0));
        assert(fseek(f, skip, SEEK_CUR) == 0);
        out->count = bvc;
        out->base = malloc((size_t)bvc * 6);
        out->skins = malloc(bvc);
        assert(fread(out->base, 2, (size_t)bvc * 3, f) == (size_t)bvc * 3);
        assert(fread(out->skins, 1, bvc, f) == bvc);
        found = 1;
    }
    free(offsets);
    fclose(f);
    return found;
}

static void free_model(PoseModel *m) {
    free(m->base);
    free(m->skins);
}

// Vertices of `m` posed with `seq_id` `cycles` into it (rest pose when the
// sequence is absent), as the viewer animates them.
static const int16_t *pose(AnimModelState *state, const PoseModel *m,
                           int seq_id, int cycles) {
    AnimSequence *seq = seq_id >= 0 && seq_id <= 0xFFFF
                      ? anim_get_sequence(g_anims, (uint16_t)seq_id) : NULL;
    if (!seq || !seq->frame_count) return m->base;
    int frame = seq->frame_count - 1;
    for (int i = 0; i < seq->frame_count; i++) {
        int d = seq->frames[i].delay > 0 ? seq->frames[i].delay : 1;
        if (cycles < d) { frame = i; break; }
        cycles -= d;
    }
    AnimFrameBase *fb = anim_get_framebase(g_anims,
                                           seq->frames[frame].frame.framebase_id);
    if (!fb) return m->base;
    anim_apply_frame(state, m->base, &seq->frames[frame].frame, fb);
    return state->verts;
}

static int sequence_cycles(int seq_id) {
    AnimSequence *seq = seq_id >= 0 && seq_id <= 0xFFFF
                      ? anim_get_sequence(g_anims, (uint16_t)seq_id) : NULL;
    int total = 0;
    for (int i = 0; seq && i < seq->frame_count; i++)
        total += seq->frames[i].delay > 0 ? seq->frames[i].delay : 1;
    return total;
}

// Distance from the start point to the nearest posed vertex. Models face -z
// and y points down, so the point is (0, -height, -forward).
static double gap_to_body(const int16_t *v, int count, int forward, int height) {
    double best = 1e18;
    for (int i = 0; i < count; i++) {
        double dx = v[i * 3], dy = v[i * 3 + 1] + height, dz = v[i * 3 + 2] + forward;
        double d = dx * dx + dy * dy + dz * dz;
        if (d < best) best = d;
    }
    return sqrt(best);
}

// Height the travel model sits above its own origin (centre of its extent),
// at the spotanim's vertical resize as the viewer draws it.
static int model_lift(int spotanim_id) {
    PoseModel m;
    const SpotAnimDef *spot = spotanim_find(g_spots, spotanim_id);
    if (!spot || !read_model(DATA "models/projectiles.models",
                             0xA2000000u + (uint32_t)spotanim_id, &m))
        return 0;
    int lo = 0, hi = 0;
    for (int i = 0; i < m.count; i++) {
        int y = m.base[i * 3 + 1];
        if (i == 0 || y < lo) lo = y;
        if (i == 0 || y > hi) hi = y;
    }
    free_model(&m);
    int resize = spot->resize_z > 0 ? (int)spot->resize_z : 128;
    return -(lo + hi) / 2 * resize / 128;
}

#define ORIGIN_TOLERANCE 64.0   // half a tile

// Returns the start point's gap to the body (fine units), or -1 when the row
// launches nothing from the NPC.
static double origin_gap(int npc_id, const char *key, const RcCombatVisualDef *row) {
    if (row->travel_spotanim_id < 0 || row->projectile_progress < 0
            || row->projectile_start_height < 0
            || (row->source_attachment != RC_COMBAT_VISUAL_ATTACH_SOURCE_ACTOR
                && row->source_attachment != RC_COMBAT_VISUAL_ATTACH_SOURCE_CENTER))
        return -1.0;
    int seq_id = row->attack_anim_id, sampled = 0;
    if (seq_id < 0) {
        char mechanic[64];
        snprintf(mechanic, sizeof(mechanic), "%s", key);
        char *colon = strchr(mechanic, ':');
        if (colon) *colon = '\0';
        const RcCombatVisualDef *launch = rc_combat_visual_for_npc_action(npc_id, mechanic);
        seq_id = launch ? launch->attack_anim_id : -1;
        sampled = 1;
    }
    PoseModel m;
    if (!read_model(DATA "models/npcs.models", (uint32_t)npc_id, &m)) return 1e9;
    AnimModelState *state = anim_model_state_create(m.skins, m.count);
    assert(state);
    int height = row->projectile_start_height + model_lift(row->travel_spotanim_id);
    int forward = row->projectile_progress;
    double best = 1e18;
    int end = sampled ? sequence_cycles(seq_id) : row->projectile_delay;
    for (int t = sampled ? 0 : row->projectile_delay; t <= end; t += 10) {
        double gap = gap_to_body(pose(state, &m, seq_id, t), m.count, forward, height);
        if (gap < best) best = gap;
        if (!sampled) break;
    }
    anim_model_state_free(state);
    free_model(&m);
    return best;
}

static void check_origin(int npc_id, const char *key, const RcCombatVisualDef *row) {
    double gap = origin_gap(npc_id, key, row);
    if (gap >= 0.0) g_origins++;
    if (gap > ORIGIN_TOLERANCE)
        fail("projectile starts off the body (fine units away)", npc_id, key, (int)gap);
}

static unsigned char *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    assert(f && "installed data file missing");
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = malloc((size_t)n);
    assert(data && fread(data, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    *size = (size_t)n;
    return data;
}

static uint32_t u32_at(const unsigned char *data, size_t at) {
    uint32_t v;
    memcpy(&v, data + at, sizeof(v));
    return v;
}

// OANM spawn templates (export_spawn_objects.py) against the object roles.
static void check_spawn_templates(const RcEncounterSpec *specs, int count) {
    size_t tsize = 0, msize = 0;
    unsigned char *t = read_file(DATA "models/spawn_objects.oanim", &tsize);
    unsigned char *m = read_file(DATA "models/spawn_objects.models", &msize);
    assert(tsize >= 12 && u32_at(t, 0) == 0x4D4E414Fu && u32_at(t, 4) == 1);
    uint32_t rows = u32_at(t, 8);
    const size_t row_size = 4 + 4 + 4 + 4 + 4 + 4 + 16;
    assert(tsize == 12 + rows * row_size);
    uint32_t model_count = u32_at(m, 4);
    for (int s = 0; s < count; s++) {
        for (int o = 0; o < specs[s].object_count; o++) {
            const RcEncounterObject *obj = &specs[s].objects[o];
            int rotations = 0;
            for (uint32_t r = 0; r < rows; r++) {
                const unsigned char *row = t + 12 + r * row_size;
                if (u32_at(row, 4) != obj->loc_id || row[21] != obj->loc_type)
                    continue;
                assert(row[23] & (1u << 2) && "template rows are flagged");
                rotations |= 1 << (row[22] & 3);
                bool modelled = false;
                for (uint32_t i = 0; i < model_count && !modelled; i++)
                    modelled = u32_at(m, u32_at(m, 8 + 4 * i)) == u32_at(row, 0);
                if (!modelled) fail("template model missing", (int)obj->loc_id,
                                    obj->name, (int)u32_at(row, 0));
            }
            if (rotations != 0xF) fail("spawn template rotations missing",
                                       (int)obj->loc_id, obj->name, rotations);
        }
    }
    free(t);
    free(m);
}

int main(void) {
    int count = rc_encounter_load_specs(DATA "defs/encounters.bin", g_specs,
                                        RC_ENC_REGISTRY_CAP);
    assert(count > 0);
    assert(rc_load_combat_visuals(DATA "defs/combat_visuals.tsv") > 0);
    runec_npc_render_defs_init(&g_render);
    assert(runec_npc_render_defs_load(&g_render, DATA "defs/npc_defs.bin") > 0);
    g_anims = anim_cache_load(DATA "anims/all.anims");
    g_spots = spotanims_load(DATA "defs/spotanims.bin");
    assert(g_anims && g_spots);

    for (int i = 0; i < count; i++) check_spec(&g_specs[i]);
    check_spawn_templates(g_specs, count);
    // Vorkath 10, KBD 4, KQ 4, Chaos Elemental 5, Graardor 1.
    assert(g_origins >= 24 && "every boss projectile row was measured");

    // The guards themselves: an unknown name and a graphic's sequence fail.
    int before = g_failures;
    check_action(8061, "No such attack");
    assert(g_failures == before + 1);
    RcCombatVisualDef *row = (RcCombatVisualDef *)
        rc_combat_visual_for_npc_action(239, "Fiery breath");
    assert(row);
    int saved = row->attack_anim_id;
    row->attack_anim_id = 86;   // the breath graphic's own sequence
    check_action(239, "Fiery breath");
    assert(g_failures == before + 2);
    row->attack_anim_id = saved;
    // An origin in the air beyond the snout, or above the head, fails.
    RcCombatVisualDef *spit = (RcCombatVisualDef *)
        rc_combat_visual_for_npc_action(8061, "Dragonfire");
    assert(spit && origin_gap(8061, "Dragonfire", spit) <= ORIGIN_TOLERANCE);
    int progress = spit->projectile_progress, height = spit->projectile_start_height;
    spit->projectile_progress = progress + 256;
    assert(origin_gap(8061, "Dragonfire", spit) > ORIGIN_TOLERANCE);
    spit->projectile_progress = progress;
    spit->projectile_start_height = height + 512;
    assert(origin_gap(8061, "Dragonfire", spit) > ORIGIN_TOLERANCE);
    spit->projectile_start_height = height;
    g_failures = before;

    anim_cache_free(g_anims);
    spotanims_free(g_spots);
    if (g_failures) {
        fprintf(stderr, "test_encounter_visuals: %d gap(s)\n", g_failures);
        return 1;
    }
    printf("test_encounter_visuals: ok (%d specs, %d projectile origins)\n",
           count, g_origins);
    return 0;
}
