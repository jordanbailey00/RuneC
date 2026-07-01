#!/usr/bin/env python3
"""Validate generated reports as RuneC data-release gates."""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]

REQUIRED_REPORTS = (
    "acquisition_sources.txt",
    "activity_mechanics.txt",
    "activity_schemas.txt",
    "activity_spawns.txt",
    "activity_states.txt",
    "area_flags.txt",
    "area_flags_sources.txt",
    "collision_tiles.txt",
    "combat_visuals.txt",
    "database_completion.txt",
    "dialogue.txt",
    "drops.txt",
    "encounters.txt",
    "gathering_nodes.txt",
    "item_effects.txt",
    "item_specials.txt",
    "items_full.txt",
    "normalization.txt",
    "npc_defs_full.txt",
    "npc_models_full.txt",
    "npc_reconciliation.txt",
    "object_behaviors.txt",
    "object_defs.txt",
    "object_placements.txt",
    "object_transports.txt",
    "player_actions.txt",
    "prayers.txt",
    "quest_steps.txt",
    "rdt_gdt.txt",
    "regular_npc_mechanics.txt",
    "shops.txt",
    "skill_drops.txt",
    "slayer.txt",
    "spawn_completeness.txt",
    "spawn_coverage.txt",
    "spawn_sources.txt",
    "spells.txt",
    "traversal_edges.txt",
)

ALLOWED_STATUSES = {
    "DATABASE_COMPLETE_V1_WITH_SOURCE_GAPS",
    "DATABASE_COMPLETE_V1_WITH_ACCEPTED_SOURCE_LIMITATIONS",
    "PRESENT",
    "READY",
    "READY_WITH_ACCEPTED_SIMPLIFICATIONS",
    "SOURCE_GAP",
    "BINARY_PRESENT",
    "PROVISIONAL_RUNTIME_DATASET_READY / AUTHORITATIVE_SIGNOFF_DEFERRED",
}

REQUIRED_SOURCE_GAP_DATASETS: set[str] = set()

BLOCKED_RUNTIME_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    ("void_rsps", re.compile(r"\bvoid_rsps\b")),
    ("VoidPS", re.compile(r"\bVoidPS\b")),
    ("voidps provenance", re.compile(r"\bvoidps:")),
    ("2011Scape", re.compile(r"\b2011Scape\b")),
    ("SCAPE_2011", re.compile(r"\bSCAPE_2011\b")),
    ("near_reality", re.compile(r"\bnear_reality\b")),
    ("Near-Reality", re.compile(r"\bNear-Reality\b")),
    ("MapLocations.java", re.compile(r"\bMapLocations\.java\b")),
    ("Zenyte", re.compile(r"\bZenyte\b")),
    ("runelite checkout", re.compile(r"\bdata/source/runelite\b")),
    ("rsmod checkout", re.compile(r"\btools/cache_pipeline/source/rsmod\b")),
    ("osrsreboxed checkout", re.compile(r"\bdata/source/osrsreboxed-db\b")),
    ("data_osrs checkout", re.compile(r"\bdata/source/data_osrs\b")),
    ("osrs-dumps checkout", re.compile(r"\btools/cache_pipeline/source/osrs-dumps\b")),
    ("wiki cache", re.compile(r"\btools/wiki_cache\b")),
)

AGGREGATE_COMPARISONS = (
    ("activity_schemas", "activity schemas", "activity_schemas.txt", "activity schemas"),
    ("activity_blocks_parity", "activity BLOCKS_PARITY rows", "activity_schemas.txt", "BLOCKS_PARITY"),
    (
        "activity_unresolved_required_data",
        "activity unresolved required data",
        "activity_schemas.txt",
        "schemas with unresolved required data",
    ),
    ("area_flag_rows", "area flag rows", "area_flags.txt", "rows"),
    ("compiled_static_spawn_source_rows", "compiled static spawn source rows", "spawn_sources.txt", "source rows"),
    ("object_behavior_rows", "object behavior rows", "object_behaviors.txt", "behavior rows"),
    ("gathering_nodes", "gathering nodes", "gathering_nodes.txt", "nodes"),
    ("traversal_edges", "traversal edges", "traversal_edges.txt", "edges"),
)


@dataclass(frozen=True)
class GateFinding:
    gate: str
    severity: str
    message: str
    path: str | None = None


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def reports_dir(report_root: Path) -> Path:
    current = report_root / "current" / "reports"
    return current if current.is_dir() else report_root


