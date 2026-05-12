"""Export OSRS animation data from cache to a binary .anims file.

Supports both 317-format (tarnish) and modern OpenRS2 flat file caches.
Reads framebases, frame archives, and sequence (animation) definitions.
Outputs a compact binary consumable by osrs_pvp_anim.h.

317 cache sources:
  - framebases.dat: from config archive (index 0, file 2)
  - seq.dat: from config archive
  - frame archives: from cache index 2

Modern cache sources:
  - frame bases: index 1 (each group is a framebase)
  - sequences: config index 2, group 12
  - frame archives: index 0

Usage (317 cache):
    uv run python scripts/export_animations.py \
        --cache ../reference/tarnish/game-server/data/cache \
        --output data/equipment.anims

Usage (modern cache):
    uv run python scripts/export_animations.py \
        --modern-cache ../reference/osrs-cache-modern \
        --output data/equipment.anims
"""

import argparse
import gzip
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
from modern_cache_reader import ModernCacheReader
from rc_cache import (
    CONFIG_SEQUENCE,
    RcCacheStore,
    decode_sequence_definition,
    load_required_frames,
    read_config_group,
)

CONFIG_ARCHIVE = 2
FRAME_CACHE_INDEX = 2  # cache index for animation frame archives (317)
MODERN_FRAME_INDEX = 0  # modern cache: frame archives
MODERN_FRAMEBASE_INDEX = 1  # modern cache: frame bases
SPOTANIM_BIN_MAGIC = 0x544F5053
OBJECT_ANIM_BIN_MAGIC = 0x4D4E414F


# --- binary reading helpers ---


def read_ubyte(buf: io.BytesIO) -> int:
    """Read unsigned byte from stream."""
    b = buf.read(1)
    if not b:
        return 0
    return b[0]


def read_ushort(buf: io.BytesIO) -> int:
    """Read big-endian unsigned short from stream."""
    b = buf.read(2)
    if len(b) < 2:
        return 0
    return (b[0] << 8) | b[1]


def read_short_smart(buf: io.BytesIO) -> int:
    """Read signed short smart (same as Java Buffer.readShortSmart).

    Single byte (peek < 128): value - 64 (range -64 to 63)
    Two bytes:                value - 0xC000 (range -16384 to 16383)
    """
    pos = buf.tell()
    peek = buf.read(1)
    if not peek:
        return 0
    val = peek[0]
    if val < 128:
        return val - 64
    buf.seek(pos)
    raw = struct.unpack(">H", buf.read(2))[0]
    return raw - 0xC000


def read_medium(buf: io.BytesIO) -> int:
    """Read 3-byte big-endian medium int."""
    b = buf.read(3)
    if len(b) < 3:
        return 0
    return (b[0] << 16) | (b[1] << 8) | b[2]


def read_int(buf: io.BytesIO) -> int:
    """Read big-endian signed 32-bit int."""
    b = buf.read(4)
    if len(b) < 4:
        return 0
    val = (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]
    if val >= 0x80000000:
        val -= 0x100000000
    return val


# --- data structures ---


@dataclass
class FrameBaseDef:
    """Transform slot layout — defines which vertex groups each slot operates on.

    Each slot has a type (0=origin, 1=translate, 2=rotate, 3=scale, 5=alpha)
    and a list of vertex group label indices (frameMaps).
    """

    base_id: int = 0
    slot_count: int = 0
    types: list[int] = field(default_factory=list)
    frame_maps: list[list[int]] = field(default_factory=list)


@dataclass
class FrameDef:
    """Single animation frame — a list of transforms to apply.

    Each entry references a slot in the FrameBase and provides dx/dy/dz values.
    Origin slots (type 0) auto-inserted before non-origin transforms.
    """

    framebase_id: int = 0
    translator_count: int = 0
    slot_indices: list[int] = field(default_factory=list)
    dx: list[int] = field(default_factory=list)
    dy: list[int] = field(default_factory=list)
    dz: list[int] = field(default_factory=list)


