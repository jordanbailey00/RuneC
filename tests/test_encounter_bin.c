// test_encounter_bin — the ENCT v13 contract between
// tools/export_encounters.py and rc_encounter_load_specs.
//
// Verifies:
//   1. The installed encounters.bin holds exactly the admitted specs
//      (Kalphite Queen, King Black Dragon, Chaos Elemental and Vorkath at
//      full support, General Graardor attacks-only) with their roles,
//      attacks and mechanic parameters.
//   2. A spec round-trips through the binary layout field for field.
//   3. Malformed, over-capacity, unsupported or inconsistent content is
//      rejected all-or-fail: nothing is published and the output is zeroed.
//   4. Role validation names the mismatching definition.
//   5. Only verified primitives resolve; every other id is disabled.

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../rc-core/api.h"
#include "../rc-core/combat.h"
#include "../rc-core/encounter.h"
#include "../rc-core/game_data.h"
#include "../rc-core/prayer.h"
#include "../rc-core/types.h"

#define RC_TEST_ENCOUNTERS_BIN RC_TEST_SOURCE_DIR "/data/defs/encounters.bin"

typedef struct {
    unsigned char data[32768];
    size_t len;
} Buf;

static void put(Buf *b, const void *p, size_t n) {
    assert(b->len + n <= sizeof(b->data));
    memcpy(b->data + b->len, p, n);
    b->len += n;
}
static void u8(Buf *b, unsigned v) { uint8_t x = (uint8_t)v; put(b, &x, 1); }
static void u16(Buf *b, unsigned v) { uint16_t x = (uint16_t)v; put(b, &x, 2); }
static void u32(Buf *b, uint32_t v) { put(b, &v, 4); }
static void str(Buf *b, const char *s) {
    size_t n = strlen(s);
    u8(b, (unsigned)n);
    put(b, s, n);
}

// Mirrors encode_encounter in tools/export_encounters.py.
static void encode_spec(Buf *b, const RcEncounterSpec *s) {
    str(b, s->slug);
    str(b, s->name);
    u8(b, s->support);
    u8(b, s->disengage);
    u16(b, s->respawn_ticks);
    u8(b, s->has_arena);
    u16(b, s->arena_min_x);
    u16(b, s->arena_min_y);
    u16(b, s->arena_max_x);
    u16(b, s->arena_max_y);
    u8(b, s->arena_plane);
    u8(b, s->form_count);
    for (int i = 0; i < s->form_count; i++) {
        const RcEncounterForm *f = &s->forms[i];
        u32(b, f->npc_id);
        str(b, f->name);
        u8(b, f->binds_on_spawn);
        u8(b, f->dormant);
        u8(b, f->on_lethal);
        u8(b, f->next_form);
        u8(b, f->next_hp);
        u16(b, f->transition_ticks);
        u16(b, f->revert_after_ticks);
        u8(b, f->revert_form);
        u8(b, f->revert_hp);
        for (int k = 0; k < 5; k++) u16(b, (uint16_t)f->defence_bonus[k]);
    }
    u8(b, s->helper_count);
    for (int i = 0; i < s->helper_count; i++) {
        u32(b, s->helpers[i].npc_id);
        str(b, s->helpers[i].name);
    }
    // An over-capacity count is written as is; the loader rejects it first.
    u8(b, s->object_count);
    for (int i = 0; i < s->object_count && i < RC_ENC_MAX_OBJECTS; i++) {
        u32(b, s->objects[i].loc_id);
        str(b, s->objects[i].name);
        u8(b, s->objects[i].loc_type);
    }
    u8(b, s->guard_count);
    for (int i = 0; i < s->guard_count && i < RC_ENC_MAX_GUARDS; i++) {
        u32(b, s->guards[i].npc_id);
        str(b, s->guards[i].name);
    }
    u8(b, s->attack_count);
    for (int i = 0; i < s->attack_count; i++) {
        const RcEncounterAttack *a = &s->attacks[i];
        str(b, a->name);
        u32(b, a->npc_id);
        u8(b, a->style);
        u16(b, a->min_hit);
        u16(b, a->max_hit);
        u8(b, a->weight_adjacent);
        u8(b, a->weight_distant);
        u8(b, a->range);
        u8(b, a->hit_delay);
        u8(b, a->breath);
        u8(b, a->adjacent_pct);
        u32(b, a->flags);
        u8(b, a->effect_id);
        u8(b, a->effect_when);
        u8(b, a->effect_a);
        u8(b, a->effect_b);
        u16(b, a->effect_chance);
        u16(b, a->effect_chance_protected);
    }
    u8(b, s->phase_count);
    for (int i = 0; i < s->phase_count; i++) {
        const RcEncounterPhase *p = &s->phases[i];
        str(b, p->id);
        u8(b, p->enter_at_hp_pct);
        u32(b, p->allowed_attack_mask);
        str(b, p->script);
        u8(b, p->player_targetable);
    }
    u8(b, s->mechanic_count);
    for (int i = 0; i < s->mechanic_count; i++) {
        const RcEncounterMechanic *m = &s->mechanics[i];
        str(b, m->name);
        u8(b, m->primitive_id);
        u16(b, m->period_ticks);
        u8(b, m->trigger_type);
        u8(b, m->phase_idx);
        u32(b, m->phase_mask);
        str(b, m->trigger_ref);
        put(b, m->param_block, sizeof(m->param_block));
    }
    u8(b, s->protection_count);
    for (int i = 0; i < s->protection_count; i++) {
        const RcEncounterProtection *p = &s->protections[i];
        u8(b, p->attack_idx);
        u8(b, p->style);
        u32(b, p->prayer_flag);
        u8(b, p->damage_pct);
    }
    u8(b, s->damage_mod_count);
    for (int i = 0; i < s->damage_mod_count; i++) {
        const RcEncounterDamageModifier *m = &s->damage_mods[i];
        u32(b, m->npc_id);
        u8(b, m->condition);
        u8(b, m->style);
        u8(b, m->damage_pct);
        u8(b, m->flags);
    }
}

