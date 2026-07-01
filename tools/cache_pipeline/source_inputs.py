"""Explicit maintainer source inputs for cache-pipeline tools."""
from __future__ import annotations

import os
from pathlib import Path


def env_path(name: str) -> Path | None:
    raw = os.environ.get(name)
    return Path(raw) if raw else None


def require_path(path: Path | None, flag: str, env: str) -> Path:
    if path is None:
        raise SystemExit(f"{flag} is required; pass {flag} or set {env}")
    return path


B237_CACHE = env_path("RUNEC_B237_CACHE")
B237_DUMP = env_path("RUNEC_B237_DUMP")
