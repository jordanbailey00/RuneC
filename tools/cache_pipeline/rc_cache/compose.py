"""Composed mapsquares: fixed map areas built from copies of other areas.

``content/world/composed_maps.toml`` lists rectangles of tiles to copy, with
the locs placed on them, from the cache's map into mapsquares the cache
leaves empty. ``maps.py`` serves the copies as ordinary terrain and location
files for those mapsquares, so every exporter reads them like the cache's
own map. Copies land on plane 0 and carry explicit heights: the source tile's
absolute height plus the copy's offset, chosen so that the copy's ``join``
tiles on its south row meet the previous copy's north row. Empty tiles within
EDGE_REACH of a copy repeat its nearest edge height, because a tile's mesh
and lighting read the heights of its neighbours.
"""

from __future__ import annotations

import io
import os
import struct
import tomllib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterable

from .locations import iter_location_placements
from .terrain import calculate_height

SPEC_ENV = "RUNEC_COMPOSED_MAPS"
DEFAULT_SPEC = Path(__file__).resolve().parents[3] / "content" / "world" / "composed_maps.toml"

# Reads one raw map file ("terrain" or "locations") of a cache mapsquare.
RawReader = Callable[[object, int, int, str], "bytes | None"]

EDGE_REACH = 2
HEIGHT_STEP = 8         # explicit heights are stored in units of 8
BRIDGE_FLAG = 0x2       # plane 1 setting that drops a column one plane


@dataclass(frozen=True)
class MapCopy:
    name: str
    source: tuple[int, int, int]        # south-west tile: x, y, plane
    size: tuple[int, int]
    dest: tuple[int, int, int]
    join: tuple[int, int] | None = None  # local x range meeting the previous copy

    def covers(self, x: int, y: int, plane: int) -> bool:
        dx, dy, dp = self.dest
        return (plane == dp and dx <= x < dx + self.size[0]
                and dy <= y < dy + self.size[1])

    def source_of(self, x: int, y: int) -> tuple[int, int, int]:
        return (self.source[0] + x - self.dest[0], self.source[1] + y - self.dest[1],
                self.source[2])

    def distance(self, x: int, y: int) -> int:
        """Chebyshev distance from a plane-0 tile to the copy (0 inside it)."""
        dx, dy, _ = self.dest
        return max(dx - x, x - (dx + self.size[0] - 1),
                   dy - y, y - (dy + self.size[1] - 1), 0)

    def clamped_source(self, x: int, y: int) -> tuple[int, int, int]:
        """Source tile of the copy's tile nearest to (x, y)."""
        dx, dy, _ = self.dest
        return self.source_of(min(max(x, dx), dx + self.size[0] - 1),
                              min(max(y, dy), dy + self.size[1] - 1))

    def mapsquares(self) -> set[tuple[int, int]]:
        dx, dy, _ = self.dest
        return {(rx, ry)
                for rx in range(dx >> 6, ((dx + self.size[0] - 1) >> 6) + 1)
                for ry in range(dy >> 6, ((dy + self.size[1] - 1) >> 6) + 1)}

    def overlaps(self, other: "MapCopy") -> bool:
        return (self.dest[2] == other.dest[2]
                and self.dest[0] < other.dest[0] + other.size[0]
                and other.dest[0] < self.dest[0] + self.size[0]
                and self.dest[1] < other.dest[1] + other.size[1]
                and other.dest[1] < self.dest[1] + self.size[1])


def spec_path() -> Path | None:
    """The composition in use: $RUNEC_COMPOSED_MAPS ("none" disables it)."""
    raw = os.environ.get(SPEC_ENV)
    if raw is not None:
        return Path(raw) if raw and raw.lower() != "none" else None
    return DEFAULT_SPEC if DEFAULT_SPEC.is_file() else None


def load_copies(path: Path | None) -> list[MapCopy]:
    if path is None or not path.is_file():
        return []
    doc = tomllib.loads(path.read_text())
    copies = []
    for row in doc.get("copy", []):
        copy = MapCopy(
            name=str(row["name"]),
            source=tuple(int(v) for v in row["source"]),
            size=tuple(int(v) for v in row["size"]),
            dest=tuple(int(v) for v in row["dest"]),
            join=tuple(int(v) for v in row["join"]) if "join" in row else None,
        )
        if len(copy.source) != 3 or len(copy.dest) != 3 or len(copy.size) != 2:
            raise ValueError(f"{path}: copy {copy.name} needs x, y, plane and a size")
        if copy.dest[2] != 0:
            raise ValueError(f"{path}: copy {copy.name} must land on plane 0")
        for other in copies:
            if copy.overlaps(other):
                raise ValueError(f"{path}: copies {other.name} and {copy.name} overlap")
        copies.append(copy)
    return copies


