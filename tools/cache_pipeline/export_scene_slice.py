#!/usr/bin/env python3
"""Generate a small plane-complete terrain/object viewer slice from b237."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from export_objects import export_modern_objects
from export_terrain import export_modern_terrain

DEFAULT_CACHE = Path(__file__).resolve().parent / "source/current_fightcaves_demo/data/cache"

def region_list(center_x: int, center_y: int, radius: int) -> list[tuple[int, int]]:
    rx = center_x // 64
    ry = center_y // 64
    return [
        (x, y)
        for x in range(rx - radius, rx + radius + 1)
        for y in range(ry - radius, ry + radius + 1)
    ]


def plane_path(prefix: Path, plane: int, suffix: str) -> Path:
    if plane == 0:
        return prefix.with_suffix(suffix)
    return prefix.with_name(f"{prefix.name}.p{plane}{suffix}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="export a generated viewer scene slice from the local b237 cache"
    )
    parser.add_argument("--center-x", type=int, required=True)
    parser.add_argument("--center-y", type=int, required=True)
    parser.add_argument("--radius-regions", type=int, default=2)
    parser.add_argument(
        "--cache",
        type=Path,
        default=DEFAULT_CACHE,
    )
    parser.add_argument("--output-prefix", type=Path, required=True)
    parser.add_argument("--planes", type=str, default="0,1,2,3")
    args = parser.parse_args()

    if not args.cache.exists():
        sys.exit(f"cache directory not found: {args.cache}")
    if args.radius_regions < 0:
        sys.exit("--radius-regions must be >= 0")

    planes = [int(p) for p in args.planes.split(",") if p.strip()]
    if any(p < 0 or p > 3 for p in planes):
        sys.exit("--planes values must be in 0..3")

    args.output_prefix.parent.mkdir(parents=True, exist_ok=True)
    regions = region_list(args.center_x, args.center_y, args.radius_regions)
    print(
        f"scene_slice: exporting {len(regions)} regions from {args.cache}",
        file=sys.stderr,
    )

    for plane in planes:
        terrain = plane_path(args.output_prefix, plane, ".terrain")
        objects = plane_path(args.output_prefix, plane, ".objects")
        print(f"scene_slice: plane {plane} terrain -> {terrain}", file=sys.stderr)
        export_modern_terrain(
            cache_dir=args.cache,
            regions=regions,
            output=terrain,
            scene_plane=plane,
        )
        print(f"scene_slice: plane {plane} objects -> {objects}", file=sys.stderr)
        export_modern_objects(
            cache_dir=args.cache,
            regions=regions,
            output=objects,
            scene_plane=plane,
            rsmod_visual_levels=True,
        )


if __name__ == "__main__":
    main()