def extract_status_lines(text: str) -> list[str]:
    statuses: list[str] = []
    for match in re.finditer(r"^status:\s*(.+?)\s*$", text, re.M):
        statuses.append(match.group(1).strip())
    return statuses


def extract_dataset_statuses(database_completion: str) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    pattern = re.compile(r"^- (?P<name>[^:]+): (?P<status>[A-Z_]+) \((?P<source>[^)]+)\)$", re.M)
    for match in pattern.finditer(database_completion):
        rows.append(
            {
                "name": match.group("name"),
                "status": match.group("status"),
                "source": match.group("source"),
            }
        )
    return rows


def extract_metric(text: str, label: str) -> int | None:
    pattern = re.compile(rf"^\s*-?\s*{re.escape(label)}:\s*([0-9]+)\b", re.M)
    match = pattern.search(text)
    if not match:
        return None
    return int(match.group(1))


def report_statuses(report_paths: dict[str, Path]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for name, path in sorted(report_paths.items()):
        if not path.is_file():
            out[name] = {"path": rel(path), "statuses": ["MISSING"]}
            continue
        text = read_text(path)
        statuses = extract_status_lines(text)
        out[name] = {
            "path": rel(path),
            "statuses": statuses or ["PRESENT"],
        }
    return out


def add_required_report_findings(report_paths: dict[str, Path], findings: list[GateFinding]) -> None:
    for name in REQUIRED_REPORTS:
        path = report_paths.get(name)
        if path is None or not path.is_file():
            findings.append(GateFinding("required_reports", "error", f"missing required report: {name}", name))


def add_status_findings(report_paths: dict[str, Path], findings: list[GateFinding]) -> list[dict[str, str]]:
    dataset_rows: list[dict[str, str]] = []
    for name, path in sorted(report_paths.items()):
        if not path.is_file():
            continue
        text = read_text(path)
        for status in extract_status_lines(text):
            if status not in ALLOWED_STATUSES:
                findings.append(GateFinding("allowed_statuses", "error", f"unsupported status: {status}", rel(path)))
        if name == "database_completion.txt":
            dataset_rows = extract_dataset_statuses(text)
            for row in dataset_rows:
                if row["status"] not in ALLOWED_STATUSES:
                    findings.append(
                        GateFinding(
                            "allowed_statuses",
                            "error",
                            f"unsupported dataset status for {row['name']}: {row['status']}",
                            rel(path),
                        )
                    )
    return dataset_rows


def add_blocks_parity_findings(report_paths: dict[str, Path], findings: list[GateFinding]) -> None:
    activity = report_paths.get("activity_schemas.txt")
    if not activity:
        return
    text = read_text(activity)
    blocks = extract_metric(text, "BLOCKS_PARITY")
    unresolved = extract_metric(text, "schemas with unresolved required data")
    if blocks != 0:
        findings.append(GateFinding("blocks_parity", "error", f"activity BLOCKS_PARITY rows: {blocks}", rel(activity)))
    if unresolved != 0:
        findings.append(
            GateFinding("blocks_parity", "error", f"activity unresolved required data rows: {unresolved}", rel(activity))
        )
    for line in text.splitlines():
        if "status=BLOCKS_PARITY" in line:
            findings.append(GateFinding("blocks_parity", "error", f"BLOCKS_PARITY row: {line}", rel(activity)))


def runtime_text_artifacts(data_root: Path, dist_root: Path) -> list[Path]:
    candidates = [
        data_root / "manifest.json",
        dist_root / "manifest.json",
        data_root / ".runtime-unpacked",
    ]
    defs = data_root / "defs"
    if defs.is_dir():
        candidates.extend(sorted(defs.glob("*.tsv")))
    return [path for path in candidates if path.is_file()]


def add_blocked_runtime_findings(data_root: Path, dist_root: Path, findings: list[GateFinding]) -> None:
    for path in runtime_text_artifacts(data_root, dist_root):
        text = read_text(path)
        for label, pattern in BLOCKED_RUNTIME_PATTERNS:
            if pattern.search(text):
                findings.append(
                    GateFinding("blocked_runtime_provenance", "error", f"blocked runtime marker: {label}", rel(path))
                )


def add_source_gap_findings(report_root: Path, findings: list[GateFinding]) -> list[dict[str, Any]]:
    path = report_root / "source_gaps.json"
    if not path.is_file():
        findings.append(GateFinding("source_gaps", "error", "missing source_gaps.json", rel(path)))
        return []
    try:
        gaps = load_json(path)
    except json.JSONDecodeError as exc:
        findings.append(GateFinding("source_gaps", "error", f"invalid source_gaps.json: {exc}", rel(path)))
        return []
    if not isinstance(gaps, list):
        findings.append(GateFinding("source_gaps", "error", "source_gaps.json must be a list", rel(path)))
        return []

    normalized: list[dict[str, Any]] = []
    seen: set[str] = set()
    for idx, row in enumerate(gaps):
        if not isinstance(row, dict):
            findings.append(GateFinding("source_gaps", "error", f"gap row {idx} is not an object", rel(path)))
            continue
        dataset = str(row.get("dataset", ""))
        seen.add(dataset)
        missing = [key for key in ("dataset", "missing_fact", "current_consumer", "approved_sources_checked", "proposed_follow_up") if not row.get(key)]
        if missing:
            findings.append(GateFinding("source_gaps", "error", f"{dataset or idx}: missing fields {', '.join(missing)}", rel(path)))
        if not isinstance(row.get("approved_sources_checked"), list) or not row.get("approved_sources_checked"):
            findings.append(GateFinding("source_gaps", "error", f"{dataset or idx}: approved_sources_checked must be a non-empty list", rel(path)))
        normalized.append(row)

    missing_datasets = sorted(REQUIRED_SOURCE_GAP_DATASETS - seen)
    if gaps and missing_datasets:
        findings.append(
            GateFinding(
                "source_gaps",
                "error",
                "missing required removed-source gap datasets: " + ", ".join(missing_datasets),
                rel(path),
            )
        )
    required_gap_reports = ["source_gaps.txt"]
    if gaps:
        required_gap_reports.extend(["area_flags_gaps.json", "area_flags_gaps.txt"])
    for name in required_gap_reports:
        expected = report_root / name
        if not expected.is_file():
            findings.append(GateFinding("source_gaps", "error", f"missing generated gap report: {name}", rel(expected)))
    return normalized


def add_aggregate_findings(report_paths: dict[str, Path], findings: list[GateFinding]) -> list[dict[str, Any]]:
    database = report_paths.get("database_completion.txt")
    if database is None:
        return []
    database_text = read_text(database)
    checks: list[dict[str, Any]] = []
    for key, db_label, report_name, report_label in AGGREGATE_COMPARISONS:
        report = report_paths.get(report_name)
        if report is None:
            continue
        db_value = extract_metric(database_text, db_label)
        report_value = extract_metric(read_text(report), report_label)
        row = {
            "id": key,
            "aggregate_label": db_label,
            "report": report_name,
            "report_label": report_label,
            "aggregate_value": db_value,
            "report_value": report_value,
            "matches": db_value == report_value,
        }
        checks.append(row)
        if db_value is None or report_value is None:
            findings.append(GateFinding("aggregate_counts", "error", f"missing aggregate comparison metric: {key}", rel(database)))
        elif db_value != report_value:
            findings.append(
                GateFinding(
                    "aggregate_counts",
                    "error",
                    f"{key} mismatch: aggregate={db_value}, {report_name}={report_value}",
                    rel(database),
                )
            )
    return checks


def add_accepted_limitations_findings(report_paths: dict[str, Path], findings: list[GateFinding]) -> list[str]:
    database = report_paths.get("database_completion.txt")
    if database is None:
        return []
    text = read_text(database)
    marker = "Accepted source limitations"
    if marker not in text:
        findings.append(GateFinding("accepted_limitations", "error", "database completion report missing accepted limitations", rel(database)))
        return []
    limitations: list[str] = []
    in_section = False
    for line in text.splitlines():
        if line.strip() == marker:
            in_section = True
            continue
        if in_section and line.startswith("Conclusion"):
            break
        if in_section and line.startswith("- "):
            limitations.append(line[2:].strip())
    if not limitations:
        findings.append(GateFinding("accepted_limitations", "error", "accepted limitations section is empty", rel(database)))
    required_keywords = ("Area flags", "Static spawn", "Object/interactable", "Music/audio")
    for keyword in required_keywords:
        if not any(keyword in row for row in limitations):
            findings.append(GateFinding("accepted_limitations", "error", f"missing accepted limitation for {keyword}", rel(database)))
    return limitations


def manifest_summary(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {"path": rel(path), "exists": False}
    doc = load_json(path)
    return {
        "path": rel(path),
        "exists": True,
        "format": doc.get("format"),
        "data_version": doc.get("data_version"),
        "packs": len(doc.get("packs", [])),
        "assets": len(doc.get("assets", [])),
        "subsets": len(doc.get("subsets", [])),
    }


def validation_payload(args: argparse.Namespace) -> tuple[dict[str, Any], dict[str, Any], list[GateFinding]]:
    report_root = args.report_root
    current_reports = reports_dir(report_root)
    report_paths = {name: current_reports / name for name in REQUIRED_REPORTS}
    findings: list[GateFinding] = []

    add_required_report_findings(report_paths, findings)
    dataset_rows = add_status_findings(report_paths, findings)
    add_blocks_parity_findings(report_paths, findings)
    add_blocked_runtime_findings(args.data_root, args.dist_root, findings)
    source_gaps = add_source_gap_findings(report_root, findings)
    aggregate_checks = add_aggregate_findings(report_paths, findings)
    limitations = add_accepted_limitations_findings(report_paths, findings)

    errors = [finding for finding in findings if finding.severity == "error"]
    source_gap_datasets = sorted({str(row.get("dataset", "")) for row in source_gaps if row.get("dataset")})
    has_source_gaps = bool(source_gaps)
    release_status = "not_publishable_source_gaps" if not errors and has_source_gaps else "publishable"
    if errors:
        release_status = "blocked"

    validation = {
        "format": "runec-report-validation-v1",
        "generated_utc": utc_now(),
        "data_version": args.version,
        "status": "passed" if not errors else "failed",
        "release_status": release_status,
        "required_reports": {name: rel(path) for name, path in sorted(report_paths.items())},
        "report_statuses": report_statuses(report_paths),
        "dataset_statuses": dataset_rows,
        "aggregate_checks": aggregate_checks,
        "source_gaps": source_gaps,
        "accepted_limitations": limitations,
        "runtime_manifests": {
            "data": manifest_summary(args.data_root / "manifest.json"),
            "dist": manifest_summary(args.dist_root / "manifest.json"),
        },
        "findings": [asdict(finding) for finding in findings],
    }

    summary = {
        "format": "runec-data-release-summary-v1",
        "generated_utc": validation["generated_utc"],
        "data_version": args.version,
        "validation_status": validation["status"],
        "release_status": release_status,
        "publishable": release_status == "publishable",
        "reason": (
            "Release is blocked by report validation errors."
            if errors
            else "Release is not publishable until source-authority gaps are resolved."
            if has_source_gaps
            else "Release gate passed."
        ),
        "source_gap_datasets": source_gap_datasets,
        "accepted_limitations": limitations,
        "runtime_manifests": validation["runtime_manifests"],
        "aggregate_checks": aggregate_checks,
        "findings": [asdict(finding) for finding in findings],
    }
    return validation, summary, findings


def write_summary_text(path: Path, summary: dict[str, Any]) -> None:
    lines = [
        "RuneC data v1 release summary",
        "",
        f"validation_status: {summary['validation_status']}",
        f"release_status: {summary['release_status']}",
        f"publishable: {str(summary['publishable']).lower()}",
        f"reason: {summary['reason']}",
        "",
        "Runtime manifests",
    ]
    for name, manifest in summary["runtime_manifests"].items():
        lines.append(
            f"- {name}: {manifest.get('path')} packs={manifest.get('packs', 0)} "
            f"assets={manifest.get('assets', 0)} subsets={manifest.get('subsets', 0)}"
        )
    lines.extend(["", "Source gap datasets"])
    for dataset in summary["source_gap_datasets"]:
        lines.append(f"- {dataset}")
    lines.extend(["", "Accepted limitations"])
    for row in summary["accepted_limitations"]:
        lines.append(f"- {row}")
    lines.extend(["", "Findings"])
    if summary["findings"]:
        for row in summary["findings"]:
            lines.append(f"- {row['severity']} {row['gate']}: {row['message']}")
    else:
        lines.append("- none")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report-root", type=Path, default=ROOT / "generated/reports")
    parser.add_argument("--data-root", type=Path, default=ROOT / "data")
    parser.add_argument("--dist-root", type=Path, default=ROOT / "dist-data")
    parser.add_argument("--version", default="v1")
    parser.add_argument("--check-only", action="store_true", help="validate without writing summary outputs")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    validation, summary, findings = validation_payload(args)
    if not args.check_only:
        write_json(args.report_root / "report-validation.json", validation)
        write_summary_text(args.report_root / f"data-{args.version}-summary.txt", summary)
        write_json(args.report_root / f"data-{args.version}-summary.json", summary)

    errors = [finding for finding in findings if finding.severity == "error"]
    if errors:
        for finding in errors:
            path = f" ({finding.path})" if finding.path else ""
            print(f"report validation error [{finding.gate}]: {finding.message}{path}", file=sys.stderr)
        return 1
    print(f"report validation passed: release_status={summary['release_status']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
