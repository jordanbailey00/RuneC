"""Export collision data from a 317-format OSRS cache to our binary .cmap format.

Reads tarnish's cache files (main_file_cache.dat + .idx*), parses the map_index
manifest, then for each region:
  1. Parse terrain data (gzipped) — mark tiles with attribute flag & 1 as BLOCKED
  2. Parse object data (gzipped) — mark walls and occupants using object definitions

The object definitions (solid, impenetrable, width, length) are also loaded from
the cache. This replicates what tarnish's RegionDecoder + ObjectDefinitionDecoder
do at server startup, ported to Python.

Output: binary .cmap file consumable by osrs_pvp_collision.h's collision_map_load().

Usage:
    uv run python scripts/export_collision_map.py \
        --cache ../reference/tarnish/game-server/data/cache \
        --output data/wilderness.cmap
"""

import argparse
import gzip
import io
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

# --- collision flag constants (from TraversalConstants.java) ---

WALL_NORTH_WEST = 0x000001
WALL_NORTH = 0x000002
WALL_NORTH_EAST = 0x000004
WALL_EAST = 0x000008
WALL_SOUTH_EAST = 0x000010
WALL_SOUTH = 0x000020
WALL_SOUTH_WEST = 0x000040
WALL_WEST = 0x000080

IMPENETRABLE_WALL_NORTH_WEST = 0x000200
IMPENETRABLE_WALL_NORTH = 0x000400
IMPENETRABLE_WALL_NORTH_EAST = 0x000800
IMPENETRABLE_WALL_EAST = 0x001000
IMPENETRABLE_WALL_SOUTH_EAST = 0x002000
IMPENETRABLE_WALL_SOUTH = 0x004000
IMPENETRABLE_WALL_SOUTH_WEST = 0x008000
IMPENETRABLE_WALL_WEST = 0x010000

IMPENETRABLE_BLOCKED = 0x020000
BLOCKED = 0x200000

# --- 317 cache format reader ---

INDEX_SIZE = 6
SECTOR_HEADER_SIZE = 8
SECTOR_SIZE = 520
SECTOR_DATA_SIZE = 512

CONFIG_INDEX = 0
MAP_INDEX = 4
MANIFEST_ARCHIVE = 5


def read_medium(data: bytes, offset: int) -> int:
    """Read a 3-byte big-endian unsigned integer."""
    return (data[offset] << 16) | (data[offset + 1] << 8) | data[offset + 2]


def read_smart(buf: io.BytesIO) -> int:
    """Read a 'smart' value (1 or 2 bytes depending on MSB)."""
    pos = buf.tell()
    peek = buf.read(1)
    if not peek:
        return 0
    buf.seek(pos)
    val = peek[0]
    if val < 128:
        return buf.read(1)[0] & 0xFF
    raw = struct.unpack(">H", buf.read(2))[0]
    return raw - 32768


class CacheReader:
    """Reads the 317-format main_file_cache.dat + .idx files."""

    def __init__(self, cache_dir: Path) -> None:
        self.data_path = cache_dir / "main_file_cache.dat"
        self.data_bytes = self.data_path.read_bytes()

        self.idx_bytes: dict[int, bytes] = {}
        for idx_path in sorted(cache_dir.glob("main_file_cache.idx*")):
            idx_id = int(idx_path.suffix.replace(".idx", ""))
            self.idx_bytes[idx_id] = idx_path.read_bytes()

    def get(self, cache_id: int, file_id: int) -> bytes | None:
        """Read a file from a cache index, following the sector chain."""
        idx_data = self.idx_bytes.get(cache_id)
        if idx_data is None:
            return None

        idx_offset = file_id * INDEX_SIZE
        if idx_offset + INDEX_SIZE > len(idx_data):
            return None

        length = read_medium(idx_data, idx_offset)
        sector_id = read_medium(idx_data, idx_offset + 3)

        if length <= 0 or sector_id <= 0:
            return None

        result = bytearray()
        chunk = 0

        while len(result) < length:
            read_size = min(length - len(result), SECTOR_DATA_SIZE)
            file_offset = sector_id * SECTOR_SIZE

            if file_offset + SECTOR_HEADER_SIZE + read_size > len(self.data_bytes):
                return None

            # sector header: 2 fileID, 2 chunk, 3 nextSector, 1 cacheID
            hdr = self.data_bytes[file_offset : file_offset + SECTOR_HEADER_SIZE]
            _file_id = (hdr[0] << 8) | hdr[1]
            _chunk = (hdr[2] << 8) | hdr[3]
            next_sector = (hdr[4] << 16) | (hdr[5] << 8) | hdr[6]
            _cache_id = hdr[7]

            data_start = file_offset + SECTOR_HEADER_SIZE
            result.extend(self.data_bytes[data_start : data_start + read_size])

            sector_id = next_sector
            chunk += 1

        return bytes(result[:length])


