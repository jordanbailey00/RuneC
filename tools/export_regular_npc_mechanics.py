#!/usr/bin/env python3
"""Compile regular NPC special-mechanics family data.

Input:  data/curated/regular_npc_special_mechanics.toml
Output: data/defs/regular_npc_mechanics.bin ('RNME' magic)
Report: tools/reports/regular_npc_mechanics.txt
"""

from __future__ import annotations

import json
import re
import struct
from pathlib import Path
from typing import Any

try:
    import tomllib
except ModuleNotFoundError:  # pragma: no cover
    tomllib = None

ROOT = Path(__file__).resolve().parents[1]
CURATED = ROOT / "data/curated/regular_npc_special_mechanics.toml"
NDEF = ROOT / "data/defs/npc_defs.bin"
OUT = ROOT / "data/defs/regular_npc_mechanics.bin"
REPORT = ROOT / "tools/reports/regular_npc_mechanics.txt"
WIKI_CACHE = ROOT / "tools/wiki_cache"

RNME_MAGIC = 0x454D4E52
RNME_VERSION = 1
NDEF_MAGIC = 0x4E444546

STATUS = {
    "missing_runtime": 0,
    "runtime_indexed": 1,
    "partial_runtime_indexed": 2,
}

TAG_BITS = {
    "dragonfire": 1 << 0,
    "icy_breath": 1 << 1,
    "equipment_mitigation": 1 << 2,
    "potion_mitigation": 1 << 3,
    "prayer_interaction": 1 << 4,
    "equipment_required": 1 << 5,
    "stat_drain": 1 << 6,
    "special_damage_prevention": 1 << 7,
    "weapon_damage_gate": 1 << 8,
    "ammo_damage_gate": 1 << 9,
    "spell_damage_gate": 1 << 10,
    "special_attack_exception": 1 << 11,
    "finisher_item": 1 << 12,
    "low_hp_rule": 1 << 13,
    "auto_finisher_unlock": 1 << 14,
    "ambush_trigger": 1 << 15,
    "child_spawn": 1 << 16,
    "minion_spawn": 1 << 17,
    "area_attack": 1 << 18,
    "poison": 1 << 19,
    "venom": 1 << 20,
    "disease": 1 << 21,
    "immunity_check": 1 << 22,
    "cure_interaction": 1 << 23,
    "healing": 1 << 24,
    "teleblock": 1 << 25,
    "freeze": 1 << 26,
    "wilderness_rule": 1 << 27,
    "jump_attack": 1 << 28,
    "mirror_shield_required": 1 << 29,
    "nose_peg_required": 1 << 30,
    "earmuffs_required": 1 << 31,
    "face_mask_required": 1 << 32,
    "witchwood_icon_required": 1 << 33,
}

SYSTEM_BITS = {
    "combat": 1 << 0,
    "equipment": 1 << 1,
    "inventory": 1 << 2,
    "prayer": 1 << 3,
    "consumables": 1 << 4,
    "slayer": 1 << 5,
    "spells": 1 << 6,
    "movement": 1 << 7,
}


def read_exact(f, n: int) -> bytes:
    b = f.read(n)
    if len(b) != n:
        raise EOFError("short read")
    return b


