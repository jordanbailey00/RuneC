#!/usr/bin/env python3
"""Validate Phase 2 source locking."""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import sys
import tomllib
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / "data-sources/sources.lock"

SCAN_ROOTS = (
    ".gitignore",
    "AGENT.md",
    "README.md",
    "CMakeLists.txt",
    "content",
    "data/README.md",
    "data-sources",
    "rc-core",
    "rc-content",
    "rc-viewer",
    "schema",
    "scripts",
    "tests",
    "tools",
)

SKIP_DIRS = {
    ".git",
    "build",
    "build_cov",
    "data/defs",
    "data/models",
    "data/regions",
    "data/spawns",
    "dist-data",
    "generated",
    "tools/reports",
    "tools/wiki_cache",
    "tools/cache_pipeline/source",
}

POLICY_FILES = {
    ".gitignore",
    "data_cleanup.md",
    "data_pipeline.md",
    "git_setup.md",
    "docs/changelog.md",
    "tools/legacy_external_source_paths.py",
    "tools/validate_no_external_repos.py",
    "tools/validate_pipeline_inputs.py",
    "tools/validate_reports.py",
    "tools/validate_schemas.py",
    "tools/validate_source_authority.py",
    "tools/validate_sources.py",
    "data-sources/README.md",
    "data-sources/sources.lock",
}

