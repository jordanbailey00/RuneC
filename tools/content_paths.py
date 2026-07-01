"""Paths for tracked authored content with temporary legacy fallback."""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONTENT_ROOT = ROOT / "content"
LEGACY_CURATED_ROOT = ROOT / "data/curated"

_warned: set[Path] = set()


def content_read_path(*parts: str) -> Path:
    path = CONTENT_ROOT.joinpath(*parts)
    if path.exists():
        return path
    legacy = LEGACY_CURATED_ROOT.joinpath(*parts)
    if legacy.exists() and legacy not in _warned:
        print(
            f"warning: using legacy curated path {legacy.relative_to(ROOT)}; "
            f"move this dataset to {path.relative_to(ROOT)}",
            file=sys.stderr,
        )
        _warned.add(legacy)
    return legacy if legacy.exists() else path


def content_write_path(*parts: str) -> Path:
    return CONTENT_ROOT.joinpath(*parts)
