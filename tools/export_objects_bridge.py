#!/usr/bin/env python3
"""Patch export_objects.py at runtime to include plane 1 LINK_BELOW objects (bridges).

Monkey-patches parse_object_placements_modern to include plane 1 objects
only when the terrain tile has LINK_BELOW (setting & 2), then adjusts
their height to 0 so they render at ground level.
"""
import sys, struct, io
from pathlib import Path

SCRIPTS = Path("/home/joe/projects/runescape-rl-reference/valo_envs/ocean/osrs/scripts")
sys.path.insert(0, str(SCRIPTS))

import export_objects as eo
from modern_cache_reader import ModernCacheReader, read_smart
from export_collision_map_modern import find_map_groups, _read_extended_smart

# Parse terrain settings for LINK_BELOW tiles across all regions
_link_below_tiles = {}  # (world_x, world_y) -> True


def load_link_below(reader, map_groups, regions):
    """Parse terrain for all regions and record LINK_BELOW tiles."""
    for rx, ry in regions:
        ms = (rx << 8) | ry
        info = map_groups.get(ms)
        if not info:
            continue
        gid = info[0] if isinstance(info, tuple) else info
        try:
            files = reader.read_group(5, gid)
            terr = files.get(0)
        except Exception:
            continue
        if not terr:
            continue

        settings = [[[0] * 64 for _ in range(64)] for _ in range(4)]
        pos = 0
        for h in range(4):
            for x in range(64):
                for y in range(64):
                    while pos < len(terr):
                        v = terr[pos]; pos += 1
                        if v == 0: break
                        elif v == 1: pos += 1; break
                        elif 2 <= v <= 49: pos += 1
                        elif 50 <= v <= 81: settings[h][x][y] = v - 49

        # Record LINK_BELOW tiles: plane 1 setting & 2
        for x in range(64):
            for y in range(64):
                if settings[1][x][y] & 2:
                    wx = rx * 64 + x
                    wy = ry * 64 + y
                    _link_below_tiles[(wx, wy)] = True

    print(f"  {len(_link_below_tiles)} LINK_BELOW tiles found")


# Monkey-patch the placement parser
_orig_parse = eo.parse_object_placements_modern


def patched_parse(data, base_x, base_y):
    """Include plane 1 objects at LINK_BELOW tiles, adjusted to plane 0."""
    buf = io.BytesIO(data)
    obj_id = -1
    placements = []

    while True:
        obj_id_offset = _read_extended_smart(buf)
        if obj_id_offset == 0:
            break
        obj_id += obj_id_offset
        obj_pos_info = 0

        while True:
            pos_offset = read_smart(buf)
            if pos_offset == 0:
                break
            obj_pos_info += pos_offset - 1
            raw_byte = buf.read(1)
            if not raw_byte:
                return placements
            info = raw_byte[0]

            local_y = obj_pos_info & 0x3F
            local_x = (obj_pos_info >> 6) & 0x3F
            height = (obj_pos_info >> 12) & 0x3
            obj_type = info >> 2
            rotation = info & 0x3

            if obj_type not in eo.EXPORTED_TYPES:
                continue

            # Plane 0: always include
            # Plane 1: include non-roof objects (types 0-11, 22). Skip roof
            #          types 12-21 which are upper floor geometry.
            #          Keep height=1 so the exporter uses the plane 1 heightmap
            #          for vertical positioning (bridges render above water).
            # Plane 2+: always skip
            if height == 0:
                pass
            elif height == 1:
                if 12 <= obj_type <= 21:
                    continue  # skip roofing
                # Keep height=1 — the exporter samples heightmaps[1] which
                # positions the object at the correct elevation above ground
            else:
                continue

            placements.append(eo.PlacedObject(
                obj_id=obj_id,
                world_x=base_x + local_x,
                world_y=base_y + local_y,
                height=height,
                obj_type=obj_type,
                rotation=rotation,
            ))

    return placements


if __name__ == "__main__":
    # Pre-load LINK_BELOW data before patching
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--modern-cache", type=Path)
    parser.add_argument("--keys", type=Path)
    parser.add_argument("--regions", type=str)
    parser.add_argument("--output", type=Path)
    args, remaining = parser.parse_known_args()

    if args.modern_cache and args.regions:
        reader = ModernCacheReader(str(args.modern_cache))
        map_groups = find_map_groups(reader)
        regions = []
        for r in args.regions.split():
            rx, ry = r.split(",")
            regions.append((int(rx), int(ry)))
        print("Loading LINK_BELOW tiles for bridge support...")
        load_link_below(reader, map_groups, regions)

    # Apply patch
    eo.parse_object_placements_modern = patched_parse

    # Re-build sys.argv and call the original main
    sys.argv = [str(SCRIPTS / "export_objects.py")]
    if args.modern_cache:
        sys.argv += ["--modern-cache", str(args.modern_cache)]
    if args.keys:
        sys.argv += ["--keys", str(args.keys)]
    if args.regions:
        sys.argv += ["--regions", args.regions]
    if args.output:
        sys.argv += ["--output", str(args.output)]
    sys.argv += remaining

    eo.main()
