"""Central paths for raw source corpora used by database exporters.

RuneC's executable pipeline lives in this repo. Raw source corpora are local to
this checkout so exporters never depend on another repository checkout.
"""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = ROOT / "data/source"
CACHE_PIPELINE_SOURCE_ROOT = ROOT / "tools/cache_pipeline/source"

DATA_OSRS = SOURCE_ROOT / "data_osrs"
OSRSREBOXED = SOURCE_ROOT / "osrsreboxed-db"
RUNELITE = SOURCE_ROOT / "runelite"
SCAPE_2011 = SOURCE_ROOT / "2011Scape-game"
CACHE_DIR = CACHE_PIPELINE_SOURCE_ROOT / "current_fightcaves_demo/data/cache"
CACHE_KEYS = CACHE_PIPELINE_SOURCE_ROOT / "current_fightcaves_demo/data/keys.json"
OSRS_DUMPS = CACHE_PIPELINE_SOURCE_ROOT / "osrs-dumps"
MODEL_DUMP = OSRS_DUMPS
NEAR_REALITY_MAP_LOCATIONS = SOURCE_ROOT / "near_reality/MapLocations.java"
