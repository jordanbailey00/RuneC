#!/usr/bin/env python3
"""Top-level data pipeline for local RuneC runtime data builds.

The pipeline is intentionally conservative: source-controlled content is
validated, approved cache-backed outputs are regenerated from explicit
maintainer inputs, reviewed runtime snapshots are verified before installation,
and runtime packs are rebuilt only after required rebuild coverage is clean.
Machine-readable records are written for each stage.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable

ROOT = Path(__file__).resolve().parents[1]

STAGE_ORDER = [
    "validate-repo-self-contained",
    "validate-source-authority",
    "validate-content",
    "export-content",
    "export-cache-derived-assets",
    "export-defs",
    "export-render-assets",
    "pack-runtime-data",
    "unpack-runtime-data",
    "generate-reports",
    "validate-reports",
]

DATA_RUNTIME_DIRS = (
    "defs",
    "regions",
    "models",
    "anims",
    "sprites",
    "ui",
    "fonts",
    "spawns",
)

REPORT_SOURCE_DIRS = (
    "tools/reports",
)

CONTENT_EXPORT_COMMANDS = (
    ("encounters", "tools/export_encounters.py"),
    ("activity-spawns", "tools/export_activity_spawns.py"),
    ("activity-states", "tools/export_activity_states.py"),
    ("activity-mechanics", "tools/export_activity_mechanics.py"),
    ("activity-schemas", "tools/export_activity_schemas.py"),
    ("dialogue", "tools/export_dialogue.py"),
    ("combat-visuals", "tools/export_combat_visuals.py"),
    ("item-effects", "tools/export_item_effects.py"),
)

CONTENT_EXPORT_OUTPUTS = (
    "data/defs/encounters.bin",
    "data/defs/activity_spawns.bin",
    "data/defs/activity_states.bin",
    "data/defs/activity_mechanics.bin",
    "data/defs/activity_schemas.bin",
    "data/defs/dialogue.bin",
    "data/defs/combat_visuals.tsv",
    "tools/reports/encounters.txt",
    "tools/reports/activity_spawns.txt",
    "tools/reports/activity_states.txt",
    "tools/reports/activity_mechanics.txt",
    "tools/reports/activity_schemas.txt",
    "tools/reports/combat_visuals.txt",
    "tools/reports/item_effects.txt",
)

DEFERRED_CONTENT_EXPORTS = (
    {
        "dataset": "quests",
        "reason": "content/quests is tracked, but tools/export_quests.py still reads the deferred wiki cache path.",
    },
    {
        "dataset": "regular_npc_special_mechanics",
        "reason": "tools/export_regular_npc_mechanics.py needs generated npc_defs plus wiki poison/venom/disease IDs before it is self-contained.",
    },
    {
        "dataset": "item_specials",
        "reason": "content/specials is currently source catalog input; runtime export is represented by tools/export_item_effects.py.",
    },
)


@dataclass(frozen=True)
class RuntimeOutputSpec:
    dataset: str
    logical_paths: tuple[str, ...]
    rebuild_inputs: tuple[str, ...]
    commands: tuple[tuple[str, ...], ...]
    authority: str
    release_required: bool = True


def py_cmd(*args: str) -> tuple[str, ...]:
    return ("{python}", *args)


B237_CACHE_INPUT = ("env:RUNEC_B237_CACHE",)
B237_CACHE_AND_DUMP_INPUTS = ("env:RUNEC_B237_CACHE", "env:RUNEC_B237_DUMP")
OSRSREBOXED_INPUT = ("env:RUNEC_OSRSREBOXED_DB",)
B237_CACHE_AND_OSRSREBOXED_INPUTS = B237_CACHE_INPUT + OSRSREBOXED_INPUT


def snapshot_cmd(*paths: str) -> tuple[str, ...]:
    return py_cmd("tools/export_runtime_snapshots.py", "--paths", *paths)

CACHE_DERIVED_REBUILD_SPECS = (
    RuntimeOutputSpec(
        dataset="spotanims",
        logical_paths=("defs/spotanims.bin",),
        rebuild_inputs=B237_CACHE_INPUT,
        commands=(
            py_cmd("tools/cache_pipeline/export_spotanims.py", "--modern-cache", "{b237_cache}", "--output", "data/defs/spotanims.bin"),
        ),
        authority="b237 cache spotanim config decoded by RuneC cache tools",
    ),
    RuntimeOutputSpec(
        dataset="projectile_models",
        logical_paths=("models/projectiles.models",),
        rebuild_inputs=B237_CACHE_INPUT,
        commands=(
            py_cmd("tools/cache_pipeline/export_projectile_models.py", "--cache", "{b237_cache}", "--spotanims", "data/defs/spotanims.bin", "--spotanim-ids", "all", "--output", "data/models/projectiles.models"),
        ),
        authority="b237 cache spotanim/projectile models decoded by RuneC cache tools",
    ),
    RuntimeOutputSpec(
        dataset="ui_sprites",
        logical_paths=("sprites/ui/",),
        rebuild_inputs=B237_CACHE_AND_DUMP_INPUTS,
        commands=(
            py_cmd("tools/cache_pipeline/export_sprites_modern.py", "--cache", "{b237_cache}", "--output", "data/sprites/ui", "--graphic-symbols", "{b237_dump}/symbols/graphic.sym"),
        ),
        authority="b237 cache plus decoded graphic symbols",
    ),
    RuntimeOutputSpec(
        dataset="item_sprites",
        logical_paths=("sprites/items/",),
        rebuild_inputs=B237_CACHE_INPUT,
        commands=(
            py_cmd(
                "tools/cache_pipeline/export_item_sprites_b237.py",
                "--cache",
                "{b237_cache}",
                "--output",
                "data/sprites/items",
                "--clean",
                "--fail-on-decode-gap",
            ),
        ),
        authority="b237 cache item config and inventory models rendered by RuneC-owned item sprite exporter",
    ),
    RuntimeOutputSpec(
        dataset="ui",
        logical_paths=("ui/",),
        rebuild_inputs=B237_CACHE_AND_DUMP_INPUTS,
        commands=(
            py_cmd("tools/cache_pipeline/export_ui_interfaces.py", "--cache", "{b237_cache}", "--dump-root", "{b237_dump}"),
        ),
        authority="b237 cache plus decoded interface symbols",
    ),
    RuntimeOutputSpec(
        dataset="regions",
        logical_paths=("regions/",),
        rebuild_inputs=B237_CACHE_INPUT,
        commands=(
            py_cmd("tools/cache_pipeline/export_terrain.py", "--modern-cache", "{b237_cache}", "--regions", "48,51 49,51 50,51 51,51 52,51 48,52 49,52 50,52 51,52 52,52 48,53 49,53 50,53 51,53 52,53 48,54 49,54 50,54 51,54 52,54 48,55 49,55 50,55 51,55 52,55", "--output", "data/regions/varrock.terrain"),
            py_cmd("tools/cache_pipeline/export_objects.py", "--modern-cache", "{b237_cache}", "--regions", "48,51 49,51 50,51 51,51 52,51 48,52 49,52 50,52 51,52 52,52 48,53 49,53 50,53 51,53 52,53 48,54 49,54 50,54 51,54 52,54 48,55 49,55 50,55 51,55 52,55", "--output", "data/regions/varrock.objects"),
            py_cmd("tools/cache_pipeline/export_collision_map_modern.py", "--cache", "{b237_cache}", "--regions", "48,51", "49,51", "50,51", "51,51", "52,51", "48,52", "49,52", "50,52", "51,52", "52,52", "48,53", "49,53", "50,53", "51,53", "52,53", "48,54", "49,54", "50,54", "51,54", "52,54", "48,55", "49,55", "50,55", "51,55", "52,55", "--output", "data/regions/varrock.cmap"),
            py_cmd("tools/cache_pipeline/export_validation_scenes.py", "--cache", "{b237_cache}", "--clean"),
        ),
        authority="b237 cache; full Varrock baseline plus first-release viewer validation scene slices",
    ),
)

POST_DEF_RENDER_REBUILD_SPECS = (
    RuntimeOutputSpec(
        dataset="item_render_models",
        logical_paths=("models/items.models", "models/items.tanim", "models/item_render.map"),
        rebuild_inputs=B237_CACHE_AND_DUMP_INPUTS,
        commands=(
            py_cmd(
                "tools/cache_pipeline/export_item_render_models.py",
                "--cache",
                "{b237_cache}",
                "--dump",
                "{b237_dump}",
                "--item-ids",
                "combat-validation",
            ),
        ),
        authority="b237 cache plus rebuilt items.bin for first-release validation equipment visuals",
    ),
    RuntimeOutputSpec(
        dataset="validation_item_icons",
        logical_paths=("sprites/items/",),
        rebuild_inputs=OSRSREBOXED_INPUT,
        commands=(
            py_cmd(
                "tools/cache_pipeline/export_reference_item_icons.py",
                "--reference",
                "{osrsreboxed_db}/docs/items-icons",
                "--items",
                "data/defs/items.bin",
                "--item-ids",
                "combat-validation",
            ),
        ),
        authority="validation-only known-good osrsreboxed item icon overlay for combat bank items; full b237 renderer remains baseline",
        release_required=False,
    ),
    RuntimeOutputSpec(
        dataset="animations",
        logical_paths=("anims/player.anims", "anims/npcs.anims", "anims/object.anims", "anims/all.anims"),
        rebuild_inputs=B237_CACHE_INPUT,
        commands=(
            py_cmd(
                "tools/cache_pipeline/export_animations.py",
                "--modern-cache",
                "{b237_cache}",
                "--spotanims",
                "data/defs/spotanims.bin",
                "--item-render-map",
                "data/models/item_render.map",
                "--combat-visuals",
                "data/defs/combat_visuals.tsv",
                "--output",
                "data/anims/player.anims",
            ),
            py_cmd(
                "tools/cache_pipeline/export_animations.py",
                "--modern-cache",
                "{b237_cache}",
                "--npc-defs",
                "data/defs/npc_defs.bin",
                "--combat-visuals",
                "data/defs/combat_visuals.tsv",
                "--output",
                "data/anims/npcs.anims",
            ),
            py_cmd(
                "tools/cache_pipeline/export_animations.py",
                "--modern-cache",
                "{b237_cache}",
                "--object-anims",
                "data/regions",
                "--output",
                "data/anims/object.anims",
            ),
            py_cmd(
                "tools/cache_pipeline/export_animations.py",
                "--modern-cache",
                "{b237_cache}",
                "--spotanims",
                "data/defs/spotanims.bin",
                "--object-anims",
                "data/regions",
                "--item-render-map",
                "data/models/item_render.map",
                "--combat-visuals",
                "data/defs/combat_visuals.tsv",
                "--npc-defs",
                "data/defs/npc_defs.bin",
                "--output",
                "data/anims/all.anims",
            ),
        ),
        authority="b237 cache sequence/frame data explicitly scoped from spotanims, item render BAS, combat visuals, object anims, and NPC defs",
    ),
)

DEF_REBUILD_SPECS = (
    RuntimeOutputSpec(
        dataset="items",
        logical_paths=("defs/items.bin",),
        rebuild_inputs=B237_CACHE_AND_OSRSREBOXED_INPUTS,
        commands=(
            py_cmd(
                "tools/export_items.py",
                "--output",
                "data/defs/items.bin",
                "--osrsreboxed-root",
                "{osrsreboxed_db}",
                "--require-osrsreboxed",
            ),
        ),
        authority="b237 cache plus explicit osrsreboxed equipment/weapon validation bridge; official release still requires RuneC-owned replacement",
    ),
    RuntimeOutputSpec(
        dataset="npc_defs",
        logical_paths=("defs/npc_defs.bin",),
        rebuild_inputs=(),
        commands=(snapshot_cmd("data/defs/npc_defs.bin"),),
        authority="tracked RuneC-owned reviewed runtime snapshot preserving accepted NPC definition semantics",
    ),
    RuntimeOutputSpec(
        dataset="spawns",
        logical_paths=("spawns/world.npc-spawns.bin", "regions/varrock.npc-spawns.bin"),
        rebuild_inputs=(),
        commands=(
            snapshot_cmd(
                "data/spawns/world.npc-spawns.bin",
                "data/regions/varrock.npc-spawns.bin",
            ),
        ),
        authority="tracked RuneC-owned reviewed runtime snapshot; direction/wander simplification accepted for first release",
    ),
    RuntimeOutputSpec(
        dataset="varbits_varps",
        logical_paths=("defs/varbits.bin", "defs/varps.bin"),
        rebuild_inputs=B237_CACHE_INPUT,
        commands=(
            py_cmd("tools/export_varbits.py", "--cache", "{b237_cache}"),
            py_cmd("tools/export_varps.py", "--cache", "{b237_cache}"),
        ),
        authority="b237 cache",
    ),
    RuntimeOutputSpec(
        dataset="object_defs_placements_collision",
        logical_paths=("defs/object_defs.bin", "defs/object_placements.bin", "defs/collision_tiles.bin"),
        rebuild_inputs=B237_CACHE_INPUT,
        commands=(
            py_cmd("tools/export_object_defs.py", "--cache", "{b237_cache}"),
            py_cmd("tools/export_object_placements.py", "--cache", "{b237_cache}"),
            py_cmd("tools/export_collision_tiles.py", "--cache", "{b237_cache}"),
        ),
        authority="b237 cache with RuneC decoders",
    ),
    RuntimeOutputSpec(
        dataset="drops_rdt_acquisition_normalization",
        logical_paths=("defs/drops.bin", "defs/rdt.bin", "defs/gdt.bin", "defs/mrdt.bin", "defs/acquisition_sources.bin", "defs/normalization.bin"),
        rebuild_inputs=(),
        commands=(
            snapshot_cmd(
                "data/defs/drops.bin",
                "data/defs/rdt.bin",
                "data/defs/gdt.bin",
                "data/defs/mrdt.bin",
                "data/defs/acquisition_sources.bin",
                "data/defs/normalization.bin",
            ),
        ),
        authority="tracked RuneC-owned reviewed runtime snapshots replacing removed research-cache inputs",
    ),
    RuntimeOutputSpec(
        dataset="recipes_skill_drops_shops",
        logical_paths=("defs/recipes.bin", "defs/skill_drops.bin", "defs/shops.bin"),
        rebuild_inputs=(),
        commands=(
            snapshot_cmd(
                "data/defs/recipes.bin",
                "data/defs/skill_drops.bin",
                "data/defs/shops.bin",
            ),
        ),
        authority="tracked RuneC-owned reviewed runtime snapshots replacing removed research-cache inputs",
    ),
    RuntimeOutputSpec(
        dataset="quests",
        logical_paths=("defs/quests.bin",),
        rebuild_inputs=(),
        commands=(snapshot_cmd("data/defs/quests.bin"),),
        authority="tracked RuneC-owned reviewed runtime snapshot; quest step source remains under content/quests",
    ),
    RuntimeOutputSpec(
        dataset="slayer",
        logical_paths=("defs/slayer.bin",),
        rebuild_inputs=(),
        commands=(snapshot_cmd("data/defs/slayer.bin"),),
        authority="tracked RuneC-owned reviewed runtime snapshot replacing removed wiki scrape dependency",
    ),
    RuntimeOutputSpec(
        dataset="prayers_player_actions",
        logical_paths=("defs/prayers.bin", "defs/player_actions.bin"),
        rebuild_inputs=(),
        commands=(
            py_cmd("tools/export_prayers.py"),
            py_cmd("tools/export_player_actions.py"),
        ),
        authority="RuneC-owned static exporter tables",
    ),
    RuntimeOutputSpec(
        dataset="spells",
        logical_paths=("defs/spells.bin", "defs/teleports.bin"),
        rebuild_inputs=(),
        commands=(snapshot_cmd("data/defs/spells.bin", "data/defs/teleports.bin"),),
        authority="tracked RuneC-owned reviewed runtime snapshot; combat spell hints accepted for first release",
    ),
    RuntimeOutputSpec(
        dataset="regular_npc_mechanics",
        logical_paths=("defs/regular_npc_mechanics.bin",),
        rebuild_inputs=(),
        commands=(snapshot_cmd("data/defs/regular_npc_mechanics.bin"),),
        authority="tracked RuneC-owned reviewed runtime snapshot plus content/regular_npc_special_mechanics.toml source",
    ),
    RuntimeOutputSpec(
        dataset="object_behaviors_transports_traversal_gathering",
        logical_paths=("defs/object_behaviors.bin", "defs/object_transports.bin", "defs/traversal_edges.bin", "defs/gathering_nodes.bin"),
        rebuild_inputs=(),
        commands=(
            snapshot_cmd(
                "data/defs/object_behaviors.bin",
                "data/defs/object_transports.bin",
                "data/defs/traversal_edges.bin",
                "data/defs/gathering_nodes.bin",
            ),
        ),
        authority="tracked RuneC-owned reviewed runtime snapshots replacing removed transport/traversal research rows",
    ),
    RuntimeOutputSpec(
        dataset="area_flags",
        logical_paths=("defs/area_flags.bin",),
        rebuild_inputs=(),
        commands=(snapshot_cmd("data/defs/area_flags.bin"),),
        authority="tracked RuneC-owned reviewed runtime snapshot accepted for first-release area semantics",
    ),
)


@dataclass(frozen=True)
class StageSpec:
    name: str
    inputs: tuple[str, ...]
    outputs: tuple[str, ...]
    description: str


@dataclass(frozen=True)
class PipelineContext:
    data_root: Path
    dist_root: Path
    generated_root: Path
    version: str
    check: bool
    force: bool

    @property
    def pipeline_root(self) -> Path:
        return self.generated_root / "pipeline"

    @property
    def report_root(self) -> Path:
        return self.generated_root / "reports"


def b237() -> str:
    return os.environ.get("RUNEC_B237_CACHE", "")


def b237_dump() -> str:
    return os.environ.get("RUNEC_B237_DUMP", "")


def command_args(command: tuple[str, ...]) -> list[str]:
    values = {
        "{python}": sys.executable,
        "{b237_cache}": b237(),
        "{b237_dump}": b237_dump(),
        "{osrsreboxed_db}": os.environ.get("RUNEC_OSRSREBOXED_DB", ""),
    }
    out: list[str] = []
    for part in command:
        expanded = part
        for token, value in values.items():
            expanded = expanded.replace(token, value)
        out.append(expanded)
    return out


def command_record(command: tuple[str, ...]) -> dict[str, Any]:
    args = []
    for part in command:
        args.append(
            part.replace("{python}", sys.executable)
            .replace("{b237_cache}", "$RUNEC_B237_CACHE")
            .replace("{b237_dump}", "$RUNEC_B237_DUMP")
            .replace("{osrsreboxed_db}", "$RUNEC_OSRSREBOXED_DB")
        )
    return {"args": args}


def missing_required_inputs(names: tuple[str, ...]) -> list[str]:
    missing: list[str] = []
    for name in names:
        if name.startswith("env:"):
            env_name = name.removeprefix("env:")
            if not os.environ.get(env_name):
                missing.append(name)
            continue
        missing.append(name)
    return missing


def output_refs(ctx: PipelineContext, logical_paths: tuple[str, ...]) -> list[dict[str, Any]]:
    refs: list[dict[str, Any]] = []
    for logical in logical_paths:
        refs.append(file_ref(ctx.data_root / logical))
    return refs


def emit_rebuild_plan(
    ctx: PipelineContext,
    record: dict[str, Any],
    specs: tuple[RuntimeOutputSpec, ...],
) -> None:
    rows: list[dict[str, Any]] = []
    gaps: list[dict[str, Any]] = []
    for spec in specs:
        missing = missing_required_inputs(spec.rebuild_inputs)
        row = {
            "dataset": spec.dataset,
            "logical_paths": list(spec.logical_paths),
            "release_required": spec.release_required,
            "authority": spec.authority,
            "required_inputs": list(spec.rebuild_inputs),
            "missing_inputs": missing,
            "planned_commands": [command_record(command) for command in spec.commands],
            "outputs_observed": output_refs(ctx, spec.logical_paths),
        }
        rows.append(row)
        if spec.release_required and missing:
            gaps.append(
                {
                    "dataset": spec.dataset,
                    "logical_paths": list(spec.logical_paths),
                    "missing_inputs": missing,
                    "authority": spec.authority,
                }
            )
    record["rebuild_coverage"] = rows
    if gaps:
        record["required_rebuild_gaps"] = gaps


def run_rebuild_specs(
    ctx: PipelineContext,
    record: dict[str, Any],
    specs: tuple[RuntimeOutputSpec, ...],
) -> None:
    for spec in specs:
        missing = missing_required_inputs(spec.rebuild_inputs)
        if missing:
            continue
        for command in spec.commands:
            run_command(command_args(command), record)
        record.setdefault("outputs_observed", []).extend(output_refs(ctx, spec.logical_paths))


def required_rebuild_gap_message(record: dict[str, Any]) -> str | None:
    gaps = record.get("required_rebuild_gaps") or []
    if not gaps:
        return None
    datasets = ", ".join(str(gap["dataset"]) for gap in gaps[:12])
    extra = "" if len(gaps) <= 12 else f", ... {len(gaps) - 12} more"
    return f"required runtime outputs still need rebuild inputs: {datasets}{extra}"


STAGE_SPECS: dict[str, StageSpec] = {
    "validate-repo-self-contained": StageSpec(
        name="validate-repo-self-contained",
        inputs=(
            ".gitignore",
            "AGENT.md",
            "data-sources/",
            "runtime-data.lock",
            "tools/validate_no_external_repos.py",
            "tools/validate_pipeline_inputs.py",
            "tools/validate_runtime_data_lock.py",
        ),
        outputs=("generated/pipeline/stages/validate-repo-self-contained.json",),
        description="Verify that the repository does not depend on local external checkouts for normal data builds.",
    ),
    "validate-source-authority": StageSpec(
        name="validate-source-authority",
        inputs=(
            "data-sources/README.md",
            "data-sources/sources.lock",
            "data-sources/source_gaps.json",
            "runtime-data.lock",
            "tools/validate_sources.py",
            "tools/validate_source_authority.py",
            "tools/validate_runtime_data_lock.py",
        ),
        outputs=(
            "generated/reports/source_gaps.json",
            "generated/reports/source_gaps.txt",
            "generated/pipeline/stages/validate-source-authority.json",
        ),
        description="Validate source locks, authority coverage, and known gap reports.",
    ),
    "validate-content": StageSpec(
        name="validate-content",
        inputs=(
            "content/",
            "schema/",
            "tools/validate_content_layout.py",
            "tools/validate_direct_global_access.py",
            "tools/validate_schemas.py",
        ),
        outputs=("generated/pipeline/stages/validate-content.json",),
        description="Validate authored content layout, runtime ownership guardrails, and binary schema coverage.",
    ),
    "export-content": StageSpec(
        name="export-content",
        inputs=(
            "content/",
            "tools/content_paths.py",
            "tools/export_activity_mechanics.py",
            "tools/export_activity_schemas.py",
            "tools/export_activity_spawns.py",
            "tools/export_activity_states.py",
            "tools/export_combat_visuals.py",
            "tools/export_dialogue.py",
            "tools/export_encounters.py",
            "tools/export_item_effects.py",
        ),
        outputs=("data/defs/", "generated/pipeline/stages/export-content.json"),
        description="Regenerate runtime outputs that are built from tracked RuneC-owned content.",
    ),
    "export-cache-derived-assets": StageSpec(
        name="export-cache-derived-assets",
        inputs=("data-sources/sources.lock", "RUNEC_B237_CACHE", "RUNEC_B237_DUMP"),
        outputs=(
            "data/models/",
            "data/anims/",
            "data/sprites/",
            "generated/pipeline/stages/export-cache-derived-assets.json",
        ),
        description="Rebuild cache-derived runtime assets from explicit maintainer inputs or record required rebuild gaps.",
    ),
    "export-defs": StageSpec(
        name="export-defs",
        inputs=("data-sources/sources.lock", "schema/defs/", "RUNEC_B237_CACHE", "RUNEC_OSRSREBOXED_DB"),
        outputs=("data/defs/", "generated/pipeline/stages/export-defs.json"),
        description="Regenerate non-content definition outputs or record required rebuild gaps.",
    ),
    "export-render-assets": StageSpec(
        name="export-render-assets",
        inputs=("data-sources/sources.lock", "RUNEC_B237_CACHE", "RUNEC_B237_DUMP", "RUNEC_OSRSREBOXED_DB"),
        outputs=(
            "data/models/",
            "data/sprites/",
            "data/ui/",
            "data/fonts/",
            "generated/pipeline/stages/export-render-assets.json",
        ),
        description="Rebuild render-facing outputs that depend on regenerated definitions before runtime packing.",
    ),
    "pack-runtime-data": StageSpec(
        name="pack-runtime-data",
        inputs=(
            "data/defs/",
            "data/regions/",
            "data/models/",
            "data/anims/",
            "data/sprites/",
            "data/ui/",
            "data/fonts/",
            "data/spawns/",
        ),
        outputs=(
            "data/manifest.json",
            "dist-data/manifest.json",
            "dist-data/packs/",
            "generated/pipeline/stages/pack-runtime-data.json",
        ),
        description="Build runtime packs and write local and distributable manifests.",
    ),
    "unpack-runtime-data": StageSpec(
        name="unpack-runtime-data",
        inputs=("dist-data/manifest.json", "dist-data/packs/"),
        outputs=("data/.runtime-unpacked", "data/manifest.json", "generated/pipeline/stages/unpack-runtime-data.json"),
        description="Verify runtime packs can hydrate the local data tree.",
    ),
    "generate-reports": StageSpec(
        name="generate-reports",
        inputs=(
            "tools/reports/",
            "tools/export_area_flags.py",
            "tools/report_database_completion.py",
            "generated/reports/source_gaps.json",
            "generated/reports/source_gaps.txt",
        ),
        outputs=("generated/reports/", "generated/pipeline/stages/generate-reports.json"),
        description="Collect generated data quality reports into a single generated report tree.",
    ),
    "validate-reports": StageSpec(
        name="validate-reports",
        inputs=("generated/reports/", "tools/validate_reports.py", "data/manifest.json", "dist-data/manifest.json"),
        outputs=(
            "generated/reports/report-validation.json",
            "generated/reports/data-v1-summary.json",
            "generated/reports/data-v1-summary.txt",
            "generated/pipeline/stages/validate-reports.json",
        ),
        description="Validate generated reports as data release gates.",
    ),
}


class PipelineError(RuntimeError):
    pass


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_ref(path: Path, *, include_sha: bool = True) -> dict[str, Any]:
    result: dict[str, Any] = {"path": rel(path), "exists": path.exists()}
    if path.is_file():
        result["bytes"] = path.stat().st_size
        if include_sha:
            result["sha256"] = sha256_file(path)
    elif path.is_dir():
        files = [p for p in path.rglob("*") if p.is_file()]
        result["files"] = len(files)
        result["bytes"] = sum(p.stat().st_size for p in files)
    return result


def tree_summary(root: Path, names: tuple[str, ...]) -> dict[str, Any]:
    entries: dict[str, Any] = {}
    total_files = 0
    total_bytes = 0
    for name in names:
        path = root / name
        if path.is_dir():
            files = [p for p in path.rglob("*") if p.is_file()]
            size = sum(p.stat().st_size for p in files)
            entries[name] = {"exists": True, "files": len(files), "bytes": size}
            total_files += len(files)
            total_bytes += size
        else:
            entries[name] = {"exists": False, "files": 0, "bytes": 0}
    return {"root": rel(root), "files": total_files, "bytes": total_bytes, "entries": entries}


def safe_under(base: Path, relative_path: str) -> Path:
    target = (base / relative_path).resolve()
    try:
        target.relative_to(base.resolve())
    except ValueError as exc:
        raise PipelineError(f"Manifest path escapes data root: {relative_path}") from exc
    return target


def load_json(path: Path) -> dict[str, Any]:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise PipelineError(f"Missing required file: {rel(path)}") from exc
    except json.JSONDecodeError as exc:
        raise PipelineError(f"Invalid JSON in {rel(path)}: {exc}") from exc


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def run_command(args: list[str], record: dict[str, Any]) -> None:
    printable = " ".join(args)
    print(f"$ {printable}", flush=True)
    started = time.monotonic()
    completed = subprocess.run(args, cwd=ROOT)
    duration = round(time.monotonic() - started, 3)
    command_record = {
        "args": args,
        "exit_code": completed.returncode,
        "duration_seconds": duration,
    }
    record.setdefault("commands", []).append(command_record)
    if completed.returncode != 0:
        raise PipelineError(f"Command failed with exit code {completed.returncode}: {printable}")


def manifest_pack_checks(manifest_path: Path, packs_dir: Path) -> list[dict[str, Any]]:
    manifest = load_json(manifest_path)
    failures: list[dict[str, Any]] = []
    for pack in manifest.get("packs", []):
        pack_name = str(pack.get("name") or Path(str(pack.get("path", ""))).name)
        path = packs_dir / pack_name
        expected_size = pack.get("bytes", pack.get("size"))
        expected_sha = pack.get("sha256")
        if not path.is_file():
            failures.append({"path": rel(path), "reason": "missing"})
            continue
        actual_size = path.stat().st_size
        if expected_size is not None and actual_size != expected_size:
            failures.append(
                {
                    "path": rel(path),
                    "reason": "size_mismatch",
                    "expected": expected_size,
                    "actual": actual_size,
                }
            )
            continue
        if expected_sha and sha256_file(path) != expected_sha:
            failures.append({"path": rel(path), "reason": "sha256_mismatch"})
    return failures


def manifest_asset_checks(manifest_path: Path, data_root: Path) -> list[dict[str, Any]]:
    manifest = load_json(manifest_path)
    failures: list[dict[str, Any]] = []
    for asset in manifest.get("assets", []):
        asset_path = str(asset.get("path", ""))
        path = safe_under(data_root, asset_path)
        if not path.is_file():
            failures.append({"path": rel(path), "reason": "missing"})
            continue
        expected_size = asset.get("bytes")
        if expected_size is not None and path.stat().st_size != expected_size:
            failures.append(
                {
                    "path": rel(path),
                    "reason": "size_mismatch",
                    "expected": expected_size,
                    "actual": path.stat().st_size,
                }
            )
            continue
        expected_sha = asset.get("sha256")
        if expected_sha and sha256_file(path) != expected_sha:
            failures.append({"path": rel(path), "reason": "sha256_mismatch"})
    return failures


def copy_dist_manifest_to_data(ctx: PipelineContext, record: dict[str, Any]) -> None:
    source = ctx.dist_root / "manifest.json"
    target = ctx.data_root / "manifest.json"
    if not source.is_file():
        raise PipelineError(f"Cannot install runtime manifest because {rel(source)} is missing")
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    record.setdefault("outputs_observed", []).append(file_ref(target))


def prior_required_rebuild_gaps(ctx: PipelineContext) -> list[dict[str, Any]]:
    gaps: list[dict[str, Any]] = []
    for stage in ("export-cache-derived-assets", "export-defs", "export-render-assets"):
        path = ctx.pipeline_root / "stages" / f"{stage}.json"
        if not path.is_file():
            continue
        data = load_json(path)
        for gap in data.get("required_rebuild_gaps") or []:
            item = dict(gap)
            item["stage"] = stage
            gaps.append(item)
    return gaps


def fail_if_required_rebuild_gaps(ctx: PipelineContext, record: dict[str, Any]) -> None:
    gaps = prior_required_rebuild_gaps(ctx)
    if not gaps:
        return
    record["blocked_by_required_rebuild_gaps"] = gaps
    datasets = ", ".join(f"{gap['stage']}:{gap['dataset']}" for gap in gaps[:12])
    extra = "" if len(gaps) <= 12 else f", ... {len(gaps) - 12} more"
    raise PipelineError(
        "refusing to pack runtime data with unresolved required rebuild gaps: "
        + datasets
        + extra
    )


def stage_validate_repo_self_contained(ctx: PipelineContext, record: dict[str, Any]) -> None:
    run_command([sys.executable, "tools/validate_no_external_repos.py", "--strict"], record)
    run_command([sys.executable, "tools/validate_pipeline_inputs.py"], record)


def run_source_authority_validators(ctx: PipelineContext, record: dict[str, Any], *, write_gap_report: bool) -> None:
    run_command([sys.executable, "tools/validate_sources.py"], record)
    run_command([sys.executable, "tools/validate_runtime_data_lock.py"], record)
    authority_args = [sys.executable, "tools/validate_source_authority.py"]
    if write_gap_report:
        authority_args.append("--write-gap-report")
    run_command(authority_args, record)


def stage_validate_source_authority(ctx: PipelineContext, record: dict[str, Any]) -> None:
    run_source_authority_validators(ctx, record, write_gap_report=True)
    for name in ("source_gaps.json", "source_gaps.txt"):
        path = ctx.report_root / name
        record.setdefault("outputs_observed", []).append(file_ref(path))


def stage_validate_content(ctx: PipelineContext, record: dict[str, Any]) -> None:
    run_command([sys.executable, "tools/validate_content_layout.py", "--strict-no-legacy"], record)
    run_command([sys.executable, "tools/validate_direct_global_access.py"], record)
    run_command([sys.executable, "tools/validate_schemas.py"], record)


def stage_export_content(ctx: PipelineContext, record: dict[str, Any]) -> None:
    record["mode"] = "regenerate_tracked_content_outputs"
    record["notes"] = [
        "Regenerates outputs whose approved inputs are tracked under content/.",
        "Deferred content datasets stay explicit until their exporters no longer read wiki/cache-derived inputs.",
    ]
    record["exporters"] = [
        {"dataset": dataset, "script": script}
        for dataset, script in CONTENT_EXPORT_COMMANDS
    ]
    record["deferred_content_exports"] = list(DEFERRED_CONTENT_EXPORTS)
    for _, script in CONTENT_EXPORT_COMMANDS:
        run_command([sys.executable, script], record)
    record["inventory"] = {
        "content": file_ref(ROOT / "content", include_sha=False),
        "defs": tree_summary(ctx.data_root, ("defs",)),
    }
    record.setdefault("outputs_observed", []).extend(
        file_ref(ROOT / output)
        for output in CONTENT_EXPORT_OUTPUTS
    )


def stage_export_cache_derived_assets(ctx: PipelineContext, record: dict[str, Any]) -> None:
    record["mode"] = "rebuild_from_explicit_maintainer_inputs"
    record["notes"] = [
        "Cache-derived release assets require explicit maintainer b237 inputs.",
        "If those inputs are absent, the stage records required rebuild gaps and the pack stage refuses to publish stale loose outputs.",
    ]
    emit_rebuild_plan(ctx, record, CACHE_DERIVED_REBUILD_SPECS)
    if not missing_required_inputs(B237_CACHE_AND_DUMP_INPUTS):
        run_command(
            [
                sys.executable,
                "tools/cache_pipeline/validate_b237_cache.py",
                "--cache",
                b237(),
                "--dump",
                b237_dump(),
            ],
            record,
        )
        run_rebuild_specs(ctx, record, CACHE_DERIVED_REBUILD_SPECS)
    record["inventory"] = {
        "cache_asset_dirs": tree_summary(ctx.data_root, ("models", "anims", "sprites", "ui", "fonts", "regions")),
        "sources_lock": file_ref(ROOT / "data-sources/sources.lock"),
    }
    message = required_rebuild_gap_message(record)
    if message:
        record["status_detail"] = "blocked_required_rebuild_inputs"


def stage_export_defs(ctx: PipelineContext, record: dict[str, Any]) -> None:
    record["mode"] = "regenerate_or_block_required_definition_outputs"
    record["notes"] = [
        "Tracked content-derived defs are regenerated by export-content before this stage.",
        "Required non-content defs are either regenerated from explicit approved inputs or recorded as required rebuild gaps.",
    ]
    emit_rebuild_plan(ctx, record, DEF_REBUILD_SPECS)
    run_rebuild_specs(ctx, record, DEF_REBUILD_SPECS)
    record["inventory"] = {
        "defs": tree_summary(ctx.data_root, ("defs",)),
        "schemas": file_ref(ROOT / "schema/defs", include_sha=False),
    }
    message = required_rebuild_gap_message(record)
    if message:
        record["status_detail"] = "blocked_required_rebuild_inputs"


def stage_export_render_assets(ctx: PipelineContext, record: dict[str, Any]) -> None:
    record["mode"] = "rebuild_post_definition_render_assets"
    record["notes"] = [
        "Render-facing outputs that depend on regenerated definitions run after export-defs.",
        "The validation item-icon overlay is non-release-required and exists to keep manual validation recognizable while the b237 icon renderer matures.",
    ]
    emit_rebuild_plan(ctx, record, POST_DEF_RENDER_REBUILD_SPECS)
    run_rebuild_specs(ctx, record, POST_DEF_RENDER_REBUILD_SPECS)
    record["inventory"] = {
        "render_assets": tree_summary(ctx.data_root, ("models", "anims", "sprites", "ui", "fonts")),
    }
    message = required_rebuild_gap_message(record)
    if message:
        record["status_detail"] = "blocked_required_rebuild_inputs"


def stage_pack_runtime_data(ctx: PipelineContext, record: dict[str, Any]) -> None:
    fail_if_required_rebuild_gaps(ctx, record)
    args = [
        sys.executable,
        "tools/pack_runtime_data.py",
        "--data-root",
        str(ctx.data_root),
        "--output",
        str(ctx.dist_root),
        "--version",
        ctx.version,
        "--force",
    ]
    run_command(args, record)
    copy_dist_manifest_to_data(ctx, record)
    record.setdefault("outputs_observed", []).extend(
        [
            file_ref(ctx.dist_root / "manifest.json"),
            file_ref(ctx.dist_root / "packs", include_sha=False),
        ]
    )


def stage_unpack_runtime_data(ctx: PipelineContext, record: dict[str, Any]) -> None:
    args = [
        sys.executable,
        "tools/unpack_runtime_data.py",
        "--data-dir",
        str(ctx.data_root),
        "--manifest",
        str(ctx.dist_root / "manifest.json"),
        "--packs-dir",
        str(ctx.dist_root / "packs"),
    ]
    run_command(args, record)
    copy_dist_manifest_to_data(ctx, record)
    record.setdefault("outputs_observed", []).append(file_ref(ctx.data_root / ".runtime-unpacked"))


def stage_generate_reports(ctx: PipelineContext, record: dict[str, Any]) -> None:
    run_command(
        [
            sys.executable,
            "tools/export_area_flags.py",
            "--out-dir",
            str(ctx.report_root),
        ],
        record,
    )
    run_command([sys.executable, "tools/report_database_completion.py"], record)
    ctx.report_root.mkdir(parents=True, exist_ok=True)
    copied: list[dict[str, Any]] = []
    for source_dir_name in REPORT_SOURCE_DIRS:
        source_dir = ROOT / source_dir_name
        if not source_dir.is_dir():
            continue
        target_dir = ctx.report_root / "current" / source_dir.name
        if target_dir.exists():
            shutil.rmtree(target_dir)
        target_dir.mkdir(parents=True, exist_ok=True)
        for source in sorted(p for p in source_dir.rglob("*") if p.is_file()):
            relative = source.relative_to(source_dir)
            target = target_dir / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            copied.append(file_ref(target))

    index = {
        "format": "runec-generated-report-index-v1",
        "generated_utc": utc_now(),
        "sources": [rel(ROOT / d) for d in REPORT_SOURCE_DIRS],
        "copied_reports": copied,
        "source_gap_reports": [
            file_ref(ctx.report_root / "source_gaps.json"),
            file_ref(ctx.report_root / "source_gaps.txt"),
        ],
    }
    write_json(ctx.report_root / "index.json", index)
    record["reports_copied"] = len(copied)
    record.setdefault("outputs_observed", []).extend(
        [
            file_ref(ctx.report_root / "index.json"),
            file_ref(ctx.report_root, include_sha=False),
        ]
    )


def stage_validate_reports(ctx: PipelineContext, record: dict[str, Any]) -> None:
    run_command(
        [
            sys.executable,
            "tools/validate_reports.py",
            "--report-root",
            str(ctx.report_root),
            "--data-root",
            str(ctx.data_root),
            "--dist-root",
            str(ctx.dist_root),
            "--version",
            ctx.version,
        ],
        record,
    )
    for name in ("report-validation.json", f"data-{ctx.version}-summary.json", f"data-{ctx.version}-summary.txt"):
        record.setdefault("outputs_observed", []).append(file_ref(ctx.report_root / name))


STAGE_FUNCS: dict[str, Callable[[PipelineContext, dict[str, Any]], None]] = {
    "validate-repo-self-contained": stage_validate_repo_self_contained,
    "validate-source-authority": stage_validate_source_authority,
    "validate-content": stage_validate_content,
    "export-content": stage_export_content,
    "export-cache-derived-assets": stage_export_cache_derived_assets,
    "export-defs": stage_export_defs,
    "export-render-assets": stage_export_render_assets,
    "pack-runtime-data": stage_pack_runtime_data,
    "unpack-runtime-data": stage_unpack_runtime_data,
    "generate-reports": stage_generate_reports,
    "validate-reports": stage_validate_reports,
}


def write_stage_record(ctx: PipelineContext, stage: str, record: dict[str, Any]) -> None:
    write_json(ctx.pipeline_root / "stages" / f"{stage}.json", record)


def run_stage(ctx: PipelineContext, stage: str) -> dict[str, Any]:
    spec = STAGE_SPECS[stage]
    record: dict[str, Any] = {
        "format": "runec-pipeline-stage-record-v1",
        "stage": stage,
        "description": spec.description,
        "status": "running",
        "started_utc": utc_now(),
        "declared_inputs": list(spec.inputs),
        "declared_outputs": list(spec.outputs),
        "commands": [],
    }
    started = time.monotonic()
    print(f"\n== {stage} ==", flush=True)
    try:
        STAGE_FUNCS[stage](ctx, record)
        record["status"] = "passed"
    except Exception as exc:
        record["status"] = "failed"
        record["error"] = str(exc)
        record["finished_utc"] = utc_now()
        record["duration_seconds"] = round(time.monotonic() - started, 3)
        write_stage_record(ctx, stage, record)
        raise
    record["finished_utc"] = utc_now()
    record["duration_seconds"] = round(time.monotonic() - started, 3)
    write_stage_record(ctx, stage, record)
    return record


def write_build_record(ctx: PipelineContext, stage_records: list[dict[str, Any]]) -> None:
    manifest = ctx.dist_root / "manifest.json"
    data_manifest = ctx.data_root / "manifest.json"
    packs_dir = ctx.dist_root / "packs"
    record = {
        "format": "runec-pipeline-build-record-v1",
        "generated_utc": utc_now(),
        "version": ctx.version,
        "stages": [
            {
                "stage": r.get("stage"),
                "status": r.get("status"),
                "duration_seconds": r.get("duration_seconds"),
            }
            for r in stage_records
        ],
        "data_manifest": file_ref(data_manifest) if data_manifest.exists() else file_ref(data_manifest, include_sha=False),
        "dist_manifest": file_ref(manifest) if manifest.exists() else file_ref(manifest, include_sha=False),
        "dist_packs": file_ref(packs_dir, include_sha=False),
        "runtime_tree": tree_summary(ctx.data_root, DATA_RUNTIME_DIRS),
    }
    write_json(ctx.pipeline_root / "build.json", record)


def clean_generated(ctx: PipelineContext) -> None:
    targets = [
        ctx.pipeline_root,
        ctx.report_root,
        ctx.data_root / "manifest.json",
        ctx.data_root / ".runtime-unpacked",
        ctx.data_root / "packs",
        ctx.data_root / "regions" / "scene_cache",
        ctx.dist_root / "manifest.json",
        ctx.dist_root / "packs",
    ]
    for target in targets:
        if not target.exists():
            continue
        if target.is_dir():
            print(f"Removing {rel(target)}/")
            shutil.rmtree(target)
        else:
            print(f"Removing {rel(target)}")
            target.unlink()


def run_check(ctx: PipelineContext) -> None:
    print("Checking repository validators")
    transient_record: dict[str, Any] = {"commands": []}
    stage_validate_repo_self_contained(ctx, transient_record)
    run_source_authority_validators(ctx, transient_record, write_gap_report=False)
    stage_validate_content(ctx, transient_record)

    print("Checking required rebuild coverage")
    rebuild_gaps = prior_required_rebuild_gaps(ctx)
    if rebuild_gaps:
        datasets = ", ".join(f"{gap['stage']}:{gap['dataset']}" for gap in rebuild_gaps[:12])
        extra = "" if len(rebuild_gaps) <= 12 else f", ... {len(rebuild_gaps) - 12} more"
        raise PipelineError(
            "required runtime outputs still have rebuild gaps: "
            + datasets
            + extra
        )

    print("Checking loose data manifest")
    data_manifest = ctx.data_root / "manifest.json"
    if not data_manifest.is_file():
        raise PipelineError(f"Missing {rel(data_manifest)}; run `python3 tools/data_pipeline.py all`")
    asset_failures = manifest_asset_checks(data_manifest, ctx.data_root)
    if asset_failures:
        sample = ", ".join(f"{f['path']}:{f['reason']}" for f in asset_failures[:5])
        raise PipelineError(f"Loose data manifest check failed for {len(asset_failures)} assets: {sample}")

    print("Checking runtime packs")
    dist_manifest = ctx.dist_root / "manifest.json"
    if not dist_manifest.is_file():
        raise PipelineError(f"Missing {rel(dist_manifest)}; run `python3 tools/data_pipeline.py all`")
    pack_failures = manifest_pack_checks(dist_manifest, ctx.dist_root / "packs")
    if pack_failures:
        sample = ", ".join(f"{f['path']}:{f['reason']}" for f in pack_failures[:5])
        raise PipelineError(f"Runtime pack check failed for {len(pack_failures)} packs: {sample}")

    print("Checking generated reports")
    missing_reports = [
        rel(path)
        for path in (
            ctx.report_root / "index.json",
            ctx.report_root / "source_gaps.json",
            ctx.report_root / "source_gaps.txt",
        )
        if not path.is_file()
    ]
    if missing_reports:
        raise PipelineError("Missing generated reports: " + ", ".join(missing_reports))
    run_command(
        [
            sys.executable,
            "tools/validate_reports.py",
            "--report-root",
            str(ctx.report_root),
            "--data-root",
            str(ctx.data_root),
            "--dist-root",
            str(ctx.dist_root),
            "--version",
            ctx.version,
            "--check-only",
        ],
        transient_record,
    )
    print("Pipeline check passed")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "command",
        nargs="?",
        default="all",
        choices=["all", "list-stages", *STAGE_ORDER],
        help="Pipeline command or individual stage to run.",
    )
    parser.add_argument("--data-root", type=Path, default=ROOT / "data", help="Runtime data root.")
    parser.add_argument("--dist-root", type=Path, default=ROOT / "dist-data", help="Pack output root.")
    parser.add_argument("--generated-root", type=Path, default=ROOT / "generated", help="Generated records/report root.")
    parser.add_argument("--version", default="v1", help="Runtime data version string for generated manifests.")
    parser.add_argument("--check", action="store_true", help="Validate current generated outputs without rebuilding packs.")
    parser.add_argument(
        "--clean-generated",
        action="store_true",
        help="Remove pipeline-generated manifests, packs, reports, and records before running the command.",
    )
    parser.add_argument("--force", action="store_true", help="Reserved for future stricter stage rebuild controls.")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    ctx = PipelineContext(
        data_root=args.data_root,
        dist_root=args.dist_root,
        generated_root=args.generated_root,
        version=args.version,
        check=args.check,
        force=args.force,
    )

    if args.command == "list-stages":
        for stage in STAGE_ORDER:
            spec = STAGE_SPECS[stage]
            print(f"{stage}: {spec.description}")
        return 0

    try:
        if args.clean_generated:
            clean_generated(ctx)

        if args.check:
            run_check(ctx)
            return 0

        stages = STAGE_ORDER if args.command == "all" else [args.command]
        stage_records = [run_stage(ctx, stage) for stage in stages]
        write_build_record(ctx, stage_records)
        print(f"\nWrote {rel(ctx.pipeline_root / 'build.json')}")
        return 0
    except PipelineError as exc:
        print(f"data_pipeline.py: error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