def hash_archive_name(name: str) -> int:
    """Compute archive name hash (same as StringUtils.hashArchive in tarnish).

    Java uses 32-bit signed int arithmetic with wraparound, so we mask to 32 bits
    and convert to signed at the end.
    """
    h = 0
    for c in name.upper():
        h = (h * 61 + ord(c) - 32) & 0xFFFFFFFF
    # convert to signed 32-bit (Java int semantics)
    if h >= 0x80000000:
        h -= 0x100000000
    return h


def decode_archive(raw: bytes) -> dict[int, bytes]:
    """Decode a 317-format archive (bzip2 compressed sectors)."""
    buf = io.BytesIO(raw)
    length = read_medium(raw, 0)
    compressed_length = read_medium(raw, 3)
    buf.seek(6)

    if compressed_length != length:
        # bzip2 compressed (headerless — need to add BZ header)
        compressed = raw[6 : 6 + compressed_length]
        import bz2

        # 317 bzip2 is headerless — prepend the standard bzip2 header
        decompressed = bz2.decompress(b"BZh1" + compressed)
        data = decompressed
    else:
        data = raw[6:]

    view = io.BytesIO(data)
    total = struct.unpack(">H", view.read(2))[0] & 0xFF
    header_start = view.tell()
    data_offset = header_start + total * 10

    sectors: dict[int, bytes] = {}
    for _ in range(total):
        name_hash = struct.unpack(">I", view.read(4))[0]
        sec_length = read_medium(view.read(3), 0)
        sec_compressed = read_medium(view.read(3), 0)

        if sec_length != sec_compressed:
            import bz2

            compressed = data[data_offset : data_offset + sec_compressed]
            sector_data = bz2.decompress(b"BZh1" + compressed)
        else:
            sector_data = data[data_offset : data_offset + sec_length]

        sectors[name_hash] = sector_data
        data_offset += sec_compressed

    return sectors


# --- object definition decoder ---


@dataclass
class ObjDef:
    """Minimal object definition for collision marking."""

    obj_id: int = 0
    width: int = 1
    length: int = 1
    solid: bool = True
    impenetrable: bool = True
    has_actions: bool = False
    is_decoration: bool = False
    actions: list[str | None] = field(default_factory=lambda: [None] * 5)


CONFIG_ARCHIVE = 2


