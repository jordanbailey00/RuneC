"""Export collision data from the local b237 OSRS cache to .cmap binary format.

Reads modern cache data (flat file format from OpenRS2), parses terrain and object
data for specified regions, and outputs collision flags compatible with
osrs_pvp_collision.h's collision_map_load().

Modern b237 cache differences from 317:
  - Map data is in index 5, unnamed mapsquare groups, file 0 terrain/file 1 locations
  - Location archives are not XTEA encrypted in b237
  - Object ID deltas in location data use chained smart values
  - Object definitions in index 2 group 6, modern opcode set

Usage:
    python3 tools/cache_pipeline/export_collision_map_modern.py \
        --output data/zulrah.cmap \
        --regions 35,48

    # export multiple regions
    python3 tools/cache_pipeline/export_collision_map_modern.py \
        --output data/world.cmap \
        --regions 35,48 34,48 36,48

    # export wilderness regions
    python3 tools/cache_pipeline/export_collision_map_modern.py \
        --output data/wilderness.cmap \
        --wilderness
"""

import argparse
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from export_collision_map import (
    BLOCKED,
    IMPENETRABLE_BLOCKED,
    WALL_EAST,
    WALL_NORTH,
    WALL_SOUTH,
    WALL_WEST,
    CollisionFlags,
    mark_occupant,
    mark_wall,
    new_collision_flags,
    parse_terrain,
    write_cmap,
)
from rc_cache import (
    CONFIG_OBJECT,
    INDEX_CONFIGS,
    RcCacheStore,
    decode_location_definition,
    find_all_map_region_files,
    find_map_region_files,
    iter_location_placements,
    read_map_region_file,
)

DEFAULT_MODERN_CACHE = Path(__file__).resolve().parent / "source/current_fightcaves_demo/data/cache"

# object type IDs (same as 317)
OBJ_STRAIGHT_WALL = 0
OBJ_DIAGONAL_CORNER = 1
OBJ_ENTIRE_WALL = 2
OBJ_WALL_CORNER = 3
OBJ_DIAGONAL_WALL = 9
OBJ_GENERAL_PROP = 10
OBJ_WALKABLE_PROP = 11
OBJ_GROUND_PROP = 22

# directions
DIR_WEST = 0
DIR_NORTH = 1
DIR_EAST = 2
DIR_SOUTH = 3

# --- modern object definition decoder ---


@dataclass
class ModernObjDef:
    """Object definition from modern OSRS cache, with collision-relevant fields."""

    obj_id: int = 0
    width: int = 1
    length: int = 1
    solid: bool = True
    impenetrable: bool = True
    has_actions: bool = False
    actions: list[str | None] = field(default_factory=lambda: [None] * 5)


def decode_modern_obj_defs_rc(store: RcCacheStore) -> dict[int, ModernObjDef]:
    """Decode collision-relevant object fields through rc_cache."""
    files = store.read_group(INDEX_CONFIGS, CONFIG_OBJECT)
    defs: dict[int, ModernObjDef] = {}
    for obj_id, data in files.items():
        loc = decode_location_definition(obj_id, data)
        defs[obj_id] = ModernObjDef(
            obj_id=obj_id,
            width=loc.width,
            length=loc.length,
            solid=loc.solid and loc.interact_type != 0,
            impenetrable=loc.impenetrable,
            has_actions=bool(any(loc.actions)),
            actions=[action or None for action in loc.actions],
        )
    return defs


# --- modern map data parsing ---


def parse_objects_modern(
    data: bytes,
    flags: CollisionFlags,
    down_heights: set[tuple[int, int, int]],
    obj_defs: dict[int, ModernObjDef],
) -> int:
    """Parse modern-format object data and mark collision flags."""
    marked_count = 0

    for placement in iter_location_placements(data, 0, 0):
        local_x = placement.local_x
        local_y = placement.local_y
        height = placement.plane
        obj_type = placement.shape
        direction = placement.rotation

        # downHeights adjustment
        if (local_x, local_y, 1) in down_heights:
            height -= 1
            if height < 0:
                continue
        elif (local_x, local_y, height) in down_heights:
            height -= 1

        if height < 0:
            continue

        d = obj_defs.get(placement.object_id)
        if d is None:
            continue

        if not d.solid:
            continue

        # swap width/length for N/S rotation
        if direction == DIR_NORTH or direction == DIR_SOUTH:
            size_x = d.length
            size_y = d.width
        else:
            size_x = d.width
            size_y = d.length

        if obj_type == OBJ_GROUND_PROP:
            if d.has_actions:
                mark_occupant(flags, height, local_x, local_y, size_x, size_y, False)
                marked_count += 1
        elif obj_type in (OBJ_GENERAL_PROP, OBJ_WALKABLE_PROP) or obj_type >= 12:
            mark_occupant(
                flags, height, local_x, local_y, size_x, size_y, d.impenetrable
            )
            marked_count += 1
        elif obj_type == OBJ_DIAGONAL_WALL:
            mark_occupant(
                flags, height, local_x, local_y, size_x, size_y, d.impenetrable
            )
            marked_count += 1
        elif 0 <= obj_type <= 3:
            mark_wall(
                flags, direction, height, local_x, local_y, obj_type, d.impenetrable
            )
            marked_count += 1

    return marked_count

# --- main ---


