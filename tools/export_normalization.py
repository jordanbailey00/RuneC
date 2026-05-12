#!/usr/bin/env python3
"""Build canonical item/NPC/source-name normalization data."""

from __future__ import annotations

import re
import struct
import time
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ITEMS = ROOT / "data/defs/items.bin"
NPCS = ROOT / "data/defs/npc_defs.bin"
SOURCES = ROOT / "data/defs/acquisition_sources.bin"
OUT = ROOT / "data/defs/normalization.bin"
REPORT = ROOT / "tools/reports/normalization.txt"

NORM_MAGIC = 0x4D524F4E
NORM_VERSION = 1
U32_MISSING = 0xFFFFFFFF

IDEF_MAGIC = 0x49444546
NDEF_MAGIC = 0x4E444546
ACQS_MAGIC = 0x53514341

F_NOTED = 1 << 6
F_PLACEHOLDER = 1 << 7
F_NOTEABLE = 1 << 8
F_HAS_EQUIPMENT = 1 << 4
F_HAS_WEAPON = 1 << 5

NORM_ITEM_NOTED = 1 << 0
NORM_ITEM_PLACEHOLDER = 1 << 1
NORM_ITEM_NOTEABLE = 1 << 2
NORM_ITEM_VARIANT = 1 << 3
NORM_NPC_ALIAS_GROUP = 1 << 0


def fnv1a(text: str) -> int:
    h = 0x811C9DC5
    for b in text.encode("ascii", "ignore"):
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


def key_name(value: str) -> str:
    value = re.sub(r"\([^)]*\)", " ", value or "")
    value = re.sub(r"[^a-zA-Z0-9]+", " ", value.lower())
    return re.sub(r"\s+", " ", value).strip()


def read_pstr(buf: memoryview, pos: int) -> tuple[str, int]:
    ln = buf[pos]
    pos += 1
    raw = bytes(buf[pos:pos + ln])
    return raw.decode("latin-1", "replace"), pos + ln


def id_or_missing(value: int | None) -> int:
    return U32_MISSING if value is None or value < 0 else int(value)


def parse_items() -> dict[int, dict[str, object]]:
    data = ITEMS.read_bytes()
    pos = 0
    magic, version, count = struct.unpack_from("<III", data, pos)
    pos += 12
    if magic != IDEF_MAGIC or version not in (1, 2):
        raise ValueError("bad items.bin")
    rows: dict[int, dict[str, object]] = {}
    for _ in range(count):
        (size,) = struct.unpack_from("<I", data, pos)
        pos += 4
        rec = memoryview(data[pos:pos + size])
        pos += size
        p = 0
        (item_id,) = struct.unpack_from("<I", rec, p)
        p += 4
        if version == 1:
            flags = rec[p]
            p += 1
        else:
            flags = struct.unpack_from("<H", rec, p)[0]
            p += 3
        name, p = read_pstr(rec, p)
        p += 2 + 12
        linked_item = linked_noted = linked_placeholder = -1
        if version == 1:
            linked_noted = struct.unpack_from("<I", rec, p)[0]
            p += 4
        else:
            linked_item, linked_noted, linked_placeholder = struct.unpack_from(
                "<III", rec, p)
            p += 4 * 11
        if linked_item == U32_MISSING:
            linked_item = -1
        if linked_noted == U32_MISSING:
            linked_noted = -1
        if linked_placeholder == U32_MISSING:
            linked_placeholder = -1
        rows[int(item_id)] = {
            "id": int(item_id),
            "name": name,
            "key": key_name(name),
            "key_hash": fnv1a(key_name(name)),
            "noted": bool(flags & F_NOTED),
            "placeholder": bool(flags & F_PLACEHOLDER),
            "noteable": bool(flags & F_NOTEABLE),
            "linked_item": int(linked_item),
            "linked_noted": int(linked_noted),
            "linked_placeholder": int(linked_placeholder),
        }
    return rows


def parse_npcs() -> dict[int, dict[str, object]]:
    data = NPCS.read_bytes()
    pos = 0
    magic, version, count = struct.unpack_from("<III", data, pos)
    pos += 12
    if magic != NDEF_MAGIC or version not in (1, 2, 3, 4):
        raise ValueError("bad npc_defs.bin")
    rows: dict[int, dict[str, object]] = {}
    for _ in range(count):
        npc_id = struct.unpack_from("<I", data, pos)[0]
        pos += 4 + 1 + 2 + 2 + 12 + 20
        name_len = data[pos]
        pos += 1
        name = data[pos:pos + name_len].decode("latin-1", "replace")
        pos += name_len
        if version >= 2:
            pos += 1 + 2 + 1 + 1 + 2 + 1 + 1 + 1
        if version >= 3:
            model_count = data[pos]
            pos += 1 + model_count * 4
        if version >= 4:
            for _ in range(5):
                option_len = data[pos]
                pos += 1 + option_len
        key = key_name(name)
        rows[int(npc_id)] = {
            "id": int(npc_id),
            "name": name,
            "key": key,
            "key_hash": fnv1a(key),
        }
    return rows


def parse_sources() -> list[dict[str, int]]:
    if not SOURCES.is_file():
        return []
    data = SOURCES.read_bytes()
    pos = 0
    magic, _version, count = struct.unpack_from("<III", data, pos)
    pos += 12
    if magic != ACQS_MAGIC:
        raise ValueError("bad acquisition_sources.bin")
    rows = []
    for _ in range(count):
        kind, cls_len, ref_id, _entry_count, name_len = struct.unpack_from(
            "<BBIIH", data, pos)
        pos += 12
        pos += cls_len
        name = data[pos:pos + name_len].decode("latin-1", "replace")
        pos += name_len
        rows.append({
            "kind": int(kind),
            "ref_id": int(ref_id),
            "key_hash": fnv1a(key_name(name)),
        })
    return rows


