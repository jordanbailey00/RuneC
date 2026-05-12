"""Read OSRS cache in OpenRS2 flat file format (modern RS2 container format).

Handles decompression (none/bzip2/gzip), RS2 reference table parsing for
index manifests, and group splitting for multi-file groups. Works with
caches extracted by OpenRS2 into numbered directory/file structure.

Cache layout:
  cache/
    0/   - frame archives
    1/   - skeletons/framebases
    2/   - configs (groups: 6=obj/items, 12=seq/animations, 13=spotanim)
    7/   - models
    255/ - index manifests (255/<index_id>.dat)
"""

import bz2
import gzip
import io
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path


# --- binary reading helpers ---


def read_u8(buf: io.BytesIO) -> int:
    """Read unsigned 8-bit integer."""
    b = buf.read(1)
    if not b:
        return 0
    return b[0]


def read_u16(buf: io.BytesIO) -> int:
    """Read big-endian unsigned 16-bit integer."""
    b = buf.read(2)
    if len(b) < 2:
        return 0
    return struct.unpack(">H", b)[0]


def read_u32(buf: io.BytesIO) -> int:
    """Read big-endian unsigned 32-bit integer."""
    b = buf.read(4)
    if len(b) < 4:
        return 0
    return struct.unpack(">I", b)[0]


def read_i32(buf: io.BytesIO) -> int:
    """Read big-endian signed 32-bit integer."""
    b = buf.read(4)
    if len(b) < 4:
        return 0
    return struct.unpack(">i", b)[0]


def read_u24(buf: io.BytesIO) -> int:
    """Read big-endian unsigned 24-bit (medium) integer."""
    b = buf.read(3)
    if len(b) < 3:
        return 0
    return (b[0] << 16) | (b[1] << 8) | b[2]


def read_smart(buf: io.BytesIO) -> int:
    """Read RS2 smart value (1 or 2 bytes, unsigned).

    If first byte < 128: returns byte value.
    If first byte >= 128: reads 2 bytes, returns value - 32768.
    """
    pos = buf.tell()
    peek = buf.read(1)
    if not peek:
        return 0
    if peek[0] < 128:
        return peek[0]
    buf.seek(pos)
    return read_u16(buf) - 32768


def read_big_smart(buf: io.BytesIO) -> int:
    """Read RS2 big smart (2 or 4 bytes, unsigned).

    If first byte < 128 (sign bit clear): reads u16.
    Otherwise: reads i32 & 0x7FFFFFFF.
    """
    pos = buf.tell()
    peek = buf.read(1)
    if not peek:
        return 0
    buf.seek(pos)
    if peek[0] < 128:
        return read_u16(buf)
    return read_i32(buf) & 0x7FFFFFFF


def read_string(buf: io.BytesIO) -> str:
    """Read null-terminated string."""
    chars = []
    while True:
        b = buf.read(1)
        if not b or b[0] == 0:
            break
        chars.append(chr(b[0]))
    return "".join(chars)


# --- container decompression ---

COMPRESSION_NONE = 0
COMPRESSION_BZIP2 = 1
COMPRESSION_GZIP = 2


def decompress_container(data: bytes) -> bytes:
    """Decompress an RS2 container (.dat file).

    Container format:
      byte 0: compression type (0=none, 1=bzip2, 2=gzip)
      bytes 1-4: compressed length (big-endian u32)
      if compressed: bytes 5-8: decompressed length (big-endian u32)
      remaining: payload

    For bzip2: RS2 strips the 'BZ' magic header. We prepend 'BZh1' before
    feeding to the standard bz2 decompressor.
    """
    if len(data) < 5:
        msg = f"container too small: {len(data)} bytes"
        raise ValueError(msg)

    compression = data[0]
    compressed_len = struct.unpack(">I", data[1:5])[0]

    if compression == COMPRESSION_NONE:
        return data[5 : 5 + compressed_len]

    if len(data) < 9:
        msg = f"compressed container too small: {len(data)} bytes"
        raise ValueError(msg)

    decompressed_len = struct.unpack(">I", data[5:9])[0]
    payload = data[9 : 9 + compressed_len]

    if compression == COMPRESSION_BZIP2:
        # RS2 strips the full 4-byte bzip2 header ('BZh' + block size digit).
        # the payload starts directly at the block magic (31 41 59 26...).
        # prepend 'BZh1' to reconstruct a valid bzip2 stream.
        bz2_data = b"BZh1" + payload
        result = bz2.decompress(bz2_data)
    elif compression == COMPRESSION_GZIP:
        result = gzip.decompress(payload)
    else:
        msg = f"unknown compression type: {compression}"
        raise ValueError(msg)

    if len(result) != decompressed_len:
        msg = (
            f"decompressed size mismatch: expected {decompressed_len}, "
            f"got {len(result)}"
        )
        raise ValueError(msg)

    return result


