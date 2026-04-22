#!/usr/bin/env python3
"""Emit data/defs/skill_drops.bin — residual dropsline rows where
"Dropped from" isn't an NPC (trees, rocks, chests, hunter, containers).

Binary format — 'SDRP' magic:
  magic u32 | version u32 | count u32
  per source:
    source_name_len u8 + source_name[]
    drops_n u16 + (item_id u32, qmin u16, qmax u16, rarity_inv u32)[]
"""
from __future__ import annotations

import json
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from export_drops import (  # noqa: E402
    load_bucket, build_item_name_to_id, build_npc_name_to_id,
    parse_rarity, parse_quantity,
)

OUT = Path("/home/joe/projects/RuneC_copy/data/defs/skill_drops.bin")
REPORT = Path("/home/joe/projects/RuneC_copy/tools/reports/skill_drops.txt")

SDRP_MAGIC = 0x50524453
SDRP_VERSION = 1


def main():
    items = build_item_name_to_id()
    npcs = build_npc_name_to_id()

    by_source: dict[str, list[dict]] = defaultdict(list)
    display_name: dict[str, str] = {}
    total = skipped_is_npc = skipped_no_item = skipped_bad = 0

    for r in load_bucket("dropsline"):
        total += 1
        dj_str = r.get("drop_json")
        if not dj_str:
            continue
        try:
            dj = json.loads(dj_str)
        except (json.JSONDecodeError, TypeError):
            skipped_bad += 1; continue
        from_name = (dj.get("Dropped from") or "").strip()
        item_name = (dj.get("Dropped item") or "").strip()
        if not from_name or not item_name:
            continue
        from_key = from_name.split("#", 1)[0].lower()
        item_key = item_name.split("#", 1)[0].lower()

        # Location-variant fallback (matches export_drops.py): if the
        # bare name doesn't hit but the base (stripped "(...)") does,
        # that row belongs in drops.bin — skip here.
        from_base = (re.sub(r"\s*\([^)]*\)\s*$", "", from_key).strip()
                     if "(" in from_key else from_key)
        item_base = (re.sub(r"\s*\([^)]*\)\s*$", "", item_key).strip()
                     if "(" in item_key else item_key)
        if from_key in npcs or from_base in npcs:
            skipped_is_npc += 1; continue
        iid = items.get(item_key) or items.get(item_base)
        if iid is None:
            skipped_no_item += 1; continue

        rarity = parse_rarity(dj.get("Rarity", ""))
        qty = parse_quantity(dj.get("Drop Quantity", "")) or (1, 1)
        rarity_inv = 0 if not rarity else max(1, round(1 / rarity))
        by_source[from_key].append({
            "item_id": iid,
            "qmin": max(0, min(65535, qty[0])),
            "qmax": max(0, min(65535, qty[1])),
            "rarity_inv": max(0, min(0xFFFFFFFF, rarity_inv)),
        })
        display_name.setdefault(from_key, from_name.split("#", 1)[0])

    total_entries = sum(len(v) for v in by_source.values())
    print(f"total {total}; is-npc {skipped_is_npc}; bad-json {skipped_bad}; "
          f"no-item {skipped_no_item}; sources {len(by_source)}; "
          f"entries {total_entries}", file=sys.stderr)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", SDRP_MAGIC, SDRP_VERSION, len(by_source)))
        for key in sorted(by_source):
            nb = display_name[key].encode("latin-1", errors="replace")[:255]
            f.write(struct.pack("<B", len(nb))); f.write(nb)
            entries = by_source[key][:65535]
            f.write(struct.pack("<H", len(entries)))
            for e in entries:
                f.write(struct.pack("<IHHI",
                                    e["item_id"], e["qmin"], e["qmax"],
                                    e["rarity_inv"]))
    print(f"  → {OUT} ({OUT.stat().st_size} bytes)", file=sys.stderr)

    REPORT.parent.mkdir(parents=True, exist_ok=True)
    with REPORT.open("w") as f:
        f.write(f"sources: {len(by_source)}\nentries: {total_entries}\n\n")
        f.write("top 30 sources by drop count:\n")
        ranked = sorted(by_source.items(), key=lambda kv: -len(kv[1]))
        for key, drops in ranked[:30]:
            f.write(f"  {len(drops):5}  {display_name[key]}\n")


if __name__ == "__main__":
    main()