@dataclass
class SequenceDef:
    """Animation sequence — ordered frames with timing and blend metadata.

    primaryFrameIds encode (groupId << 16 | fileId) for cache frame lookup.
    interleaveOrder defines which slots come from secondary (idle/walk) animation.
    """

    seq_id: int = 0
    frame_count: int = 0
    frame_delays: list[int] = field(default_factory=list)
    primary_frame_ids: list[int] = field(default_factory=list)
    frame_step: int = -1
    interleave_order: list[int] = field(default_factory=list)
    priority: int = 5
    loop_count: int = 99
    walk_flag: int = -1  # opcode 10: 0=stall movement, -1=default (derive from interleave)
    run_flag: int = -1   # opcode 9: 0=stall pre-anim steps, -1=default


# --- parsing ---


def parse_framebases(data: bytes) -> dict[int, FrameBaseDef]:
    """Parse framebases.dat from config archive.

    Format: u16 count, then for each: u16 id, u16 size, [size bytes of framebase data].
    """
    buf = io.BytesIO(data)
    count = read_ushort(buf)
    bases: dict[int, FrameBaseDef] = {}

    for _ in range(count):
        base_id = read_ushort(buf)
        file_size = read_ushort(buf)
        file_data = buf.read(file_size)

        fb = FrameBaseDef(base_id=base_id)
        fbuf = io.BytesIO(file_data)

        fb.slot_count = read_ubyte(fbuf)
        fb.types = [read_ubyte(fbuf) for _ in range(fb.slot_count)]

        map_lengths = [read_ubyte(fbuf) for _ in range(fb.slot_count)]
        fb.frame_maps = []
        for length in map_lengths:
            fb.frame_maps.append([read_ubyte(fbuf) for _ in range(length)])

        bases[base_id] = fb

    return bases


def parse_frame_archive(
    group_id: int, data: bytes, framebases: dict[int, FrameBaseDef],
) -> dict[int, FrameDef]:
    """Parse a frame archive from cache index 2.

    Format: u16 highestFileId, then for each: u16 fileId, u24 fileSize, [fileSize bytes].
    Each frame is a NormalFrame referencing a FrameBase.
    """
    buf = io.BytesIO(data)
    highest_file_id = read_ushort(buf)
    frames: dict[int, FrameDef] = {}

    for _ in range(highest_file_id + 1):
        if buf.tell() >= len(data):
            break

        file_id = read_ushort(buf)
        file_size = read_medium(buf)

        if file_size <= 0 or buf.tell() + file_size > len(data):
            break

        file_data = buf.read(file_size)
        frame = _parse_normal_frame(group_id, file_id, file_data, framebases)
        if frame is not None:
            frames[file_id] = frame

        if file_id >= highest_file_id:
            break

    return frames


def _parse_normal_frame(
    group_id: int,
    file_id: int,
    data: bytes,
    framebases: dict[int, FrameBaseDef],
) -> FrameDef | None:
    """Parse a NormalFrame from raw bytes. Mirrors Java NormalFrame constructor."""
    fbuf = io.BytesIO(data)

    framebase_id = read_ushort(fbuf)
    slot_count = read_ubyte(fbuf)

    fb = framebases.get(framebase_id)
    if fb is None:
        return None

    types = fb.types

    # read attribute bytes (one per slot)
    attr_start = fbuf.tell()
    attributes = [read_ubyte(fbuf) for _ in range(slot_count)]

    # data stream starts after attributes
    dbuf = io.BytesIO(data)
    dbuf.seek(slot_count + attr_start)

    slot_indices: list[int] = []
    dx_list: list[int] = []
    dy_list: list[int] = []
    dz_list: list[int] = []

    last_i = -1
    for i in range(slot_count):
        attr = attributes[i]
        if attr <= 0:
            continue

        # get the slot type from the framebase
        slot_type = types[i] if i < len(types) else 0

        # auto-insert preceding origin slot (type 0) if this slot isn't an origin
        if slot_type != 0:
            for j in range(i - 1, last_i, -1):
                if j < len(types) and types[j] == 0:
                    slot_indices.append(j)
                    dx_list.append(0)
                    dy_list.append(0)
                    dz_list.append(0)
                    break

        slot_indices.append(i)

        default_val = 128 if slot_type == 3 else 0
        dx_list.append(read_short_smart(dbuf) if (attr & 1) else default_val)
        dy_list.append(read_short_smart(dbuf) if (attr & 2) else default_val)
        dz_list.append(read_short_smart(dbuf) if (attr & 4) else default_val)

        last_i = i

    frame = FrameDef(
        framebase_id=framebase_id,
        translator_count=len(slot_indices),
        slot_indices=slot_indices,
        dx=dx_list,
        dy=dy_list,
        dz=dz_list,
    )
    return frame