# --- index manifest (RS2 reference table) ---


@dataclass
class IndexManifest:
    """Parsed RS2 reference table for a cache index.

    Contains metadata about all groups in the index: which file IDs each
    group contains, CRCs, revisions, and optional name hashes.
    """

    protocol: int = 0
    revision: int = 0
    has_names: bool = False
    group_ids: list[int] = field(default_factory=list)
    group_name_hashes: dict[int, int] = field(default_factory=dict)
    group_crcs: dict[int, int] = field(default_factory=dict)
    group_revisions: dict[int, int] = field(default_factory=dict)
    group_file_ids: dict[int, list[int]] = field(default_factory=dict)
    group_file_name_hashes: dict[int, dict[int, int]] = field(default_factory=dict)


def parse_index_manifest(data: bytes) -> IndexManifest:
    """Parse an RS2 reference table from decompressed container data.

    Reference table format (protocol 5-7):
      u8 protocol
      if protocol >= 6: u32 revision
      u8 flags:
        bit 0 = has names
        bit 1 = has whirlpool digests
        bit 2 = has lengths (compressed + decompressed sizes per group)
        bit 3 = has uncompressed CRC32s
      group count: big_smart if protocol >= 7, else u16
      group IDs: delta-encoded (big_smart if >= 7, else u16)
      if has_names: i32[group_count] name hashes
      u32[group_count] CRC32s
      if has_whirlpool: 64 bytes per group (whirlpool digests, skipped)
      if has_lengths: u32[group_count] compressed sizes, u32[group_count] decompressed sizes
      if has_uncompressed_crc: u32[group_count] uncompressed CRC32s
      u32[group_count] revisions
      file counts per group: big_smart if >= 7, else u16
      file IDs per group: delta-encoded (same width)
      if has_names: i32[total_files] file name hashes
    """
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

    # group count
    if manifest.protocol >= 7:
        group_count = read_big_smart(buf)
    else:
        group_count = read_u16(buf)

    # group IDs (delta-encoded)
    manifest.group_ids = []
    accumulator = 0
    for _ in range(group_count):
        if manifest.protocol >= 7:
            delta = read_big_smart(buf)
        else:
            delta = read_u16(buf)
        accumulator += delta
        manifest.group_ids.append(accumulator)

    # name hashes
    if manifest.has_names:
        for gid in manifest.group_ids:
            manifest.group_name_hashes[gid] = read_i32(buf)

    # CRC32s
    for gid in manifest.group_ids:
        manifest.group_crcs[gid] = read_u32(buf)

    # whirlpool digests (64 bytes each, skip)
    if has_whirlpool:
        buf.read(64 * group_count)

    # compressed + decompressed sizes per group (skip, metadata only)
    if has_lengths:
        buf.read(4 * group_count)  # compressed sizes
        buf.read(4 * group_count)  # decompressed sizes

    # uncompressed CRC32s (skip)
    if has_uncompressed_crc:
        buf.read(4 * group_count)

    # revisions
    for gid in manifest.group_ids:
        manifest.group_revisions[gid] = read_u32(buf)

    # file counts per group
    file_counts: dict[int, int] = {}
    for gid in manifest.group_ids:
        if manifest.protocol >= 7:
            file_counts[gid] = read_big_smart(buf)
        else:
            file_counts[gid] = read_u16(buf)

    # file IDs per group (delta-encoded)
    for gid in manifest.group_ids:
        count = file_counts[gid]
        file_ids = []
        acc = 0
        for _ in range(count):
            if manifest.protocol >= 7:
                delta = read_big_smart(buf)
            else:
                delta = read_u16(buf)
            acc += delta
            file_ids.append(acc)
        manifest.group_file_ids[gid] = file_ids

    # file name hashes
    if manifest.has_names:
        for gid in manifest.group_ids:
            fids = manifest.group_file_ids[gid]
            name_map: dict[int, int] = {}
            for fid in fids:
                name_map[fid] = read_i32(buf)
            manifest.group_file_name_hashes[gid] = name_map

    return manifest


