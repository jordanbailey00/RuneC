#!/usr/bin/env python3
"""Compile admitted encounter TOMLs into `data/defs/encounters.bin` (ENCT v13).

Only documents that declare `support = "full"` or `support = "attacks_only"`
are exported. Every other document is listed as unsupported in the report and
never reaches the game. Admitted documents are validated strictly: unknown
keys, names or primitives, unresolved references and out-of-range values fail
the export with a non-zero exit and nothing is written.

Runtime schema: `schema/defs/encounters.schema.toml`. Loader:
`rc-core/encounter.c` (`rc_encounter_load_specs`), which repeats these checks.
"""
from __future__ import annotations

import argparse
import struct
import sys
import tomllib
from pathlib import Path

from content_paths import content_read_path

ROOT = Path(__file__).resolve().parents[1]
CURATED = content_read_path("encounters")
OUT = ROOT / "data/defs/encounters.bin"
NDEF = ROOT / "data/defs/npc_defs.bin"
ODEF = ROOT / "data/defs/object_defs.bin"
REPORT = ROOT / "tools/reports/encounters.txt"

ENCT_MAGIC = 0x54434E45   # 'ENCT'
ENCT_VERSION = 14
PARAM_BLOCK_SIZE = 64

# Capacities and enums mirror rc-core/encounter.h.
MAX_SPECS = 64
MAX_FORMS = 4
MAX_HELPERS = 4
MAX_OBJECTS = 4
MAX_GUARDS = 4
NO_OBJECT = 0xFF
MAX_LOC_TYPE = 22
MAX_ATTACKS = 16
MAX_PHASES = 8
MAX_MECHANICS = 16
MAX_PROTECTIONS = 16
MAX_DAMAGE_MODS = 16
MAX_RUN_HELPERS = 8
RUN_EFFECT_QUOTA = 32

SUPPORT = {"full": 1, "attacks_only": 2}
DISENGAGE = {"keep": 0, "reset": 1}
LETHAL = {"die": 0, "transform": 1}
FORM_HP = {"full": 0, "keep": 1}
STYLE = {"stab": 1, "slash": 2, "crush": 3, "ranged": 4, "magic": 5}
PRAYER = {
    "protect_from_magic": 1 << 16,
    "protect_from_missiles": 1 << 17,
    "protect_from_melee": 1 << 18,
}
ATTACK_FLAGS = {
    "always_accurate": 1 << 0,
    "ignores_protection": 1 << 1,
    "tile_target": 1 << 2,
    "no_damage": 1 << 3,
    "needs_free_slot": 1 << 4,
    "diagonal_reach": 1 << 5,
}
# Effect kind -> (id, amount field for effect_a, field for effect_b).
EFFECTS = {
    "poison": (1, "damage", None),
    "venom": (2, "damage", None),
    "deactivate_prayers": (3, None, None),
    "drain_prayer": (4, "points", None),
    "drain_all_stats": (5, "levels", None),
    "freeze": (6, None, "ticks"),
    "teleport_away": (7, "min_distance", "max_distance"),
    "unequip": (8, None, "count"),
}
EFFECT_WHEN = {"damaged": 0, "always": 1, "rolled": 2}
# Content breath profiles: rc-content/combat/dragonfire.h.
BREATH = {"kbd_fiery": 1, "kbd_special": 2, "vorkath": 3, "vorkath_coloured": 4}
DAMAGE_CONDITION = {"style_not": 1, "style_is": 5}
TRIGGER = {"periodic": 0, "phase_enter": 1, "phase_exit": 2, "phase_in": 3,
           "attack_count": 6, "boss_hit": 8}
PRIMITIVE = {
    "attack_counter_alternate_special": 24,
    "acid_field": 82,
    "spawn_owned_helpers": 83,
    "hatch_helpers_on_hit": 84,
}

DOC_KEYS = {"name", "slug", "support", "disengage", "respawn_ticks",
            "source_pages", "notes", "arena", "forms", "helpers", "guards",
            "objects", "attacks", "phases", "mechanics", "protections",
            "damage_modifiers"}
ARENA_KEYS = {"min_x", "min_y", "max_x", "max_y", "plane"}
FORM_KEYS = {"id", "npc_id", "name", "binds", "dormant", "on_lethal", "next",
             "next_hp", "transition_ticks", "revert_after_ticks", "revert_to",
             "revert_hp", "defence", "notes"}