def decode_object_definitions(cache: CacheReader) -> dict[int, ObjDef]:
    """Decode object definitions from cache config archive."""
    raw = cache.get(CONFIG_INDEX, CONFIG_ARCHIVE)
    if raw is None:
        print("warning: could not read config archive", file=sys.stderr)
        return {}

    archive = decode_archive(raw)
    loc_hash = hash_archive_name("loc.dat")
    idx_hash = hash_archive_name("loc.idx")

    loc_data = archive.get(loc_hash)
    loc_idx = archive.get(idx_hash)

    if loc_data is None or loc_idx is None:
        print("warning: loc.dat/loc.idx not found in config archive", file=sys.stderr)
        return {}

    buf = io.BytesIO(loc_data)
    idx_buf = io.BytesIO(loc_idx)

    total = struct.unpack(">H", idx_buf.read(2))[0]
    defs: dict[int, ObjDef] = {}

    for obj_id in range(total):
        d = ObjDef(obj_id=obj_id)

        while True:
            opcode = buf.read(1)
            if not opcode:
                break
            opcode = opcode[0]

            if opcode == 0:
                break
            elif opcode == 1:
                model_count = buf.read(1)[0]
                for _ in range(model_count):
                    buf.read(2)  # model id
                    buf.read(1)  # model type
            elif opcode == 2:
                _name = _read_string(buf)
            elif opcode == 3:
                _desc = _read_string(buf)
            elif opcode == 5:
                model_count = buf.read(1)[0]
                for _ in range(model_count):
                    buf.read(2)  # model id
            elif opcode == 14:
                d.width = buf.read(1)[0]
            elif opcode == 15:
                d.length = buf.read(1)[0]
            elif opcode == 17:
                d.solid = False
            elif opcode == 18:
                d.impenetrable = False
            elif opcode == 19:
                val = buf.read(1)[0]
                d.has_actions = val == 1
            elif opcode == 21:
                pass  # contouredGround
            elif opcode == 22:
                pass  # nonFlatShading
            elif opcode == 23:
                pass  # modelClipped
            elif opcode == 24:
                buf.read(2)  # animation id
            elif opcode == 28:
                buf.read(1)  # decorDisplacement
            elif opcode == 29:
                buf.read(1)  # ambient
            elif opcode == 39:
                buf.read(1)  # contrast
            elif 30 <= opcode <= 34:
                action = _read_string(buf)
                d.actions[opcode - 30] = action if action != "hidden" else None
                if action and action != "hidden":
                    d.has_actions = True
            elif opcode == 40:
                count = buf.read(1)[0]
                for _ in range(count):
                    buf.read(2)  # old color
                    buf.read(2)  # new color
            elif opcode == 60:
                buf.read(2)  # mapAreaId
            elif opcode == 62:
                pass  # isRotated
            elif opcode == 64:
                pass  # shadow = false
            elif opcode == 65:
                buf.read(2)  # modelSizeX
            elif opcode == 66:
                buf.read(2)  # modelSizeH
            elif opcode == 67:
                buf.read(2)  # modelSizeY
            elif opcode == 68:
                buf.read(2)  # mapsceneID
            elif opcode == 69:
                buf.read(1)  # surroundings
            elif opcode == 70:
                buf.read(2)  # translateX
            elif opcode == 71:
                buf.read(2)  # translateH
            elif opcode == 72:
                buf.read(2)  # translateY
            elif opcode == 73:
                pass  # obstructsGround
            elif opcode == 74:
                pass  # isHollow (= not solid)
                d.solid = False
            elif opcode == 75:
                buf.read(1)  # supportItems
            elif opcode == 77:
                # varbit / varp / transforms
                buf.read(2)  # varpID
                buf.read(2)  # varbitID
                count = buf.read(1)[0]
                for _ in range(count + 1):
                    buf.read(2)  # transform id
            else:
                # unknown opcode — can't safely skip, break
                break

        defs[obj_id] = d

    return defs


def _read_string(buf: io.BytesIO) -> str:
    """Read a null-terminated string."""
    chars = []
    while True:
        b = buf.read(1)
        if not b or b[0] == 0:
            break
        chars.append(chr(b[0]))
    return "".join(chars)


# --- region / collision data ---


@dataclass
class RegionDef:
    """Region definition from map_index."""

    region_hash: int
    terrain_file: int
    object_file: int

    @property
    def region_x(self) -> int:
        return (self.region_hash >> 8) & 0xFF

    @property
    def region_y(self) -> int:
        return self.region_hash & 0xFF


def load_map_index(cache: CacheReader) -> list[RegionDef]:
    """Load region definitions from the manifest archive's map_index."""
    # manifest archive is idx0, file 5
    raw = cache.get(CONFIG_INDEX, MANIFEST_ARCHIVE)
    if raw is None:
        sys.exit("could not read manifest archive (idx0, file 5)")

    archive = decode_archive(raw)
    map_idx_hash = hash_archive_name("map_index")
    map_idx_data = archive.get(map_idx_hash)

    if map_idx_data is None:
        sys.exit("map_index not found in manifest archive")

    buf = io.BytesIO(map_idx_data)
    count = struct.unpack(">H", buf.read(2))[0]

    regions = []
    for _ in range(count):
        region_hash = struct.unpack(">H", buf.read(2))[0]
        terrain_file = struct.unpack(">H", buf.read(2))[0]
        object_file = struct.unpack(">H", buf.read(2))[0]
        regions.append(RegionDef(region_hash, terrain_file, object_file))

    return regions


# collision flag storage: flags[height][local_x][local_y]
CollisionFlags = list[list[list[int]]]  # [4][64][64]


def new_collision_flags() -> CollisionFlags:
    return [[[0 for _ in range(64)] for _ in range(64)] for _ in range(4)]


