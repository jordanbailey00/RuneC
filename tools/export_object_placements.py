#!/usr/bin/env python3
"""Emit full-world object placement rows from the local b237 cache.

Output: data/defs/object_placements.bin ('OPLC' v2).
Rows are gameplay placements, not render geometry.
"""
from __future__ import annotations

import argparse
import io
import struct
import sys
import time
from collections import Counter
from pathlib import Path

from source_paths import CACHE_DIR

PIPELINE = Path(__file__).resolve().parent / "cache_pipeline"
sys.path.insert(0, str(PIPELINE))

from rc_cache import (  # noqa: E402
    MapRegionFiles,
    RcCacheStore,
    find_all_map_region_files,
    iter_location_placements,
    read_map_region_file,
)

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "data/defs/object_placements.bin"
REPORT = ROOT / "tools/reports/object_placements.txt"
ODEF = ROOT / "data/defs/object_defs.bin"

OPLC_MAGIC = 0x434C504F  # OPLC
OPLC_VERSION = 2
ODEF_MAGIC = 0x4645444F


def parse_down_heights(data: bytes) -> set[tuple[int, int, int]]:
    attributes = [[[0 for _ in range(64)] for _ in range(64)] for _ in range(4)]
    buf = io.BytesIO(data)
    for plane in range(4):
        for local_x in range(64):
            for local_y in range(64):
                while True:
                    raw = buf.read(2)
                    if len(raw) < 2:
                        return set()
                    attr = struct.unpack(">H", raw)[0]
                    if attr == 0:
                        break
                    if attr == 1:
                        buf.read(1)
                        break
                    if attr <= 49:
                        buf.read(2)
                    elif attr <= 81:
                        attributes[plane][local_x][local_y] = attr - 49
    down: set[tuple[int, int, int]] = set()
    for plane in range(4):
        for local_x in range(64):
            for local_y in range(64):
                if attributes[plane][local_x][local_y] & 0x2:
                    down.add((local_x, local_y, plane))
    return down


def gameplay_plane(raw_plane: int, local_x: int, local_y: int,
                   down_heights: set[tuple[int, int, int]]) -> int:
    plane = raw_plane
    if (local_x, local_y, 1) in down_heights:
        plane -= 1
    elif (local_x, local_y, raw_plane) in down_heights:
        plane -= 1
    return plane


def placement_key(values: tuple[int, ...]) -> int:
    h = 0xCBF29CE484222325
    payload = struct.pack("<IHHHBBBBI", *values)
    for b in payload:
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h or 1


def object_names(path: Path) -> dict[int, str]:
    if not path.is_file():
        return {}
    data = path.read_bytes()
    if len(data) < 12:
        return {}
    magic, version, count = struct.unpack_from("<III", data, 0)
    if magic != ODEF_MAGIC:
        return {}
    if version not in (1, 2):
        return {}
    pos = 12
    row_fmt = "<IHHBBBBBiiiII"
    row_size = struct.calcsize(row_fmt)
    extra_fmt = "<BBHiHH"
    extra_size = struct.calcsize(extra_fmt)
    out: dict[int, str] = {}
    for _ in range(count):
        row = struct.unpack_from(row_fmt, data, pos)
        obj_id = row[0]
        model_count = row[5]
        transform_count = row[6]
        param_count = 0
        pos += row_size
        if version >= 2:
            _supports_items, _clip_flags, param_count, _ambient_sound_id, \
                _ambient_sound_distance, _ambient_sound_retain = \
                struct.unpack_from(extra_fmt, data, pos)
            pos += extra_size
        name_len = struct.unpack_from("<H", data, pos)[0]
        pos += 2
        name = data[pos:pos + name_len].decode("latin-1", errors="replace")
        pos += name_len
        for _slot in range(5):
            act_len = struct.unpack_from("<H", data, pos)[0]
            pos += 2 + act_len
        pos += model_count * 4 + transform_count * 4
        pos += param_count * 8
        out[obj_id] = name
    return out


