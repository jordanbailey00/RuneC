#!/usr/bin/env python3
"""Build a unified acquisition-source index.

This does not replace drops.bin, skill_drops.bin, shops.bin, or
recipes.bin. It records the source inventory across those datasets so
parity checks can reason about acquisition breadth in one place.
"""
from __future__ import annotations

import json
import struct
from collections import defaultdict
from pathlib import Path

from acquisition_common import (
    build_npc_name_to_id, classify_non_npc_source, key_name, load_bucket,
    resolve_name,
)

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "data/defs/acquisition_sources.bin"
REPORT = ROOT / "tools/reports/acquisition_sources.txt"

ACQS_MAGIC = 0x53514341  # ACQS
ACQS_VERSION = 1

KIND_NPC_DROP = 1
KIND_NON_NPC_DROP = 2
KIND_SHOP = 3
KIND_RECIPE = 4
KIND_SHARED_TABLE = 5


def rel(path: Path) -> str:
    return str(path.relative_to(ROOT))


def read_bin_count(path: Path) -> int:
    if not path.is_file():
        return 0
    data = path.read_bytes()[:12]
    if len(data) < 12:
        return 0
    return struct.unpack("<III", data)[2]


def recipe_kept(row: dict) -> bool:
    if row.get("source_template") != "recipe":
        return False
    try:
        production = json.loads(row.get("production_json") or "{}")
    except (json.JSONDecodeError, TypeError):
        return False
    return bool(production.get("skills"))


def add_source(sources: dict[str, dict], key: str, kind: int, name: str,
               ref_id: int = 0, class_name: str = "") -> dict:
    src = sources.setdefault(key, {
        "kind": kind,
        "ref_id": ref_id,
        "name": name,
        "class_name": class_name,
        "entry_count": 0,
        "raw_rows": 0,
    })
    src["entry_count"] += 1
    src["raw_rows"] += 1
    return src


def main() -> int:
    npcs = build_npc_name_to_id()
    sources: dict[str, dict] = {}
    counters: dict[str, int] = defaultdict(int)

    for row in load_bucket("dropsline"):
        try:
            drop = json.loads(row.get("drop_json") or "{}")
        except (json.JSONDecodeError, TypeError):
            counters["bad_drop_json"] += 1
            continue
        from_name = (drop.get("Dropped from") or "").strip()
        if not from_name:
            counters["missing_source"] += 1
            continue
        npc_id = resolve_name(from_name, npcs)
        if npc_id is not None:
            key = f"npc:{npc_id}"
            add_source(sources, key, KIND_NPC_DROP,
                       from_name.split("#", 1)[0], npc_id, "npc_drop")
            counters["npc_drop_rows"] += 1
        else:
            class_name = classify_non_npc_source(from_name)
            key = f"non_npc:{key_name(from_name)}"
            add_source(sources, key, KIND_NON_NPC_DROP,
                       from_name.split("#", 1)[0], 0, class_name)
            counters[f"non_npc_{class_name}"] += 1

    shop_names: set[str] = set()
    shop_stock_counts: dict[str, int] = defaultdict(int)
    for row in load_bucket("infobox_shop"):
        name = (row.get("page_name") or "").strip()
        if name:
            shop_names.add(name)
    for row in load_bucket("storeline"):
        name = (row.get("page_name") or "").strip()
        if name:
            shop_names.add(name)
            shop_stock_counts[name] += 1
    for name in shop_names:
        sources[f"shop:{key_name(name)}"] = {
            "kind": KIND_SHOP,
            "ref_id": 0,
            "name": name,
            "class_name": "shop",
            "entry_count": shop_stock_counts.get(name, 0),
            "raw_rows": shop_stock_counts.get(name, 0),
        }

    for row in load_bucket("recipe"):
        if not recipe_kept(row):
            continue
        name = (row.get("page_name") or "").strip()
        if name:
            add_source(sources, f"recipe:{key_name(name)}", KIND_RECIPE,
                       name, 0, "recipe")

    for tag, path in (
        ("RDT_", ROOT / "data/defs/rdt.bin"),
        ("GDT_", ROOT / "data/defs/gdt.bin"),
        ("MRDT", ROOT / "data/defs/mrdt.bin"),
    ):
        sources[f"shared:{tag}"] = {
            "kind": KIND_SHARED_TABLE,
            "ref_id": 0,
            "name": tag,
            "class_name": "shared_drop_table",
            "entry_count": read_bin_count(path),
            "raw_rows": read_bin_count(path),
        }

    rows = sorted(sources.values(), key=lambda r: (r["kind"], r["name"].lower()))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", ACQS_MAGIC, ACQS_VERSION, len(rows)))
        for row in rows:
            name = row["name"].encode("latin-1", errors="replace")[:65535]
            cls = row["class_name"].encode("ascii", errors="replace")[:255]
            f.write(struct.pack("<BBIIH", row["kind"], len(cls), row["ref_id"],
                                row["entry_count"], len(name)))
            f.write(cls)
            f.write(name)

    by_kind: dict[int, int] = defaultdict(int)
    by_class: dict[str, int] = defaultdict(int)
    for row in rows:
        by_kind[row["kind"]] += 1
        by_class[row["class_name"]] += 1

    kind_names = {
        KIND_NPC_DROP: "npc_drop",
        KIND_NON_NPC_DROP: "non_npc_drop",
        KIND_SHOP: "shop",
        KIND_RECIPE: "recipe",
        KIND_SHARED_TABLE: "shared_table",
    }
    lines = [
        "Acquisition source index",
        "",
        f"output binary: {rel(OUT)} ({OUT.stat().st_size} bytes)",
        f"source rows:   {len(rows)}",
        "",
        "by source kind:",
    ]
    for kind in sorted(by_kind):
        lines.append(f"  {kind_names[kind]:<16} {by_kind[kind]}")
    lines.extend(["", "by source class:"])
    for cls, count in sorted(by_class.items(), key=lambda kv: (-kv[1], kv[0])):
        lines.append(f"  {cls:<24} {count}")
    lines.extend(["", "raw row counters:"])
    for key, count in sorted(counters.items()):
        lines.append(f"  {key:<32} {count}")
    lines.extend(["", "largest sources by row count:"])
    for row in sorted(rows, key=lambda r: -r["entry_count"])[:40]:
        lines.append(
            f"  {kind_names[row['kind']]:<16} {row['entry_count']:>5} "
            f"{row['class_name']:<24} {row['name']}"
        )
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text("\n".join(lines) + "\n")
    print(REPORT.read_text())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