# --- group splitting (multi-file groups) ---


def split_group(data: bytes, file_ids: list[int]) -> dict[int, bytes]:
    """Split a decompressed group into individual files.

    If the group has only one file, the entire data IS that file.

    Multi-file groups use a trailer format:
      - last byte of data = chunk_count
      - before that: chunk_count * file_count * 4 bytes of delta-encoded
        sizes (big-endian i32)
      - data region: chunks concatenated, each chunk has one segment per file
      - final file bytes = concatenation of that file's segment from each chunk

    Size encoding (matching RuneLite GroupDecompressor): for each chunk, iterate
    through files reading i32 deltas. A running accumulator (reset per chunk)
    tracks the current size. The accumulated value IS the segment size for that
    file in that chunk.
    """
    if len(file_ids) == 1:
        return {file_ids[0]: data}

    file_count = len(file_ids)

    # last byte = chunk count
    chunk_count = data[-1]
    if chunk_count < 1:
        msg = f"invalid chunk count: {chunk_count}"
        raise ValueError(msg)

    # read the size table from the trailer
    # size table: chunk_count * file_count * 4 bytes, right before the last byte
    size_table_len = chunk_count * file_count * 4
    size_table_start = len(data) - 1 - size_table_len
    if size_table_start < 0:
        msg = f"group data too small for {chunk_count} chunks x {file_count} files"
        raise ValueError(msg)

    size_buf = io.BytesIO(data[size_table_start : len(data) - 1])

    # parse delta-encoded sizes: for each chunk, accumulate deltas across files.
    # the accumulator resets to 0 at the start of each chunk. the accumulated
    # value is the segment size for that file in that chunk.
    chunk_sizes: list[list[int]] = []
    for _chunk in range(chunk_count):
        sizes = []
        chunk_size = 0
        for _f in range(file_count):
            delta = struct.unpack(">i", size_buf.read(4))[0]
            chunk_size += delta
            sizes.append(chunk_size)
        chunk_sizes.append(sizes)

    # extract file data by concatenating each file's segment from each chunk
    file_buffers: dict[int, bytearray] = {fid: bytearray() for fid in file_ids}
    offset = 0
    for chunk_idx in range(chunk_count):
        for f_idx, fid in enumerate(file_ids):
            size = chunk_sizes[chunk_idx][f_idx]
            file_buffers[fid].extend(data[offset : offset + size])
            offset += size

    return {fid: bytes(buf) for fid, buf in file_buffers.items()}


# --- main reader class ---


