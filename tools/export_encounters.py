#!/usr/bin/env python3
"""Compile encounter TOMLs into `data/defs/encounters.bin`.

Input:  `data/curated/encounters/*.toml` — 50 hand-authored encounter
        specs, schema described in `database.md`, with field catalog in
        `docs/encounter_primitives.md`.

Output: `data/defs/encounters.bin` with 'ENCT' magic. Loaded at
        `rc_world_create_config` time by `rc-core/encounter.c` when
        `RC_SUB_ENCOUNTER` is enabled.

Binary format (pass-2.11 — encounter presentation/state effects):
  magic u32 ('ENCT') | version u32 | encounter_count u32
  per encounter:
    slug_len u8 + slug[]
    npc_id_count u8 + (npc_id u32)[]
    attack_count u8
      per attack:
        name_len u8 + name[]
        style u8
        max_hit u16
        warning_ticks u8
        owner_npc_id u32      (0 = any NPC in spec)
        min_hit u16
        flags u32             (forced, prayer-ignorable, melee-range, room)
        effect_id u8
        effect_min u8
        effect_max u8
        effect_pct u8
        effect_flags u8
    phase_count u8
      per phase:
        id_len u8 + id[]
        enter_at_hp_pct u8     (0 = hard hp=0 trigger; 100 = fight start)
        hard_hp_trigger u8     (1 if `enter_at_hp = 0`)
        script_len u8 + script[]
        allowed_attack_mask u32
        adjacent_style_weights u8[8]
        distant_style_weights u8[8]
        player_targetable u8
        allowed_attack_mask_explicit u8
    mechanic_count u8
      per mechanic:
        name_len u8 + name[]
        primitive_id u8        (enum — see PRIMITIVE_IDS)
        period_ticks u16
        trigger_type u8        (0=periodic, 1=phase_enter,
                                2=phase_exit, 3=phase_in, 4=after_attack,
                                5=during_mechanic, 6=attack_counter,
                                7=event, 255=none)
        phase_idx u8           (index into encounter phases, or 255)
        phase_mask u32         (union phase bindings, or 0)
        trigger_ref_len u8 + trigger_ref[]
        param_block[64]        (opaque — primitive-specific struct)
    protection_count u8
      per protection:
        attack_idx u8
        style u8
        prayer_flag u32
        damage_pct u8
    damage_modifier_count u8
      per modifier:
        owner_npc_id u32       (0 = any NPC in spec)
        condition u8           (enum — supported generic predicates)
        style u8               (RcCombatStyle for style predicates)
        damage_pct u8
        flags u8

Still not consumed in pass 2 (pass 3+ work):
  - raid `[[rooms]]`, wave `[[waves]]` progression (skipped)
  - variant_override
  - Zulrah form-specific damage modifiers
  - encounter_type flag (skipping skilling bosses)
  - rotations (tick-driven phases — Zulrah)
  - exact persistent object/arena state for mechanics whose current
    generic primitive only owns first runtime behavior
"""
from __future__ import annotations

import re
import struct
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

CURATED = ROOT / "data/curated/encounters"
OUT = ROOT / "data/defs/encounters.bin"
REPORT = ROOT / "tools/reports/encounters.txt"

ENCT_MAGIC = 0x54434E45   # 'ENCT'
ENCT_VERSION = 12

TRIGGER_PERIODIC = 0
TRIGGER_PHASE_ENTER = 1
TRIGGER_PHASE_EXIT = 2
TRIGGER_PHASE_IN = 3
TRIGGER_AFTER_ATTACK = 4
TRIGGER_DURING_MECH = 5
TRIGGER_ATTACK_COUNT = 6
TRIGGER_EVENT = 7
TRIGGER_NONE = 255

# Style enum — mirrors rc-core/types.h RcCombatStyle.
STYLE = {
    "": 0, "none": 0,
    "melee": 3,            # default: crush
    "melee_stab": 1, "stab": 1,
    "melee_slash": 2, "slash": 2,
    "melee_crush": 3, "crush": 3,
    "ranged": 4,
    "magic": 5,
    # Custom-style aliases from various boss TOMLs — normalise to
    # their closest base style so combat math still resolves.
    "dragonfire": 5,
    "dragonfire_special": 5,
    "dragonfire_aoe": 5,
    "magic_aoe_tile": 5,
    "magical_ranged": 4,
    "melee_tile_slam": 3,
    "ranged_aoe_room": 4,
    "random": 0,
    "alternating": 0,
}

WEIGHT_STYLE = {
    "melee": 3,
    "crush": 3,
    "melee_crush": 3,
    "slash": 2,
    "melee_slash": 2,
    "stab": 1,
    "melee_stab": 1,
    "ranged": 4,
    "range": 4,
    "magic": 5,
    "mage": 5,
    "fire_breath": 5,
    "special_breath": 5,
    "shockwave": 4,
}

PRAYER_FLAGS = {
    "protect_from_magic": 1 << 16,
    "protect_from_missiles": 1 << 17,
    "protect_from_range": 1 << 17,
    "protect_from_ranged": 1 << 17,
    "protect_from_melee": 1 << 18,
}

ATTACK_FLAGS = {
    "forced_hit": 1 << 0,
    "prayer_ignorable": 1 << 1,
    "requires_player_in_melee_range": 1 << 2,
    "hits_all_players_in_room": 1 << 3,
}

ATTACK_EFFECT_NONE = 0
ATTACK_EFFECT_DRAIN_HEAL = 1
ATTACK_EFFECT_SPLIT_PROJECTILES = 2
ATTACK_EFFECT_DRAIN_PRAYER_PCT = 3
ATTACK_EFFECT_POISON = 4
ATTACK_EFFECT_KNOCKBACK = 5
ATTACK_EFFECT_VENOM = 6
ATTACK_EFFECT_DEACTIVATE_PRAYERS = 7
ATTACK_EFFECT_DRAIN_MAGIC = 1 << 0
ATTACK_EFFECT_DRAIN_PRAYER = 1 << 1
ATTACK_EFFECT_POISON_PIERCES_PRAYER = 1 << 2

DAMAGE_MODIFIER_CONDITIONS = {
    "style_not": 1,
    "not_corpbane_stab": 2,
    "melee_not_halberd_salamander": 3,
    "cap_range": 4,
    "style_is": 5,
    "melee_not_halberd": 6,
    "player_not_facemask": 7,
}

