// test_content_runtime_api — rc-core services that content modules build
// on: per-world content ticks (order, state lookup, capacity, release on
// reset and destroy), the chatbox choice prompt, the game message ring,
// whole varps, NPC combat overrides, and content-scripted attacks and
// mechanic damage.

#include "../rc-core/api.h"
#include "../rc-core/combat.h"
#include "../rc-core/dialogue.h"
#include "../rc-core/interaction.h"
#include "../rc-core/npc.h"
#include "../rc-core/prayer.h"
#include "../rc-core/varbits.h"
#include "runtime_test_fixture.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int runs;
    int last_order;
    int releases;
} TickState;

static int g_order;

static void tick_a(RcWorld *world, void *ctx) {
    (void)world;
    TickState *s = ctx;
    s->runs++;
    s->last_order = ++g_order;
}

static void tick_b(RcWorld *world, void *ctx) {
    tick_a(world, ctx);
}

static void tick_filler(RcWorld *world, void *ctx) {
    (void)world;
    (void)ctx;
}

static void release_state(void *ctx) {
    ((TickState *)ctx)->releases++;
}

static int g_def;

static RcWorld *make_world(void) {
    g_npc_def_count = 0;
    g_def = g_npc_def_count++;
    RcNpcDef *d = &g_npc_defs[g_def];
    memset(d, 0, sizeof(*d));
    d->id = 9801;
    strcpy(d->name, "Content API dummy");
    d->size = 1;
    d->combat_level = 40;
    d->hitpoints = 30;
    d->stats[0] = d->stats[1] = d->stats[2] = 50;
    d->stats[3] = 30;
    d->stats[4] = d->stats[5] = 50;
    d->max_hit = 3;
    d->attack_speed = 4;
    d->attack_types = 0x04;
    d->regen_ticks = 1;
    strcpy(d->options[1], "Attack");
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    cfg.seed = 31;
    RcWorld *world = rc_test_world_create_with_defs(&cfg, "content_api", 0);
    assert(world);
    RcPlayer *p = &world->player;
    p->x = 3200;
    p->y = 3200;
    p->plane = 0;
    rc_test_open_mapsquare(world, p->x, p->y, p->plane);
    p->current_hp = p->max_hp = 990;
    p->current_prayer_points = 990;
    for (int i = 0; i < SKILL_COUNT; i++)
        p->skills.base_level[i] = p->skills.boosted_level[i] = 99;
    return world;
}

static void test_content_ticks(void) {
    RcWorld *world = make_world();
    TickState a = {0}, b = {0};
    assert(rc_world_add_content_tick(world, tick_a, &a, release_state));
    assert(rc_world_add_content_tick(world, tick_b, &b, NULL));
    assert(rc_world_content_ctx(world, tick_a) == &a);
    assert(rc_world_content_ctx(world, tick_b) == &b);
    assert(rc_world_content_ctx(world, tick_filler) == NULL);
    rc_world_tick(world);
    rc_world_tick(world);
    assert(a.runs == 2 && b.runs == 2);
    assert(a.last_order < b.last_order && "ticks run in registration order");
    // The table holds RC_MAX_CONTENT_TICKS entries.
    for (int i = 2; i < RC_MAX_CONTENT_TICKS; i++)
        assert(rc_world_add_content_tick(world, tick_filler, NULL, NULL));
    assert(!rc_world_add_content_tick(world, tick_filler, NULL, NULL));
    // Reset releases the per-world state; with no installer nothing returns.
    assert(rc_world_reset(world));
    assert(a.releases == 1 && b.releases == 0);
    assert(rc_world_content_ctx(world, tick_a) == NULL);
    rc_world_tick(world);
    assert(a.runs == 2);
    // Destroy releases too.
    TickState c = {0};
    assert(rc_world_add_content_tick(world, tick_a, &c, release_state));
    rc_world_destroy(world);
    assert(c.releases == 1);
}

static int g_picked;

static void on_choice(RcWorld *world, int option, void *ctx) {
    (void)world;
    assert(ctx == &g_picked);
    g_picked = option;
}

static void settle(RcWorld *world) {
    for (int i = 0; i < 4; i++) rc_world_tick(world);
}

