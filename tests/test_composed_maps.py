#!/usr/bin/env python3
"""Tests for composed mapsquares (tools/cache_pipeline/rc_cache/compose.py)."""

from __future__ import annotations

import os
import struct
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "cache_pipeline"))

import pack_runtime_data
from export_terrain import parse_terrain_full
from rc_cache import RcCacheStore, compose
from rc_cache.config import INDEX_MAPS
from rc_cache.locations import iter_location_placements
from rc_cache.maps import (
    find_all_map_region_files,
    find_map_region_files,
    map_composition,
    read_map_region_file,
)

SOURCE = (10, 10)                       # source mapsquare
SOURCE_GROUP = (SOURCE[0] << 8) | SOURCE[1]


def terrain_bytes(tiles: dict[tuple[int, int, int], tuple[int, ...]]) -> bytes:
    """Terrain file: (plane, x, y) -> (height byte, underlay, overlay attr,
    overlay id, settings); other tiles take procedural heights."""
    out = bytearray()
    for plane in range(4):
        for x in range(64):
            for y in range(64):
                tile = tiles.get((plane, x, y))
                if tile is None:
                    out += b"\x00\x00"
                    continue
                height, underlay, overlay_attr, overlay_id, settings = tile
                if overlay_attr:
                    out += struct.pack(">HH", overlay_attr, overlay_id)
                if settings:
                    out += struct.pack(">H", 49 + settings)
                if underlay:
                    out += struct.pack(">H", 81 + underlay)
                out += struct.pack(">HB", 1, height)
    return bytes(out)


def source_terrain() -> bytes:
    """Plane 0 at height byte 10; plane 1 has an 8x8 room 10 above it with a
    floor that rises by one byte per row, and a second flat room beside it."""
    tiles = {}
    for x in range(64):
        for y in range(64):
            tiles[(0, x, y)] = (10, 1, 0, 0, 0)
    for x in range(8):
        for y in range(8):
            tiles[(1, x, y)] = (10 + y, 2, 2 + 4 * (x % 3) + (y & 3), 7, 1 if x == 3 else 0)
            tiles[(1, 8 + x, y)] = (40, 3, 0, 0, 4)
    return terrain_bytes(tiles)


def source_locations() -> bytes:
    # (object id, packed plane/x/y, shape << 2 | rotation)
    return compose.encode_locations([
        (100, (1 << 12) | (2 << 6) | 3, (10 << 2) | 1),
        (100, (1 << 12) | (5 << 6) | 5, (22 << 2) | 0),
        (40000, (1 << 12) | (9 << 6) | 1, (0 << 2) | 2),
        (7, (0 << 12) | (2 << 6) | 3, 0),                  # plane 0: not copied
    ])


class FakeStore:
    """A cache with one unnamed mapsquare group (files 0 and 1)."""

    def __init__(self, groups: dict[int, dict[int, bytes]]):
        self.groups = groups

    def read_index_manifest(self, index: int) -> SimpleNamespace:
        assert index == INDEX_MAPS
        return SimpleNamespace(
            has_names=False,
            group_ids=sorted(self.groups),
            group_file_ids={g: sorted(files) for g, files in self.groups.items()},
        )

    def read_group(self, index: int, group_id: int) -> dict[int, bytes]:
        assert index == INDEX_MAPS
        return self.groups[group_id]


def write_spec(directory: Path, rows: str) -> Path:
    path = directory / "composed.toml"
    path.write_text(rows)
    return path


SPEC = """
[[copy]]
name = "low"
source = [640, 640, 1]
size = [8, 8]
dest = [1288, 1288, 0]

[[copy]]
name = "high"
source = [648, 640, 1]
size = [8, 8]
dest = [1288, 1296, 0]
join = [3, 4]
"""


class ComposeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.store = FakeStore({SOURCE_GROUP: {0: source_terrain(), 1: source_locations()}})

    def use_spec(self, rows: str) -> None:
        env = patch.dict(os.environ, {compose.SPEC_ENV: str(write_spec(Path(self.tmp.name), rows))})
        env.start()
        self.addCleanup(env.stop)

    def test_encode_locations_round_trips(self) -> None:
        placements = [
            (5, (0 << 12) | (1 << 6) | 2, (10 << 2) | 3),
            (5, (2 << 12) | (63 << 6) | 63, (4 << 2) | 1),
            (5, (0 << 12) | (1 << 6) | 9, 0),
            (70000, (1 << 12) | (30 << 6) | 31, (22 << 2) | 2),
        ]
        data = compose.encode_locations(placements)
        got = sorted(
            (loc.object_id, (loc.plane << 12) | (loc.local_x << 6) | loc.local_y,
             (loc.shape << 2) | loc.rotation)
            for loc in iter_location_placements(data, 3, 4))
        self.assertEqual(got, sorted(placements))
        self.assertEqual(list(iter_location_placements(compose.encode_locations([]), 0, 0)), [])

    def test_composed_squares_resolve_beside_the_cache(self) -> None:
        self.use_spec(SPEC)
        regions = find_all_map_region_files(self.store)
        self.assertEqual(sorted(regions), [SOURCE_GROUP, (20 << 8) | 20])
        entry = find_map_region_files(self.store, 20, 20)
        self.assertTrue(entry.composed and entry.has_terrain and entry.has_locations)
        self.assertFalse(find_map_region_files(self.store, *SOURCE).composed)
        self.assertEqual(read_map_region_file(self.store, *SOURCE, "m"), source_terrain())
        self.assertIsNone(read_map_region_file(self.store, 21, 20, "terrain"))
        with self.assertRaisesRegex(ValueError, "unknown map file kind"):
            read_map_region_file(self.store, 20, 20, "models")

    def test_copies_keep_tiles_and_locs_and_join_heights(self) -> None:
        self.use_spec(SPEC)
        src = parse_terrain_full(source_terrain(), 640, 640)
        out = parse_terrain_full(read_map_region_file(self.store, 20, 20, "terrain"), 1280, 1280)
        fields = ("underlay_ids", "overlay_ids", "shapes", "rotations", "settings")
        for copy, sx0, offset in (("low", 0, 0), ("high", 8, None)):
            dy0 = 8 if copy == "low" else 16
            for lx in range(8):
                for ly in range(8):
                    for name in fields:
                        self.assertEqual(getattr(out, name)[0][8 + lx][dy0 + ly],
                                         getattr(src, name)[1][sx0 + lx][ly], (copy, name))
        # Absolute source heights (plane 1 sits on plane 0) land on plane 0.
        for lx in range(8):
            for ly in range(8):
                self.assertEqual(out.heights[0][8 + lx][8 + ly], -(10 + 10 + ly) * 8)
        # The flat room moves so its join tiles meet the low room's top row.
        top = -(10 + 10 + 7) * 8
        for lx in range(8):
            self.assertEqual(out.heights[0][8 + lx][16], top)
        # Empty tiles repeat the nearest edge up to EDGE_REACH, then go procedural.
        self.assertEqual(out.heights[0][6][8], out.heights[0][8][8])
        self.assertEqual(out.heights[0][8][24 + 1], top)
        self.assertNotEqual(out.heights[0][5][8], out.heights[0][8][8])
        self.assertEqual(out.underlay_ids[0][6][8], 0)
        for plane in (1, 2, 3):
            self.assertFalse(any(out.underlay_ids[plane][x][y] or out.overlay_ids[plane][x][y]
                                 for x in range(64) for y in range(64)))

        locs = sorted((loc.object_id, loc.world_x, loc.world_y, loc.plane, loc.shape, loc.rotation)
                      for loc in iter_location_placements(
                          read_map_region_file(self.store, 20, 20, "locations"), 20, 20))
        self.assertEqual(locs, [
            (100, 1290, 1291, 0, 10, 1),
            (100, 1293, 1293, 0, 22, 0),
            (40000, 1289, 1297, 0, 0, 2),
        ])

    def test_rejects_bad_compositions(self) -> None:
        cases = {
            "overlap": SPEC.replace("dest = [1288, 1296, 0]", "dest = [1290, 1290, 0]"),
            "plane 0": SPEC.replace("dest = [1288, 1288, 0]", "dest = [1288, 1288, 1]"),
            "needs x, y, plane": SPEC.replace("size = [8, 8]", "size = [8]", 1),
            "already exists": SPEC.replace("dest = [1288, 1288, 0]", "dest = [640, 700, 0]"),
        }
        for message, rows in cases.items():
            with self.subTest(message):
                store = FakeStore(self.store.groups)
                self.use_spec(rows)
                with self.assertRaisesRegex(ValueError, message):
                    find_all_map_region_files(store)

        tiles = {(1, x, y): (10, 2, 0, 0, 0) for x in range(16) for y in range(8)}
        tiles[(1, 2, 2)] = (10, 2, 0, 0, compose.BRIDGE_FLAG)
        store = FakeStore({SOURCE_GROUP: {0: terrain_bytes(tiles), 1: source_locations()}})
        self.use_spec(SPEC)
        with self.assertRaisesRegex(ValueError, "under a bridge"):
            read_map_region_file(store, 20, 20, "terrain")

    def test_rejects_missing_sources_and_unstorable_heights(self) -> None:
        self.use_spec(SPEC.replace("source = [640, 640, 1]", "source = [64, 64, 1]"))
        with self.assertRaisesRegex(ValueError, "has no terrain"):
            read_map_region_file(FakeStore(self.store.groups), 20, 20, "terrain")

        # Two planes of 255 put the copy 4,080 units up; a byte holds 2,040.
        tiles = {(p, x, y): (255, 2, 0, 0, 0) for p in (0, 1) for x in range(16)
                 for y in range(8)}
        store = FakeStore({SOURCE_GROUP: {0: terrain_bytes(tiles)}})
        self.use_spec(SPEC)
        with self.assertRaisesRegex(ValueError, "cannot be stored"):
            read_map_region_file(store, 20, 20, "terrain")
        # A source without a locations file composes no locs.
        self.assertEqual(read_map_region_file(store, 20, 20, "locations"), b"\x00")

        with self.assertRaisesRegex(ValueError, "smart out of range"):
            compose._write_smart(bytearray(), 32768)

    def test_no_spec_means_no_composition(self) -> None:
        self.use_spec("")
        self.assertEqual(sorted(find_all_map_region_files(self.store)), [SOURCE_GROUP])
        self.assertEqual(compose.load_copies(Path(self.tmp.name) / "missing.toml"), [])
        with patch.dict(os.environ, {compose.SPEC_ENV: "none"}):
            self.assertIsNone(compose.spec_path())
        env = {k: v for k, v in os.environ.items() if k != compose.SPEC_ENV}
        with patch.dict(os.environ, env, clear=True):
            self.assertEqual(compose.spec_path(), compose.DEFAULT_SPEC)
        # A short terrain file leaves the remaining tiles empty.
        terrain = compose.decode_terrain(b"\x00\x01\x05", 0, 0)
        self.assertEqual(terrain.heights[0][0][0], -40)
        self.assertEqual(terrain.heights[0][0][1], 0)