# Primitive-name → enum id mapping. Matches the table in
# docs/encounter_primitives.md. Unknown primitives → 0
# (no-op) so the binary stays loadable even for mechanics the
# engine doesn't yet implement.
PRIMITIVE_IDS = {
    "": 0,
    "telegraphed_aoe_tile":                    1,
    "spawn_npcs":                              2,
    "spawn_npcs_once":                         3,
    "heal_at_object":                          4,
    "periodic_heal_boss":                      5,
    "drain_prayer_on_hit":                     6,
    "chain_magic_to_nearest_player":           7,
    "preserve_stat_drains_across_transition":  8,
    "teleport_on_incoming_attack":             9,
    "teleport_player_nearby":                 10,
    "unequip_player_items":                   11,
    "positional_aoe":                         12,
    "spawn_leech_npc":                        13,
    "regen_when_no_player":                   14,
    "attack_counter_special":                 15,
    "spawn_soul_attackers":                   16,
    "dot_tile_placement":                     17,
    "static_dot_line":                        18,
    "spawn_hidden_minions":                   19,
    "phase_advance_on_condition":             20,
    "group_kill_required":                    21,
    "periodic_respawn_if_dead":               22,
    "form_transition_dive":                   23,
    "attack_counter_alternate_special":       24,
    "covered_arena_environment":              25,
    "damage_reduction_until_mechanic_triggered": 26,
    "animation_warning_style_swap":           27,
    "converging_aoe":                         28,
    "stun_then_fire_walls":                   29,
    "moving_dot_line":                        30,
    "object_interaction_ticked":              31,
    "totem_charge_progression":               32,
    "telegraphed_portal_aoe":                 33,
    "spawn_paired_husks":                     34,
    "quadrant_safe_zone_dot":                 35,
    "shuffle_player_prayers":                 36,
    "infectious_dot_with_cure":               37,
    "spawn_convergent_minions":               38,
    "aoe_tile_debuff":                        39,
    "line_dash":                              40,
    "spawn_objective_npcs":                   41,
    "npc_pathed_movement":                    42,
    "spawn_wall_tentacles":                   43,
    "teleporting_tile_aoe":                   44,
    "homing_projectiles_with_walls":          45,
    "smite_drain_shield":                     46,
    "periodic_spike_cluster":                 47,
    "moving_rotational_hazards":              48,
    "heal_boss_on_player_attack_miss":        49,
    "tile_debuff_on_stand":                   50,
    "spawn_buff_zone_npc":                    51,
    "one_shot_arena_effect":                  52,
    "player_sanity_tracker":                  53,
    "spawn_tentacle_projectiles":             54,
    "aoe_prayer_swap_demand":                 55,
    "audio_visual_disruption":                56,
    "object_item_interaction":                57,
    "passive_heal_during_phase":              58,
    "attack_counter_style_swap":              59,
    "spawn_tracking_tornadoes":               60,
    "forced_prayer_switch_on_style_swap":     61,
    "hp_gated_style_increase":                62,
    "run_level_modifier_registry":            63,
    "line_of_sight_shield":                   64,
    "hp_gated_healer_spawn":                  65,
    "destructible_pillars":                   66,
    "wave_end_minion":                        67,
    "multi_limb_boss":                        68,
    "player_position_swap":                   69,
    "environmental_wall_spawn":               70,
    "tile_telegraph_lightning":               71,
    "continuous_heal_unless_interrupted":     72,
    "one_shot_weapon_provided":               73,
    "spawn_web_tiles":                        74,
    "spawn_colored_nylocas":                  75,
    "persistent_dot_tile_pool":               76,
    "obelisk_dps_check":                      77,
    "spawn_energized_pylons":                 78,
    "periodic_death_tile_wave":               79,
    "surviving_boss_enrage":                  80,
    "heal_altars_player_must_disable":        81,
    "interactive_environment_object":         82,
    "crafting_resource_loop":                 83,
    "interactive_resource_nodes":             84,
    "interactive_object_with_feed":           85,
    "periodic_water_rise":                    86,
    "periodic_object_damage_event":           87,
    "spawn_ally_npcs":                        88,
    "periodic_tile_damage_all_players":       89,
    "periodic_telegraphed_snowballs":         90,
    "damage_reduction_until_vented":          91,
}

EQUIP_SLOTS = {
    "head": 0,
    "cape": 1,
    "amulet": 2,
    "weapon": 3,
    "body": 4,
    "shield": 5,
    "legs": 6,
    "gloves": 7,
    "hands": 7,
    "boots": 8,
    "feet": 8,
    "ring": 9,
    "ammo": 10,
}


def pack_short(s: str, maxlen: int = 255) -> bytes:
    return (s or "").encode("utf-8", errors="replace")[:maxlen]


PARAM_BLOCK_SIZE = 64


def pack_cstr(s: str, size: int) -> bytes:
    b = (s or "").encode("utf-8", errors="replace")[: size - 1]
    return b + b"\x00" * (size - len(b))


def slot_mask_from_names(names) -> int:
    mask = 0
    for name in names or []:
        idx = EQUIP_SLOTS.get(str(name).strip().lower())
        if idx is not None:
            mask |= (1 << idx)
    return mask


def style_code(name: str) -> int:
    return STYLE.get(str(name or "").strip().lower(), 0)


def pct(value) -> int:
    return max(0, min(100, round(float(value or 0) * 100)))


def pct_u8(value) -> int:
    return max(0, min(255, round(float(value or 0) * 100)))


def prayer_condition_style(condition: str) -> int:
    c = str(condition or "").strip().lower()
    if c == "target_praying:protect_from_melee":
        return STYLE["melee"]
    if c in ("target_praying:protect_from_missiles",
             "target_praying:protect_from_range",
             "target_praying:protect_from_ranged"):
        return STYLE["ranged"]
    if c == "target_praying:protect_from_magic":
        return STYLE["magic"]
    return 0