def parse_terrain(data: bytes) -> tuple[CollisionFlags, set[tuple[int, int, int]]]:
    """Parse terrain data, return (collision_flags, down_heights_set).

    Terrain attribute & 1 marks tiles as BLOCKED.
    Terrain attribute & 2 marks tiles for height-plane adjustment (downHeights).
    """
    flags = new_collision_flags()
    down_heights: set[tuple[int, int, int]] = set()
    attributes = [[[0 for _ in range(64)] for _ in range(64)] for _ in range(4)]

    buf = io.BytesIO(data)

    # phase 1: read attributes
    for height in range(4):
        for local_x in range(64):
            for local_y in range(64):
                while True:
                    raw = buf.read(2)
                    if len(raw) < 2:
                        break
                    attr_id = struct.unpack(">H", raw)[0]

                    if attr_id == 0:
                        break
                    elif attr_id == 1:
                        buf.read(1)  # tile height
                        break
                    elif attr_id <= 49:
                        buf.read(2)  # overlay id
                    elif attr_id <= 81:
                        attributes[height][local_x][local_y] = attr_id - 49

    # phase 2: apply terrain flags
    for height in range(4):
        for local_x in range(64):
            for local_y in range(64):
                attr = attributes[height][local_x][local_y]

                if attr & 2:
                    down_heights.add((local_x, local_y, height))

                if attr & 1:
                    plane = height
                    if attributes[1][local_x][local_y] & 2:
                        down_heights.add((local_x, local_y, 1))
                        plane -= 1
                    if plane >= 0:
                        flags[plane][local_x][local_y] |= BLOCKED

    return flags, down_heights


# --- wall marking (from TraversalMap.java markWall) ---

# object type IDs
OBJ_STRAIGHT_WALL = 0
OBJ_DIAGONAL_CORNER = 1
OBJ_ENTIRE_WALL = 2
OBJ_WALL_CORNER = 3
OBJ_DIAGONAL_WALL = 9
OBJ_GENERAL_PROP = 10
OBJ_WALKABLE_PROP = 11
OBJ_GROUND_PROP = 22

# object direction
DIR_WEST = 0
DIR_NORTH = 1
DIR_EAST = 2
DIR_SOUTH = 3