def parse_sequences(data: bytes) -> dict[int, SequenceDef]:
    """Parse seq.dat from config archive. Mirrors Java Animation.unpackConfig."""
    buf = io.BytesIO(data)
    highest_file_id = read_ushort(buf)
    seqs: dict[int, SequenceDef] = {}

    for _ in range(highest_file_id):
        if buf.tell() >= len(data):
            break

        seq_id = read_ushort(buf)
        if seq_id == 0xFFFF or seq_id >= 32767:
            continue

        anim_length = read_ushort(buf)
        anim_data = buf.read(anim_length)

        seq = _parse_sequence(seq_id, anim_data)
        if seq is not None:
            seqs[seq_id] = seq

        if seq_id >= highest_file_id:
            break

    return seqs


def _parse_sequence(seq_id: int, data: bytes) -> SequenceDef | None:
    """Parse a single animation sequence from opcode stream."""
    seq = SequenceDef(seq_id=seq_id)
    buf = io.BytesIO(data)

    while True:
        opcode = read_ubyte(buf)
        if opcode == 0:
            break
        elif opcode == 1:
            seq.frame_count = read_ushort(buf)
            seq.frame_delays = [read_ushort(buf) for _ in range(seq.frame_count)]
            file_ids = [read_ushort(buf) for _ in range(seq.frame_count)]
            group_ids = [read_ushort(buf) for _ in range(seq.frame_count)]
            seq.primary_frame_ids = [
                (group_ids[i] << 16) | file_ids[i] for i in range(seq.frame_count)
            ]
        elif opcode == 2:
            seq.frame_step = read_ushort(buf)
        elif opcode == 3:
            n = read_ubyte(buf)
            seq.interleave_order = [read_ubyte(buf) for _ in range(n)]
        elif opcode == 4:
            pass  # allowsRotation = true
        elif opcode == 5:
            seq.priority = read_ubyte(buf)
        elif opcode == 6:
            read_ushort(buf)  # shield
        elif opcode == 7:
            read_ushort(buf)  # weapon
        elif opcode == 8:
            seq.loop_count = read_ubyte(buf)
        elif opcode == 9:
            seq.run_flag = read_ubyte(buf)
        elif opcode == 10:
            seq.walk_flag = read_ubyte(buf)
        elif opcode == 11:
            read_ubyte(buf)  # type
        elif opcode == 12:
            n = read_ubyte(buf)
            for _ in range(n):
                read_ushort(buf)
            for _ in range(n):
                read_ushort(buf)
        elif opcode == 13:
            n = read_ubyte(buf)
            for _ in range(n):
                read_medium(buf)
        elif opcode == 14:
            read_int(buf)  # skeletalFrameId
        elif opcode == 15:
            n = read_ushort(buf)
            for _ in range(n):
                read_ushort(buf)
                read_medium(buf)
        elif opcode == 16:
            read_ushort(buf)  # rangeBegin
            read_ushort(buf)  # rangeEnd
        elif opcode == 17:
            n = read_ubyte(buf)
            for _ in range(n):
                read_ubyte(buf)
        else:
            print(f"  warning: unknown seq opcode {opcode} for id {seq_id}", file=sys.stderr)
            break

    if seq.frame_count == 0:
        seq.frame_count = 1
        seq.primary_frame_ids = [-1]
        seq.frame_delays = [-1]

    return seq


# --- binary output ---


ANIM_MAGIC = 0x414E494D  # legacy v1 wrote this little-endian, producing bytes "MINA"
ANIM2_MAGIC = b"ANM2"
ANIM_FORMAT_VERSION = 2
ANIM_HEADER_SIZE = 24
ANIM_FLAG_NORMAL_FRAMES = 1 << 0
ANIM_FLAG_PRESENTATION_METADATA_OMITTED = 1 << 1


@dataclass
class AnimationWriteStats:
    framebase_count: int = 0
    sequence_count: int = 0
    sequence_frame_count: int = 0
    skipped_missing_frames: list[int] = field(default_factory=list)


def _as_i8(value: int) -> int:
    return max(-128, min(127, int(value)))


