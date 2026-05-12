"""Animation framebase and frame decoders for modern OSRS caches."""

from __future__ import annotations

import io
from dataclasses import dataclass, field

from .bytes import read_u8, read_u16, read_u24
from .config import INDEX_FRAMES, INDEX_SKELETONS
from .store import RcCacheStore


@dataclass
class FrameBaseDef:
    """Transform slot layout used by one or more animation frames."""

    base_id: int = 0
    slot_count: int = 0
    types: list[int] = field(default_factory=list)
    frame_maps: list[list[int]] = field(default_factory=list)


@dataclass
class FrameDef:
    """Single normal animation frame referencing a framebase."""

    group_id: int = 0
    file_id: int = 0
    framebase_id: int = 0
    translator_count: int = 0
    slot_indices: list[int] = field(default_factory=list)
    dx: list[int] = field(default_factory=list)
    dy: list[int] = field(default_factory=list)
    dz: list[int] = field(default_factory=list)


def read_short_smart(buf: io.BytesIO) -> int:
    """Read the signed short-smart transform delta used by normal frames."""
    pos = buf.tell()
    peek = buf.read(1)
    if not peek:
        return 0
    if peek[0] < 128:
        return peek[0] - 64
    buf.seek(pos)
    return read_u16(buf) - 0xC000


def decode_framebase(base_id: int, data: bytes) -> FrameBaseDef:
    """Decode one modern framebase/skeleton payload."""
    buf = io.BytesIO(data)
    slot_count = read_u8(buf)
    types = [read_u8(buf) for _ in range(slot_count)]
    map_lengths = [read_u8(buf) for _ in range(slot_count)]
    frame_maps = [[read_u8(buf) for _ in range(length)] for length in map_lengths]
    return FrameBaseDef(
        base_id=base_id,
        slot_count=slot_count,
        types=types,
        frame_maps=frame_maps,
    )


def decode_frame(
    group_id: int,
    file_id: int,
    data: bytes,
    framebases: dict[int, FrameBaseDef],
) -> FrameDef | None:
    """Decode one normal frame payload from cache index 0."""
    if len(data) < 3:
        return None
    fbuf = io.BytesIO(data)
    framebase_id = read_u16(fbuf)
    slot_count = read_u8(fbuf)
    framebase = framebases.get(framebase_id)
    if framebase is None:
        return None

    attr_start = fbuf.tell()
    attributes = [read_u8(fbuf) for _ in range(slot_count)]
    dbuf = io.BytesIO(data)
    dbuf.seek(attr_start + slot_count)

    slot_indices: list[int] = []
    dx: list[int] = []
    dy: list[int] = []
    dz: list[int] = []
    last_i = -1

    for i, attr in enumerate(attributes):
        if attr <= 0:
            continue

        slot_type = framebase.types[i] if i < len(framebase.types) else 0
        if slot_type != 0:
            for j in range(i - 1, last_i, -1):
                if j < len(framebase.types) and framebase.types[j] == 0:
                    slot_indices.append(j)
                    dx.append(0)
                    dy.append(0)
                    dz.append(0)
                    break

        slot_indices.append(i)
        default_value = 128 if slot_type == 3 else 0
        dx.append(read_short_smart(dbuf) if attr & 1 else default_value)
        dy.append(read_short_smart(dbuf) if attr & 2 else default_value)
        dz.append(read_short_smart(dbuf) if attr & 4 else default_value)
        last_i = i

    return FrameDef(
        group_id=group_id,
        file_id=file_id,
        framebase_id=framebase_id,
        translator_count=len(slot_indices),
        slot_indices=slot_indices,
        dx=dx,
        dy=dy,
        dz=dz,
    )


def decode_legacy_framebase_archive(data: bytes) -> dict[int, FrameBaseDef]:
    """Decode the old packed framebases.dat format kept for legacy exports."""
    buf = io.BytesIO(data)
    count = read_u16(buf)
    framebases: dict[int, FrameBaseDef] = {}
    for _ in range(count):
        base_id = read_u16(buf)
        file_size = read_u16(buf)
        framebases[base_id] = decode_framebase(base_id, buf.read(file_size))
    return framebases


def decode_legacy_frame_archive(
    group_id: int,
    data: bytes,
    framebases: dict[int, FrameBaseDef],
) -> dict[int, FrameDef]:
    """Decode the old packed frame archive format kept for legacy exports."""
    buf = io.BytesIO(data)
    highest_file_id = read_u16(buf)
    frames: dict[int, FrameDef] = {}
    for _ in range(highest_file_id + 1):
        if buf.tell() >= len(data):
            break
        file_id = read_u16(buf)
        file_size = read_u24(buf)
        if file_size <= 0 or buf.tell() + file_size > len(data):
            break
        frame = decode_frame(group_id, file_id, buf.read(file_size), framebases)
        if frame is not None:
            frames[file_id] = frame
        if file_id >= highest_file_id:
            break
    return frames


def discover_framebase_ids(raw_frame_data: dict[int, dict[int, bytes]]) -> set[int]:
    base_ids: set[int] = set()
    for files in raw_frame_data.values():
        for data in files.values():
            if len(data) >= 2:
                base_ids.add((data[0] << 8) | data[1])
    return base_ids


def load_framebases(
    store: RcCacheStore,
    base_ids: set[int],
) -> tuple[dict[int, FrameBaseDef], list[int]]:
    framebases: dict[int, FrameBaseDef] = {}
    missing: list[int] = []
    for base_id in sorted(base_ids):
        data = store.read_container(INDEX_SKELETONS, base_id)
        if data is None:
            missing.append(base_id)
            continue
        framebases[base_id] = decode_framebase(base_id, data)
    return framebases, missing


def read_frame_archive_files(
    store: RcCacheStore,
    group_ids: set[int],
) -> tuple[dict[int, dict[int, bytes]], list[int]]:
    raw_frame_data: dict[int, dict[int, bytes]] = {}
    missing: list[int] = []
    for group_id in sorted(group_ids):
        try:
            raw_frame_data[group_id] = store.read_group(INDEX_FRAMES, group_id)
        except (KeyError, FileNotFoundError):
            missing.append(group_id)
    return raw_frame_data, missing


def load_required_frames(
    store: RcCacheStore,
    primary_frame_ids: set[int],
) -> tuple[dict[int, FrameBaseDef], dict[int, dict[int, FrameDef]], list[int], list[int]]:
    """Load framebases and normal frames referenced by sequence frame ids."""
    group_ids = {frame_id >> 16 for frame_id in primary_frame_ids if frame_id >= 0}
    raw_frame_data, missing_groups = read_frame_archive_files(store, group_ids)
    base_ids = discover_framebase_ids(raw_frame_data)
    framebases, missing_bases = load_framebases(store, base_ids)

    all_frames: dict[int, dict[int, FrameDef]] = {}
    for group_id, files in raw_frame_data.items():
        frames: dict[int, FrameDef] = {}
        for file_id, data in files.items():
            frame = decode_frame(group_id, file_id, data, framebases)
            if frame is not None:
                frames[file_id] = frame
        if frames:
            all_frames[group_id] = frames

    return framebases, all_frames, missing_groups, missing_bases
