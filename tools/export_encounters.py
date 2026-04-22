#!/usr/bin/env python3
"""Compile encounter TOMLs into `data/defs/encounters.bin`.

Input:  `data/curated/encounters/*.toml` — 50 hand-authored encounter
        specs, schema described in `database.md`, with field catalog in
        `data/curated/encounters/_primitives.md`.

Output: `data/defs/encounters.bin` with 'ENCT' magic. Loaded at
        `rc_world_create_config` time by `rc-core/encounter.c` when
        `RC_SUB_ENCOUNTER` is enabled.

Binary format (pass-2.2 — param blocks + simple phase triggers):
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
    phase_count u8
      per phase:
        id_len u8 + id[]
        enter_at_hp_pct u8     (0 = hard hp=0 trigger; 100 = fight start)
        hard_hp_trigger u8     (1 if `enter_at_hp = 0`)
    mechanic_count u8
      per mechanic:
        name_len u8 + name[]
        primitive_id u8        (enum — see PRIMITIVE_IDS)
        period_ticks u16
        trigger_type u8        (0=periodic, 1=phase_enter,
                                2=phase_exit, 255=deferred/unsupported)
        phase_idx u8           (index into encounter phases, or 255)
        param_block[64]        (opaque — primitive-specific struct)

Still not consumed in pass 2 (pass 3+ work):
  - multi-boss `[[bosses]]` arrays (picks first boss's attacks)
  - raid `[[rooms]]`, wave `[[waves]]` progression (skipped)
  - damage_modifiers, protections, variant_override
  - encounter_type flag (skipping skilling bosses)
  - rotations (tick-driven phases — Zulrah)
  - richer trigger DSL (`phase_in`, `while_in_phase`, `after_attack`,
    `during_mechanic`, unions like `a|b`)
  - per-phase `script = "..."`
"""
from __future__ import annotations

import re
import struct
import sys
import tomllib
from pathlib import Path

CURATED = Path("/home/joe/projects/RuneC_copy/data/curated/encounters")
OUT = Path("/home/joe/projects/RuneC_copy/data/defs/encounters.bin")
REPORT = Path("/home/joe/projects/RuneC_copy/tools/reports/encounters.txt")

ENCT_MAGIC = 0x54434E45   # 'ENCT'
ENCT_VERSION = 2

TRIGGER_PERIODIC = 0
TRIGGER_PHASE_ENTER = 1
TRIGGER_PHASE_EXIT = 2
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

# Primitive-name → enum id mapping. Matches the table in
# data/curated/encounters/_primitives.md. Unknown primitives → 0
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


def pack_short(s: str, maxlen: int = 255) -> bytes:
    return (s or "").encode("utf-8", errors="replace")[:maxlen]


PARAM_BLOCK_SIZE = 64


def pack_cstr(s: str, size: int) -> bytes:
    b = (s or "").encode("utf-8", errors="replace")[: size - 1]
    return b + b"\x00" * (size - len(b))


def pack_param_block(primitive_id: int, params: dict) -> bytes:
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
    elif primitive_id == 2:  # spawn_npcs
        body = pack_cstr(str(params.get("npc_name", "")), 32) + struct.pack(
            "<BB",
            int(params.get("count", 0)) & 0xFF,
            1 if params.get("persist_after_death") else 0,
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
        out.append(a)
    # Multi-boss: pull first-boss attacks so engine at least has
    # something to dispatch during pass 1 (most single-boss-vs-encounter
    # tests will hit the primary boss).
    for b in doc.get("bosses") or []:
        for a in b.get("attacks") or []:
            out.append(a)
    return out[:16]


def parse_hp_pct(val) -> tuple[int, bool]:
    """Return (pct, hard_hp_trigger). pct=100 means fight-start;
    pct=0 + hard_trigger=True means `enter_at_hp = 0`."""
    if isinstance(val, int):
        return max(0, min(100, val)), False
    return 100, False


def parse_mechanic_trigger(raw, phase_index: dict[str, int]
                           ) -> tuple[int, int, str | None]:
    text = str(raw or "").strip()
    if not text:
        return TRIGGER_PERIODIC, 0xFF, None
    if "|" in text:
        return TRIGGER_NONE, 0xFF, f"unsupported trigger union '{text}'"
    for prefix, trigger_type in (
        ("phase_enter:", TRIGGER_PHASE_ENTER),
        ("phase_exit:", TRIGGER_PHASE_EXIT),
    ):
        if text.startswith(prefix):
            phase_id = text[len(prefix):]
            if not phase_id:
                return TRIGGER_NONE, 0xFF, f"missing phase id in '{text}'"
            idx = phase_index.get(phase_id)
            if idx is None:
                return TRIGGER_NONE, 0xFF, (
                    f"unknown phase '{phase_id}' in trigger '{text}'"
                )
            return trigger_type, idx, None
    return TRIGGER_NONE, 0xFF, f"unsupported trigger '{text}'"


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
        buf += struct.pack("<B", len(name_b)); buf += name_b
        buf += struct.pack("<BHB", style & 0xFF, max(0, min(65535, max_hit)), warn)

    phases = doc.get("phases") or []
    phase_index = {}
    for idx, ph in enumerate(phases):
        pid = str(ph.get("id") or "")
        if pid and pid not in phase_index:
            phase_index[pid] = idx
    buf += struct.pack("<B", min(255, len(phases)))
    for ph in phases:
        pid_b = pack_short(str(ph.get("id") or ""))
        if "enter_at_hp" in ph:
            pct, hard = 0, True
        else:
            pct, hard = parse_hp_pct(ph.get("enter_at_hp_pct"))
        buf += struct.pack("<B", len(pid_b)); buf += pid_b
        buf += struct.pack("<BB", pct, 1 if hard else 0)

    mechanics = doc.get("mechanics") or []
    buf += struct.pack("<B", min(255, len(mechanics)))
    for m in mechanics:
        name_b = pack_short(str(m.get("name") or ""))
        prim = PRIMITIVE_IDS.get(str(m.get("primitive") or "").lower(), 0)
        period = int(m.get("period_ticks") or 0)
        trigger_type, phase_idx, warning = parse_mechanic_trigger(
            m.get("trigger"), phase_index
        )
        if warning:
            warnings.append(f"{m.get('name')}: {warning}")
        buf += struct.pack("<B", len(name_b)); buf += name_b
        buf += struct.pack("<BHBB",
                           prim & 0xFF,
                           max(0, min(65535, period)),
                           trigger_type & 0xFF,
                           phase_idx & 0xFF)
        buf += pack_param_block(prim, m.get("params") or {})

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
        f.write(f"output:             {OUT} ({OUT.stat().st_size} bytes)\n\n")
        if skipped:
            f.write("--- skipped files ---\n")
            for name, reason in skipped:
                f.write(f"  {name}: {reason}\n")
        if warnings:
            f.write("\n--- deferred trigger bindings ---\n")
            for warning in warnings:
                f.write(f"  {warning}\n")
    print(f"  → {OUT} ({OUT.stat().st_size} bytes)", file=sys.stderr)
    print(f"  encoded {len(encoded)} encounters, skipped {len(skipped)}, "
          f"warnings {len(warnings)}",
          file=sys.stderr)


if __name__ == "__main__":
    main()
