"""Parse spotanim data from OSRS cache and export spotanim metadata.

Supports both 317-format (tarnish) and modern OpenRS2 flat file caches.

SpotAnimations (GFX effects) are visual effects like spell impacts, projectiles,
and special attack graphics. Each has a model ID, animation sequence ID, scale,
and optional recolors.

Usage (317 cache):
    uv run python scripts/export_spotanims.py \
        --cache ../reference/tarnish/game-server/data/cache

Usage (modern cache):
    uv run python scripts/export_spotanims.py \
        --modern-cache ../reference/osrs-cache-modern

    # export specific GFX IDs only
    uv run python scripts/export_spotanims.py \
        --cache ../reference/tarnish/game-server/data/cache \
        --ids 27,368,369,377
"""

import argparse
import io
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from export_collision_map import (
    CONFIG_INDEX,
    CacheReader,
    decode_archive,
    hash_archive_name,
)
from rc_cache import (
    CONFIG_SPOTANIM,
    INDEX_CONFIGS,
    RcCacheStore,
    decode_spotanim_definition,
)

CONFIG_ARCHIVE = 2
SPOTANIM_BIN_MAGIC = 0x544F5053  # SPOT
SPOTANIM_BIN_VERSION = 1


@dataclass
class SpotAnimDef:
    """SpotAnimation definition from spotanim.dat."""

    gfx_id: int
    model_id: int = 0
    animation_id: int = -1
    resize_xy: int = 128  # 128 = 1.0x scale
    resize_z: int = 128
    rotation: int = 0
    brightness: int = 0
    shadow: int = 0
    recolor_src: list[int] = field(default_factory=list)
    recolor_dst: list[int] = field(default_factory=list)


def decode_spotanims(cache: CacheReader) -> dict[int, SpotAnimDef]:
    """Parse spotanim.dat from config archive, return dict of gfx_id -> SpotAnimDef."""
    raw = cache.get(CONFIG_INDEX, CONFIG_ARCHIVE)
    if raw is None:
        sys.exit("could not read config archive")

    archive = decode_archive(raw)
    key = hash_archive_name("spotanim.dat") & 0xFFFFFFFF
    data = archive.get(key)
    if data is None:
        # try signed hash
        data = archive.get(hash_archive_name("spotanim.dat"))
    if data is None:
        sys.exit("spotanim.dat not found in config archive")

    buf = io.BytesIO(data)
    count = struct.unpack(">H", buf.read(2))[0]
    print(f"spotanim.dat: {count + 1} entries declared")

    spotanims: dict[int, SpotAnimDef] = {}

    for _ in range(count + 1):
        gfx_id_raw = buf.read(2)
        if len(gfx_id_raw) < 2:
            break
        gfx_id = struct.unpack(">H", gfx_id_raw)[0]
        if gfx_id == 65535:
            break

        data_size = struct.unpack(">H", buf.read(2))[0]
        entry_data = buf.read(data_size)

        sa = SpotAnimDef(gfx_id=gfx_id)
        entry_buf = io.BytesIO(entry_data)

        while True:
            opcode_raw = entry_buf.read(1)
            if len(opcode_raw) == 0:
                break
            opcode = opcode_raw[0]

            if opcode == 0:
                break
            elif opcode == 1:
                sa.model_id = struct.unpack(">H", entry_buf.read(2))[0]
            elif opcode == 2:
                anim_id = struct.unpack(">H", entry_buf.read(2))[0]
                sa.animation_id = anim_id if anim_id != 65535 else -1
            elif opcode == 4:
                sa.resize_xy = struct.unpack(">H", entry_buf.read(2))[0]
            elif opcode == 5:
                sa.resize_z = struct.unpack(">H", entry_buf.read(2))[0]
            elif opcode == 6:
                sa.rotation = struct.unpack(">H", entry_buf.read(2))[0]
            elif opcode == 7:
                sa.brightness = entry_buf.read(1)[0]
            elif opcode == 8:
                sa.shadow = entry_buf.read(1)[0]
            elif opcode == 40:
                length = entry_buf.read(1)[0]
                for _ in range(length):
                    src = struct.unpack(">H", entry_buf.read(2))[0]
                    dst = struct.unpack(">H", entry_buf.read(2))[0]
                    sa.recolor_src.append(src)
                    sa.recolor_dst.append(dst)
            elif opcode == 41:
                length = entry_buf.read(1)[0]
                for _ in range(length):
                    entry_buf.read(4)  # skip retexture pairs
            else:
                print(f"  warning: unknown opcode {opcode} for GFX {gfx_id}")
                break

        spotanims[gfx_id] = sa

    return spotanims


