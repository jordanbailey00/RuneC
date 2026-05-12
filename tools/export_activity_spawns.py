#!/usr/bin/env python3
"""Compile activity-local spawn/object anchor data."""

from __future__ import annotations

import struct
import time
import tomllib
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "data/curated/activity_spawns.toml"
OUT = ROOT / "data/defs/activity_spawns.bin"
REPORT = ROOT / "tools/reports/activity_spawns.txt"

ASPN_MAGIC = 0x4E505341
ASPN_VERSION = 1
NO_ID = 0xFFFFFFFF
NO_COORD = 0xFFFF

K_POINT = 1
K_REGION = 2
K_DYNAMIC = 3
K_WAVE_POINT = 4
K_WAVE_REGION_REF = 5
K_OBJECT_ANCHOR = 6
K_SAFE_TILE = 7
K_UNRESOLVED = 8

KIND_NAMES = {
    K_POINT: "point",
    K_REGION: "region",
    K_DYNAMIC: "dynamic",
    K_WAVE_POINT: "wave_point",
    K_WAVE_REGION_REF: "wave_region_ref",
    K_OBJECT_ANCHOR: "object_anchor",
    K_SAFE_TILE: "safe_tile",
    K_UNRESOLVED: "unresolved",
}

F_POINT = 1 << 0
F_REGION = 1 << 1
F_NPC = 1 << 2
F_OBJECT = 1 << 3
F_WAVE = 1 << 4
F_DYNAMIC = 1 << 5
F_REQUIRED = 1 << 6


def pstr(value: object, limit: int = 255) -> bytes:
    raw = str(value or "").encode("utf-8", "replace")[:limit]
    return struct.pack("<B", len(raw)) + raw


def opt_u16(item: dict[str, object], key: str) -> int:
    if key not in item:
        return NO_COORD
    return int(item[key])


def base_row(slug: str, kind: int, item: dict[str, object]) -> dict[str, object]:
    npc_id = int(item.get("npc_id", NO_ID))
    object_id = int(item.get("object_id", NO_ID))
    flags = 0
    if npc_id != NO_ID:
        flags |= F_NPC
    if object_id != NO_ID:
        flags |= F_OBJECT
    if int(item.get("wave", 0)):
        flags |= F_WAVE
    if kind == K_DYNAMIC:
        flags |= F_DYNAMIC
    return {
        "slug": slug,
        "kind": kind,
        "key": str(item.get("key") or item.get("label") or ""),
        "entity": str(item.get("entity") or ""),
        "ref": str(item.get("ref") or item.get("source") or ""),
        "plane": int(item.get("plane", 0)),
        "rotation": int(item.get("rotation", 0)),
        "random_offset": int(item.get("random_offset", 0)),
        "flags": flags,
        "wave": int(item.get("wave", 0)),
        "x": 0,
        "y": 0,
        "min_x": 0,
        "max_x": 0,
        "min_y": 0,
        "max_y": 0,
        "local_x": opt_u16(item, "local_x"),
        "local_y": opt_u16(item, "local_y"),
        "npc_id": npc_id,
        "object_id": object_id,
    }


def point_row(slug: str, kind: int, item: dict[str, object],
              parent: dict[str, object] | None = None) -> dict[str, object]:
    src = dict(parent or {})
    src.update(item)
    row = base_row(slug, kind, src)
    row["flags"] |= F_POINT
    row["x"] = int(src["x"])
    row["y"] = int(src["y"])
    row["min_x"] = row["max_x"] = row["x"]
    row["min_y"] = row["max_y"] = row["y"]
    return row


def region_row(slug: str, item: dict[str, object]) -> dict[str, object]:
    row = base_row(slug, K_REGION, item)
    row["flags"] |= F_REGION
    row["min_x"] = int(item["min_x"])
    row["max_x"] = int(item["max_x"])
    row["min_y"] = int(item["min_y"])
    row["max_y"] = int(item["max_y"])
    return row


def unresolved_row(slug: str, item: dict[str, object]) -> dict[str, object]:
    row = base_row(slug, K_UNRESOLVED, item)
    row["flags"] |= F_REQUIRED if item.get("required") else 0
    row["ref"] = str(item.get("needed_source") or item.get("reason") or "")
    return row


def add_points(rows: list[dict[str, object]], slug: str, kind: int,
               item: dict[str, object]) -> None:
    if "points" in item:
        for point in item.get("points", []) or []:
            if isinstance(point, dict):
                rows.append(point_row(slug, kind, point, item))
    elif "x" in item and "y" in item:
        rows.append(point_row(slug, kind, item))


