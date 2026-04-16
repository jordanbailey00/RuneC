#!/usr/bin/env python3
"""Export objects with RSMod's visual level resolution (LINK_BELOW + plane filtering).

Implements GameMapDecoder.kt's algorithm:
  1. For each object at (x, y, level):
     - Check tile at (x, y, level+1) for LINK_BELOW flag (setting & 2)
     - If set, resolved flags = tile above flags; else = current tile flags
     - If resolved flags have LINK_BELOW, visualLevel = level - 1
     - Otherwise visualLevel = level
  2. Only include objects where visualLevel == 0 (ground-level rendering)
  3. Objects keep their ORIGINAL height for heightmap sampling (so bridges
     use plane 1 heightmap and render above water)

Terrain opcodes are read as 2-byte unsigned shorts matching RSMod's
MapTileDecoder.kt (not 1-byte as older formats).
"""
import sys, struct, io
from pathlib import Path

SCRIPTS = Path("/home/joe/projects/runescape-rl-reference/valo_envs/ocean/osrs/scripts")
sys.path.insert(0, str(SCRIPTS))

import export_objects as eo
from modern_cache_reader import ModernCacheReader, read_smart
from export_collision_map_modern import find_map_groups, _read_extended_smart

# Per-region terrain tile settings [4][64][64]
_terrain_settings = {}  # (rx, ry) -> settings[4][64][64]


def load_terrain_settings(reader, map_groups, regions):
    """Parse terrain tile settings for all regions.

    RSMod MapTileDecoder reads opcodes as unsigned SHORT (2 bytes), not byte.
    Overlays (opcode 2-49) also read a 2-byte ID.
    """
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
                    while pos + 1 < len(terr):
                        # RSMod reads opcode as unsigned short (2 bytes, big-endian)
                        opcode = (terr[pos] << 8) | terr[pos + 1]; pos += 2
                        if opcode == 0:
                            break
                        elif opcode == 1:
                            pos += 1  # height byte
                            break
                        elif opcode <= 49:
                            pos += 2  # overlay ID (short)
                        elif opcode <= 81:
                            rule = opcode - 49
                            settings[h][x][y] |= rule
                        # else: underlay ID encoded in opcode

        _terrain_settings[(rx, ry)] = settings

    # Count LINK_BELOW tiles for diagnostic
    lb_count = 0
    for settings in _terrain_settings.values():
        for h in range(4):
            for x in range(64):
                for y in range(64):
                    if settings[h][x][y] & 0x2:
                        lb_count += 1
    print(f"  Loaded terrain settings for {len(_terrain_settings)} regions, {lb_count} LINK_BELOW tiles")


def get_tile_setting(world_x, world_y, level):
    """Get terrain tile setting at world coords and level."""
    rx, ry = world_x // 64, world_y // 64
    lx, ly = world_x % 64, world_y % 64
    settings = _terrain_settings.get((rx, ry))
    if not settings:
        return 0
    if level < 0 or level > 3:
        return 0
    return settings[level][lx][ly]


def resolve_visual_level(world_x, world_y, level):
    """RSMod GameMapDecoder.kt visual level resolution."""
    LINK_BELOW = 0x2

    tile_flags = get_tile_setting(world_x, world_y, level)

    if level < 3:
        tile_above_flags = get_tile_setting(world_x, world_y, level + 1)
    else:
        tile_above_flags = tile_flags

    if tile_above_flags & LINK_BELOW:
        resolved_flags = tile_above_flags
    else:
        resolved_flags = tile_flags

    if resolved_flags & LINK_BELOW:
        return level - 1
    else:
        return level


# Monkey-patch the placement parser
_orig_parse = eo.parse_object_placements_modern


def patched_parse(data, base_x, base_y):
    """Include objects whose resolved visual level == 0."""
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

            # RSMod visual level resolution
            world_x = base_x + local_x
            world_y = base_y + local_y
            visual_level = resolve_visual_level(world_x, world_y, height)

            # Only render objects whose visual level is 0 (ground floor)
            if visual_level != 0:
                continue

            # Keep original height for heightmap sampling —
            # bridges on data plane 1 use plane 1 heightmap (above water)
            placements.append(eo.PlacedObject(
                obj_id=obj_id,
                world_x=world_x,
                world_y=world_y,
                height=height,
                obj_type=obj_type,
                rotation=rotation,
            ))

    return placements


if __name__ == "__main__":
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
        print("Loading terrain settings for visual level resolution...")
        load_terrain_settings(reader, map_groups, regions)

    # Apply patch
    eo.parse_object_placements_modern = patched_parse

    # Call original main
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
