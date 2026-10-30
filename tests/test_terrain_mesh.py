#!/usr/bin/env python3
"""build_terrain_mesh against the client's floor rules: an untextured overlay
coloured 0xFF00FF is never drawn (a chasm's void), a full tile of it leaves a
hole and a shaped one keeps only its underlay part."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "cache_pipeline"))

import export_terrain

GRASS = 0x397A3D
SLATE = 0x3D597A


def floor(floor_id: int, rgb: int, texture: int = -1) -> export_terrain.FloorDef:
    flo = export_terrain.FloorDef(floor_id=floor_id, rgb=rgb, texture=texture)
    export_terrain._rgb_to_hsl(rgb, flo)
    return flo


UNDERLAYS = {0: floor(0, GRASS)}
OVERLAYS = {0: floor(0, SLATE), 1: floor(1, 0xFF00FF)}
VISIBLE, HIDDEN = 1, 2   # overlay ids as the map stores them (definition + 1)


def region() -> export_terrain.RegionTerrain:
    t = export_terrain.RegionTerrain(region_x=50, region_y=53)
    t.heights = [[[0] * 65 for _ in range(65)] for _ in range(4)]
    t.underlay_ids = [[[1] * 64 for _ in range(64)] for _ in range(4)]
    t.overlay_ids = [[[0] * 64 for _ in range(64)] for _ in range(4)]
    t.shapes = [[[0] * 64 for _ in range(64)] for _ in range(4)]
    t.rotations = [[[0] * 64 for _ in range(64)] for _ in range(4)]
    t.settings = [[[0] * 64 for _ in range(64)] for _ in range(4)]
    return t


def tile_triangles(t: export_terrain.RegionTerrain, tx: int, ty: int) -> list[tuple[int, ...]]:
    """The colours of the triangles drawn on one tile."""
    verts, colors = export_terrain.build_terrain_mesh({(50, 53): t}, UNDERLAYS, OVERLAYS)
    out = []
    for i in range(0, len(verts), 9):
        cx = (verts[i] + verts[i + 3] + verts[i + 6]) / 3 - 50 * 64
        cy = -(verts[i + 2] + verts[i + 5] + verts[i + 8]) / 3 - 53 * 64
        if tx < cx < tx + 1 and ty < cy < ty + 1:
            out.append(tuple(colors[i // 3 * 4:i // 3 * 4 + 3]))
    return out


class HiddenOverlayTests(unittest.TestCase):
    def test_only_untextured_magenta_is_hidden(self) -> None:
        self.assertTrue(export_terrain.overlay_hidden(OVERLAYS[1]))
        self.assertFalse(export_terrain.overlay_hidden(OVERLAYS[0]))
        self.assertFalse(export_terrain.overlay_hidden(None))
        self.assertFalse(export_terrain.overlay_hidden(floor(2, 0xFF00FF, texture=3)))

    def test_visible_overlay_draws_over_the_underlay(self) -> None:
        t = region()
        t.overlay_ids[0][10][10] = VISIBLE
        self.assertEqual(len(tile_triangles(t, 10, 10)), 2)
        self.assertEqual(len(tile_triangles(t, 11, 10)), 2)

    def test_hidden_full_tile_is_a_hole(self) -> None:
        t = region()
        t.overlay_ids[0][10][10] = HIDDEN
        self.assertEqual(tile_triangles(t, 10, 10), [])
        self.assertEqual(len(tile_triangles(t, 11, 10)), 2)

    def test_hidden_shaped_tile_keeps_its_underlay_part(self) -> None:
        t = region()
        t.overlay_ids[0][20][20] = HIDDEN
        t.shapes[0][20][20] = 1   # scene shape 2: one underlay, one overlay triangle
        drawn = tile_triangles(t, 20, 20)
        plain = tile_triangles(region(), 20, 20)
        self.assertEqual(len(drawn), 1)
        self.assertEqual(drawn[0], plain[0])

    def test_hidden_overlay_without_underlay_draws_nothing(self) -> None:
        t = region()
        t.underlay_ids[0][30][30] = 0
        t.overlay_ids[0][30][30] = HIDDEN
        t.shapes[0][30][30] = 1
        self.assertEqual(tile_triangles(t, 30, 30), [])


if __name__ == "__main__":
    unittest.main()