def mark_wall(
    flags: CollisionFlags,
    direction: int,
    height: int,
    lx: int,
    ly: int,
    obj_type: int,
    impenetrable: bool,
) -> None:
    """Mark wall collision flags on the tile and its neighbor (from TraversalMap.markWall)."""
    if obj_type == OBJ_STRAIGHT_WALL:
        if direction == DIR_WEST:
            _flag(flags, height, lx, ly, WALL_WEST)
            _flag_safe(flags, height, lx - 1, ly, WALL_EAST)
            if impenetrable:
                _flag(flags, height, lx, ly, IMPENETRABLE_WALL_WEST)
                _flag_safe(flags, height, lx - 1, ly, IMPENETRABLE_WALL_EAST)
        elif direction == DIR_NORTH:
            _flag(flags, height, lx, ly, WALL_NORTH)
            _flag_safe(flags, height, lx, ly + 1, WALL_SOUTH)
            if impenetrable:
                _flag(flags, height, lx, ly, IMPENETRABLE_WALL_NORTH)
                _flag_safe(flags, height, lx, ly + 1, IMPENETRABLE_WALL_SOUTH)
        elif direction == DIR_EAST:
            _flag(flags, height, lx, ly, WALL_EAST)
            _flag_safe(flags, height, lx + 1, ly, WALL_WEST)
            if impenetrable:
                _flag(flags, height, lx, ly, IMPENETRABLE_WALL_EAST)
                _flag_safe(flags, height, lx + 1, ly, IMPENETRABLE_WALL_WEST)
        elif direction == DIR_SOUTH:
            _flag(flags, height, lx, ly, WALL_SOUTH)
            _flag_safe(flags, height, lx, ly - 1, WALL_NORTH)
            if impenetrable:
                _flag(flags, height, lx, ly, IMPENETRABLE_WALL_SOUTH)
                _flag_safe(flags, height, lx, ly - 1, IMPENETRABLE_WALL_NORTH)

    elif obj_type == OBJ_ENTIRE_WALL:
        if direction == DIR_WEST:
            _flag(flags, height, lx, ly, WALL_WEST | WALL_NORTH)
            _flag_safe(flags, height, lx - 1, ly, WALL_EAST)
            _flag_safe(flags, height, lx, ly + 1, WALL_SOUTH)
            if impenetrable:
                _flag(flags, height, lx, ly, IMPENETRABLE_WALL_WEST | IMPENETRABLE_WALL_NORTH)
                _flag_safe(flags, height, lx - 1, ly, IMPENETRABLE_WALL_EAST)
                _flag_safe(flags, height, lx, ly + 1, IMPENETRABLE_WALL_SOUTH)
        elif direction == DIR_NORTH:
            _flag(flags, height, lx, ly, WALL_EAST | WALL_NORTH)
            _flag_safe(flags, height, lx, ly + 1, WALL_SOUTH)
            _flag_safe(flags, height, lx + 1, ly, WALL_WEST)
            if impenetrable:
                _flag(flags, height, lx, ly, IMPENETRABLE_WALL_EAST | IMPENETRABLE_WALL_NORTH)
                _flag_safe(flags, height, lx, ly + 1, IMPENETRABLE_WALL_SOUTH)
                _flag_safe(flags, height, lx + 1, ly, IMPENETRABLE_WALL_WEST)
        elif direction == DIR_EAST:
            _flag(flags, height, lx, ly, WALL_EAST | WALL_SOUTH)
            _flag_safe(flags, height, lx + 1, ly, WALL_WEST)
            _flag_safe(flags, height, lx, ly - 1, WALL_NORTH)
            if impenetrable:
                _flag(flags, height, lx, ly, IMPENETRABLE_WALL_EAST | IMPENETRABLE_WALL_SOUTH)
                _flag_safe(flags, height, lx + 1, ly, IMPENETRABLE_WALL_WEST)
                _flag_safe(flags, height, lx, ly - 1, IMPENETRABLE_WALL_NORTH)
        elif direction == DIR_SOUTH:
            _flag(flags, height, lx, ly, WALL_WEST | WALL_SOUTH)
            _flag_safe(flags, height, lx - 1, ly, WALL_EAST)
            _flag_safe(flags, height, lx, ly - 1, WALL_NORTH)
            if impenetrable:
                _flag(flags, height, lx, ly, IMPENETRABLE_WALL_WEST | IMPENETRABLE_WALL_SOUTH)
                _flag_safe(flags, height, lx - 1, ly, IMPENETRABLE_WALL_EAST)
                _flag_safe(flags, height, lx, ly - 1, IMPENETRABLE_WALL_NORTH)

    elif obj_type == OBJ_DIAGONAL_CORNER:
        if direction == DIR_WEST:
            _flag(flags, height, lx, ly, WALL_NORTH_WEST)
            _flag_safe(flags, height, lx - 1, ly + 1, WALL_SOUTH_EAST)
            if impenetrable:
                _flag(flags, height, lx, ly, IMPENETRABLE_WALL_NORTH_WEST)
                _flag_safe(flags, height, lx - 1, ly + 1, IMPENETRABLE_WALL_SOUTH_EAST)
        elif direction == DIR_NORTH:
            _flag(flags, height, lx, ly, WALL_NORTH_EAST)
            _flag_safe(flags, height, lx + 1, ly + 1, WALL_SOUTH_WEST)
            if impenetrable:
                _flag(flags, height, lx, ly, IMPENETRABLE_WALL_NORTH_EAST)
                _flag_safe(flags, height, lx + 1, ly + 1, IMPENETRABLE_WALL_SOUTH_WEST)
        elif direction == DIR_EAST:
            _flag(flags, height, lx, ly, WALL_SOUTH_EAST)
            _flag_safe(flags, height, lx + 1, ly - 1, WALL_NORTH_WEST)
            if impenetrable:
                _flag(flags, height, lx, ly, IMPENETRABLE_WALL_SOUTH_EAST)
                _flag_safe(flags, height, lx + 1, ly - 1, IMPENETRABLE_WALL_NORTH_WEST)
        elif direction == DIR_SOUTH:
            _flag(flags, height, lx, ly, WALL_SOUTH_WEST)
            _flag_safe(flags, height, lx - 1, ly - 1, WALL_NORTH_EAST)
            if impenetrable:
                _flag(flags, height, lx, ly, IMPENETRABLE_WALL_SOUTH_WEST)
                _flag_safe(flags, height, lx - 1, ly - 1, IMPENETRABLE_WALL_NORTH_EAST)

    elif obj_type == OBJ_WALL_CORNER:
        if direction == DIR_WEST:
            _flag(flags, height, lx, ly, WALL_WEST)
            _flag_safe(flags, height, lx - 1, ly, WALL_EAST)
            if impenetrable:
                _flag(flags, height, lx, ly, IMPENETRABLE_WALL_WEST)
                _flag_safe(flags, height, lx - 1, ly, IMPENETRABLE_WALL_EAST)
        elif direction == DIR_NORTH:
            _flag(flags, height, lx, ly, WALL_NORTH)
            _flag_safe(flags, height, lx, ly + 1, WALL_SOUTH)
            if impenetrable:
                _flag(flags, height, lx, ly, IMPENETRABLE_WALL_NORTH)
                _flag_safe(flags, height, lx, ly + 1, IMPENETRABLE_WALL_SOUTH)
        elif direction == DIR_EAST:
            _flag(flags, height, lx, ly, WALL_EAST)
            _flag_safe(flags, height, lx + 1, ly, WALL_WEST)
            if impenetrable:
                _flag(flags, height, lx, ly, IMPENETRABLE_WALL_EAST)
                _flag_safe(flags, height, lx + 1, ly, IMPENETRABLE_WALL_WEST)
        elif direction == DIR_SOUTH:
            _flag(flags, height, lx, ly, WALL_SOUTH)
            _flag_safe(flags, height, lx, ly - 1, WALL_NORTH)
            if impenetrable:
                _flag(flags, height, lx, ly, IMPENETRABLE_WALL_SOUTH)
                _flag_safe(flags, height, lx, ly - 1, IMPENETRABLE_WALL_NORTH)


