#!/usr/bin/env python3
"""Export item definitions with equipment bonuses → data/defs/items.bin (IDEF).

Sources:
  - osrsreboxed-db (primary) — per-item metadata, equipment bonuses, weapon
    attack speed + stances.
  - b237 cache (optional, for cross-validation only; osrsreboxed is already
    decoded from a modern cache).

Excluded per ignore.md:
  - No GE price field (single-player, no live market).
  - `cost` (shop base price) + `highalch` + `lowalch` are kept — those are
    static values baked into the cache, not a market.
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from cache_item_defs import load_cache_item_defs
from database_sources import OsrsreboxedDB, OSRSREBOXED
from source_paths import CACHE_DIR, require_cache_dir
from wiki_item_bonuses import load_wiki_bonus_overrides

IDEF_MAGIC = 0x49444546  # "IDEF"
IDEF_VERSION = 2

# Equipment slots — must match rc-core/types.h RcEquipSlot.
SLOT_NAMES = {
    "head": 0, "cape": 1, "neck": 2, "weapon": 3, "body": 4,
    "shield": 5, "legs": 6, "hands": 7, "feet": 8, "ring": 9,
    "ammo": 10, "2h": 3,
}

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
    "partisan": 30, "atlatl": 31,
}

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
        "weight": (int_or(cache.get("weight"), 0) / 100.0),
        "highalch": 0,
        "lowalch": 0,
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
    fill_identity = bool(out.get("_wiki_supplemental") or out.get("_cache_fill"))
    if fill_identity and cache_real_item(cache):
        out["name"] = cache["name"]
        out["members"] = bool(cache.get("members"))
        out["tradeable"] = bool(cache.get("tradeable"))
        out["stackable"] = bool(cache.get("stackable"))
        out["cost"] = int_or(cache.get("cost"), out.get("cost") or 0)
    if cache:
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
    """Pack the 13-bonus + slot + up-to-4-reqs equipment block.
    Layout: slot u8, req_count u8, (req_skill u8 + req_level u8)*req_count,
            13 × i16 bonuses in order stab/slash/crush/magic_att/ranged_att/
            def_stab/def_slash/def_crush/def_magic/def_ranged/str/
            ranged_str/magic_dmg/prayer (14 total? that's 14. Let me
            recount): stab(att) slash(att) crush(att) magic(att) ranged(att)
            = 5 attack; stab(def) slash(def) crush(def) magic(def) ranged(def)
            = 5 defence; melee_str, ranged_str, magic_dmg, prayer = 4 extras
            = 14 total i16.
    """
    slot = SLOT_NAMES.get(eq.get("slot"), 0xFF)
    reqs = eq.get("requirements") or {}
    req_pairs = [(SKILL_IDS.get(s, 0xFF), lvl) for s, lvl in reqs.items()
                 if SKILL_IDS.get(s) is not None]
    req_pairs = req_pairs[:4]

    buf = bytearray()
    buf += struct.pack("<BB", slot & 0xFF, len(req_pairs))
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
    wt_id = WEAPON_TYPES.get(wp.get("weapon_type"), 0)
    stances = wp.get("stances") or []

    stance_bits = 0
    for s in stances:
        style = s.get("attack_style")
        if style in STANCE_BITS:
            stance_bits |= STANCE_BITS[style]

    buf = bytearray()
    buf += struct.pack("<BBBB", atk_speed & 0xFF, wt_id & 0xFF,
                       stance_bits & 0xFF, min(len(stances), 255))
    for s in stances:
        cs = (s.get("combat_style") or "").encode("ascii", errors="replace")[:31]
        buf += struct.pack("<B", len(cs))
        buf += cs
    return bytes(buf)


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
    if rec.get("stackable"):       flags |= F_STACKABLE
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
    # Weight in centigrams (whole number 0.453 kg = 45 cg)
    weight_cg = int(round((rec.get("weight") or 0.0) * 100.0))
    buf += struct.pack("<H", max(0, min(65535, weight_cg)))
    buf += struct.pack("<I", int_or(rec.get("highalch"), 0))
    buf += struct.pack("<I", int_or(rec.get("lowalch"), 0))
    buf += struct.pack("<I", int_or(rec.get("cost"), 0))
    buf += struct.pack("<I", id_or_missing(rec.get("linked_id_item")))
    buf += struct.pack("<I", id_or_missing(rec.get("linked_id_noted")))
    buf += struct.pack("<I", id_or_missing(rec.get("linked_id_placeholder")))
    buf += struct.pack("<I", id_or_missing(rec.get("buy_limit")))
    for model_id in models:
        buf += struct.pack("<I", id_or_missing(model_id))

    if eq:
        buf += pack_equipment(eq)
    if wp:
        buf += pack_weapon(wp)
    return bytes(buf)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--report", type=Path, default=Path("tools/reports/items_full.txt"))
    p.add_argument("--triage-report", type=Path,
                   default=Path("tools/reports/items_incomplete_triage.txt"))
    p.add_argument("--wiki-triage-report", type=Path,
                   default=Path("tools/reports/items_wiki_supplemental_triage.txt"))
    p.add_argument("--cache-dir", type=Path, default=CACHE_DIR)
    p.add_argument("--limit", type=int, default=0, help="debug: cap records")
    args = p.parse_args()

    db = open_optional_item_db()
    model_links = load_item_models()
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