def pack_param_block(primitive_id: int, params: dict, trigger_text: str = "") -> bytes:
    """Pack primitive-specific param struct to a fixed 64-byte block.
    Layouts must match the packed structs in rc-core/encounter.h."""
    if params is None:
        params = {}
    body = b""
    if primitive_id == 1:   # telegraphed_aoe_tile
        body = struct.pack(
            "<HHHBBB",
            int(params.get("damage_min", 0)) & 0xFFFF,
            int(params.get("damage_max", 0)) & 0xFFFF,
            int(params.get("solo_damage_max", 0)) & 0xFFFF,
            int(params.get("warning_ticks", 0)) & 0xFF,
            int(params.get("extra_random_tiles", 0)) & 0xFF,
            1 if params.get("target_current_tile") else 0,
        )
    elif primitive_id in (2, 3):  # spawn_npcs, spawn_npcs_once
        body = pack_cstr(str(params.get("npc_name", "")), 32) + struct.pack(
            "<BBBBB",
            int(params.get("count", 0)) & 0xFF,
            1 if params.get("persist_after_death") else 0,
            int(params.get("heals_boss_on_contact", 0)) & 0xFF,
            1 if params.get("blocks_boss_damage_until_dead") else 0,
            int(params.get("freeze_player_ticks", 0)) & 0xFF,
        )
    elif primitive_id == 5:  # periodic_heal_boss
        alive_npc = ""
        if params.get("requires_npc_alive"):
            alive_npc = str(params.get("requires_npc_alive") or "").strip()
        elif trigger_text.startswith("while_alive:"):
            alive_npc = trigger_text.split(":", 1)[1].strip()
        body = pack_cstr(alive_npc, 32) + struct.pack(
            "<B",
            int(params.get("heal_per_tick", 0)) & 0xFF,
        )
    elif primitive_id == 4:  # heal_at_object
        body = struct.pack(
            "<BBB",
            int(params.get("heal_per_player", 0)) & 0xFF,
            int(params.get("heal_ticks_cap", 0)) & 0xFF,
            int(params.get("tick_period", 0)) & 0xFF,
        )
    elif primitive_id == 6:  # drain_prayer_on_hit
        body = struct.pack(
            "<BB",
            int(params.get("points", 0)) & 0xFF,
            1 if params.get("spectral_shield_mitigation") else 0,
        )
    elif primitive_id == 7:  # chain_magic_to_nearest_player
        body = struct.pack("<B", int(params.get("max_bounces", 0)) & 0xFF)
    elif primitive_id == 8:  # preserve_stat_drains_across_transition
        body = struct.pack("<B", 1)
    elif primitive_id == 9:  # teleport_on_incoming_attack
        band = params.get("hp_pct_band") or [0, 100]
        chance = float(params.get("chance_per_incoming", 0.0))
        body = struct.pack(
            "<BBBBBBB",
            int(band[0] if len(band) > 0 else 0) & 0xFF,
            int(band[1] if len(band) > 1 else 100) & 0xFF,
            max(0, min(100, round(chance * 100))) & 0xFF,
            1 if params.get("exclude_splashed_magic") else 0,
            1 if params.get("exclude_poison_dot") else 0,
            1 if params.get("extinguishes_light") else 0,
            1 if params.get("drops_aggression") else 0,
        )
    elif primitive_id == 10:  # teleport_player_nearby
        body = struct.pack(
            "<BBB",
            int(params.get("min_distance", 0)) & 0xFF,
            int(params.get("max_distance", 0)) & 0xFF,
            1 if params.get("constrain_to_arena") else 0,
        )
    elif primitive_id == 11:  # unequip_player_items
        body = struct.pack(
            "<BBH",
            int(params.get("count", 0)) & 0xFF,
            1 if params.get("weapon_priority") else 0,
            slot_mask_from_names(params.get("affects_slots")) & 0xFFFF,
        )
    elif primitive_id == 12:  # positional_aoe
        body = struct.pack(
            "<HHB",
            int(params.get("damage_min", 0)) & 0xFFFF,
            int(params.get("damage_max", 0)) & 0xFFFF,
            1 if params.get("prayer_ignorable") else 0,
        )
    elif primitive_id == 13:  # spawn_leech_npc
        body = pack_cstr(str(params.get("npc_name", "")), 32) + struct.pack(
            "<BBBHHHBB",
            int(params.get("leech_per_tick", 0)) & 0xFF,
            1 if params.get("heals_boss_with_leeched") else 0,
            1 if params.get("poison_weak") else 0,
            int(params.get("spawn_cooldown_ticks", 0)) & 0xFFFF,
            int(params.get("spawn_on_hit_min_damage", 0)) & 0xFFFF,
            int(params.get("spawn_when_boss_hp_below", 0)) & 0xFFFF,
            int(params.get("spawn_chance_denominator", 0)) & 0xFF,
            int(params.get("poisoned_leech_period_ticks", 0)) & 0xFF,
        )
    elif primitive_id == 15:  # attack_counter_special
        seq = list(params.get("sequence") or [])[:4]
        styles = [0] * 4
        delays = [0] * 4
        for i, row in enumerate(seq):
            styles[i] = style_code(row.get("style") if isinstance(row, dict) else "")
            delays[i] = int(row.get("tick_delay", 0) if isinstance(row, dict) else 0)
        body = struct.pack(
            "<HBBBBB4B4BBBBHBB",
            int(params.get("every_n_attacks", 0)) & 0xFFFF,
            1 if params.get("first_attack_trigger") else 0,
            int(params.get("min_hit", 0)) & 0xFF,
            int(params.get("max_hit", 0)) & 0xFF,
            style_code(params.get("style", "")),
            len(seq) & 0xFF,
            *(x & 0xFF for x in styles),
            *(x & 0xFF for x in delays),
            prayer_condition_style(params.get("condition", "")) & 0xFF,
            1 if params.get("prayer_ignorable") else 0,
            int(params.get("prayer_drain_pct", 0)) & 0xFF,
            int(params.get("max_hp", 0)) & 0xFFFF,
            pct(params.get("skip_chance")) & 0xFF,
            int(params.get("priority", 255)) & 0xFF,
        )
    elif primitive_id == 16:  # spawn_soul_attackers
        styles = [style_code(s) for s in (params.get("styles") or [])[:3]]
        body = struct.pack(
            "<HHHBBBBBBBBBBBH",
            int(params.get("every_n_attacks", 0)) & 0xFFFF,
            int(params.get("min_hp", 0)) & 0xFFFF,
            int(params.get("max_hp", 0)) & 0xFFFF,
            len(styles) & 0xFF,
            styles[0] if len(styles) > 0 else 0,
            styles[1] if len(styles) > 1 else 0,
            styles[2] if len(styles) > 2 else 0,
            int(params.get("damage_if_unblocked", 0)) & 0xFF,
            int(params.get("prayer_drain_if_blocked", 0)) & 0xFF,
            pct(params.get("skip_chance")) & 0xFF,
            int(params.get("priority", 255)) & 0xFF,
            int(params.get("prayer_drain_with_ward_of_arceuus", 0)) & 0xFF,
            int(params.get("prayer_drain_with_spectral_shield", 0)) & 0xFF,
            int(params.get("prayer_drain_with_both", 0)) & 0xFF,
            int(params.get("spectral_shield_item_id", 12821)) & 0xFFFF,
        )
    elif primitive_id == 17:  # dot_tile_placement
        body = struct.pack(
            "<HHBBBBBBBB",
            int(params.get("every_n_attacks", 0)) & 0xFFFF,
            int(params.get("max_hp", 0)) & 0xFFFF,
            int(params.get("pool_count", 0)) & 0xFF,
            int(params.get("pools_on_player", 0)) & 0xFF,
            int(params.get("dot_per_tick", 0)) & 0xFF,
            pct(params.get("skip_chance")) & 0xFF,
            int(params.get("priority", 255)) & 0xFF,
            int(params.get("pools_random", 0)) & 0xFF,
            int(params.get("duration_ticks", 10)) & 0xFF,
            1 if params.get("dragonfire_protection_applies") else 0,
        )
    elif primitive_id == 18:  # static_dot_line
        body = struct.pack(
            "<B",
            int(params.get("damage_per_cross", 0)) & 0xFF,
        )
    elif primitive_id == 19:  # spawn_hidden_minions
        body = pack_cstr(str(params.get("npc_name", "")), 32)
        body += pack_cstr(str(params.get("hidden_object_name", "")), 24)
        body += struct.pack(
            "<BB",
            int(params.get("count", 0)) & 0xFF,
            int(params.get("tentacle_max_hit", 0)) & 0xFF,
        )
    elif primitive_id == 20:  # phase_advance_on_condition
        condition = str(params.get("condition", ""))
        npc_name = ""
        if condition.startswith("all_npcs_dead:"):
            npc_name = condition.split(":", 1)[1].strip()
        body = pack_cstr(npc_name, 32) + pack_cstr(
            str(params.get("advance_to", "")), 24)
    elif primitive_id == 21:  # group_kill_required
        ids = [int(x) for x in params.get("required_npc_ids", [])[:8]]
        body = struct.pack("<B", len(ids))
        for i in range(8):
            body += struct.pack("<I", ids[i] if i < len(ids) else 0)
        drop_ids = [int(x) for x in params.get("grant_drops_for", [])[:8]]
        body += struct.pack("<B", len(drop_ids))
        for i in range(8):
            body += struct.pack("<H", drop_ids[i] if i < len(drop_ids) else 0)
    elif primitive_id == 22:  # periodic_respawn_if_dead
        ids = [int(x) for x in params.get("applies_to_npc_ids", [])[:8]]
        body = struct.pack(
            "<HB",
            int(params.get("respawn_cooldown_ticks", 0)) & 0xFFFF,
            len(ids),
        )
        for i in range(8):
            body += struct.pack("<I", ids[i] if i < len(ids) else 0)
    elif primitive_id == 23:  # form_transition_dive
        body = struct.pack(
            "<BBBB",
            int(params.get("dive_ticks", 0)) & 0xFF,
            int(params.get("resurface_ticks", 0)) & 0xFF,
            1 if params.get("untargetable_during_dive") else 0,
            1 if params.get("in_flight_attacks_still_resolve") else 0,
        )
    elif primitive_id == 24:  # attack_counter_alternate_special
        specials = [str(x) for x in params.get("specials", [])[:2]]
        every = int(params.get("standard_attacks_between", 0)) + 1
        body = struct.pack("<BB", every & 0xFF, len(specials) & 0xFF)
        for i in range(2):
            body += pack_cstr(specials[i] if i < len(specials) else "", 24)
    elif primitive_id == 25:  # covered_arena_environment
        body = struct.pack(
            "<HBBBB",
            int(params.get("duration_ticks", 0)) & 0xFFFF,
            int(params.get("dot_per_tick", 0)) & 0xFF,
            1 if params.get("safe_path_navigation") else 0,
            int(params.get("pool_count", 10)) & 0xFF,
            int(params.get("damage_reduction_pct", 0)) & 0xFF,
        )
    elif primitive_id == 26:  # damage_reduction_until_mechanic_triggered
        clear_npc = ""
        if str(params.get("clear_on") or "").strip().lower() == "all_vines_dead":
            clear_npc = str(params.get("clear_npc_name") or "Thorny vine")
        else:
            clear_npc = str(params.get("clear_npc_name") or "")
        body = struct.pack(
            "<BB",
            pct_u8(params.get("damage_reduction_pct")) & 0xFF,
            pct_u8(params.get("increase_damage_pct")) & 0xFF,
        ) + pack_cstr(clear_npc, 32)
    elif primitive_id == 27:  # animation_warning_style_swap
        body = struct.pack(
            "<HB",
            int(params.get("every_n_attacks", 0)) & 0xFFFF,
            int(params.get("warning_animation_ticks", 0)) & 0xFF,
        )
    elif primitive_id == 28:  # converging_aoe
        body = struct.pack(
            "<BBB",
            int(params.get("bolt_count", 0)) & 0xFF,
            int(params.get("damage_per_bolt", 0)) & 0xFF,
            int(params.get("warning_ticks", 2)) & 0xFF,
        )
    elif primitive_id == 29:  # stun_then_fire_walls
        body = struct.pack(
            "<BBB",
            int(params.get("stun_ticks", 0)) & 0xFF,
            int(params.get("fire_wall_damage", 0)) & 0xFF,
            int(params.get("duration_ticks", 8)) & 0xFF,
        )
    elif primitive_id == 30:  # moving_dot_line
        body = struct.pack(
            "<BBBB",
            int(params.get("trail_length", 0)) & 0xFF,
            int(params.get("dot_per_tick", 0)) & 0xFF,
            int(params.get("duration_ticks", 8)) & 0xFF,
            1 if params.get("slows_player") else 0,
        )
    elif primitive_id == 31:  # object_interaction_ticked
        objects = params.get("objects") or []
        for i in range(3):
            name = ""
            if i < len(objects) and isinstance(objects[i], dict):
                name = str(objects[i].get("name", ""))
            body += pack_cstr(name, 18)
        body += struct.pack(
            "<B",
            int(params.get("activation_ticks", 0)) & 0xFF,
        )
    elif primitive_id == 32:  # totem_charge_progression
        body = struct.pack(
            "<BBBH",
            int(params.get("totem_count", 0)) & 0xFF,
            int(params.get("charge_per_attack", 0)) & 0xFF,
            int(params.get("total_charge_per_totem", 0)) & 0xFF,
            int(params.get("advance_damage_per_full_charge", 0)) & 0xFFFF,
        )
    elif primitive_id == 33:  # telegraphed_portal_aoe
        body = struct.pack(
            "<BBBB",
            int(params.get("portal_count_extra", 0)) & 0xFF,
            int(params.get("warning_ticks", 0)) & 0xFF,
            int(params.get("damage_max", 0)) & 0xFF,
            1 if params.get("prayer_ignorable") else 0,
        )
    elif primitive_id == 34:  # spawn_paired_husks
        body = struct.pack(
            "<BBBB",
            int(params.get("pair_count", 0)) & 0xFF,
            style_code(params.get("husk_blue_style", "")) & 0xFF,
            style_code(params.get("husk_green_style", "")) & 0xFF,
            1 if params.get("immobilizes_target") else 0,
        )
    elif primitive_id == 35:  # quadrant_safe_zone_dot
        body = struct.pack(
            "<BBBB",
            int(params.get("safe_quadrants", 0)) & 0xFF,
            int(params.get("unsafe_dot_per_tick", 0)) & 0xFF,
            max(0, min(255, round(float(params.get("heals_boss_multiplier", 0)) * 10))) & 0xFF,
            int(params.get("duration_ticks", 12)) & 0xFF,
        )
    elif primitive_id == 36:  # shuffle_player_prayers
        body = struct.pack(
            "<B",
            int(params.get("duration_attacks", 0)) & 0xFF,
        )
    elif primitive_id == 37:  # infectious_dot_with_cure
        body = struct.pack(
            "<BBBBB",
            int(params.get("incubation_ticks", 0)) & 0xFF,
            int(params.get("burst_damage", 0)) & 0xFF,
            int(params.get("burst_damage_if_cured", 0)) & 0xFF,
            int(params.get("parasite_hp_uncured", 0)) & 0xFF,
            int(params.get("parasite_hp_cured", 0)) & 0xFF,
        )
    elif primitive_id == 38:  # spawn_convergent_minions
        body = pack_cstr(str(params.get("npc_name", "")), 32) + struct.pack(
            "<BBBB",
            int(params.get("count_per_player", 0)) & 0xFF,
            int(params.get("absorb_damage_per_walker", 0)) & 0xFF,
            1 if params.get("power_blast_on_absorption") else 0,
            int(params.get("power_blast_min", 0)) & 0xFF,
        )
    elif primitive_id == 39:  # aoe_tile_debuff
        debuff = params.get("debuff") if isinstance(params.get("debuff"), dict) else {}
        body = struct.pack(
            "<BBBB",
            int(params.get("aoe_size", 0)) & 0xFF,
            int(params.get("debuff_ticks", 0)) & 0xFF,
            int(debuff.get("attack_speed_penalty", 0)) & 0xFF,
            1 if debuff.get("run_disabled") else 0,
        )
    elif primitive_id == 40:  # line_dash
        body = struct.pack("<B", int(params.get("damage_max", 0)) & 0xFF)
    elif primitive_id == 41:  # spawn_objective_npcs
        body = pack_cstr(str(params.get("npc_name", "")), 32)
        body += pack_cstr(str(params.get("advance_to", "")), 24)
        body += struct.pack(
            "<BB",
            int(params.get("count", 0)) & 0xFF,
            int(params.get("fallback_ticks", 20)) & 0xFF,
        )
    elif primitive_id == 42:  # npc_pathed_movement
        body = pack_cstr(str(params.get("advance_to", "")), 24)
        body += struct.pack(
            "<H",
            int(params.get("ticks_to_complete", 0)) & 0xFFFF,
        )
    elif primitive_id == 43:  # spawn_wall_tentacles
        body = pack_cstr(str(params.get("npc_name", "Abyssal tentacle")), 32)
        body += pack_cstr(str(params.get("advance_to", "")), 24)
        body += struct.pack(
            "<BB",
            int(params.get("tentacle_count", 0)) & 0xFF,
            int(params.get("fallback_ticks", 20)) & 0xFF,
        )
    elif primitive_id == 44:  # teleporting_tile_aoe
        body = struct.pack(
            "<BBBB",
            int(params.get("cloud_count", 0)) & 0xFF,
            int(params.get("damage_max", 0)) & 0xFF,
            1 if params.get("safe_tile_exists") else 0,
            1 if params.get("boss_untargetable_during") else 0,
        )
    elif primitive_id == 45:  # homing_projectiles_with_walls
        rng = params.get("spike_count_range") or [0, 0]
        body = struct.pack(
            "<BBBB",
            int(rng[0] if len(rng) > 0 else 0) & 0xFF,
            int(rng[1] if len(rng) > 1 else 0) & 0xFF,
            int(params.get("harden_ticks", 0)) & 0xFF,
            int(params.get("damage_on_hit", 0)) & 0xFF,
        )
    elif primitive_id == 46:  # smite_drain_shield
        body = struct.pack(
            "<HBB",
            int(params.get("shield_hp", 0)) & 0xFFFF,
            int(params.get("hits_to_drain", 0)) & 0xFF,
            int(params.get("sapphire_bolt_bonus_drain", 0)) & 0xFF,
        )
    elif primitive_id == 47:  # periodic_spike_cluster
        body = struct.pack(
            "<HBBB",
            int(params.get("every_n_attacks", 0)) & 0xFFFF,
            int(params.get("cluster_tiles", 0)) & 0xFF,
            1 if params.get("one_on_player") else 0,
            int(params.get("damage_on_hit", 0)) & 0xFF,
        )
    elif primitive_id == 48:  # moving_rotational_hazards
        body = struct.pack(
            "<BBBB",
            int(params.get("axe_count", 0)) & 0xFF,
            int(params.get("orbit_radius", 0)) & 0xFF,
            int(params.get("rotation_ticks_per_full_circle", 0)) & 0xFF,
            int(params.get("damage_on_hit", 0)) & 0xFF,
        )
    elif primitive_id == 49:  # heal_boss_on_player_attack_miss
        body = struct.pack(
            "<BBB",
            int(params.get("warning_ticks", 0)) & 0xFF,
            int(params.get("heal_per_player_attack", 0)) & 0xFF,
            1 if params.get("cancel_player_attack") else 0,
        )
    elif primitive_id == 50:  # tile_debuff_on_stand
        debuff = params.get("debuff") if isinstance(params.get("debuff"), dict) else {}
        body = struct.pack(
            "<BBb",
            int(params.get("tile_count", 0)) & 0xFF,
            int(params.get("damage_on_step", 0)) & 0xFF,
            max(-128, min(127, int(debuff.get("amount", 0)))),
        )
    elif primitive_id == 51:  # spawn_buff_zone_npc
        inside = params.get("buffs_inside") if isinstance(params.get("buffs_inside"), dict) else {}
        outside = params.get("debuffs_outside") if isinstance(params.get("debuffs_outside"), dict) else {}
        body = pack_cstr(str(params.get("npc_name", "")), 32) + struct.pack(
            "<BBB",
            int(params.get("buff_aoe_size", 0)) & 0xFF,
            int(inside.get("minimum_damage_pct", 0)) & 0xFF,
            pct_u8(outside.get("player_damage_scale", 1.0)) & 0xFF,
        )
    elif primitive_id == 52:  # one_shot_arena_effect
        body = struct.pack(
            "<BB",
            int(params.get("tile_count", 0)) & 0xFF,
            int(params.get("damage_max", 0)) & 0xFF,
        )
    elif primitive_id == 53:  # player_sanity_tracker
        body = struct.pack(
            "<BBBBBB",
            int(params.get("max_sanity", 0)) & 0xFF,
            int(params.get("drain_per_tentacle_hit", 0)) & 0xFF,
            int(params.get("drain_per_wrong_prayer_tick", 0)) & 0xFF,
            int(params.get("regen_per_correct_prayer_tick", 0)) & 0xFF,
            int(params.get("insane_threshold", 0)) & 0xFF,
            int(params.get("restored_on_boss_death", 0)) & 0xFF,
        )
    elif primitive_id == 54:  # spawn_tentacle_projectiles
        body = struct.pack(
            "<BBBB",
            int(params.get("tentacle_count", 0)) & 0xFF,
            int(params.get("warning_ticks", 0)) & 0xFF,
            int(params.get("damage_on_hit", 0)) & 0xFF,
            int(params.get("sanity_drain_on_hit", 0)) & 0xFF,
        )
    elif primitive_id == 55:  # aoe_prayer_swap_demand
        body = struct.pack(
            "<BBB",
            int(params.get("warning_ticks", 0)) & 0xFF,
            int(params.get("wrong_prayer_damage", 0)) & 0xFF,
            int(params.get("wrong_prayer_sanity_drain", 0)) & 0xFF,
        )
    elif primitive_id == 56:  # audio_visual_disruption
        body = struct.pack(
            "<BBB",
            int(params.get("duration_ticks", 0)) & 0xFF,
            1 if params.get("disables_audio_cues") else 0,
            1 if params.get("hides_overhead_warning") else 0,
        )
    elif primitive_id == 57:  # object_item_interaction
        body = pack_cstr(str(params.get("advance_to", "")), 24) + struct.pack(
            "<BBB",
            int(params.get("wrong_herb_damage", 0)) & 0xFF,
            int(params.get("wrong_herb_delays_wake_ticks", 0)) & 0xFF,
            int(params.get("correct_herbs_to_wake", 0)) & 0xFF,
        )
    elif primitive_id == 58:  # passive_heal_during_phase
        body = struct.pack(
            "<BB",
            int(params.get("heal_per_tick", 0)) & 0xFF,
            1 if params.get("cancelled_by_correct_herb") else 0,
        )
    elif primitive_id == 59:  # attack_counter_style_swap
        cycle = [style_code(x) for x in (params.get("cycle") or [])[:4]]
        body = struct.pack(
            "<HB4B",
            int(params.get("every_n_attacks", 0)) & 0xFFFF,
            len(cycle) & 0xFF,
            cycle[0] if len(cycle) > 0 else 0,
            cycle[1] if len(cycle) > 1 else 0,
            cycle[2] if len(cycle) > 2 else 0,
            cycle[3] if len(cycle) > 3 else 0,
        )
    elif primitive_id == 60:  # spawn_tracking_tornadoes
        body = struct.pack(
            "<BBB",
            int(params.get("tornado_count", 0)) & 0xFF,
            int(params.get("damage_on_contact", 0)) & 0xFF,
            int(params.get("tornado_duration_ticks", 0)) & 0xFF,
        )
    elif primitive_id == 61:  # forced_prayer_switch_on_style_swap
        body = struct.pack("<B", 1)
    elif primitive_id == 66:  # destructible_pillars
        body = struct.pack(
            "<BHB",
            int(params.get("pillar_count", 0)) & 0xFF,
            int(params.get("pillar_hp", 0)) & 0xFFFF,
            int(params.get("damaged_per_electrify", 0)) & 0xFF,
        )
    elif primitive_id == 68:  # multi_limb_boss
        limbs = params.get("limbs") or []
        body = struct.pack("<B", min(3, len(limbs)))
        for i in range(3):
            limb = limbs[i] if i < len(limbs) and isinstance(limbs[i], dict) else {}
            body += pack_cstr(str(limb.get("name", "")), 16)
        for i in range(3):
            limb = limbs[i] if i < len(limbs) and isinstance(limbs[i], dict) else {}
            body += struct.pack("<H", int(limb.get("hp", 0)) & 0xFFFF)
    elif primitive_id == 69:  # player_position_swap
        body = struct.pack("<B", int(params.get("min_distance", 0)) & 0xFF)
    elif primitive_id == 70:  # environmental_wall_spawn
        body = struct.pack(
            "<BB",
            int(params.get("wall_length", 0)) & 0xFF,
            int(params.get("damage_on_cross", 0)) & 0xFF,
        )
    elif primitive_id == 71:  # tile_telegraph_lightning
        body = struct.pack(
            "<BBB",
            int(params.get("telegraph_ticks", 0)) & 0xFF,
            int(params.get("damage_on_tile", 0)) & 0xFF,
            int(params.get("tiles_per_volley", 0)) & 0xFF,
        )
    elif primitive_id == 72:  # continuous_heal_unless_interrupted
        body = struct.pack("<B", int(params.get("heal_per_tick", 0)) & 0xFF)
        body += pack_cstr(str(params.get("interrupt_object", "")), 32)
        body += struct.pack(
            "<B",
            int(params.get("interrupt_cooldown_ticks", 0)) & 0xFF,
        )
    elif primitive_id == 73:  # one_shot_weapon_provided
        body = pack_cstr(str(params.get("item", "")), 32)
        body += struct.pack(
            "<B",
            int(params.get("effective_max_hit", 0)) & 0xFF,
        )
        body += pack_cstr(str(params.get("removed_at_phase", "")), 24)
    elif primitive_id == 74:  # spawn_web_tiles
        body = struct.pack(
            "<BB",
            int(params.get("web_tile_count", 0)) & 0xFF,
            int(params.get("immobilizes_player_ticks", 0)) & 0xFF,
        )
    elif primitive_id == 75:  # spawn_colored_nylocas
        colors = params.get("nylo_colors") or []
        body = struct.pack(
            "<BBB",
            min(255, len(colors)),
            1 if params.get("heals_verzik_if_not_killed") else 0,
            int(params.get("heal_per_nylo_per_tick", 0)) & 0xFF,
        )
    elif primitive_id == 76:  # persistent_dot_tile_pool
        body = struct.pack(
            "<BBB",
            int(params.get("duration_ticks", 0)) & 0xFF,
            int(params.get("tile_count_per_spawn", 0)) & 0xFF,
            int(params.get("dot_per_tick", 0)) & 0xFF,
        )
    elif primitive_id == 77:  # obelisk_dps_check
        body = struct.pack(
            "<HH",
            int(params.get("obelisk_hp", 0)) & 0xFFFF,
            int(params.get("time_limit_ticks", 0)) & 0xFFFF,
        )
    elif primitive_id == 78:  # spawn_energized_pylons
        body = struct.pack(
            "<BBB",
            int(params.get("pylon_count", 0)) & 0xFF,
            int(params.get("buff_range_tiles", 0)) & 0xFF,
            pct_u8(params.get("buff_damage_multiplier", 1.0)) & 0xFF,
        )
    elif primitive_id == 79:  # periodic_death_tile_wave
        body = struct.pack(
            "<BH",
            int(params.get("wave_tile_count", 0)) & 0xFF,
            int(params.get("damage_on_tile", 0)) & 0xFFFF,
        )
    elif primitive_id == 81:  # heal_altars_player_must_disable
        body = struct.pack(
            "<BBBB",
            int(params.get("altar_count", 0)) & 0xFF,
            int(params.get("heal_per_altar_per_tick", 0)) & 0xFF,
            1 if params.get("disable_on_click") else 0,
            int(params.get("altars_respawn_ticks", 0)) & 0xFF,
        )
    # Pad to fixed size. Truncate if the struct ever exceeds (guarded).
    if len(body) > PARAM_BLOCK_SIZE:
        body = body[:PARAM_BLOCK_SIZE]
    return body + b"\x00" * (PARAM_BLOCK_SIZE - len(body))