def write_animations_binary(
    output_path: Path,
    framebases: dict[int, FrameBaseDef],
    all_frames: dict[int, dict[int, FrameDef]],
    sequences: dict[int, SequenceDef],
    needed_seq_ids: set[int],
) -> AnimationWriteStats:
    """Write animation data to .anims binary format.

    Only exports sequences in needed_seq_ids and their referenced framebases/frames.

    Binary layout:
      header:
        char[4] magic ("ANM2")
        uint16  version (=2)
        uint16  header_size
        uint32  framebase_count
        uint32  sequence_count
        uint32  sequence_frame_count
        uint32  flags

      framebases section (sorted by id):
        for each framebase:
          uint16 base_id
          uint8  slot_count
          uint8[slot_count] types
          for each slot:
            uint8 map_length
            uint8[map_length] frame_map entries

      sequences section:
        for each sequence:
          uint16 seq_id
          uint16 frame_count
          uint8  interleave_count (0 if none)
          uint8[interleave_count] interleave_order
          int8   walk_flag (-1=default, 0=stall movement during anim)
          for each frame in sequence:
            uint16 delay (game ticks)
            uint16 framebase_id
            uint8  translator_count
            for each translator:
              uint8  slot_index
              int16  dx
              int16  dy
              int16  dz
    """
    output_path.parent.mkdir(parents=True, exist_ok=True)

    # collect needed framebases from sequences
    needed_bases: set[int] = set()
    valid_seqs: list[SequenceDef] = []
    stats = AnimationWriteStats()
    for seq_id in sorted(needed_seq_ids):
        seq = sequences.get(seq_id)
        if seq is None:
            continue

        # check all frames exist
        has_frames = True
        seq_bases: set[int] = set()
        for fid in seq.primary_frame_ids:
            if fid == -1:
                continue
            group_id = fid >> 16
            file_id = fid & 0xFFFF
            group = all_frames.get(group_id)
            if group is None or file_id not in group:
                has_frames = False
                break
            seq_bases.add(group[file_id].framebase_id)

        if has_frames:
            needed_bases.update(seq_bases)
            valid_seqs.append(seq)
        else:
            stats.skipped_missing_frames.append(seq_id)

    # remap framebase IDs to compact indices
    sorted_bases = sorted(needed_bases)
    sequence_frame_count = sum(seq.frame_count for seq in valid_seqs)
    stats.framebase_count = len(sorted_bases)
    stats.sequence_count = len(valid_seqs)
    stats.sequence_frame_count = sequence_frame_count

    with open(output_path, "wb") as f:
        f.write(ANIM2_MAGIC)
        f.write(struct.pack("<HHIIII",
                            ANIM_FORMAT_VERSION,
                            ANIM_HEADER_SIZE,
                            len(sorted_bases),
                            len(valid_seqs),
                            sequence_frame_count,
                            ANIM_FLAG_NORMAL_FRAMES
                            | ANIM_FLAG_PRESENTATION_METADATA_OMITTED))

        # framebases section
        for base_id in sorted_bases:
            fb = framebases[base_id]
            f.write(struct.pack("<H", base_id))
            f.write(struct.pack("B", fb.slot_count))
            for t in fb.types:
                f.write(struct.pack("B", t))
            for fmap in fb.frame_maps:
                f.write(struct.pack("B", len(fmap)))
                for entry in fmap:
                    f.write(struct.pack("B", entry))

        # sequences section
        for seq in valid_seqs:
            f.write(struct.pack("<H", seq.seq_id))
            f.write(struct.pack("<H", seq.frame_count))

            il = seq.interleave_order
            f.write(struct.pack("B", len(il)))
            for v in il:
                f.write(struct.pack("B", v))

            # walk_flag: signed int8 (-1=default/no stall, 0=stall movement)
            # matches Animation.java walkFlag from opcode 10
            f.write(struct.pack("b", _as_i8(seq.walk_flag)))

            for i in range(seq.frame_count):
                delay = seq.frame_delays[i]
                f.write(struct.pack("<H", max(0, delay)))

                fid = seq.primary_frame_ids[i]
                if fid == -1:
                    # empty frame
                    f.write(struct.pack("<H", 0xFFFF))
                    f.write(struct.pack("B", 0))
                    continue

                group_id = fid >> 16
                file_id = fid & 0xFFFF
                frame = all_frames[group_id][file_id]

                f.write(struct.pack("<H", frame.framebase_id))
                f.write(struct.pack("B", frame.translator_count))

                for j in range(frame.translator_count):
                    f.write(struct.pack("B", frame.slot_indices[j]))
                    f.write(struct.pack("<h", frame.dx[j]))
                    f.write(struct.pack("<h", frame.dy[j]))
                    f.write(struct.pack("<h", frame.dz[j]))

    file_size = output_path.stat().st_size
    print(f"\nwrote {file_size:,} bytes to {output_path}")
    print(
        f"  ANM2 v{ANIM_FORMAT_VERSION}: {len(sorted_bases)} framebases, "
        f"{len(valid_seqs)} sequences, {sequence_frame_count} sequence frames"
    )
    if stats.skipped_missing_frames:
        print(
            f"  warning: skipped {len(stats.skipped_missing_frames)} sequences "
            "with missing frame data"
        )
    return stats


