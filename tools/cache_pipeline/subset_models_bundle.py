#!/usr/bin/env python3
"""Create a small MDL2/MDL3 model bundle by copying selected entries."""

from __future__ import annotations

import argparse
import shutil
import struct
from pathlib import Path


MDL2_MAGIC = 0x4D444C32
MDL3_MAGIC = 0x4D444C33


def parse_ids(raw: str) -> set[int]:
    ids: set[int] = set()
    for part in raw.replace("\n", ",").split(","):
        part = part.strip()
        if part:
            ids.add(int(part, 0))
    return ids


def companion_path(path: Path, suffix: str) -> Path:
    return path.with_suffix(suffix)


def copy_if_present(src: Path, dst: Path) -> None:
    if src.is_file():
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dst)
        print(f"copied {dst}")


def subset_models(src: Path, dst: Path, ids: set[int]) -> None:
    data = src.read_bytes()
    view = memoryview(data)
    if len(view) < 8:
        raise SystemExit(f"model bundle is too small: {src}")
    magic, count = struct.unpack_from("<II", view, 0)
    if magic not in (MDL2_MAGIC, MDL3_MAGIC):
        raise SystemExit(f"bad model magic: {src}")
    offsets_off = 8
    offsets_end = offsets_off + count * 4
    if len(view) < offsets_end:
        raise SystemExit(f"truncated model offset table: {src}")
    offsets = list(struct.unpack_from(f"<{count}I", view, offsets_off))

    selected: list[tuple[int, bytes]] = []
    found: set[int] = set()
    for idx, off in enumerate(offsets):
        next_off = offsets[idx + 1] if idx + 1 < count else len(view)
        if off + 4 > len(view) or next_off < off or next_off > len(view):
            raise SystemExit(f"bad model offset table entry {idx}: {src}")
        model_id = struct.unpack_from("<I", view, off)[0]
        if model_id in ids:
            selected.append((model_id, bytes(view[off:next_off])))
            found.add(model_id)

    missing = sorted(ids - found)
    if missing:
        raise SystemExit(
            "requested model IDs missing from bundle: "
            + ", ".join(str(i) for i in missing)
        )

    dst.parent.mkdir(parents=True, exist_ok=True)
    header_size = 8 + len(selected) * 4
    next_out = header_size
    new_offsets: list[int] = []
    for _model_id, blob in selected:
        new_offsets.append(next_out)
        next_out += len(blob)

    with dst.open("wb") as out:
        out.write(struct.pack("<II", magic, len(selected)))
        for off in new_offsets:
            out.write(struct.pack("<I", off))
        for _model_id, blob in selected:
            out.write(blob)
    print(f"wrote {dst}: {len(selected)} models")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="copy selected model entries into a smaller MDL2/MDL3 bundle"
    )
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--ids", required=True, help="comma-separated model IDs")
    args = parser.parse_args()

    ids = parse_ids(args.ids)
    if not ids:
        raise SystemExit("--ids must contain at least one model ID")
    subset_models(args.input, args.output, ids)
    copy_if_present(companion_path(args.input, ".atlas"), companion_path(args.output, ".atlas"))
    copy_if_present(companion_path(args.input, ".tanim"), companion_path(args.output, ".tanim"))


if __name__ == "__main__":
    main()
