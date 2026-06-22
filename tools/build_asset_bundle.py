#!/usr/bin/env python3
"""Build a scoped RuneC loose-asset bundle from a content manifest."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
from typing import Iterable

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_VISUALS = ROOT / "data/defs/combat_visuals.tsv"

BASE_ASSETS = {
    "defs/npc_defs.bin": ROOT / "data/defs/npc_defs.bin",
    "defs/items.bin": ROOT / "data/defs/items.bin",
    "defs/prayers.bin": ROOT / "data/defs/prayers.bin",
    "defs/spells.bin": ROOT / "data/defs/spells.bin",
    "defs/spotanims.bin": ROOT / "data/defs/spotanims.bin",
    "defs/regular_npc_mechanics.bin": ROOT / "data/defs/regular_npc_mechanics.bin",
    "defs/activity_schemas.bin": ROOT / "data/defs/activity_schemas.bin",
    "defs/activity_spawns.bin": ROOT / "data/defs/activity_spawns.bin",
    "defs/activity_mechanics.bin": ROOT / "data/defs/activity_mechanics.bin",
    "defs/activity_states.bin": ROOT / "data/defs/activity_states.bin",
    "defs/encounters.bin": ROOT / "data/defs/encounters.bin",
    "defs/player_actions.bin": ROOT / "data/defs/player_actions.bin",
    "defs/object_defs.bin": ROOT / "data/defs/object_defs.bin",
    "defs/object_placements.bin": ROOT / "data/defs/object_placements.bin",
    "defs/object_behaviors.bin": ROOT / "data/defs/object_behaviors.bin",
    "defs/object_transports.bin": ROOT / "data/defs/object_transports.bin",
    "defs/collision_tiles.bin": ROOT / "data/defs/collision_tiles.bin",
    "defs/area_flags.bin": ROOT / "data/defs/area_flags.bin",
    "defs/traversal_edges.bin": ROOT / "data/defs/traversal_edges.bin",
    "spawns/world.npc-spawns.bin": ROOT / "data/spawns/world.npc-spawns.bin",
    "models/npcs.models": ROOT / "data/models/npcs.models",
    "models/npcs.atlas": ROOT / "data/models/npcs.atlas",
    "models/player.models": ROOT / "data/models/player.models",
    "models/items.models": ROOT / "data/models/items.models",
    "models/items.atlas": ROOT / "data/models/items.atlas",
    "models/items.tanim": ROOT / "data/models/items.tanim",
    "models/projectiles.models": ROOT / "data/models/projectiles.models",
    "models/projectiles.atlas": ROOT / "data/models/projectiles.atlas",
    "models/item_render.map": ROOT / "data/models/item_render.map",
    "anims/npcs.anims": ROOT / "data/anims/npcs.anims",
    "anims/player.anims": ROOT / "data/anims/player.anims",
    "anims/all.anims": ROOT / "data/anims/all.anims",
    "sprites/items/item_stack_variants.tsv": ROOT / "data/sprites/items/item_stack_variants.tsv",
    "fonts/runescape.ttf": ROOT / "data/fonts/runescape.ttf",
    "fonts/runescape_small.ttf": ROOT / "data/fonts/runescape_small.ttf",
    "ui/interfaces.bin": ROOT / "data/ui/interfaces.bin",
    "ui/interface_manifest.json": ROOT / "data/ui/interface_manifest.json",
}

SCENE_SUFFIXES = (
    ".terrain",
    ".objects",
    ".atlas",
    ".tanim",
    ".oanim",
    ".object_anim.models",
    ".object_anim.atlas",
    ".object_anim.tanim",
)


def default_assets() -> dict[str, Path]:
    assets = dict(BASE_ASSETS)
    sprites_ui = ROOT / "data/sprites/ui"
    if sprites_ui.is_dir():
        for src in sorted(sprites_ui.glob("*.png")):
            assets[f"sprites/ui/{src.name}"] = src
    return assets


@dataclass(frozen=True)
class ProfileSelector:
    kind: str
    key: str
    styles: tuple[str, ...]

    def matches(self, row: dict[str, str]) -> bool:
        if row.get("kind") != self.kind or row.get("key") != self.key:
            return False
        return not self.styles or row.get("style") in self.styles

    def required_rows(self) -> list[str]:
        if not self.styles:
            return [f"{self.kind}:{self.key}"]
        return [f"{self.kind}:{self.key}:{style}" for style in self.styles]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build a scoped RuneC runtime asset bundle from a manifest."
    )
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--visuals", type=Path, default=DEFAULT_VISUALS)
    parser.add_argument(
        "--link-mode",
        choices=("symlink", "copy"),
        default="symlink",
        help="How to stage large existing assets. Defaults to symlink.",
    )
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--no-validate", action="store_true")
    return parser.parse_args()


def load_manifest(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as f:
        manifest = json.load(f)
    if manifest.get("schema") != "runec.asset_manifest.v1":
        raise SystemExit(f"unsupported asset manifest schema in {path}")
    if not manifest.get("name"):
        raise SystemExit("asset manifest is missing name")
    return manifest


def repo_path(path: str | Path) -> Path:
    p = Path(path)
    return p if p.is_absolute() else ROOT / p


def data_logical_path(path: Path) -> str:
    resolved = path.resolve()
    data_root = (ROOT / "data").resolve()
    try:
        return resolved.relative_to(data_root).as_posix()
    except ValueError:
        return resolved.as_posix()


def scene_logical_file(scene: dict, suffix: str, fallback: str) -> str:
    prefix_raw = scene.get("prefix")
    if not prefix_raw:
        return fallback
    prefix = repo_path(prefix_raw)
    return data_logical_path(prefix.with_name(prefix.name + suffix))


def scene_files(manifest: dict) -> dict[str, Path]:
    scene = manifest.get("scene") or {}
    prefix_raw = scene.get("prefix")
    if not prefix_raw:
        return {}
    prefix = repo_path(prefix_raw)
    out: dict[str, Path] = {}
    for suffix in SCENE_SUFFIXES:
        src = prefix.with_name(prefix.name + suffix)
        if src.exists():
            out[data_logical_path(src)] = src
    for plane in scene.get("planes", []):
        if int(plane) == 0:
            continue
        plane_prefix = prefix.with_name(f"{prefix.name}.p{int(plane)}")
        for suffix in SCENE_SUFFIXES:
            src = plane_prefix.with_name(plane_prefix.name + suffix)
            if src.exists():
                out[data_logical_path(src)] = src
    return out


def selectors_from_manifest(manifest: dict) -> list[ProfileSelector]:
    out: list[ProfileSelector] = []
    for entry in manifest.get("visual_profiles", []):
        kind = str(entry.get("kind", "")).strip()
        key = str(entry.get("key", "")).strip()
        styles = tuple(str(style).strip() for style in entry.get("styles", []))
        if not kind or not key:
            raise SystemExit("visual_profiles entries require kind and key")
        out.append(ProfileSelector(kind, key, styles))
    return out


def read_visual_rows(path: Path) -> tuple[list[str], list[dict[str, str]]]:
    lines = path.read_text(encoding="utf-8").splitlines()
    header: list[str] | None = None
    rows: list[dict[str, str]] = []
    for raw in lines:
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = [part.strip() for part in line.split("|")]
        if header is None:
            header = parts
            continue
        rows.append({
            name: parts[idx] if idx < len(parts) and parts[idx] else "-"
            for idx, name in enumerate(header)
        })
    if header is None:
        raise SystemExit(f"combat visuals file has no header: {path}")
    return header, rows


def select_visual_rows(
    visuals_path: Path,
    selectors: list[ProfileSelector],
) -> tuple[list[str], list[dict[str, str]]]:
    header, rows = read_visual_rows(visuals_path)
    if not selectors:
        return header, rows
    selected = [
        row for row in rows
        if any(selector.matches(row) for selector in selectors)
    ]
    return header, selected


def write_visual_rows(path: Path, header: list[str],
                      rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        f.write("|".join(header) + "\n")
        for row in rows:
            f.write("|".join(row.get(name, "-") or "-" for name in header) + "\n")


def ensure_source(path: Path) -> None:
    if not path.is_file():
        raise SystemExit(f"required asset is missing: {path}")


def stage_file(src: Path, dst: Path, link_mode: str, force: bool) -> None:
    ensure_source(src)
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists() or dst.is_symlink():
        if not force:
            return
        dst.unlink()
    if link_mode == "symlink":
        dst.symlink_to(src.resolve())
    else:
        shutil.copy2(src, dst)


def shell_export(key: str, value: str | int | bool) -> str:
    if isinstance(value, bool):
        value = "1" if value else "0"
    return f"export {key}={shlex.quote(str(value))}"


def env_from_manifest(bundle_root: Path, manifest: dict) -> dict[str, str | int | bool]:
    scene = manifest.get("scene") or {}
    viewer = manifest.get("viewer") or {}
    terrain = scene_logical_file(scene, ".terrain", "regions/varrock.terrain")
    objects = scene_logical_file(scene, ".objects", "regions/varrock.objects")
    origin = scene.get("origin", [3072, 3264])
    size = scene.get("size", [320, 320])
    player_start = scene.get("player_start", [origin[0], origin[1], 0])

    env: dict[str, str | int | bool] = {
        "RUNEC_DATA_ROOT": str(bundle_root.resolve()),
        "RUNEC_ASSET_BACKEND": "loose",
        "RUNEC_COMBAT_VISUALS": "defs/combat_visuals.tsv",
        "RUNEC_SPOTANIMS": "defs/spotanims.bin",
        "RUNEC_NPC_DEFS": "defs/npc_defs.bin",
        "RUNEC_ITEMS": "defs/items.bin",
        "RUNEC_PRAYERS": "defs/prayers.bin",
        "RUNEC_SPELLS": "defs/spells.bin",
        "RUNEC_MONSTER_MECHANICS": "defs/regular_npc_mechanics.bin",
        "RUNEC_ACTIVITY_SCHEMAS": "defs/activity_schemas.bin",
        "RUNEC_ACTIVITY_SPAWNS": "defs/activity_spawns.bin",
        "RUNEC_ACTIVITY_MECHANICS": "defs/activity_mechanics.bin",
        "RUNEC_ACTIVITY_STATES": "defs/activity_states.bin",
        "RUNEC_ENCOUNTERS": "defs/encounters.bin",
        "RUNEC_PLAYER_ACTIONS": "defs/player_actions.bin",
        "RUNEC_OBJECT_DEFS": "defs/object_defs.bin",
        "RUNEC_OBJECT_PLACEMENTS": "defs/object_placements.bin",
        "RUNEC_OBJECT_BEHAVIORS": "defs/object_behaviors.bin",
        "RUNEC_OBJECT_TRANSPORTS": "defs/object_transports.bin",
        "RUNEC_COLLISION_TILES": "defs/collision_tiles.bin",
        "RUNEC_NPC_SPAWNS": "spawns/world.npc-spawns.bin",
        "RUNEC_AREA_FLAGS": "defs/area_flags.bin",
        "RUNEC_TRAVERSAL_EDGES": "defs/traversal_edges.bin",
        "RUNEC_TERRAIN": terrain,
        "RUNEC_OBJECTS": objects,
        "RUNEC_NPC_MODELS": "models/npcs.models",
        "RUNEC_NPC_ANIMS": "anims/npcs.anims",
        "RUNEC_NPC_FALLBACK_ANIMS": "anims/all.anims",
        "RUNEC_PLAYER_MODELS": "models/player.models",
        "RUNEC_PLAYER_ANIMS": "anims/player.anims",
        "RUNEC_FALLBACK_ANIMS": "anims/all.anims",
        "RUNEC_ITEM_MODELS": "models/items.models",
        "RUNEC_ITEM_RENDER_MAP": "models/item_render.map",
        "RUNEC_PROJECTILE_MODELS": "models/projectiles.models",
        "RUNEC_ITEM_STACK_VARIANTS": "sprites/items/item_stack_variants.tsv",
        "RUNEC_WORLD_ORIGIN_X": int(origin[0]),
        "RUNEC_WORLD_ORIGIN_Y": int(origin[1]),
        "RUNEC_WORLD_W": int(size[0]),
        "RUNEC_WORLD_H": int(size[1]),
        "RUNEC_PLAYER_START_X": int(player_start[0]),
        "RUNEC_PLAYER_START_Y": int(player_start[1]),
        "RUNEC_SCENE_PLANE": int(player_start[2]) if len(player_start) > 2 else 0,
        "RUNEC_SCENE_AUTO_EXPORT": bool(viewer.get("scene_auto_export", False)),
        "RUNEC_SCENE_RADIUS_REGIONS": int(scene.get("scene_radius_regions", 1)),
        "RUNEC_RENDER_PROFILE": str(viewer.get("render_profile", "osrs")),
        "RUNEC_DEBUG_COMBAT_VISUAL_EVENTS": bool(
            viewer.get("debug_combat_visual_events", False)
        ),
        "RUNEC_DEV_BOSS_ATTACKS": bool(viewer.get("dev_boss_attacks", True)),
    }
    if scene.get("dev_transport"):
        env["RUNEC_DEV_TRANSPORT_DEST"] = str(scene["dev_transport"])
    return env


def write_env_file(path: Path, env: dict[str, str | int | bool]) -> None:
    lines = [
        "# generated by tools/build_asset_bundle.py",
        "# source this file from the RuneC repository root before launching rc-viewer",
    ]
    lines.extend(shell_export(key, env[key]) for key in sorted(env))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_runner(path: Path, env_path: Path) -> None:
    script = f"""#!/usr/bin/env bash
