#!/usr/bin/env python3
"""Emit full-world sparse collision tiles from the local b237 cache."""
from __future__ import annotations

import struct
import sys
import argparse
from collections import Counter
from pathlib import Path

from source_paths import CACHE_DIR

PIPELINE = Path(__file__).resolve().parent / "cache_pipeline"
sys.path.insert(0, str(PIPELINE))

from export_collision_map import new_collision_flags, parse_terrain  # noqa: E402
from export_collision_map_modern import (  # noqa: E402
    decode_modern_obj_defs_rc,
    parse_objects_modern,
)
from rc_cache import RcCacheStore, find_all_map_region_files, read_map_region_file  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "data/defs/collision_tiles.bin"
REPORT = ROOT / "tools/reports/collision_tiles.txt"

CTIL_MAGIC = 0x4C495443  # CTIL
CTIL_VERSION = 1


def nonzero_rows(mapsquare: int, flags: list[list[list[int]]]) -> list[tuple[int, int, int, int, int]]:
    rows: list[tuple[int, int, int, int, int]] = []
    for plane in range(4):
        for lx in range(64):
            for ly in range(64):
                value = flags[plane][lx][ly]
                if value:
                    rows.append((mapsquare, lx, ly, plane, value))
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=OUT)
    parser.add_argument("--report", type=Path, default=REPORT)
    parser.add_argument("--limit-regions", type=int, default=0)
    args = parser.parse_args()

    store = RcCacheStore(CACHE_DIR)
    obj_defs = decode_modern_obj_defs_rc(store)
    map_regions = find_all_map_region_files(store)
    rows: list[tuple[int, int, int, int, int]] = []
    plane_counts: Counter[int] = Counter()
    region_count = 0
    error_count = 0
    collision_objects = 0

    targets = sorted(map_regions.items())
    if args.limit_regions > 0:
        targets = targets[:args.limit_regions]

    for ms, region in targets:
        terrain = None
        loc = None
        try:
            if region.has_terrain:
                terrain = read_map_region_file(
                    store, region.region_x, region.region_y, "terrain"
                )
            if region.has_locations:
                loc = read_map_region_file(
                    store, region.region_x, region.region_y, "locations"
                )
        except Exception:
            error_count += 1
            continue
        if terrain is None and loc is None:
            error_count += 1
            continue
        flags = new_collision_flags()
        down_heights: set[tuple[int, int, int]] = set()
        if terrain:
            try:
                flags, down_heights = parse_terrain(terrain)
            except Exception:
                error_count += 1
                flags = new_collision_flags()
                down_heights = set()
        if loc:
            try:
                collision_objects += parse_objects_modern(loc, flags, down_heights, obj_defs)
            except Exception:
                error_count += 1
        rrows = nonzero_rows(ms, flags)
        if rrows:
            region_count += 1
            for row in rrows:
                plane_counts[row[3]] += 1
            rows.extend(rrows)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("wb") as f:
        f.write(struct.pack("<IIII", CTIL_MAGIC, CTIL_VERSION, len(rows), region_count))
        for ms, lx, ly, plane, flags in rows:
            f.write(struct.pack("<HBBBBI", ms, lx, ly, plane, 0, flags & 0xFFFFFFFF))

    lines = [
        "Collision tile catalog",
        "",
        "status: READY_WITH_ACCEPTED_SIMPLIFICATIONS",
        f"output binary: {args.output.relative_to(ROOT) if args.output.is_relative_to(ROOT) else args.output} ({args.output.stat().st_size} bytes)",
        f"source cache: {CACHE_DIR.relative_to(ROOT)}",
        "source object defs: b237 cache index 2 group 6",
        f"cache map groups: {len(map_regions)}",
        f"exported map groups: {len(targets)}",
        f"regions with collision: {region_count}",
        f"non-zero collision tiles: {len(rows)}",
        f"collision-marked objects: {collision_objects}",
        f"region parse/read errors: {error_count}",
        "",
        "by plane:",
    ]
    for plane in range(4):
        lines.append(f"  plane {plane}: {plane_counts[plane]}")
    lines.extend([
        "",
        "accepted simplifications:",
        "  - sparse rows store non-zero collision flags only; runtime expands them to shared dense lookup",
        "  - area semantics such as wilderness/multi/safe-zone are a separate area-flag dataset",
    ])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text("\n".join(lines) + "\n")
    print(args.report.read_text())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
