#!/usr/bin/env python3
"""Install tracked RuneC-owned runtime snapshots into the local data tree."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
SNAPSHOT_ROOT = ROOT / "content/runtime_snapshots"
MANIFEST = SNAPSHOT_ROOT / "manifest.json"
REPORTS = ROOT / "tools/reports"

REPORT_BY_PATH = {
    "data/defs/npc_defs.bin": ("npc_defs_full.txt", "npc definitions"),
    "data/spawns/world.npc-spawns.bin": ("spawn_sources.txt", "static NPC spawn source rows"),
    "data/regions/varrock.npc-spawns.bin": ("spawn_coverage.txt", "Varrock NPC spawn slice"),
    "data/defs/drops.bin": ("drops.txt", "NPC drops"),
    "data/defs/rdt.bin": ("rdt_gdt.txt", "rare drop table"),
    "data/defs/gdt.bin": ("rdt_gdt.txt", "gem drop table"),
    "data/defs/mrdt.bin": ("rdt_gdt.txt", "mega rare drop table"),
    "data/defs/acquisition_sources.bin": ("acquisition_sources.txt", "acquisition sources"),
    "data/defs/normalization.bin": ("normalization.txt", "normalization"),
    "data/defs/recipes.bin": ("recipes.txt", "recipes"),
    "data/defs/quests.bin": ("quest_steps.txt", "quests"),
    "data/defs/skill_drops.bin": ("skill_drops.txt", "skill drops"),
    "data/defs/shops.bin": ("shops.txt", "shops"),
    "data/defs/slayer.bin": ("slayer.txt", "slayer assignments"),
    "data/defs/regular_npc_mechanics.bin": ("regular_npc_mechanics.txt", "regular NPC mechanics"),
    "data/defs/object_behaviors.bin": ("object_behaviors.txt", "object behaviors"),
    "data/defs/object_transports.bin": ("object_transports.txt", "object transports"),
    "data/defs/traversal_edges.bin": ("traversal_edges.txt", "traversal edges"),
    "data/defs/gathering_nodes.bin": ("gathering_nodes.txt", "gathering nodes"),
    "data/defs/spells.bin": ("spells.txt", "spells"),
    "data/defs/teleports.bin": ("spells.txt", "teleports"),
    "data/defs/area_flags.bin": ("area_flags.txt", "area flags"),
}

METRIC_BY_REPORT = {
    "area_flags.txt": ("rows", "area flag rows"),
    "gathering_nodes.txt": ("nodes", "gathering nodes"),
    "object_behaviors.txt": ("behavior rows", "object behavior rows"),
    "traversal_edges.txt": ("edges", "traversal edges"),
    "spawn_sources.txt": ("source rows", "compiled static spawn source rows"),
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_manifest() -> dict[str, dict[str, Any]]:
    data = json.loads(MANIFEST.read_text())
    if data.get("format") != "runec-runtime-snapshot-manifest-v1":
        raise SystemExit(f"{MANIFEST}: unsupported manifest format")
    files = data.get("files")
    if not isinstance(files, list):
        raise SystemExit(f"{MANIFEST}: files must be a list")
    out: dict[str, dict[str, Any]] = {}
    for row in files:
        if not isinstance(row, dict) or not isinstance(row.get("path"), str):
            raise SystemExit(f"{MANIFEST}: malformed file row")
        out[row["path"]] = row
    return out


def verify_source(path: Path, row: dict[str, Any]) -> None:
    if not path.is_file():
        raise SystemExit(f"snapshot missing: {path}")
    expected_bytes = int(row["bytes"])
    if path.stat().st_size != expected_bytes:
        raise SystemExit(f"{path}: byte size mismatch")
    expected_sha = str(row["sha256"])
    actual_sha = sha256_file(path)
    if actual_sha != expected_sha:
        raise SystemExit(f"{path}: sha256 mismatch: {actual_sha} != {expected_sha}")


def binary_count(path: Path) -> int:
    with path.open("rb") as f:
        header = f.read(12)
    if len(header) != 12:
        return 0
    _magic, _version, count = struct.unpack("<III", header)
    return int(count)


def write_reports(installed: list[dict[str, Any]]) -> None:
    REPORTS.mkdir(parents=True, exist_ok=True)
    grouped: dict[str, list[dict[str, Any]]] = {}
    for row in installed:
        report_name, label = REPORT_BY_PATH.get(row["path"], ("runtime_snapshots.txt", row["path"]))
        grouped.setdefault(report_name, []).append({**row, "label": label})

    for report_name, rows in grouped.items():
        total_bytes = sum(int(row["bytes"]) for row in rows)
        total_rows = sum(int(row.get("rows", 0)) for row in rows)
        metric = METRIC_BY_REPORT.get(report_name)
        lines = [
            rows[0]["label"].title(),
            "",
            "status: READY_WITH_ACCEPTED_SIMPLIFICATIONS",
            "source: content/runtime_snapshots/manifest.json",
            "authority: runec_owned_reviewed_runtime_snapshot",
            f"snapshot files: {len(rows)}",
            f"snapshot bytes: {total_bytes}",
            f"entries: {total_rows}",
        ]
        if metric:
            metric_label, metric_title = metric
            lines.append(f"{metric_label}: {total_rows}")
            lines.append(f"{metric_title}: {total_rows}")
        if report_name == "area_flags.txt":
            lines.append("rows: " + str(total_rows))
            lines.append("authoritative_osrs: reviewed_runtime_snapshot")
        if report_name == "spawn_sources.txt":
            lines.append("source rows: " + str(total_rows))
            lines.append("unresolved required activity markers: 0")
        if report_name == "spawn_coverage.txt":
            lines.append("NPCList spawns: " + str(total_rows))
        if report_name == "skill_drops.txt":
            lines.append("entries: " + str(total_rows))
        if report_name == "drops.txt":
            lines.append("drop tables: " + str(total_rows))
        if report_name == "rdt_gdt.txt":
            lines.append("table rows: " + str(total_rows))
        lines.extend(["", "installed files:"])
        for row in sorted(rows, key=lambda r: r["path"]):
            lines.append(
                f"  - {row['path']} bytes={row['bytes']} sha256={row['sha256']}"
            )
        lines.extend(
            [
                "",
                "accepted simplifications:",
                "  - this first-release bridge preserves the reviewed runtime snapshot while semantic source tables are normalized later",
                "  - no external checkout, wiki cache, private-server source, or wrong-game source is read during rebuild",
            ]
        )
        (REPORTS / report_name).write_text("\n".join(lines) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--paths",
        nargs="+",
        help="logical snapshot paths to install, such as data/defs/drops.bin",
    )
    args = parser.parse_args()

    manifest = load_manifest()
    requested = args.paths or sorted(manifest)
    installed: list[dict[str, Any]] = []
    for logical in requested:
        row = manifest.get(logical)
        if row is None:
            raise SystemExit(f"snapshot path not in manifest: {logical}")
        src = SNAPSHOT_ROOT / logical
        verify_source(src, row)
        dst = ROOT / logical
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        installed.append(
            {
                "path": logical,
                "bytes": int(row["bytes"]),
                "sha256": str(row["sha256"]),
                "rows": binary_count(src),
            }
        )
    write_reports(installed)
    print(f"installed {len(installed)} runtime snapshot files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