class CoxLayoutSpecTests(unittest.TestCase):
    """content/world/composed_maps.toml: the fixed Chambers of Xeric layout."""

    def test_rooms_stack_north_from_the_start_room(self) -> None:
        copies = compose.load_copies(compose.DEFAULT_SPEC)
        self.assertEqual([c.name for c in copies], [
            "cox_start", "cox_tightrope", "cox_crabs", "cox_shamans",
            "cox_muttadiles", "cox_tekton", "cox_end",
        ])
        for i, copy in enumerate(copies):
            self.assertEqual(copy.size, (32, 32))
            self.assertEqual(copy.dest, (3472, 5440 + 32 * i, 0))
            self.assertEqual(copy.source[0], 3296)          # north-exit variants
            self.assertEqual(copy.join, None if i == 0 else (15, 16))
        squares = set().union(*(c.mapsquares() for c in copies))
        self.assertEqual(squares, {(54, 85), (54, 86), (54, 87), (54, 88)})
        self.assertEqual(pack_runtime_data.COMPOSED_MAPSQUARE_REGIONS, len(squares))

    @unittest.skipUnless(os.environ.get("RUNEC_B237_CACHE"), "needs RUNEC_B237_CACHE")
    def test_cache_rooms_copy_exactly(self) -> None:
        with patch.dict(os.environ, {compose.SPEC_ENV: str(compose.DEFAULT_SPEC)}):
            store = RcCacheStore(os.environ["RUNEC_B237_CACHE"])
            composition = map_composition(store)
            self.assertIsNotNone(composition)
            fields = ("underlay_ids", "overlay_ids", "shapes", "rotations", "settings")
            terrain = {}

            def tiles(rx: int, ry: int):
                if (rx, ry) not in terrain:
                    terrain[(rx, ry)] = parse_terrain_full(
                        read_map_region_file(store, rx, ry, "terrain"), rx * 64, ry * 64)
                return terrain[(rx, ry)]

            def locs(copy, origin, plane):
                x0, y0 = origin[0], origin[1]
                squares = {((x0 + dx) >> 6, (y0 + dy) >> 6) for dx in (0, 31) for dy in (0, 31)}
                return sorted(
                    (loc.object_id, loc.world_x - x0, loc.world_y - y0, loc.shape, loc.rotation)
                    for rx, ry in squares
                    for loc in iter_location_placements(
                        read_map_region_file(store, rx, ry, "locations"), rx, ry)
                    if loc.plane == plane and 0 <= loc.world_x - x0 < 32
                    and 0 <= loc.world_y - y0 < 32)

            for copy, offset in zip(composition.copies, composition.offsets()):
                sx0, sy0, sp = copy.source
                for lx in range(32):
                    for ly in range(32):
                        s = tiles((sx0 + lx) >> 6, (sy0 + ly) >> 6)
                        d = tiles((3472 + lx) >> 6, (copy.dest[1] + ly) >> 6)
                        sxy = ((sx0 + lx) & 63, (sy0 + ly) & 63)
                        dxy = ((3472 + lx) & 63, (copy.dest[1] + ly) & 63)
                        for name in fields:
                            self.assertEqual(getattr(d, name)[0][dxy[0]][dxy[1]],
                                             getattr(s, name)[sp][sxy[0]][sxy[1]])
                        self.assertEqual(d.heights[0][dxy[0]][dxy[1]],
                                         s.heights[sp][sxy[0]][sxy[1]] + offset)
                self.assertEqual(locs(copy, copy.dest, 0), locs(copy, copy.source, sp), copy.name)


if __name__ == "__main__":
    unittest.main()