def flatten_npc_ids(doc: dict) -> list[int]:
    ids = []
    raw = doc.get("npc_ids") or []
    if isinstance(raw, list):
        for v in raw:
            try:
                ids.append(int(v))
            except (TypeError, ValueError):
                pass
    # Multi-boss encounters also list ids per boss in [[bosses]].
    for b in doc.get("bosses") or []:
        nid = b.get("npc_id")
        if nid is not None:
            try:
                ids.append(int(nid))
            except (TypeError, ValueError):
                pass
    # Dedupe preserving order.
    seen, out = set(), []
    for i in ids:
        if i not in seen:
            seen.add(i)
            out.append(i)
    return out[:255]


def flatten_attacks(doc: dict) -> list[dict]:
    out = []
    # Top-level [[attacks]]
    for a in doc.get("attacks") or []:
        row = dict(a)
        row["_owner_npc_id"] = 0
        out.append(row)
    # Multi-boss: preserve owner NPC so runtime attack selection stays
    # data-driven for GWD/DKS-style encounters.
    for b in doc.get("bosses") or []:
        owner = int(b.get("npc_id") or 0)
        for a in b.get("attacks") or []:
            row = dict(a)
            row["_owner_npc_id"] = owner
            out.append(row)
    return out[:16]


def attack_flags(a: dict) -> int:
    flags = 0
    for key, bit in ATTACK_FLAGS.items():
        if a.get(key):
            flags |= bit
    if isinstance(a.get("conditional"), dict):
        if a["conditional"].get("used_when") == "no_player_targeting_boss":
            flags |= 0
    return flags