# Defence bonus order matches RcCombatStyle - 1 (rc-core/types.h).
DEFENCE_KEYS = ("stab", "slash", "crush", "ranged", "magic")
HELPER_KEYS = {"id", "npc_id", "name", "notes"}
GUARD_KEYS = {"id", "npc_id", "name", "notes"}
OBJECT_KEYS = {"id", "object_id", "name", "type", "notes"}
ATTACK_KEYS = {"name", "form", "style", "min_hit", "max_hit", "weight_adjacent",
               "weight_distant", "range", "hit_delay", "breath", "adjacent_pct",
               "flags", "effect", "notes"}
EFFECT_KEYS = {"kind", "when", "chance", "chance_protected", "damage", "points",
               "levels", "ticks", "min_distance", "max_distance", "count"}
PHASE_KEYS = {"id", "enter_at_hp_pct", "attacks", "script", "targetable", "notes"}
MECHANIC_KEYS = {"name", "primitive", "trigger", "period_ticks", "phases",
                 "params", "notes"}
PROTECTION_KEYS = {"attack", "prayer", "damage_pct", "notes"}
MODIFIER_KEYS = {"form", "condition", "style", "damage_pct", "notes"}
PARAM_KEYS = {
    24: {"every_n_attacks", "specials", "random_first"},
    82: {"pool_count", "pool_radius", "pool_damage_max", "pool_leech_pct",
         "pool_ticks", "strike_count", "strike_delay", "strike_min",
         "strike_max", "damage_taken_pct", "strike_style", "pool_object",
         "pool_delay"},
    83: {"helper", "count", "spawn_distance", "explode_max", "boss_immune",
         "freeze_player", "boss_idle", "instakill_spell", "spawn_delay"},
    84: {"helper", "chance_denominator", "near_loc", "near_distance",
         "despawn_ticks"},
}
OPTIONAL_PARAMS = {"random_first", "boss_immune", "freeze_player", "boss_idle",
                   "instakill_spell", "explode_max", "pool_leech_pct",
                   "pool_object", "pool_delay", "spawn_delay"}


class EncodeError(Exception):
    pass


def check_keys(where: str, row: dict, allowed: set[str]) -> None:
    unknown = sorted(set(row) - allowed)
    if unknown:
        raise EncodeError(f"{where}: unknown key(s) {', '.join(unknown)}")


def need(where: str, row: dict, key: str):
    if key not in row:
        raise EncodeError(f"{where}: missing '{key}'")
    return row[key]


def lookup(where: str, table: dict, value):
    if value not in table:
        raise EncodeError(f"{where}: unknown value {value!r} "
                          f"(expected one of {', '.join(map(str, table))})")
    return table[value]


def int_in(where: str, value, lo: int, hi: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not lo <= value <= hi:
        raise EncodeError(f"{where}: {value!r} is not an integer in {lo}..{hi}")
    return value


def flag(where: str, value) -> int:
    if not isinstance(value, bool):
        raise EncodeError(f"{where}: {value!r} is not true/false")
    return 1 if value else 0


def per_mille(where: str, value) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, float)) \
            or not 0 <= value <= 1:
        raise EncodeError(f"{where}: chance {value!r} is not in 0..1")
    return round(value * 1000)


def pstr(where: str, text, cap: int, required: bool = True) -> bytes:
    if not isinstance(text, str):
        raise EncodeError(f"{where}: {text!r} is not a string")
    raw = text.encode("latin-1")
    if len(raw) >= cap or (required and not raw) or b"\0" in raw:
        raise EncodeError(f"{where}: {text!r} must be 1..{cap - 1} bytes")
    return struct.pack("<B", len(raw)) + raw


def cstr(where: str, text: str, size: int) -> bytes:
    raw = text.encode("latin-1")
    if len(raw) >= size:
        raise EncodeError(f"{where}: {text!r} longer than {size - 1} bytes")
    return raw + b"\0" * (size - len(raw))


def index_of(where: str, labels: list[str], name) -> int:
    if name not in labels:
        raise EncodeError(f"{where}: unknown reference {name!r}")
    return labels.index(name)


