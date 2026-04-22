#include "combat.h"
#include "events.h"
#include "npc.h"
#include "prayer.h"
#include "rng.h"
#include "types.h"
#include <stddef.h>   // NULL

// ---- Hit chance (OSRS DPS formula) ------------------------------------

float rc_hit_chance(int att_roll, int def_roll) {
    if (att_roll > def_roll) {
        return 1.0f - ((float)(def_roll + 2) / (2.0f * (att_roll + 1)));
    }
    return (float)att_roll / (2.0f * (def_roll + 1));
}

// ---- Effective level helpers ------------------------------------------

// Player melee effective attack = floor((base + stance) * (1 + prayer/100)) + 8.
// Stance bonus hardcoded to Accurate (+3) for this pass; stance
// field will be added to RcPlayer when the UI lands (TODO #3).
static int eff_attack_melee(const RcPlayer *p) {
    int base = p->skills.boosted_level[SKILL_ATTACK];
    int stance = 3;                    // Accurate
    int prayer = rc_prayer_attack_bonus(p->active_prayers);
    return ((base + stance) * (100 + prayer)) / 100 + 8;
}

static int eff_strength_melee(const RcPlayer *p) {
    int base = p->skills.boosted_level[SKILL_STRENGTH];
    int stance = 0;                    // Accurate grants atk, not str
    int prayer = rc_prayer_strength_bonus(p->active_prayers);
    return ((base + stance) * (100 + prayer)) / 100 + 8;
}

static int eff_defence(const RcPlayer *p) {
    int base = p->skills.boosted_level[SKILL_DEFENCE];
    int stance = 0;
    int prayer = rc_prayer_defence_bonus(p->active_prayers);
    return ((base + stance) * (100 + prayer)) / 100 + 8;
}

static int eff_ranged_atk(const RcPlayer *p) {
    int base = p->skills.boosted_level[SKILL_RANGED];
    int stance = 3;                    // Accurate (Ranged)
    int prayer = 0;                    // Eagle Eye / Rigour → extend later
    return ((base + stance) * (100 + prayer)) / 100 + 8;
}

static int eff_ranged_str(const RcPlayer *p) {
    int base = p->skills.boosted_level[SKILL_RANGED];
    int stance = 0;
    int prayer = 0;
    return ((base + stance) * (100 + prayer)) / 100 + 8;
}

static int eff_magic_atk(const RcPlayer *p) {
    int base = p->skills.boosted_level[SKILL_MAGIC];
    int stance = 3;
    int prayer = 0;                    // Mystic Might / Augury → later
    return ((base + stance) * (100 + prayer)) / 100 + 8;
}

// NPC effective stats: flat +9 to everything.
static int npc_eff(int stat) { return stat + 9; }

// Map RcCombatStyle to the equipment_bonuses index.
static int atk_bonus_idx(RcCombatStyle style) {
    switch (style) {
        case COMBAT_MELEE_STAB:  return EQ_STAB_ATK;
        case COMBAT_MELEE_SLASH: return EQ_SLASH_ATK;
        case COMBAT_MELEE_CRUSH: return EQ_CRUSH_ATK;
        case COMBAT_RANGED:      return EQ_RANGED_ATK;
        case COMBAT_MAGIC:       return EQ_MAGIC_ATK;
        default:                 return EQ_CRUSH_ATK;
    }
}
static int def_bonus_idx(RcCombatStyle style) {
    switch (style) {
        case COMBAT_MELEE_STAB:  return EQ_STAB_DEF;
        case COMBAT_MELEE_SLASH: return EQ_SLASH_DEF;
        case COMBAT_MELEE_CRUSH: return EQ_CRUSH_DEF;
        case COMBAT_RANGED:      return EQ_RANGED_DEF;
        case COMBAT_MAGIC:       return EQ_MAGIC_DEF;
        default:                 return EQ_CRUSH_DEF;
    }
}

// ---- Player vs NPC calc ------------------------------------------------

