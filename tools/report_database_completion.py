#!/usr/bin/env python3
"""Emit the database-complete v1 closure report.

This report is intentionally about database coverage, not exact runtime
behavior. It closes the data-catalog milestone when the current exported
datasets and coverage reports are present, no activity schema blocks
parity, and the known source limitations are explicitly accepted.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REPORTS = ROOT / "tools/reports"
OUT = REPORTS / "database_completion.txt"

STATUS_READY = {"READY", "READY_WITH_ACCEPTED_SIMPLIFICATIONS"}


@dataclass(frozen=True)
class DatasetCheck:
    name: str
    report: str | None
    required_patterns: tuple[str, ...] = ()
    binaries: tuple[str, ...] = ()
    accepted_status: bool = True
    source_gap_allowed: bool = False


CHECKS: tuple[DatasetCheck, ...] = (
    DatasetCheck("NPC definitions and reconciliation", "npc_defs_full.txt", ("NPC",)),
    DatasetCheck("Items, equipment, item forms, and item effects", "items_full.txt", ("item",)),
    DatasetCheck("Object definitions", "object_defs.txt", ("object",)),
    DatasetCheck("Object placements", "object_placements.txt", ("status:",)),
    DatasetCheck("Object behavior rules", "object_behaviors.txt", ("status:",)),
    DatasetCheck("Collision tiles", "collision_tiles.txt", ("status:",)),
    DatasetCheck(
        "Area flags",
        "area_flags.txt",
        ("authoritative_osrs:", "rows:"),
        source_gap_allowed=True,
    ),
    DatasetCheck("World and activity spawn sources", "spawn_sources.txt", ("unresolved required activity markers:",)),
    DatasetCheck("Spawn coverage", "spawn_coverage.txt", ("NPCList spawns:",)),
    DatasetCheck("Drops and shared drop tables", "drops.txt", ("drop",)),
    DatasetCheck("RDT/GDT/MRDT", "rdt_gdt.txt", ("table",)),
    DatasetCheck("Non-drop acquisition sources", "acquisition_sources.txt", ("source",)),
    DatasetCheck("Skill drops", "skill_drops.txt", ("entries:",)),
    DatasetCheck("Gathering nodes", "gathering_nodes.txt", ("status:",)),
    DatasetCheck("Traversal edges", "traversal_edges.txt", ("status:",)),
    DatasetCheck("Recipes", None, binaries=("data/defs/recipes.bin",)),
    DatasetCheck("Shops", "shops.txt", ("shop",)),
    DatasetCheck("Prayers", "prayers.txt", ("prayer",)),
    DatasetCheck("Spells", "spells.txt", ("spell",)),
    DatasetCheck("Player actions", "player_actions.txt", ("action",)),
    DatasetCheck("Quests", "quest_steps.txt", ("quest",)),
    DatasetCheck("Dialogue transcripts", "dialogue.txt", ("dialogue",)),
    DatasetCheck("Varbits/varps", None, binaries=("data/defs/varbits.bin", "data/defs/varps.bin")),
    DatasetCheck("Teleports", None, binaries=("data/defs/teleports.bin",)),
    DatasetCheck("Slayer", "slayer.txt", ("slayer",)),
    DatasetCheck("Regular NPC mechanics", "regular_npc_mechanics.txt", ("mechanics",)),
    DatasetCheck("Activity mechanics", "activity_mechanics.txt", ("activity",)),
    DatasetCheck(
        "Activity schemas",
        "activity_schemas.txt",
        ("BLOCKS_PARITY: 0", "schemas with unresolved required data: 0"),
    ),
    DatasetCheck("Normalization", "normalization.txt", ("normalization",)),
)


def read_report(name: str) -> str:
    path = REPORTS / name
    if not path.exists():
        raise SystemExit(f"missing report: {path.relative_to(ROOT)}")
    return path.read_text(errors="replace")


def extract_status(text: str) -> str:
    match = re.search(r"^status:\s*([A-Z_]+)", text, re.M)
    if not match:
        return "PRESENT"
    return match.group(1)


def extract_metric(text: str, label: str) -> str | None:
    match = re.search(rf"^{re.escape(label)}:\s*(.+)$", text, re.M)
    return match.group(1).strip() if match else None


def validate_check(check: DatasetCheck) -> tuple[str, str]:
    if check.report is None:
        for raw in check.binaries:
            path = ROOT / raw
            if not path.exists():
                raise SystemExit(f"missing binary: {raw}")
            if path.stat().st_size <= 0:
                raise SystemExit(f"empty binary: {raw}")
        return "BINARY_PRESENT", ", ".join(check.binaries)

    text = read_report(check.report)
    folded = text.lower()
    for pattern in check.required_patterns:
        if pattern.lower() not in folded:
            raise SystemExit(f"{check.report} missing required marker: {pattern}")
    status = extract_status(text)
    if status == "SOURCE_GAP" and check.source_gap_allowed:
        return status, check.report
    if check.accepted_status and status != "PRESENT" and status not in STATUS_READY:
        raise SystemExit(f"{check.report} has non-accepted status: {status}")
    return status, check.report


def main() -> None:
    rows: list[tuple[str, str, str]] = []
    for check in CHECKS:
        status, report = validate_check(check)
        rows.append((check.name, status, report))
    has_source_gap = any(status == "SOURCE_GAP" for _, status, _ in rows)

    activity_text = read_report("activity_schemas.txt")
    area_text = read_report("area_flags.txt")
    spawn_text = read_report("spawn_sources.txt")
    object_text = read_report("object_behaviors.txt")
    gathering_text = read_report("gathering_nodes.txt")
    traversal_text = read_report("traversal_edges.txt")

    lines: list[str] = [
        "RuneC database completion closure",
        "",
        "status: "
        + (
            "DATABASE_COMPLETE_V1_WITH_SOURCE_GAPS"
            if has_source_gap
            else "DATABASE_COMPLETE_V1_WITH_ACCEPTED_SOURCE_LIMITATIONS"
        ),
        "scope: exported database coverage, source catalog coverage, and loadable schema/index coverage",
        "excludes: exact encounter logic parity, exact tick timing, UI behavior, and full runtime gameplay parity",
        "",
        "Closure criteria",
        "- all required database-category reports are present",
        "- report statuses are READY or READY_WITH_ACCEPTED_SIMPLIFICATIONS where a status is emitted",
        "- source-authority gaps are closed or represented as accepted first-release runtime snapshots",
        "- activity schemas have 0 BLOCKS_PARITY rows",
        "- activity schemas have 0 unresolved required data rows",
        "- remaining limitations are runtime-consumer/parity deepening work, not missing database categories",
        "",
        "Key metrics",
        f"- activity schemas: {extract_metric(activity_text, 'activity schemas') or 'unknown'}",
        f"- activity BLOCKS_PARITY rows: {extract_metric(activity_text, 'BLOCKS_PARITY') or '0'}",
        f"- activity unresolved required data: {extract_metric(activity_text, 'schemas with unresolved required data') or '0'}",
        f"- area flag rows: {extract_metric(area_text, 'rows') or 'unknown'}",
        f"- area flag authoritative_osrs: {extract_metric(area_text, 'authoritative_osrs') or 'unknown'}",
        f"- compiled static spawn source rows: {extract_metric(spawn_text, 'source rows') or 'unknown'}",
        f"- object behavior rows: {extract_metric(object_text, 'behavior rows') or 'unknown'}",
        f"- gathering nodes: {extract_metric(gathering_text, 'nodes') or 'unknown'}",
        f"- traversal edges: {extract_metric(traversal_text, 'edges') or 'unknown'}",
        "",
        "Dataset checks",
    ]
    for name, status, report in rows:
        lines.append(f"- {name}: {status} ({report})")

    lines.extend(
        [
            "",
            "Accepted source limitations",
            "- Area flags are accepted for the first release through the tracked RuneC-owned runtime snapshot; semantic polygon-table normalization remains future data-deepening work.",
            "- Wilderness-level values are generated from the local b237 clientscript path and remain source-backed within the local corpus.",
            "- Static spawn direction/wander fidelity is accepted as a first-release simplification in the tracked runtime snapshot; spawn identity, coordinates, instance/activity markers, and activity-local anchors are database-covered.",
            "- Object/interactable coverage is category-complete, but uncommon object-specific effects and exact door-pair replacement rules remain focused runtime-consumer work.",
            "- Per-skill rules are database-indexed through recipes, gathering nodes, skill drops, object behaviors, and traversal/action indexes; exact rolls, tool checks, and uncommon activity-local skilling behavior remain runtime-consumer work.",
            "- Nex/Sol and other activity fixtures have loadable schemas with unresolved required data at 0; exact arena geometry/timing remains encounter/activity parity work.",
            "- Music/audio packs are optional for the v1 gameplay data release gate and are not required runtime database closure inputs.",
            "",
            "Conclusion",
            (
                "- Database completion v1 remains open for source-authority "
                "closure."
                if has_source_gap
                else "- Database completion v1 is closed."
            ),
            (
                "- Resolve SOURCE_GAP datasets before treating the database "
                "milestone as source-authoritative."
                if has_source_gap
                else "- Future work should treat new data additions as targeted parity/deepening work, not as blockers to the database-completion milestone."
            ),
        ]
    )

    OUT.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
