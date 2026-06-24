#!/usr/bin/env python3
"""Validate object placement animation rows against a RuneC .anims bundle."""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

from validate_combat_visuals import read_anim_sequence_ids

ROOT = Path(__file__).resolve().parents[2]
OANM_MAGIC = 0x4D4E414F
OANM_VERSION = 1
OANM_ROW = struct.Struct("<IIiiiBBBBffff")


def iter_oanim_paths(paths: list[Path]):
    for path in paths:
        path = path if path.is_absolute() else ROOT / path
        if path.is_dir():
            yield from sorted(path.rglob("*.oanim"))
        else:
            yield path


def read_object_anim_ids(paths: list[Path]) -> tuple[set[int], int, list[Path]]:
    ids: set[int] = set()
    row_count = 0
    used_paths: list[Path] = []
    for path in iter_oanim_paths(paths):
        if not path.exists():
            continue
        data = path.read_bytes()
        if len(data) < 12:
            raise ValueError(f"object anim file too small: {path}")
        magic, version, count = struct.unpack_from("<III", data, 0)
        if magic != OANM_MAGIC or version != OANM_VERSION:
            raise ValueError(f"bad object anim header: {path}")
        used_paths.append(path)
        pos = 12
        for _ in range(count):
            if pos + OANM_ROW.size > len(data):
                raise ValueError(f"truncated object anim row: {path}")
            _model_id, _obj_id, anim_id, *_rest = OANM_ROW.unpack_from(data, pos)
            pos += OANM_ROW.size
            row_count += 1
            if anim_id >= 0:
                ids.add(int(anim_id))
    return ids, row_count, used_paths


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--object-anims",
        type=Path,
        nargs="+",
        default=[ROOT / "data/regions"],
        help="OANM files or directories to scan recursively.",
    )
    parser.add_argument(
        "--anims",
        type=Path,
        default=ROOT / "data/anims/object.anims",
        help="Animation bundle that should contain the OANM sequence ids.",
    )
    parser.add_argument("--max-missing", type=int, default=40)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    object_ids, row_count, paths = read_object_anim_ids(args.object_anims)
    anims_path = args.anims if args.anims.is_absolute() else ROOT / args.anims
    sequence_ids = read_anim_sequence_ids(anims_path)
    missing = sorted(object_ids - sequence_ids)

    print(
        "object anim validation: "
        f"files={len(paths)} rows={row_count} "
        f"unique_refs={len(object_ids)} anim_bundle={anims_path}"
    )
    print(
        f"coverage: {len(object_ids) - len(missing)}/{len(object_ids)} "
        f"({0.0 if not object_ids else (len(object_ids) - len(missing)) / len(object_ids) * 100.0:.1f}%)"
    )
    if missing:
        shown = missing[: max(0, args.max_missing)]
        print(f"missing: {shown}")
        if len(missing) > len(shown):
            print(f"... {len(missing) - len(shown)} more missing")
        return 1
    print("issues: errors=0")
    return 0


if __name__ == "__main__":
    sys.exit(main())
