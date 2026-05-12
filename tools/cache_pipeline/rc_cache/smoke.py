"""Smoke checks for the repo-local cache foundation."""

from __future__ import annotations

import argparse
import io
import struct
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from rc_cache.bytes import read_big_smart, read_i32, read_string, read_u16, read_u24
    from rc_cache.config import CONFIG_OBJECT, INDEX_CONFIGS, INDEX_MAPS, INDEX_MODELS
    from rc_cache.container import decompress_container
    from rc_cache.groups import split_group
    from rc_cache.index import parse_index_manifest
    from rc_cache.maps import find_map_region_files, read_map_region_file
    from rc_cache.names import find_group_id, name_hash
    from rc_cache.store import RcCacheStore
else:
    from .bytes import read_big_smart, read_i32, read_string, read_u16, read_u24
    from .config import CONFIG_OBJECT, INDEX_CONFIGS, INDEX_MAPS, INDEX_MODELS
    from .container import decompress_container
    from .groups import split_group
    from .index import parse_index_manifest
    from .maps import find_map_region_files, read_map_region_file
    from .names import find_group_id, name_hash
    from .store import RcCacheStore


def _container_none(payload: bytes) -> bytes:
    return bytes([0]) + struct.pack(">I", len(payload)) + payload


def _synthetic_manifest() -> bytes:
    # protocol 6, revision 1, unnamed, groups 5 and 7.
    data = bytearray()
    data.append(6)
    data.extend(struct.pack(">I", 1))
    data.append(0)
    data.extend(struct.pack(">H", 2))
    data.extend(struct.pack(">H", 5))
    data.extend(struct.pack(">H", 2))
    data.extend(struct.pack(">II", 11, 22))
    data.extend(struct.pack(">II", 1, 1))
    data.extend(struct.pack(">HH", 1, 2))
    data.extend(struct.pack(">H", 0))
    data.extend(struct.pack(">HH", 0, 2))
    return bytes(data)


def run_synthetic_smoke() -> None:
    buf = io.BytesIO(b"\x12\x34\x01\x02\x03\x01\x00\x80\x00ok\x00")
    assert read_u16(buf) == 0x1234
    assert read_u24(buf) == 0x010203
    assert read_big_smart(buf) == 0x0100
    assert read_u16(buf) == 0x8000
    assert read_string(buf) == "ok"

    i32 = io.BytesIO(struct.pack(">i", -99))
    assert read_i32(i32) == -99

    group_payload = b"axybz"
    trailer = struct.pack(">iiii", 1, 1, 1, 0)
    files = split_group(group_payload + trailer + b"\x02", [0, 2])
    assert files == {0: b"ab", 2: b"xyz"}

    assert decompress_container(_container_none(b"cache")) == b"cache"

    manifest = parse_index_manifest(_synthetic_manifest())
    assert manifest.protocol == 6
    assert manifest.revision == 1
    assert manifest.group_ids == [5, 7]
    assert manifest.group_file_ids[5] == [0]
    assert manifest.group_file_ids[7] == [0, 2]

    named = parse_index_manifest(
        bytes([6])
        + struct.pack(">I", 1)
        + bytes([1])
        + struct.pack(">H", 1)
        + struct.pack(">H", 5)
        + struct.pack(">i", name_hash("m50_53"))
        + struct.pack(">I", 1)
        + struct.pack(">I", 1)
        + struct.pack(">H", 1)
        + struct.pack(">H", 0)
        + struct.pack(">i", name_hash("payload"))
    )
    assert find_group_id(named, "M50_53") == 5

    print("rc_cache synthetic smoke: ok")


def run_cache_smoke(cache_dir: Path, require_b237_configs: bool = False) -> None:
    store = RcCacheStore(cache_dir)
    config_manifest = store.read_index_manifest(INDEX_CONFIGS)
    print(
        "config manifest: "
        f"layout={store.layout} "
        f"protocol={config_manifest.protocol} groups={len(config_manifest.group_ids)}"
    )

    if CONFIG_OBJECT in config_manifest.group_file_ids:
        object_files = store.read_group(INDEX_CONFIGS, CONFIG_OBJECT)
        print(f"object config group {CONFIG_OBJECT}: files={len(object_files)}")
    else:
        print(
            f"object config group {CONFIG_OBJECT}: missing "
            f"(available={config_manifest.group_ids})"
        )
        if require_b237_configs:
            msg = f"required b237 object config group {CONFIG_OBJECT} is missing"
            raise KeyError(msg)

    map_manifest = store.read_index_manifest(INDEX_MAPS)
    named_maps = bool(map_manifest.group_name_hashes)
    print(f"map manifest: groups={len(map_manifest.group_ids)} named={named_maps}")
    if named_maps:
        print(f"map m50_53 group: {find_group_id(map_manifest, 'm50_53')}")
        print(f"loc l50_53 group: {find_group_id(map_manifest, 'l50_53')}")
    region = find_map_region_files(store, 50, 53)
    terrain = read_map_region_file(store, 50, 53, "terrain")
    locations = read_map_region_file(store, 50, 53, "locations")
    print(
        "region 50,53 files: "
        f"terrain={region.terrain_group_id}/{region.terrain_file_id} "
        f"bytes={len(terrain) if terrain is not None else 0} "
        f"locations={region.location_group_id}/{region.location_file_id} "
        f"bytes={len(locations) if locations is not None else 0}"
    )

    model_manifest = store.read_index_manifest(INDEX_MODELS)
    if model_manifest.group_ids:
        first_model = model_manifest.group_ids[0]
        model_files = store.read_group(INDEX_MODELS, first_model)
        first_file = next(iter(model_files.values()), b"")
        print(f"first model group: id={first_model} bytes={len(first_file)}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, help="Path to OpenRS2 or Jagex cache root")
    parser.add_argument(
        "--synthetic",
        action="store_true",
        help="Run local synthetic checks that do not require cache files",
    )
    parser.add_argument(
        "--require-b237-configs",
        action="store_true",
        help="Fail if modern b237 config groups such as object group 6 are missing",
    )
    args = parser.parse_args(argv)

    if args.synthetic:
        run_synthetic_smoke()

    if args.cache is not None:
        if not args.cache.is_dir():
            print(f"cache directory not found: {args.cache}", file=sys.stderr)
            return 2
        run_cache_smoke(args.cache, require_b237_configs=args.require_b237_configs)

    if not args.synthetic and args.cache is None:
        parser.error("pass --synthetic, --cache, or both")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
