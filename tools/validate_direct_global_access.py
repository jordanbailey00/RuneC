#!/usr/bin/env python3
"""Fail production C sources that directly consume migrated runtime globals."""
from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

SCAN_ROOTS = ("rc-core", "rc-content", "rc-viewer")
TEXT_SUFFIXES = {".c", ".h"}

COMPATIBILITY_FILES = {
    "rc-core/game_data.c",
}


@dataclass(frozen=True)
class ProtectedGlobal:
    symbol: str
    owner_files: frozenset[str]
    replacement: str


def owned(symbols: tuple[str, ...], owner: str, replacement: str) -> tuple[ProtectedGlobal, ...]:
    return tuple(
        ProtectedGlobal(symbol, frozenset({owner}), replacement)
        for symbol in symbols
    )


PROTECTED_GLOBALS: tuple[ProtectedGlobal, ...] = (
    *owned(
        ("g_item_defs", "g_item_def_count"),
        "rc-core/items.c",
        "rc_item_* accessors or RcGameData item-definition accessors",
    ),
    *owned(
        ("g_npc_defs", "g_npc_def_count"),
        "rc-core/npc.c",
        "rc_npc_* accessors or RcGameData NPC-definition accessors",
    ),
    *owned(
        (
            "g_rc_drop_tables",
            "g_rc_drop_entries",
            "g_rc_rdt_entries",
            "g_rc_gdt_entries",
            "g_rc_mrdt_entries",
            "g_rc_drop_table_count",
            "g_rc_drop_entry_count",
            "g_rc_rdt_entry_count",
            "g_rc_gdt_entry_count",
            "g_rc_mrdt_entry_count",
        ),
        "rc-core/drops.c",
        "rc_drops_* accessors or RcGameData drop-data accessors",
    ),
    *owned(
        ("g_rc_prayer_defs", "g_rc_prayer_count"),
        "rc-core/prayer.c",
        "rc_prayer_* accessors or RcGameData prayer accessors",
    ),
    *owned(
        ("g_rc_spell_defs", "g_rc_spell_count"),
        "rc-core/spells.c",
        "rc_spell_* accessors or RcGameData spell accessors",
    ),
    *owned(
        (
            "g_rc_item_normalization",
            "g_rc_npc_normalization",
            "g_rc_source_normalization",
            "g_rc_item_normalization_count",
            "g_rc_npc_normalization_count",
            "g_rc_source_normalization_count",
        ),
        "rc-core/normalization.c",
        "rc_normalization_* accessors or RcGameData normalization accessors",
    ),
    *owned(
        (
            "g_rc_object_defs",
            "g_rc_object_behaviors",
            "g_rc_object_placements",
            "g_rc_object_transports",
            "g_rc_object_params",
            "g_rc_object_def_count",
            "g_rc_object_behavior_count",
            "g_rc_object_placement_count",
            "g_rc_object_transport_count",
            "g_rc_object_param_count",
        ),
        "rc-core/objects.c",
        "rc_object_* accessors or RcGameData object-data accessors",
    ),
    *owned(
        ("g_rc_collision_regions", "g_rc_collision_region_count"),
        "rc-core/collision.c",
        "rc_collision_* accessors or RcGameData collision accessors",
    ),
    *owned(
        ("g_rc_area_flags", "g_rc_area_flag_count"),
        "rc-core/area_flags.c",
        "rc_area_flags_* accessors or RcGameData area-flag accessors",
    ),
    *owned(
        ("g_rc_traversal_edges", "g_rc_traversal_edge_count"),
        "rc-core/traversal.c",
        "rc_traversal_* accessors or RcGameData traversal accessors",
    ),
    *owned(
        ("g_rc_activity_schemas", "g_rc_activity_schema_count"),
        "rc-core/activity_schemas.c",
        "rc_activity_schema_* accessors or RcGameData activity-schema accessors",
    ),
    *owned(
        ("g_rc_activity_spawns", "g_rc_activity_spawn_count"),
        "rc-core/activity_spawns.c",
        "rc_activity_spawn_* accessors or RcGameData activity-spawn accessors",
    ),
    *owned(
        ("g_rc_activity_mechanics", "g_rc_activity_mechanic_count"),
        "rc-core/activity_mechanics.c",
        "rc_activity_mechanic_* accessors or RcGameData activity-mechanic accessors",
    ),
    *owned(
        ("g_rc_activity_states", "g_rc_activity_state_count"),
        "rc-core/activity_states.c",
        "rc_activity_state_* accessors or RcGameData activity-state accessors",
    ),
    *owned(
        (
            "g_rc_varbits",
            "g_rc_varps",
            "g_rc_varbit_count",
            "g_rc_varp_count",
        ),
        "rc-core/varbits.c",
        "rc_varbit_* accessors or RcGameData var-data accessors",
    ),
    *owned(
        (
            "g_rc_recipes",
            "g_rc_recipe_count",
            "g_rc_skill_drop_sources",
            "g_rc_skill_drops",
            "g_rc_skill_drop_source_count",
            "g_rc_skill_drop_count",
            "g_rc_gathering_nodes",
            "g_rc_gathering_node_count",
        ),
        "rc-core/skills.c",
        "rc_recipe/rc_skill/rc_gathering accessors or RcGameData skill-data accessors",
    ),
    *owned(
        ("g_shops", "g_shop_stock", "g_shop_count", "g_shop_stock_count"),
        "rc-core/shops.c",
        "rc_shop_* accessors or RcGameData shop-data accessors",
    ),
    *owned(
        ("g_rc_quest_defs", "g_rc_quest_count"),
        "rc-core/quests.c",
        "rc_quest_* accessors or RcGameData quest-data accessors",
    ),
    *owned(
        (
            "g_rc_dialogue_transcripts",
            "g_rc_dialogue_nodes",
            "g_rc_dialogue_child_ids",
            "g_rc_dialogue_npc_name_offsets",
            "g_rc_dialogue_transcript_count",
            "g_rc_dialogue_node_count",
            "g_rc_dialogue_child_id_count",
        ),
        "rc-core/dialogue.c",
        "rc_dialogue_* accessors or RcGameData dialogue-data accessors",
    ),
)

