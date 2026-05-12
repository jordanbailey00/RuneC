#!/usr/bin/env python3
"""Resolve OSRS Wiki equipment bonuses to item IDs."""
from __future__ import annotations

import json
import re
from collections import defaultdict
from pathlib import Path
from typing import Any

CACHE = Path(__file__).resolve().parent / "wiki_cache"

WIKI_TO_EQ = {
    "stab_attack_bonus": "attack_stab",
    "slash_attack_bonus": "attack_slash",
    "crush_attack_bonus": "attack_crush",
    "magic_attack_bonus": "attack_magic",
    "range_attack_bonus": "attack_ranged",
    "stab_defence_bonus": "defence_stab",
    "slash_defence_bonus": "defence_slash",
    "crush_defence_bonus": "defence_crush",
    "magic_defence_bonus": "defence_magic",
    "range_defence_bonus": "defence_ranged",
    "strength_bonus": "melee_strength",
    "ranged_strength_bonus": "ranged_strength",
    "magic_damage_bonus": "magic_damage",
    "prayer_bonus": "prayer",
}


def load_bucket(bucket: str) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for p in sorted(CACHE.glob(f"{bucket}_*.json")):
        rows.extend(json.loads(p.read_text()).get("bucket", []))
    return rows


def parse_int(v, default=None):
    if v is None:
        return default
    if isinstance(v, (int, float)):
        return int(v)
    m = re.search(r"-?\d+", str(v))
    return int(m.group(0)) if m else default


def norm_name(s: str) -> str:
    s = s.lower().replace("’", "'")
    s = re.sub(r"<[^>]+>", "", s)
    s = re.sub(r"\[\[category:.*?\]\]", "", s)
    return re.sub(r"\s+", " ", s).strip()


def bonus_score(row: dict[str, Any]) -> int:
    return sum(abs(parse_int(row.get(k), 0) or 0) for k in WIKI_TO_EQ)


def equipment_score(eq: dict[str, Any]) -> int:
    return sum(abs(parse_int(eq.get(k), 0) or 0) for k in WIKI_TO_EQ.values())


def empty_equipment(row: dict[str, Any]) -> dict[str, Any]:
    return {
        "slot": str(row.get("equipment_slot") or "").strip().lower(),
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


def row_to_equipment(row: dict[str, Any], base: dict[str, Any] | None) -> dict[str, Any]:
    eq = dict(base) if base else empty_equipment(row)
    for wiki_key, eq_key in WIKI_TO_EQ.items():
        v = parse_int(row.get(wiki_key))
        if v is not None:
            eq[eq_key] = v
    if row.get("equipment_slot"):
        eq["slot"] = str(row["equipment_slot"]).strip().lower()
    return eq


def item_name_index(db, extra_items: dict[int, dict[str, Any]] | None = None) -> dict[str, list[int]]:
    out: dict[str, list[int]] = defaultdict(list)
    for row in load_bucket("infobox_item"):
        ids = row.get("item_id") or []
        if not isinstance(ids, list):
            ids = [ids]
        names = [row.get("item_name"), row.get("page_name")]
        for raw_id in ids:
            try:
                item_id = int(raw_id)
            except (TypeError, ValueError):
                continue
            for name in names:
                if not name:
                    continue
                key = norm_name(str(name))
                if item_id not in out[key]:
                    out[key].append(item_id)
    for rec in db.iter_items():
        key = norm_name(rec.get("name") or "")
        item_id = int(rec["id"])
        if key and item_id not in out[key]:
            out[key].append(item_id)
    for item_id, rec in (extra_items or {}).items():
        key = norm_name(rec.get("name") or "")
        if key and item_id not in out[key]:
            out[key].append(item_id)
    return out


def best_bonus_rows() -> tuple[dict[str, dict[str, Any]], int]:
    best: dict[str, dict[str, Any]] = {}
    conflicts = 0
    for row in load_bucket("infobox_bonuses"):
        name = row.get("page_name")
        if not name:
            continue
        if bonus_score(row) == 0:
            continue
        key = norm_name(name)
        if key in best:
            conflicts += 1
            if bonus_score(row) <= bonus_score(best[key]):
                continue
        best[key] = row
    return best, conflicts


def item_record(db, extra_items: dict[int, dict[str, Any]], item_id: int) -> dict[str, Any] | None:
    return db.item(item_id) or extra_items.get(item_id)


def choose_item_id(db, extra_items: dict[int, dict[str, Any]],
                   name: str, candidates: list[int]) -> int | None:
    best_id = None
    best_score = -1
    for item_id in sorted(set(candidates)):
        rec = item_record(db, extra_items, item_id)
        if not rec:
            continue
        if rec.get("noted") or rec.get("placeholder"):
            continue
        score = equipment_score(rec.get("equipment") or {})
        score += 100000 if norm_name(rec.get("name") or "") == name else 0
        if score > best_score:
            best_id = item_id
            best_score = score
    return best_id


def load_wiki_bonus_overrides(db, extra_items: dict[int, dict[str, Any]] | None = None) -> dict[str, Any]:
    extra_items = extra_items or {}
    names = item_name_index(db, extra_items)
    best_rows, conflicts = best_bonus_rows()
    overrides: dict[int, dict[str, Any]] = {}
    resolved: dict[int, str] = {}
    unresolved: list[str] = []
    for key, row in best_rows.items():
        item_id = choose_item_id(db, extra_items, key, names.get(key, []))
        if item_id is None:
            unresolved.append(row.get("page_name") or key)
            continue
        rec = item_record(db, extra_items, item_id)
        overrides[item_id] = row_to_equipment(row, rec.get("equipment"))
        resolved[item_id] = row.get("page_name") or key
    return {
        "overrides": overrides,
        "resolved": resolved,
        "rows": best_rows,
        "unresolved": unresolved,
        "conflicting_rows": conflicts,
    }
