#!/usr/bin/env python3
"""Parse `dropsline` bucket rows into RuneC drops.bin.

Input: cached `dropsline_*.json` files under `tools/wiki_cache/`.
       38,638 raw rows. Each row has `item_name`, `drop_json` blob,
       and `rare_drop_table` flag.

Joins: resolves `Dropped item` → item_id and `Dropped from` → npc_id
using the already-scraped `infobox_item` and `infobox_monster` buckets.

Output: `data/defs/drops.bin` (DROP magic) per the NPC drop-table
shape documented in `database.md`, plus a coverage report under
`tools/reports/drops.txt`.

`rarity_inv` is stored as u32 fixed-point = round(1/rarity). e.g. 128
for 1/128, 1 for Always. Drops with unparseable rarity ("Varies")
default to rarity_inv=0 → runtime can interpret as "roll separately"
(Phase 2 can add per-NPC custom tables for these).
"""
from __future__ import annotations

import json
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from acquisition_common import (  # noqa: E402
    build_item_name_to_id, build_npc_name_to_id, classify_non_npc_source,
    key_name, load_bucket, resolve_name,
)

OUT_BIN = ROOT / "data/defs/drops.bin"
OUT_REPORT = ROOT / "tools/reports/drops.txt"

DROP_MAGIC = 0x50_4F_52_44  # 'DROP' little-endian
DROP_VERSION = 1


def rel(path: Path) -> str:
    return str(path.relative_to(ROOT))


# -------- Parsers --------

# Em dash (U+2013), en dash, ascii hyphen — wiki uses all three.
_RANGE_RE = re.compile(r"^\s*(\d+)\s*[\u2013\u2014\-]\s*(\d+)\s*$")
_INT_RE = re.compile(r"^\s*(\d+)\s*$")
_SIMPLE_FRAC = re.compile(r"^\s*(\d+)\s*/\s*([\d,]+(?:\.\d+)?)\s*$")

# Keyword rarities — treat as approximate weights.
_NAMED_RARITY = {
    "always": 1.0,
    "common": 1 / 20.0,
    "uncommon": 1 / 50.0,
    "rare": 1 / 128.0,
    "very rare": 1 / 512.0,
    # "varies" and blank → None
}


def parse_rarity(s: str) -> float | None:
    """Return probability in [0, 1] or None if unparseable."""
    if not s:
        return None
    s = s.strip()
    lower = s.lower()
    if lower in _NAMED_RARITY:
        return _NAMED_RARITY[lower]
    if lower in ("varies", "?", ""):
        return None
    m = _SIMPLE_FRAC.match(s)
    if m:
        num = float(m.group(1))
        denom = float(m.group(2).replace(",", ""))
        if denom <= 0:
            return None
        return num / denom
    # Fallthrough: try "1/x" with extra junk (approx notation).
    m2 = re.search(r"(\d+)\s*/\s*([\d,]+(?:\.\d+)?)", s)
    if m2:
        num = float(m2.group(1))
        denom = float(m2.group(2).replace(",", ""))
        if denom > 0:
            return num / denom
    return None


def parse_quantity(s) -> tuple[int, int] | None:
    """Return (min, max) inclusive or None if unparseable.

    Handles: "1", "1–2", "1-2", "15", "Varies", "1; 1 (noted)", etc.
    """
    if s is None:
        return None
    s = str(s).strip()
    if not s or s.lower() in ("varies", "?"):
        return None
    m = _INT_RE.match(s)
    if m:
        n = int(m.group(1))
        return (n, n)
    m = _RANGE_RE.match(s)
    if m:
        return (int(m.group(1)), int(m.group(2)))
    # Compound forms like "1; 1 (noted)" — take the first number.
    first = re.search(r"\d+", s)
    if first:
        n = int(first.group(0))
        return (n, n)
    return None


# -------- Drop classification --------

def classify(rarity: float | None, drop_type: str) -> str:
    """'always' | 'main' | 'tertiary' per the live drop-table rules.

    Tertiary = drops that roll independently of the main table —
    clue scrolls, pet rolls, etc. We tag by drop_type where the wiki
    provides it.
    """
    if rarity is not None and rarity >= 0.9999:
        return "always"
    dt = (drop_type or "").lower()
    if dt in ("tertiary", "clue", "pet"):
        return "tertiary"
    return "main"


# -------- Binary emit --------

def emit_bin(path: Path, tables: dict[int, dict]):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as f:
        f.write(struct.pack("<III", DROP_MAGIC, DROP_VERSION, len(tables)))
        for npc_id in sorted(tables):
            t = tables[npc_id]
            f.write(struct.pack("<I", npc_id))
            f.write(struct.pack("<B", len(t["always"])))
            for d in t["always"]:
                f.write(struct.pack("<IHH",
                                    d["item_id"], d["qmin"], d["qmax"]))
            f.write(struct.pack("<B", min(255, len(t["main"]))))
            for d in t["main"][:255]:
                f.write(struct.pack("<IHHI",
                                    d["item_id"], d["qmin"], d["qmax"],
                                    d["rarity_inv"]))
            f.write(struct.pack("<B", min(255, len(t["tertiary"]))))
            for d in t["tertiary"][:255]:
                f.write(struct.pack("<IHHI",
                                    d["item_id"], d["qmin"], d["qmax"],
                                    d["rarity_inv"]))
            f.write(struct.pack("<I", t["rare_table_weight"]))