def canonical_items(items: dict[int, dict[str, object]]) -> list[dict[str, int]]:
    by_key: dict[str, list[int]] = defaultdict(list)
    for item_id, rec in items.items():
        if rec["key"]:
            by_key[str(rec["key"])].append(item_id)
    preferred: dict[str, int] = {}
    for key, ids in by_key.items():
        real = [
            item_id for item_id in ids
            if not items[item_id]["noted"] and not items[item_id]["placeholder"]
        ]
        preferred[key] = min(real or ids)

    rows = []
    for item_id in sorted(items):
        rec = items[item_id]
        canonical = item_id
        linked_item = int(rec["linked_item"])
        if linked_item in items and not items[linked_item]["placeholder"]:
            canonical = linked_item
        elif str(rec["key"]) in preferred:
            canonical = preferred[str(rec["key"])]
        flags = 0
        if rec["noted"]:
            flags |= NORM_ITEM_NOTED
        if rec["placeholder"]:
            flags |= NORM_ITEM_PLACEHOLDER
        if rec["noteable"]:
            flags |= NORM_ITEM_NOTEABLE
        if canonical != item_id:
            flags |= NORM_ITEM_VARIANT
        noted = int(rec["linked_noted"])
        placeholder = int(rec["linked_placeholder"])
        if rec["noted"]:
            noted = item_id
        if rec["placeholder"]:
            placeholder = item_id
        rows.append({
            "id": item_id,
            "canonical": canonical,
            "noted": noted,
            "placeholder": placeholder,
            "key_hash": int(rec["key_hash"]),
            "flags": flags,
        })
    return rows


def canonical_npcs(npcs: dict[int, dict[str, object]]) -> list[dict[str, int]]:
    by_key: dict[str, list[int]] = defaultdict(list)
    for npc_id, rec in npcs.items():
        if rec["key"]:
            by_key[str(rec["key"])].append(npc_id)
    canonical = {key: min(ids) for key, ids in by_key.items()}
    rows = []
    for npc_id in sorted(npcs):
        rec = npcs[npc_id]
        key = str(rec["key"])
        canon = canonical.get(key, npc_id)
        flags = NORM_NPC_ALIAS_GROUP if len(by_key.get(key, [])) > 1 else 0
        rows.append({
            "id": npc_id,
            "canonical": canon,
            "key_hash": int(rec["key_hash"]),
            "flags": flags,
        })
    return rows


def write_bin(items: list[dict[str, int]], npcs: list[dict[str, int]],
              sources: list[dict[str, int]]) -> None:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<IIIII", NORM_MAGIC, NORM_VERSION,
                            len(items), len(npcs), len(sources)))
        for row in items:
            f.write(struct.pack("<IIIIIH",
                                row["id"], row["canonical"],
                                id_or_missing(row["noted"]),
                                id_or_missing(row["placeholder"]),
                                row["key_hash"], row["flags"]))
        for row in npcs:
            f.write(struct.pack("<IIIH", row["id"], row["canonical"],
                                row["key_hash"], row["flags"]))
        for row in sources:
            f.write(struct.pack("<BII", row["kind"], row["key_hash"],
                                row["ref_id"]))


def write_report(items: list[dict[str, int]], npcs: list[dict[str, int]],
                 sources: list[dict[str, int]], elapsed_ms: float) -> None:
    item_variants = [r for r in items if r["canonical"] != r["id"]]
    npc_aliases = [r for r in npcs if r["canonical"] != r["id"]]
    lines = [
        "Normalization export",
        "",
        "status: READY_WITH_ACCEPTED_SIMPLIFICATIONS",
        f"output binary: data/defs/normalization.bin ({OUT.stat().st_size} bytes)",
        "sources: items.bin, npc_defs.bin, acquisition_sources.bin",
        f"item rows: {len(items)}",
        f"item canonical/form mappings: {len(item_variants)}",
        f"noted item links: {sum(1 for r in items if r['noted'] >= 0)}",
        f"placeholder item links: {sum(1 for r in items if r['placeholder'] >= 0)}",
        f"npc rows: {len(npcs)}",
        f"npc alias/form mappings: {len(npc_aliases)}",
        f"source-name rows: {len(sources)}",
        f"elapsed_ms: {elapsed_ms:.3f}",
        "",
        "accepted simplifications:",
        "  - charged/degraded/ornamented variants are canonicalized by form links first, then normalized display-name groups",
        "  - NPC aliases use normalized display-name groups; exact combat/runtime IDs remain available and should not be discarded",
        "  - source-name rows store hashes for deterministic joins, not original prose labels",
        "",
        "sample item mappings:",
    ]
    for row in item_variants[:20]:
        lines.append(f"  {row['id']} -> {row['canonical']}")
    lines.append("")
    lines.append("sample npc mappings:")
    for row in npc_aliases[:20]:
        lines.append(f"  {row['id']} -> {row['canonical']}")
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text("\n".join(lines) + "\n")


def main() -> int:
    start = time.perf_counter()
    item_rows = canonical_items(parse_items())
    npc_rows = canonical_npcs(parse_npcs())
    source_rows = parse_sources()
    write_bin(item_rows, npc_rows, source_rows)
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    write_report(item_rows, npc_rows, source_rows, elapsed_ms)
    print(f"exported normalization rows: items={len(item_rows)} "
          f"npcs={len(npc_rows)} sources={len(source_rows)} "
          f"in {elapsed_ms:.3f} ms")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