def read_ndef(path: Path) -> dict[int, dict[str, Any]]:
    defs: dict[int, dict[str, Any]] = {}
    with path.open("rb") as f:
        magic, version, count = struct.unpack("<III", read_exact(f, 12))
        if magic != NDEF_MAGIC:
            raise ValueError("bad npc_defs magic")
        for _ in range(count):
            npc_id = struct.unpack("<I", read_exact(f, 4))[0]
            size = struct.unpack("<B", read_exact(f, 1))[0]
            combat = struct.unpack("<h", read_exact(f, 2))[0]
            hp = struct.unpack("<H", read_exact(f, 2))[0]
            read_exact(f, 12)  # stats
            read_exact(f, 20)  # anims
            name_len = struct.unpack("<B", read_exact(f, 1))[0]
            name = read_exact(f, name_len).decode("latin-1", "replace")
            if version >= 2:
                read_exact(f, 10)
            models: list[int] = []
            if version >= 3:
                model_count = struct.unpack("<B", read_exact(f, 1))[0]
                if model_count:
                    models = list(struct.unpack(f"<{model_count}I",
                                                read_exact(f, model_count * 4)))
            if version >= 4:
                for _ in range(5):
                    option_len = struct.unpack("<B", read_exact(f, 1))[0]
                    read_exact(f, option_len)
            defs[npc_id] = {
                "id": npc_id,
                "name": name,
                "name_key": norm(name),
                "combat": combat,
                "hp": hp,
                "size": size,
                "models": models,
            }
    return defs


def norm(s: str) -> str:
    return re.sub(r"\s+", " ", s.strip().lower())


def bitmask(values: list[str], table: dict[str, int], label: str) -> int:
    out = 0
    for value in values:
        if value not in table:
            raise ValueError(f"unknown {label}: {value}")
        out |= table[value]
    return out


def load_wiki_status_ids(defs: dict[int, dict[str, Any]]
                         ) -> tuple[set[int], set[int], set[int]]:
    poison: set[int] = set()
    venom: set[int] = set()
    disease: set[int] = set()
    for path in WIKI_CACHE.glob("infobox_monster_*.json"):
        data = json.loads(path.read_text())
        for row in data.get("bucket", []):
            poisonous = str(row.get("poisonous") or "").strip().lower()
            if not poisonous or poisonous == "no" or poisonous == "n/a":
                continue
            target = venom if "venom" in poisonous else \
                disease if "disease" in poisonous else poison
            raw_ids = row.get("id") or []
            if isinstance(raw_ids, str):
                raw_ids = [raw_ids]
            for raw in raw_ids:
                m = re.search(r"\d+", str(raw))
                if not m:
                    continue
                npc_id = int(m.group(0))
                rec = defs.get(npc_id)
                if rec:
                    target.add(npc_id)
    return poison, venom, disease


def select_ids(family: dict[str, Any],
               defs: dict[int, dict[str, Any]],
               wiki_status_ids: tuple[set[int], set[int], set[int]]
               ) -> tuple[list[int], list[str]]:
    ids: set[int] = set(int(x) for x in family.get("npc_ids", []))
    notes: list[str] = []
    combat_only = bool(family.get("combat_only", True))

    exact = {norm(x) for x in family.get("exact_names", [])}
    contains = [norm(x) for x in family.get("contains_names", [])]
    exclude = {norm(x) for x in family.get("exclude_names", [])}
    exclude_contains = [norm(x) for x in family.get("exclude_contains", [])]
    exclude_ids = {int(x) for x in family.get("exclude_npc_ids", [])}

    for npc_id, rec in defs.items():
        if combat_only and rec["combat"] <= 0:
            continue
        name_key = rec["name_key"]
        if name_key in exact or any(pat in name_key for pat in contains):
            ids.add(npc_id)

    poison_ids, venom_ids, disease_ids = wiki_status_ids
    wiki_ids_added: set[int] = set()
    if family.get("wiki_poisonous", False):
        ids |= poison_ids
        wiki_ids_added |= poison_ids
        notes.append(f"wiki_poisonous_ids={len(poison_ids)}")
    if family.get("wiki_venomous", False):
        ids |= venom_ids
        wiki_ids_added |= venom_ids
        notes.append(f"wiki_venomous_ids={len(venom_ids)}")
    if family.get("wiki_disease", False):
        ids |= disease_ids
        wiki_ids_added |= disease_ids
        notes.append(f"wiki_disease_ids={len(disease_ids)}")

    filtered: list[int] = []
    for npc_id in sorted(ids):
        rec = defs.get(npc_id)
        if not rec:
            notes.append(f"missing_npc_def={npc_id}")
            continue
        name_key = rec["name_key"]
        if npc_id in exclude_ids or name_key in exclude:
            continue
        if any(pat in name_key for pat in exclude_contains):
            continue
        if combat_only and rec["combat"] <= 0 and npc_id not in wiki_ids_added:
            continue
        filtered.append(npc_id)
    return filtered, notes


