#!/usr/bin/env python3
"""Tests for aggregate and mapsquare-split visual export orchestration."""

from __future__ import annotations

import argparse
import io
import math
import struct
import sys
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "cache_pipeline"))

import export_models
import export_animations
import export_item_render_models
import export_minimap
import export_objects
import export_scene_slice
import export_terrain
import pack_runtime_data
import spawn_index
import stack_model_overlays
from rc_cache import decode_location_definition, decode_sprite_group
from export_textures import TextureAtlas


class MapsquareSceneTests(unittest.TestCase):
    def test_location_definition_retains_map_scene_id(self) -> None:
        definition = decode_location_definition(123, bytes((68, 0, 42, 0)))

        self.assertTrue(definition.complete)
        self.assertEqual(definition.map_scene_id, 42)

    def test_location_transform_retains_absent_fallback(self) -> None:
        definition = decode_location_definition(
            123,
            bytes((
                77,
                0xFF, 0xFF,
                0x00, 0x00,
                0,
                0x00, 0x7B,
                0,
            )),
        )

        self.assertTrue(definition.complete)
        self.assertEqual(definition.varbit, -1)
        self.assertEqual(definition.varp, 0)
        self.assertEqual(definition.transforms, [123, -1])

    def test_sprite_group_consumes_zero_sized_frame_flags(self) -> None:
        pixels = bytes((0, 0, 1))
        palette = bytes((0x11, 0x22, 0x33))
        metadata = bytes(
            (
                0, 1, 0, 1, 1,  # canvas 1x1, two palette entries
                0, 0, 0, 0,     # x offsets
                0, 0, 0, 0,     # y offsets
                0, 0, 0, 1,     # widths: 0, 1
                0, 0, 0, 1,     # heights: 0, 1
            )
        )
        sprites = decode_sprite_group(
            pixels + palette + metadata + bytes((0, 2))
        )

        self.assertEqual(len(sprites), 2)
        self.assertEqual(sprites[0].pixels, bytes(4))
        self.assertEqual(sprites[1].pixels, bytes((0x11, 0x22, 0x33, 0xFF)))

    def test_parsers_are_deterministic_and_validate_bounds(self) -> None:
        self.assertEqual(export_scene_slice.parse_planes("2,0,2"), [2, 0])
        self.assertEqual(
            export_scene_slice.plane_path(Path("scene"), 2, ".terrain"),
            Path("scene.p2.terrain"),
        )
        self.assertEqual(
            export_scene_slice.parse_regions("51,53 50,54 50,53 51,53"),
            [(50, 53), (51, 53), (50, 54)],
        )
        for raw in ("", "50", "256,53"):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                export_scene_slice.parse_regions(raw)
        for raw in ("", "4"):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                export_scene_slice.parse_planes(raw)

        regions = [(index, 53) for index in range(7)]
        self.assertEqual(
            [
                len(batch)
                for batch in export_scene_slice.partition_regions(regions, 3)
            ],
            [3, 2, 2],
        )
        self.assertEqual(
            export_scene_slice.partition_regions(regions[:2], 8),
            [regions[:1], regions[1:2]],
        )
        with self.assertRaisesRegex(ValueError, "jobs must be >= 1"):
            export_scene_slice.partition_regions(regions, 0)

    def test_cli_rejects_ambiguous_or_incomplete_output_contracts(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            cache = root / "cache"
            cache.mkdir()
            common = ["--cache", str(cache)]
            center = ["--center-x", "3213", "--center-y", "3428"]
            invalid = (
                [],
                ["--center-x", "3213"],
                [*center, "--regions", "50,53"],
                [*center, "--radius-regions", "-1"],
                [
                    *center,
                    "--jobs", "0",
                    "--split-by-mapsquare",
                    "--output-dir", str(root / "regions"),
                ],
                [
                    *center,
                    "--jobs", "2",
                    "--output-prefix", str(root / "scene"),
                ],
                [*center, "--planes", "4", "--output-prefix", str(root / "scene")],
                [*center, "--split-by-mapsquare"],
                [
                    *center,
                    "--split-by-mapsquare",
                    "--output-dir", str(root / "regions"),
                    "--output-prefix", str(root / "scene"),
                ],
                center,
                [
                    *center,
                    "--output-prefix", str(root / "scene"),
                    "--output-dir", str(root / "regions"),
                ],
            )
            for args in invalid:
                with (
                    self.subTest(args=args),
                    redirect_stderr(io.StringIO()),
                    self.assertRaises(SystemExit) as raised,
                ):
                    export_scene_slice.main([*common, *args])
                self.assertEqual(raised.exception.code, 2)

    def test_split_exporters_validate_inputs_before_cache_decode(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            cache = root / "cache"
            output = root / "regions"
            for exporter in (
                export_minimap.export_minimap_split,
                export_terrain.export_modern_terrain_split,
                export_objects.export_modern_objects_split,
            ):
                with self.subTest(exporter=exporter.__name__, case="cache"):
                    with self.assertRaises(FileNotFoundError):
                        exporter(cache, [(50, 53)], output)

            cache.mkdir()
            for exporter in (
                export_minimap.export_minimap_split,
                export_terrain.export_modern_terrain_split,
                export_objects.export_modern_objects_split,
            ):
                with self.subTest(exporter=exporter.__name__, case="regions"):
                    with self.assertRaises(ValueError):
                        exporter(cache, [], output)
                with self.subTest(exporter=exporter.__name__, case="planes"):
                    with self.assertRaises(ValueError):
                        exporter(cache, [(50, 53)], output, [])

    def test_split_mode_dispatches_one_bounded_batch(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            cache = root / "cache"
            output = root / "regions"
            cache.mkdir()
            with (
                patch(
                    "export_scene_slice.write_mapsquare_catalog",
                    return_value={(50, 53), (51, 53)},
                ) as catalog_export,
                patch(
                    "export_scene_slice.export_modern_terrain_split",
                    return_value=[output / "50_53.p0.terrain"],
                ) as terrain_export,
                patch(
                    "export_scene_slice.export_modern_objects_split",
                    return_value=[output / "50_53.p0.objects"],
                ) as object_export,
                patch(
                    "export_scene_slice.export_minimap_split",
                    return_value=[output / "50_53.p0.minimap.png"],
                ) as minimap_export,
            ):
                export_scene_slice.main([
                    "--cache", str(cache),
                    "--regions", "51,53 50,53",
                    "--planes", "0,2",
                    "--split-by-mapsquare",
                    "--output-dir", str(output),
                ])

            expected_regions = [(50, 53), (51, 53)]
            catalog_export.assert_called_once_with(cache, output)
            terrain_export.assert_called_once_with(cache, expected_regions, output, [0, 2])
            object_export.assert_called_once_with(
                cache,
                expected_regions,
                output,
                [0, 2],
                rsmod_visual_levels=True,
                write_shared_materials=True,
            )
            minimap_export.assert_called_once_with(
                cache, expected_regions, output, [0, 2]
            )

    def test_split_mode_skips_authoritative_void_mapsquares(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            cache = root / "cache"
            output = root / "regions"
            cache.mkdir()
            present = {(28, 81), (29, 81)}
            with (
                patch(
                    "export_scene_slice.write_mapsquare_catalog",
                    return_value=present,
                ),
                patch(
                    "export_scene_slice.export_modern_terrain_split",
                    return_value=[],
                ) as terrain_export,
                patch(
                    "export_scene_slice.export_modern_objects_split",
                    return_value=[],
                ) as object_export,
                patch(
                    "export_scene_slice.export_minimap_split",
                    return_value=[],
                ) as minimap_export,
            ):
                export_scene_slice.main([
                    "--cache", str(cache),
                    "--regions", "28,81 29,81 30,81",
                    "--planes", "0",
                    "--split-by-mapsquare",
                    "--output-dir", str(output),
                ])

            expected = [(28, 81), (29, 81)]
            terrain_export.assert_called_once_with(cache, expected, output, [0])
            object_export.assert_called_once_with(
                cache,
                expected,
                output,
                [0],
                rsmod_visual_levels=True,
                write_shared_materials=True,
            )
            minimap_export.assert_called_once_with(cache, expected, output, [0])

    def test_parallel_split_dispatches_disjoint_bounded_batches(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            cache = root / "cache"
            output = root / "regions"
            cache.mkdir()
            regions = [(48, 53), (49, 53), (50, 53), (51, 53), (52, 53)]
            submitted: list[tuple[object, ...]] = []
            worker_counts: list[int] = []

            class ImmediateFuture:
                def result(self) -> tuple[int, int]:
                    return 4, 9

            class ImmediateExecutor:
                def __init__(self, max_workers: int):
                    worker_counts.append(max_workers)

                def __enter__(self) -> "ImmediateExecutor":
                    return self

                def __exit__(self, *_args: object) -> None:
                    return None

                def submit(self, _call: object, *args: object) -> ImmediateFuture:
                    submitted.append(args)
                    return ImmediateFuture()

            with (
                patch(
                    "export_scene_slice.write_mapsquare_catalog",
                    return_value=set(regions),
                ),
                patch("export_scene_slice.ProcessPoolExecutor", ImmediateExecutor),
                patch("export_scene_slice.as_completed", side_effect=lambda values: values),
            ):
                export_scene_slice.main([
                    "--cache", str(cache),
                    "--regions", " ".join(f"{x},{y}" for x, y in regions),
                    "--planes", "0,1,2,3",
                    "--jobs", "2",
                    "--split-by-mapsquare",
                    "--output-dir", str(output),
                ])

            self.assertEqual(len(submitted), 2)
            self.assertEqual(worker_counts, [2])
            self.assertEqual(submitted[0][1], regions[:3])
            self.assertEqual(submitted[1][1], regions[3:])
            self.assertTrue(submitted[0][-1])
            self.assertFalse(submitted[1][-1])

    def test_mapsquare_catalog_is_sorted_and_marks_terrain_regions(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            cache = root / "cache"
            output = root / "regions"
            cache.mkdir()
            entries = {
                1: argparse.Namespace(
                    region_x=30, region_y=81, has_terrain=False),
                2: argparse.Namespace(
                    region_x=29, region_y=81, has_terrain=True),
                3: argparse.Namespace(
                    region_x=28, region_y=80, has_terrain=True),
            }
            with patch(
                "export_scene_slice.find_all_map_region_files",
                return_value=entries,
            ):
                present = export_scene_slice.write_mapsquare_catalog(
                    cache, output)

            payload = (output / "mapsquare.catalog").read_bytes()
            magic, count = struct.unpack_from("<II", payload)
            coordinates = [
                struct.unpack_from("<BB", payload, 8 + index * 2)
                for index in range(count)
            ]
            self.assertEqual(magic, export_scene_slice.MAPSQUARE_CATALOG_MAGIC)
            self.assertEqual(coordinates, [(28, 80), (29, 81)])
            self.assertEqual(present, {(28, 80), (29, 81)})

    def test_split_mode_stops_cleanly_when_every_requested_region_is_void(
        self,
    ) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            cache = root / "cache"
            output = root / "regions"
            cache.mkdir()
            with (
                patch(
                    "export_scene_slice.write_mapsquare_catalog",
                    return_value=set(),
                ),
                patch(
                    "export_scene_slice.export_modern_terrain_split",
                ) as terrain_export,
                patch(
                    "export_scene_slice.export_modern_objects_split",
                ) as object_export,
            ):
                export_scene_slice.main([
                    "--cache", str(cache),
                    "--regions", "30,81",
                    "--planes", "0",
                    "--split-by-mapsquare",
                    "--output-dir", str(output),
                ])

            terrain_export.assert_not_called()
            object_export.assert_not_called()

    def test_aggregate_mode_remains_available_as_dev_cache(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            cache = root / "cache"
            prefix = root / "scene"
            cache.mkdir()
            with (
                patch("export_scene_slice.export_modern_terrain") as terrain_export,
                patch("export_scene_slice.export_modern_objects") as object_export,
            ):
                export_scene_slice.main([
                    "--cache", str(cache),
                    "--center-x", "3213",
                    "--center-y", "3428",
                    "--radius-regions", "0",
                    "--planes", "0",
                    "--output-prefix", str(prefix),
                ])

            terrain_export.assert_called_once_with(
                cache_dir=cache,
                regions=[(50, 53)],
                output=prefix.with_suffix(".terrain"),
                scene_plane=0,
            )
            object_export.assert_called_once_with(
                cache_dir=cache,
                regions=[(50, 53)],
                output=prefix.with_suffix(".objects"),
                scene_plane=0,
                rsmod_visual_levels=True,
            )


class PlayerWeaponPoseTests(unittest.TestCase):
    @staticmethod
    def weapon(
        item_id: int,
        name: str,
    ) -> export_item_render_models.ItemRenderDef:
        return export_item_render_models.ItemRenderDef(
            item_id=item_id,
            name=name,
            flags=export_item_render_models.IDEF_HAS_EQUIPMENT,
            linked_id_item=-1,
            linked_id_noted=-1,
            linked_id_placeholder=-1,
            ground_model_id=1,
            male_model_ids=(2, -1, -1),
            female_model_ids=(3, -1, -1),
            equip_slot=export_item_render_models.EQUIP_WEAPON,
        )

    def test_reviewed_pose_source_matches_b237_items(self) -> None:
        items = export_item_render_models.parse_items_bin(
            ROOT / "data/defs/items.bin"
        )
        poses = export_item_render_models.load_player_pose_sets(
            ROOT / "content/items/player_pose_sets.toml",
            items,
        )

        self.assertEqual(len(poses), 77)
        self.assertEqual(poses[11804], (7053, 7052, 7043))
        self.assertEqual(poses[4718], (2065, 2064, -1))
        self.assertEqual(poses[12904], (813, 1205, 1210))
        self.assertEqual(poses[19481], (7220, 7223, 7221))
        self.assertEqual(poses[27275], (9494, 1703, 1707))
        self.assertNotIn(839, poses)  # Longbows intentionally use human defaults.

    def test_generated_pose_map_and_player_animations_are_complete(self) -> None:
        items = export_item_render_models.parse_items_bin(
            ROOT / "data/defs/items.bin"
        )
        expected = export_item_render_models.load_player_pose_sets(
            ROOT / "content/items/player_pose_sets.toml",
            items,
        )

        raw = (ROOT / "data/models/item_render.map").read_bytes()
        magic, version, count, body_count = struct.unpack_from("<IIII", raw, 0)
        self.assertEqual(magic, export_item_render_models.IREM_MAGIC)
        self.assertEqual(version, export_item_render_models.IREM_V2)
        pos = 16 + body_count * 4
        actual: dict[int, tuple[int, int, int]] = {}
        for _ in range(count):
            row = struct.unpack_from("<13I", raw, pos)
            pos += 13 * 4
            if row[0] in expected:
                actual[row[0]] = tuple(
                    -1 if value == export_item_render_models.MISSING_U32 else value
                    for value in row[10:13]
                )
        self.assertEqual(actual, expected)

        required = export_animations.read_item_render_sequence_ids(
            ROOT / "data/models/item_render.map"
        )
        player_anims = (ROOT / "data/anims/player.anims").read_bytes()
        self.assertEqual(player_anims[:4], b"ANM2")
        _version, header_size, framebase_count, sequence_count = (
            struct.unpack_from("<HHII", player_anims, 4)
        )
        pos = header_size
        for _ in range(framebase_count):
            pos += 2
            slot_count = player_anims[pos]
            pos += 1 + slot_count
            for _slot in range(slot_count):
                map_length = player_anims[pos]
                pos += 1 + map_length

        available: set[int] = set()
        for _ in range(sequence_count):
            sequence_id, frame_count = struct.unpack_from(
                "<HH", player_anims, pos
            )
            available.add(sequence_id)
            pos += 4
            interleave_count = player_anims[pos]
            pos += 1 + interleave_count + 1
            for _frame in range(frame_count):
                pos += 4
                translator_count = player_anims[pos]
                pos += 1 + translator_count * 7
        self.assertEqual(required - available, set())

    def test_npc_attack_animation_table_is_strict_and_exportable(self) -> None:
        path = ROOT / "content/npc_attack_anims/animations.tsv"
        required = export_animations.read_npc_attack_sequence_ids(path)
        self.assertGreater(len(required), 250)
        self.assertIn(6489, required)
        self.assertIn(422, required)

        with TemporaryDirectory() as tmp:
            malformed = Path(tmp) / "npc_attack_anims.tsv"
            malformed.write_text(
                "stand_anim|attack_anim|note\n"
                "6487|6489|curated:b237:test\n"
                "6487|422|curated:b237:duplicate\n"
            )
            with self.assertRaisesRegex(ValueError, "invalid NPC attack"):
                export_animations.read_npc_attack_sequence_ids(malformed)

    def test_pose_source_rejects_name_drift_and_duplicates(self) -> None:
        items = {11804: self.weapon(11804, "Bandos godsword")}
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "poses.toml"
            path.write_text(
                """schema_version = 1
[[pose]]
name = "godsword"
ready_anim = 7053
walk_anim = 7052
run_anim = 7043
items = [{ id = 11804, name = "Wrong godsword" }]
"""
            )
            with self.assertRaisesRegex(ValueError, "expected name"):
                export_item_render_models.load_player_pose_sets(path, items)

            path.write_text(
                """schema_version = 1
[[pose]]
name = "first"
ready_anim = 7053
items = [{ id = 11804, name = "Bandos godsword" }]
[[pose]]
name = "second"
walk_anim = 7052
items = [{ id = 11804, name = "Bandos godsword" }]
"""
            )
            with self.assertRaisesRegex(ValueError, "duplicate player pose item"):
                export_item_render_models.load_player_pose_sets(path, items)

    def test_pose_source_and_cache_sequence_failures_are_explicit(self) -> None:
        items = {11804: self.weapon(11804, "Bandos godsword")}
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "poses.toml"
            with self.assertRaisesRegex(ValueError, "does not exist"):
                export_item_render_models.load_player_pose_sets(path, items)

            path.write_text(
                """schema_version = 1
[[pose]]
name = "godsword"
ready_anim = 7053
walk_anim = 7052
run_anim = 7043
items = [{ id = 11804, name = "Bandos godsword" }]
"""
            )
            poses = export_item_render_models.load_player_pose_sets(path, items)
            with self.assertRaisesRegex(ValueError, "missing from B237 cache"):
                export_item_render_models.validate_player_pose_sequences(
                    path, poses, {7053, 7052}
                )


class TerrainBoundaryTests(unittest.TestCase):
    @staticmethod
    def region(region_x: int, region_y: int) -> export_terrain.RegionTerrain:
        terrain = export_terrain.RegionTerrain(region_x=region_x, region_y=region_y)
        terrain.heights = [
            [[0 for _ in range(65)] for _ in range(65)] for _ in range(4)
        ]
        terrain.underlay_ids = [
            [[0 for _ in range(64)] for _ in range(64)] for _ in range(4)
        ]
        terrain.overlay_ids = [
            [[0 for _ in range(64)] for _ in range(64)] for _ in range(4)
        ]
        terrain.shapes = [
            [[0 for _ in range(64)] for _ in range(64)] for _ in range(4)
        ]
        terrain.rotations = [
            [[0 for _ in range(64)] for _ in range(64)] for _ in range(4)
        ]
        terrain.settings = [
            [[0 for _ in range(64)] for _ in range(64)] for _ in range(4)
        ]
        return terrain

    def test_underlay_blending_crosses_mapsquare_boundary(self) -> None:
        west = self.region(50, 53)
        east = self.region(51, 53)
        for x in range(64):
            for y in range(64):
                west.underlay_ids[0][x][y] = 1
                east.underlay_ids[0][x][y] = 2

        west_floor = export_terrain.FloorDef(floor_id=0, rgb=0x397A3D)
        east_floor = export_terrain.FloorDef(floor_id=1, rgb=0x3D597A)
        export_terrain._rgb_to_hsl(west_floor.rgb, west_floor)
        export_terrain._rgb_to_hsl(east_floor.rgb, east_floor)
        floors = {0: west_floor, 1: east_floor}

        isolated_west = export_terrain._lit_underlay_rgb(
            {(50, 53): west}, 50, 53, 0, 63, 32, floors, 96)
        isolated_east = export_terrain._lit_underlay_rgb(
            {(51, 53): east}, 51, 53, 0, 0, 32, floors, 96)
        context = {(50, 53): west, (51, 53): east}
        blended_west = export_terrain._lit_underlay_rgb(
            context, 50, 53, 0, 63, 32, floors, 96)
        blended_east = export_terrain._lit_underlay_rgb(
            context, 51, 53, 0, 0, 32, floors, 96)

        self.assertIsNotNone(isolated_west)
        self.assertIsNotNone(isolated_east)
        self.assertIsNotNone(blended_west)
        self.assertIsNotNone(blended_east)
        isolated_delta = sum(
            (a - b) ** 2 for a, b in zip(isolated_west, isolated_east)
        )
        blended_delta = sum(
            (a - b) ** 2 for a, b in zip(blended_west, blended_east)
        )
        self.assertLess(blended_delta, isolated_delta)

    def test_streamed_heightmap_contains_outer_corner_samples(self) -> None:
        local = self.region(50, 53)
        east = self.region(51, 53)
        north = self.region(50, 54)
        northeast = self.region(51, 54)
        for plane in range(4):
            for y in range(65):
                east.heights[plane][0][y] = -256
            for x in range(65):
                north.heights[plane][x][0] = -384
            northeast.heights[plane][0][0] = -512

        context = {
            (50, 53): local,
            (51, 53): east,
            (50, 54): north,
            (51, 54): northeast,
        }
        min_x, min_y, width, height, values = export_terrain.build_heightmap(
            {(50, 53): local}, context_regions=context)

        self.assertEqual((min_x, min_y), (3200, 3392))
        self.assertEqual((width, height), (65, 65))
        self.assertEqual(values[64 + 10 * width], 2.0)
        self.assertEqual(values[10 + 64 * width], 3.0)
        self.assertEqual(values[64 + 64 * width], 4.0)

    def test_streamed_heightmap_retains_local_edge_without_neighbor(self) -> None:
        local = self.region(50, 53)
        local.heights[0][64][10] = -128
        local.heights[0][10][64] = -256
        local.heights[0][64][64] = -384

        _, _, width, height, values = export_terrain.build_heightmap(
            {(50, 53): local})

        self.assertEqual((width, height), (65, 65))
        self.assertEqual(values[64 + 10 * width], 1.0)
        self.assertEqual(values[10 + 64 * width], 2.0)
        self.assertEqual(values[64 + 64 * width], 3.0)
        self.assertEqual(
            export_terrain._context_height(
                {(50, 53): local}, local, 50, 53, 0, -1, 10),
            local.heights[0][0][10],
        )


class SharedMaterialTests(unittest.TestCase):
    @staticmethod
    def atlas() -> TextureAtlas:
        return TextureAtlas(
            width=1,
            height=1,
            pixels=b"\xff\xff\xff\xff",
            white_u=0.5,
            white_v=0.5,
        )

    def test_model_writer_can_reference_an_external_shared_atlas(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            output = root / "chunk.object_anim.models"
            export_models.write_models_binary(
                output,
                [],
                atlas=self.atlas(),
                write_atlas_companion=False,
            )
            magic, count = struct.unpack_from("<II", output.read_bytes())
            self.assertEqual(magic, export_models.MDL3_MAGIC)
            self.assertEqual(count, 0)
            self.assertFalse(output.with_suffix(".atlas").exists())

            companion_output = root / "default.models"
            export_models.write_models_binary(
                companion_output,
                [],
                atlas=self.atlas(),
            )
            self.assertTrue(companion_output.with_suffix(".atlas").is_file())

    def test_empty_chunk_keeps_oanim_contract_without_material_copy(self) -> None:
        with TemporaryDirectory() as tmp:
            output = Path(tmp) / "50_53.p3.objects"
            with (
                patch("export_objects.stitch_region_edges") as stitch_edges,
                patch("export_objects.build_heightmap") as build_heightmap,
            ):
                export_objects._build_and_write(
                    argparse.Namespace(output=output),
                    [],
                    {},
                    lambda _model_id: None,
                    {(50, 53): object()},
                    {},
                    self.atlas(),
                    prebuilt_heightmaps={0: (3200, 3392, 1, 1, [0.0])},
                    write_anim_atlas=False,
                    always_write_anim_sidecar=True,
                    verbose=False,
                )
                stitch_edges.assert_not_called()
                build_heightmap.assert_not_called()

            object_magic = struct.unpack_from("<I", output.read_bytes())[0]
            anim_magic, version, count = struct.unpack(
                "<III", output.with_suffix(".oanim").read_bytes()
            )
            self.assertEqual(object_magic, export_objects.OBJ2_MAGIC)
            self.assertEqual(anim_magic, export_objects.OANM_MAGIC)
            self.assertEqual(version, export_objects.OANM_VERSION)
            self.assertEqual(count, 0)
            self.assertFalse(output.with_suffix(".atlas").exists())
            self.assertFalse(output.with_suffix(".object_anim.atlas").exists())

    def test_resource_sidecar_tracks_replacement_and_disappearance(self) -> None:
        model = export_models.ModelData(
            model_id=10,
            vertex_count=3,
            face_count=1,
            vertices_x=[0, 128, 0],
            vertices_y=[0, 0, 0],
            vertices_z=[0, 0, 128],
            face_a=[0],
            face_b=[1],
            face_c=[2],
            face_colors=[0],
        )
        base = export_objects.LocDef(
            obj_id=1,
            model_ids=[10],
            model_types=[10],
            has_typed_models=True,
        )
        replacement = export_objects.LocDef(
            obj_id=2,
            model_ids=[10],
            model_types=[10],
            has_typed_models=True,
        )
        placement = export_objects.PlacedObject(
            obj_id=1,
            world_x=3200,
            world_y=3200,
            obj_type=10,
        )

        expanded, rows, models = export_objects.process_placements(
            [placement],
            {1: base, 2: replacement},
            lambda model_id: model if model_id == 10 else None,
            {},
            dynamic_behaviors={
                1: export_objects.DynamicObjectBehavior(
                    2, export_objects.OBHV_RESOURCE),
            },
            gathering_placements={(1, 3200, 3200, 0, 10, 0)},
            report_gaps=False,
        )
        self.assertEqual(expanded, [])
        self.assertEqual(len(models), 2)
        self.assertEqual([row.obj_id for row in rows], [1, 2])
        self.assertEqual(
            [row.flags for row in rows],
            [export_objects.OANM_FLAG_DYNAMIC_BASE,
             export_objects.OANM_FLAG_DYNAMIC_REPLACEMENT],
        )

        expanded, rows, models = export_objects.process_placements(
            [placement],
            {1: base},
            lambda model_id: model if model_id == 10 else None,
            {},
            dynamic_behaviors={
                1: export_objects.DynamicObjectBehavior(
                    -1, export_objects.OBHV_RESOURCE),
            },
            gathering_placements={(1, 3200, 3200, 0, 10, 0)},
            report_gaps=False,
        )
        self.assertEqual(expanded, [])
        self.assertEqual(len(models), 1)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0].obj_id, 1)
        self.assertEqual(rows[0].flags,
                         export_objects.OANM_FLAG_DYNAMIC_BASE)

        expanded, rows, models = export_objects.process_placements(
            [placement],
            {1: base},
            lambda model_id: model if model_id == 10 else None,
            {},
            dynamic_behaviors={
                1: export_objects.DynamicObjectBehavior(
                    -1, export_objects.OBHV_RESOURCE),
            },
            report_gaps=False,
        )
        self.assertEqual(len(expanded), 1)
        self.assertEqual(rows, [])
        self.assertEqual(models, [])

    def test_gathering_placements_fail_open_on_malformed_data(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "gathering_nodes.bin"
            path.write_bytes(struct.pack("<III", 0x444F4E47, 2, 1))
            with self.assertRaisesRegex(ValueError, "expected 44 bytes"):
                export_objects.load_gathering_placements(path)

    def test_dynamic_behaviors_fail_open_on_malformed_data(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "object_behaviors.bin"
            path.write_bytes(struct.pack("<III", 0x5648424F, 2, 1))
            with self.assertRaisesRegex(ValueError, "expected 40 bytes"):
                export_objects.load_dynamic_behaviors(path)

    def test_packer_classifies_chunk_and_shared_material_files(self) -> None:
        names = (
            "50_53.p0.terrain",
            "50_53.p0.objects",
            "50_53.p0.oanim",
            "50_53.p0.object_anim.models",
            "50_53.p0.minimap.png",
            "mapsquare.materials.atlas",
            "mapsquare.materials.tanim",
            "mapsquare.catalog",
        )
        for name in names:
            with self.subTest(name=name):
                self.assertTrue(pack_runtime_data.is_region_runtime_file(Path(name)))
        for name in (
            "varrock.terrain",
            "scene_cache/50_53.p0.terrain",
            "scene_cache/scene_3136_3392_r1.terrain",
            "varrock_chunks/49_53.objects",
        ):
            with self.subTest(name=name):
                self.assertFalse(
                    pack_runtime_data.is_region_runtime_file(Path(name)))


class RuntimePackMapsquareTests(unittest.TestCase):
    @staticmethod
    def write_fixture(root: Path) -> Path:
        regions = root / "regions"
        regions.mkdir()
        (regions / "mapsquare.catalog").write_bytes(
            struct.pack("<II", pack_runtime_data.MAPSQUARE_CATALOG_MAGIC, 1)
            + struct.pack("<BB", 50, 53)
        )
        (regions / "mapsquare.materials.atlas").write_bytes(b"atlas")
        (regions / "mapsquare.materials.tanim").write_bytes(b"tanim")
        for plane in range(4):
            stem = regions / f"50_53.p{plane}"
            stem.with_suffix(f".p{plane}.terrain").write_bytes(b"terrain")
            stem.with_suffix(f".p{plane}.objects").write_bytes(b"objects")
            stem.with_suffix(f".p{plane}.oanim").write_bytes(
                struct.pack(
                    "<III",
                    pack_runtime_data.MAPSQUARE_OANIM_MAGIC,
                    pack_runtime_data.MAPSQUARE_OANIM_VERSION,
                    0,
                )
            )
            (regions / f"50_53.p{plane}.minimap.png").write_bytes(b"png")
        return regions

    def test_release_pack_requires_every_catalog_plane(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            regions = self.write_fixture(root)
            summary = pack_runtime_data.validate_mapsquare_runtime_assets(
                root, allow_partial=False)
            self.assertEqual(summary["catalog_regions"], 1)
            self.assertEqual(summary["expected_catalog_regions"], 1)
            self.assertEqual(summary["required_region_planes"], 4)
            self.assertEqual(summary["complete_region_planes"], 4)
            self.assertTrue(summary["complete"])
            self.assertEqual(summary["scope"], "full")

            (regions / "50_53.p2.objects").unlink()
            with self.assertRaisesRegex(
                SystemExit, "incomplete prebuilt mapsquare runtime data"
            ):
                pack_runtime_data.validate_mapsquare_runtime_assets(
                    root, allow_partial=False)
            partial = pack_runtime_data.validate_mapsquare_runtime_assets(
                root, allow_partial=True)
            self.assertFalse(partial["complete"])
            self.assertEqual(partial["scope"], "partial")
            self.assertEqual(partial["missing_asset_count"], 1)
            self.assertEqual(partial["complete_region_planes"], 3)

            with self.assertRaisesRegex(
                SystemExit, "incomplete mapsquare catalog"
            ):
                pack_runtime_data.validate_mapsquare_runtime_assets(
                    root,
                    allow_partial=False,
                    expected_catalog_regions=2950,
                )

    def test_spawn_pack_includes_npcs_and_static_ground_items(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            spawns = root / "spawns"
            spawns.mkdir()
            (spawns / "world.npc-spawns.indexed.bin").write_bytes(b"npcs")
            (spawns / "world.ground-items.indexed.bin").write_bytes(b"items")
            specs = pack_runtime_data.build_specs(root)
            spawn_spec = next(spec for spec in specs if spec.stem == "runec-spawns")
            self.assertEqual(
                {
                    pack_runtime_data.logical_path(root, path)
                    for path in spawn_spec.files
                },
                {
                    "spawns/world.npc-spawns.indexed.bin",
                    "spawns/world.ground-items.indexed.bin",
                },
            )

    def test_indexed_assets_are_stored_for_pack_range_reads(self) -> None:
        self.assertEqual(
            pack_runtime_data.compression_for(
                Path("world.npc-spawns.indexed.bin"), set(), 6
            ),
            "store",
        )
        self.assertEqual(
            pack_runtime_data.compression_for(Path("npc_defs.bin"), set(), 6),
            "zlib",
        )

    def test_spawn_pack_validation_reads_indexed_files(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            spawns = root / "spawns"
            spawn_index.write_npc_spawns(
                spawns / "world.npc-spawns.indexed.bin",
                [(10, 3200, 3200, 0, 2, 0, 0)],
            )
            spawn_index.write_ground_item_spawns(
                spawns / "world.ground-items.indexed.bin",
                [(995, 1, 3200, 3200, 0, 0, 100)],
            )
            self.assertEqual(
                pack_runtime_data.validate_spawn_runtime_assets(root),
                {
                    "npc_rows": 1,
                    "npc_mapsquares": 1,
                    "ground_item_rows": 1,
                    "ground_item_mapsquares": 1,
                },
            )

    def test_animated_object_plane_requires_model_sidecar(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            regions = self.write_fixture(root)
            (regions / "50_53.p1.oanim").write_bytes(
                struct.pack(
                    "<III",
                    pack_runtime_data.MAPSQUARE_OANIM_MAGIC,
                    pack_runtime_data.MAPSQUARE_OANIM_VERSION,
                    1,
                )
            )
            with self.assertRaisesRegex(
                SystemExit, "50_53.p1.object_anim.models"
            ):
                pack_runtime_data.validate_mapsquare_runtime_assets(
                    root, allow_partial=False)
            (regions / "50_53.p1.object_anim.models").write_bytes(b"models")
            summary = pack_runtime_data.validate_mapsquare_runtime_assets(
                root, allow_partial=False)
            self.assertEqual(summary["animated_model_assets"], 1)

    def test_catalog_must_be_exact_sorted_and_unique(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "mapsquare.catalog"
            with self.assertRaisesRegex(SystemExit, "missing mapsquare catalog"):
                pack_runtime_data.load_mapsquare_catalog(path)
            path.write_bytes(b"short")
            with self.assertRaisesRegex(SystemExit, "truncated mapsquare catalog"):
                pack_runtime_data.load_mapsquare_catalog(path)
            path.write_bytes(struct.pack("<II", 0, 0))
            with self.assertRaisesRegex(SystemExit, "invalid mapsquare catalog"):
                pack_runtime_data.load_mapsquare_catalog(path)
            path.write_bytes(
                struct.pack("<II", pack_runtime_data.MAPSQUARE_CATALOG_MAGIC, 2)
                + struct.pack("<BBBB", 50, 53, 49, 53)
            )
            with self.assertRaisesRegex(SystemExit, "sorted and unique"):
                pack_runtime_data.load_mapsquare_catalog(path)

    def test_pack_validation_rejects_missing_shared_and_unknown_regions(
        self,
    ) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "regions").mkdir()
            with self.assertRaisesRegex(
                SystemExit, "missing mapsquare runtime assets"
            ):
                pack_runtime_data.validate_mapsquare_runtime_assets(
                    root, allow_partial=True)

        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            regions = self.write_fixture(root)
            (regions / "51_53.p0.terrain").write_bytes(b"terrain")
            with self.assertRaisesRegex(SystemExit, "absent from the catalog"):
                pack_runtime_data.validate_mapsquare_runtime_assets(
                    root, allow_partial=True)

    def test_pack_validation_rejects_malformed_animation_sidecars(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            regions = self.write_fixture(root)
            oanim = regions / "50_53.p0.oanim"
            oanim.write_bytes(b"short")
            with self.assertRaisesRegex(
                SystemExit, "truncated mapsquare animation sidecar"
            ):
                pack_runtime_data.validate_mapsquare_runtime_assets(
                    root, allow_partial=False)
            oanim.write_bytes(struct.pack("<III", 0, 0, 0))
            with self.assertRaisesRegex(
                SystemExit, "invalid mapsquare animation sidecar"
            ):
                pack_runtime_data.validate_mapsquare_runtime_assets(
                    root, allow_partial=False)
            oanim.unlink()
            oanim.mkdir()
            with self.assertRaisesRegex(
                SystemExit, "cannot read mapsquare animation sidecar"
            ):
                pack_runtime_data.mapsquare_oanim_count(oanim)


def tri_model(model_id, verts, faces, priorities=None, render_types=None):
    xs, ys, zs = zip(*verts)
    return export_models.ModelData(
        model_id=model_id,
        vertex_count=len(verts),
        face_count=len(faces),
        vertices_x=list(xs),
        vertices_y=list(ys),
        vertices_z=list(zs),
        face_a=[f[0] for f in faces],
        face_b=[f[1] for f in faces],
        face_c=[f[2] for f in faces],
        face_colors=[0] * len(faces),
        face_priorities=list(priorities or [0] * len(faces)),
        face_render_types=list(render_types or []),
    )


class SceneryPlacementTests(unittest.TestCase):
    """Client placement rules and depth-buffer ordering for scenery exports."""

    def assertPartsEqual(self, actual, expected) -> None:
        self.assertEqual(len(actual), len(expected))
        for (rot, x, y), (e_rot, e_x, e_y) in zip(actual, expected):
            self.assertEqual(rot, e_rot)
            self.assertAlmostEqual(x, e_x, places=6)
            self.assertAlmostEqual(y, e_y, places=6)

    def test_wall_decor_parts_match_client_renderables(self) -> None:
        parts = export_objects.placement_parts
        front = export_objects.WALL_DECOR_FRONT
        diag = front / math.sqrt(2.0)
        self.assertPartsEqual(parts(4, 1, 99), [(1, 0.0, -front)])
        self.assertPartsEqual(parts(5, 2, None), [(2, -(16 + front), 0.0)])
        self.assertPartsEqual(parts(5, 0, 32), [(0, 32 + front, 0.0)])
        self.assertPartsEqual(parts(6, 0, None), [(4, 8 + diag, -(8 + diag))])
        self.assertPartsEqual(parts(7, 0, 32), [(6, -diag, diag)])
        self.assertPartsEqual(
            parts(8, 1, 32),
            [(5, -(16 + diag), -(16 + diag)), (7, diag, diag)],
        )
        self.assertPartsEqual(parts(2, 3, None), [(7, 0.0, 0.0), (0, 0.0, 0.0)])
        self.assertPartsEqual(parts(10, 2, 32), [(2, 0.0, 0.0)])

    def test_diagonal_orientation_turns_45_degrees_after_mirroring(self) -> None:
        model = tri_model(1, [(64, 0, 0), (-64, 0, 0), (0, 0, 64)], [(0, 1, 2)])
        export_objects.orient_loc_model(
            model, export_objects.LocDef(obj_id=1), 6, 4)
        # Mirrored (orientation > 3), then sine-table 256 turn and (45, -45).
        self.assertEqual(model.vertices_x, [90, -1, -1])
        self.assertEqual(model.vertices_z, [-91, 0, -91])
        self.assertEqual((model.face_a, model.face_c), ([2], [0]))

    def test_resize_follows_rotation_and_truncates_like_java(self) -> None:
        model = tri_model(1, [(10, 0, 0), (0, 0, 0), (-5, -1, 0)], [(0, 1, 2)])
        loc = export_objects.LocDef(obj_id=1, model_size_x=256)
        export_objects.orient_loc_model(model, loc, 10, 1)
        # Rotation first: x 10 -> z -10, untouched by the x resize.
        self.assertEqual((model.vertices_x[0], model.vertices_z[0]), (0, -10))
        model = tri_model(1, [(-5, 0, 0), (0, 0, 0), (0, -1, 0)], [(0, 1, 2)])
        export_objects.scale_model(model, 100, 128, 128)
        self.assertEqual(model.vertices_x[0], -3)

    def test_base_height_averages_corners_around_footprint_centre(self) -> None:
        hm = (3200, 3200, 4, 4, [float(i) for i in range(16)])
        height = export_objects.placement_base_height
        self.assertAlmostEqual(height(hm, 3200, 3200, 1, 1), 2.5)
        self.assertAlmostEqual(height(hm, 3200, 3200, 2, 2), 5.0)
        self.assertAlmostEqual(height(hm, 3200, 3200, 3, 1), 3.5)

    def test_ground_decor_lift_covers_twist_of_touching_tiles(self) -> None:
        heights = [0.0] * 9
        heights[0] = 0.4  # SW corner of tile (0, 0) only
        hm = (0, 0, 3, 3, heights)
        lift = export_objects.ground_decor_lift
        base = export_objects.GROUND_DECOR_LIFT
        self.assertAlmostEqual(lift(hm, 0.5, 0.5), base + 0.1)
        self.assertAlmostEqual(lift(hm, 1.0, 0.5), base + 0.1)
        self.assertAlmostEqual(lift(hm, 1.5, 0.5), base)

    def test_overlay_offsets_move_only_stacked_faces(self) -> None:
        model = tri_model(
            1,
            [(0, 0, 0), (100, 0, 0), (0, 0, 100),        # base, priority 0
             (10, 0, 10), (40, 0, 10), (10, 0, 40),     # detail on base
             (12, 0, 12), (30, 0, 12), (12, 0, 30),     # detail on detail
             (100, 0, 0), (100, 0, 100), (0, 0, 100),   # shares an edge only
             (10, -2, 10), (40, -2, 10), (10, -2, 40)], # 2 units clear
            [(0, 1, 2), (3, 4, 5), (6, 7, 8), (9, 10, 11), (12, 13, 14)],
            priorities=[0, 1, 2, 1, 1],
        )
        sep = export_models.OVERLAY_SEPARATION
        offsets = export_models.overlay_face_offsets(model)
        self.assertEqual(offsets, [0.0, sep, 2 * sep, 0.0, 0.0])

        verts, _colors, _uvs = export_models.expand_model(model)
        self.assertEqual(verts[1], 0.0)
        self.assertAlmostEqual(verts[10], sep)
        self.assertAlmostEqual(verts[19], 2 * sep)
        flat, _colors, _uvs = export_models.expand_model(
            model, bake_priority_offsets=False)
        self.assertEqual(flat[10], 0.0)

        model.face_render_types = [2, 0, 0, 0, 0]
        self.assertEqual(export_models.overlay_face_offsets(model),
                         [0.0, 0.0, sep, 0.0, 0.0])

        # Out-of-range indices and zero-area faces never stack.
        model.face_render_types = []
        model.face_a[1], model.face_b[2], model.face_c[2] = 99, 6, 6
        self.assertEqual(export_models.overlay_face_offsets(model),
                         [0.0, 0.0, 0.0, 0.0, 0.0])

    def test_placements_push_wall_decor_by_the_walls_displacement(self) -> None:
        decor_model = tri_model(
            20, [(-64, 0, 0), (-64, -10, 0), (-64, 0, 10)], [(0, 1, 2)])
        wall_model = tri_model(
            21, [(-64, 0, -64), (-64, -100, -64), (-64, 0, 64)], [(0, 1, 2)])
        models = {20: decor_model, 21: wall_model}
        decor = export_objects.LocDef(
            obj_id=2, model_ids=[20], model_types=[4], has_typed_models=True,
            decor_offset=64)
        wall = export_objects.LocDef(
            obj_id=1, model_ids=[21], model_types=[0], has_typed_models=True,
            decor_offset=32)
        po_decor = export_objects.PlacedObject(
            obj_id=2, world_x=3200, world_y=3200, obj_type=5)
        po_wall = export_objects.PlacedObject(
            obj_id=1, world_x=3200, world_y=3200, obj_type=0)
        front = export_objects.WALL_DECOR_FRONT
        edge = 3200 + 0.5 - 64 / 128

        def decor_x(placements, defs) -> float:
            expanded, _rows, _models = export_objects.process_placements(
                placements, defs, models.get, {}, report_gaps=False)
            return [p for p in expanded if p.obj_type == 5][0].vertices[0]

        # The wall's displacement wins whichever loads first; the
        # decoration's own value is never used.
        for order in ([po_decor, po_wall], [po_wall, po_decor]):
            self.assertAlmostEqual(decor_x(order, {1: wall, 2: decor}),
                                   edge + (32 + front) / 128, places=5)
        self.assertAlmostEqual(decor_x([po_decor], {2: decor}),
                               edge + (16 + front) / 128, places=5)

        animated = export_objects.LocDef(
            obj_id=3, model_ids=[20], model_types=[4], has_typed_models=True,
            animation_id=7)
        po_animated = export_objects.PlacedObject(
            obj_id=3, world_x=3200, world_y=3200, obj_type=5)
        _expanded, rows, _models = export_objects.process_placements(
            [po_animated, po_wall], {1: wall, 3: animated}, models.get, {},
            report_gaps=False)
        self.assertAlmostEqual(rows[0].pos_x, 3200.5 + (32 + front) / 128,
                               places=5)

        # Content-declared replacement states share the base's pushed parts.
        _expanded, rows, _models = export_objects.process_placements(
            [po_decor, po_wall], {1: wall, 2: decor, 3: animated},
            models.get, {},
            dynamic_behaviors={2: export_objects.DynamicObjectBehavior(
                -1, 0, replacements=(3,), runtime=True)},
            report_gaps=False)
        self.assertEqual([row.obj_id for row in rows], [2, 3])
        self.assertEqual(rows[0].pos_x, rows[1].pos_x)
        self.assertAlmostEqual(rows[1].pos_x, 3200.5 + (32 + front) / 128,
                               places=5)

    def test_spawn_templates_lift_ground_decor(self) -> None:
        import export_spawn_objects
        loc = export_objects.LocDef(obj_id=1, width=2, length=1, offset_h=64)
        self.assertEqual(export_spawn_objects.template_position(loc, 10, 1),
                         (0.5, 0.5, -1.0))
        x, y, z = export_spawn_objects.template_position(loc, 22, 0)
        self.assertEqual((x, z), (1.0, -0.5))
        self.assertAlmostEqual(y, 0.5 + export_objects.GROUND_DECOR_LIFT)

    def test_ground_decor_sits_above_terrain(self) -> None:
        model = tri_model(30, [(-64, 0, -64), (64, 0, -64), (-64, 0, 64)],
                          [(0, 1, 2)])
        loc = export_objects.LocDef(
            obj_id=4, model_ids=[30], model_types=[22], has_typed_models=True)
        placement = export_objects.PlacedObject(
            obj_id=4, world_x=3200, world_y=3200, obj_type=22)
        expanded, _rows, _models = export_objects.process_placements(
            [placement], {4: loc}, {30: model}.get, {},
            heightmaps={0: (3200, 3200, 3, 3, [0.0] * 9)}, report_gaps=False)
        self.assertEqual(
            expanded[0].vertices[1::3],
            [export_objects.GROUND_DECOR_LIFT] * 3)

        animated = export_objects.LocDef(
            obj_id=5, model_ids=[30], model_types=[22], has_typed_models=True,
            animation_id=7)
        _expanded, rows, _models = export_objects.process_placements(
            [export_objects.PlacedObject(
                obj_id=5, world_x=3200, world_y=3200, obj_type=22)],
            {5: animated}, {30: model}.get, {}, report_gaps=False)
        self.assertAlmostEqual(rows[0].pos_y, export_objects.GROUND_DECOR_LIFT)


def stacked_models() -> list[export_models.ModelData]:
    """A base face with a detail stacked on it, and a face beside them."""
    models = []
    for model_id in (7, 8, 9):
        model = tri_model(
            model_id,
            [(0, 0, 0), (100, 0, 0), (0, 0, 100),
             (10, 0, 10), (40, 0, 10), (10, 0, 40),
             (100, 0, 0), (100, 0, 100), (0, 0, 100)],
            [(0, 1, 2), (3, 4, 5), (6, 7, 8)],
            priorities=[0, 1, 1],
        )
        model.vertex_skins = [0] * model.vertex_count
        model.face_skins = [1, 2, 3]
        model.face_alphas = [0, 0, 0]
        models.append(model)
    return models


def mof1_entries(record: bytes) -> list[tuple[int, float]]:
    at = record.rfind(struct.pack("<I", export_models.MOF1_MAGIC))
    if at < 0:
        return []
    count = struct.unpack_from("<I", record, at + 4)[0]
    return [struct.unpack_from("<HxxF".replace("F", "f"), record, at + 8 + i * 8)
            for i in range(count)]


def bundle_records(data: bytes) -> list[bytes]:
    _magic, count = struct.unpack_from("<II", data, 0)
    offsets = list(struct.unpack_from(f"<{count}I", data, 8)) + [len(data)]
    return [data[offsets[i]:offsets[i + 1]] for i in range(count)]


class StackedFaceModelTests(unittest.TestCase):
    """Stacked-face offsets in .models bundles (MOF1) for posed models."""

    def write(self, path: Path, models, bake: bool = True, atlas=None) -> bytes:
        export_models.write_models_binary(
            path, models, atlas=atlas, write_atlas_companion=False,
            bake_priority_offsets=bake)
        return path.read_bytes()

    def test_writer_bakes_and_records_stacked_face_offsets(self) -> None:
        sep = export_models.OVERLAY_SEPARATION
        with TemporaryDirectory() as tmp:
            data = self.write(Path(tmp) / "set.models", stacked_models()[:1])
            record = bundle_records(data)[0]
            self.assertEqual(mof1_entries(record), [(1, sep)])
            verts = struct.unpack_from("<27f", record, 10)
            self.assertEqual(verts[1], 0.0)
            self.assertAlmostEqual(verts[10], sep)
            self.assertEqual(verts[19], 0.0)

            plain = self.write(Path(tmp) / "plain.models", stacked_models()[:1],
                               bake=False)
            self.assertEqual(mof1_entries(bundle_records(plain)[0]), [])

    def test_tool_rewrites_old_bundles_like_the_writer(self) -> None:
        atlas = SharedMaterialTests.atlas()
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name, set_atlas in (("mdl2", None), ("mdl3", atlas)):
                models = stacked_models()
                models[2].face_render_types = [2, 0, 0]  # hidden base
                expected = self.write(root / f"{name}.expected.models", models,
                                      atlas=set_atlas)
                old = root / f"{name}.models"
                self.write(old, stacked_models()[:2] + [models[2]], bake=False,
                           atlas=set_atlas)

                stats = stack_model_overlays.stack_bundle(old)
                self.assertEqual(old.read_bytes(), expected)
                self.assertEqual(stats.changed_models, [7, 8])
                self.assertEqual((stats.stacked, stats.moved, stats.unbiased),
                                 (2, 2, 0))
                again = stack_model_overlays.stack_bundle(old)
                self.assertEqual(again.changed_models, [])
                self.assertEqual(old.read_bytes(), expected)

    def test_tool_drops_older_uniform_priority_bias(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "biased.models"
            expected = self.write(path, stacked_models())
            data = bytearray(expected)
            first = struct.unpack_from("<I", data, 8)[0]
            face2_y = first + 10 + (2 * 9 + 1) * 4
            struct.pack_into("<f", data, face2_y, 0.15)
            for jobs in (1, 2):
                path.write_bytes(bytes(data))
                stats = stack_model_overlays.stack_bundle(path, jobs=jobs)
                self.assertEqual(path.read_bytes(), expected)
                self.assertEqual((stats.moved, stats.unbiased), (1, 1))

            path.write_bytes(struct.pack("<II", 0x4D4F444C, 0))
            with self.assertRaisesRegex(ValueError, "not an MDL2/MDL3"):
                stack_model_overlays.stack_bundle(path)

    def test_tool_keeps_zero_triangles_and_rejects_malformed_records(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "odd.models"
            models = stacked_models()[:2]
            models[0].face_c[2] = 99  # out of range
            models[1].face_a[2] = -1  # stored as vertex 0
            expected = self.write(path, models)
            stats = stack_model_overlays.stack_bundle(path)
            self.assertEqual((stats.changed_models, stats.degenerate), ([], 1))
            self.assertEqual(path.read_bytes(), expected)

            record = bundle_records(expected)[0]
            bad = bytearray(record)
            struct.pack_into("<H", bad, 4, 8)  # expanded vertex count
            with self.assertRaisesRegex(ValueError, "expanded vertices"):
                stack_model_overlays.stack_model_record(bytes(bad), False)
            with self.assertRaisesRegex(ValueError, "stray bytes"):
                stack_model_overlays.stack_model_record(record + b"\0\0\0", False)

            path.write_bytes(expected + struct.pack("<II", 0x58585858, 0))
            with self.assertRaisesRegex(ValueError, "unknown block"):
                stack_model_overlays.stack_bundle(path)

    def test_tool_cli_checks_directories_without_writing(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "sub").mkdir()
            path = root / "sub" / "set.models"
            old = self.write(path, stacked_models(), bake=False)
            out = io.StringIO()
            with redirect_stdout(out):
                stack_model_overlays.main([str(root), "--check"])
            self.assertEqual(path.read_bytes(), old)
            self.assertIn("would change 1/1 bundles: 3/3 models", out.getvalue())
            with redirect_stdout(io.StringIO()):
                stack_model_overlays.main([str(path)])
            expected = self.write(root / "x.models", stacked_models())
            self.assertEqual(path.read_bytes(), expected)

            other = root / "sub" / "other.models"
            self.write(other, stacked_models(), bake=False)
            path.write_bytes(old)
            out = io.StringIO()
            with redirect_stdout(out):
                stack_model_overlays.main([str(root / "sub"), "--jobs", "2"])
            self.assertIn("changed 2/2 bundles: 6/6 models", out.getvalue())
            self.assertEqual(path.read_bytes(), expected)
            self.assertEqual(other.read_bytes(), expected)


if __name__ == "__main__":
    unittest.main()