def decode_spotanims_modern(store: RcCacheStore) -> dict[int, SpotAnimDef]:
    """Parse spotanim entries from modern cache (config index 2, group 13)."""
    files = store.read_group(INDEX_CONFIGS, CONFIG_SPOTANIM)
    spotanims: dict[int, SpotAnimDef] = {}

    for gfx_id, entry_data in files.items():
        decoded = decode_spotanim_definition(gfx_id, entry_data)
        if not decoded.complete:
            print(
                f"  warning: unknown modern spotanim opcode "
                f"{decoded.unknown_opcode} for GFX {gfx_id}"
            )
        spotanims[gfx_id] = SpotAnimDef(
            gfx_id=gfx_id,
            model_id=decoded.model_id,
            animation_id=decoded.animation_id,
            resize_xy=decoded.resize_xy,
            resize_z=decoded.resize_z,
            rotation=decoded.rotation,
            brightness=decoded.brightness,
            shadow=decoded.shadow,
            recolor_src=list(decoded.recolor_from),
            recolor_dst=list(decoded.recolor_to),
        )

    return spotanims


def write_spotanims_binary(path: Path, spotanims: dict[int, SpotAnimDef]) -> None:
    """Write cache-backed spot animation definitions for the C viewer."""
    path.parent.mkdir(parents=True, exist_ok=True)
    rows = [
        sa for sa in sorted(spotanims.values(), key=lambda row: row.gfx_id)
        if sa.model_id >= 0
    ]
    with path.open("wb") as f:
        f.write(struct.pack("<III", SPOTANIM_BIN_MAGIC,
                            SPOTANIM_BIN_VERSION, len(rows)))
        for sa in rows:
            f.write(struct.pack(
                "<IiiIIIii",
                sa.gfx_id & 0xFFFFFFFF,
                int(sa.model_id),
                int(sa.animation_id),
                sa.resize_xy & 0xFFFFFFFF,
                sa.resize_z & 0xFFFFFFFF,
                sa.rotation & 0xFFFFFFFF,
                int(sa.brightness),
                int(sa.shadow),
            ))


# GFX IDs we need for the PvP viewer
TARGET_GFX_IDS = {
    27,    # crossbow bolt projectile
    368,   # ice barrage projectile (orb)
    369,   # ice barrage impact (freeze splash)
    377,   # blood barrage impact
    1468,  # dragon bolt projectile
}


def main() -> None:
    """Parse spotanim data and print metadata for target GFX IDs."""
    parser = argparse.ArgumentParser(description="parse spotanim data from OSRS cache")
    cache_group = parser.add_mutually_exclusive_group(required=True)
    cache_group.add_argument(
        "--cache",
        type=Path,
        help="path to 317 tarnish cache directory",
    )
    cache_group.add_argument(
        "--modern-cache",
        type=Path,
        help="path to modern OpenRS2 cache directory",
    )
    parser.add_argument(
        "--ids",
        type=str,
        default=None,
        help="comma-separated GFX IDs to show (default: all PvP-relevant)",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="print all spotanims (not just targets)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="write SPOT binary definitions for the RuneC viewer",
    )
    args = parser.parse_args()

    use_modern = args.modern_cache is not None
    cache_path = args.modern_cache if use_modern else args.cache

    print(f"reading {'modern' if use_modern else '317'} cache from {cache_path}")

    if use_modern:
        store = RcCacheStore(cache_path)
        spotanims = decode_spotanims_modern(store)
    else:
        cache = CacheReader(cache_path)
        spotanims = decode_spotanims(cache)
    print(f"parsed {len(spotanims)} spotanims total\n")

    if args.output:
        write_spotanims_binary(args.output, spotanims)
        print(f"wrote {args.output}")

    if args.ids:
        target_ids = {int(x) for x in args.ids.split(",")}
    elif args.all:
        target_ids = set(spotanims.keys())
    else:
        target_ids = TARGET_GFX_IDS

    print(f"{'GFX':>5}  {'model':>6}  {'anim':>5}  {'scaleXY':>7}  {'scaleZ':>6}  {'rot':>4}  recolors")
    print("-" * 70)

    for gfx_id in sorted(target_ids):
        sa = spotanims.get(gfx_id)
        if sa is None:
            print(f"{gfx_id:>5}  (not found)")
            continue

        recolors = ""
        if sa.recolor_src:
            recolors = ", ".join(
                f"{s}->{d}" for s, d in zip(sa.recolor_src, sa.recolor_dst)
            )

        print(
            f"{gfx_id:>5}  {sa.model_id:>6}  {sa.animation_id:>5}  "
            f"{sa.resize_xy:>7}  {sa.resize_z:>6}  {sa.rotation:>4}  {recolors}"
        )

    # print summary for integration
    model_ids = set()
    anim_ids = set()
    for gfx_id in target_ids:
        sa = spotanims.get(gfx_id)
        if sa:
            if sa.model_id > 0:
                model_ids.add(sa.model_id)
            if sa.animation_id >= 0:
                anim_ids.add(sa.animation_id)

    print(f"\nmodel IDs to add to export_models.py: {sorted(model_ids)}")
    print(f"anim seq IDs to add to export_animations.py: {sorted(anim_ids)}")


if __name__ == "__main__":
    main()