# --- known animation IDs for PvP simulation ---

# player default animations
ANIM_IDLE = 808
ANIM_WALK = 819
ANIM_RUN = 824
ANIM_TURN_180 = 820
ANIM_TURN_CW = 821
ANIM_TURN_CCW = 822

# attack animations by weapon (from OSRS wiki)
ANIM_WHIP_ATTACK = 1658
ANIM_WHIP_SPEC = 1658
ANIM_STAFF_BASH = 393
ANIM_STAFF_SLAM = 414
ANIM_DAGGER_STAB = 376
ANIM_DAGGER_SPEC = 1062
ANIM_CROSSBOW_SHOOT = 4230
ANIM_SCIMITAR_SLASH = 390
ANIM_LONGSWORD_SLASH = 390
ANIM_2H_SLASH = 407
ANIM_MAUL_CRUSH = 1665
ANIM_MAUL_SPEC = 1667
ANIM_CLAWS_SLASH = 393
ANIM_CLAWS_SPEC = 7514
ANIM_GODSWORD_SLASH = 7045
ANIM_GODSWORD_READY = 7053
ANIM_GODSWORD_WALK = 7052
ANIM_GODSWORD_RUN = 7043
ANIM_GODSWORD_TURN = 7044
ANIM_GODSWORD_WALK_LEFT = 7048
ANIM_GODSWORD_WALK_RIGHT = 7047
ANIM_GODSWORD_BLOCK = 7056
ANIM_GODSWORD_SPEC_AGS = 7644
ANIM_BALLISTA_SHOOT = 7218
ANIM_DARK_BOW_SHOOT = 426
ANIM_JAVELIN_THROW = 806
ANIM_WARHAMMER_CRUSH = 401
ANIM_WARHAMMER_SPEC = 1378
ANIM_RAPIER_STAB = 8145
ANIM_MACE_CRUSH = 400
ANIM_INQUISITOR_SPEC = 1060
ANIM_ELDER_MAUL_CRUSH = 7516
ANIM_VOIDWAKER_SPEC = 1378
ANIM_VLS_SLASH = 390
ANIM_VLS_SPEC = 7515

# magic animations
ANIM_CAST_STANDARD = 1162
ANIM_CAST_ICE_BARRAGE = 1979
ANIM_CAST_BLOOD_BARRAGE = 1979
ANIM_CAST_VENGEANCE = 4410

# eating / potions
ANIM_EAT = 829
ANIM_DRINK = 829

# defensive
ANIM_BLOCK_SHIELD = 1156
ANIM_BLOCK_MELEE = 424
ANIM_DEATH = 836
ANIM_PUNCH = 422

# kodai wand
ANIM_KODAI_BASH = 414
ANIM_VOLATILE_SPEC = 8532