@dataclass
class _Terrain:
    """One decoded source mapsquare: absolute heights and raw tile fields."""

    heights: list = field(default_factory=lambda: _grid(0))
    underlay: list = field(default_factory=lambda: _grid(0))
    overlay_attr: list = field(default_factory=lambda: _grid(0))
    overlay_raw: list = field(default_factory=lambda: _grid(0))
    settings: list = field(default_factory=lambda: _grid(0))


def _grid(value: int) -> list:
    return [[[value] * 64 for _ in range(64)] for _ in range(4)]


def decode_terrain(data: bytes, region_x: int, region_y: int) -> _Terrain:
    """Decode a terrain file the way the client does (heights made absolute)."""
    t = _Terrain()
    buf = io.BytesIO(data)
    base_x, base_y = region_x * 64, region_y * 64
    for plane in range(4):
        for x in range(64):
            for y in range(64):
                while True:
                    raw = buf.read(2)
                    if len(raw) < 2:
                        break
                    attr = struct.unpack(">H", raw)[0]
                    if attr in (0, 1):
                        if attr == 0:
                            step = (calculate_height(0xE3B7B + x + base_x,
                                                     0x87CCE + y + base_y) * 8
                                    if plane == 0 else 240)
                        else:
                            step = buf.read(1)[0]
                            step = (0 if step == 1 else step) * 8
                        below = t.heights[plane - 1][x][y] if plane else 0
                        t.heights[plane][x][y] = below - step
                        break
                    if attr <= 49:
                        t.overlay_attr[plane][x][y] = attr
                        t.overlay_raw[plane][x][y] = struct.unpack(">H", buf.read(2))[0]
                    elif attr <= 81:
                        t.settings[plane][x][y] = attr - 49
                    else:
                        t.underlay[plane][x][y] = attr - 81
    return t


def _write_smart(out: bytearray, value: int) -> None:
    if not 0 <= value < 32768:
        raise ValueError(f"smart out of range: {value}")
    if value < 128:
        out.append(value)
    else:
        out += struct.pack(">H", value + 32768)


def encode_locations(placements: Iterable[tuple[int, int, int]]) -> bytes:
    """Location file for (object id, packed plane/x/y, shape << 2 | rotation)."""
    by_id: dict[int, list[tuple[int, int]]] = {}
    for object_id, packed, info in placements:
        by_id.setdefault(object_id, []).append((packed, info))
    out = bytearray()
    previous_id = -1
    for object_id in sorted(by_id):
        delta = object_id - previous_id
        while delta >= 32767:
            _write_smart(out, 32767)
            delta -= 32767
        _write_smart(out, delta)
        previous_id = object_id
        previous_pos = 0
        for packed, info in sorted(by_id[object_id]):
            _write_smart(out, packed - previous_pos + 1)
            previous_pos = packed
            out.append(info)
        _write_smart(out, 0)
    _write_smart(out, 0)
    return bytes(out)