def rows(doc: dict, key: str, cap: int, required: bool) -> list[dict]:
    value = doc.get(key, [])
    if not isinstance(value, list) or any(not isinstance(r, dict) for r in value):
        raise EncodeError(f"{key}: expected an array of tables")
    if required and not value:
        raise EncodeError(f"{key}: at least one row is required")
    if len(value) > cap:
        raise EncodeError(f"{key}: {len(value)} rows exceed the cap of {cap}")
    return value


def encode_form(where: str, form: dict, labels: list[str]) -> bytes:
    check_keys(where, form, FORM_KEYS)
    on_lethal = lookup(f"{where}.on_lethal", LETHAL, form.get("on_lethal", "die"))
    dormant = flag(f"{where}.dormant", form.get("dormant", False))
    moves = on_lethal == LETHAL["transform"] or dormant
    if moves:
        next_form = index_of(f"{where}.next", labels, need(where, form, "next"))
    elif "next" in form or "transition_ticks" in form:
        raise EncodeError(f"{where}: 'next'/'transition_ticks' need a transform "
                          "or dormant form")
    else:
        next_form = 0
    revert_after = int_in(f"{where}.revert_after_ticks",
                          form.get("revert_after_ticks", 0), 0, 0xFFFF)
    revert_to = index_of(f"{where}.revert_to", labels,
                         need(where, form, "revert_to")) if revert_after else 0
    defence = form.get("defence", {})
    if not isinstance(defence, dict):
        raise EncodeError(f"{where}.defence: expected a table")
    check_keys(f"{where}.defence", defence, set(DEFENCE_KEYS))
    bonuses = [int_in(f"{where}.defence.{k}", defence.get(k, 0), -32768, 32767)
               for k in DEFENCE_KEYS]
    return b"".join([
        struct.pack("<I", int_in(f"{where}.npc_id", need(where, form, "npc_id"),
                                 1, 0xFFFFFFFF)),
        pstr(f"{where}.name", need(where, form, "name"), 64),
        struct.pack("<5BHH2B",
                    flag(f"{where}.binds", form.get("binds", False)),
                    dormant, on_lethal, next_form,
                    lookup(f"{where}.next_hp", FORM_HP, form.get("next_hp", "full")),
                    int_in(f"{where}.transition_ticks",
                           form.get("transition_ticks", 0), 0, 0xFFFF),
                    revert_after, revert_to,
                    lookup(f"{where}.revert_hp", FORM_HP,
                           form.get("revert_hp", "full"))),
        struct.pack("<5h", *bonuses),
    ])