# all animation IDs we want to export
NEEDED_ANIMATIONS = {
    ANIM_IDLE, ANIM_WALK, ANIM_RUN,
    ANIM_TURN_180, ANIM_TURN_CW, ANIM_TURN_CCW,
    ANIM_WHIP_ATTACK, ANIM_STAFF_BASH, ANIM_STAFF_SLAM,
    ANIM_DAGGER_STAB, ANIM_DAGGER_SPEC,
    ANIM_CROSSBOW_SHOOT, ANIM_SCIMITAR_SLASH,
    ANIM_2H_SLASH, ANIM_MAUL_CRUSH, ANIM_MAUL_SPEC,
    ANIM_CLAWS_SLASH, ANIM_CLAWS_SPEC,
    ANIM_GODSWORD_SLASH, ANIM_GODSWORD_READY, ANIM_GODSWORD_WALK,
    ANIM_GODSWORD_RUN, ANIM_GODSWORD_TURN, ANIM_GODSWORD_WALK_LEFT,
    ANIM_GODSWORD_WALK_RIGHT, ANIM_GODSWORD_BLOCK,
    ANIM_GODSWORD_SPEC_AGS,
    ANIM_BALLISTA_SHOOT, ANIM_DARK_BOW_SHOOT,
    ANIM_JAVELIN_THROW,
    ANIM_WARHAMMER_CRUSH, ANIM_WARHAMMER_SPEC,
    ANIM_RAPIER_STAB, ANIM_MACE_CRUSH, ANIM_INQUISITOR_SPEC,
    ANIM_ELDER_MAUL_CRUSH, ANIM_VOIDWAKER_SPEC,
    ANIM_VLS_SLASH, ANIM_VLS_SPEC,
    ANIM_CAST_STANDARD, ANIM_CAST_ICE_BARRAGE,
    ANIM_CAST_VENGEANCE,
    ANIM_EAT,
    ANIM_BLOCK_SHIELD, ANIM_BLOCK_MELEE,
    ANIM_DEATH, ANIM_PUNCH,
    ANIM_KODAI_BASH, ANIM_VOLATILE_SPEC,
    # spotanim effect animations (spell effects, projectiles)
    653,   # magic splash (GFX 85)
    1964,  # ice barrage projectile (GFX 368)
    1965,  # ice barrage impact (GFX 369)
    1967,  # blood barrage impact (GFX 377)
    # zulrah NPC animations
    5068, 5069, 5070, 5071, 5072,  # zulrah attack/idle/dive/surface
    5806, 5807,                     # zulrah additional anims
    # snakeling animations
    1721,  # snakeling idle
    140,   # snakeling melee attack
    185,   # snakeling magic attack
    138,   # snakeling death
    2405,  # snakeling walk
    # zulrah spotanim/projectile animations
    5358,  # GFX 1044 ranged projectile / GFX 1047 snakeling spawn orb
    3151,  # GFX 1045 toxic cloud
    6648,  # GFX 1046 magic projectile
    # player weapon attack animations (zulrah encounter)
    1167,  # trident cast wave (HUMAN_CASTWAVE_STAFF)
    1074,  # magic shortbow snapshot special
    5061,  # toxic blowpipe attack
    # player weapon projectile spotanim animations
    5460,  # GFX 665 trident casting effect
    5461,  # GFX 1042 trident impact
    5462,  # GFX 1040 trident projectile
    6622,  # GFX 1122 dragon dart projectile
    876,   # GFX 1043 blowpipe special attack
}


def parse_modern_framebase(base_id: int, data: bytes) -> FrameBaseDef:
    """Parse a single framebase from modern cache (index 1).

    Modern framebases are stored as individual entries. The format inside
    is the same as 317: u8 slot_count, u8[slot_count] types,
    u8[slot_count] map_lengths, then map entries.
    """
    fb = FrameBaseDef(base_id=base_id)
    fbuf = io.BytesIO(data)

    fb.slot_count = read_ubyte(fbuf)
    fb.types = [read_ubyte(fbuf) for _ in range(fb.slot_count)]

    map_lengths = [read_ubyte(fbuf) for _ in range(fb.slot_count)]
    fb.frame_maps = []
    for length in map_lengths:
        fb.frame_maps.append([read_ubyte(fbuf) for _ in range(length)])

    return fb


def load_modern_framebases(
    reader: ModernCacheReader, needed_base_ids: set[int],
) -> dict[int, FrameBaseDef]:
    """Load framebases from modern cache index 1.

    Each framebase is a separate group in index 1. Groups may contain
    multiple files — we use file 0 as the framebase data.
    """
    framebases: dict[int, FrameBaseDef] = {}

    for base_id in sorted(needed_base_ids):
        raw = reader.read_container(MODERN_FRAMEBASE_INDEX, base_id)
        if raw is None:
            print(f"  warning: framebase {base_id} not found in index {MODERN_FRAMEBASE_INDEX}")
            continue

        fb = parse_modern_framebase(base_id, raw)
        framebases[base_id] = fb

    return framebases


