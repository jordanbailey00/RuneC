#include "../rc-viewer/anims.h"
#include <assert.h>

static int near(float a, float b) { return fabsf(a - b) < 1e-4f; }

// A stacked face moves its MOF1 offset along the posed face's front normal;
// other faces and models without offsets expand as posed.
static void test_posed_mesh_keeps_stacked_face_offsets(void) {
    int16_t flat[] = {0, 0, 0, 100, 0, 0, 0, 0, 100};
    int16_t upright[] = {0, 0, 0, 100, 0, 0, 0, -100, 0};
    uint16_t faces[] = {0, 1, 2, 0, 1, 2};
    float offsets[] = {0.0f, 0.5f};
    float mesh[18];

    AnimModelState state = {.verts = flat, .vert_count = 3};
    anim_update_mesh(mesh, &state, faces, offsets, 2);
    for (int k = 0; k < 3; k++) {
        assert(near(mesh[k * 3 + 1], 0.0f));
        assert(near(mesh[9 + k * 3 + 1], 0.5f));
    }
    assert(near(mesh[12], 100.0f) && near(mesh[17], 100.0f));

    state.verts = upright;  // the same faces posed upright: push follows
    anim_update_mesh(mesh, &state, faces, offsets, 2);
    for (int k = 0; k < 3; k++) {
        assert(near(mesh[k * 3 + 2], 0.0f));
        assert(near(mesh[9 + k * 3 + 2], -0.5f));
    }
    assert(near(mesh[16], 100.0f));

    anim_update_mesh(mesh, &state, faces, NULL, 2);
    assert(near(mesh[11], 0.0f));

    int16_t line[] = {0, 0, 0, 50, 0, 0, 100, 0, 0};  // zero area: unmoved
    state.verts = line;
    anim_update_mesh(mesh, &state, faces, offsets, 2);
    assert(near(mesh[10], 0.0f) && near(mesh[11], 0.0f));
}

int main(void) {
    test_posed_mesh_keeps_stacked_face_offsets();
    AnimSequenceFrame frames[] = {{.delay = 30}, {.delay = 0}, {.delay = 35}};
    AnimSequence timed = {.frame_count = 3, .frames = frames};
    assert(anim_sequence_game_ticks(NULL) == 0);
    assert(anim_sequence_game_ticks(&timed) == 3);
    timed.frame_count = 1;
    assert(anim_sequence_game_ticks(&timed) == 1);
    timed.frame_count = 0;
    assert(anim_sequence_game_ticks(&timed) == 0);
    AnimCache empty = {0};
    assert(!anim_get_sequence(NULL, 1));
    assert(!anim_get_sequence(&empty, 1));
    assert(!anim_get_framebase(NULL, 1));
    assert(!anim_get_framebase(&empty, 1));
    AnimSequence seq[] = {{.seq_id = 9}, {.seq_id = 1}, {.seq_id = 4}};
    AnimFrameBase base[] = {{.base_id = 8}, {.base_id = 2}, {.base_id = 5}};
    AnimCache synthetic = {.sequences = seq, .seq_count = 3, .bases = base, .base_count = 3};
    qsort(seq, 3, sizeof(*seq), anim_compare_sequence);
    qsort(base, 3, sizeof(*base), anim_compare_framebase);
    assert(anim_get_sequence(&synthetic, 4) == &seq[1]);
    assert(anim_get_framebase(&synthetic, 5) == &base[1]);
    assert(!anim_get_sequence(&synthetic, 8));
    assert(!anim_get_framebase(&synthetic, 1));

    AnimCache *cache = anim_cache_load(RC_TEST_SOURCE_DIR "/data/anims/all.anims");
    assert(cache && cache->seq_count > 0 && cache->base_count > 0);
    for (int i = 0; i < cache->seq_count; i++) {
        AnimSequence *s = &cache->sequences[i];
        assert(i == 0 || cache->sequences[i - 1].seq_id < s->seq_id);
        assert(anim_get_sequence(cache, s->seq_id) == s);
    }
    for (int i = 0; i < cache->base_count; i++) {
        AnimFrameBase *b = &cache->bases[i];
        assert(i == 0 || cache->bases[i - 1].base_id < b->base_id);
        assert(anim_get_framebase(cache, b->base_id) == b);
    }
    assert(anim_get_sequence(cache, 10501) && "warped sceptre cast missing");
    assert(anim_get_sequence(cache, 12397) && "Eye of Ayak cast missing");
    int casts[] = {1162, 1167, 1978, 1979, 7855, 8972, 8977};
    for (size_t i = 0; i < sizeof(casts) / sizeof(casts[0]); i++) {
        AnimSequence *cast = anim_get_sequence(cache, casts[i]);
        assert(cast && cast->frame_count && "combat cast sequence missing");
        assert(anim_sequence_game_ticks(cast) > 0);
        for (int f = 0; f < cast->frame_count; f++)
            assert(anim_get_framebase(cache, cast->frames[f].frame.framebase_id));
    }
    anim_cache_free(cache);
    puts("Shared animation cache: every sequence/base resolves; missing keys fail explicitly.");
    return 0;
}
