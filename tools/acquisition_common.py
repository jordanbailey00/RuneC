#!/usr/bin/env python3
"""Shared acquisition-source normalization for drop/acquisition exporters."""
from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Iterator

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / "tools/wiki_cache"


def load_bucket(bucket: str) -> Iterator[dict]:
    for path in sorted(CACHE.glob(f"{bucket}_*.json")):
        data = json.loads(path.read_text())
        for row in data.get("bucket", []):
            yield row


def base_name(name: str) -> str:
    return (name or "").split("#", 1)[0].strip()


def key_name(name: str) -> str:
    s = base_name(name).replace("\u2019", "'").replace("\u2018", "'")
    s = re.sub(r"\s+", " ", s).strip().lower()
    return s


def strip_trailing_paren(key: str) -> str:
    if "(" not in key:
        return key
    return re.sub(r"\s*\([^)]*\)\s*$", "", key).strip()


def map_names(row: dict, fields: tuple[str, ...], ids_field: str) -> dict[str, int]:
    out: dict[str, int] = {}
    ids = row.get(ids_field) or []
    if not isinstance(ids, list):
        ids = [ids]
    parsed: list[int] = []
    for raw in ids:
        try:
            parsed.append(int(raw))
        except (TypeError, ValueError):
            continue
    if not parsed:
        return out
    item_id = min(parsed)
    for field in fields:
        name = (row.get(field) or "").strip()
        if not name:
            continue
        out[key_name(name)] = item_id
    return out


def build_item_name_to_id() -> dict[str, int]:
    out: dict[str, int] = {}
    for row in load_bucket("infobox_item"):
        for key, item_id in map_names(row, ("item_name", "page_name"), "item_id").items():
            prior = out.get(key)
            if prior is None or item_id < prior:
                out[key] = item_id
    return out


def build_npc_name_to_id() -> dict[str, int]:
    out: dict[str, int] = {}
    for row in load_bucket("infobox_monster"):
        for key, npc_id in map_names(row, ("name", "page_name"), "id").items():
            prior = out.get(key)
            if prior is None or npc_id < prior:
                out[key] = npc_id
    return out


def resolve_name(name: str, table: dict[str, int]) -> int | None:
    key = key_name(name)
    found = table.get(key)
    if found is not None:
        return found
    return table.get(strip_trailing_paren(key))


def classify_non_npc_source(name: str) -> str:
    key = key_name(name)
    if any(w in key for w in (
        "tree", "rocks", "crystal", "plant", "soil", "herb", "ore", "vein",
        "bush", "vine", "stump", "logs", "sapling",
    )):
        return "resource_node"
    if any(w in key for w in (
        "chest", "casket", "crate", "box", "barrel", "lockbox", "coffer",
        "cupboard", "drawer", "sack", "pack", "pool",
    )):
        return "container_reward"
    if any(w in key for w in (
        "gamble", "gauntlet", "toa", "tombs of amascut", "colosseum",
        "barbarian assault", "wintertodt", "tempoross", "trawler",
        "forestry", "clue", "reward", "offering",
    )):
        return "activity_reward"
    if any(w in key for w in (
        "impling", "kebbit", "salamander", "lizard", "wagtail", "swift",
        "butterfly", "chinchompa",
    )):
        return "hunter_source"
    if any(w in key for w in (
        "cape", "accumulator", "attractor", "assembler", "repair",
        "rusty sword", "upgrade",
    )):
        return "upgrade_or_repair"
    if any(w in key for w in ("stall", "pickpocket", "merchant", "shop")):
        return "npc_or_service_reward"
    return "other_non_npc"