static void encode_file(Buf *b, const RcEncounterSpec *specs, int count,
                        uint32_t version) {
    b->len = 0;
    u32(b, 0x54434E45u);
    u32(b, version);
    u32(b, (uint32_t)count);
    for (int i = 0; i < count; i++) encode_spec(b, &specs[i]);
}

static char g_path[256];
static RcEncounterSpec g_out[RC_ENC_REGISTRY_CAP];

static int load_buf(const Buf *b) {
    FILE *f = fopen(g_path, "wb");
    assert(f && fwrite(b->data, 1, b->len, f) == b->len);
    fclose(f);
    memset(g_out, 0xA5, sizeof(g_out));
    return rc_encounter_load_specs(g_path, g_out, RC_ENC_REGISTRY_CAP);
}

static bool out_zeroed(void) {
    const unsigned char *p = (const unsigned char *)g_out;
    for (size_t i = 0; i < sizeof(g_out); i++) {
        if (p[i]) return false;
    }
    return true;
}

// A minimal valid spec: two forms (transform), a helper, an object, a melee
// and a breath attack, a phase and the three supported primitives.
static RcEncounterSpec valid_spec(const char *slug, uint32_t base_id) {
    RcEncounterSpec s;
    memset(&s, 0, sizeof(s));
    snprintf(s.slug, sizeof(s.slug), "%s", slug);
    snprintf(s.name, sizeof(s.name), "Spec %s", slug);
    s.support = RC_ENC_SUPPORT_FULL;
    s.disengage = RC_ENC_DISENGAGE_RESET;
    s.respawn_ticks = 7;
    s.form_count = 2;
    s.forms[0] = (RcEncounterForm){.npc_id = base_id, .name = "Form A",
        .binds_on_spawn = true, .on_lethal = RC_ENC_LETHAL_TRANSFORM,
        .next_form = 1, .next_hp = RC_ENC_FORM_HP_FULL, .transition_ticks = 4};
    s.forms[1] = (RcEncounterForm){.npc_id = base_id + 1, .name = "Form B",
        .revert_after_ticks = 30, .revert_form = 0,
        .revert_hp = RC_ENC_FORM_HP_KEEP,
        .defence_bonus = {100, 100, 100, 10, -20}};
    s.has_arena = true;
    s.arena_min_x = 3000;
    s.arena_min_y = 3100;
    s.arena_max_x = 3012;
    s.arena_max_y = 3118;
    s.arena_plane = 2;
    s.guard_count = 1;
    s.guards[0] = (RcEncounterGuard){.npc_id = base_id + 4, .name = "Guard"};
    s.helper_count = 1;
    s.helpers[0] = (RcEncounterHelper){.npc_id = base_id + 2, .name = "Helper"};
    s.object_count = 1;
    s.objects[0] = (RcEncounterObject){.loc_id = base_id + 3, .name = "Puddle",
        .loc_type = 10};
    s.attack_count = 2;
    s.attacks[0] = (RcEncounterAttack){.name = "Bite", .style = COMBAT_MELEE_STAB,
        .max_hit = 20, .weight_adjacent = 2, .flags = RC_ENC_ATTACK_DIAGONAL_REACH};
    s.attacks[1] = (RcEncounterAttack){.name = "Breath", .npc_id = base_id + 1,
        .style = COMBAT_MAGIC, .max_hit = 50, .weight_adjacent = 1,
        .weight_distant = 1, .breath = 3,
        .effect_id = RC_ENC_ATTACK_EFFECT_FREEZE,
        .effect_when = RC_ENC_EFFECT_WHEN_ALWAYS, .effect_b = 5,
        .effect_chance = 500, .effect_chance_protected = 125};
    s.phase_count = 1;
    s.phases[0] = (RcEncounterPhase){.id = "fight", .enter_at_hp_pct = 100,
        .allowed_attack_mask = 3, .player_targetable = true};
    s.mechanic_count = 4;
    RcEncounterMechanic *hatch = &s.mechanics[3];
    snprintf(hatch->name, sizeof(hatch->name), "Hatching");
    hatch->primitive_id = RC_PRIM_HATCH_HELPERS_ON_HIT;
    hatch->trigger_type = RC_ENC_TRIGGER_BOSS_HIT;
    hatch->phase_idx = 0xFF;
    RcPrimParamsHatchHelpersOnHit hh = {
        .helper = 0, .chance_denominator = 20, .near_loc = 23611,
        .near_distance = 3, .despawn_ticks = 100,
    };
    memcpy(hatch->param_block, &hh, sizeof(hh));
    RcEncounterMechanic *rot = &s.mechanics[0];
    snprintf(rot->name, sizeof(rot->name), "Rotation");
    rot->primitive_id = RC_PRIM_ATTACK_COUNTER_ALTERNATE_SPECIAL;
    rot->trigger_type = RC_ENC_TRIGGER_ATTACK_COUNT;
    rot->phase_idx = 0xFF;
    RcPrimParamsAttackCounterAlternateSpecial rp = {
        .every_n_attacks = 4, .special_count = 2,
        .special_mechanics = {1, 2}, .random_first = 1,
    };
    memcpy(rot->param_block, &rp, sizeof(rp));
    RcEncounterMechanic *acid = &s.mechanics[1];
    snprintf(acid->name, sizeof(acid->name), "Acid");
    acid->primitive_id = RC_PRIM_ACID_FIELD;
    acid->trigger_type = RC_ENC_TRIGGER_ATTACK_COUNT;
    acid->phase_idx = 0xFF;
    RcPrimParamsAcidField ap = {
        .pool_count = 4, .pool_radius = 3, .pool_damage_max = 10,
        .pool_leech_pct = 100, .pool_ticks = 9, .strike_count = 5,
        .strike_delay = 2, .strike_min = 5, .strike_max = 9,
        .damage_taken_pct = 50, .strike_style = COMBAT_MAGIC,
        .pool_object = 0, .pool_delay = 1,
    };
    memcpy(acid->param_block, &ap, sizeof(ap));
    RcEncounterMechanic *spawn = &s.mechanics[2];
    snprintf(spawn->name, sizeof(spawn->name), "Spawn");
    spawn->primitive_id = RC_PRIM_SPAWN_OWNED_HELPERS;
    spawn->trigger_type = RC_ENC_TRIGGER_ATTACK_COUNT;
    spawn->phase_idx = 0xFF;
    RcPrimParamsSpawnOwnedHelpers hp = {
        .helper = 0, .count = 1, .spawn_distance = 6, .explode_max = 30,
        .boss_immune = 1, .freeze_player = 1, .boss_idle = 1,
        .instakill_spell = "Crumble Undead", .spawn_delay = 2,
    };
    memcpy(spawn->param_block, &hp, sizeof(hp));
    s.protection_count = 1;
    s.protections[0] = (RcEncounterProtection){.attack_idx = 0,
        .style = COMBAT_MELEE_STAB, .prayer_flag = PRAYER_PROTECT_MELEE,
        .damage_pct = 40};
    s.damage_mod_count = 1;
    s.damage_mods[0] = (RcEncounterDamageModifier){.npc_id = base_id,
        .condition = RC_ENC_DMG_STYLE_NOT, .style = COMBAT_MAGIC,
        .damage_pct = 50};
    return s;
}

