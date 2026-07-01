#!/usr/bin/env python3
"""Validate tracked authored content has replaced legacy data/curated files."""
from __future__ import annotations

import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONTENT = ROOT / "content"
LEGACY = ROOT / "data/curated"


def files_under(root: Path) -> dict[str, Path]:
    if not root.exists():
        return {}
    return {
        path.relative_to(root).as_posix(): path
        for path in root.rglob("*")
        if path.is_file()
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--strict-no-legacy",
        action="store_true",
        help="fail if any data/curated files remain",
    )
    args = parser.parse_args()

    content_files = files_under(CONTENT)
    legacy_files = files_under(LEGACY)

    if not content_files:
        print("content/ has no authored files")
        return 1

    failures: list[str] = []
    for rel, legacy in sorted(legacy_files.items()):
        current = content_files.get(rel)
        if current is None:
            failures.append(f"legacy-only file: data/curated/{rel}")
            continue
        if legacy.read_bytes() != current.read_bytes():
            failures.append(f"legacy diverges from content: data/curated/{rel}")

    if args.strict_no_legacy and legacy_files:
        failures.append(f"legacy data/curated files remain: {len(legacy_files)}")

    if failures:
        print("content layout guard failed:")
        for row in failures[:200]:
            print(f"  {row}")
        if len(failures) > 200:
            print(f"  ... {len(failures) - 200} more")
        return 1

    print(
        f"content layout guard passed: {len(content_files)} content files, "
        f"{len(legacy_files)} mirrored legacy files"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
