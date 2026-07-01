#!/usr/bin/env python3
"""Export combat visuals from RuneC-owned content."""
from __future__ import annotations

import argparse
import shutil
from pathlib import Path

from content_paths import content_read_path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INPUT = content_read_path("combat_visuals/visuals.tsv")
DEFAULT_OUTPUT = ROOT / "data/defs/combat_visuals.tsv"
DEFAULT_REPORT = ROOT / "tools/reports/combat_visuals.txt"

HEADER = (
    "kind|key|style|attack_anim|launch_spotanim|travel_spotanim|"
    "impact_spotanim|projectile_model|projectile_anim|hit_delay|"
    "client_delay|proj_start_height|proj_end_height|proj_delay|proj_angle|"
    "proj_length_adjustment|proj_progress|proj_step_multiplier|note|"
    "projectile_count|alt_proj_start_height|alt_proj_end_height|"
    "alt_proj_delay|alt_proj_angle|alt_proj_length_adjustment|"
    "alt_proj_progress|alt_proj_step_multiplier|aux_travel_spotanim|"
    "aux_impact_spotanim|aux_projectile_model|aux_projectile_anim|"
    "impact_on_last_only|double_launch_spotanim|stance_idx|attack_key|"
    "launch_gfx_height|impact_gfx_height|impact_gfx_delay|"
    "impact_gfx_rotation|primitive_type|source_attachment|"
    "target_attachment|launch_attachment|impact_attachment|authority"
)
HEADER_COLS = HEADER.split("|")
NOTE_COL = HEADER_COLS.index("note")
STANCE_COL = HEADER_COLS.index("stance_idx")
ATTACK_KEY_COL = HEADER_COLS.index("attack_key")
AUTHORITY_COL = HEADER_COLS.index("authority")
BLOCKED_MARKERS = (
    "void" "ps:",
    "void" "_rsps",
    "2011" "Scape",
    "SCAPE" "_2011",
    "near" "_reality",
    "Near" "-Reality",
    "Zen" "yte",
)


def validate_rows(path: Path) -> tuple[list[list[str]], list[str], list[str]]:
    failures: list[str] = []
    warnings: list[str] = []
    lines = path.read_text().splitlines()
    if not lines:
        return [], ["empty combat visual table"], []
    if lines[0] != HEADER:
        failures.append("header does not match combat visual schema")

    rows: list[list[str]] = []
    seen: set[tuple[str, str, str, str, str]] = set()
    for lineno, line in enumerate(lines[1:], 2):
        if not line.strip():
            continue
        for marker in BLOCKED_MARKERS:
            if marker in line:
                failures.append(f"line {lineno}: blocked marker {marker}")
        cols = line.split("|")
        if len(cols) != len(HEADER_COLS):
            failures.append(
                f"line {lineno}: expected {len(HEADER_COLS)} columns, got {len(cols)}"
            )
            continue
        if (
            cols[0] == "npc"
            and cols[AUTHORITY_COL] == "curated"
            and not cols[NOTE_COL].startswith("curated:b237:")
        ):
            failures.append(
                f"line {lineno}: curated NPC row must carry a curated:b237 note"
            )
        key = (
            cols[0],
            cols[1],
            cols[2],
            cols[3],
            cols[STANCE_COL],
            cols[ATTACK_KEY_COL],
        )
        if key in seen:
            warnings.append(f"line {lineno}: duplicate visual key {key}")
        seen.add(key)
        rows.append(cols)
    return rows, failures, warnings


def write_report(path: Path, source: Path, rows: list[list[str]],
                 failures: list[str], warnings: list[str]) -> None:
    by_kind: dict[str, int] = {}
    by_authority: dict[str, int] = {}
    npc_rows = [row for row in rows if row[0] == "npc"]
    reviewed_npc_rows = [
        row
        for row in npc_rows
        if row[AUTHORITY_COL] == "curated"
        and row[NOTE_COL].startswith("curated:b237:")
    ]
    for row in rows:
        by_kind[row[0]] = by_kind.get(row[0], 0) + 1
        authority = row[AUTHORITY_COL] if row[AUTHORITY_COL] else "-"
        by_authority[authority] = by_authority.get(authority, 0) + 1

    lines = [
        "Combat visuals content export",
        "",
        f"source: {source.relative_to(ROOT).as_posix()}",
        "authority: RuneC-owned content table",
        "source gap status: closed",
        "provenance note: curated NPC rows are reviewed RuneC-owned rows with curated:b237 notes; any unresolved projectile/profile fact must be tracked in data-sources/source_gaps.json.",
        f"rows: {len(rows)}",
        f"reviewed NPC rows: {len(reviewed_npc_rows)}/{len(npc_rows)}",
        "",
        "rows by kind:",
    ]
    lines.extend(f"  {key}: {value}" for key, value in sorted(by_kind.items()))
    lines += ["", "rows by authority:"]
    lines.extend(
        f"  {key}: {value}" for key, value in sorted(by_authority.items())
    )
    lines += ["", f"validation failures: {len(failures)}"]
    lines.extend(f"  {failure}" for failure in failures[:200])
    if len(failures) > 200:
        lines.append(f"  ... {len(failures) - 200} more")
    lines += ["", f"validation warnings: {len(warnings)}"]
    lines.extend(f"  {warning}" for warning in warnings[:200])
    if len(warnings) > 200:
        lines.append(f"  ... {len(warnings) - 200} more")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines).rstrip() + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    args = parser.parse_args()

    rows, failures, warnings = validate_rows(args.input)
    write_report(args.report, args.input, rows, failures, warnings)
    if failures:
        print(f"combat visuals validation failed: {len(failures)} failures")
        print(f"wrote {args.report}")
        return 1

    args.output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.input, args.output)
    print(f"wrote {args.output} from {args.input} ({len(rows)} rows)")
    print(f"wrote {args.report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