class ModernCacheReader:
    """Read OSRS cache in OpenRS2 flat file format.

    Expects a directory with numbered subdirectories (0/, 1/, 2/, ..., 255/)
    each containing .dat files named by group ID. Also accepts the Jagex
    disk-store layout (`main_file_cache.dat2` plus `main_file_cache.idx*`);
    the public read APIs are identical for both layouts.
    """

    def __init__(self, cache_dir: str | Path) -> None:
        """Initialize with path to cache root directory."""
        self.cache_dir = Path(cache_dir)
        if not self.cache_dir.is_dir():
            msg = f"cache directory not found: {self.cache_dir}"
            raise FileNotFoundError(msg)
        self._jagex_dat2 = self.cache_dir / "main_file_cache.dat2"
        self._jagex_layout = self._jagex_dat2.exists()
        self._manifest_cache: dict[int, IndexManifest] = {}

    def _read_raw(self, index_id: int, group_id: int) -> bytes | None:
        """Read raw container bytes from disk."""
        if self._jagex_layout:
            return self._read_jagex_raw(index_id, group_id)
        path = self.cache_dir / str(index_id) / f"{group_id}.dat"
        if not path.exists():
            return None
        return path.read_bytes()

    def _read_jagex_raw(self, index_id: int, group_id: int) -> bytes | None:
        """Read one archive container from a Jagex dat2/idx disk store.

        The idx file stores 6-byte entries: 3-byte archive length and 3-byte
        first sector. Each dat2 sector is 520 bytes. Small archive ids use an
        8-byte sector header; large archive ids use a 10-byte sector header.
        This mirrors the standard RuneLite/Jagex disk-store reader.
        """
        idx_path = self.cache_dir / f"main_file_cache.idx{index_id}"
        if not idx_path.exists():
            return None

        entry_offset = group_id * 6
        with idx_path.open("rb") as idx:
            idx.seek(0, 2)
            if idx.tell() < entry_offset + 6:
                return None
            idx.seek(entry_offset)
            entry = idx.read(6)

        if len(entry) != 6:
            return None
        size = (entry[0] << 16) | (entry[1] << 8) | entry[2]
        sector = (entry[3] << 16) | (entry[4] << 8) | entry[5]
        if size <= 0 or sector <= 0:
            return None

        header_len = 10 if group_id > 0xFFFF else 8
        payload_len = 520 - header_len
        data = bytearray()
        chunk = 0

        with self._jagex_dat2.open("rb") as dat:
            dat.seek(0, 2)
            dat_size = dat.tell()
            while len(data) < size:
                sector_offset = sector * 520
                if sector_offset + header_len > dat_size:
                    raise ValueError(
                        f"invalid dat2 sector {sector} for index={index_id} group={group_id}"
                    )
                dat.seek(sector_offset)
                header = dat.read(header_len)
                if len(header) != header_len:
                    raise ValueError(
                        f"short dat2 sector header for index={index_id} group={group_id}"
                    )

                if header_len == 10:
                    archive_id = (
                        (header[0] << 24)
                        | (header[1] << 16)
                        | (header[2] << 8)
                        | header[3]
                    )
                    chunk_id = (header[4] << 8) | header[5]
                    next_sector = (header[6] << 16) | (header[7] << 8) | header[8]
                    sector_index = header[9]
                else:
                    archive_id = (header[0] << 8) | header[1]
                    chunk_id = (header[2] << 8) | header[3]
                    next_sector = (header[4] << 16) | (header[5] << 8) | header[6]
                    sector_index = header[7]

                if archive_id != group_id or chunk_id != chunk or sector_index != index_id:
                    raise ValueError(
                        "corrupt dat2 sector chain "
                        f"index={index_id} group={group_id} sector={sector}"
                    )

                remaining = size - len(data)
                data.extend(dat.read(min(payload_len, remaining)))
                sector = next_sector
                chunk += 1

                if sector == 0 and len(data) < size:
                    raise ValueError(
                        f"truncated dat2 archive index={index_id} group={group_id}"
                    )

        return bytes(data)

    def read_container(self, index_id: int, group_id: int) -> bytes | None:
        """Read and decompress a container from the cache."""
        raw = self._read_raw(index_id, group_id)
        if raw is None:
            return None
        return decompress_container(raw)

    def read_index_manifest(self, index_id: int) -> IndexManifest:
        """Read and parse the reference table (manifest) for an index.

        The manifest for index N is stored at 255/N.dat.
        """
        if index_id in self._manifest_cache:
            return self._manifest_cache[index_id]

        data = self.read_container(255, index_id)
        if data is None:
            msg = f"manifest not found for index {index_id}"
            raise FileNotFoundError(msg)

        manifest = parse_index_manifest(data)
        self._manifest_cache[index_id] = manifest
        return manifest

    def read_group(self, index_id: int, group_id: int) -> dict[int, bytes]:
        """Read a group and split into individual file entries.

        Returns dict mapping file_id -> decompressed file bytes.
        """
        manifest = self.read_index_manifest(index_id)

        if group_id not in manifest.group_file_ids:
            msg = f"group {group_id} not in index {index_id} manifest"
            raise KeyError(msg)

        data = self.read_container(index_id, group_id)
        if data is None:
            msg = f"group data not found: index={index_id} group={group_id}"
            raise FileNotFoundError(msg)

        file_ids = manifest.group_file_ids[group_id]
        return split_group(data, file_ids)

    def read_config_entry(self, group_id: int, entry_id: int) -> bytes:
        """Read a single config entry from index 2.

        Convenience method: configs live in index 2, where group_id selects
        the config type (6=obj/items, 12=seq/animations, 13=spotanim) and
        entry_id selects the specific entry.
        """
        files = self.read_group(2, group_id)
        if entry_id not in files:
            msg = f"config entry {entry_id} not found in group {group_id}"
            raise KeyError(msg)
        return files[entry_id]


