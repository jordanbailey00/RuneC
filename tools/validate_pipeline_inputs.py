#!/usr/bin/env python3
"""Validate data-pipeline input policy for the current migration stage."""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--strict-external-sources",
        action="store_true",
        help="fail on deferred data_osrs/osrsreboxed/wiki/osrs-dumps reads",
    )
    args = parser.parse_args()

    cmd = [sys.executable, str(ROOT / "tools/validate_no_external_repos.py")]
    if args.strict_external_sources:
        cmd.append("--strict")
    result = subprocess.run(cmd, cwd=ROOT)
    if result.returncode != 0:
        return result.returncode

    result = subprocess.run(
        [sys.executable, str(ROOT / "tools/validate_sources.py")],
        cwd=ROOT,
    )
    if result.returncode != 0:
        return result.returncode

    result = subprocess.run(
        [sys.executable, str(ROOT / "tools/validate_runtime_data_lock.py")],
        cwd=ROOT,
    )
    if result.returncode != 0:
        return result.returncode

    result = subprocess.run(
        [sys.executable, str(ROOT / "tools/validate_schemas.py")],
        cwd=ROOT,
    )
    if result.returncode != 0:
        return result.returncode

    print("pipeline input guard passed")
    print("source lock, runtime-data lock, and schema guard passed; stage records are produced by tools/data_pipeline.py")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