def write_pstr(f, value: str):
    b = value.encode("utf-8")
    if len(b) > 255:
        raise ValueError(f"string too long: {value[:40]}")
    f.write(struct.pack("<B", len(b)))
    f.write(b)


def main() -> int:
    if tomllib is None:
        raise SystemExit("Python 3.11+ tomllib is required")

    curated = tomllib.loads(CURATED.read_text())
    if curated.get("schema_version") != 2:
        raise SystemExit("unsupported regular NPC mechanics schema")

    defs = read_ndef(NDEF)
    wiki_status_ids = load_wiki_status_ids(defs)
    families: list[dict[str, Any]] = []
    report_lines = [
        "Regular NPC special-mechanics export",
        "",
        f"input: {CURATED.relative_to(ROOT)}",
        f"npc_defs: {len(defs)}",
        f"wiki poison/venom/disease IDs: "
        f"{len(wiki_status_ids[0])}/{len(wiki_status_ids[1])}/"
        f"{len(wiki_status_ids[2])}",
        "",
        "Families",
    ]

    for family in curated.get("families", []):
        key = str(family["key"])
        ids, notes = select_ids(family, defs, wiki_status_ids)
        if not ids:
            raise SystemExit(f"family resolved zero NPC IDs: {key}")
        status = STATUS.get(str(family.get("status", "")))
        if status is None:
            raise SystemExit(f"unknown family status: {key}")
        tag_bits = bitmask(list(family.get("tags", [])), TAG_BITS, "tag")
        system_bits = bitmask(list(family.get("required_systems", [])),
                              SYSTEM_BITS, "system")
        families.append({
            "key": key,
            "status": status,
            "tag_bits": tag_bits,
            "system_bits": system_bits,
            "ids": ids,
        })
        sample = ", ".join(f"{npc_id}:{defs[npc_id]['name']}" for npc_id in ids[:8])
        extra = f" ({'; '.join(notes)})" if notes else ""
        report_lines.append(f"- {key}: {len(ids)} NPC IDs{extra}")
        report_lines.append(f"  tags: {', '.join(family.get('tags', []))}")
        report_lines.append(f"  sample: {sample}")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", RNME_MAGIC, RNME_VERSION, len(families)))
        for family in families:
            write_pstr(f, family["key"])
            f.write(struct.pack("<BHIQ", family["status"],
                                len(family["ids"]),
                                family["system_bits"],
                                family["tag_bits"]))
            for npc_id in family["ids"]:
                f.write(struct.pack("<I", npc_id))

    total_ids = sum(len(f["ids"]) for f in families)
    report_lines.extend([
        "",
        "Summary",
        f"families: {len(families)}",
        f"family NPC ID links: {total_ids}",
        f"output: {OUT.relative_to(ROOT)}",
        "",
        "Status",
        "READY_WITH_ACCEPTED_SIMPLIFICATIONS: regular special-mechanics families are indexed for runtime lookup.",
        "READY_WITH_ACCEPTED_SIMPLIFICATIONS: combat consumes damage gates, breath mitigation, statuses, revenants, shaman pressure, and slayer protection tags.",
        "READY_WITH_ACCEPTED_SIMPLIFICATIONS: slayer consumes NPC-death events for task kill-credit decrement.",
        "BLOCKS_PARITY: exact slayer eligibility, unlock, task amount, extension, and per-family edge cases still need deeper slayer rules.",
    ])
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text("\n".join(report_lines) + "\n")
    print(f"exported {len(families)} regular NPC mechanics families "
          f"({total_ids} NPC links)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