static void expect_reject(const RcEncounterSpec *specs, int count,
                          uint32_t version, const char *why) {
    Buf b;
    encode_file(&b, specs, count, version);
    int loaded = load_buf(&b);
    if (loaded != -1) fprintf(stderr, "accepted invalid content: %s\n", why);
    assert(loaded == -1);
    assert(out_zeroed() && "a rejected load publishes nothing");
}

static void test_installed_specs(void) {
    int count = rc_encounter_load_specs(RC_TEST_ENCOUNTERS_BIN, g_out,
                                        RC_ENC_REGISTRY_CAP);
    assert(count == 5);
    const RcEncounterSpec *kq = NULL, *kbd = NULL, *ce = NULL, *vork = NULL,
                          *gg = NULL;
    for (int i = 0; i < count; i++) {
        const RcEncounterSpec *s = &g_out[i];
        if (!strcmp(s->slug, "kalphite_queen")) kq = s;
        else if (!strcmp(s->slug, "king_black_dragon")) kbd = s;
        else if (!strcmp(s->slug, "chaos_elemental")) ce = s;
        else if (!strcmp(s->slug, "vorkath")) vork = s;
        else if (!strcmp(s->slug, "general_graardor")) gg = s;
    }
    assert(kq && kbd && ce && vork && gg);

    assert(kq->support == RC_ENC_SUPPORT_FULL && kq->form_count == 2);
    assert(kq->forms[0].npc_id == 963 && kq->forms[0].binds_on_spawn);
    assert(kq->forms[0].on_lethal == RC_ENC_LETHAL_TRANSFORM);
    assert(kq->forms[0].transition_ticks == 20 && kq->forms[0].next_form == 1);
    assert(kq->forms[1].npc_id == 965 && !kq->forms[1].binds_on_spawn);
    assert(kq->forms[1].revert_after_ticks == 2000);
    assert(kq->forms[1].revert_hp == RC_ENC_FORM_HP_KEEP);
    assert(kq->attack_count == 3 && kq->attacks[0].weight_distant == 0);
    assert(kq->attacks[1].flags & RC_ENC_ATTACK_ALWAYS_ACCURATE);
    assert(kq->attacks[1].effect_id == RC_ENC_ATTACK_EFFECT_DRAIN_PRAYER);
    assert(kq->attacks[1].effect_when == RC_ENC_EFFECT_WHEN_ROLLED);
    // Defence bonuses by style - 1: stab, slash, crush, ranged, magic.
    const int16_t crawling[5] = {50, 50, 10, 100, 100};
    const int16_t airborne[5] = {100, 100, 100, 10, 10};
    assert(!memcmp(kq->forms[0].defence_bonus, crawling, sizeof(crawling)));
    assert(!memcmp(kq->forms[1].defence_bonus, airborne, sizeof(airborne)));
    assert(kq->helper_count == 1 && kq->helpers[0].npc_id == 961);
    assert(kq->mechanic_count == 1 && kq->mechanics[0].prim);
    assert(kq->mechanics[0].trigger_type == RC_ENC_TRIGGER_BOSS_HIT);
    const RcPrimParamsHatchHelpersOnHit *hatch =
        (const void *)kq->mechanics[0].param_block;
    assert(hatch->helper == 0 && hatch->chance_denominator == 20);
    assert(hatch->near_loc == 23611 && hatch->near_distance == 3);
    assert(hatch->despawn_ticks == 100 && !kq->has_arena);

    assert(kbd->forms[0].npc_id == 239 && kbd->attack_count == 5);
    assert(kbd->respawn_ticks == 16);
    for (int i = 2; i < 5; i++) {
        assert(kbd->attacks[i].breath == 2);
        assert(kbd->attacks[i].effect_chance == 500);
        assert(kbd->attacks[i].effect_chance_protected == 125);
    }

    assert(ce->forms[0].npc_id == 2054 && ce->attack_count == 5);
    assert(ce->attacks[4].flags & RC_ENC_ATTACK_NEEDS_FREE_SLOT);
    assert(ce->attacks[4].effect_id == RC_ENC_ATTACK_EFFECT_UNEQUIP);
    assert(ce->attacks[4].effect_b == 4);

    assert(gg->support == RC_ENC_SUPPORT_FULL);
    assert(gg->mechanic_count == 0 && gg->attacks[1].min_hit == 15);
    assert(gg->has_arena && gg->arena_plane == 2 && gg->respawn_ticks == 150);
    assert(gg->arena_min_x == 2864 && gg->arena_min_y == 5351);
    assert(gg->arena_max_x == 2876 && gg->arena_max_y == 5369);
    assert(gg->guard_count == 3 && gg->guards[0].npc_id == 2216);
    assert(gg->guards[1].npc_id == 2217 && gg->guards[2].npc_id == 2218);
    assert(!strcmp(gg->guards[2].name, "Sergeant Grimspike"));

    assert(vork->disengage == RC_ENC_DISENGAGE_RESET);
    assert(vork->forms[0].npc_id == 8059 && vork->forms[0].dormant);
    assert(vork->forms[1].npc_id == 8061);
    assert(vork->helper_count == 1 && vork->helpers[0].npc_id == 8063);
    assert(vork->object_count == 1 && vork->objects[0].loc_id == 32000);
    assert(!strcmp(vork->objects[0].name, "Acid pool"));
    assert(vork->objects[0].loc_type == 10);
    assert(vork->mechanic_count == 3);
    for (int i = 0; i < 3; i++) assert(vork->mechanics[i].prim);
    const RcPrimParamsAttackCounterAlternateSpecial *rot =
        (const void *)vork->mechanics[0].param_block;
    assert(rot->every_n_attacks == 7 && rot->special_count == 2);
    assert(rot->special_mechanics[0] == 1 && rot->special_mechanics[1] == 2);
    const RcPrimParamsAcidField *acid = (const void *)vork->mechanics[1].param_block;
    assert(acid->strike_count == 25 && acid->strike_min == 25);
    assert(acid->strike_max == 41 && acid->damage_taken_pct == 50);
    assert(acid->pool_object == 0 && acid->pool_delay == 2);
    const RcPrimParamsSpawnOwnedHelpers *spawn =
        (const void *)vork->mechanics[2].param_block;
    assert(spawn->spawn_distance == 10 && spawn->explode_max == 60);
    assert(spawn->spawn_delay == 3);
    assert(!strcmp(spawn->instakill_spell, "Crumble Undead"));
    const RcEncounterAttack *bomb = &vork->attacks[6];
    assert(bomb->flags & RC_ENC_ATTACK_TILE_TARGET);
    assert(bomb->adjacent_pct == 50 && bomb->max_hit == 121);
}

