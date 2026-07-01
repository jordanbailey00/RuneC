"""Legacy paths for research-only external source mirrors.

RUNEC_LEGACY_EXTERNAL_SOURCE_TOOL

These paths are intentionally not part of the official RuneC build/export
pipeline. Tools that import this module are deferred migration or research
helpers and must not be used to publish official runtime data.
"""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = ROOT / "data/source"
CACHE_PIPELINE_SOURCE_ROOT = ROOT / "tools/cache_pipeline/source"

DATA_OSRS = SOURCE_ROOT / "data_osrs"
OSRSREBOXED = SOURCE_ROOT / "osrsreboxed-db"
OSRS_DUMPS = CACHE_PIPELINE_SOURCE_ROOT / "osrs-dumps"
MODEL_DUMP = OSRS_DUMPS