# -------- Main --------

def main():
    print("building item name→id map…", file=sys.stderr)
    items = build_item_name_to_id()
    print(f"  {len(items)} item names", file=sys.stderr)

    print("building npc name→id map…", file=sys.stderr)
    npcs = build_npc_name_to_id()
    print(f"  {len(npcs)} npc names", file=sys.stderr)

    tables: dict[int, dict] = defaultdict(
        lambda: {"always": [], "main": [], "tertiary": [],
                 "rare_table_weight": 0})

    # Stats
    total_rows = 0
    bad_json = 0
    npc_rows = 0
    non_npc_rows = 0
    unresolved_item = 0
    non_item_reward = 0
    unparseable_rarity = 0
    unparseable_qty = 0
    rare_table_flagged = 0

    non_npc_names: set[str] = set()
    non_npc_kinds: dict[str, int] = defaultdict(int)
    unresolved_item_names: set[str] = set()
    non_item_reward_names: set[str] = set()

    print("parsing dropsline…", file=sys.stderr)
    for r in load_bucket("dropsline"):
        total_rows += 1
        dj_str = r.get("drop_json")
        if not dj_str:
            continue
        try:
            dj = json.loads(dj_str)
        except (json.JSONDecodeError, TypeError):
            bad_json += 1
            continue

        item_name = (dj.get("Dropped item") or "").strip()
        from_name = (dj.get("Dropped from") or "").strip()
        if not item_name or not from_name:
            continue

        nid = resolve_name(from_name, npcs)
        if nid is None:
            non_npc_rows += 1
            kind = classify_non_npc_source(from_name)
            non_npc_kinds[kind] += 1
            if len(non_npc_names) < 80:
                non_npc_names.add(from_name)
            continue
        npc_rows += 1

        iid = resolve_name(item_name, items)
        if iid is None:
            if resolve_name(item_name, npcs) is not None:
                non_item_reward += 1
                if len(non_item_reward_names) < 40:
                    non_item_reward_names.add(item_name)
            else:
                unresolved_item += 1
                if len(unresolved_item_names) < 40:
                    unresolved_item_names.add(item_name)
            continue

        rarity = parse_rarity(dj.get("Rarity", ""))
        qty = parse_quantity(dj.get("Drop Quantity", ""))
        if rarity is None and str(dj.get("Rarity", "")).strip():
            unparseable_rarity += 1
        if qty is None:
            unparseable_qty += 1
            qty = (1, 1)

        rarity_inv = 0 if not rarity else max(1, round(1 / rarity))

        entry = {
            "item_id": iid,
            "qmin": max(0, min(65535, qty[0])),
            "qmax": max(0, min(65535, qty[1])),
            "rarity_inv": max(0, min(0xFFFFFFFF, rarity_inv)),
        }

        bucket = classify(rarity, dj.get("Drop type", ""))
        if r.get("rare_drop_table"):
            rare_table_flagged += 1
            tables[nid]["rare_table_weight"] += 1
            continue

        if bucket == "always":
            # Drop the rarity_inv from the always record (guaranteed).
            tables[nid][bucket].append({
                "item_id": entry["item_id"],
                "qmin": entry["qmin"],
                "qmax": entry["qmax"],
            })
        else:
            tables[nid][bucket].append(entry)

    emit_bin(OUT_BIN, tables)

    # Coverage report
    OUT_REPORT.parent.mkdir(parents=True, exist_ok=True)
    with OUT_REPORT.open("w") as f:
        f.write(f"total dropsline rows:       {total_rows}\n")
        f.write(f"bad drop_json:              {bad_json}\n")
        f.write(f"resolved NPC-source rows:   {npc_rows}\n")
        f.write(f"non-NPC acquisition rows:   {non_npc_rows}\n")
        f.write(f"true unresolved item names: {unresolved_item}\n")
        f.write(f"non-item reward rows:       {non_item_reward}\n")
        f.write(f"unparseable rarity:         {unparseable_rarity}\n")
        f.write(f"unparseable quantity:       {unparseable_qty}\n")
        f.write(f"rare_drop_table flagged:    {rare_table_flagged}\n")
        f.write(f"distinct NPC drop tables:   {len(tables)}\n")
        total_entries = sum(len(t["always"]) + len(t["main"])
                            + len(t["tertiary"]) for t in tables.values())
        f.write(f"total drop entries:         {total_entries}\n")
        f.write(f"output binary:              {rel(OUT_BIN)} "
                f"({OUT_BIN.stat().st_size} bytes)\n")
        f.write("\n--- non-NPC acquisition row classes ---\n")
        for kind, count in sorted(non_npc_kinds.items(),
                                  key=lambda kv: (-kv[1], kv[0])):
            f.write(f"  {kind:<24} {count}\n")
        f.write("\n--- sample non-NPC acquisition sources (first 80) ---\n")
        for n in sorted(non_npc_names):
            f.write(f"  {n}\n")
        f.write("\n--- sample true unresolved dropped item names (first 40) ---\n")
        for n in sorted(unresolved_item_names):
            f.write(f"  {n}\n")
        f.write("\n--- sample non-item reward names (first 40) ---\n")
        for n in sorted(non_item_reward_names):
            f.write(f"  {n}\n")
    print(OUT_REPORT.read_text(), file=sys.stderr)


if __name__ == "__main__":
    main()
