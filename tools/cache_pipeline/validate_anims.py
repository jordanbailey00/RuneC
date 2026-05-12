#!/usr/bin/env python3
"""Validate RuneC .anims files produced by export_animations.py."""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass, field
from pathlib import Path


LEGACY_MAGIC = 0x414E494D
ANM2_MAGIC = b"ANM2"
ANM2_VERSION = 2


@dataclass
class AnimSummary:
    version: int
    flags: int = 0
    framebase_count: int = 0
    sequence_count: int = 0
    sequence_frame_count: int = 0
    sequence_ids: set[int] = field(default_factory=set)


class Reader:
    def __init__(self, data: bytes, path: Path) -> None:
        self.data = data
        self.path = path
        self.off = 0

    def require(self, size: int) -> None:
        if self.off + size > len(self.data):
            raise ValueError(f"{self.path}: truncated at offset {self.off}")

    def read_u8(self) -> int:
        self.require(1)
        value = self.data[self.off]
        self.off += 1
        return value

    def read_u16(self) -> int:
        self.require(2)
        value = struct.unpack_from("<H", self.data, self.off)[0]
        self.off += 2
        return value

    def read_u32(self) -> int:
        self.require(4)
        value = struct.unpack_from("<I", self.data, self.off)[0]
        self.off += 4
        return value

    def skip(self, size: int) -> None:
        self.require(size)
        self.off += size


def parse_anims(path: Path) -> AnimSummary:
    r = Reader(path.read_bytes(), path)
    magic_bytes = r.data[:4]
    magic = r.read_u32()
    if magic_bytes == ANM2_MAGIC:
        version = r.read_u16()
        header_size = r.read_u16()
        if version != ANM2_VERSION or header_size < 24:
            raise ValueError(f"{path}: unsupported ANM2 header version={version} size={header_size}")
        summary = AnimSummary(
            version=version,
            framebase_count=r.read_u32(),
            sequence_count=r.read_u32(),
            sequence_frame_count=r.read_u32(),
            flags=r.read_u32(),
        )
        r.skip(header_size - 24)
    elif magic == LEGACY_MAGIC:
        summary = AnimSummary(
            version=1,
            framebase_count=r.read_u16(),
            sequence_count=r.read_u16(),
        )
    else:
        raise ValueError(f"{path}: bad magic bytes {magic_bytes!r}")

    for _ in range(summary.framebase_count):
        r.skip(2)
        slot_count = r.read_u8()
        r.skip(slot_count)
        for _ in range(slot_count):
            map_len = r.read_u8()
            r.skip(map_len)

    read_sequence_frames = 0
    for _ in range(summary.sequence_count):
        seq_id = r.read_u16()
        summary.sequence_ids.add(seq_id)
        frame_count = r.read_u16()
        interleave_count = r.read_u8()
        r.skip(interleave_count)
        r.skip(1)
        for _ in range(frame_count):
            r.skip(4)
            transform_count = r.read_u8()
            r.skip(transform_count * 7)
            read_sequence_frames += 1

    if summary.version == ANM2_VERSION and read_sequence_frames != summary.sequence_frame_count:
        raise ValueError(
            f"{path}: frame count mismatch header={summary.sequence_frame_count} "
            f"parsed={read_sequence_frames}"
        )
    if r.off != len(r.data):
        raise ValueError(f"{path}: {len(r.data) - r.off} trailing bytes")
    summary.sequence_frame_count = read_sequence_frames
    return summary


def main() -> None:
    parser = argparse.ArgumentParser(description="validate RuneC animation binary")
    parser.add_argument("file", type=Path)
    parser.add_argument("--expect-version", type=int)
    parser.add_argument("--require-seq", type=int, action="append", default=[])
    args = parser.parse_args()

    summary = parse_anims(args.file)
    if args.expect_version is not None and summary.version != args.expect_version:
        raise SystemExit(
            f"{args.file}: expected version {args.expect_version}, got {summary.version}"
        )

    missing = sorted(set(args.require_seq) - summary.sequence_ids)
    if missing:
        raise SystemExit(f"{args.file}: missing required sequences {missing}")

    print(
        f"{args.file}: ANM{summary.version} flags=0x{summary.flags:08X} "
        f"framebases={summary.framebase_count} sequences={summary.sequence_count} "
        f"sequence_frames={summary.sequence_frame_count}"
    )


if __name__ == "__main__":
    main()
