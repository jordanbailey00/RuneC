#!/usr/bin/env python3
"""Export first-release viewer validation scene slices from b237."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from source_inputs import B237_CACHE, require_path

ROOT = Path(__file__).resolve().parents[2]


@dataclass(frozen=True)
class ValidationScene:
    key: str
    label: str
    center_x: int
    center_y: int
    plane: int
    radius_regions: int
    reason: str

    @property
    def origin_x(self) -> int:
        return ((self.center_x // 64) - self.radius_regions) * 64

    @property
    def origin_y(self) -> int:
        return ((self.center_y // 64) - self.radius_regions) * 64

    @property
    def prefix_name(self) -> str:
        return f"scene_{self.origin_x}_{self.origin_y}_r{self.radius_regions}"


VALIDATION_SCENES = (
    ValidationScene("graardor", "Graardor", 2872, 5358, 2, 1, "dev validation boss"),
    ValidationScene("kbd", "KBD", 2269, 4697, 0, 1, "dev validation boss"),
    ValidationScene("vorkath", "Vorkath", 2269, 4062, 0, 1, "dev validation boss"),
    ValidationScene("jad", "Jad", 2400, 5088, 0, 1, "dev validation boss"),
    ValidationScene("edgeville_dungeon", "Edgeville dungeon", 3117, 9852, 0, 1, "object transport smoke destination"),
    ValidationScene("varrock_rat_pits", "Varrock Rat Pits", 2894, 5097, 0, 1, "object transport smoke destination"),
    ValidationScene("varrock_sewer", "Varrock sewer", 3237, 9858, 0, 1, "object transport smoke destination"),
    ValidationScene("wilderness_lever", "Wilderness lever/coffin", 3154, 3924, 0, 1, "object transport smoke destination"),
    ValidationScene("observatory_ladder", "Observatory ladder", 2465, 3495, 1, 1, "object traversal smoke destination"),
    ValidationScene("yanille_railing", "Yanille railing", 2519, 3163, 0, 1, "skill traversal smoke destination"),
)


def clean_scene_outputs(output_dir: Path) -> None:
    for path in output_dir.iterdir():
        if path.is_file():
            path.unlink()


def run_scene_export(cache: Path, output_dir: Path, scene: ValidationScene) -> None:
    prefix = output_dir / scene.prefix_name
    args = [
        sys.executable,
        str(ROOT / "tools/cache_pipeline/export_scene_slice.py"),
        "--cache",
        str(cache),
        "--center-x",
        str(scene.center_x),
        "--center-y",
        str(scene.center_y),
        "--radius-regions",
        str(scene.radius_regions),
        "--output-prefix",
        str(prefix),
        "--planes",
        str(scene.plane),
    ]
    print("$ " + " ".join(args), flush=True)
    subprocess.run(args, cwd=ROOT, check=True)


def write_manifest(output_dir: Path, scenes: tuple[ValidationScene, ...]) -> None:
    payload = {
        "format": "runec-viewer-validation-scenes-v1",
        "scene_cache_dir": output_dir.relative_to(ROOT).as_posix(),
        "scenes": [
            {
                "key": scene.key,
                "label": scene.label,
                "center_x": scene.center_x,
                "center_y": scene.center_y,
                "plane": scene.plane,
                "radius_regions": scene.radius_regions,
                "origin_x": scene.origin_x,
                "origin_y": scene.origin_y,
                "prefix": scene.prefix_name,
                "reason": scene.reason,
            }
            for scene in scenes
        ],
    }
    (output_dir / "validation_scenes.json").write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=B237_CACHE)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=ROOT / "data/regions/scene_cache",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Remove previously generated files for this validation scene set before exporting.",
    )
    args = parser.parse_args()

    cache = require_path(args.cache, "--cache", "RUNEC_B237_CACHE")
    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    if args.clean:
        clean_scene_outputs(output_dir)
    for scene in VALIDATION_SCENES:
        run_scene_export(cache, output_dir, scene)
    write_manifest(output_dir, VALIDATION_SCENES)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
