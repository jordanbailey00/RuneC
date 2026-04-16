#!/usr/bin/env python3
"""Export collision map by directly calling the reference export_collision_map_modern module.

No reimplementation — just calls the proven functions with our region list.
"""
import sys, struct, argparse
from pathlib import Path

SCRIPTS = Path("/home/joe/projects/runescape-rl-reference/valo_envs/ocean/osrs/scripts")
sys.path.insert(0, str(SCRIPTS))

from modern_cache_reader import ModernCacheReader, read_u8, read_u16, read_u32
from export_collision_map import new_collision_flags, BLOCKED, parse_terrain
from export_collision_map_modern import (
    find_map_groups, load_xtea_keys,
    parse_objects_modern, ModernObjDef, _read_modern_obj_string,
)


def decode_obj_defs_b237(reader):
    """Parse object definitions with b237 int32 model IDs (opcodes 1,5,6,7)."""
    import io
    manifest = reader.read_index_manifest(2)
    if 6 not in manifest.group_file_ids:
        return {}
    files = reader.read_group(2, 6)
    defs = {}
    warned = 0
    for file_id, data in files.items():
        d = ModernObjDef(obj_id=file_id)
        buf = io.BytesIO(data)
        try:
            while True:
                raw = buf.read(1)
                if not raw: break
                op = raw[0]
                if op == 0: break
                elif op == 1:  # models+types, u16 model IDs (old)
                    n = read_u8(buf)
                    for _ in range(n): read_u16(buf); read_u8(buf)
                elif op == 2: _read_modern_obj_string(buf)
                elif op == 5:  # models no types, u16
                    n = read_u8(buf)
                    for _ in range(n): read_u16(buf)
                elif op == 6:  # models+types, int32 model IDs (b237)
                    n = read_u8(buf)
                    for _ in range(n): read_u32(buf); read_u8(buf)
                elif op == 7:  # models no types, int32 (b237)
                    n = read_u8(buf)
                    for _ in range(n): read_u32(buf)
                elif op == 14: d.width = read_u8(buf)
                elif op == 15: d.length = read_u8(buf)
                elif op == 17: d.solid = False
                elif op == 18: d.impenetrable = False
                elif op == 19: d.has_actions = (read_u8(buf) == 1)
                elif op in (21, 22, 23, 62, 64, 73): pass
                elif op == 24: read_u16(buf)
                elif op == 27: pass
                elif op == 28: read_u8(buf)
                elif op == 29: buf.read(1)
                elif 30 <= op <= 34:
                    s = _read_modern_obj_string(buf)
                    d.actions[op-30] = s if s != "hidden" else None
                    if s and s != "hidden": d.has_actions = True
                elif op == 39: buf.read(1)
                elif op == 40:
                    n = read_u8(buf)
                    for _ in range(n): read_u16(buf); read_u16(buf)
                elif op == 41:
                    n = read_u8(buf)
                    for _ in range(n): read_u16(buf); read_u16(buf)
                elif op == 60: read_u16(buf)
                elif op == 61: read_u16(buf)
                elif op in (65, 66, 67, 68): read_u16(buf)
                elif op == 69: read_u8(buf)
                elif op in (70, 71, 72): read_u16(buf)
                elif op == 74: d.solid = False
                elif op == 75: read_u8(buf)
                elif op == 77:
                    read_u16(buf); read_u16(buf)
                    n = read_u8(buf)
                    for _ in range(n+1): read_u16(buf)
                elif op == 78: read_u16(buf); read_u8(buf); read_u8(buf)
                elif op == 79:
                    read_u16(buf); read_u16(buf); read_u8(buf); read_u8(buf)
                    n = read_u8(buf)
                    for _ in range(n): read_u16(buf)
                elif op == 81: read_u8(buf)
                elif op == 82: pass
                elif op in (92, 93):
                    read_u16(buf); read_u16(buf)
                    if op == 93: read_u16(buf)
                    n = read_u8(buf)
                    for _ in range(n+1): read_u16(buf)
                elif op in (100, 101, 102):
                    # entityOps b237
                    read_u8(buf)
                    sub = read_u8(buf)
                    for _ in range(sub):
                        read_u8(buf)
                        read_u16(buf)
                elif op == 249:
                    n = read_u8(buf)
                    for _ in range(n):
                        is_str = read_u8(buf)
                        read_u8(buf); read_u8(buf); read_u8(buf)  # u24 key
                        if is_str: _read_modern_obj_string(buf)
                        else: read_u32(buf)
                else:
                    if warned < 5:
                        print(f"  warn: unknown opcode {op} for obj {file_id}")
                    warned += 1
                    break
        except Exception:
            pass
        defs[file_id] = d
    print(f"  {len(defs)} obj defs parsed ({warned} warnings)")
    return defs

CMAP_MAGIC = 0x434D4150
CMAP_VERSION = 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--keys", type=Path, required=True)
    parser.add_argument("--regions", type=str, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    regions = [(int(r.split(",")[0]), int(r.split(",")[1])) for r in args.regions.split()]
    reader = ModernCacheReader(str(args.cache))
    xtea_keys = load_xtea_keys(str(args.keys))
    print(f"Loaded {len(xtea_keys)} XTEA keys")

    print("Loading object definitions (b237 format)...")
    obj_defs = decode_obj_defs_b237(reader)

    print("Finding map groups...")
    map_groups = find_map_groups(reader)
    print(f"  {len(map_groups)} regions")

    all_flags = {}
    for rx, ry in sorted(regions):
        ms = (rx << 8) | ry
        info = map_groups.get(ms)
        if not info:
            print(f"  ({rx},{ry}): not in cache")
            all_flags[ms] = new_collision_flags()
            continue

        terrain_gid, obj_gid = info
        flags = new_collision_flags()
        down_heights = set()

        # Terrain — use reference parse_terrain which handles modern format
        if terrain_gid is not None:
            try:
                terr_data = reader.read_container(5, terrain_gid)
                if terr_data:
                    flags, down_heights = parse_terrain(terr_data)
            except Exception:
                # Fallback: try read_group file 0
                try:
                    files = reader.read_group(5, terrain_gid)
                    terr_data = files.get(0)
                    if terr_data:
                        flags, down_heights = parse_terrain(terr_data)
                except Exception as e:
                    print(f"  ({rx},{ry}): terrain error: {e}")

        # Objects
        obj_marked = 0
        if obj_gid is not None:
            loc_data = None
            # Try read_group first (works for b237 flat files)
            try:
                files = reader.read_group(5, obj_gid)
                loc_data = files.get(1)
            except Exception:
                pass

            # Fallback: manual XTEA
            if loc_data is None:
                import bz2, zlib
                from export_collision_map_modern import xtea_decrypt
                key = xtea_keys.get(ms)
                if key is not None:
                    raw = reader._read_raw(5, obj_gid)
                    if raw and len(raw) >= 5:
                        compression = raw[0]
                        compressed_len = struct.unpack(">I", raw[1:5])[0]
                        try:
                            decrypted = xtea_decrypt(raw[5:], key)
                            if compression == 0:
                                loc_data = decrypted[:compressed_len]
                            else:
                                payload = decrypted[4:4 + compressed_len]
                                if compression == 2:
                                    loc_data = zlib.decompress(payload[10:], -zlib.MAX_WBITS)
                                elif compression == 1:
                                    loc_data = bz2.decompress(b"BZh1" + payload)
                        except Exception:
                            pass

            if loc_data:
                obj_marked = parse_objects_modern(loc_data, flags, down_heights, obj_defs)

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