PROTECTED_BY_SYMBOL = {row.symbol: row for row in PROTECTED_GLOBALS}
SYMBOL_RE = re.compile(
    r"\b(" + "|".join(re.escape(row.symbol) for row in PROTECTED_GLOBALS) + r")\b"
)


@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    symbol: str
    replacement: str
    text: str


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def strip_comments_and_literals(text: str) -> str:
    out: list[str] = []
    state = "code"
    i = 0
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if ch == "/" and nxt == "/":
                out.extend((" ", " "))
                i += 2
                state = "line_comment"
                continue
            if ch == "/" and nxt == "*":
                out.extend((" ", " "))
                i += 2
                state = "block_comment"
                continue
            if ch == '"':
                out.append(" ")
                i += 1
                state = "string"
                continue
            if ch == "'":
                out.append(" ")
                i += 1
                state = "char"
                continue
            out.append(ch)
            i += 1
            continue

        if state == "line_comment":
            out.append("\n" if ch == "\n" else " ")
            if ch == "\n":
                state = "code"
            i += 1
            continue

        if state == "block_comment":
            if ch == "*" and nxt == "/":
                out.extend((" ", " "))
                i += 2
                state = "code"
                continue
            out.append("\n" if ch == "\n" else " ")
            i += 1
            continue

        if state in {"string", "char"}:
            if ch == "\\" and nxt:
                out.extend(("\n", " ") if nxt == "\n" else (" ", " "))
                i += 2
                continue
            if (state == "string" and ch == '"') or (state == "char" and ch == "'"):
                out.append(" ")
                state = "code"
                i += 1
                continue
            out.append("\n" if ch == "\n" else " ")
            i += 1
            continue

    return "".join(out)


def candidate_files() -> list[Path]:
    files: list[Path] = []
    for root_name in SCAN_ROOTS:
        root = ROOT / root_name
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.is_file() and path.suffix in TEXT_SUFFIXES:
                files.append(path)
    return sorted(files)


def header_extern_declaration(lines: list[str], index: int) -> bool:
    start = max(0, index - 2)
    end = min(len(lines), index + 3)
    context = " ".join(line.strip() for line in lines[start:end])
    return "extern" in context and ";" in context


def allowed_file(row: ProtectedGlobal, path: str) -> bool:
    return path in COMPATIBILITY_FILES or path in row.owner_files


def scan_file(path: Path) -> list[Finding]:
    try:
        original = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        original = path.read_text(errors="replace")
    code = strip_comments_and_literals(original)
    code_lines = code.splitlines()
    original_lines = original.splitlines()
    rel_path = rel(path)
    findings: list[Finding] = []
    for i, line in enumerate(code_lines):
        for match in SYMBOL_RE.finditer(line):
            symbol = match.group(1)
            row = PROTECTED_BY_SYMBOL[symbol]
            if allowed_file(row, rel_path):
                continue
            if path.suffix == ".h" and header_extern_declaration(code_lines, i):
                continue
            findings.append(
                Finding(
                    path=rel_path,
                    line=i + 1,
                    symbol=symbol,
                    replacement=row.replacement,
                    text=original_lines[i].strip() if i < len(original_lines) else "",
                )
            )
    return findings


def scan() -> list[Finding]:
    findings: list[Finding] = []
    for path in candidate_files():
        findings.extend(scan_file(path))
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()

    findings = scan()
    if findings:
        print("direct migrated runtime-global access found:")
        for finding in findings[:200]:
            print(
                f"{finding.path}:{finding.line}: {finding.symbol}: "
                f"use {finding.replacement}"
            )
            if finding.text:
                print(f"  {finding.text}")
        if len(findings) > 200:
            print(f"  ... {len(findings) - 200} more")
        print(
            "Owner modules and rc-core/game_data.c may touch transitional "
            "globals; production consumers should use accessors/views."
        )
        return 1

    print(
        "direct global access guard passed: "
        f"{len(PROTECTED_GLOBALS)} migrated globals, "
        f"{len(candidate_files())} production C/header files scanned"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