# --- sequence parsing (modern OSRS cache format, rev226+) ---


def _read_frame_sound_rev226(buf: io.BytesIO) -> None:
    """Skip a frame sound entry (rev226 format with rev220FrameSounds).

    Format: u16 id, u8 weight, u8 loops, u8 location, u8 retain.
    """
    read_u16(buf)   # sound id
    read_u8(buf)    # weight
    read_u8(buf)    # loops
    read_u8(buf)    # location
    read_u8(buf)    # retain


@dataclass
class SequenceDef:
    """Animation sequence definition parsed from modern OSRS cache.

    Field names match RuneLite's SequenceDefinition. Opcode mapping is for
    the modern (rev226+) format used by current OSRS.
    """

    seq_id: int = 0
    frame_count: int = 0
    frame_delays: list[int] = field(default_factory=list)
    primary_frame_ids: list[int] = field(default_factory=list)
    frame_step: int = -1
    interleave_order: list[int] = field(default_factory=list)
    stretches: bool = False
    forced_priority: int = 5
    left_hand_item: int = -1
    right_hand_item: int = -1
    max_loops: int = 99
    precedence_animating: int = -1
    priority: int = -1
    reply_mode: int = -1


def parse_sequence(seq_id: int, data: bytes) -> SequenceDef:
    """Parse a single sequence from opcode stream (modern OSRS, rev226+).

    Opcode layout from RuneLite's SequenceLoader.decodeValues with rev226=true
    and rev220FrameSounds=true (modern OSRS cache revisions are well above
    both thresholds of 1141 and 1268).

    Opcode map (rev226=true):
      1:  frame data (delays, file IDs, group IDs)
      2:  frame step
      3:  interleave order
      4:  stretches flag
      5:  forced priority
      6:  left hand item
      7:  right hand item
      8:  max loops
      9:  precedence animating
      10: priority
      11: reply mode
      12: chat frame IDs
      13: animMayaID (i32) — rev226 remaps this from old opcode 14
      14: frame sounds (u16 count) — rev226 remaps this from old opcode 15
      15: animMayaStart/End — rev226 remaps this from old opcode 16
      16: vertical offset (i8)
      17: animMayaMasks
      18: debug name (string)
      19: sounds cross world view flag
    """
    seq = SequenceDef(seq_id=seq_id)
    buf = io.BytesIO(data)

    while True:
        opcode = read_u8(buf)
        if opcode == 0:
            break
        elif opcode == 1:
            seq.frame_count = read_u16(buf)
            seq.frame_delays = [read_u16(buf) for _ in range(seq.frame_count)]
            file_ids = [read_u16(buf) for _ in range(seq.frame_count)]
            group_ids = [read_u16(buf) for _ in range(seq.frame_count)]
            seq.primary_frame_ids = [
                (group_ids[i] << 16) | file_ids[i] for i in range(seq.frame_count)
            ]
        elif opcode == 2:
            seq.frame_step = read_u16(buf)
        elif opcode == 3:
            n = read_u8(buf)
            seq.interleave_order = [read_u8(buf) for _ in range(n)]
        elif opcode == 4:
            seq.stretches = True
        elif opcode == 5:
            seq.forced_priority = read_u8(buf)
        elif opcode == 6:
            seq.left_hand_item = read_u16(buf)
        elif opcode == 7:
            seq.right_hand_item = read_u16(buf)
        elif opcode == 8:
            seq.max_loops = read_u8(buf)
        elif opcode == 9:
            seq.precedence_animating = read_u8(buf)
        elif opcode == 10:
            seq.priority = read_u8(buf)
        elif opcode == 11:
            seq.reply_mode = read_u8(buf)
        elif opcode == 12:
            # chat frame IDs: u8 count, then u16[count] + u16[count] (low + high)
            n = read_u8(buf)
            for _ in range(n):
                read_u16(buf)
            for _ in range(n):
                read_u16(buf)
        elif opcode == 13:
            # rev226: animMayaID (remapped from old opcode 14)
            read_i32(buf)
        elif opcode == 14:
            # rev226: frame sounds (remapped from old opcode 15)
            n = read_u16(buf)
            for _ in range(n):
                read_u16(buf)  # frame index
                _read_frame_sound_rev226(buf)
        elif opcode == 15:
            # rev226: animMayaStart + animMayaEnd (remapped from old opcode 16)
            read_u16(buf)
            read_u16(buf)
        elif opcode == 16:
            # vertical offset (signed byte)
            read_u8(buf)
        elif opcode == 17:
            # animMayaMasks: u8 count, then u8[count] mask indices
            n = read_u8(buf)
            for _ in range(n):
                read_u8(buf)
        elif opcode == 18:
            # debug name (null-terminated string)
            read_string(buf)
        elif opcode == 19:
            pass  # soundsCrossWorldView = true
        else:
            print(
                f"  warning: unknown seq opcode {opcode} for id {seq_id}",
                file=sys.stderr,
            )
            break

    if seq.frame_count == 0:
        seq.frame_count = 1
        seq.primary_frame_ids = [-1]
        seq.frame_delays = [-1]

    return seq