def collect_rows() -> list[dict[str, object]]:
    doc = tomllib.loads(SRC.read_text(errors="replace"))
    rows: list[dict[str, object]] = []
    for activity in doc.get("activities", []) or []:
        slug = str(activity["slug"])
        for item in activity.get("spawn_points", []) or []:
            if isinstance(item, dict):
                add_points(rows, slug, K_POINT, item)
        for item in activity.get("dynamic_spawns", []) or []:
            if isinstance(item, dict):
                add_points(rows, slug, K_DYNAMIC, item)
        for item in activity.get("spawn_regions", []) or []:
            if isinstance(item, dict):
                rows.append(region_row(slug, item))
        for item in activity.get("object_anchors", []) or []:
            if isinstance(item, dict):
                rows.append(point_row(slug, K_OBJECT_ANCHOR, item))
        for item in activity.get("safe_tiles", []) or []:
            if isinstance(item, dict):
                add_points(rows, slug, K_SAFE_TILE, item)
        for item in activity.get("wave_spawns", []) or []:
            if not isinstance(item, dict):
                continue
            if "rotation_regions" in item:
                for ref in item.get("rotation_regions", []) or []:
                    row = base_row(slug, K_WAVE_REGION_REF,
                                   {**item, "ref": ref})
                    row["flags"] |= F_WAVE
                    rows.append(row)
            else:
                add_points(rows, slug, K_WAVE_POINT, item)
        for item in activity.get("unresolved_spawns", []) or []:
            if isinstance(item, dict):
                rows.append(unresolved_row(slug, item))
    return sorted(rows, key=lambda r: (r["slug"], r["kind"], r["wave"],
                                      r["key"], r["entity"], r["x"], r["y"]))


def write_bin(rows: list[dict[str, object]]) -> None:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", ASPN_MAGIC, ASPN_VERSION, len(rows)))
        for row in rows:
            f.write(struct.pack(
                "<BBBBHHHHHHHHHHII",
                int(row["kind"]), int(row["plane"]), int(row["rotation"]),
                int(row["random_offset"]), int(row["flags"]),
                int(row["wave"]), int(row["x"]), int(row["y"]),
                int(row["min_x"]), int(row["max_x"]),
                int(row["min_y"]), int(row["max_y"]),
                int(row["local_x"]), int(row["local_y"]),
                int(row["npc_id"]), int(row["object_id"]),
            ))
            f.write(pstr(row["slug"], 63))
            f.write(pstr(row["key"], 47))
            f.write(pstr(row["entity"], 63))
            f.write(pstr(row["ref"], 47))


def write_report(rows: list[dict[str, object]], elapsed_ms: float) -> None:
    counts = Counter(int(r["kind"]) for r in rows)
    activities = sorted({str(r["slug"]) for r in rows})
    unresolved = [r for r in rows if int(r["kind"]) == K_UNRESOLVED]
    doc = tomllib.loads(SRC.read_text(errors="replace"))
    source_status = Counter(
        str(a.get("source_status", "unknown"))
        for a in doc.get("activities", []) or []
    )
    lines = [
        "Activity spawn export",
        "",
        f"activity spawn rows: {len(rows)}",
        f"activities: {len(activities)}",
        f"npc-spawnable rows: {sum(1 for r in rows if int(r['flags']) & F_NPC and int(r['flags']) & F_POINT)}",
        f"object-anchor rows: {counts[K_OBJECT_ANCHOR]}",
        f"unresolved required rows: {len(unresolved)}",
        f"output: {OUT.relative_to(ROOT)} ({OUT.stat().st_size} bytes)",
        f"elapsed_ms: {elapsed_ms:.3f}",
        "",
        "Status",
        "READY rows are loadable activity-local points, regions, dynamic pools, wave refs, or object anchors.",
        "Unresolved rows are explicit parity blockers; the exporter does not infer Nex/Sol coordinates from arena geometry.",
        "Provisional rows are runtime-usable only when their source_status explicitly says so; they are not authoritative OSRS sign-off.",
        "",
        "Source status",
    ]
    for key, count in sorted(source_status.items()):
        lines.append(f"- {key}: {count}")
    lines.extend([
        "",
        "Rows by kind",
    ])
    for kind in sorted(KIND_NAMES):
        lines.append(f"- {KIND_NAMES[kind]}: {counts[kind]}")
    lines.extend(["", "Unresolved rows"])
    if unresolved:
        for row in unresolved:
            lines.append(
                f"- {row['slug']}:{row['key']} entity={row['entity']} "
                f"needed={row['ref']}"
            )
    else:
        lines.append("- none")
    lines.extend(["", "Activities"])
    for slug in activities:
        rows_for_slug = [r for r in rows if r["slug"] == slug]
        by_kind = Counter(int(r["kind"]) for r in rows_for_slug)
        parts = ", ".join(f"{KIND_NAMES[k]}={by_kind[k]}"
                          for k in sorted(by_kind))
        lines.append(f"- {slug}: {len(rows_for_slug)} rows ({parts})")
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text("\n".join(lines) + "\n")


def main() -> int:
    start = time.perf_counter()
    rows = collect_rows()
    write_bin(rows)
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    write_report(rows, elapsed_ms)
    print(f"exported {len(rows)} activity spawn rows in {elapsed_ms:.3f} ms")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