static void test_choice_prompt(void) {
    RcWorld *world = make_world();
    static const char *const options[] = {"Yes.", "No."};
    static const char *const many[RC_CHOICE_MAX_OPTIONS + 1] = {
        "1", "2", "3", "4", "5", "6"};
    assert(!rc_world_open_choice(world, "Title", options, 0, on_choice, NULL));
    assert(!rc_world_open_choice(world, "Title", many, RC_CHOICE_MAX_OPTIONS + 1,
                                 on_choice, NULL));
    assert(!rc_world_open_choice(world, "Title", options, 2, NULL, NULL));

    g_picked = -1;
    assert(rc_world_open_choice(world, NULL, options, 2, on_choice, &g_picked));
    assert(world->choice.active && world->choice.option_count == 2);
    assert(strcmp(world->choice.title, "Select an Option") == 0);
    uint32_t generation = world->choice.generation;
    assert(rc_player_widget_action(world, RC_CHOICE_WIDGET, 1, 0));
    settle(world);
    assert(g_picked == 1 && !world->choice.active);

    // A closed prompt ignores a late pick.
    g_picked = -1;
    assert(rc_world_open_choice(world, "Start the raid?", options, 2, on_choice,
                                &g_picked));
    assert(world->choice.generation == generation + 1);
    rc_world_close_choice(world);
    rc_player_widget_action(world, RC_CHOICE_WIDGET, 0, 0);
    settle(world);
    assert(g_picked == -1);

    // Out-of-range picks run nothing and leave it open.
    assert(rc_world_open_choice(world, "Title", options, 2, on_choice, &g_picked));
    rc_player_widget_action(world, RC_CHOICE_WIDGET, 2, 0);
    settle(world);
    assert(g_picked == -1 && world->choice.active);

    // Walking away (applied on the next tick) closes it without a pick.
    assert(rc_player_walk_to(world, world->player.x + 2, world->player.y));
    rc_world_tick(world);
    assert(!world->choice.active && g_picked == -1);
    rc_world_destroy(world);
}

static void test_message_ring(void) {
    RcWorld *world = make_world();
    RcPlayer *p = &world->player;
    assert(rc_player_next_game_message(p, 0) == NULL);
    rc_interaction_publish_message(p, "The raid has begun!");
    rc_interaction_publish_message(p, "");
    const RcGameMessage *m = rc_player_next_game_message(p, 0);
    assert(m && m->sequence == 1 && strcmp(m->text, "The raid has begun!") == 0);
    assert(rc_player_next_game_message(p, m->sequence) == NULL &&
           "empty messages are not kept");
    // Past capacity the oldest drop; a reader that fell behind resumes at
    // the oldest kept message, in order.
    char text[32];
    for (int i = 2; i <= RC_GAME_MESSAGE_CAPACITY + 5; i++) {
        snprintf(text, sizeof(text), "message %d", i);
        rc_interaction_publish_message(p, text);
    }
    uint64_t newest = RC_GAME_MESSAGE_CAPACITY + 5;
    m = rc_player_next_game_message(p, 1);
    assert(m && m->sequence == newest - RC_GAME_MESSAGE_CAPACITY + 1);
    int seen = 0;
    for (uint64_t seq = 1; (m = rc_player_next_game_message(p, seq)); seq = m->sequence) {
        snprintf(text, sizeof(text), "message %d", (int)m->sequence);
        assert(strcmp(m->text, text) == 0);
        seen++;
    }
    assert(seen == RC_GAME_MESSAGE_CAPACITY);
    rc_world_destroy(world);
}

static void test_varps(void) {
    RcWorld *world = make_world();
    assert(rc_varp_set(world, 4609, 123456) == 0);
    assert(rc_varp_get(world, 4609) == 123456);
    assert(rc_varp_set(world, -1, 1) == -1 && rc_varp_set(world, RC_MAX_VARPS, 1) == -1);
    assert(rc_varp_get(world, RC_MAX_VARPS) == 0 && rc_varp_get(NULL, 4609) == 0);
    rc_world_destroy(world);
}

