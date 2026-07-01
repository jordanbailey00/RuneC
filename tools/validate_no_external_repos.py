#!/usr/bin/env python3
"""Validate RuneC does not depend on local external repo checkouts."""
from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SCAN_ROOTS = (
    "AGENT.md",
    "README.md",
    "CMakeLists.txt",
    "content",
    "docs",
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
    "docs/archive",
    "generated",
    "tools/cache_pipeline/source",
    "tools/reports",
    "tools/wiki_cache",
}

POLICY_FILES = {
    "data_cleanup.md",
    "data_pipeline.md",
    "git_setup.md",
    "docs/changelog.md",
    "docs/git_history.md",
    "tools/validate_no_external_repos.py",
    "tools/validate_pipeline_inputs.py",
    "tools/validate_source_authority.py",
    "tools/validate_sources.py",
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

BLOCKED_LOCAL_PATHS = (
    "data/source/void_rsps",
    "data/source/near_reality",
    "data/source/2011Scape-game",
    "data/source/runelite",
    "tools/cache_pipeline/source/rsmod",
)

DEFERRED_LOCAL_PATHS = (
    "data/source/data_osrs",
    "data/source/osrsreboxed-db",
    "tools/cache_pipeline/source/current_fightcaves_demo",
    "tools/cache_pipeline/source/osrsreboxed-db",
    "tools/cache_pipeline/source/osrs-dumps",
    "tools/wiki_cache",
)

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
    "tools/legacy_external_source_paths.py",
    "tools/patch_npc_defs_wiki.py",
    "tools/scrape_all.py",
    "tools/scrape_bosses.py",
    "tools/scrape_item_specials.py",
    "tools/scrape_strategies.py",
    "tools/scrape_transcripts.py",
    "tools/wiki_pages.py",
    "tools/xvalidate.py",
}


@dataclass(frozen=True)
class Pattern:
    label: str
    regex: re.Pattern[str]


HARD_PATTERNS = (
    Pattern("local RuneLite source path", re.compile(r"\bdata/source/runelite\b")),
    Pattern(
        "local RSMod source path",
        re.compile(r"\btools/cache_pipeline/source/rsmod\b"),
    ),
    Pattern("RSMod root environment dependency", re.compile(r"\bRUNEC_RSMOD_ROOT\b")),
    Pattern("RuneLite source_paths import", re.compile(r"from source_paths import .*RUNELITE")),
    Pattern("RuneC-DB setup dependency", re.compile(r"\bRuneC-DB\b")),
    Pattern(
        "external setup clone",
        re.compile(r"git clone https://github\.com/(runelite|rsmod|jordanbailey00/RuneC-DB)"),
    ),
)

DEFERRED_PATTERNS = (
    Pattern("data_osrs source_paths import", re.compile(r"from source_paths import .*DATA_OSRS")),
    Pattern(
        "osrsreboxed source_paths import",
        re.compile(r"from source_paths import .*OSRSREBOXED"),
    ),
    Pattern(
        "legacy external source import",
        re.compile(r"from legacy_external_source_paths import "),
    ),
    Pattern(
        "legacy database source import",
        re.compile(r"from database_sources import "),
    ),
    Pattern("local data_osrs path", re.compile(r"\bdata/source/data_osrs\b")),
    Pattern("local osrsreboxed path", re.compile(r"\bdata/source/osrsreboxed-db\b")),
    Pattern(
        "local b237 cache mirror path",
        re.compile(r"\btools/cache_pipeline/source/current_fightcaves_demo\b"),
    ),
    Pattern(
        "local osrsreboxed source path",
        re.compile(r"\btools/cache_pipeline/source/osrsreboxed-db\b"),
    ),
    Pattern("wiki cache path", re.compile(r"\btools/wiki_cache\b")),
    Pattern(
        "osrs-dumps snapshot path",
        re.compile(r"\btools/cache_pipeline/source/osrs-dumps\b"),
    ),
)


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


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


def nested_git_dirs() -> list[str]:
    out: list[str] = []
    for path in ROOT.rglob(".git"):
        if path == ROOT / ".git":
            continue
        if skip_path(path):
            continue
        out.append(rel(path))
    return sorted(out)


def present_blocked_paths() -> list[str]:
    return [p for p in BLOCKED_LOCAL_PATHS if (ROOT / p).exists()]


def present_deferred_paths() -> list[str]:
    return [p for p in DEFERRED_LOCAL_PATHS if (ROOT / p).exists()]


def scan(
    patterns: tuple[Pattern, ...],
    *,
    skip_legacy_external_sources: bool = False,
) -> list[tuple[str, int, str, str]]:
    findings: list[tuple[str, int, str, str]] = []
    for path in candidate_files():
        if skip_legacy_external_sources and rel(path) in LEGACY_EXTERNAL_SOURCE_FILES:
            continue
        try:
            lines = path.read_text(errors="replace").splitlines()
        except OSError:
            continue
        for lineno, line in enumerate(lines, 1):
            for pattern in patterns:
                if pattern.regex.search(line):
                    findings.append((rel(path), lineno, pattern.label, line.strip()))
    return findings


def print_findings(title: str, findings: list[tuple[str, int, str, str]]) -> None:
    print(title)
    for path, lineno, label, line in findings[:200]:
        print(f"  {path}:{lineno}: {label}: {line}")
    if len(findings) > 200:
        print(f"  ... {len(findings) - 200} more")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--strict",
        action="store_true",
        help="also fail on deferred data_osrs/osrsreboxed/wiki/osrs-dumps reads",
    )
    args = parser.parse_args()

    failures: list[str] = []
    for path in nested_git_dirs():
        failures.append(f"nested git directory: {path}")
    if (ROOT / ".gitmodules").exists():
        failures.append(".gitmodules exists")
    for path in present_blocked_paths():
        failures.append(f"blocked local source path exists: {path}")

    hard_findings = scan(HARD_PATTERNS)
    deferred_layout = present_deferred_paths()
    deferred_findings = scan(
        DEFERRED_PATTERNS,
        skip_legacy_external_sources=True,
    )

    if failures:
        print("external repo layout blockers:")
        for failure in failures:
            print(f"  {failure}")
    if hard_findings:
        print_findings("external repo hard references:", hard_findings)
    if deferred_layout:
        print(
            "deferred local source mirrors"
            + (" (strict failures):" if args.strict else " (non-fatal):")
        )
        for path in deferred_layout:
            print(f"  {path}")
    if deferred_findings:
        print_findings(
            "active external data-source references"
            + (" (strict failures):" if args.strict else " (non-fatal):"),
            deferred_findings,
        )

    if failures or hard_findings or (
        args.strict and (deferred_layout or deferred_findings)
    ):
        return 1
    print("external repo guard passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