static void test_round_trip(void) {
    RcEncounterSpec specs[2] = {valid_spec("one", 60000), valid_spec("two", 60010)};
    Buf b;
    encode_file(&b, specs, 2, 14);
    assert(load_buf(&b) == 2);
    for (int i = 0; i < 2; i++) {
        // prim pointers are resolved at load; re-encoding compares every
        // serialized field.
        for (int m = 0; m < specs[i].mechanic_count; m++) {
            assert(g_out[i].mechanics[m].prim
                   == rc_encounter_prim_lookup(specs[i].mechanics[m].primitive_id));
        }
        Buf want = {.len = 0}, have = {.len = 0};
        encode_spec(&want, &specs[i]);
        encode_spec(&have, &g_out[i]);
        assert(want.len == have.len && !memcmp(want.data, have.data, want.len));
    }
    // An empty file of the current version is valid.
    encode_file(&b, specs, 0, 14);
    assert(load_buf(&b) == 0);
}

static void test_rejections(void) {
    RcEncounterSpec base = valid_spec("base", 60000);
    RcEncounterSpec s;
    Buf b;

    expect_reject(&base, 1, 13, "older version");
    expect_reject(&base, 1, 15, "newer version");
    encode_file(&b, &base, 1, 14);
    b.data[0] ^= 0xFF;
    assert(load_buf(&b) == -1 && out_zeroed());
    encode_file(&b, &base, 1, 14);
    b.len -= 3;
    assert(load_buf(&b) == -1 && out_zeroed());
    encode_file(&b, &base, 1, 14);
    u8(&b, 0);
    assert(load_buf(&b) == -1 && out_zeroed());
    encode_file(&b, &base, 1, 14);
    b.data[8] = 2;   // header count promises a second spec
    assert(load_buf(&b) == -1 && out_zeroed());
    encode_file(&b, &base, 1, 14);
    b.data[8] = 65;  // above RC_ENC_REGISTRY_CAP
    assert(load_buf(&b) == -1 && out_zeroed());

    s = base; s.support = RC_ENC_SUPPORT_NONE;
    expect_reject(&s, 1, 14, "support none");
    s = base; s.forms[0].on_lethal = 2;
    expect_reject(&s, 1, 14, "lethal policy");
    s = base;
    ((RcPrimParamsAttackCounterAlternateSpecial *)s.mechanics[0].param_block)
        ->every_n_attacks = 1;
    expect_reject(&s, 1, 14, "rotation every attack");
    {
        // A protection count above the cap: encode none, then patch the count.
        RcEncounterSpec probe = base;
        probe.protection_count = 0;
        probe.damage_mod_count = 0;
        encode_file(&b, &probe, 1, 14);
        b.data[b.len - 2] = RC_ENC_MAX_PROTECTIONS + 1;
        assert(load_buf(&b) == -1 && out_zeroed());
    }
    s = base; s.disengage = 2;
    expect_reject(&s, 1, 14, "disengage policy");
    s = base; s.form_count = 0;
    expect_reject(&s, 1, 14, "no forms");
    s = base; s.forms[0].binds_on_spawn = false;
    expect_reject(&s, 1, 14, "no binding form");
    s = base; s.forms[0].next_form = 0;
    expect_reject(&s, 1, 14, "transform into itself");
    s = base; s.forms[0].next_form = 2;
    expect_reject(&s, 1, 14, "transform out of range");
    s = base; s.forms[1].revert_form = 1;
    expect_reject(&s, 1, 14, "revert into itself");
    s = base; s.forms[1].npc_id = s.forms[0].npc_id;
    expect_reject(&s, 1, 14, "duplicate form");
    s = base; s.helpers[0].npc_id = s.forms[1].npc_id;
    expect_reject(&s, 1, 14, "helper is a form");
    s = base; s.object_count = RC_ENC_MAX_OBJECTS + 1;
    expect_reject(&s, 1, 14, "object count");
    s = base; s.objects[0].loc_type = 23;
    expect_reject(&s, 1, 14, "object placement type");
    s = base; s.object_count = 2; s.objects[1] = s.objects[0];
    expect_reject(&s, 1, 14, "duplicate object");
    s = base; s.objects[0].name[0] = '\0';
    expect_reject(&s, 1, 14, "unnamed object");
    s = base; s.forms[0].dormant = true;
    expect_reject(&s, 1, 14, "dormant transform form");
    s = base; s.forms[1].transition_ticks = 3;
    expect_reject(&s, 1, 14, "transition on a final form");
    s = base; s.attacks[0].style = COMBAT_NONE;
    expect_reject(&s, 1, 14, "attack style");
    s = base; s.attacks[0].flags |= 1u << 9;
    expect_reject(&s, 1, 14, "unknown attack flag");
    s = base; s.attacks[0].weight_adjacent = 0;
    expect_reject(&s, 1, 14, "unselectable attack");
    s = base; s.attacks[0].min_hit = 21;
    expect_reject(&s, 1, 14, "min above max");
    s = base; s.attacks[0].flags |= RC_ENC_ATTACK_NO_DAMAGE;
    expect_reject(&s, 1, 14, "no-damage attack with a max hit");
    s = base; s.attacks[1].effect_chance = 1001;
    expect_reject(&s, 1, 14, "effect chance");
    s = base; s.attacks[1].effect_id = RC_ENC_ATTACK_EFFECT_COUNT;
    expect_reject(&s, 1, 14, "effect id");
    s = base; s.attacks[1].npc_id = 1;
    expect_reject(&s, 1, 14, "attack owner is not a form");
    s = base; s.attacks[1].flags |= RC_ENC_ATTACK_ALWAYS_ACCURATE;
    expect_reject(&s, 1, 14, "breath with forced accuracy");
    s = base; s.attacks[0].flags |= RC_ENC_ATTACK_TILE_TARGET;
    expect_reject(&s, 1, 14, "tile attack without a delay");
    s = base; s.phases[0].allowed_attack_mask = 1u << 5;
    expect_reject(&s, 1, 14, "phase mask names a missing attack");
    s = base; s.phases[0].enter_at_hp_pct = 101;
    expect_reject(&s, 1, 14, "phase threshold");
    s = base; s.mechanics[1].primitive_id = RC_PRIM_TELEGRAPHED_AOE_TILE;
    expect_reject(&s, 1, 14, "unverified primitive");
    s = base; s.mechanics[1].trigger_type = RC_ENC_TRIGGER_AFTER_ATTACK;
    expect_reject(&s, 1, 14, "undispatched trigger");
    s = base; s.mechanics[1].trigger_type = RC_ENC_TRIGGER_PERIODIC;
    expect_reject(&s, 1, 14, "period-0 periodic");
    s = base; s.mechanics[1].trigger_type = RC_ENC_TRIGGER_PHASE_ENTER;
    s.mechanics[1].period_ticks = 5;
    expect_reject(&s, 1, 14, "period on a phase-enter trigger");
    s = base; s.mechanics[1].phase_mask = 1u << 3;
    expect_reject(&s, 1, 14, "mechanic phase out of range");
    s = base;
    ((RcPrimParamsAttackCounterAlternateSpecial *)s.mechanics[0].param_block)
        ->special_mechanics[1] = 0;
    expect_reject(&s, 1, 14, "rotation lists itself");
    s = base;
    ((RcPrimParamsAttackCounterAlternateSpecial *)s.mechanics[0].param_block)
        ->special_count = 1;
    expect_reject(&s, 1, 14, "attack-count mechanic outside the rotation");
    s = base;
    ((RcPrimParamsAcidField *)s.mechanics[1].param_block)->strike_count = 0;
    expect_reject(&s, 1, 14, "acid field without strikes");
    s = base;
    ((RcPrimParamsAcidField *)s.mechanics[1].param_block)->pool_count = 17;
    expect_reject(&s, 1, 14, "acid pools above the run quota");
    s = base;
    ((RcPrimParamsAcidField *)s.mechanics[1].param_block)->pool_object = 1;
    expect_reject(&s, 1, 14, "acid pool object index");
    s = base;
    ((RcPrimParamsSpawnOwnedHelpers *)s.mechanics[2].param_block)->helper = 1;
    expect_reject(&s, 1, 14, "owned helper index");
    s = base;
    memset(((RcPrimParamsSpawnOwnedHelpers *)s.mechanics[2].param_block)
           ->instakill_spell, 'x', 24);
    expect_reject(&s, 1, 14, "unterminated spell name");
    s = base; s.protections[0].attack_idx = 2;
    expect_reject(&s, 1, 14, "protection attack index");
    s = base; snprintf(s.attacks[0].name, sizeof(s.attacks[0].name), "Bite:hard");
    expect_reject(&s, 1, 14, "':' in an attack name");
    s = base; snprintf(s.mechanics[1].name, sizeof(s.mechanics[1].name), "Acid:pool");
    expect_reject(&s, 1, 14, "':' in a mechanic name");
    s = base; snprintf(s.mechanics[2].name, sizeof(s.mechanics[2].name), "form");
    expect_reject(&s, 1, 14, "a mechanic named like the form-change keys");
    s = base; s.damage_mods[0].condition = RC_ENC_DMG_CAP_RANGE;
    expect_reject(&s, 1, 14, "unsupported damage condition");

    s = base;
    s.support = RC_ENC_SUPPORT_ATTACKS_ONLY;
    expect_reject(&s, 1, 14, "attacks-only spec with forms and mechanics");
    {
        // An attacks-only spec may not spawn objects either.
        RcEncounterSpec plain = base;
        plain.support = RC_ENC_SUPPORT_ATTACKS_ONLY;
        plain.form_count = 1;
        plain.forms[0].on_lethal = RC_ENC_LETHAL_DIE;
        plain.forms[0].next_form = 0;
        plain.forms[0].transition_ticks = 0;
        plain.attacks[1].npc_id = 0;
        plain.helper_count = 0;
        plain.mechanic_count = 0;
        expect_reject(&plain, 1, 14, "attacks-only spec with an object");
        plain.object_count = 0;
        expect_reject(&plain, 1, 14, "attacks-only spec with guards");
        plain.guard_count = 0;
        expect_reject(&plain, 1, 14, "attacks-only spec with an arena");
        plain.has_arena = false;
        plain.arena_min_x = plain.arena_min_y = 0;
        plain.arena_max_x = plain.arena_max_y = 0;
        plain.arena_plane = 0;
        encode_file(&b, &plain, 1, 14);
        assert(load_buf(&b) == 1);
    }

    // Arena and guards.
    s = base; s.arena_min_x = s.arena_max_x + 1;
    expect_reject(&s, 1, 14, "arena min above max");
    s = base; s.arena_plane = 4;
    expect_reject(&s, 1, 14, "arena plane");
    s = base; s.has_arena = false; s.guard_count = 0;
    expect_reject(&s, 1, 14, "arena bounds without an arena");
    s = base; s.has_arena = false;
    s.arena_min_x = s.arena_min_y = s.arena_max_x = s.arena_max_y = 0;
    s.arena_plane = 0;
    expect_reject(&s, 1, 14, "guards without an arena");
    s = base; s.guards[0].npc_id = s.forms[1].npc_id;
    expect_reject(&s, 1, 14, "guard is a form");
    s = base; s.guards[0].npc_id = s.helpers[0].npc_id;
    expect_reject(&s, 1, 14, "guard is a helper");
    s = base; s.guard_count = 2; s.guards[1] = s.guards[0];
    expect_reject(&s, 1, 14, "duplicate guard");
    s = base; s.guards[0].npc_id = 0;
    expect_reject(&s, 1, 14, "guard id 0");
    s = base; s.guard_count = RC_ENC_MAX_GUARDS + 1;
    expect_reject(&s, 1, 14, "guard count");
    {
        RcEncounterSpec two[2] = {valid_spec("a", 60000), valid_spec("b", 60010)};
        two[1].guards[0].npc_id = 60000;
        expect_reject(two, 2, 14, "guard is another spec's form");
        two[1].guards[0].npc_id = 60014;
        two[0].guards[0].npc_id = 60010;
        expect_reject(two, 2, 14, "a later spec's form guards an earlier room");
        two[0].guards[0].npc_id = 60004;
        two[1].guards[0].npc_id = 60004;
        encode_file(&b, two, 2, 14);
        assert(load_buf(&b) == 2 && "one guard type may guard two rooms");
    }

    // Hatching runs on boss hits only, and only boss hits run hatching.
    {
        RcPrimParamsHatchHelpersOnHit *hp =
            (RcPrimParamsHatchHelpersOnHit *)s.mechanics[3].param_block;
        s = base; s.mechanics[3].trigger_type = RC_ENC_TRIGGER_PERIODIC;
        s.mechanics[3].period_ticks = 5;
        expect_reject(&s, 1, 14, "hatching on a period");
        s = base;   // the rotation keeps only Acid; Spawn moves to boss hits
        ((RcPrimParamsAttackCounterAlternateSpecial *)s.mechanics[0].param_block)
            ->special_count = 1;
        s.mechanics[2].trigger_type = RC_ENC_TRIGGER_BOSS_HIT;
        expect_reject(&s, 1, 14, "boss_hit on another primitive");
        s = base; hp->chance_denominator = 0;
        expect_reject(&s, 1, 14, "hatch chance 0");
        s = base; hp->near_loc = 0;
        expect_reject(&s, 1, 14, "hatch without a loc");
        s = base; hp->near_distance = 33;
        expect_reject(&s, 1, 14, "hatch distance");
        s = base; hp->despawn_ticks = 0;
        expect_reject(&s, 1, 14, "hatch lifetime");
        s = base; hp->helper = 1;
        expect_reject(&s, 1, 14, "hatch helper index");
    }

    RcEncounterSpec pair[2] = {valid_spec("dup", 60000), valid_spec("dup", 60010)};
    expect_reject(pair, 2, 14, "duplicate slug");
    pair[1] = valid_spec("other", 60001);
    expect_reject(pair, 2, 14, "form claimed by two specs");
    pair[1] = valid_spec("other", 60002);
    expect_reject(pair, 2, 14, "helper of one spec is a form of another");

    // Strings: empty required names and embedded NULs.
    encode_file(&b, &base, 1, 14);
    b.data[12] = 0;   // slug length 0
    assert(load_buf(&b) == -1 && out_zeroed());
    encode_file(&b, &base, 1, 14);
    b.data[14] = 0;   // NUL inside the slug "base"
    assert(load_buf(&b) == -1 && out_zeroed());
    assert(rc_encounter_load_specs("/nonexistent/encounters.bin", g_out, 4) == -1);
}