class Composition:
    """The composed mapsquares of one cache, built on demand and kept."""

    def __init__(self, store: object, copies: list[MapCopy], read_raw: RawReader):
        self.store = store
        self.copies = copies
        self.read_raw = read_raw
        self._terrain: dict[tuple[int, int], _Terrain] = {}
        self._offsets: list[int] | None = None
        self._files: dict[tuple[int, int, str], bytes] = {}
        self.mapsquares = set().union(*(c.mapsquares() for c in copies)) if copies else set()
        for region_x, region_y in sorted(self.mapsquares):
            if (read_raw(store, region_x, region_y, "terrain") is not None
                    or read_raw(store, region_x, region_y, "locations") is not None):
                raise ValueError(
                    f"composed mapsquare {region_x},{region_y} already exists in the cache")

    def _source_terrain(self, region_x: int, region_y: int) -> _Terrain:
        key = (region_x, region_y)
        if key not in self._terrain:
            data = self.read_raw(self.store, region_x, region_y, "terrain")
            if data is None:
                raise ValueError(f"composed source mapsquare {key} has no terrain")
            self._terrain[key] = decode_terrain(data, region_x, region_y)
        return self._terrain[key]

    def _check_sources(self) -> None:
        """A bridge flag would move a copied column to another plane."""
        for copy in self.copies:
            sx0, sy0, _ = copy.source
            for sx in range(sx0, sx0 + copy.size[0]):
                for sy in range(sy0, sy0 + copy.size[1]):
                    if self._source_terrain(sx >> 6, sy >> 6).settings[1][sx & 63][sy & 63] \
                            & BRIDGE_FLAG:
                        raise ValueError(
                            f"copy {copy.name}: source tile {sx},{sy} is under a bridge")

    def _source_height(self, x: int, y: int, plane: int) -> int:
        return self._source_terrain(x >> 6, y >> 6).heights[plane][x & 63][y & 63]

    def offsets(self) -> list[int]:
        """Each copy's height offset: a joined copy meets the one before it."""
        if self._offsets is None:
            self._offsets = []
            for i, copy in enumerate(self.copies):
                offset = 0
                if copy.join and i > 0:
                    prev = self.copies[i - 1]
                    xs = range(copy.join[0], copy.join[1] + 1)
                    top = prev.source[1] + prev.size[1] - 1
                    above = [self._source_height(prev.source[0] + lx, top, prev.source[2])
                             + self._offsets[i - 1] for lx in xs]
                    below = [self._source_height(copy.source[0] + lx, copy.source[1],
                                                 copy.source[2]) for lx in xs]
                    offset = HEIGHT_STEP * round(
                        (sum(above) - sum(below)) / len(xs) / HEIGHT_STEP)
                self._offsets.append(offset)
        return self._offsets

    def _copy_at(self, x: int, y: int, plane: int) -> int:
        for i, copy in enumerate(self.copies):
            if copy.covers(x, y, plane):
                return i
        return -1

    def _nearest_copy(self, x: int, y: int) -> int:
        """The copy within EDGE_REACH of an empty plane-0 tile, or -1."""
        best, best_distance = -1, EDGE_REACH + 1
        for i, copy in enumerate(self.copies):
            distance = copy.distance(x, y)
            if distance < best_distance:
                best, best_distance = i, distance
        return best

    def height(self, x: int, y: int) -> int | None:
        """Composed plane-0 height of a tile, or None for procedural."""
        i = self._copy_at(x, y, 0)
        if i < 0:
            i = self._nearest_copy(x, y)
            if i < 0:
                return None
        sx, sy, sp = self.copies[i].clamped_source(x, y)
        return self._source_height(sx, sy, sp) + self.offsets()[i]

    def file(self, region_x: int, region_y: int, kind: str) -> bytes:
        key = (region_x, region_y, kind)
        if key not in self._files:
            if not self._files:
                self._check_sources()
            self._files[key] = (self._terrain_file(region_x, region_y) if kind == "terrain"
                                else self._locations_file(region_x, region_y))
        return self._files[key]

    def _terrain_file(self, region_x: int, region_y: int) -> bytes:
        out = bytearray()
        for plane in range(4):
            for lx in range(64):
                for ly in range(64):
                    x, y = region_x * 64 + lx, region_y * 64 + ly
                    i = self._copy_at(x, y, plane)
                    if i >= 0:
                        sx, sy, sp = self.copies[i].source_of(x, y)
                        src = self._source_terrain(sx >> 6, sy >> 6)
                        tx, ty = sx & 63, sy & 63
                        if src.overlay_attr[sp][tx][ty]:
                            out += struct.pack(">HH", src.overlay_attr[sp][tx][ty],
                                               src.overlay_raw[sp][tx][ty])
                        if src.settings[sp][tx][ty]:
                            out += struct.pack(">H", 49 + src.settings[sp][tx][ty])
                        if src.underlay[sp][tx][ty]:
                            out += struct.pack(">H", 81 + src.underlay[sp][tx][ty])
                    height = self.height(x, y) if plane == 0 else None
                    if height is None:
                        out += b"\x00\x00"      # procedural height
                        continue
                    if not -255 * HEIGHT_STEP <= height <= 0 or height % HEIGHT_STEP:
                        raise ValueError(f"composed tile {x},{y}: height {height} "
                                         "cannot be stored")
                    h = -height // HEIGHT_STEP
                    out += struct.pack(">HB", 1, 0 if h == 1 else h)  # 1 decodes as 0
        return bytes(out)

    def _locations_file(self, region_x: int, region_y: int) -> bytes:
        placements = []
        for copy in self.copies:
            sx0, sy0, sp = copy.source
            regions = {(x >> 6, y >> 6)
                       for x in range(sx0, sx0 + copy.size[0])
                       for y in range(sy0, sy0 + copy.size[1])}
            for rx, ry in sorted(regions):
                data = self.read_raw(self.store, rx, ry, "locations")
                if data is None:
                    continue
                for loc in iter_location_placements(data, rx, ry):
                    if (loc.plane != sp or not sx0 <= loc.world_x < sx0 + copy.size[0]
                            or not sy0 <= loc.world_y < sy0 + copy.size[1]):
                        continue
                    x = copy.dest[0] + loc.world_x - sx0
                    y = copy.dest[1] + loc.world_y - sy0
                    if (x >> 6, y >> 6) != (region_x, region_y):
                        continue
                    packed = (copy.dest[2] << 12) | ((x & 63) << 6) | (y & 63)
                    placements.append((loc.object_id, packed,
                                       (loc.shape << 2) | loc.rotation))
        return encode_locations(placements)


def composition_for(store: object, read_raw: RawReader) -> Composition | None:
    """The composition of ``store`` (built once, kept on the store), or None."""
    attrs = getattr(store, "__dict__", {})
    if "_rc_composition" not in attrs:
        copies = load_copies(spec_path())
        attrs["_rc_composition"] = Composition(store, copies, read_raw) if copies else None
    return attrs["_rc_composition"]