def attack_effect(a: dict) -> tuple[int, int, int, int, int]:
    on_hit = a.get("on_hit") if isinstance(a.get("on_hit"), dict) else {}
    splits = a.get("splits_into") if isinstance(a.get("splits_into"), dict) else {}
    if splits:
        return (
            ATTACK_EFFECT_SPLIT_PROJECTILES,
            max(0, min(255, int(splits.get("count") or 0))),
            max(0, min(255, int(splits.get("per_proj_max_on") or 0))),
            max(0, min(255, int(splits.get("per_proj_max_adj") or 0))),
            max(0, min(255, int(a.get("main_max_adj") or 0))),
        )
    poison_damage = a.get("poison_starting_damage")
    if poison_damage is None:
        poison_damage = on_hit.get("poison_starting_damage")
    poison = a.get("inflicts_poison") or on_hit.get("inflict_poison")
    if str(on_hit.get("effect") or "").strip().lower() == "inflict_poison":
        poison = True
    if poison:
        flags = ATTACK_EFFECT_POISON_PIERCES_PRAYER if a.get("poison_pierces_prayer") else 0
        chance = on_hit.get("effect_chance", a.get("effect_chance", 1.0))
        return (
            ATTACK_EFFECT_POISON,
            max(0, min(255, int(poison_damage or 2))),
            0,
            pct(chance),
            flags,
        )
    venom_damage = a.get("envenoms_starting_damage")
    if venom_damage is None:
        venom_damage = on_hit.get("envenoms_starting_damage")
    venom = a.get("envenoms_on_hit") or on_hit.get("inflict_venom")
    if str(on_hit.get("effect") or "").strip().lower() == "inflict_venom":
        venom = True
    if venom:
        return (
            ATTACK_EFFECT_VENOM,
            max(0, min(255, int(venom_damage or 6))),
            0,
            0,
            0,
        )
    if not on_hit:
        return ATTACK_EFFECT_NONE, 0, 0, 0, 0
    if str(on_hit.get("effect") or "").strip().lower() == "deactivate_all_prayers":
        return ATTACK_EFFECT_DEACTIVATE_PRAYERS, 0, 0, 0, 0
    drain_pct = int(on_hit.get("drain_prayer_pct") or 0)
    if drain_pct:
        return (
            ATTACK_EFFECT_DRAIN_PRAYER_PCT,
            0,
            0,
            max(0, min(100, drain_pct)),
            0,
        )
    knockback = int(on_hit.get("knockback_tiles") or a.get("knockback_tiles") or 0)
    if knockback:
        return (
            ATTACK_EFFECT_KNOCKBACK,
            max(0, min(255, knockback)),
            0,
            0,
            0,
        )
    drain_range = on_hit.get("drain_range") or [0, 0]
    drain_min = int(drain_range[0]) if len(drain_range) > 0 else 0
    drain_max = int(drain_range[1]) if len(drain_range) > 1 else drain_min
    heal_pct = int(on_hit.get("heal_boss_pct_of_damage") or 0)
    flags = 0
    for name in on_hit.get("drain_one_of") or []:
        key = str(name).strip().lower()
        if key == "magic":
            flags |= ATTACK_EFFECT_DRAIN_MAGIC
        elif key == "prayer":
            flags |= ATTACK_EFFECT_DRAIN_PRAYER
    if flags or heal_pct:
        return (
            ATTACK_EFFECT_DRAIN_HEAL,
            max(0, min(255, drain_min)),
            max(0, min(255, drain_max)),
            max(0, min(255, heal_pct)),
            flags & 0xFF,
        )
    return ATTACK_EFFECT_NONE, 0, 0, 0, 0