RcCombatCalc rc_calc_melee(const RcPlayer *atk, int npc_def_id) {
    RcCombatCalc c = {0};
    if (npc_def_id < 0 || npc_def_id >= g_npc_def_count) return c;
    const RcNpcDef *d = &g_npc_defs[npc_def_id];

    int style_idx = atk_bonus_idx(atk->combat_style);
    int atk_bonus = atk->equipment_bonuses[style_idx];
    int str_bonus = atk->equipment_bonuses[EQ_STR];

    int eff_a = eff_attack_melee(atk);
    int eff_s = eff_strength_melee(atk);

    c.attack_roll = eff_a * (atk_bonus + 64);

    // NPC defence roll — pick the matching defence stat index. NPCs
    // use stats[1] for defence; OSRS NPCs have a per-style defence
    // bonus (0 for most). We default to +9 effective defence with
    // 0 style bonus unless extended.
    c.defence_roll = (d->stats[1] + 9) * (0 + 64);

    c.hit_chance = rc_hit_chance(c.attack_roll, c.defence_roll);
    c.max_hit = (eff_s * (str_bonus + 64)) / 640 + 1;   // +0.5 rounded
    return c;
}

RcCombatCalc rc_calc_ranged(const RcPlayer *atk, int npc_def_id) {
    RcCombatCalc c = {0};
    if (npc_def_id < 0 || npc_def_id >= g_npc_def_count) return c;
    const RcNpcDef *d = &g_npc_defs[npc_def_id];

    int atk_bonus = atk->equipment_bonuses[EQ_RANGED_ATK];
    int str_bonus = atk->equipment_bonuses[EQ_RANGED_STR];

    int eff_a = eff_ranged_atk(atk);
    int eff_s = eff_ranged_str(atk);

    c.attack_roll = eff_a * (atk_bonus + 64);
    c.defence_roll = (d->stats[1] + 9) * (0 + 64);
    c.hit_chance = rc_hit_chance(c.attack_roll, c.defence_roll);
    c.max_hit = (eff_s * (str_bonus + 64)) / 640 + 1;
    return c;
}

RcCombatCalc rc_calc_magic(const RcPlayer *atk, int npc_def_id,
                           int spell_max_hit) {
    RcCombatCalc c = {0};
    if (npc_def_id < 0 || npc_def_id >= g_npc_def_count) return c;
    const RcNpcDef *d = &g_npc_defs[npc_def_id];

    int atk_bonus = atk->equipment_bonuses[EQ_MAGIC_ATK];
    int magic_dmg = atk->equipment_bonuses[EQ_MAGIC_DMG];   // %

    int eff_a = eff_magic_atk(atk);

    c.attack_roll = eff_a * (atk_bonus + 64);
    // Magic defence uses NPC's magic level, not defence level.
    c.defence_roll = (d->stats[5] + 9) * (0 + 64);
    c.hit_chance = rc_hit_chance(c.attack_roll, c.defence_roll);
    // Magic max hit = spell base × (1 + magic_damage_bonus).
    c.max_hit = spell_max_hit + (spell_max_hit * magic_dmg) / 100;
    return c;
}

// ---- NPC vs Player calc ------------------------------------------------

// Map NPC attack_types bitfield → the style it uses. NPCs with
// multiple flags: prefer melee > ranged > magic (bosses often have
// all three; the dispatcher picks based on distance / mechanic).
static RcCombatStyle npc_preferred_style(int attack_types) {
    if (attack_types & 0x01) return COMBAT_MELEE_STAB;
    if (attack_types & 0x02) return COMBAT_MELEE_SLASH;
    if (attack_types & 0x04) return COMBAT_MELEE_CRUSH;
    if (attack_types & 0x10) return COMBAT_RANGED;
    if (attack_types & 0x08) return COMBAT_MAGIC;
    return COMBAT_MELEE_CRUSH;
}

RcCombatCalc rc_calc_npc_attack(int npc_def_id, const RcPlayer *def) {
    RcCombatCalc c = {0};
    if (npc_def_id < 0 || npc_def_id >= g_npc_def_count) return c;
    const RcNpcDef *d = &g_npc_defs[npc_def_id];

    RcCombatStyle style = npc_preferred_style(d->attack_types);
    int atk_stat = 0;
    switch (style) {
        case COMBAT_RANGED: atk_stat = d->stats[4]; break;
        case COMBAT_MAGIC:  atk_stat = d->stats[5]; break;
        default:            atk_stat = d->stats[0]; break;   // attack
    }

    // Simple NPC attack bonus — assume 0 until NDEF v3 exposes
    // per-style offensive bonuses.
    c.attack_roll = npc_eff(atk_stat) * (0 + 64);

    int def_bonus = def->equipment_bonuses[def_bonus_idx(style)];
    c.defence_roll = eff_defence(def) * (def_bonus + 64);
    c.hit_chance = rc_hit_chance(c.attack_roll, c.defence_roll);
    c.max_hit = d->max_hit;
    return c;
}