def write_rows(store: RcCacheStore, map_regions: dict[int, MapRegionFiles]):
    OUT.parent.mkdir(parents=True, exist_ok=True)
    type_counts: Counter[int] = Counter()
    plane_counts: Counter[int] = Counter()
    object_counts: Counter[int] = Counter()
    regions_scanned = regions_with_loc = regions_with_rows = 0
    read_errors = 0
    terrain_errors = 0
    linked_below_adjusted = 0
    linked_below_skipped = 0
    row_count = 0
    duplicate_counts: Counter[tuple[int, int, int, int, int, int, int, int]] = Counter()

    with OUT.open("wb") as f:
        f.write(struct.pack("<IIII", OPLC_MAGIC, OPLC_VERSION, 0, 0))
        for mapsquare, region in sorted(map_regions.items()):
            regions_scanned += 1
            if not region.has_locations:
                continue
            try:
                loc_data = read_map_region_file(
                    store,
                    region.region_x,
                    region.region_y,
                    "locations",
                )
            except Exception:
                read_errors += 1
                continue
            if not loc_data:
                continue
            down_heights: set[tuple[int, int, int]] = set()
            try:
                terrain_data = read_map_region_file(
                    store,
                    region.region_x,
                    region.region_y,
                    "terrain",
                )
                if terrain_data:
                    down_heights = parse_down_heights(terrain_data)
            except Exception:
                terrain_errors += 1
            regions_with_loc += 1
            before = row_count
            for placement in iter_location_placements(
                loc_data,
                region.region_x,
                region.region_y,
            ):
                key_tuple = (
                    placement.object_id & 0xFFFFFFFF,
                    placement.world_x & 0xFFFF,
                    placement.world_y & 0xFFFF,
                    mapsquare & 0xFFFF,
                    gameplay_plane(
                        placement.plane,
                        placement.local_x,
                        placement.local_y,
                        down_heights,
                    ) & 0xFF,
                    placement.shape & 0xFF,
                    placement.rotation & 0xFF,
                    0,
                )
                if key_tuple[4] != (placement.plane & 0xFF):
                    linked_below_adjusted += 1
                if key_tuple[4] > 3:
                    linked_below_skipped += 1
                    continue
                duplicate_ordinal = duplicate_counts[key_tuple]
                duplicate_counts[key_tuple] += 1
                key = placement_key((*key_tuple, duplicate_ordinal))
                f.write(struct.pack(
                    "<IQHHHBBBB",
                    placement.object_id & 0xFFFFFFFF,
                    key,
                    placement.world_x & 0xFFFF,
                    placement.world_y & 0xFFFF,
                    mapsquare & 0xFFFF,
                    key_tuple[4],
                    placement.shape & 0xFF,
                    placement.rotation & 0xFF,
                    0,
                ))
                row_count += 1
                type_counts[placement.shape] += 1
                plane_counts[key_tuple[4]] += 1
                object_counts[placement.object_id] += 1
            if row_count != before:
                regions_with_rows += 1
        f.seek(8)
        f.write(struct.pack("<II", row_count, regions_with_rows))

    return {
        "row_count": row_count,
        "regions_scanned": regions_scanned,
        "regions_with_loc": regions_with_loc,
        "regions_with_rows": regions_with_rows,
        "read_errors": read_errors,
        "terrain_errors": terrain_errors,
        "linked_below_adjusted": linked_below_adjusted,
        "linked_below_skipped": linked_below_skipped,
        "type_counts": type_counts,
        "plane_counts": plane_counts,
        "object_counts": object_counts,
        "duplicate_keys": sum(count - 1 for count in duplicate_counts.values()
                              if count > 1),
    }


def write_report(stats: dict, names: dict[int, str], elapsed_ms: float) -> None:
    missing_defs = sum(
        count for obj_id, count in stats["object_counts"].items()
        if obj_id not in names
    )
    lines = [
        "Object placement gameplay index",
        "",
        "status: READY_WITH_ACCEPTED_SIMPLIFICATIONS",
        f"output binary: data/defs/object_placements.bin ({OUT.stat().st_size} bytes)",
        f"cache source: tools/cache_pipeline/source/current_fightcaves_demo/data/cache",
        "OpenRS2 layout reference: https://archive.openrs2.org/api",
        f"elapsed_ms: {elapsed_ms:.2f}",
        "",
        "counts:",
        f"  placements:              {stats['row_count']}",
        f"  unique object ids:        {len(stats['object_counts'])}",
        f"  regions scanned:          {stats['regions_scanned']}",
        f"  regions with loc data:    {stats['regions_with_loc']}",
        f"  regions with placements:  {stats['regions_with_rows']}",
        f"  loc read errors:          {stats['read_errors']}",
        f"  terrain read errors:      {stats['terrain_errors']}",
        f"  linked-below plane adjustments: {stats['linked_below_adjusted']}",
        f"  linked-below skipped raw plane 0 rows: {stats['linked_below_skipped']}",
        f"  duplicate placement ordinals: {stats['duplicate_keys']}",
        f"  placement rows missing object_defs.bin ids: {missing_defs}",
        "",
        "by plane:",
    ]
    for plane, count in sorted(stats["plane_counts"].items()):
        lines.append(f"  {plane}: {count}")
    lines.extend(["", "by object type:"])
    for obj_type, count in sorted(stats["type_counts"].items()):
        lines.append(f"  {obj_type:>2}: {count}")
    lines.extend(["", "top placed objects:"])
    for obj_id, count in stats["object_counts"].most_common(40):
        lines.append(
            f"  {count:>7}  {obj_id:<6} {names.get(obj_id, '')}".rstrip()
        )
    lines.extend([
        "",
        "accepted simplifications:",
        "  - rows store placement identity only, not mesh vertices or atlas data",
        "  - planes are gameplay/collision planes after LINK_BELOW lowering, not raw cache planes",
        "  - initial object state rules are runtime-owned; placement rows remain immutable source anchors",
        "  - instance/activity-local dynamic object spawns remain activity-schema work",
        "",
        "remaining object/interactable parity work:",
        "  - refine uncommon object-specific consumers as exact source data lands",
        "  - pair-specific dynamic state lives in object behavior/runtime exports",
        "  - keep viewer mesh export separate from gameplay placement ownership",
    ])
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text("\n".join(lines) + "\n")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", type=Path, default=CACHE_DIR)
    args = ap.parse_args()

    start = time.perf_counter()
    store = RcCacheStore(args.cache)
    map_regions = find_all_map_region_files(store)
    names = object_names(ODEF)
    stats = write_rows(store, map_regions)
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    write_report(stats, names, elapsed_ms)
    print(REPORT.read_text())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
