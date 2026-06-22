#!/usr/bin/env python3
"""Validate the promoted RuneC frontend defaults without touching core runtime."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools/frontend_validation"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate promoted OSRS frontend defaults and rollback paths."
    )
    parser.add_argument(
        "--bundle-dir",
        type=Path,
        default=Path("/tmp/runec-phase8-default-bundles"),
        help="Directory where scoped validation bundles are staged.",
    )
    parser.add_argument(
        "--link-mode",
        choices=("symlink", "copy"),
        default="symlink",
        help="Bundle staging mode passed to build_asset_bundle.py.",
    )
    parser.add_argument(
        "--with-screenshots",
        action="store_true",
        help="Run the GUI screenshot harness instead of only dry-running it.",
    )
    return parser.parse_args()


def run(cmd: list[str]) -> None:
    print(" ".join(cmd), flush=True)
    result = subprocess.run(cmd, cwd=ROOT, text=True)
    if result.returncode != 0:
        raise SystemExit(result.returncode)


def assert_viewer_default_source() -> None:
    source = (ROOT / "rc-viewer/viewer.c").read_text(encoding="utf-8")
    required = [
        'env_path("RUNEC_RENDER_PROFILE", "osrs")',
        "unknown RUNEC_RENDER_PROFILE=%s; using osrs",
        "RUNEC_RENDER_PROFILE_LEGACY",
    ]
    missing = [needle for needle in required if needle not in source]
    if missing:
        raise SystemExit(
            "viewer render-profile defaults are not promoted; missing: "
            + ", ".join(missing)
        )
    print("viewer default profile source check: ok", flush=True)


def build_bundle(manifest: str, output: Path, link_mode: str) -> None:
    run([
        sys.executable,
        str(TOOLS / "build_asset_bundle.py"),
        "--manifest",
        str(ROOT / manifest),
        "--output",
        str(output),
        "--link-mode",
        link_mode,
        "--force",
    ])


def main() -> int:
    args = parse_args()
    bundle_dir = args.bundle_dir.resolve()

    assert_viewer_default_source()
    run([
        sys.executable,
        str(TOOLS / "validate_combat_visuals.py"),
        "--preset",
        "fight_caves_jad",
        "--mode",
        "strict",
    ])
    build_bundle(
        "assets/manifests/fight_caves_jad.json",
        bundle_dir / "fight_caves_jad",
        args.link_mode,
    )
    build_bundle(
        "assets/manifests/varrock_base.json",
        bundle_dir / "varrock_base",
        args.link_mode,
    )

    screenshot_cmd = [
        sys.executable,
        str(TOOLS / "capture_regression_screenshots.py"),
        "--scene",
        "all",
        "--bundle-dir",
        str(bundle_dir),
        "--no-build",
        "--render-profile",
        "osrs",
        "--camera-preset",
        "osrs",
    ]
    if not args.with_screenshots:
        screenshot_cmd.append("--dry-run")
    run(screenshot_cmd)

    print("promoted frontend defaults validation: ok", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
