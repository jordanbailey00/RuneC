#!/usr/bin/env python3
"""Read current item config definitions from the repo-local rc_cache decoder."""
from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

PIPELINE = Path(__file__).resolve().parent / "cache_pipeline"
sys.path.insert(0, str(PIPELINE))

from rc_cache import (  # noqa: E402
    CONFIG_ITEM,
    INDEX_CONFIGS,
    ItemDef,
    RcCacheStore,
    decode_item_definition,
)


def _present(value: int) -> int | None:
    return None if value < 0 else value


def _actions(values: list[str | None]) -> list[str | None]:
    return [value or None for value in values]


def item_def_to_dict(defn: ItemDef) -> dict[str, Any]:
    return {
        "id": defn.item_id,
        "name": defn.name or "null",
        "members": defn.members,
        "tradeable": defn.tradeable,
        "stackable": defn.stackable,
        "cost": defn.cost,
        "weight": defn.weight,
        "ground_model_id": _present(defn.inventory_model),
        "male_model_ids": [_present(value) for value in defn.male_model_ids],
        "female_model_ids": [_present(value) for value in defn.female_model_ids],
        "wearpos1": _present(defn.wearpos1),
        "wearpos2": _present(defn.wearpos2),
        "wearpos3": _present(defn.wearpos3),
        "note_id": _present(defn.note_id),
        "note_template_id": _present(defn.note_template_id),
        "placeholder_id": _present(defn.placeholder_id),
        "placeholder_template_id": _present(defn.placeholder_template_id),
        "ground_ops": _actions(defn.ground_actions),
        "interface_ops": _actions(defn.inventory_actions),
    }


def decode_item_config(item_id: int, data: bytes) -> dict[str, Any]:
    defn = decode_item_definition(item_id, data)
    if not defn.complete:
        raise ValueError(f"item {item_id}: unsupported opcode {defn.unknown_opcode}")
    return item_def_to_dict(defn)


def load_cache_item_defs(cache_dir: Path) -> dict[int, dict[str, Any]]:
    if not cache_dir.is_dir():
        return {}
    store = RcCacheStore(cache_dir)
    files = store.read_group(INDEX_CONFIGS, CONFIG_ITEM)
    return {item_id: decode_item_config(item_id, data) for item_id, data in files.items()}
