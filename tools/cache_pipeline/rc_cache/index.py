"""RS2 reference-table parsing."""

from __future__ import annotations

import io
from dataclasses import dataclass, field

from .bytes import read_big_smart, read_i32, read_u8, read_u16, read_u32


@dataclass
class IndexManifest:
    protocol: int = 0
    revision: int = 0
    has_names: bool = False
    group_ids: list[int] = field(default_factory=list)
    group_name_hashes: dict[int, int] = field(default_factory=dict)
    group_crcs: dict[int, int] = field(default_factory=dict)
    group_revisions: dict[int, int] = field(default_factory=dict)
    group_file_ids: dict[int, list[int]] = field(default_factory=dict)
    group_file_name_hashes: dict[int, dict[int, int]] = field(default_factory=dict)
    group_compressed_lengths: dict[int, int] = field(default_factory=dict)
    group_uncompressed_lengths: dict[int, int] = field(default_factory=dict)
    group_uncompressed_crcs: dict[int, int] = field(default_factory=dict)
    group_whirlpool_digests: dict[int, bytes] = field(default_factory=dict)


def _read_manifest_count(buf: io.BytesIO, protocol: int) -> int:
    if protocol >= 7:
        return read_big_smart(buf)
    return read_u16(buf)


def parse_index_manifest(data: bytes) -> IndexManifest:
    """Parse a decompressed RS2 reference table."""
    buf = io.BytesIO(data)
    manifest = IndexManifest()

    manifest.protocol = read_u8(buf)
    if manifest.protocol < 5 or manifest.protocol > 7:
        msg = f"unsupported reference table protocol: {manifest.protocol}"
        raise ValueError(msg)

    if manifest.protocol >= 6:
        manifest.revision = read_u32(buf)

    flags = read_u8(buf)
    manifest.has_names = bool(flags & 0x01)
    has_whirlpool = bool(flags & 0x02)
    has_lengths = bool(flags & 0x04)
    has_uncompressed_crc = bool(flags & 0x08)

    group_count = _read_manifest_count(buf, manifest.protocol)

    accumulator = 0
    for _ in range(group_count):
        accumulator += _read_manifest_count(buf, manifest.protocol)
        manifest.group_ids.append(accumulator)

    if manifest.has_names:
        for gid in manifest.group_ids:
            manifest.group_name_hashes[gid] = read_i32(buf)

    for gid in manifest.group_ids:
        manifest.group_crcs[gid] = read_u32(buf)

    if has_whirlpool:
        for gid in manifest.group_ids:
            digest = buf.read(64)
            if len(digest) != 64:
                raise ValueError("truncated reference-table whirlpool digests")
            manifest.group_whirlpool_digests[gid] = digest

    if has_lengths:
        for gid in manifest.group_ids:
            manifest.group_compressed_lengths[gid] = read_u32(buf)
        for gid in manifest.group_ids:
            manifest.group_uncompressed_lengths[gid] = read_u32(buf)

    if has_uncompressed_crc:
        for gid in manifest.group_ids:
            manifest.group_uncompressed_crcs[gid] = read_u32(buf)

    for gid in manifest.group_ids:
        manifest.group_revisions[gid] = read_u32(buf)

    file_counts: dict[int, int] = {}
    for gid in manifest.group_ids:
        file_counts[gid] = _read_manifest_count(buf, manifest.protocol)

    for gid in manifest.group_ids:
        file_ids = []
        accumulator = 0
        for _ in range(file_counts[gid]):
            accumulator += _read_manifest_count(buf, manifest.protocol)
            file_ids.append(accumulator)
        manifest.group_file_ids[gid] = file_ids

    if manifest.has_names:
        for gid in manifest.group_ids:
            name_map: dict[int, int] = {}
            for fid in manifest.group_file_ids[gid]:
                name_map[fid] = read_i32(buf)
            manifest.group_file_name_hashes[gid] = name_map

    return manifest