def encode_attack(where: str, atk: dict, forms: list[dict],
                  form_labels: list[str]) -> bytes:
    check_keys(where, atk, ATTACK_KEYS)
    owner = 0
    if "form" in atk:
        owner = forms[index_of(f"{where}.form", form_labels, atk["form"])]["npc_id"]
    flags = 0
    for name in atk.get("flags", []):
        flags |= lookup(f"{where}.flags", ATTACK_FLAGS, name)
    max_hit = int_in(f"{where}.max_hit", atk.get("max_hit", 0), 0, 0xFFFF)
    min_hit = int_in(f"{where}.min_hit", atk.get("min_hit", 0), 0, max_hit)
    if bool(flags & ATTACK_FLAGS["no_damage"]) == (max_hit > 0):
        raise EncodeError(f"{where}: no_damage attacks have max_hit 0; "
                          "every other attack needs max_hit > 0")
    breath = lookup(f"{where}.breath", BREATH, atk["breath"]) if "breath" in atk else 0
    effect = atk.get("effect", {})
    if not isinstance(effect, dict):
        raise EncodeError(f"{where}.effect: expected a table")
    effect_id = effect_when = effect_a = effect_b = chance = chance_protected = 0
    if effect:
        check_keys(f"{where}.effect", effect, EFFECT_KEYS)
        effect_id, a_key, b_key = lookup(f"{where}.effect.kind", EFFECTS,
                                         need(f"{where}.effect", effect, "kind"))
        allowed = {"kind", "when", "chance", "chance_protected"} | {a_key, b_key}
        check_keys(f"{where}.effect", effect, {k for k in allowed if k})
        effect_when = lookup(f"{where}.effect.when", EFFECT_WHEN,
                             need(f"{where}.effect", effect, "when"))
        if a_key:
            effect_a = int_in(f"{where}.effect.{a_key}",
                              need(f"{where}.effect", effect, a_key), 1, 255)
        if b_key:
            effect_b = int_in(f"{where}.effect.{b_key}",
                              need(f"{where}.effect", effect, b_key), 1, 255)
        if effect_id == EFFECTS["teleport_away"][0] and effect_a > effect_b:
            raise EncodeError(f"{where}.effect: min_distance exceeds max_distance")
        chance = per_mille(f"{where}.effect.chance", effect.get("chance", 1))
        chance_protected = per_mille(f"{where}.effect.chance_protected",
                                     effect.get("chance_protected",
                                                effect.get("chance", 1)))
    weight_adjacent = int_in(f"{where}.weight_adjacent",
                             atk.get("weight_adjacent", 0), 0, 255)
    weight_distant = int_in(f"{where}.weight_distant",
                            atk.get("weight_distant", 0), 0, 255)
    if not weight_adjacent and not weight_distant:
        raise EncodeError(f"{where}: needs a selection weight")
    hit_delay = int_in(f"{where}.hit_delay", atk.get("hit_delay", 0), 0, 255)
    if flags & ATTACK_FLAGS["tile_target"] and (breath or effect_id or not hit_delay):
        raise EncodeError(f"{where}: tile attacks need hit_delay and no "
                          "breath or effect")
    if breath and flags & (ATTACK_FLAGS["always_accurate"] | ATTACK_FLAGS["no_damage"]):
        raise EncodeError(f"{where}: breath attacks roll their own accuracy")
    return b"".join([
        pstr(f"{where}.name", need(where, atk, "name"), 48),
        struct.pack("<IBHH6BI4B2H", owner,
                    lookup(f"{where}.style", STYLE, need(where, atk, "style")),
                    min_hit, max_hit, weight_adjacent, weight_distant,
                    int_in(f"{where}.range", atk.get("range", 0), 0, 255),
                    hit_delay, breath,
                    int_in(f"{where}.adjacent_pct", atk.get("adjacent_pct", 0), 0, 100),
                    flags, effect_id, effect_when, effect_a, effect_b,
                    chance, chance_protected),
    ])