static void test_npc_overrides(void) {
    RcWorld *world = make_world();
    int slot = rc_npc_spawn(world, g_def, 3205, 3205, 0);
    assert(slot >= 0);
    RcNpc *npc = &world->npcs[slot];
    const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
    assert(rc_npc_max_hit(npc, def) == 3 && rc_npc_attack_speed(npc, def) == 4);

    RcNpcCombatOverride o = {
        .hitpoints = 200,
        .stats = {150, 0, 120, 0, 0, 0},
        .max_hit = 25,
        .attack_speed = 6,
        .attack_style = COMBAT_MAGIC,
        .attack_bonus = {0, 0, 0, 0, 40},
        .defence_bonus = {10, 20, 30, 40, 50},
    };
    rc_npc_apply_combat_override(npc, &o);
    assert(npc->spawn_hp == 200 && npc->current_hp == 200);
    assert(npc->stats[0] == 150 && npc->stats[2] == 120);
    assert(npc->stats[1] == 50 && "zero fields keep the definition's value");
    assert(rc_npc_max_hit(npc, def) == 25 && rc_npc_attack_speed(npc, def) == 6);
    assert(npc->attack_style_override == COMBAT_MAGIC);
    assert(npc->attack_bonus[4] == 40 && npc->defence_bonus[2] == 30);

    // Drained stats regenerate toward the override, not the definition.
    npc->stats[0] = 140;
    for (int i = 0; i < 40; i++) rc_npc_status_tick(world, npc);
    assert(npc->stats[0] == 150);

    // A new life returns to the definition.
    rc_npc_reset_life(world, npc);
    assert(npc->spawn_hp == 30 && npc->stats[0] == 50);
    assert(rc_npc_max_hit(npc, def) == 3 && rc_npc_attack_speed(npc, def) == 4);
    assert(npc->attack_style_override == 0 && npc->defence_bonus[2] == 0);
    rc_world_destroy(world);
}

static int land(RcWorld *world, int ticks) {
    int before = world->player.current_hp;
    for (int i = 0; i < ticks; i++) rc_world_tick(world);
    return (before - world->player.current_hp) / 10;
}

static void test_scripted_attacks(void) {
    RcWorld *world = make_world();
    RcPlayer *p = &world->player;
    int slot = rc_npc_spawn(world, g_def, 3201, 3200, 0);
    assert(slot >= 0);
    RcNpc *npc = &world->npcs[slot];
    npc->owner_controlled = true;

    RcNpcScriptedAttack hit = {.style = COMBAT_MAGIC, .min_hit = 10,
                               .max_hit = 10, .delay = 1,
                               .flags = RC_NPC_ATTACK_ALWAYS_ACCURATE,
                               .key = "Blast"};
    assert(rc_combat_npc_scripted_attack(world, npc, &hit) == 10);
    assert(land(world, 3) == 10);

    // The matching protection prayer at launch scales it.
    p->active_prayers |= PRAYER_PROTECT_MAGIC;
    hit.protected_pct = 50;
    assert(rc_combat_npc_scripted_attack(world, npc, &hit) == 5);
    hit.protected_pct = 0;
    assert(rc_combat_npc_scripted_attack(world, npc, &hit) == 0);
    p->active_prayers &= ~PRAYER_PROTECT_MAGIC;
    p->current_hp = p->max_hp;
    land(world, 3);

    // A tile-aimed attack still lands on the player it was rolled against.
    hit.flags |= RC_NPC_ATTACK_AT_TILE;
    hit.target_x = p->x;
    hit.target_y = p->y;
    p->current_hp = p->max_hp;
    assert(rc_combat_npc_scripted_attack(world, npc, &hit) == 10);
    assert(land(world, 3) == 10);

    // Mechanic damage skips accuracy and prayer.
    p->active_prayers |= PRAYER_PROTECT_MAGIC;
    p->current_hp = p->max_hp;
    assert(rc_combat_damage_player(world, 7, 0, COMBAT_MAGIC, npc->uid));
    assert(land(world, 2) == 7);

    // A rolled attack that lands never hits below its minimum.
    p->active_prayers = 0;
    RcNpcScriptedAttack floor_hit = {.style = COMBAT_MAGIC, .min_hit = 10,
                                     .max_hit = 10, .delay = 1, .key = "Blast"};
    bool landed = false;
    for (int i = 0; i < 60 && !landed; i++) {
        int dealt = rc_combat_npc_scripted_attack(world, npc, &floor_hit);
        assert(dealt == 0 || dealt == 10);
        landed = dealt == 10;
        p->num_pending_hits = 0;
        rc_world_tick(world);
    }
    assert(landed);
    // A full hit queue refuses the launch.
    p->num_pending_hits = RC_MAX_PENDING_HITS;
    assert(rc_combat_npc_scripted_attack(world, npc, &hit) == -1);
    p->num_pending_hits = 0;

    // A dead attacker cannot launch.
    npc->is_dead = true;
    assert(rc_combat_npc_scripted_attack(world, npc, &hit) == -1);
    rc_world_destroy(world);
}

int main(void) {
    test_content_ticks();
    test_choice_prompt();
    test_message_ring();
    test_varps();
    test_npc_overrides();
    test_scripted_attacks();
    printf("test_content_runtime_api: ok\n");
    return 0;
}