static const char *fake_names(const void *ctx, uint32_t npc_id) {
    (void)ctx;
    switch (npc_id) {
        case 60000: return "Form A";
        case 60001: return "Form B";
        case 60002: return "Helper";
        case 60004: return "Guard";
        default: return NULL;
    }
}

static const char *fake_objects(const void *ctx, uint32_t loc_id) {
    (void)ctx;
    return loc_id == 60003 ? "Puddle" : NULL;
}

static void test_roles(void) {
    RcEncounterSpec s = valid_spec("roles", 60000);
    char message[256] = "";
    assert(rc_encounter_validate_roles(&s, 1, fake_names, fake_objects, NULL,
                                       message, sizeof(message)) == 1);
    snprintf(s.helpers[0].name, sizeof(s.helpers[0].name), "Wrong");
    assert(rc_encounter_validate_roles(&s, 1, fake_names, fake_objects, NULL,
                                       message, sizeof(message)) == 0);
    assert(strstr(message, "helper 60002") && strstr(message, "'Wrong'"));
    s = valid_spec("roles", 60000);
    snprintf(s.guards[0].name, sizeof(s.guards[0].name), "Sentry");
    assert(rc_encounter_validate_roles(&s, 1, fake_names, fake_objects, NULL,
                                       message, sizeof(message)) == 0);
    assert(strstr(message, "guard 60004") && strstr(message, "'Sentry'"));
    s = valid_spec("roles", 60000);
    s.forms[1].npc_id = 61000;
    assert(rc_encounter_validate_roles(&s, 1, fake_names, fake_objects, NULL,
                                       message, sizeof(message)) == 0);
    assert(strstr(message, "<missing>"));
    s = valid_spec("roles", 60000);
    snprintf(s.objects[0].name, sizeof(s.objects[0].name), "Lake");
    assert(rc_encounter_validate_roles(&s, 1, fake_names, fake_objects, NULL,
                                       message, sizeof(message)) == 0);
    assert(strstr(message, "object 60003") && strstr(message, "'Lake'"));
    // Without object data only the NPC roles are checked.
    assert(rc_encounter_validate_roles(&s, 1, fake_names, NULL, NULL,
                                       message, sizeof(message)) == 1);
    assert(rc_encounter_validate_roles(&s, 1, NULL, NULL, NULL, NULL, 0) == 0);
}