set -euo pipefail
cd {shlex.quote(str(ROOT))}
set -a
. {shlex.quote(str(env_path.resolve()))}
set +a
exec ./build/rc-viewer "$@"
"""
    path.write_text(script, encoding="utf-8")
    path.chmod(0o755)


def validation_command(bundle_root: Path, manifest: dict) -> list[str]:
    validation = manifest.get("validation") or {}
    cmd = [
        sys.executable,
        str(ROOT / "tools/validate_combat_visuals.py"),
        "--visuals",
        str(bundle_root / "defs/combat_visuals.tsv"),
        "--spotanims",
        str(bundle_root / "defs/spotanims.bin"),
        "--model-bundle",
        str(bundle_root / "models/projectiles.models"),
        "--model-bundle",
        str(bundle_root / "models/items.models"),
        "--model-bundle",
        str(bundle_root / "models/npcs.models"),
        "--animation-cache",
        str(bundle_root / "anims/player.anims"),
        "--animation-cache",
        str(bundle_root / "anims/npcs.anims"),
        "--animation-cache",
        str(bundle_root / "anims/all.anims"),
        "--mode",
        str(validation.get("mode", "strict")),
    ]
    for selector in validation.get("require_rows", []):
        cmd.extend(["--require-row", str(selector)])
    return cmd


def run_validation(bundle_root: Path, manifest: dict) -> None:
    cmd = validation_command(bundle_root, manifest)
    result = subprocess.run(cmd, cwd=ROOT, text=True)
    if result.returncode != 0:
        raise SystemExit(result.returncode)


def sorted_asset_items(assets: dict[str, Path]) -> Iterable[tuple[str, Path]]:
    return sorted(assets.items(), key=lambda item: item[0])


def build_bundle(args: argparse.Namespace) -> None:
    manifest_path = args.manifest.resolve()
    manifest = load_manifest(manifest_path)
    selectors = selectors_from_manifest(manifest)
    header, visual_rows = select_visual_rows(args.visuals, selectors)
    if selectors and not visual_rows:
        raise SystemExit("manifest selected zero combat visual rows")

    assets = default_assets()
    assets.update(scene_files(manifest))
    extra_assets = manifest.get("assets") or {}
    for logical, src in extra_assets.items():
        assets[str(logical)] = repo_path(src)

    output = args.output.resolve()
    bundle_visuals = output / "defs/combat_visuals.tsv"
    env = env_from_manifest(output, manifest)

    if args.dry_run:
        print(f"bundle: {manifest['name']} -> {output}")
        print(f"visual rows: {len(visual_rows)}")
        print(f"assets: {len(assets)}")
        for logical, src in sorted_asset_items(assets):
            print(f"{logical}\t{src}")
        return

    output.mkdir(parents=True, exist_ok=True)
    for logical, src in sorted_asset_items(assets):
        stage_file(src, output / logical, args.link_mode, args.force)
    write_visual_rows(bundle_visuals, header, visual_rows)

    resolved = {
        "schema": "runec.resolved_asset_bundle.v1",
        "name": manifest["name"],
        "source_manifest": str(manifest_path),
        "lighting": manifest.get("lighting", "-"),
        "npc_ids": manifest.get("npc_ids", []),
        "spotanim_ids": manifest.get("spotanim_ids", []),
        "item_ids": manifest.get("item_ids", []),
        "animation_ids": manifest.get("animation_ids", []),
        "player_loadouts": manifest.get("player_loadouts", []),
        "combat_visual_rows": len(visual_rows),
        "assets": [
            {
                "logical_path": logical,
                "source": str(src.resolve()),
                "staged": str((output / logical).resolve()),
            }
            for logical, src in sorted_asset_items(assets)
        ],
    }
    (output / "bundle_manifest.json").write_text(
        json.dumps(resolved, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    write_env_file(output / "run.env", env)
    write_runner(output / "run_viewer.sh", output / "run.env")

    if not args.no_validate:
        run_validation(output, manifest)
    print(f"bundle built: {output}")
    print(f"visual rows: {len(visual_rows)}")
    print(f"run: {output / 'run_viewer.sh'}")


def main() -> int:
    args = parse_args()
    build_bundle(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