def main() -> None:
    """Export collision maps from modern OSRS cache."""
    parser = argparse.ArgumentParser(
        description="export collision data from modern OpenRS2 OSRS cache"
    )
    parser.add_argument(
        "--cache",
        type=Path,
        default=DEFAULT_MODERN_CACHE,
        help="path to the repo-local b237 OpenRS2 cache directory",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("data/zulrah.cmap"),
        help="output .cmap binary file",
    )
    parser.add_argument(
        "--regions",
        nargs="+",
        type=str,
        help="region coordinates as rx,ry pairs (e.g. 35,48 34,48)",
    )
    parser.add_argument(
        "--wilderness",
        action="store_true",
        help="export wilderness regions (rx=44-56, ry=48-62)",
    )
    parser.add_argument(
        "--all-regions",
        action="store_true",
        help="export all available regions",
    )
    parser.add_argument(
        "--ascii",
        action="store_true",
        help="print ASCII visualization of collision map",
    )
    args = parser.parse_args()

    if not args.cache.exists():
        sys.exit(f"cache directory not found: {args.cache}")
    args.output.parent.mkdir(parents=True, exist_ok=True)

    print(f"reading modern cache from {args.cache}")
    reader = RcCacheStore(args.cache)

    print("loading modern object definitions...")
    obj_defs = decode_modern_obj_defs_rc(reader)
    print(f"  {len(obj_defs)} object definitions parsed")

    print("resolving index 5 map regions...")
    map_regions = find_all_map_region_files(reader)
    print(f"  {len(map_regions)} regions found in index 5")

    # determine which regions to export
    target_mapsquares: set[int] = set()

    if args.regions:
        for coord in args.regions:
            parts = coord.split(",")
            rx, ry = int(parts[0]), int(parts[1])
            ms = (rx << 8) | ry
            target_mapsquares.add(ms)
    elif args.wilderness:
        for rx in range(44, 57):
            for ry in range(48, 63):
                ms = (rx << 8) | ry
                if ms in map_regions:
                    target_mapsquares.add(ms)
    elif args.all_regions:
        target_mapsquares = set(map_regions.keys())
    else:
        # default: Zulrah region
        target_mapsquares.add((35 << 8) | 48)

    print(f"\nexporting {len(target_mapsquares)} regions...")

    output_regions: dict[int, CollisionFlags] = {}
    decoded = 0
    errors = 0
    total_obj_marked = 0

    for ms in sorted(target_mapsquares):
        rx = (ms >> 8) & 0xFF
        ry = ms & 0xFF

        region = find_map_region_files(reader, rx, ry)
        if not region.has_terrain and not region.has_locations:
            print(f"  region ({rx},{ry}): not found in index 5")
            errors += 1
            continue

        # parse terrain
        if not region.has_terrain:
            print(f"  region ({rx},{ry}): no terrain group")
            errors += 1
            continue

        terrain_data = read_map_region_file(reader, rx, ry, "terrain")
        if terrain_data is None:
            print(f"  region ({rx},{ry}): failed to read terrain")
            errors += 1
            continue

        flags, down_heights = parse_terrain(terrain_data)

        # parse objects (XTEA encrypted)
        obj_marked = 0
        if region.has_locations:
            obj_data = read_map_region_file(reader, rx, ry, "locations")
            if obj_data:
                obj_marked = parse_objects_modern(
                    obj_data, flags, down_heights, obj_defs
                )
                total_obj_marked += obj_marked

        output_regions[ms] = flags
        decoded += 1

        # per-region stats
        blocked = sum(
            1
            for x in range(64)
            for y in range(64)
            if flags[0][x][y] & BLOCKED
        )
        walled = sum(
            1
            for x in range(64)
            for y in range(64)
            if flags[0][x][y] & (WALL_NORTH | WALL_SOUTH | WALL_EAST | WALL_WEST)
        )
        print(
            f"  region ({rx},{ry}): "
            f"{blocked} blocked, {walled} walled, {obj_marked} objects marked"
        )

    print(f"\ndecoded {decoded} regions, {errors} skipped")
    print(f"total objects marked for collision: {total_obj_marked}")

    # overall stats
    total_blocked = 0
    total_walled = 0
    for region_flags in output_regions.values():
        for x in range(64):
            for y in range(64):
                f = region_flags[0][x][y]
                if f & BLOCKED:
                    total_blocked += 1
                if f & (WALL_NORTH | WALL_SOUTH | WALL_EAST | WALL_WEST):
                    total_walled += 1

    print(f"height-0 totals: {total_blocked} blocked, {total_walled} walled")

    # ASCII visualization
    if args.ascii and len(output_regions) == 1:
        ms = next(iter(output_regions))
        rx = (ms >> 8) & 0xFF
        ry = ms & 0xFF
        region_flags = output_regions[ms]

        print(f"\n--- collision map for region ({rx},{ry}) height 0 ---")
        print("  legend: . = walkable, # = blocked (terrain), W = walled (object)")
        print("          B = blocked (object), X = blocked + walled")
        print()

        for local_y in range(63, -1, -1):
            row = []
            for local_x in range(64):
                f = region_flags[0][local_x][local_y]
                has_block = bool(f & BLOCKED)
                has_wall = bool(
                    f & (WALL_NORTH | WALL_SOUTH | WALL_EAST | WALL_WEST)
                )
                has_imp_block = bool(f & IMPENETRABLE_BLOCKED)

                if has_block and has_wall:
                    row.append("X")
                elif has_wall:
                    row.append("W")
                elif has_imp_block:
                    row.append("B")
                elif has_block:
                    row.append("#")
                else:
                    row.append(".")
            print("".join(row))

    write_cmap(args.output, output_regions)
    file_size = args.output.stat().st_size
    print(f"\nwrote {file_size:,} bytes to {args.output}")


if __name__ == "__main__":
    main()
