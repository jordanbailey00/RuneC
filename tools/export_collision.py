#!/usr/bin/env python3
"""Export dense collision maps from the local b237 cache."""
import sys, struct, argparse
from pathlib import Path

from source_paths import CACHE_DIR

PIPELINE = Path(__file__).resolve().parent / "cache_pipeline"
sys.path.insert(0, str(PIPELINE))

from export_collision_map import new_collision_flags, parse_terrain
from export_collision_map_modern import (
    parse_objects_modern, ModernObjDef,
)
from rc_cache import (
    CONFIG_OBJECT,
    INDEX_CONFIGS,
    RcCacheStore,
    decode_location_definition,
    find_map_region_files,
    read_map_region_file,
)


def decode_obj_defs_b237(store: RcCacheStore) -> dict[int, ModernObjDef]:
    """Read collision-relevant object definition fields through rc_cache."""
    files = store.read_group(INDEX_CONFIGS, CONFIG_OBJECT)
    defs = {}
    for file_id, data in files.items():
        loc = decode_location_definition(file_id, data)
        actions = [action or None for action in loc.actions]
        defs[file_id] = ModernObjDef(
            obj_id=file_id,
            width=loc.width,
            length=loc.length,
            solid=loc.solid and loc.interact_type != 0,
            impenetrable=loc.impenetrable,
            has_actions=bool(any(loc.actions)),
            actions=actions,
        )
    print(f"  {len(defs)} obj defs parsed")
    return defs

CMAP_MAGIC = 0x434D4150
CMAP_VERSION = 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache", type=Path, default=CACHE_DIR)
    parser.add_argument("--regions", type=str, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    regions = [(int(r.split(",")[0]), int(r.split(",")[1])) for r in args.regions.split()]
    store = RcCacheStore(args.cache)

    print("Loading object definitions (b237 format)...")
    obj_defs = decode_obj_defs_b237(store)

    all_flags = {}
    for rx, ry in sorted(regions):
        region = find_map_region_files(store, rx, ry)
        ms = region.mapsquare
        flags = new_collision_flags()
        down_heights = set()

        if region.has_terrain:
            try:
                terr_data = read_map_region_file(store, rx, ry, "terrain")
                if terr_data:
                    flags, down_heights = parse_terrain(terr_data)
            except Exception as e:
                print(f"  ({rx},{ry}): terrain error: {e}")

        obj_marked = 0
        if region.has_locations:
            try:
                loc_data = read_map_region_file(store, rx, ry, "locations")
                if loc_data:
                    obj_marked = parse_objects_modern(loc_data, flags, down_heights, obj_defs)
            except Exception as e:
                print(f"  ({rx},{ry}): loc error: {e}")

        nz0 = sum(1 for x in range(64) for y in range(64) if flags[0][x][y] != 0)
        nz1 = sum(1 for x in range(64) for y in range(64) if flags[1][x][y] != 0)
        print(f"  ({rx},{ry}): {obj_marked} collision objects, {nz0} p0, {nz1} p1")

        all_flags[ms] = flags

    # No blanket plane merge — parse_terrain and parse_objects_modern already
    # handle downHeights shifting per-tile/per-object correctly.

    # Write .cmap
    with open(args.output, "wb") as f:
        f.write(struct.pack("<III", CMAP_MAGIC, CMAP_VERSION, len(all_flags)))
        for key, flags in sorted(all_flags.items()):
            f.write(struct.pack("<i", key))
            for h in range(4):
                for x in range(64):
                    for y in range(64):
                        f.write(struct.pack("<i", flags[h][x][y]))

    total = sum(1 for fl in all_flags.values() for x in range(64) for y in range(64) if fl[0][x][y] != 0)
    print(f"\nWrote {args.output}: {len(all_flags)} regions, {total} non-zero tiles on plane 0")


if __name__ == "__main__":
    main()