# --- test ---


def main() -> None:
    """Test the modern cache reader against a local cache."""
    cache_path = "../reference/osrs-cache-modern/"
    print(f"opening cache at {cache_path}")

    reader = ModernCacheReader(cache_path)

    # parse index 2 manifest (configs)
    print("\nparsing index 2 (configs) manifest...")
    manifest = reader.read_index_manifest(2)
    print(f"  protocol: {manifest.protocol}")
    print(f"  revision: {manifest.revision}")
    print(f"  has_names: {manifest.has_names}")
    print(f"  total groups: {len(manifest.group_ids)}")

    # report key config groups
    key_groups = {6: "obj/items", 12: "seq/animations", 13: "spotanim"}
    for gid, name in key_groups.items():
        if gid in manifest.group_file_ids:
            file_count = len(manifest.group_file_ids[gid])
            print(f"  group {gid} ({name}): {file_count} entries")
        else:
            print(f"  group {gid} ({name}): NOT FOUND")

    # read and parse sequences from group 12
    print("\nreading seq group 12...")
    seq_files = reader.read_group(2, 12)
    print(f"  loaded {len(seq_files)} sequence entries")

    # parse known sequences
    test_seqs = {808: "idle", 1979: "cast_barrage"}
    for seq_id, name in test_seqs.items():
        if seq_id not in seq_files:
            print(f"  seq {seq_id} ({name}): NOT FOUND in group")
            continue
        seq = parse_sequence(seq_id, seq_files[seq_id])
        print(
            f"  seq {seq_id} ({name}): "
            f"frames={seq.frame_count}, "
            f"priority={seq.priority}, "
            f"forced_priority={seq.forced_priority}, "
            f"precedence_animating={seq.precedence_animating}, "
            f"delays={seq.frame_delays[:5]}{'...' if len(seq.frame_delays) > 5 else ''}"
        )

    # also check a spotanim entry from group 13
    print("\nreading spotanim group 13...")
    spotanim_files = reader.read_group(2, 13)
    print(f"  loaded {len(spotanim_files)} spotanim entries")

    # check a few obj entries from group 6
    print("\nreading obj group 6 (sampling first 3 entries)...")
    obj_files = reader.read_group(2, 6)
    print(f"  loaded {len(obj_files)} obj/item entries")
    sample_ids = sorted(obj_files.keys())[:3]
    for oid in sample_ids:
        print(f"  obj {oid}: {len(obj_files[oid])} bytes")


if __name__ == "__main__":
    main()
