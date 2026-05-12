#!/usr/bin/env python3
"""Validate required rc-viewer UI sprite assets.

The viewer treats cache gameframe sprites as required and item icon PNGs as
optional fallbacks. This check deliberately follows the required `UI_ASSET(...)`
entries from `rc-viewer/ui_assets.c` so it stays in sync with the loader.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
UI_ASSET_RE = re.compile(r'UI_ASSET\("([^"]+)"\)')


def required_asset_names(source: Path) -> list[str]:
    text = source.read_text(encoding="utf-8")
    names: list[str] = []
    seen: set[str] = set()
    for match in UI_ASSET_RE.finditer(text):
        name = match.group(1)
        if name in seen:
            continue
        seen.add(name)
        names.append(name)
    return names


def is_png(path: Path) -> bool:
    try:
        with path.open("rb") as fp:
            return fp.read(len(PNG_SIGNATURE)) == PNG_SIGNATURE
    except OSError:
        return False


def transparent_pixel_count(path: Path) -> int:
    try:
        from PIL import Image
    except ImportError as exc:  # pragma: no cover
        raise SystemExit(
            "Pillow is required when --require-transparent is used"
        ) from exc

    with Image.open(path) as image:
        rgba = image.convert("RGBA")
        return sum(1 for pixel in rgba.getdata() if pixel[3] == 0)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source",
        type=Path,
        default=Path("rc-viewer/ui_assets.c"),
        help="UI asset loader source file",
    )
    parser.add_argument(
        "--assets-dir",
        type=Path,
        default=Path("data/sprites/ui"),
        help="Directory containing exported UI PNGs",
    )
    parser.add_argument(
        "--require-transparent",
        action="append",
        default=[],
        metavar="NAME",
        help=(
            "Require the named UI sprite, without .png, to contain transparent "
            "pixels. Can be passed multiple times."
        ),
    )
    args = parser.parse_args(argv)

    names = required_asset_names(args.source)
    missing: list[Path] = []
    bad_png: list[Path] = []
    bad_alpha: list[Path] = []

    for name in names:
        path = args.assets_dir / f"{name}.png"
        if not path.exists():
            missing.append(path)
        elif not is_png(path):
            bad_png.append(path)

    for name in args.require_transparent:
        path = args.assets_dir / f"{name}.png"
        if not path.exists():
            missing.append(path)
        elif not is_png(path):
            bad_png.append(path)
        elif transparent_pixel_count(path) <= 0:
            bad_alpha.append(path)

    for path in missing:
        print(f"missing required UI sprite: {path}", file=sys.stderr)
    for path in bad_png:
        print(f"invalid PNG UI sprite: {path}", file=sys.stderr)
    for path in bad_alpha:
        print(f"UI sprite has no transparent pixels: {path}", file=sys.stderr)

    valid = len(names) - len(missing) - len(bad_png)
    print(
        f"validated {valid}/{len(names)} required UI sprites in {args.assets_dir}"
    )
    if args.require_transparent:
        print(
            f"validated transparency for {len(args.require_transparent) - len(bad_alpha)}/"
            f"{len(args.require_transparent)} selected UI sprites"
        )
    return 1 if missing or bad_png or bad_alpha else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