LEGACY_EXTERNAL_SOURCE_FILES = {
    "tools/acquisition_common.py",
    "tools/audit_npc_reconciliation.py",
    "tools/audit_npc_semantics.py",
    "tools/database_sources.py",
    "tools/export_drops.py",
    "tools/export_items.py",
    "tools/export_music.py",
    "tools/export_npc_defs_full.py",
    "tools/export_npc_models_full.py",
    "tools/export_object_behaviors.py",
    "tools/export_object_defs.py",
    "tools/export_object_transports.py",
    "tools/export_quests.py",
    "tools/export_regular_npc_mechanics.py",
    "tools/export_spawn_sources.py",
    "tools/export_spawns.py",
    "tools/export_spells.py",
    "tools/export_traversal_edges.py",
    "tools/export_varbits.py",
    "tools/extract_dialogue.py",
    "tools/extract_mechanics.py",
    "tools/extract_quest_steps.py",
    "tools/patch_npc_defs_wiki.py",
    "tools/scrape_all.py",
    "tools/scrape_bosses.py",
    "tools/scrape_item_specials.py",
    "tools/scrape_strategies.py",
    "tools/scrape_transcripts.py",
    "tools/wiki_pages.py",
    "tools/xvalidate.py",
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

SOURCE_PATH_RE = re.compile(
    r"\b(?:data/source|tools/cache_pipeline/source|tools/wiki_cache)"
    r"[\w./#-]*"
)

ALLOWED_STATUSES = {"required", "optional", "blocked", "deprecated", "unused"}


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tree_sha256(path: Path) -> tuple[str, int]:
    h = hashlib.sha256()
    count = 0
    for child in sorted(path.rglob("*")):
        if not child.is_file():
            continue
        child_rel = rel(child)
        h.update(child_rel.encode())
        h.update(b"\0")
        h.update(file_sha256(child).encode())
        h.update(b"\n")
        count += 1
    return h.hexdigest(), count


def is_inside_repo(path: Path) -> bool:
    try:
        path.resolve().relative_to(ROOT)
        return True
    except ValueError:
        return False


def skip_path(path: Path) -> bool:
    r = rel(path)
    if r in POLICY_FILES:
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


def validate_required_source(row: dict[str, Any], failures: list[str]) -> None:
    path = ROOT / str(row.get("path", ""))
    kind = str(row.get("kind", ""))
    if kind == "tracked_tree":
        if not path.is_dir():
            failures.append(f"{row['id']}: missing required tree {rel(path)}")
            return
        actual_hash, actual_count = tree_sha256(path)
        if actual_hash != row.get("sha256"):
            failures.append(f"{row['id']}: tree checksum mismatch")
        if actual_count != int(row.get("file_count", -1)):
            failures.append(
                f"{row['id']}: file count mismatch "
                f"(expected {row.get('file_count')}, got {actual_count})"
            )
        return
    if kind == "tracked_file":
        if not path.is_file():
            failures.append(f"{row['id']}: missing required file {rel(path)}")
            return
        if file_sha256(path) != row.get("sha256"):
            failures.append(f"{row['id']}: file checksum mismatch")
        return
    failures.append(f"{row['id']}: unsupported required source kind {kind}")


def validate_optional_source(row: dict[str, Any], notes: list[str],
                             failures: list[str]) -> None:
    env = row.get("env")
    if not env:
        notes.append(f"{row['id']}: optional source has no env binding")
        return
    raw = os.environ.get(str(env))
    if not raw:
        notes.append(f"{row['id']}: optional source missing ({env} unset)")
        return
    path = Path(raw)
    if not path.exists():
        failures.append(f"{row['id']}: {env} path does not exist: {path}")
    if row.get("must_be_outside_repo") and is_inside_repo(path):
        failures.append(f"{row['id']}: {env} must point outside the repo: {path}")


def validate_sources(doc: dict[str, Any]) -> tuple[list[str], list[str]]:
    failures: list[str] = []
    notes: list[str] = []
    ids: set[str] = set()
    for row in doc.get("source", []):
        source_id = str(row.get("id", ""))
        if not source_id:
            failures.append("source row missing id")
            continue
        if source_id in ids:
            failures.append(f"duplicate source id: {source_id}")
        ids.add(source_id)
        status = str(row.get("status", ""))
        if status not in ALLOWED_STATUSES:
            failures.append(f"{source_id}: invalid status {status}")
            continue
        if status == "required":
            validate_required_source(row, failures)
        elif status == "optional":
            validate_optional_source(row, notes, failures)
    return failures, notes


def validate_source_gap_mirror(failures: list[str]) -> None:
    tracked = ROOT / "data-sources/source_gaps.json"
    generated = ROOT / "generated/reports/source_gaps.json"
    if tracked.is_file() and generated.is_file() and tracked.read_bytes() != generated.read_bytes():
        failures.append("data-sources/source_gaps.json differs from generated/reports/source_gaps.json")


def validate_blocked_sources(doc: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    blocked_patterns: list[tuple[str, str]] = []
    for row in doc.get("blocked_source", []):
        source_id = str(row.get("id", "blocked_source"))
        for raw_path in row.get("paths", []) or []:
            path = ROOT / str(raw_path)
            if path.exists():
                failures.append(f"{source_id}: blocked path exists: {raw_path}")
        for pattern in row.get("patterns", []) or []:
            blocked_patterns.append((source_id, str(pattern)))

    for path in candidate_files():
        path_rel = rel(path)
        if path_rel in LEGACY_EXTERNAL_SOURCE_FILES:
            continue
        try:
            text = path.read_text(errors="replace")
        except OSError:
            continue
        for source_id, pattern in blocked_patterns:
            if pattern and pattern in text:
                failures.append(f"{path_rel}: blocked source reference {source_id}: {pattern}")
        for match in SOURCE_PATH_RE.findall(text):
            if path_rel == "tools/pack_runtime_data.py" and match == "data/source/":
                continue
            if not any(match.startswith(pat) for _sid, pat in blocked_patterns):
                failures.append(f"{path_rel}: unlocked source path reference: {match}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lock", type=Path, default=LOCK)
    args = parser.parse_args()

    if not args.lock.is_file():
        print(f"source lock missing: {args.lock}", file=sys.stderr)
        return 1
    doc = tomllib.loads(args.lock.read_text())
    failures, notes = validate_sources(doc)
    failures.extend(validate_blocked_sources(doc))
    validate_source_gap_mirror(failures)

    for note in notes:
        print(f"optional: {note}")
    if failures:
        print("source validation failed:", file=sys.stderr)
        for failure in failures[:200]:
            print(f"  {failure}", file=sys.stderr)
        if len(failures) > 200:
            print(f"  ... {len(failures) - 200} more", file=sys.stderr)
        return 1
    print("source lock validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