def mark_occupant(
    flags: CollisionFlags,
    height: int,
    lx: int,
    ly: int,
    width: int,
    length: int,
    impenetrable: bool,
) -> None:
    """Mark a multi-tile occupant as BLOCKED + optionally IMPENETRABLE_BLOCKED."""
    flag = BLOCKED
    if impenetrable:
        flag |= IMPENETRABLE_BLOCKED
    for xi in range(lx, lx + width):
        for yi in range(ly, ly + length):
            _flag_safe(flags, height, xi, yi, flag)


def _flag(flags: CollisionFlags, height: int, lx: int, ly: int, flag: int) -> None:
    """Set flag bits on a local tile (no bounds check)."""
    flags[height][lx][ly] |= flag


def _flag_safe(flags: CollisionFlags, height: int, lx: int, ly: int, flag: int) -> None:
    """Set flag bits with bounds check (neighbor might be outside 64x64 region)."""
    if 0 <= lx < 64 and 0 <= ly < 64 and 0 <= height < 4:
        flags[height][lx][ly] |= flag


def parse_objects(
    data: bytes,
    flags: CollisionFlags,
    base_x: int,
    base_y: int,
    down_heights: set[tuple[int, int, int]],
    obj_defs: dict[int, ObjDef],
) -> None:
    """Parse object data and mark collision flags (walls, occupants)."""
    buf = io.BytesIO(data)
    obj_id = -1

    while True:
        obj_id_offset = read_smart(buf)
        if obj_id_offset == 0:
            break

        obj_id += obj_id_offset
        obj_pos_info = 0

        while True:
            pos_offset = read_smart(buf)
            if pos_offset == 0:
                break
            obj_pos_info += pos_offset - 1

            raw_byte = buf.read(1)
            if not raw_byte:
                return
            obj_other_info = raw_byte[0]
            local_y = obj_pos_info & 0x3F
            local_x = (obj_pos_info >> 6) & 0x3F
            height = (obj_pos_info >> 12) & 0x3

            obj_type = obj_other_info >> 2
            direction = obj_other_info & 0x3

            # downHeights adjustment (from RegionDecoder.java)
            if (local_x, local_y, 1) in down_heights:
                height -= 1
                if height < 0:
                    continue
            elif (local_x, local_y, height) in down_heights:
                height -= 1

            if height < 0:
                continue

            d = obj_defs.get(obj_id)
            if d is None:
                continue

            if not d.solid:
                continue

            # swap width/length for N/S rotation (from TraversalMap.markObject)
            if direction == DIR_NORTH or direction == DIR_SOUTH:
                size_x = d.length
                size_y = d.width
            else:
                size_x = d.width
                size_y = d.length

            if obj_type == OBJ_GROUND_PROP:
                if d.has_actions:
                    mark_occupant(flags, height, local_x, local_y, size_x, size_y, False)
            elif obj_type in (OBJ_GENERAL_PROP, OBJ_WALKABLE_PROP) or obj_type >= 12:
                mark_occupant(flags, height, local_x, local_y, size_x, size_y, d.impenetrable)
            elif obj_type == OBJ_DIAGONAL_WALL:
                mark_occupant(flags, height, local_x, local_y, size_x, size_y, d.impenetrable)
            elif 0 <= obj_type <= 3:
                mark_wall(flags, direction, height, local_x, local_y, obj_type, d.impenetrable)


