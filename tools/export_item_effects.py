#!/usr/bin/env python3
"""Validate RuneC-owned item effect content."""
from __future__ import annotations

import argparse
import json
import tomllib
from pathlib import Path
from typing import Any

from content_paths import content_read_path

DEFAULT_INPUT = content_read_path("items/effects.toml")
DEFAULT_REPORT = Path("tools/reports/item_effects.txt")


def as_int_list(value: Any) -> list[int] | None:
    if not isinstance(value, list):
        return None
    out: list[int] = []
    for item in value:
        if not isinstance(item, int):
            return None
        out.append(item)
    return out


def validate_effects(path: Path) -> tuple[list[dict[str, Any]], list[str]]:
    data = tomllib.loads(path.read_text())
    rows = data.get("effect")
    failures: list[str] = []
    if not isinstance(rows, list):
        return [], ["missing [[effect]] table"]

    for idx, row in enumerate(rows, 1):
        if not isinstance(row, dict):
            failures.append(f"effect {idx}: row is not a table")
            continue
        for key in ("source", "kind", "effect"):
            if not isinstance(row.get(key), str) or not row.get(key):
                failures.append(f"effect {idx}: missing string {key}")
        item_ids = as_int_list(row.get("item_ids"))
        if item_ids is None:
            failures.append(f"effect {idx}: item_ids must be a list of ints")
        elif not item_ids and not row.get("unresolved"):
            failures.append(f"effect {idx}: empty item_ids without unresolved marker")
        if row.get("path") is not None and not isinstance(row.get("path"), str):
            failures.append(f"effect {idx}: path must be a string when present")
        unresolved = row.get("unresolved")
        if unresolved is not None and not isinstance(unresolved, list):
            failures.append(f"effect {idx}: unresolved must be a list when present")
    return rows, failures


def write_report(path: Path, rows: list[dict[str, Any]], failures: list[str]) -> None:
    source_counts: dict[str, int] = {}
    kind_counts: dict[str, int] = {}
    unresolved = 0
    total_item_ids = 0
    for row in rows:
        source = str(row.get("source", ""))
        kind = str(row.get("kind", ""))
        source_counts[source] = source_counts.get(source, 0) + 1
        kind_counts[kind] = kind_counts.get(kind, 0) + 1
        total_item_ids += len(row.get("item_ids") or [])
        if row.get("unresolved"):
            unresolved += 1

    lines = [
        "Item effects content validation",
        "",
        "source: content/items/effects.toml",
        "authority: RuneC-owned reviewed content",
        f"effect rows: {len(rows)}",
        f"total item id references: {total_item_ids}",
        f"rows with unresolved markers: {unresolved}",
        "",
        "rows by source:",
    ]
    lines.extend(
        f"  {key}: {value}" for key, value in sorted(source_counts.items())
    )
    lines += ["", "rows by kind:"]
    lines.extend(f"  {key}: {value}" for key, value in sorted(kind_counts.items()))
    lines += ["", f"validation failures: {len(failures)}"]
    lines.extend(f"  {failure}" for failure in failures[:200])
    if len(failures) > 200:
        lines.append(f"  ... {len(failures) - 200} more")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines).rstrip() + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    parser.add_argument(
        "--json-summary",
        type=Path,
        help="optional path for a machine-readable validation summary",
    )
    args = parser.parse_args()

    rows, failures = validate_effects(args.input)
    write_report(args.report, rows, failures)
    if args.json_summary:
        args.json_summary.parent.mkdir(parents=True, exist_ok=True)
        args.json_summary.write_text(
            json.dumps(
                {
                    "source": args.input.as_posix(),
                    "rows": len(rows),
                    "failures": failures,
                },
                indent=2,
            )
            + "\n"
        )
    if failures:
        print(f"item effects validation failed: {len(failures)} failures")
        print(f"wrote {args.report}")
        return 1
    print(f"item effects content valid: {len(rows)} rows")
    print(f"wrote {args.report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
