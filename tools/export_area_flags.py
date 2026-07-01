#!/usr/bin/env python3
"""Emit area-flag source-gap closure reports.

Area flags are currently supplied by a tracked RuneC-owned runtime snapshot for
the first release. This tool keeps the generated gap-report files present for
report consumers, but the gap list is now empty.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPORT_DIR = ROOT / "generated/reports"


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def write_reports(out_dir: Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "area_flags_gaps.json").write_text("[]\n")

    lines = [
        "RuneC area flag source gap closure",
        "",
        "status: READY_WITH_ACCEPTED_SIMPLIFICATIONS",
        "source: content/runtime_snapshots/manifest.json",
        "authority: runec_owned_reviewed_runtime_snapshot",
        "source_gap_rows: 0",
        "",
        "accepted simplification:",
        "  - first-release area semantics are supplied by the tracked runtime snapshot; semantic polygon-table normalization remains future data-deepening work",
    ]
    (out_dir / "area_flags_gaps.txt").write_text("\n".join(lines))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out-dir", type=Path, default=REPORT_DIR)
    args = parser.parse_args()

    write_reports(args.out_dir)
    print(f"wrote area flag source-gap closure reports to {rel(args.out_dir)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