def attack_name_index(attacks: list[dict]) -> dict[str, int]:
    return {
        str(a.get("name") or "").strip().lower(): i
        for i, a in enumerate(attacks)
    }


def allowed_attack_mask(phase: dict, name_index: dict[str, int],
                        attack_count: int) -> int:
    allowed = phase.get("allowed_attacks")
    if allowed is None:
        return (1 << attack_count) - 1 if attack_count < 32 else 0xFFFFFFFF
    mask = 0
    for name in allowed:
        idx = name_index.get(str(name).strip().lower())
        if idx is not None and idx < 32:
            mask |= 1 << idx
    return mask


def style_weights(raw: dict | None) -> bytes:
    weights = [0] * 8
    for key, value in (raw or {}).items():
        idx = WEIGHT_STYLE.get(str(key).strip().lower())
        if idx is None:
            continue
        weights[idx] = max(0, min(255, int(value or 0)))
    return bytes(weights)


def parse_hp_pct(val) -> tuple[int, bool]:
    """Return (pct, hard_hp_trigger). pct=100 means fight-start;
    pct=0 + hard_trigger=True means `enter_at_hp = 0`."""
    if isinstance(val, int):
        return max(0, min(100, val)), False
    return 100, False


def parse_phase_terms(text: str, prefix: str, phase_index: dict[str, int],
                      ) -> tuple[int, int, str | None]:
    body = text[len(prefix):]
    if not body:
        return 0xFF, 0, f"missing phase id in '{text}'"
    mask = 0
    first = 0xFF
    for term in body.split("|"):
        term = term.strip()
        if term.startswith(prefix):
            term = term[len(prefix):]
        idx = phase_index.get(term)
        if idx is None:
            return 0xFF, 0, f"unknown phase '{term}' in trigger '{text}'"
        if idx >= 32:
            return 0xFF, 0, f"phase index too high in trigger '{text}'"
        if first == 0xFF:
            first = idx
        mask |= 1 << idx
    return first, mask, None