# --- .cmap binary writer ---

CMAP_MAGIC = 0x434D4150
CMAP_VERSION = 1


def write_cmap(output_path: Path, regions: dict[int, CollisionFlags]) -> None:
    """Write regions to our binary .cmap format."""
    with open(output_path, "wb") as f:
        f.write(struct.pack("<III", CMAP_MAGIC, CMAP_VERSION, len(regions)))

        for key, flags in sorted(regions.items()):
            f.write(struct.pack("<i", key))
            # flags[4][64][64] as int32 little-endian
            for h in range(4):
                for x in range(64):
                    for y in range(64):
                        f.write(struct.pack("<i", flags[h][x][y]))


# --- main ---


def main() -> None:
    parser = argparse.ArgumentParser(description="export OSRS collision map from 317 cache")
    parser.add_argument(
        "--cache",
        type=Path,
        default=Path("../reference/tarnish/game-server/data/cache"),
        help="path to cache directory containing main_file_cache.dat + .idx files",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("data/wilderness.cmap"),
        help="output .cmap binary file",
    )
    parser.add_argument(
        "--all-regions",
        action="store_true",
        help="export all regions (default: wilderness only)",
    )
    args = parser.parse_args()

    if not args.cache.exists():
        sys.exit(f"cache directory not found: {args.cache}")

    args.output.parent.mkdir(parents=True, exist_ok=True)

    print(f"reading cache from {args.cache}")
    cache = CacheReader(args.cache)

    print("loading object definitions from cache...")
    obj_defs = decode_object_definitions(cache)
    print(f"  loaded {len(obj_defs)} object definitions")

    print("loading map index...")
    region_defs = load_map_index(cache)
    print(f"  {len(region_defs)} regions in map index")

    # wilderness is roughly regionX=46-53, regionY=48-62
    # world coords: x=2944-3392, y=3072-3968
    # our arena is centered at (3222, 3544) → regionX=50, regionY=55
    if not args.all_regions:
        wilderness_defs = [
            rd for rd in region_defs if 44 <= rd.region_x <= 56 and 48 <= rd.region_y <= 62
        ]
        print(f"  filtering to wilderness: {len(wilderness_defs)} regions")
    else:
        wilderness_defs = region_defs
        print(f"  exporting all {len(wilderness_defs)} regions")

    output_regions: dict[int, CollisionFlags] = {}
    decoded = 0
    errors = 0

    for rd in wilderness_defs:
        base_x = rd.region_x << 6
        base_y = rd.region_y << 6

        terrain_data = cache.get(MAP_INDEX, rd.terrain_file)
        if terrain_data is None:
            errors += 1
            continue

        try:
            terrain_data = gzip.decompress(terrain_data)
        except Exception:
            errors += 1
            continue

        flags, down_heights = parse_terrain(terrain_data)

        # parse objects
        obj_data = cache.get(MAP_INDEX, rd.object_file)
        if obj_data is not None:
            try:
                obj_data = gzip.decompress(obj_data)
                parse_objects(obj_data, flags, base_x, base_y, down_heights, obj_defs)
            except Exception as e:
                print(f"  warning: object parse failed for region ({rd.region_x}, {rd.region_y}): {e}")

        # compute the region map key the same way as osrs_pvp_collision.h
        key = (rd.region_x << 8) | rd.region_y
        output_regions[key] = flags
        decoded += 1

    print(f"\ndecoded {decoded} regions, {errors} skipped")

    # count non-zero tiles
    blocked_count = 0
    wall_count = 0
    for flags in output_regions.values():
        for x in range(64):
            for y in range(64):
                f = flags[0][x][y]  # height 0 only for stats
                if f & BLOCKED:
                    blocked_count += 1
                if f & (WALL_NORTH | WALL_SOUTH | WALL_EAST | WALL_WEST):
                    wall_count += 1

    print(f"height-0 stats: {blocked_count} blocked tiles, {wall_count} tiles with walls")

    write_cmap(args.output, output_regions)
    file_size = args.output.stat().st_size
    print(f"\nwrote {file_size:,} bytes to {args.output}")


if __name__ == "__main__":
    main()
