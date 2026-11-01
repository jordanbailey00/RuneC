#!/usr/bin/env python3
"""Export B241 item definitions and combat metadata to IDEF.

Sources:
  - RuneC's b241 cache (revision authority) — item identity, models, links,
    weight, wear positions, actions, examine text, equipment bonuses, attack
    speed, attack range and the requirements it lists.
  - content/items/weapons.tsv — weapon category of every wieldable item.
  - osrsreboxed-db (supplemental) — bonuses of the few items the cache has no
    params for, and requirements beyond the two the cache stores.

Excluded per ignore.md:
  - No GE price field (single-player, no live market).
  - `cost` (shop base price) + `highalch` + `lowalch` are kept — those are
    static values baked into the cache, not a market.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from cache_item_defs import load_cache_item_defs
from database_sources import OsrsreboxedDB, OSRSREBOXED
from source_paths import CACHE_DIR, require_cache_dir
from wiki_item_bonuses import load_wiki_bonus_overrides
from content_paths import content_read_path

WEAPON_TABLE_PATH = content_read_path("items/weapons.tsv")


def load_weapon_table(path: Path = WEAPON_TABLE_PATH) -> dict[int, dict]:
    rows: dict[int, dict] = {}
    for line in path.read_text().splitlines():
        if not line or line.startswith(("#", "item_id|")):
            continue
        item_id, name, weapon_type, _source = line.split("|")
        rows[int(item_id)] = {"name": name, "weapon_type": weapon_type}
    return rows


WEAPON_TABLE = load_weapon_table()

IDEF_MAGIC = 0x49444546  # "IDEF"
IDEF_VERSION = 3

# Equipment slots — must match rc-core/types.h RcEquipSlot.
SLOT_NAMES = {
    "head": 0, "cape": 1, "neck": 2, "weapon": 3, "body": 4,
    "shield": 5, "legs": 6, "hands": 7, "feet": 8, "ring": 9,
    "ammo": 10, "2h": 3,
}
EQUIP_SLOT_KEYS = {"2h": "weapon"}

# Skill IDs for level requirements — must match rc-core/skills.c
SKILL_IDS = {
    "attack": 0, "defence": 1, "strength": 2, "hitpoints": 3, "ranged": 4,
    "prayer": 5, "magic": 6, "cooking": 7, "woodcutting": 8, "fletching": 9,
    "fishing": 10, "firemaking": 11, "crafting": 12, "smithing": 13,
    "mining": 14, "herblore": 15, "agility": 16, "thieving": 17,
    "slayer": 18, "farming": 19, "runecraft": 20, "runecrafting": 20,
    "hunter": 21, "construction": 22,
}

WEAPON_TYPES = {
    None: 0,
    "unarmed": 0, "2h_sword": 1, "axe": 2, "banner": 3, "blunt": 4,
    "bludgeon": 5, "bulwark": 6, "chinchompas": 7, "claw": 8, "crossbow": 9,
    "whip": 10, "fixed_device": 11, "gun": 12, "pickaxe": 13, "polearm": 14,
    "polestaff": 15, "powered_staff": 16, "scythe": 17, "slash_sword": 18,
    "spear": 19, "spiked": 20, "stab_sword": 21, "staff": 22, "thrown": 23,
    "two-handed_sword": 24, "bow": 25, "salamander": 26,
    "multi-style": 27, "powered_wand": 28, "bladed_staff": 29,
    "partisan": 30, "atlatl": 31, "slash_flail": 32, "multi_melee": 33,
    "chainmace": 34, "powered_book": 35,
}

# Style rows of the b241 combat_interface_* DB table: (name, attack_style).
_MELEE3 = (("accurate", "accurate"), ("aggressive", "aggressive"), ("defensive", "defensive"))
_MELEE4 = _MELEE3[:2] + (("controlled", "controlled"),) + _MELEE3[2:]
_RANGED = (("accurate", "accurate"), ("rapid", "rapid"), ("longrange", "longrange"))
_POWERED = (("accurate", "accurate"), ("accurate", "accurate"), ("longrange", "longrange"))
WEAPON_STANCES = {
    "unarmed": _MELEE3, "blunt": _MELEE3, "staff": _MELEE3, "polestaff": _MELEE3,
    "whip": (("flick", "accurate"), ("lash", "controlled"), ("deflect", "defensive")),
    "polearm": (("jab", "controlled"), ("swipe", "aggressive"), ("fend", "defensive")),
    "bulwark": (("pummel", "accurate"), ("block", "defensive")),
    "bow": _RANGED, "crossbow": _RANGED, "thrown": _RANGED, "chinchompas": _RANGED,
    "powered_staff": _POWERED, "powered_wand": _POWERED, "powered_book": _POWERED,
    "salamander": (("scorch", "aggressive"), ("flare", "accurate"), ("blaze", "defensive")),
    "slash_flail": _MELEE3,
    "chainmace": (("thrust", "accurate"), ("thrust", "aggressive"), ("smash", "aggressive")),
    "bladed_staff": (("jab", "accurate"), ("swipe", "aggressive"), ("fend", "defensive")),
    "bludgeon": (("pound", "aggressive"), ("pummel", "aggressive"), ("smash", "aggressive")),
}

# Bonus params; missing params read 0 once an item carries any of them.
BONUS_PARAMS = {
    0: "attack_stab", 1: "attack_slash", 2: "attack_crush", 3: "attack_magic",
    4: "attack_ranged", 5: "defence_stab", 6: "defence_slash", 7: "defence_crush",
    8: "defence_magic", 9: "defence_ranged", 10: "melee_strength", 11: "prayer",
}
RANGED_STRENGTH_PARAMS = (12, 189)
MAGIC_DAMAGE_PARAM = 299  # tenths of a percent, as IDEF stores it
ATTACK_RANGE_PARAM = 13
ATTACK_SPEED_PARAM = 14
REQUIREMENT_PARAMS = ((434, 436), (435, 437))  # (skill, level)
# The same params record the level to make a few items; those are not wear rules.
CREATION_REQUIREMENT_SKILLS = {"cooking", "fletching", "firemaking", "crafting",
                               "smithing", "mining", "herblore", "construction"}

STANCE_BITS = {  # bitfield
    "accurate": 1 << 0, "aggressive": 1 << 1, "controlled": 1 << 2,
    "defensive": 1 << 3, "rapid": 1 << 4, "longrange": 1 << 5,
    "autocast": 1 << 6, "defensive_autocast": 1 << 7,
}

# Flag bits for the u8 flags byte in IDEF
F_STACKABLE      = 1 << 0
F_TRADEABLE      = 1 << 1
F_MEMBERS        = 1 << 2
F_QUEST_ITEM     = 1 << 3
F_HAS_EQUIPMENT  = 1 << 4
F_HAS_WEAPON     = 1 << 5
F_NOTED          = 1 << 6
F_PLACEHOLDER    = 1 << 7
F_NOTEABLE       = 1 << 8
F_EQUIP_PLAYER   = 1 << 9
F_EQUIP_WEAPON   = 1 << 10
F_DUPLICATE      = 1 << 11

ITEM_KIND_GENERIC     = 0
ITEM_KIND_EQUIPMENT   = 1
ITEM_KIND_WEAPON      = 2
ITEM_KIND_NOTED       = 3
ITEM_KIND_PLACEHOLDER = 4
ITEM_KIND_QUEST       = 5

MODEL_TYPES = {
    "item_model_ground": 0,
    "item_model_male0": 1,
    "item_model_male1": 2,
    "item_model_male2": 3,
    "item_model_female0": 4,
    "item_model_female1": 5,
    "item_model_female2": 6,
}

WEARPOS_HAT = 0
WEARPOS_BACK = 1
WEARPOS_FRONT = 2
WEARPOS_RIGHT_HAND = 3
WEARPOS_TORSO = 4
WEARPOS_LEFT_HAND = 5
WEARPOS_ARMS = 6
WEARPOS_LEGS = 7
WEARPOS_HEAD = 8
WEARPOS_HANDS = 9
WEARPOS_FEET = 10
WEARPOS_JAW = 11
WEARPOS_RING = 12
WEARPOS_QUIVER = 13

WEARPOS_TO_SLOT = {
    WEARPOS_HAT: SLOT_NAMES["head"],
    WEARPOS_BACK: SLOT_NAMES["cape"],
    WEARPOS_FRONT: SLOT_NAMES["neck"],
    WEARPOS_RIGHT_HAND: SLOT_NAMES["weapon"],
    WEARPOS_TORSO: SLOT_NAMES["body"],
    WEARPOS_LEFT_HAND: SLOT_NAMES["shield"],
    WEARPOS_LEGS: SLOT_NAMES["legs"],
    WEARPOS_HANDS: SLOT_NAMES["hands"],
    WEARPOS_FEET: SLOT_NAMES["feet"],
    WEARPOS_RING: SLOT_NAMES["ring"],
    WEARPOS_QUIVER: SLOT_NAMES["ammo"],
}

META_CACHE = 1 << 0
META_EQUIP = 1 << 1
META_EQUIP_STATS = 1 << 2
META_WEAPON = 1 << 3


def default_osrsreboxed_root() -> Path:
    raw = os.environ.get("RUNEC_OSRSREBOXED_DB")
    return Path(raw) if raw else OSRSREBOXED


def int_or(v, default):
    return int(v) if v is not None else default


def id_or_missing(v):
    return int(v) if v is not None else 0xFFFFFFFF


def parse_model_id(v) -> int | None:
    if isinstance(v, int):
        return v
    if isinstance(v, str):
        m = re.search(r"-?\d+", v)
        return int(m.group(0)) if m else None
    return None


def load_item_models(root: Path = OSRSREBOXED) -> dict[int, list[int]]:
    path = root / "docs" / "models-summary.json"
    by_item: dict[int, list[int]] = {}
    if not path.is_file():
        return by_item
    data = json.loads(path.read_text())
    for row in data.values():
        slot = MODEL_TYPES.get(row.get("model_type"))
        if slot is None:
            continue
        try:
            item_id = int(row.get("model_type_id"))
        except (TypeError, ValueError):
            continue
        model_id = parse_model_id(row.get("model_ids"))
        if model_id is None:
            continue
        models = by_item.setdefault(item_id, [0xFFFFFFFF] * 7)
        models[slot] = model_id
    return by_item


def load_wiki_supplemental_items(cache: Path = Path("tools/wiki_cache")) -> dict[int, dict]:
    rows: dict[int, dict] = {}
    for path in sorted(cache.glob("infobox_item_*.json")):
        data = json.loads(path.read_text())
        for row in data.get("bucket", []):
            ids = row.get("item_id") or []
            if not isinstance(ids, list):
                ids = [ids]
            for raw_id in ids:
                if not isinstance(raw_id, int) and not (
                        isinstance(raw_id, str) and raw_id.isdigit()):
                    continue
                item_id = int(raw_id)
                rows.setdefault(item_id, {
                    "id": item_id,
                    "name": row.get("item_name") or row.get("page_name")
                            or f"item_{item_id}",
                    "members": bool(row.get("is_members_only")),
                    "tradeable": False,
                    "stackable": False,
                    "noted": False,
                    "noteable": False,
                    "placeholder": False,
                    "quest_item": False,
                    "duplicate": False,
                    "incomplete": False,
                    "equipment": None,
                    "weapon": None,
                    "_wiki_supplemental": True,
                })
    return rows


def cache_model_links(cache_items: dict[int, dict]) -> dict[int, list[int]]:
    links: dict[int, list[int]] = {}
    for item_id, rec in cache_items.items():
        models = [0xFFFFFFFF] * 7
        if rec.get("ground_model_id") is not None:
            models[0] = int(rec["ground_model_id"])
        for i, model_id in enumerate(rec.get("male_model_ids") or []):
            if model_id is not None and i < 3:
                models[1 + i] = int(model_id)
        for i, model_id in enumerate(rec.get("female_model_ids") or []):
            if model_id is not None and i < 3:
                models[4 + i] = int(model_id)
        if any(m != 0xFFFFFFFF for m in models):
            links[item_id] = models
    return links


def cache_form_links(cache_items: dict[int, dict]) -> tuple[dict[int, int], dict[int, int]]:
    noted: dict[int, int] = {}
    placeholder: dict[int, int] = {}
    for item_id, rec in cache_items.items():
        note_id = rec.get("note_id")
        if note_id is not None:
            if rec.get("note_template_id") is not None:
                noted.setdefault(int(note_id), item_id)
            else:
                noted.setdefault(item_id, int(note_id))
        ph_id = rec.get("placeholder_id")
        if ph_id is not None:
            if rec.get("placeholder_template_id") is not None:
                placeholder.setdefault(int(ph_id), item_id)
            else:
                placeholder.setdefault(item_id, int(ph_id))
    return noted, placeholder


def cache_real_item(rec: dict | None) -> bool:
    name = (rec or {}).get("name")
    return bool(name and str(name).strip().lower() != "null")


def cache_item_has_wear_action(cache: dict | None) -> bool:
    if not cache:
        return False
    for action in cache.get("interface_ops") or []:
        if str(action or "").strip().lower() in {"wear", "wield", "equip", "hold"}:
            return True
    return False


def cache_wear_positions(cache: dict | None) -> list[int]:
    if not cache:
        return []
    out: list[int] = []
    for key in ("wearpos1", "wearpos2", "wearpos3"):
        value = cache.get(key)
        if value is not None:
            out.append(int(value))
    return out


def infer_cache_equip_slot(cache: dict | None) -> str | None:
    wearpos = set(cache_wear_positions(cache))
    if not wearpos:
        return None
    if WEARPOS_RIGHT_HAND in wearpos:
        return "weapon"
    if WEARPOS_LEFT_HAND in wearpos:
        return "shield"
    if WEARPOS_TORSO in wearpos or WEARPOS_ARMS in wearpos:
        return "body"
    if WEARPOS_LEGS in wearpos:
        return "legs"
    if WEARPOS_HANDS in wearpos:
        return "hands"
    if WEARPOS_FEET in wearpos:
        return "feet"
    if wearpos & {WEARPOS_HAT, WEARPOS_HEAD, WEARPOS_JAW}:
        return "head"
    if WEARPOS_BACK in wearpos:
        return "cape"
    if WEARPOS_FRONT in wearpos:
        return "neck"
    if WEARPOS_RING in wearpos:
        return "ring"
    if WEARPOS_QUIVER in wearpos:
        return "ammo"
    return None


def empty_equipment(slot: str) -> dict:
    return {
        "slot": slot,
        "requirements": {},
        "attack_stab": 0,
        "attack_slash": 0,
        "attack_crush": 0,
        "attack_magic": 0,
        "attack_ranged": 0,
        "defence_stab": 0,
        "defence_slash": 0,
        "defence_crush": 0,
        "defence_magic": 0,
        "defence_ranged": 0,
        "melee_strength": 0,
        "ranged_strength": 0,
        "magic_damage": 0,
        "prayer": 0,
    }


def apply_cache_equipment_fallback(rec: dict, cache: dict | None) -> dict:
    if (rec.get("equipment") or rec.get("noted") or rec.get("placeholder")
            or not cache_item_has_wear_action(cache)):
        return rec
    slot = infer_cache_equip_slot(cache)
    if not slot:
        return rec
    out = dict(rec)
    out["equipment"] = empty_equipment(slot)
    out["equipable_by_player"] = True
    if slot == "weapon":
        out["equipable_weapon"] = True
    out["_cache_equipment_fallback"] = True
    return out


SKILL_NAMES = {skill_id: name for name, skill_id in SKILL_IDS.items()
               if name != "runecrafting"}
COMBAT_SKILLS = {"attack", "defence", "strength", "hitpoints", "ranged", "prayer", "magic"}


def cache_bonuses(params: dict) -> dict | None:
    if not any(key in params for key in
               (*BONUS_PARAMS, *RANGED_STRENGTH_PARAMS, MAGIC_DAMAGE_PARAM)):
        return None
    out = {field: int(params.get(key, 0)) for key, field in BONUS_PARAMS.items()}
    out["ranged_strength"] = next((int(params[key]) for key in RANGED_STRENGTH_PARAMS
                                   if params.get(key)), 0)
    out["magic_damage"] = int(params.get(MAGIC_DAMAGE_PARAM, 0))
    return out


def merged_requirements(params: dict, known: dict) -> dict:
    """The cache lists at most two requirements. Keep a longer known set that
    agrees with them; otherwise the cache wins for combat skills."""
    listed: dict[str, int] = {}
    for skill_key, level_key in REQUIREMENT_PARAMS:
        name = SKILL_NAMES.get(params.get(skill_key, -1))
        level = int(params.get(level_key, 0) or 0)
        if name and level and name not in CREATION_REQUIREMENT_SKILLS:
            listed[name] = level
    if not listed or all(known.get(skill) == level for skill, level in listed.items()):
        return known
    extra = {skill: level for skill, level in known.items()
             if skill not in COMBAT_SKILLS and skill not in listed}
    return listed | extra


def apply_cache_combat(rec: dict, cache: dict | None) -> dict:
    """Overlay cache bonuses, requirements, speed, range and weapon category."""
    item_id = int(rec["id"])
    if not cache or rec.get("noted") or rec.get("placeholder"):
        return rec
    params = cache.get("params") or {}
    out = dict(rec)
    weapon_row = WEAPON_TABLE.get(item_id)
    if weapon_row and not out.get("equipment"):
        out["equipment"] = empty_equipment("weapon")
        out["equipable_by_player"] = True
        out["equipable_weapon"] = True
        out["_cache_equipment_fallback"] = True
    equipment = out.get("equipment")
    slot = infer_cache_equip_slot(cache)
    if equipment and slot and EQUIP_SLOT_KEYS.get(equipment.get("slot"), equipment.get("slot")) != slot:
        # The record describes some other item; only the cache's slot is known.
        equipment = empty_equipment(slot)
        out.pop("weapon", None)
        out["equipable_weapon"] = slot == "weapon"
        out["_cache_equipment_fallback"] = True
    if equipment:
        equipment = dict(equipment)
        bonuses = cache_bonuses(params)
        if bonuses is not None:
            equipment.update(bonuses)
            out.pop("_cache_equipment_fallback", None)
        elif not out.get("_cache_equipment_fallback"):
            # osrsreboxed stores whole percents; IDEF stores tenths.
            equipment["magic_damage"] = 10 * int_or(equipment.get("magic_damage"), 0)
        equipment["requirements"] = merged_requirements(
            params, equipment.get("requirements") or {})
        out["equipment"] = equipment
    if weapon_row:
        if cache.get("name") != weapon_row["name"]:
            raise ValueError(f"item {item_id}: weapons.tsv says {weapon_row['name']!r}, "
                             f"cache says {cache.get('name')!r}")
        known = out.get("weapon") or {}
        weapon_type = weapon_row["weapon_type"]
        out["weapon"] = {
            "attack_speed": int(params.get(ATTACK_SPEED_PARAM) or known.get("attack_speed") or 4),
            "attack_range": int(params.get(ATTACK_RANGE_PARAM) or -1),
            "weapon_type": weapon_type,
            "stances": [{"combat_style": name, "attack_style": style} for name, style in
                        WEAPON_STANCES.get(weapon_type, _MELEE4)],
        }
    return out


class OptionalItemDB:
    """Small no-op adapter used when optional osrsreboxed item JSON is absent."""

    available = False

    def item(self, _item_id: int) -> dict | None:
        return None

    def iter_items(self):
        return iter(())


def open_optional_item_db(root: Path = OSRSREBOXED):
    try:
        db = OsrsreboxedDB(root)
        db.available = True
        return db
    except AssertionError as exc:
        print(f"warning: optional osrsreboxed item source unavailable: {exc}", file=sys.stderr)
        return OptionalItemDB()


def cache_item_record(cache: dict) -> dict:
    item_id = int(cache["id"])
    return {
        "id": item_id,
        "name": cache.get("name") or f"item_{item_id}",
        "members": bool(cache.get("members")),
        "tradeable": bool(cache.get("tradeable")),
        "stackable": bool(cache.get("stackable")),
        "cost": int_or(cache.get("cost"), 0),
        "weight_grams": int_or(cache.get("weight"), 0),
        "highalch": int_or(cache.get("cost"), 0) * 3 // 5,
        "lowalch": int_or(cache.get("cost"), 0) * 2 // 5,
        "noted": cache.get("note_template_id") is not None,
        "noteable": False,
        "placeholder": cache.get("placeholder_template_id") is not None,
        "quest_item": False,
        "duplicate": False,
        "incomplete": False,
        "equipment": None,
        "weapon": None,
        "_cache_fill": True,
    }


def overlay_cache(rec: dict, cache: dict | None,
                  noted_links: dict[int, int],
                  placeholder_links: dict[int, int],
                  cache_items: dict[int, dict]) -> dict:
    out = dict(rec)
    item_id = int(out["id"])
    if cache_real_item(cache):
        out["name"] = cache["name"]
        out["members"] = bool(cache.get("members"))
        out["stackable"] = bool(cache.get("stackable"))
        out["cost"] = int_or(cache.get("cost"), out.get("cost") or 0)
        # The cache flag means Grand Exchange tradeable, so coins read false.
        if out.get("_wiki_supplemental") or out.get("_cache_fill"):
            out["tradeable"] = bool(cache.get("tradeable"))
    if cache:
        out["weight_grams"] = int_or(cache.get("weight"), 0)
        out["examine"] = cache.get("examine") or ""
        out["ground_ops"] = list(cache.get("ground_ops") or [None] * 5)
        out["interface_ops"] = list(cache.get("interface_ops") or [None] * 5)
        out["wearpos1"] = cache.get("wearpos1")
        out["wearpos2"] = cache.get("wearpos2")
        out["wearpos3"] = cache.get("wearpos3")
        if cache.get("note_template_id") is not None and cache.get("note_id") is not None:
            out["noted"] = True
            out["linked_id_item"] = int(cache["note_id"])
            base = cache_items.get(int(cache["note_id"]))
            if cache_real_item(base):
                out["name"] = base["name"]
        if (cache.get("placeholder_template_id") is not None
                and cache.get("placeholder_id") is not None):
            out["placeholder"] = True
            out["linked_id_item"] = int(cache["placeholder_id"])
            base = cache_items.get(int(cache["placeholder_id"]))
            if cache_real_item(base):
                out["name"] = base["name"]
    out.setdefault("tradeable", False)
    out.setdefault("stackable", False)
    out.setdefault("members", False)
    out.setdefault("noted", False)
    out.setdefault("noteable", False)
    out.setdefault("placeholder", False)
    out.setdefault("quest_item", False)
    out.setdefault("duplicate", False)
    out.setdefault("equipment", None)
    out.setdefault("weapon", None)
    if out.get("linked_id_noted") is None and item_id in noted_links:
        out["linked_id_noted"] = noted_links[item_id]
        out["noteable"] = True
    if out.get("linked_id_placeholder") is None and item_id in placeholder_links:
        out["linked_id_placeholder"] = placeholder_links[item_id]
    return out


def write_incomplete_triage(path: Path, rows: list[dict]):
    counts: dict[str, int] = {}
    samples: dict[str, list[str]] = {}
    for row in rows:
        status = row["status"]
        counts[status] = counts.get(status, 0) + 1
        samples.setdefault(status, [])
        if len(samples[status]) < 20:
            samples[status].append(f"  id={row['id']:<6} {row['name']}")

    lines = ["Incomplete item row triage", ""]
    for key in sorted(counts):
        lines.append(f"{key}: {counts[key]}")
    lines.append("")
    for key in sorted(samples):
        lines.append(f"--- {key} samples ---")
        lines.extend(samples[key])
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines).rstrip() + "\n")


def write_wiki_supplemental_triage(path: Path, rows: list[dict]):
    counts: dict[str, int] = {}
    samples: dict[str, list[str]] = {}
    for row in rows:
        status = row["status"]
        counts[status] = counts.get(status, 0) + 1
        samples.setdefault(status, [])
        if len(samples[status]) < 40:
            samples[status].append(f"  id={row['id']:<6} {row['name']}")
    lines = ["Wiki supplemental item triage", ""]
    for key in sorted(counts):
        lines.append(f"{key}: {counts[key]}")
    lines.append("")
    for key in sorted(samples):
        lines.append(f"--- {key} samples ---")
        lines.extend(samples[key])
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines).rstrip() + "\n")


def classify_item(rec: dict) -> int:
    if rec.get("placeholder"):
        return ITEM_KIND_PLACEHOLDER
    if rec.get("noted"):
        return ITEM_KIND_NOTED
    if rec.get("weapon"):
        return ITEM_KIND_WEAPON
    if rec.get("equipment"):
        return ITEM_KIND_EQUIPMENT
    if rec.get("quest_item"):
        return ITEM_KIND_QUEST
    return ITEM_KIND_GENERIC


def pack_equipment(eq: dict) -> bytes:
    """Pack the equipment slot, complete requirements, and bonuses.
    Layout: slot u8, req_count u8, combat_level u8,
            (req_skill u8 + req_level u8)*req_count,
            14 × i16 bonuses in order stab/slash/crush/magic_att/ranged_att/
            def_stab/def_slash/def_crush/def_magic/def_ranged/str/
            ranged_str/magic_dmg/prayer.
    """
    slot = SLOT_NAMES.get(eq.get("slot"), 0xFF)
    reqs = eq.get("requirements") or {}
    unknown = sorted(str(skill) for skill in reqs
                     if skill not in SKILL_IDS and skill != "combat")
    if unknown:
        raise ValueError(f"unknown equipment requirement skills: {unknown}")
    req_pairs = [(SKILL_IDS[s], int(lvl)) for s, lvl in reqs.items()
                 if s in SKILL_IDS]
    if len(req_pairs) > len(SKILL_IDS):
        raise ValueError(f"too many equipment requirements: {len(req_pairs)}")

    buf = bytearray()
    combat_level = int_or(reqs.get("combat"), 0)
    if combat_level < 0 or combat_level > 255:
        raise ValueError(f"invalid combat requirement: {combat_level}")
    buf += struct.pack("<BBB", slot & 0xFF, len(req_pairs), combat_level)
    for sid, lvl in req_pairs:
        buf += struct.pack("<BB", sid, lvl)

    bonuses = [
        int_or(eq.get("attack_stab"), 0),
        int_or(eq.get("attack_slash"), 0),
        int_or(eq.get("attack_crush"), 0),
        int_or(eq.get("attack_magic"), 0),
        int_or(eq.get("attack_ranged"), 0),
        int_or(eq.get("defence_stab"), 0),
        int_or(eq.get("defence_slash"), 0),
        int_or(eq.get("defence_crush"), 0),
        int_or(eq.get("defence_magic"), 0),
        int_or(eq.get("defence_ranged"), 0),
        int_or(eq.get("melee_strength"), 0),
        int_or(eq.get("ranged_strength"), 0),
        int_or(eq.get("magic_damage"), 0),
        int_or(eq.get("prayer"), 0),
    ]
    for b in bonuses:
        buf += struct.pack("<h", max(-32768, min(32767, b)))
    return bytes(buf)


def pack_weapon(wp: dict) -> bytes:
    """Pack: attack_speed u8, weapon_type u8, stance_bits u8, stance_count u8,
       per stance: combat_style_name_len u8 + string (variable)."""
    atk_speed = int_or(wp.get("attack_speed"), 4)
    if wp.get("weapon_type") not in WEAPON_TYPES:
        raise ValueError(f"unknown weapon type: {wp.get('weapon_type')}")
    wt_id = WEAPON_TYPES[wp.get("weapon_type")]
    attack_range = int_or(wp.get("attack_range"), -1)
    if attack_range < -1 or attack_range > 127:
        raise ValueError(f"invalid weapon attack range: {attack_range}")
    stances = wp.get("stances") or []

    stance_bits = 0
    for s in stances:
        style = s.get("attack_style")
        if style in STANCE_BITS:
            stance_bits |= STANCE_BITS[style]

    buf = bytearray()
    buf += struct.pack("<BBBBb", atk_speed & 0xFF, wt_id & 0xFF,
                       stance_bits & 0xFF, min(len(stances), 255),
                       attack_range)
    for s in stances:
        cs = (s.get("combat_style") or "").encode("ascii", errors="replace")[:31]
        buf += struct.pack("<B", len(cs))
        buf += cs
    return bytes(buf)


def reviewed_attack_range(name: str, weapon_type: int, current: int) -> int:
    name = name.lower()
    if weapon_type == WEAPON_TYPES["salamander"]:
        return 1
    if weapon_type == WEAPON_TYPES["polearm"]:
        return 2
    if weapon_type not in {7, 9, 11, 12, 16, 23, 25, 28, 35}:
        return 1
    if current > 0:
        return current  # cache param
    if "twisted bow" in name or "dark bow" in name:
        return 10
    if "shortbow" in name:
        return 7
    if "longbow" in name or "comp bow" in name:
        return 10
    if "dart" in name:
        return 3
    if "knife" in name:
        return 4
    if "chinchompa" in name or "ballista" in name:
        return 9
    if "trident" in name:
        return 7
    if "tumeken" in name:
        return 8
    if "crossbow" in name:
        if name == "crossbow":
            return 10
        if "phoenix" in name:
            return 5
        if "armadyl" in name or "zaryte" in name or "karil" in name:
            return 8
        if "dorgeshuun" in name:
            return 6
        if "hunter" in name and "dragon hunter" not in name:
            return 8
        return 7
    return current


def build_record(rec: dict, model_links: dict[int, list[int]]) -> bytes | None:
    """One IDEF record. Returns bytes or None if record should be skipped."""
    if rec.get("incomplete"):
        return None

    item_id = int(rec["id"])
    models = model_links.get(item_id, [0xFFFFFFFF] * 7)
    has_wear_model = any(m != 0xFFFFFFFF for m in models[1:])
    eq = rec.get("equipment")
    wp = rec.get("weapon")
    equip_player = bool(rec.get("equipable_by_player"))
    equip_weapon = bool(rec.get("equipable_weapon"))
    if eq and has_wear_model and not rec.get("noted") and not rec.get("placeholder"):
        equip_player = True
        if eq.get("slot") == "weapon":
            equip_weapon = True

    flags = 0
    # Certificate linking makes notes stackable even without cache opcode 11.
    if rec.get("stackable") or rec.get("noted"): flags |= F_STACKABLE
    if rec.get("tradeable"):       flags |= F_TRADEABLE
    if rec.get("members"):         flags |= F_MEMBERS
    if rec.get("quest_item"):      flags |= F_QUEST_ITEM
    if rec.get("noted"):           flags |= F_NOTED
    if rec.get("noteable"):        flags |= F_NOTEABLE
    if rec.get("placeholder"):     flags |= F_PLACEHOLDER
    if equip_player:               flags |= F_EQUIP_PLAYER
    if equip_weapon:               flags |= F_EQUIP_WEAPON
    if rec.get("duplicate"):       flags |= F_DUPLICATE
    if eq:                         flags |= F_HAS_EQUIPMENT
    if wp:                         flags |= F_HAS_WEAPON

    name = (rec.get("name") or "").encode("latin-1", errors="replace")[:63]

    buf = bytearray()
    buf += struct.pack("<I", rec["id"])
    buf += struct.pack("<H", flags)
    buf += struct.pack("<B", classify_item(rec))
    buf += struct.pack("<B", len(name))
    buf += name
    weight_grams = int_or(rec.get("weight_grams"),
                          int(round((rec.get("weight") or 0.0) * 1000.0)))
    if weight_grams < -2147483648 or weight_grams > 2147483647:
        raise ValueError(f"item {item_id}: weight out of i32 range")
    buf += struct.pack("<i", weight_grams)
    buf += struct.pack("<I", int_or(rec.get("highalch"), 0))
    buf += struct.pack("<I", int_or(rec.get("lowalch"), 0))
    buf += struct.pack("<I", int_or(rec.get("cost"), 0))
    buf += struct.pack("<I", id_or_missing(rec.get("linked_id_item")))
    buf += struct.pack("<I", id_or_missing(rec.get("linked_id_noted")))
    buf += struct.pack("<I", id_or_missing(rec.get("linked_id_placeholder")))
    buf += struct.pack("<I", id_or_missing(rec.get("buy_limit")))
    for model_id in models:
        buf += struct.pack("<I", id_or_missing(model_id))

    wear_positions = [rec.get(f"wearpos{i}") for i in range(1, 4)]
    conflict_mask = 0
    for wearpos in wear_positions:
        slot = WEARPOS_TO_SLOT.get(wearpos)
        if slot is not None:
            conflict_mask |= 1 << slot
    if eq:
        slot = SLOT_NAMES.get(eq.get("slot"))
        if slot is not None:
            conflict_mask |= 1 << slot
    metadata = 0
    if rec.get("_cache_fill") or rec.get("wearpos1") is not None \
            or rec.get("interface_ops") is not None:
        metadata |= META_CACHE
    if eq:
        metadata |= META_EQUIP
        if not rec.get("_cache_equipment_fallback"):
            metadata |= META_EQUIP_STATS
    if wp:
        metadata |= META_WEAPON
    buf += struct.pack("<H", metadata)
    buf += struct.pack("<BBB", *(0xFF if value is None else int(value)
                                 for value in wear_positions))
    buf += struct.pack("<H", conflict_mask)
    for action_set in (rec.get("interface_ops"), rec.get("ground_ops")):
        actions = list(action_set or [])[:5]
        actions.extend([None] * (5 - len(actions)))
        for action in actions:
            raw = str(action or "").encode("latin-1", errors="replace")[:23]
            buf += struct.pack("<B", len(raw)) + raw
    examine = str(rec.get("examine") or "").encode(
        "latin-1", errors="replace")[:127]
    buf += struct.pack("<B", len(examine)) + examine

    if eq:
        buf += pack_equipment(eq)
    if wp:
        wp = dict(wp, attack_range=reviewed_attack_range(rec.get("name", ""),
            WEAPON_TYPES[wp["weapon_type"]], int_or(wp.get("attack_range"), -1)))
        buf += pack_weapon(wp)
    return bytes(buf)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--output", type=Path)
    p.add_argument("--report", type=Path, default=Path("tools/reports/items_full.txt"))
    p.add_argument("--triage-report", type=Path,
                   default=Path("tools/reports/items_incomplete_triage.txt"))
    p.add_argument("--wiki-triage-report", type=Path,
                   default=Path("tools/reports/items_wiki_supplemental_triage.txt"))
    p.add_argument("--cache-dir", type=Path, default=CACHE_DIR)
    p.add_argument("--osrsreboxed-root", type=Path,
                   default=default_osrsreboxed_root(),
                   help=("optional osrsreboxed-db checkout used for equipment/"
                         "weapon metadata during validation rebuilds; may also "
                         "be provided with RUNEC_OSRSREBOXED_DB"))
    p.add_argument("--require-osrsreboxed", action="store_true",
                   help="fail instead of emitting cache-only non-equipment item defs")
    p.add_argument("--limit", type=int, default=0, help="debug: cap records")
    args = p.parse_args()
    if args.output is None:
        p.error("--output is required")

    db = open_optional_item_db(args.osrsreboxed_root)
    if args.require_osrsreboxed and not getattr(db, "available", False):
        raise SystemExit(
            "osrsreboxed item source is required for equipment/weapon metadata; "
            "pass --osrsreboxed-root or set RUNEC_OSRSREBOXED_DB"
        )
    model_links = load_item_models(args.osrsreboxed_root)
    cache_items = load_cache_item_defs(require_cache_dir(args.cache_dir))
    cache_models = cache_model_links(cache_items)
    model_links.update(cache_models)
    noted_links, placeholder_links = cache_form_links(cache_items)
    wiki_items = load_wiki_supplemental_items()
    wiki_bonus_extra = dict(cache_items)
    wiki_bonus_extra.update(wiki_items)
    wiki_bonus = load_wiki_bonus_overrides(db, wiki_bonus_extra)
    wiki_bonus_overrides = wiki_bonus["overrides"]
    records = []
    skipped_incomplete = 0
    resolved_incomplete = 0
    exported_dup = 0
    exported_equipment = 0
    exported_weapon = 0
    exported_model_links = 0
    exported_link_item = 0
    exported_link_noted = 0
    exported_link_placeholder = 0
    exported_wiki_supplemental = 0
    exported_wiki_cache_backed = 0
    exported_wiki_bonus_overrides = 0
    exported_cache_equipment_fallbacks = 0
    skipped_wiki_supplemental = 0
    exported_ids: set[int] = set()
    incomplete_triage: list[dict] = []
    wiki_triage: list[dict] = []

    total = 0
    for rec in db.iter_items():
        total += 1
        item_id = int(rec["id"])
        cache = cache_items.get(item_id)
        if rec.get("incomplete"):
            if cache_real_item(cache) or item_id in wiki_items:
                resolved_incomplete += 1
                rec = dict(rec)
                rec["incomplete"] = False
                rec["_cache_fill"] = True
                incomplete_triage.append({
                    "id": item_id,
                    "name": rec.get("name") or (cache or {}).get("name") or f"item_{item_id}",
                    "status": "real_item_resolved_from_cache_or_wiki",
                })
            else:
                skipped_incomplete += 1
                status = "ignored_null_cache_form" if cache else "ignored_stale_or_non_item"
                incomplete_triage.append({
                    "id": item_id,
                    "name": rec.get("name") or f"item_{item_id}",
                    "status": status,
                })
                continue
        rec = overlay_cache(rec, cache, noted_links, placeholder_links, cache_items)
        if item_id in wiki_bonus_overrides and rec.get("equipment"):
            rec = dict(rec)
            rec["equipment"] = dict(wiki_bonus_overrides[item_id])
            exported_wiki_bonus_overrides += 1
        had_equipment = bool(rec.get("equipment"))
        rec = apply_cache_equipment_fallback(rec, cache)
        exported_cache_equipment_fallbacks += int(
            not had_equipment and bool(rec.get("equipment"))
        )
        rec = apply_cache_combat(rec, cache)
        packed = build_record(rec, model_links)
        if packed is None:
            continue
        records.append((item_id, packed))
        exported_ids.add(item_id)
        exported_dup += bool(rec.get("duplicate"))
        exported_equipment += bool(rec.get("equipment"))
        exported_weapon += bool(rec.get("weapon"))
        exported_model_links += int(item_id in model_links)
        exported_link_item += rec.get("linked_id_item") is not None
        exported_link_noted += rec.get("linked_id_noted") is not None
        exported_link_placeholder += rec.get("linked_id_placeholder") is not None
        if args.limit and len(records) >= args.limit:
            break

    exported_null_cache_rows = 0
    for item_id, cache in sorted(cache_items.items()):
        if item_id in exported_ids:
            continue
        rec = overlay_cache(cache_item_record(cache), cache,
                            noted_links, placeholder_links, cache_items)
        if item_id in wiki_bonus_overrides:
            rec = dict(rec)
            rec["equipment"] = dict(wiki_bonus_overrides[item_id])
            exported_wiki_bonus_overrides += 1
        had_equipment = bool(rec.get("equipment"))
        rec = apply_cache_equipment_fallback(rec, cache)
        exported_cache_equipment_fallbacks += int(
            not had_equipment and bool(rec.get("equipment"))
        )
        rec = apply_cache_combat(rec, cache)
        packed = build_record(rec, model_links)
        if packed is None:
            continue
        records.append((item_id, packed))
        exported_ids.add(item_id)
        exported_null_cache_rows += int(not cache_real_item(cache))
        exported_equipment += bool(rec.get("equipment"))
        exported_weapon += bool(rec.get("weapon"))
        exported_model_links += int(item_id in model_links)
        exported_link_item += rec.get("linked_id_item") is not None
        exported_link_noted += rec.get("linked_id_noted") is not None
        exported_link_placeholder += rec.get("linked_id_placeholder") is not None
        if args.limit and len(records) >= args.limit:
            break

    if not args.limit:
        for item_id, rec in sorted(wiki_items.items()):
            if item_id in exported_ids:
                continue
            if not cache_real_item(cache_items.get(item_id)):
                skipped_wiki_supplemental += 1
                wiki_triage.append({
                    "id": item_id,
                    "name": rec.get("name") or f"item_{item_id}",
                    "status": "ignored_no_current_cache_item",
                })
                continue
            rec = overlay_cache(rec, cache_items.get(item_id),
                                noted_links, placeholder_links, cache_items)
            if item_id in wiki_bonus_overrides:
                rec = dict(rec)
                rec["equipment"] = dict(wiki_bonus_overrides[item_id])
                exported_wiki_bonus_overrides += 1
            had_equipment = bool(rec.get("equipment"))
            rec = apply_cache_equipment_fallback(rec, cache_items.get(item_id))
            exported_cache_equipment_fallbacks += int(
                not had_equipment and bool(rec.get("equipment"))
            )
            rec = apply_cache_combat(rec, cache_items.get(item_id))
            packed = build_record(rec, model_links)
            if packed is None:
                continue
            records.append((item_id, packed))
            exported_ids.add(item_id)
            exported_wiki_supplemental += 1
            exported_wiki_cache_backed += int(cache_real_item(cache_items.get(item_id)))
            exported_equipment += bool(rec.get("equipment"))
            exported_weapon += bool(rec.get("weapon"))
            exported_model_links += int(item_id in model_links)
            exported_link_item += rec.get("linked_id_item") is not None
            exported_link_noted += rec.get("linked_id_noted") is not None
            exported_link_placeholder += rec.get("linked_id_placeholder") is not None
            wiki_triage.append({
                "id": item_id,
                "name": rec.get("name") or f"item_{item_id}",
                "status": "real_item_resolved_from_cache",
            })

    records.sort(key=lambda x: x[0])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("wb") as f:
        f.write(struct.pack("<III", IDEF_MAGIC, IDEF_VERSION, len(records)))
        for _, packed in records:
            f.write(struct.pack("<I", len(packed)))
            f.write(packed)

    sz = args.output.stat().st_size
    write_incomplete_triage(args.triage_report, incomplete_triage)
    write_wiki_supplemental_triage(args.wiki_triage_report, wiki_triage)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(
        "\n".join([
            "Full item export",
            "",
            f"osrsreboxed item source available: {bool(getattr(db, 'available', False))}",
            f"source rows scanned: {total}",
            f"cache item configs loaded: {len(cache_items)}",
            f"exported records: {len(records)}",
            f"skipped incomplete rows: {skipped_incomplete}",
            f"resolved incomplete rows: {resolved_incomplete}",
            f"duplicate/form rows preserved: {exported_dup}",
            f"wiki-only supplemental rows: {exported_wiki_supplemental}",
            f"wiki-only supplemental rows cache-backed: {exported_wiki_cache_backed}",
            f"wiki-only supplemental rows ignored as non-current/non-item: {skipped_wiki_supplemental}",
            f"cache placeholder/null rows preserved: {exported_null_cache_rows}",
            f"wiki bonus overrides applied: {exported_wiki_bonus_overrides}",
            f"cache equipment fallbacks applied: {exported_cache_equipment_fallbacks}",
            f"wiki bonus rows not resolved to current item defs: {len(wiki_bonus['unresolved'])}",
            f"equipment rows: {exported_equipment}",
            f"weapon rows: {exported_weapon}",
            f"rows with model linkage: {exported_model_links}",
            f"rows with linked_id_item: {exported_link_item}",
            f"rows with linked_id_noted: {exported_link_noted}",
            f"rows with linked_id_placeholder: {exported_link_placeholder}",
            f"max exported item id: {max((rid for rid, _ in records), default=-1)}",
            "",
            "Deferred work:",
            "- item effects are curated source rows; runtime loading/behavior is a later gameplay task",
            "- item mesh decoding/rendering is owned by rc-viewer/tools, not rc-core item defs",
            "",
        ]) + "\n"
    )
    print(f"items total scanned: {total}")
    print(f"  duplicate/form rows preserved: {exported_dup}")
    print(f"  wiki-only supplemental rows: {exported_wiki_supplemental}")
    print(f"  cache placeholder/null rows preserved: {exported_null_cache_rows}")
    print(f"  skipped incomplete: {skipped_incomplete}")
    print(f"  resolved incomplete: {resolved_incomplete}")
    print(f"  exported records: {len(records)}")
    print(f"wrote {args.output} ({sz:,} bytes, ~{sz//max(1,len(records))} B/item)")
    print(f"wrote {args.report}")
    print(f"wrote {args.triage_report}")
    print(f"wrote {args.wiki_triage_report}")


if __name__ == "__main__":
    main()