def load_modern_frame_archive(
    reader: ModernCacheReader,
    group_id: int,
    framebases: dict[int, FrameBaseDef],
) -> dict[int, FrameDef]:
    """Load a frame archive from modern cache index 0.

    In modern cache, frame archives are in index 0. Each group contains
    multiple files (one per frame). We use read_group to get all files,
    then parse each as a NormalFrame.
    """
    try:
        files = reader.read_group(MODERN_FRAME_INDEX, group_id)
    except (KeyError, FileNotFoundError):
        return {}

    frames: dict[int, FrameDef] = {}
    for file_id, file_data in files.items():
        if len(file_data) < 3:
            continue
        frame = _parse_normal_frame(group_id, file_id, file_data, framebases)
        if frame is not None:
            frames[file_id] = frame

    return frames


def load_sequences_from_modern_cache(
    store: RcCacheStore,
) -> dict[int, SequenceDef]:
    seq_files = read_config_group(store, CONFIG_SEQUENCE)
    return {
        seq_id: decode_sequence_definition(seq_id, entry_data)
        for seq_id, entry_data in seq_files.items()
    }


def export_animations_from_modern_cache(
    cache_path: Path,
    output_path: Path,
    needed_seq_ids: set[int],
) -> AnimationWriteStats:
    modern_store = RcCacheStore(cache_path)
    print(f"reading modern cache from {cache_path}")
    print("loading sequences...")
    sequences = load_sequences_from_modern_cache(modern_store)
    print(f"  loaded {len(sequences)} sequences")

    available = set(needed_seq_ids) & set(sequences.keys())
    missing = set(needed_seq_ids) - set(sequences.keys())
    if missing:
        print(f"  warning: {len(missing)} animations not found in cache: {sorted(missing)}")
    print(f"  {len(available)} needed animations available")

    needed_frame_ids = {
        fid
        for seq_id in available
        for fid in sequences[seq_id].primary_frame_ids
        if fid != -1
    }
    needed_groups = {fid >> 16 for fid in needed_frame_ids}
    print(f"loading {len(needed_groups)} frame archives from cache...")

    framebases, all_frames, missing_groups, missing_bases = load_required_frames(
        modern_store,
        needed_frame_ids,
    )
    loaded = len(all_frames)
    errors = len(missing_groups) + len(missing_bases)
    if missing_groups:
        print(
            f"  warning: {len(missing_groups)} frame archive groups missing "
            f"from index {MODERN_FRAME_INDEX}"
        )
    if missing_bases:
        print(
            f"  warning: {len(missing_bases)} framebases missing "
            f"from index {MODERN_FRAMEBASE_INDEX}"
        )
    print(f"  loaded {len(framebases)} framebases")
    print(
        f"  loaded {loaded} frame archives "
        f"({sum(len(v) for v in all_frames.values())} total frames), {errors} errors"
    )

    return write_animations_binary(output_path, framebases, all_frames, sequences, available)


def read_spotanim_sequence_ids(path: Path) -> set[int]:
    data = path.read_bytes()
    if len(data) < 12:
        raise ValueError(f"spotanim file too small: {path}")
    magic, _version, count = struct.unpack_from("<III", data, 0)
    if magic != SPOTANIM_BIN_MAGIC:
        raise ValueError(f"bad spotanim magic in {path}")
    pos = 12
    row_size = struct.calcsize("<IiiIIIii")
    out: set[int] = set()
    for _ in range(count):
        if pos + row_size > len(data):
            raise ValueError(f"truncated spotanim row in {path}")
        _gfx_id, _model_id, anim_id, _sx, _sz, _rot, _bright, _shadow = (
            struct.unpack_from("<IiiIIIii", data, pos)
        )
        pos += row_size
        if anim_id >= 0:
            out.add(anim_id)
    return out


def read_object_anim_sequence_ids(paths: list[Path]) -> set[int]:
    out: set[int] = set()
    row_size = struct.calcsize("<IIiiiBBBBffff")
    for path in paths:
        if not path.exists():
            continue
        data = path.read_bytes()
        if len(data) < 12:
            raise ValueError(f"object anim file too small: {path}")
        magic, _version, count = struct.unpack_from("<III", data, 0)
        if magic != OBJECT_ANIM_BIN_MAGIC:
            raise ValueError(f"bad object anim magic in {path}")
        pos = 12
        for _ in range(count):
            if pos + row_size > len(data):
                raise ValueError(f"truncated object anim row in {path}")
            _model_id, _obj_id, anim_id, *_rest = struct.unpack_from(
                "<IIiiiBBBBffff", data, pos)
            pos += row_size
            if anim_id >= 0:
                out.add(anim_id)
    return out