def parse_mechanic_trigger(raw, phase_index: dict[str, int], primitive_id: int,
                           params: dict | None = None
                           ) -> tuple[int, int, int, str, str | None]:
    text = str(raw or "").strip()
    if params is None:
        params = {}
    if not text:
        if primitive_id == 9:
            return TRIGGER_NONE, 0xFF, 0, "", None
        if primitive_id in (15, 16, 17) and "every_n_attacks" in params:
            return TRIGGER_ATTACK_COUNT, 0xFF, 0, "attack_counter_special", None
        if primitive_id == 24 and "standard_attacks_between" in params:
            return TRIGGER_ATTACK_COUNT, 0xFF, 0, "attack_counter_special", None
        return TRIGGER_PERIODIC, 0xFF, 0, "", None
    if primitive_id == 5 and text.startswith("while_alive:"):
        alive_name = text.split(":", 1)[1].strip()
        if not alive_name:
            return TRIGGER_NONE, 0xFF, 0, "", f"missing npc name in '{text}'"
        return TRIGGER_PERIODIC, 0xFF, 0, "", None
    for prefix, trigger_type in (
        ("phase_enter:", TRIGGER_PHASE_ENTER),
        ("phase_exit:", TRIGGER_PHASE_EXIT),
        ("phase_in:", TRIGGER_PHASE_IN),
        ("while_in_phase:", TRIGGER_PHASE_IN),
    ):
        if text.startswith(prefix):
            idx, mask, warning = parse_phase_terms(text, prefix, phase_index)
            if warning:
                return TRIGGER_NONE, 0xFF, 0, "", warning
            return trigger_type, idx, mask, "", None
    if text.startswith("after_attack:"):
        return TRIGGER_AFTER_ATTACK, 0xFF, 0, text.split(":", 1)[1].strip(), None
    if text.startswith("during_mechanic:"):
        return TRIGGER_DURING_MECH, 0xFF, 0, text.split(":", 1)[1].strip(), None
    if text == "attack_counter_special":
        return TRIGGER_ATTACK_COUNT, 0xFF, 0, text, None
    if text.startswith("event:"):
        return TRIGGER_EVENT, 0xFF, 0, text.split(":", 1)[1].strip(), None
    if text == "zuk_killed":
        return TRIGGER_EVENT, 0xFF, 0, text, None
    return TRIGGER_NONE, 0xFF, 0, "", f"unsupported trigger '{text}'"


def encode_protections(doc: dict, attacks: list[dict]) -> bytes:
    name_index = attack_name_index(attacks)
    out = bytearray()
    rows = []
    for p in doc.get("protections") or []:
        attack_name = str(p.get("attack") or "").strip().lower()
        attack_idx = name_index.get(attack_name)
        if attack_idx is None:
            continue
        style = STYLE.get(str(attacks[attack_idx].get("style") or "").lower(), 0)
        prayer = PRAYER_FLAGS.get(str(p.get("player_prayer") or "").strip().lower())
        if prayer is None:
            continue
        scale = float(p.get("damage_scale", 1.0))
        rows.append((attack_idx, style, prayer, round(scale * 100)))
    out += struct.pack("<B", min(255, len(rows)))
    for attack_idx, style, prayer, pct in rows[:255]:
        out += struct.pack("<BBIB", attack_idx & 0xFF, style & 0xFF,
                           prayer & 0xFFFFFFFF,
                           max(0, min(255, int(pct))))
    return bytes(out)


def owner_ids(row: dict, fallback: int = 0) -> list[int]:
    raw = row.get("applies_to_npc_ids")
    if isinstance(raw, list) and raw:
        out = []
        for value in raw:
            try:
                out.append(int(value))
            except (TypeError, ValueError):
                pass
        return out
    return [fallback]


def damage_condition(condition: str) -> tuple[int, int] | None:
    text = str(condition or "").strip().lower().replace('"', "'")
    if text == "player_facemask_equipped == false":
        return DAMAGE_MODIFIER_CONDITIONS["player_not_facemask"], 0
    if text == "weapon_not_corpbane_stab":
        return DAMAGE_MODIFIER_CONDITIONS["not_corpbane_stab"], 0
    if text == "incoming_damage > 50":
        return DAMAGE_MODIFIER_CONDITIONS["cap_range"], 0
    if "weapon_not_halberd_or_salamander" in text:
        if "incoming_style" in text and "melee" in text:
            return (
                DAMAGE_MODIFIER_CONDITIONS["melee_not_halberd_salamander"],
                STYLE["melee"],
            )
        return None
    if "weapon_not_halberd" in text and "incoming_style" in text and "melee" in text:
        return DAMAGE_MODIFIER_CONDITIONS["melee_not_halberd"], STYLE["melee"]
    m = re.fullmatch(r"incoming_style\s*==\s*'?([a-z_]+)'?", text)
    if m:
        return DAMAGE_MODIFIER_CONDITIONS["style_is"], style_code(m.group(1))
    m = re.fullmatch(r"incoming_style\s*!=\s*'?([a-z_]+)'?", text)
    if m:
        return DAMAGE_MODIFIER_CONDITIONS["style_not"], style_code(m.group(1))
    return None


def flatten_damage_modifiers(doc: dict) -> list[dict]:
    rows = []
    for raw in doc.get("damage_modifiers") or []:
        for owner in owner_ids(raw, 0):
            row = dict(raw)
            row["_owner_npc_id"] = owner
            rows.append(row)
    for b in doc.get("bosses") or []:
        owner = int(b.get("npc_id") or 0)
        for raw in b.get("damage_modifiers") or []:
            row = dict(raw)
            row["_owner_npc_id"] = owner
            rows.append(row)
    return rows[:16]


def encode_damage_modifiers(doc: dict) -> bytes:
    rows = []
    for row in flatten_damage_modifiers(doc):
        parsed = damage_condition(str(row.get("condition") or ""))
        if not parsed:
            continue
        cond, style = parsed
        if cond == DAMAGE_MODIFIER_CONDITIONS["cap_range"]:
            rng = row.get("replace_damage_range") or [0, 0]
            min_hit = int(rng[0]) if len(rng) > 0 else 0
            max_hit = int(rng[1]) if len(rng) > 1 else min_hit
            rows.append((
                int(row.get("_owner_npc_id") or 0),
                cond,
                max(0, min(255, min_hit)),
                max(0, min(255, max_hit)),
                0,
            ))
            continue
        rows.append((
            int(row.get("_owner_npc_id") or 0),
            cond,
            style,
            pct(row.get("damage_scale", 1.0)),
            0,
        ))
    out = bytearray(struct.pack("<B", min(255, len(rows))))
    for owner, cond, style, scale, flags in rows[:255]:
        out += struct.pack("<IBBBB", owner & 0xFFFFFFFF, cond & 0xFF,
                           style & 0xFF, scale & 0xFF, flags & 0xFF)
    return bytes(out)


