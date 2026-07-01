"""Central paths for RuneC-owned/generated inputs.

Do not add external repo checkout roots here. Legacy research tools that still
inspect off-repo/reference data must use ``legacy_external_source_paths.py`` so
the official pipeline boundary remains visible to validation.
"""
from __future__ import annotations

import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def _env_path(name: str) -> Path | None:
    raw = os.environ.get(name)
    return Path(raw) if raw else None


def require_cache_dir(path: Path | None) -> Path:
    if path is None:
        raise SystemExit(
            "b237 cache path required; pass --cache or set RUNEC_B237_CACHE"
        )
    return path


def display_path(path: Path | None) -> str:
    if path is None:
        return "explicit --cache/RUNEC_B237_CACHE"
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


CACHE_DIR = _env_path("RUNEC_B237_CACHE")
CACHE_KEYS = _env_path("RUNEC_B237_KEYS")