// Game data refuses specs whose roles name the wrong B237 definitions.
static void test_game_data_rejects_mislabelled_roles(void) {
    RcEncounterSpec s = valid_spec("mislabelled", 963);
    Buf b;
    encode_file(&b, &s, 1, 14);
    FILE *f = fopen(g_path, "wb");
    assert(f && fwrite(b.data, 1, b.len, f) == b.len);
    fclose(f);
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems |= RC_SUB_ENCOUNTER;
    cfg.encounters_path = g_path;
    RcGameDataLoadReport report;
    memset(&report, 0, sizeof(report));
    assert(rc_game_data_load(&cfg, &report) == NULL);
    assert(strstr(report.message, "form 963 is 'Kalphite Queen', expected 'Form A'"));
}

// With object definitions loaded, game data also refuses an object role that
// names a missing or differently named loc.
static void test_game_data_rejects_mislabelled_objects(void) {
    int count = rc_encounter_load_specs(RC_TEST_ENCOUNTERS_BIN, g_out,
                                        RC_ENC_REGISTRY_CAP);
    static RcEncounterSpec specs[RC_ENC_REGISTRY_CAP];
    memcpy(specs, g_out, sizeof(specs));
    RcEncounterSpec *vork = NULL;
    for (int i = 0; i < count; i++) {
        if (!strcmp(specs[i].slug, "vorkath")) vork = &specs[i];
    }
    assert(vork && vork->object_count == 1);
    RcWorldConfig full = rc_preset_full_game();
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems |= RC_SUB_ENCOUNTER | RC_SUB_OBJECTS;
    cfg.object_defs_path = full.object_defs_path;
    cfg.object_behaviors_path = full.object_behaviors_path;
    cfg.encounters_path = g_path;
    const struct { uint32_t loc_id; const char *name, *message; } cases[] = {
        {65535, "Acid pool", "object 65535 is '<missing>', expected 'Acid pool'"},
        {32000, "Lake", "object 32000 is 'Acid pool', expected 'Lake'"},
    };
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        vork->objects[0].loc_id = cases[c].loc_id;
        snprintf(vork->objects[0].name, sizeof(vork->objects[0].name), "%s",
                 cases[c].name);
        Buf b;
        encode_file(&b, specs, count, 14);
        FILE *f = fopen(g_path, "wb");
        assert(f && fwrite(b.data, 1, b.len, f) == b.len);
        fclose(f);
        RcGameDataLoadReport report;
        memset(&report, 0, sizeof(report));
        assert(rc_game_data_load(&cfg, &report) == NULL);
        assert(strstr(report.message, cases[c].message));
    }
}

static void test_primitive_gate(void) {
    for (int id = 0; id < 256; id++) {
        bool supported = id == RC_PRIM_ATTACK_COUNTER_ALTERNATE_SPECIAL
                      || id == RC_PRIM_ACID_FIELD
                      || id == RC_PRIM_SPAWN_OWNED_HELPERS
                      || id == RC_PRIM_HATCH_HELPERS_ON_HIT;
        assert((rc_encounter_prim_lookup((uint8_t)id) != NULL) == supported);
    }
}

int main(void) {
    snprintf(g_path, sizeof(g_path), "/tmp/runec_enct_%ld.bin", (long)getpid());
    test_installed_specs();
    test_round_trip();
    test_rejections();
    test_roles();
    test_game_data_rejects_mislabelled_roles();
    test_game_data_rejects_mislabelled_objects();
    test_primitive_gate();
    unlink(g_path);
    printf("test_encounter_bin: ok\n");
    return 0;
}