// ---- Roll + queue ------------------------------------------------------

int rc_roll_attack(const RcCombatCalc *calc, uint32_t *rng_state) {
    // Accuracy check: fractional hit_chance × 0x10000 → uniform roll.
    uint32_t roll = rc_rng_next(rng_state) & 0xFFFF;
    uint32_t threshold = (uint32_t)(calc->hit_chance * 65536.0f);
    if (roll >= threshold) return 0;           // miss
    // Damage: uniform [0, max_hit].
    return rc_rng_range(rng_state, calc->max_hit);
}

void rc_queue_hit(RcPendingHit *hits, int *count, int damage, int delay,
                  int style, int source_idx, uint32_t prayer_snapshot,
                  int world_tick) {
    if (*count >= RC_MAX_PENDING_HITS) return;
    RcPendingHit *h = &hits[*count];
    h->active = 1;
    h->damage = damage;
    h->ticks_remaining = delay;
    h->attack_style = style;
    h->source_idx = source_idx;
    h->prayer_snapshot = (int)prayer_snapshot;
    h->prayer_lock_tick = world_tick;
    (*count)++;
}

// ---- Pending-hit resolve ----------------------------------------------

static int apply_protection(int damage, int style, uint32_t snapshot,
                            bool is_player_defender) {
    uint32_t flag = 0;
    switch (style) {
        case COMBAT_MELEE_STAB:
        case COMBAT_MELEE_SLASH:
        case COMBAT_MELEE_CRUSH:
            flag = PRAYER_PROTECT_MELEE; break;
        case COMBAT_RANGED:
            flag = PRAYER_PROTECT_RANGE; break;
        case COMBAT_MAGIC:
            flag = PRAYER_PROTECT_MAGIC; break;
        default: break;
    }
    if (!(snapshot & flag)) return damage;
    // Player-side protection: full block.
    // NPC-side protection (boss overhead prayer): 50% reduction.
    // is_player_defender == true means player is praying (snapshot
    // is the player's prayer state).
    return is_player_defender ? 0 : damage / 2;
}

int rc_resolve_pending(RcPendingHit *hits, int *count,
                       bool is_player_defender) {
    int total = 0;
    for (int i = 0; i < *count; i++) {
        RcPendingHit *h = &hits[i];
        if (!h->active) continue;
        if (h->ticks_remaining > 0) {
            h->ticks_remaining--;
            continue;
        }
        int dmg = apply_protection(h->damage, h->attack_style,
                                   (uint32_t)h->prayer_snapshot,
                                   is_player_defender);
        total += dmg;
        h->active = 0;
    }
    // Compact — remove inactive entries by swapping with tail.
    int w = 0;
    for (int r = 0; r < *count; r++) {
        if (hits[r].active) {
            if (w != r) hits[w] = hits[r];
            w++;
        }
    }
    *count = w;
    return total;
}

// ---- Auto-attack tick --------------------------------------------------

// Chebyshev distance (tile-space range check).
static int chebyshev(int x1, int y1, int x2, int y2) {
    int dx = x1 > x2 ? x1 - x2 : x2 - x1;
    int dy = y1 > y2 ? y1 - y2 : y2 - y1;
    return dx > dy ? dx : dy;
}

// Default player attack range: 1 = melee, 7 = bow default. Per-weapon
// range will come from items.bin once equipment resolution lands.
static int player_attack_range(const RcPlayer *p) {
    switch (p->combat_style) {
        case COMBAT_RANGED: return 7;
        case COMBAT_MAGIC:  return 10;
        default:            return 1;
    }
}
static int hit_delay_for_style(RcCombatStyle style) {
    switch (style) {
        case COMBAT_RANGED: return HIT_DELAY_RANGED;
        case COMBAT_MAGIC:  return HIT_DELAY_MAGIC;
        default:            return HIT_DELAY_MELEE;
    }
}

static RcNpc *find_npc_by_uid(struct RcWorld *world, int uid) {
    for (int i = 0; i < world->npc_count; i++) {
        if (world->npcs[i].active && world->npcs[i].uid == uid) {
            return &world->npcs[i];
        }
    }
    return NULL;
}