def encode_encounter(doc: dict) -> tuple[bytes, list[str]]:
    buf = bytearray()
    warnings: list[str] = []
    slug = doc.get("slug") or doc.get("name", "").lower().replace(" ", "_")
    slug_b = pack_short(slug)
    buf += struct.pack("<B", len(slug_b)); buf += slug_b

    npc_ids = flatten_npc_ids(doc)
    buf += struct.pack("<B", len(npc_ids))
    for nid in npc_ids:
        buf += struct.pack("<I", nid & 0xFFFFFFFF)

    attacks = flatten_attacks(doc)
    buf += struct.pack("<B", min(255, len(attacks)))
    for a in attacks:
        name_b = pack_short(str(a.get("name") or ""))
        style = STYLE.get(str(a.get("style") or "").lower(), 0)
        max_hit = a.get("max_hit", 0)
        try:
            max_hit = int(max_hit)
        except (TypeError, ValueError):
            max_hit = 0
        warn = int(a.get("warning_ticks") or 0) & 0xFF
        min_hit = int(a.get("min_hit") or 0)
        owner = int(a.get("_owner_npc_id") or 0)
        flags = attack_flags(a)
        effect_id, effect_min, effect_max, effect_pct, effect_flags = (
            attack_effect(a)
        )
        buf += struct.pack("<B", len(name_b)); buf += name_b
        buf += struct.pack("<BHB", style & 0xFF, max(0, min(65535, max_hit)), warn)
        buf += struct.pack("<IHI", owner & 0xFFFFFFFF,
                           max(0, min(65535, min_hit)),
                           flags & 0xFFFFFFFF)
        buf += struct.pack("<BBBBB", effect_id & 0xFF, effect_min & 0xFF,
                           effect_max & 0xFF, effect_pct & 0xFF,
                           effect_flags & 0xFF)

    phases = doc.get("phases") or []
    attack_index = attack_name_index(attacks)
    phase_index = {}
    for idx, ph in enumerate(phases):
        pid = str(ph.get("id") or "")
        if pid and pid not in phase_index:
            phase_index[pid] = idx
    buf += struct.pack("<B", min(255, len(phases)))
    for ph in phases:
        pid_b = pack_short(str(ph.get("id") or ""))
        script_b = pack_short(str(ph.get("script") or ""), 47)
        if "enter_at_hp" in ph:
            pct, hard = 0, True
        else:
            pct, hard = parse_hp_pct(ph.get("enter_at_hp_pct"))
        buf += struct.pack("<B", len(pid_b)); buf += pid_b
        buf += struct.pack("<BB", pct, 1 if hard else 0)
        buf += struct.pack("<B", len(script_b)); buf += script_b
        has_allowed = ph.get("allowed_attacks") is not None
        buf += struct.pack("<I", allowed_attack_mask(ph, attack_index, len(attacks)))
        buf += style_weights(ph.get("adjacency_style_weights")
                             or ph.get("style_weights"))
        buf += style_weights(ph.get("non_adjacent_style_weights")
                             or ph.get("style_weights"))
        targetable = ph.get("player_targetable", ph.get("targetable", True))
        if ph.get("untargetable"):
            targetable = False
        buf += struct.pack("<B", 1 if targetable else 0)
        buf += struct.pack("<B", 1 if has_allowed else 0)

    mechanics = doc.get("mechanics") or []
    buf += struct.pack("<B", min(255, len(mechanics)))
    for m in mechanics:
        name_b = pack_short(str(m.get("name") or ""))
        prim = PRIMITIVE_IDS.get(str(m.get("primitive") or "").lower(), 0)
        params = m.get("params") or {}
        period = int(m.get("period_ticks") or 0)
        if prim == 5 and period == 0:
            period = int(params.get("period_ticks") or 0)
            if period == 0:
                period = 1
        trigger_text = str(m.get("trigger") or "")
        trigger_type, phase_idx, phase_mask, trigger_ref, warning = parse_mechanic_trigger(
            trigger_text, phase_index, prim, params
        )
        if not trigger_text and m.get("one_shot_at_fight_start"):
            trigger_type, phase_idx, phase_mask, trigger_ref = (
                TRIGGER_PHASE_ENTER, 0, 1, ""
            )
        if not trigger_text and prim == 26:
            trigger_type, phase_idx, phase_mask, trigger_ref = (
                TRIGGER_PHASE_ENTER, 0, 1, ""
            )
        if not trigger_text and prim == 68:
            trigger_type, phase_idx, phase_mask, trigger_ref = (
                TRIGGER_PHASE_ENTER, 0, 1, ""
            )
        if prim == 20 and trigger_type == TRIGGER_PHASE_IN and period == 0:
            period = 1
        if prim == 53 and period == 0:
            period = 1
        if prim == 58 and trigger_type == TRIGGER_PHASE_IN and period == 0:
            period = 1
        if prim == 59 and period == 0:
            trigger_type, phase_idx, phase_mask, trigger_ref = (
                TRIGGER_ATTACK_COUNT, 0xFF, 0, "attack_counter_style_swap"
            )
        if prim == 72 and trigger_type == TRIGGER_PHASE_IN and period == 0:
            period = 1
        if prim == 78 and trigger_type == TRIGGER_PHASE_IN and period == 0:
            trigger_type = TRIGGER_PHASE_ENTER
        if prim == 81 and period == 0:
            period = 1
        trigger_ref_b = pack_short(trigger_ref, 31)
        if warning:
            warnings.append(f"{m.get('name')}: {warning}")
        buf += struct.pack("<B", len(name_b)); buf += name_b
        buf += struct.pack("<BHBB",
                           prim & 0xFF,
                           max(0, min(65535, period)),
                           trigger_type & 0xFF,
                           phase_idx & 0xFF)
        buf += struct.pack("<I", phase_mask & 0xFFFFFFFF)
        buf += struct.pack("<B", len(trigger_ref_b)); buf += trigger_ref_b
        buf += pack_param_block(prim, params, trigger_text)

    buf += encode_protections(doc, attacks)
    buf += encode_damage_modifiers(doc)

    return bytes(buf), warnings


def main():
    files = sorted(CURATED.glob("*.toml"))
    # Skip the primitives doc.
    files = [f for f in files if not f.name.startswith("_")]

    encoded = []
    skipped: list[tuple[str, str]] = []
    warnings: list[str] = []
    for p in files:
        with p.open("rb") as f:
            try:
                doc = tomllib.load(f)
            except Exception as e:
                skipped.append((p.name, str(e)))
                continue
        try:
            blob, enc_warnings = encode_encounter(doc)
            encoded.append((p.name, blob))
            for warning in enc_warnings:
                warnings.append(f"{p.name}: {warning}")
        except Exception as e:
            skipped.append((p.name, f"encode: {e}"))

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", ENCT_MAGIC, ENCT_VERSION, len(encoded)))
        for _, blob in encoded:
            f.write(blob)

    REPORT.parent.mkdir(parents=True, exist_ok=True)
    with REPORT.open("w") as f:
        f.write(f"encounters encoded: {len(encoded)}\n")
        f.write(f"skipped:            {len(skipped)}\n")
        f.write(f"warnings:           {len(warnings)}\n")
        f.write(f"output:             {OUT.relative_to(ROOT)} "
                f"({OUT.stat().st_size} bytes)\n\n")
        if skipped:
            f.write("--- skipped files ---\n")
            for name, reason in skipped:
                f.write(f"  {name}: {reason}\n")
        if warnings:
            f.write("\n--- unresolved trigger bindings ---\n")
            for warning in warnings:
                f.write(f"  {warning}\n")
    print(f"  → {OUT} ({OUT.stat().st_size} bytes)", file=sys.stderr)
    print(f"  encoded {len(encoded)} encounters, skipped {len(skipped)}, "
          f"warnings {len(warnings)}",
          file=sys.stderr)


if __name__ == "__main__":
    main()
