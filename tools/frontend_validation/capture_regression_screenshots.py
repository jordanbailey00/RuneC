#!/usr/bin/env python3
"""Capture deterministic frontend regression screenshots from scoped bundles."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
BUILD_BUNDLE = ROOT / "tools/frontend_validation/build_asset_bundle.py"

SCENES = {
    "fight_caves_jad": {
        "manifest": ROOT / "assets/manifests/fight_caves_jad.json",
        "bundle": Path("/tmp/runec-fight-caves-jad-bundle"),
    },
    "varrock_base": {
        "manifest": ROOT / "assets/manifests/varrock_base.json",
        "bundle": Path("/tmp/runec-varrock-base-bundle"),
    },
}

CAMERA_PRESETS = {
    "osrs": {"pitch": 0.6, "dist": 50.0, "fov": 45.0},
    "fight_caves": {"pitch": 0.8, "dist": 30.0, "fov": 45.0},
    "wide": {"pitch": 0.7, "dist": 80.0, "fov": 45.0},
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Capture scoped RuneC frontend regression screenshots."
    )
    parser.add_argument(
        "--scene",
        action="append",
        choices=tuple(SCENES) + ("all",),
        default=[],
        help="Scene to capture. May be repeated. Defaults to all scenes.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=ROOT / "build/visual-regression/screenshots",
        help="Directory for screenshots, logs, and reports.",
    )
    parser.add_argument(
        "--bundle-dir",
        type=Path,
        default=None,
        help="Optional directory for generated scene bundles.",
    )
    parser.add_argument(
        "--no-build",
        action="store_true",
        help="Use existing bundles instead of rebuilding them first.",
    )
    parser.add_argument(
        "--link-mode",
        choices=("symlink", "copy"),
        default="symlink",
        help="Bundle staging mode passed to build_asset_bundle.py.",
    )
    parser.add_argument(
        "--repeat",
        type=int,
        default=1,
        help="Capture each selected scene this many times.",
    )
    parser.add_argument(
        "--compare-repeat",
        action="store_true",
        help="Fail if repeated captures for a scene produce different hashes.",
    )
    parser.add_argument(
        "--baseline",
        type=Path,
        help="Compare first capture for each scene against a baseline JSON.",
    )
    parser.add_argument(
        "--write-baseline",
        type=Path,
        help="Write a baseline JSON from the first capture for each scene.",
    )
    parser.add_argument(
        "--render-profile",
        default="osrs",
        help="RUNEC_RENDER_PROFILE used for each capture.",
    )
    parser.add_argument(
        "--color-lift",
        choices=("default", "0", "1"),
        default="default",
        help="Optional RUNEC_COLOR_LIFT override.",
    )
    parser.add_argument(
        "--camera-preset",
        choices=tuple(CAMERA_PRESETS),
        default="osrs",
        help="Named camera preset for repeatable framing.",
    )
    parser.add_argument("--camera-pitch", type=float)
    parser.add_argument("--camera-dist", type=float)
    parser.add_argument("--camera-fov", type=float)
    parser.add_argument(
        "--exit-frames",
        type=int,
        default=2,
        help="RC_VIEWER_EXIT_FRAMES for the capture run.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print planned bundle/capture commands without running them.",
    )
    return parser.parse_args()


def selected_scene_names(args: argparse.Namespace) -> list[str]:
    requested = args.scene or ["all"]
    if "all" in requested:
        return list(SCENES)
    seen: set[str] = set()
    names: list[str] = []
    for name in requested:
        if name not in seen:
            seen.add(name)
            names.append(name)
    return names


def camera_settings(args: argparse.Namespace) -> dict[str, float]:
    preset = dict(CAMERA_PRESETS[args.camera_preset])
    if args.camera_pitch is not None:
        preset["pitch"] = args.camera_pitch
    if args.camera_dist is not None:
        preset["dist"] = args.camera_dist
    if args.camera_fov is not None:
        preset["fov"] = args.camera_fov
    return preset


def scene_bundle_path(args: argparse.Namespace, scene_name: str) -> Path:
    if args.bundle_dir:
        return args.bundle_dir.resolve() / scene_name
    return SCENES[scene_name]["bundle"]


def run_command(cmd: list[str], *, env: dict[str, str] | None = None,
                log_path: Path | None = None,
                dry_run: bool = False) -> None:
    if dry_run:
        print(" ".join(cmd))
        if env:
            interesting = {
                key: env[key] for key in sorted(env)
                if key.startswith("RUNEC_") or key.startswith("RC_VIEWER_")
            }
            print(json.dumps(interesting, indent=2, sort_keys=True))
        return

    if log_path:
        log_path.parent.mkdir(parents=True, exist_ok=True)
        with log_path.open("w", encoding="utf-8") as log:
            result = subprocess.run(
                cmd,
                cwd=ROOT,
                env=env,
                stdout=log,
                stderr=subprocess.STDOUT,
                text=True,
            )
    else:
        result = subprocess.run(cmd, cwd=ROOT, env=env, text=True)
    if result.returncode != 0:
        detail = f" See log: {log_path}" if log_path else ""
        raise SystemExit(f"command failed ({result.returncode}): {' '.join(cmd)}{detail}")


def build_bundle(args: argparse.Namespace, scene_name: str, bundle: Path) -> None:
    if args.no_build:
        return
    manifest = SCENES[scene_name]["manifest"]
    cmd = [
        sys.executable,
        str(BUILD_BUNDLE),
        "--manifest",
        str(manifest),
        "--output",
        str(bundle),
        "--link-mode",
        args.link_mode,
        "--force",
    ]
    run_command(cmd, dry_run=args.dry_run)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def load_baseline(path: Path | None) -> dict[str, Any]:
    if not path:
        return {}
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    scenes = data.get("scenes", {})
    if not isinstance(scenes, dict):
        raise SystemExit(f"baseline has no scene map: {path}")
    return scenes


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")


def capture_scene(args: argparse.Namespace, scene_name: str,
                  run_index: int, bundle: Path,
                  camera: dict[str, float]) -> dict[str, Any]:
    suffix = "" if args.repeat == 1 else f"_run{run_index}"
    image_path = args.output_dir / f"{scene_name}{suffix}.png"
    log_path = args.output_dir / "logs" / f"{scene_name}{suffix}.log"
    env = os.environ.copy()
    env.update({
        "RC_VIEWER_SCREENSHOT": str(image_path),
        "RC_VIEWER_EXIT_FRAMES": str(max(1, args.exit_frames)),
        "RUNEC_RENDER_PROFILE": args.render_profile,
        "RUNEC_CAMERA_PITCH": f"{camera['pitch']:.6g}",
        "RUNEC_CAMERA_DIST": f"{camera['dist']:.6g}",
        "RUNEC_CAMERA_FOV": f"{camera['fov']:.6g}",
    })
    if args.color_lift != "default":
        env["RUNEC_COLOR_LIFT"] = args.color_lift

    cmd = [str(bundle / "run_viewer.sh")]
    run_command(cmd, env=env, log_path=log_path, dry_run=args.dry_run)
    if args.dry_run:
        return {
            "scene": scene_name,
            "image": str(image_path),
            "log": str(log_path),
            "sha256": "<dry-run>",
        }
    if not image_path.is_file():
        raise SystemExit(f"screenshot was not written: {image_path}")
    digest = sha256_file(image_path)
    return {
        "scene": scene_name,
        "image": str(image_path),
        "log": str(log_path),
        "sha256": digest,
        "bytes": image_path.stat().st_size,
    }


def main() -> int:
    args = parse_args()
    if args.repeat < 1:
        raise SystemExit("--repeat must be >= 1")
    args.output_dir = args.output_dir.resolve()
    camera = camera_settings(args)
    scene_names = selected_scene_names(args)
    baseline = load_baseline(args.baseline)
    results: dict[str, list[dict[str, Any]]] = {}

    for scene_name in scene_names:
        bundle = scene_bundle_path(args, scene_name)
        build_bundle(args, scene_name, bundle)
        captures: list[dict[str, Any]] = []
        for run_index in range(1, args.repeat + 1):
            captures.append(capture_scene(args, scene_name, run_index, bundle, camera))
        results[scene_name] = captures

    failed = False
    if args.compare_repeat and not args.dry_run:
        for scene_name, captures in results.items():
            hashes = {capture["sha256"] for capture in captures}
            if len(hashes) > 1:
                print(f"repeat mismatch: {scene_name} produced {len(hashes)} hashes")
                failed = True

    if baseline and not args.dry_run:
        for scene_name, captures in results.items():
            expected = baseline.get(scene_name, {}).get("sha256")
            actual = captures[0]["sha256"]
            if expected and expected != actual:
                print(f"baseline mismatch: {scene_name} expected {expected} got {actual}")
                failed = True
            elif not expected:
                print(f"baseline missing scene: {scene_name}")
                failed = True

    report = {
        "schema": "runec.visual_regression_report.v1",
        "render_profile": args.render_profile,
        "color_lift": args.color_lift,
        "camera_preset": args.camera_preset,
        "camera": camera,
        "repeat": args.repeat,
        "scenes": results,
    }
    if args.dry_run:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        write_json(args.output_dir / "visual_regression_report.json", report)

    if args.write_baseline and not args.dry_run:
        baseline_data = {
            "schema": "runec.visual_regression_baseline.v1",
            "render_profile": args.render_profile,
            "color_lift": args.color_lift,
            "camera_preset": args.camera_preset,
            "camera": camera,
            "scenes": {
                scene_name: {
                    "sha256": captures[0]["sha256"],
                    "image": captures[0]["image"],
                }
                for scene_name, captures in results.items()
            },
        }
        write_json(args.write_baseline.resolve(), baseline_data)

    for scene_name, captures in results.items():
        for capture in captures:
            print(f"{scene_name}: {capture['sha256']} {capture['image']}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