void rc_combat_tick_player(struct RcWorld *world) {
    RcPlayer *p = &world->player;
    if (p->attack_timer > 0) p->attack_timer--;
    if (p->attack_target < 0) return;
    RcNpc *target = find_npc_by_uid(world, p->attack_target);
    if (!target || target->is_dead) {
        p->attack_target = -1;
        return;
    }
    // Range check.
    int range = player_attack_range(p);
    if (chebyshev(p->x, p->y, target->x, target->y) > range) return;
    if (p->attack_timer > 0) return;

    // Pick the calc based on current combat style.
    int def_idx = target->def_id;
    RcCombatCalc calc;
    switch (p->combat_style) {
        case COMBAT_RANGED:
            calc = rc_calc_ranged(p, def_idx); break;
        case COMBAT_MAGIC:
            // Spell max_hit provided by spellbook; default 13
            // (e.g. Fire Blast) until spell-selection UI lands.
            calc = rc_calc_magic(p, def_idx, 13); break;
        default:
            calc = rc_calc_melee(p, def_idx); break;
    }
    int dmg = rc_roll_attack(&calc, &world->rng_state);
    int delay = hit_delay_for_style(p->combat_style);

    // NPCs don't flick prayer (overhead prayer is static per phase),
    // so the snapshot is the NPC's phase prayer flags. For now,
    // NPC overhead prayer is not modelled — snapshot = 0.
    rc_queue_hit(target->pending_hits, &target->num_pending_hits,
                 dmg, delay, p->combat_style,
                 -1 /* player source */,
                 0u /* NPC prayer snapshot */, world->tick);

    // Reset attack timer. Default weapon speed 4 ticks until
    // items.bin equipment resolution is wired.
    p->attack_timer = 4;
}

void rc_combat_tick_npc(struct RcWorld *world, RcNpc *npc) {
    if (npc->is_dead || !npc->active) return;
    if (npc->attack_timer > 0) { npc->attack_timer--; return; }
    if (npc->target_uid < 0) return;
    // We only support targeting the player for now.
    RcPlayer *p = &world->player;
    if (npc->target_uid != 0 /* placeholder player uid */) return;

    const RcNpcDef *d = &g_npc_defs[npc->def_id];
    if (d->attack_speed <= 0 || d->max_hit <= 0) return;

    int range = 1;                     // melee default
    if (d->attack_types & 0x18) range = 7;   // ranged/magic
    if (chebyshev(npc->x, npc->y, p->x, p->y) > range) return;

    RcCombatCalc calc = rc_calc_npc_attack(npc->def_id, p);
    int dmg = rc_roll_attack(&calc, &world->rng_state);
    RcCombatStyle style = npc_preferred_style(d->attack_types);
    int delay = hit_delay_for_style(style);

    // Prayer snapshot at queue tick — per FC lesson memory, protection
    // prayer must be active NOW (queue tick) to block this hit, even
    // if the player turns it off before impact.
    rc_queue_hit(p->pending_hits, &p->num_pending_hits,
                 dmg, delay, style,
                 npc->uid, p->active_prayers, world->tick);

    npc->attack_timer = d->attack_speed;
}

// Resolve pending hits on the player. Fires RC_EVT_PLAYER_DAMAGED
// per landing hit so subsystems (encounter, prayer debuffs, etc.)
// can react to the source + style + final (post-protection) damage.
void rc_resolve_player_hits(struct RcWorld *world) {
    RcPlayer *p = &world->player;
    int total = 0;
    for (int i = 0; i < p->num_pending_hits; i++) {
        RcPendingHit *h = &p->pending_hits[i];
        if (!h->active) continue;
        if (h->ticks_remaining > 0) { h->ticks_remaining--; continue; }

        int dmg = apply_protection(h->damage, h->attack_style,
                                   (uint32_t)h->prayer_snapshot,
                                   true /* player defender */);
        total += dmg;

        RcPayloadPlayerDamaged payload = {
            .source_npc_id = h->source_idx >= 0
                             ? (uint16_t)h->source_idx
                             : 0xFFFFu,   // 0xFFFF = self / non-NPC source
            .damage = (uint16_t)(dmg & 0xFFFF),
            .style = (uint8_t)h->attack_style,
        };
        rc_event_fire(world, RC_EVT_PLAYER_DAMAGED, &payload);
        h->active = 0;
    }
    int w = 0;
    for (int r = 0; r < p->num_pending_hits; r++) {
        if (p->pending_hits[r].active) {
            if (w != r) p->pending_hits[w] = p->pending_hits[r];
            w++;
        }
    }
    p->num_pending_hits = w;

    if (total > 0) {
        p->current_hp -= total * 10;   // hp stored in tenths
        if (p->current_hp < 0) p->current_hp = 0;
    }
}
