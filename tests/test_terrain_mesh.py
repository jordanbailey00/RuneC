#!/usr/bin/env python3
"""build_terrain_mesh against the client's floor rules: an untextured overlay
coloured 0xFF00FF is never drawn (a chasm's void), a full tile of it leaves a
hole and a shaped one keeps only its underlay part. A bridge keeps the ground
below it, and a wall on an empty tile takes its top from the walkway above."""

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
    return [rgb for rgb, _ in tile_layers(t, tx, ty)]


def tile_layers(t: export_terrain.RegionTerrain, tx: int, ty: int
                ) -> list[tuple[tuple[int, ...], float]]:
    """The colour and height of each triangle drawn on one tile, in order."""
    verts, colors = export_terrain.build_terrain_mesh({(50, 53): t}, UNDERLAYS, OVERLAYS)
    out = []
    for i in range(0, len(verts), 9):
        cx = (verts[i] + verts[i + 3] + verts[i + 6]) / 3 - 50 * 64
        cy = -(verts[i + 2] + verts[i + 5] + verts[i + 8]) / 3 - 53 * 64
        if tx < cx < tx + 1 and ty < cy < ty + 1:
            out.append((tuple(colors[i // 3 * 4:i // 3 * 4 + 3]), verts[i + 1]))
    return out


def raise_plane(t: export_terrain.RegionTerrain, plane: int, units: int) -> None:
    t.heights[plane] = [[-units] * 65 for _ in range(65)]


def bridge(t: export_terrain.RegionTerrain, tx: int, ty: int, overlay: int) -> None:
    t.settings[1][tx][ty] = 2
    t.underlay_ids[1][tx][ty] = 0
    t.overlay_ids[1][tx][ty] = overlay


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


class PaintedPlaneTests(unittest.TestCase):
    def test_hidden_bridge_tile_shows_the_ground_below(self) -> None:
        t = region()
        raise_plane(t, 1, 240)
        bridge(t, 10, 10, HIDDEN)
        self.assertEqual(tile_layers(t, 10, 10), tile_layers(region(), 10, 10))

    def test_bridge_draws_over_the_ground_it_keeps(self) -> None:
        t = region()
        raise_plane(t, 1, 240)
        bridge(t, 10, 10, VISIBLE)
        layers = tile_layers(t, 10, 10)
        self.assertEqual(layers[:2], tile_layers(region(), 10, 10))
        self.assertEqual(len(layers), 4)
        self.assertEqual({height for _, height in layers[2:]}, {240 / 128})
        self.assertNotEqual(layers[2][0], layers[0][0])

    def test_bridge_tile_lying_on_the_ground_replaces_it(self) -> None:
        t = region()
        raise_plane(t, 1, export_terrain.BRIDGE_MIN_CLEARANCE - 1)
        bridge(t, 10, 10, VISIBLE)
        self.assertEqual(len(tile_layers(t, 10, 10)), 2)

    def test_wall_on_an_empty_tile_takes_the_walkway_above(self) -> None:
        t = region()
        raise_plane(t, 1, 240)
        t.underlay_ids[0][10][10] = 0
        layers = tile_layers(t, 10, 10)
        self.assertEqual(len(layers), 2)
        self.assertEqual({height for _, height in layers}, {240 / 128})

    def test_hidden_tile_stays_a_hole_under_a_floor(self) -> None:
        t = region()
        raise_plane(t, 1, 240)
        t.overlay_ids[0][10][10] = HIDDEN
        self.assertEqual(tile_layers(t, 10, 10), [])

    def test_tile_empty_on_both_planes_stays_a_hole(self) -> None:
        t = region()
        t.underlay_ids[0][10][10] = t.underlay_ids[1][10][10] = 0
        self.assertEqual(tile_layers(t, 10, 10), [])

    def test_blocked_tile_under_a_floor_is_a_wall_topped_by_it(self) -> None:
        t = region()
        raise_plane(t, 1, export_terrain.WALL_MIN_HEIGHT)
        t.settings[0][10][10] = 1
        layers = tile_layers(t, 10, 10)
        self.assertEqual(layers[:2], tile_layers(region(), 10, 10))
        self.assertEqual({h for _, h in layers[2:]},
                         {export_terrain.WALL_MIN_HEIGHT / 128})

    def test_blocked_tile_under_a_low_floor_stays_ground(self) -> None:
        t = region()
        raise_plane(t, 1, export_terrain.WALL_MIN_HEIGHT - 1)
        t.settings[0][10][10] = 1
        self.assertEqual(tile_layers(t, 10, 10), tile_layers(region(), 10, 10))

    def test_roofed_blocked_tile_stays_ground(self) -> None:
        t = region()
        raise_plane(t, 1, 240)
        t.settings[0][10][10] = 5
        self.assertEqual(tile_layers(t, 10, 10), tile_layers(region(), 10, 10))

    def test_partial_floor_beside_a_wall_tops_its_diagonal_corner(self) -> None:
        t = region()
        raise_plane(t, 1, 240)
        t.settings[0][11][10] = 1
        t.underlay_ids[1][10][10] = 0
        t.overlay_ids[1][10][10] = VISIBLE
        t.shapes[1][10][10] = 1
        layers = tile_layers(t, 10, 10)
        self.assertEqual(layers[:2], tile_layers(region(), 10, 10))
        self.assertEqual({h for _, h in layers[2:]}, {240 / 128})

    def test_partial_floor_away_from_walls_stays_hidden(self) -> None:
        t = region()
        raise_plane(t, 1, 240)
        t.underlay_ids[1][10][10] = 0
        t.overlay_ids[1][10][10] = VISIBLE
        t.shapes[1][10][10] = 1
        self.assertEqual(tile_layers(t, 10, 10), tile_layers(region(), 10, 10))

    def test_painted_ground_ignores_the_plane_above(self) -> None:
        t = region()
        raise_plane(t, 1, 240)
        self.assertEqual({h for _, h in tile_layers(t, 10, 10)}, {0.0})


if __name__ == "__main__":
    unittest.main()