def encode_params(where: str, primitive: int, params: dict,
                  mechanic_labels: list[str], helper_labels: list[str],
                  object_labels: list[str], triggers: list[int]) -> bytes:
    check_keys(f"{where}.params", params, PARAM_KEYS[primitive])
    for key in PARAM_KEYS[primitive]:
        if key not in params and key not in OPTIONAL_PARAMS:
            raise EncodeError(f"{where}.params: missing '{key}'")
    p = lambda k, lo, hi, d=0: int_in(f"{where}.params.{k}", params.get(k, d), lo, hi)
    b = lambda k: flag(f"{where}.params.{k}", params.get(k, False))
    if primitive == 24:
        specials = params["specials"]
        if not isinstance(specials, list) or not 1 <= len(specials) <= 2:
            raise EncodeError(f"{where}.params.specials: one or two mechanic names")
        idx = [index_of(f"{where}.params.specials", mechanic_labels, s)
               for s in specials]
        for i in idx:
            if triggers[i] != TRIGGER["attack_count"]:
                raise EncodeError(f"{where}: special {mechanic_labels[i]!r} "
                                  "needs trigger = \"attack_count\"")
        idx += [0] * (2 - len(idx))
        body = struct.pack("<5B", p("every_n_attacks", 2, 255), len(specials),
                           idx[0], idx[1], b("random_first"))
    elif primitive == 82:
        pool_count = p("pool_count", 0, RUN_EFFECT_QUOTA // 2)
        strike_delay = p("strike_delay", 0, 255)
        if strike_delay + pool_count >= RUN_EFFECT_QUOTA:
            raise EncodeError(f"{where}: pools and in-flight strikes exceed the "
                              f"run quota of {RUN_EFFECT_QUOTA}")
        strike_max = p("strike_max", 0, 0xFFFF)
        pool_object = NO_OBJECT
        if "pool_object" in params:
            pool_object = index_of(f"{where}.params.pool_object", object_labels,
                                   params["pool_object"])
        body = struct.pack("<7B2H4B", pool_count, p("pool_radius", 0, 255),
                           p("pool_damage_max", 1, 255),
                           p("pool_leech_pct", 0, 100), p("pool_ticks", 1, 255),
                           p("strike_count", 1, 255), strike_delay,
                           p("strike_min", 0, strike_max), strike_max,
                           p("damage_taken_pct", 0, 100),
                           lookup(f"{where}.params.strike_style", STYLE,
                                  params["strike_style"]),
                           pool_object, p("pool_delay", 0, 255))
    elif primitive == 84:
        helper = index_of(f"{where}.params.helper", helper_labels, params["helper"])
        body = struct.pack("<BBIBH", helper, p("chance_denominator", 1, 255),
                           p("near_loc", 1, 0xFFFF), p("near_distance", 0, 32),
                           p("despawn_ticks", 1, 0xFFFF))
    else:
        helper = index_of(f"{where}.params.helper", helper_labels, params["helper"])
        body = struct.pack("<7B", helper, p("count", 1, MAX_RUN_HELPERS),
                           p("spawn_distance", 1, 255), p("explode_max", 0, 255),
                           b("boss_immune"), b("freeze_player"), b("boss_idle"))
        body += cstr(f"{where}.params.instakill_spell",
                     params.get("instakill_spell", ""), 24)
        body += struct.pack("<B", p("spawn_delay", 0, 255))
    return body + b"\0" * (PARAM_BLOCK_SIZE - len(body))


def encode_encounter(doc: dict, ndef: dict[int, str] | None,
                     odef: dict[int, str] | None = None) -> bytes:
    check_keys("document", doc, DOC_KEYS)
    support = lookup("support", SUPPORT, doc["support"])
    forms = rows(doc, "forms", MAX_FORMS, True)
    helpers = rows(doc, "helpers", MAX_HELPERS, False)
    guards = rows(doc, "guards", MAX_GUARDS, False)
    objects = rows(doc, "objects", MAX_OBJECTS, False)
    attacks = rows(doc, "attacks", MAX_ATTACKS, True)
    phases = rows(doc, "phases", MAX_PHASES, True)
    mechanics = rows(doc, "mechanics", MAX_MECHANICS, False)
    protections = rows(doc, "protections", MAX_PROTECTIONS, False)
    modifiers = rows(doc, "damage_modifiers", MAX_DAMAGE_MODS, False)

    def labels(table: list[dict], key: str, what: str) -> list[str]:
        out = [need(f"{what}[{i}]", r, key) for i, r in enumerate(table)]
        if len(set(out)) != len(out):
            raise EncodeError(f"{what}: duplicate {key}")
        return out

    form_labels = labels(forms, "id", "forms")
    helper_labels = labels(helpers, "id", "helpers")
    labels(guards, "id", "guards")
    object_labels = labels(objects, "id", "objects")
    attack_labels = labels(attacks, "name", "attacks")
    phase_labels = labels(phases, "id", "phases")
    mechanic_labels = labels(mechanics, "name", "mechanics")
    # Presentation keys: "<attack>", "<mechanic>[:<part>]" and "form:<change>".
    for label in attack_labels + mechanic_labels:
        if ":" in label:
            raise EncodeError(f"name {label!r}: ':' is reserved for "
                              "presentation part keys")
    if "form" in mechanic_labels:
        raise EncodeError("mechanics: 'form' is reserved for form-change keys")
    role_ids = [r["npc_id"] for r in forms + helpers + guards]
    if len(set(role_ids)) != len(role_ids):
        raise EncodeError("an NPC id appears in two roles")
    if not any(f.get("binds", False) for f in forms):
        raise EncodeError("forms: no form binds on spawn")
    arena = doc.get("arena")
    if guards and arena is None:
        raise EncodeError("guards: need an arena (their room)")
    if ndef is not None:
        for role in forms + helpers + guards:
            have = ndef.get(role["npc_id"])
            if have != role["name"]:
                raise EncodeError(f"NPC {role['npc_id']} is {have!r} in installed "
                                  f"NDEF, expected {role['name']!r}")
    loc_ids = [need(f"objects[{i}]", o, "object_id") for i, o in enumerate(objects)]
    if len(set(loc_ids)) != len(loc_ids):
        raise EncodeError("objects: duplicate object_id")
    if odef is not None:
        for o in objects:
            have = odef.get(o["object_id"])
            if have != o.get("name"):
                raise EncodeError(f"object {o['object_id']} is {have!r} in "
                                  f"installed object defs, expected "
                                  f"{o.get('name')!r}")
    if support == SUPPORT["attacks_only"] and (
            len(forms) != 1 or len(phases) != 1 or helpers or objects or mechanics
            or guards or arena is not None
            or phases[0].get("script")
            or forms[0].get("on_lethal", "die") != "die"
            or forms[0].get("dormant", False)
            or forms[0].get("revert_after_ticks", 0)):
        raise EncodeError("attacks_only specs declare one form, one phase and "
                          "attacks only")

    out = [pstr("slug", need("document", doc, "slug"), 48),
           pstr("name", need("document", doc, "name"), 64),
           struct.pack("<BBH", support,
                       lookup("disengage", DISENGAGE, doc.get("disengage", "keep")),
                       int_in("respawn_ticks", doc.get("respawn_ticks", 0), 0, 0xFFFF))]
    if arena is None:
        out.append(struct.pack("<B4HB", 0, 0, 0, 0, 0, 0))
    else:
        if not isinstance(arena, dict):
            raise EncodeError("arena: expected a table")
        check_keys("arena", arena, ARENA_KEYS)
        box = [int_in(f"arena.{k}", need("arena", arena, k), 0, 0x3FFF)
               for k in ("min_x", "min_y", "max_x", "max_y")]
        if box[0] > box[2] or box[1] > box[3]:
            raise EncodeError("arena: min exceeds max")
        out.append(struct.pack("<B4HB", 1, *box,
                               int_in("arena.plane", need("arena", arena, "plane"), 0, 3)))
    out.append(struct.pack("<B", len(forms)))
    out += [encode_form(f"forms[{i}]", f, form_labels) for i, f in enumerate(forms)]
    out.append(struct.pack("<B", len(helpers)))
    for i, h in enumerate(helpers):
        check_keys(f"helpers[{i}]", h, HELPER_KEYS)
        out.append(struct.pack("<I", int_in(f"helpers[{i}].npc_id", h["npc_id"],
                                            1, 0xFFFFFFFF)))
        out.append(pstr(f"helpers[{i}].name", need(f"helpers[{i}]", h, "name"), 64))
    out.append(struct.pack("<B", len(objects)))
    for i, o in enumerate(objects):
        where = f"objects[{i}]"
        check_keys(where, o, OBJECT_KEYS)
        out.append(struct.pack("<I", int_in(f"{where}.object_id", o["object_id"],
                                            0, 0xFFFF)))
        out.append(pstr(f"{where}.name", need(where, o, "name"), 64))
        out.append(struct.pack("<B", int_in(f"{where}.type", need(where, o, "type"),
                                            0, MAX_LOC_TYPE)))
    out.append(struct.pack("<B", len(guards)))
    for i, g in enumerate(guards):
        check_keys(f"guards[{i}]", g, GUARD_KEYS)
        out.append(struct.pack("<I", int_in(f"guards[{i}].npc_id", g["npc_id"],
                                            1, 0xFFFFFFFF)))
        out.append(pstr(f"guards[{i}].name", need(f"guards[{i}]", g, "name"), 64))
    out.append(struct.pack("<B", len(attacks)))
    out += [encode_attack(f"attacks[{i}]", a, forms, form_labels)
            for i, a in enumerate(attacks)]
    out.append(struct.pack("<B", len(phases)))
    for i, ph in enumerate(phases):
        where = f"phases[{i}]"
        check_keys(where, ph, PHASE_KEYS)
        mask = 0
        for name in ph.get("attacks", attack_labels):
            mask |= 1 << index_of(f"{where}.attacks", attack_labels, name)
        out.append(pstr(f"{where}.id", ph["id"], 32))
        out.append(struct.pack("<BI", int_in(f"{where}.enter_at_hp_pct",
                                             ph.get("enter_at_hp_pct", 100), 0, 100),
                               mask))
        out.append(pstr(f"{where}.script", ph.get("script", ""), 48, required=False))
        out.append(struct.pack("<B", flag(f"{where}.targetable",
                                          ph.get("targetable", True))))
    triggers = [lookup(f"mechanics[{i}].trigger", TRIGGER, need(f"mechanics[{i}]", m, "trigger"))
                for i, m in enumerate(mechanics)]
    out.append(struct.pack("<B", len(mechanics)))
    for i, m in enumerate(mechanics):
        where = f"mechanics[{i}]"
        check_keys(where, m, MECHANIC_KEYS)
        primitive = lookup(f"{where}.primitive", PRIMITIVE, need(where, m, "primitive"))
        period = int_in(f"{where}.period_ticks", m.get("period_ticks", 0), 0, 0xFFFF)
        trigger = triggers[i]
        if trigger == TRIGGER["periodic"] and not period:
            raise EncodeError(f"{where}: periodic mechanics need period_ticks")
        if trigger not in (TRIGGER["periodic"], TRIGGER["phase_in"]) and period:
            raise EncodeError(f"{where}: period_ticks only applies to periodic "
                              "or phase_in triggers")
        if primitive == PRIMITIVE["attack_counter_alternate_special"] \
                and trigger != TRIGGER["attack_count"]:
            raise EncodeError(f"{where}: the special rotation uses "
                              "trigger = \"attack_count\"")
        if (primitive == PRIMITIVE["hatch_helpers_on_hit"]) \
                != (trigger == TRIGGER["boss_hit"]):
            raise EncodeError(f"{where}: hatch_helpers_on_hit and "
                              "trigger = \"boss_hit\" go together")
        mask = 0
        for name in m.get("phases", []):
            mask |= 1 << index_of(f"{where}.phases", phase_labels, name)
        params = m.get("params", {})
        if not isinstance(params, dict):
            raise EncodeError(f"{where}.params: expected a table")
        out.append(pstr(f"{where}.name", m["name"], 48))
        # phase_idx 0xFF: the mask selects phases, and no mask means any.
        out.append(struct.pack("<BHBBI", primitive, period, trigger, 0xFF, mask))
        out.append(pstr(f"{where}.trigger_ref", "", 32, required=False))
        out.append(encode_params(where, primitive, params, mechanic_labels,
                                 helper_labels, object_labels, triggers))
    rotations = [i for i, m in enumerate(mechanics)
                 if m["primitive"] == "attack_counter_alternate_special"]
    if len(rotations) > 1:
        raise EncodeError("mechanics: more than one special rotation")
    for i, m in enumerate(mechanics):
        if triggers[i] == TRIGGER["attack_count"] and i not in rotations and not any(
                m["name"] in mechanics[r]["params"]["specials"] for r in rotations):
            raise EncodeError(f"mechanics[{i}]: attack_count mechanics must be "
                              "listed by the special rotation")
    out.append(struct.pack("<B", len(protections)))
    for i, pr in enumerate(protections):
        where = f"protections[{i}]"
        check_keys(where, pr, PROTECTION_KEYS)
        attack = index_of(f"{where}.attack", attack_labels, need(where, pr, "attack"))
        out.append(struct.pack("<BBIB", attack,
                               STYLE[attacks[attack]["style"]],
                               lookup(f"{where}.prayer", PRAYER, need(where, pr, "prayer")),
                               int_in(f"{where}.damage_pct", need(where, pr, "damage_pct"),
                                      0, 100)))
    out.append(struct.pack("<B", len(modifiers)))
    for i, md in enumerate(modifiers):
        where = f"damage_modifiers[{i}]"
        check_keys(where, md, MODIFIER_KEYS)
        owner = 0
        if "form" in md:
            owner = forms[index_of(f"{where}.form", form_labels, md["form"])]["npc_id"]
        out.append(struct.pack("<IBBBB", owner,
                               lookup(f"{where}.condition", DAMAGE_CONDITION,
                                      need(where, md, "condition")),
                               lookup(f"{where}.style", STYLE, need(where, md, "style")),
                               int_in(f"{where}.damage_pct", need(where, md, "damage_pct"),
                                      0, 100), 0))
    return b"".join(out)


def read_ndef_names(path: Path) -> dict[int, str] | None:
    if not path.exists():
        return None
    from export_regular_npc_mechanics import read_ndef
    return {npc_id: row["name"] for npc_id, row in read_ndef(path).items()}


def read_odef_names(path: Path) -> dict[int, str] | None:
    """Object names from an installed ODEF v1/v2 binary."""
    if not path.exists():
        return None
    data = path.read_bytes()
    magic, version, count = struct.unpack_from("<III", data, 0)
    if magic != 0x4645444F or version not in (1, 2):
        raise EncodeError(f"{path}: not an ODEF v1/v2 file")
    pos, names = 12, {}

    def pstr16() -> str:
        nonlocal pos
        (n,) = struct.unpack_from("<H", data, pos)
        pos += 2 + n
        return data[pos - n:pos].decode("latin-1")

    for _ in range(count):
        obj_id, _w, _l, _it, _ac, models, transforms, _fa = struct.unpack_from(
            "<IHHBBBBB", data, pos)
        pos += 13 + 20
        params = 0
        if version >= 2:
            (params,) = struct.unpack_from("<H", data, pos + 2)
            pos += 12
        names[obj_id] = pstr16()
        for _ in range(5):
            pstr16()
        pos += 4 * models + 4 * transforms + 8 * params
    return names


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true",
                        help="validate and compare with the installed binary; "
                             "write nothing")
    args = parser.parse_args()

    ndef = read_ndef_names(NDEF)
    odef = read_odef_names(ODEF)
    files = sorted(f for f in CURATED.glob("*.toml") if not f.name.startswith("_"))
    admitted: list[tuple[str, dict, bytes]] = []
    unsupported: list[str] = []
    errors: list[str] = []
    slugs: dict[str, str] = {}
    claimed: dict[int, str] = {}
    for path in files:
        try:
            with path.open("rb") as f:
                doc = tomllib.load(f)
        except tomllib.TOMLDecodeError as e:
            errors.append(f"{path.name}: TOML: {e}")
            continue
        if "support" not in doc:
            unsupported.append(path.name)
            continue
        try:
            blob = encode_encounter(doc, ndef, odef)
        except (EncodeError, KeyError, TypeError) as e:
            errors.append(f"{path.name}: {e}")
            continue
        slug = doc["slug"]
        if slug in slugs:
            errors.append(f"{path.name}: slug {slug!r} also in {slugs[slug]}")
        slugs[slug] = path.name
        for role in doc.get("forms", []) + doc.get("helpers", []):
            other = claimed.setdefault(role["npc_id"], path.name)
            if other != path.name:
                errors.append(f"{path.name}: NPC {role['npc_id']} also claimed "
                              f"by {other}")
        admitted.append((path.name, doc, blob))
    if len(admitted) > MAX_SPECS:
        errors.append(f"{len(admitted)} admitted specs exceed the cap of {MAX_SPECS}")
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        print(f"export_encounters: {len(errors)} error(s); nothing written",
              file=sys.stderr)
        return 1

    data = struct.pack("<III", ENCT_MAGIC, ENCT_VERSION, len(admitted))
    data += b"".join(blob for _, _, blob in admitted)
    if args.check:
        current = OUT.read_bytes() if OUT.exists() else b""
        if current != data:
            print(f"export_encounters: {OUT.relative_to(ROOT)} is stale",
                  file=sys.stderr)
            return 1
        print("export_encounters: up to date", file=sys.stderr)
        return 0
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(data)
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    with REPORT.open("w") as f:
        f.write(f"ENCT v{ENCT_VERSION}: {len(admitted)} admitted, "
                f"{len(unsupported)} unsupported, {len(data)} bytes\n")
        if ndef is None:
            f.write("NDEF not installed: role names are checked at load only\n")
        if odef is None:
            f.write("object defs not installed: object names are checked at "
                    "load only\n")
        f.write("\n--- admitted ---\n")
        for name, doc, _ in admitted:
            roles = ", ".join(f"{r['id']}={r['npc_id']}" for r in doc["forms"])
            f.write(f"  {name}: {doc['support']} ({roles})\n")
        f.write("\n--- unsupported (no support level; not exported) ---\n")
        for name in unsupported:
            f.write(f"  {name}\n")
    print(f"  -> {OUT} ({len(data)} bytes): {len(admitted)} admitted, "
          f"{len(unsupported)} unsupported", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