def main() -> None:
    """Export animation data from OSRS cache."""
    parser = argparse.ArgumentParser(description="export OSRS animations from cache")
    cache_group = parser.add_mutually_exclusive_group(required=True)
    cache_group.add_argument("--cache", type=Path, help="path to 317 tarnish cache directory")
    cache_group.add_argument("--modern-cache", type=Path, help="path to modern OpenRS2 cache directory")
    parser.add_argument("--output", required=True, help="output .anims file path")
    parser.add_argument("--spotanims", type=Path,
                        help="include sequence ids referenced by a SPOT binary")
    parser.add_argument("--object-anims", type=Path, nargs="*",
                        help="include sequence ids referenced by OANM binaries")
    args = parser.parse_args()

    output_path = Path(args.output)
    use_modern = args.modern_cache is not None
    cache_path = args.modern_cache if use_modern else args.cache

    if use_modern:
        needed = set(NEEDED_ANIMATIONS)
        if args.spotanims:
            spotanim_ids = read_spotanim_sequence_ids(args.spotanims)
            needed |= spotanim_ids
            print(f"including {len(spotanim_ids)} spotanim sequence IDs")
        if args.object_anims:
            object_anim_ids = read_object_anim_sequence_ids(args.object_anims)
            needed |= object_anim_ids
            print(f"including {len(object_anim_ids)} object animation sequence IDs")
        export_animations_from_modern_cache(
            cache_path,
            output_path,
            needed,
        )
        return
    else:
        cache = CacheReader(cache_path)

    # 1. load sequences
    print("loading sequences...")
    raw = cache.get(CONFIG_INDEX, CONFIG_ARCHIVE)
    if raw is None:
        sys.exit("could not read config archive")

    archive = decode_archive(raw)
    seq_hash = hash_archive_name("seq.dat") & 0xFFFFFFFF
    seq_data = archive.get(seq_hash) or archive.get(hash_archive_name("seq.dat"))
    if seq_data is None:
        sys.exit("seq.dat not found in config archive")

    sequences = parse_sequences(seq_data)
    print(f"  loaded {len(sequences)} sequences")

    # filter to needed animations
    available = NEEDED_ANIMATIONS & set(sequences.keys())
    missing = NEEDED_ANIMATIONS - set(sequences.keys())
    if missing:
        print(f"  warning: {len(missing)} animations not found in cache: {sorted(missing)}")
    print(f"  {len(available)} needed animations available")

    # 2. collect needed frame group IDs from sequences
    needed_groups: set[int] = set()
    for seq_id in available:
        seq = sequences[seq_id]
        for fid in seq.primary_frame_ids:
            if fid != -1:
                needed_groups.add(fid >> 16)

    print(f"loading {len(needed_groups)} frame archives from cache...")

    # 3. 317 path: load framebases from config archive first
    print("loading framebases...")
    raw = cache.get(CONFIG_INDEX, CONFIG_ARCHIVE)
    if raw is None:
        sys.exit("could not read config archive")

    archive = decode_archive(raw)
    fb_hash = hash_archive_name("framebases.dat") & 0xFFFFFFFF
    fb_data = archive.get(fb_hash) or archive.get(hash_archive_name("framebases.dat"))
    if fb_data is None:
        sys.exit("framebases.dat not found in config archive")

    framebases = parse_framebases(fb_data)
    print(f"  loaded {len(framebases)} framebases")

    # 4. load frame archives
    all_frames = {}
    loaded = 0
    errors = 0
    for group_id in sorted(needed_groups):
        raw = cache.get(FRAME_CACHE_INDEX, group_id)
        if raw is None:
            print(f"  warning: frame archive group {group_id} not found in cache index {FRAME_CACHE_INDEX}")
            errors += 1
            continue

        # frame archives are gzip-compressed in the cache
        if raw[:2] == b"\x1f\x8b":
            raw = gzip.decompress(raw)

        frames = parse_frame_archive(group_id, raw, framebases)
        if frames:
            all_frames[group_id] = frames
            loaded += 1

    print(f"  loaded {loaded} frame archives ({sum(len(v) for v in all_frames.values())} total frames), {errors} errors")

    # 5. write output
    write_animations_binary(output_path, framebases, all_frames, sequences, available)


if __name__ == "__main__":
    main()
