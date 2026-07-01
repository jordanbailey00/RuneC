#!/usr/bin/env python3
"""Fail active source files that still reference blocked data authorities."""
from __future__ import annotations

import argparse
import json
import re
from dataclasses import asdict, dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GENERATED_REPORTS = ROOT / "generated/reports"

SCAN_ROOTS = (
    "README.md",
    "AGENT.md",
    "CMakeLists.txt",
    "content",
    "rc-core",
    "rc-content",
    "rc-viewer",
    "scripts",
    "tests",
    "tools",
)

SKIP_DIRS = {
    ".git",
    "build",
    "build_cov",
    "data",
    "dist-data",
    "generated",
    "tools/reports",
    "tools/wiki_cache",
    "tools/cache_pipeline/source",
}

SKIP_FILES = {
    "architecture_roadmap.md",
    "data_cleanup.md",
    "docs/changelog.md",
    "tools/validate_no_external_repos.py",
    "tools/validate_pipeline_inputs.py",
    "tools/validate_reports.py",
    "tools/validate_source_authority.py",
}

TEXT_SUFFIXES = {
    "",
    ".c",
    ".cc",
    ".cmake",
    ".h",
    ".md",
    ".py",
    ".sh",
    ".toml",
    ".tsv",
    ".txt",
}

BLOCKED_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    ("void_rsps", re.compile(r"\bvoid_rsps\b")),
    ("VoidPS", re.compile(r"\bVoidPS\b")),
    ("voidps provenance", re.compile(r"\bvoidps:")),
    ("2011Scape", re.compile(r"\b2011Scape\b")),
    ("SCAPE_2011", re.compile(r"\bSCAPE_2011\b")),
    ("near_reality", re.compile(r"\bnear_reality\b")),
    ("Near-Reality", re.compile(r"\bNear-Reality\b")),
    ("MapLocations.java", re.compile(r"\bMapLocations\.java\b")),
    (
        "Zenyte source marker",
        re.compile(
            r"(?i)(\bzenyte\b.*\b(maplocations|geometry|authority|source)\b)"
            r"|(\b(maplocations|geometry|authority|source)\b.*\bzenyte\b)"
        ),
    ),
)


@dataclass(frozen=True)
class SourceGap:
    dataset: str
    missing_fact: str
    current_consumer: str
    approved_sources_checked: list[str]
    proposed_follow_up: str


SOURCE_GAPS: tuple[SourceGap, ...] = ()


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def skip_path(path: Path) -> bool:
    r = rel(path)
    if r in SKIP_FILES:
        return True
    return any(r == d or r.startswith(f"{d}/") for d in SKIP_DIRS)


def candidate_files() -> list[Path]:
    out: list[Path] = []
    for raw in SCAN_ROOTS:
        path = ROOT / raw
        if not path.exists():
            continue
        if path.is_file():
            if not skip_path(path) and path.suffix in TEXT_SUFFIXES:
                out.append(path)
            continue
        for child in path.rglob("*"):
            if child.is_file() and not skip_path(child) and child.suffix in TEXT_SUFFIXES:
                out.append(child)
    return sorted(set(out))


def scan() -> list[tuple[str, int, str, str]]:
    findings: list[tuple[str, int, str, str]] = []
    for path in candidate_files():
        try:
            lines = path.read_text(errors="replace").splitlines()
        except OSError:
            continue
        for lineno, line in enumerate(lines, 1):
            for label, pattern in BLOCKED_PATTERNS:
                if pattern.search(line):
                    findings.append((rel(path), lineno, label, line.strip()))
    return findings


def write_gap_reports() -> None:
    GENERATED_REPORTS.mkdir(parents=True, exist_ok=True)
    data = [asdict(row) for row in SOURCE_GAPS]
    (GENERATED_REPORTS / "source_gaps.json").write_text(
        json.dumps(data, indent=2) + "\n"
    )
    lines = ["RuneC source authority gaps", ""]
    for row in SOURCE_GAPS:
        lines.extend(
            [
                f"dataset: {row.dataset}",
                f"missing_fact: {row.missing_fact}",
                f"current_consumer: {row.current_consumer}",
                "approved_sources_checked: "
                + ", ".join(row.approved_sources_checked),
                f"proposed_follow_up: {row.proposed_follow_up}",
                "",
            ]
        )
    (GENERATED_REPORTS / "source_gaps.txt").write_text("\n".join(lines))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--write-gap-report",
        action="store_true",
        help="write generated/reports/source_gaps.{txt,json}",
    )
    args = parser.parse_args()

    if args.write_gap_report:
        write_gap_reports()

    findings = scan()
    if findings:
        print("blocked source references found in active files:")
        for path, lineno, label, line in findings:
            print(f"{path}:{lineno}: {label}: {line}")
        return 1
    print("source authority guard passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
