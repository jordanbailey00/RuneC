#include "combat_hit.h"

#include "prayer.h"
#include <string.h>

int rc_combat_hit_delay_for_style(RcCombatStyle style) {
    switch (style) {
        case COMBAT_RANGED: return HIT_DELAY_RANGED;
        case COMBAT_MAGIC:  return HIT_DELAY_MAGIC;
        default:            return HIT_DELAY_MELEE;
    }
}

void rc_queue_hit(RcPendingHit *hits, int *count, int damage, int delay,
                  int style, int source_idx, uint32_t prayer_snapshot,
                  int world_tick) {
    rc_queue_hit_meta(hits, count, damage, delay, style, source_idx,
                      prayer_snapshot, world_tick, 0, damage, delay);
}

void rc_queue_hit_meta(RcPendingHit *hits, int *count, int damage, int delay,
                       int style, int source_idx, uint32_t prayer_snapshot,
                       int world_tick, uint8_t flags, int max_hit,
                       int client_delay) {
    if (*count >= RC_MAX_PENDING_HITS) return;
    RcPendingHit *h = &hits[*count];
    h->active = 1;
    h->damage = damage;
    h->max_hit = max_hit;
    h->ticks_remaining = delay;
    h->apply_tick = world_tick + delay;
    h->client_delay = client_delay;
    h->attack_style = style;
    h->source_idx = source_idx;
    h->prayer_snapshot = (int)prayer_snapshot;
    h->prayer_lock_tick = world_tick;
    h->hit_type = damage <= 0 ? RC_HIT_TYPE_MISS :
                  (max_hit > 0 && damage >= max_hit
                   ? RC_HIT_TYPE_MAX : RC_HIT_TYPE_NORMAL);
    h->flags = flags;
    (*count)++;
}

void rc_queue_hit_flags(RcPendingHit *hits, int *count, int damage, int delay,
                        int style, int source_idx, uint32_t prayer_snapshot,
                        int world_tick, uint8_t flags) {
    int before = *count;
    rc_queue_hit_meta(hits, count, damage, delay, style, source_idx,
                      prayer_snapshot, world_tick, flags, damage, delay);
    if (*count <= before) return;
}

int rc_combat_apply_protection(int damage, int style, uint32_t snapshot,
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
    return is_player_defender ? 0 : damage / 2;
}

int rc_combat_resolve_hit_damage(const RcPendingHit *hit,
                                 bool is_player_defender) {
    if (!hit || !hit->active) return 0;
    return rc_combat_apply_protection(hit->damage, hit->attack_style,
                                      (uint32_t)hit->prayer_snapshot,
                                      is_player_defender);
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
        int dmg = rc_combat_resolve_hit_damage(h, is_player_defender);
        total += dmg;
        h->active = 0;
    }

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

void rc_combat_actor_record_hit(RcCombatActorState *state, int damage,
                                int max_hit, int style, int source_uid,
                                uint8_t hit_type, uint8_t flags, int timer) {
    if (!state) return;
    if (timer <= 0) timer = 4;
    if (state->recent_hit_count < 0 || state->recent_hit_count > 4) {
        state->recent_hit_count = 0;
    }
    int idx = state->recent_hit_count;
    if (idx >= 4) {
        memmove(&state->recent_hits[0], &state->recent_hits[1],
                3 * sizeof(state->recent_hits[0]));
        idx = 3;
    } else {
        state->recent_hit_count++;
    }
    state->recent_hits[idx] = (RcCombatRecentHit){
        .damage = damage,
        .max_hit = max_hit,
        .style = style,
        .source_uid = source_uid,
        .timer = timer,
        .hit_type = hit_type,
        .flags = flags,
    };
}

void rc_combat_actor_tick_recent_hits(RcCombatActorState *state) {
    if (!state || state->recent_hit_count <= 0) return;
    int w = 0;
    for (int i = 0; i < state->recent_hit_count && i < 4; i++) {
        RcCombatRecentHit hit = state->recent_hits[i];
        if (hit.timer > 0) hit.timer--;
        if (hit.timer > 0) state->recent_hits[w++] = hit;
    }
    for (int i = w; i < 4; i++) {
        memset(&state->recent_hits[i], 0, sizeof(state->recent_hits[i]));
    }
    state->recent_hit_count = w;
}
