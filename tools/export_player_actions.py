#!/usr/bin/env python3
"""Emit data/defs/player_actions.bin for core player input gates."""
from __future__ import annotations

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "data/defs/player_actions.bin"
REPORT = ROOT / "tools/reports/player_actions.txt"

MAGIC = 0x54434150
VERSION = 1

RC_SUB_COMBAT = 1 << 0
RC_SUB_PRAYER = 1 << 1
RC_SUB_EQUIPMENT = 1 << 2
RC_SUB_INVENTORY = 1 << 3
RC_SUB_CONSUMABLES = 1 << 4
RC_SUB_LOOT = 1 << 5
RC_SUB_OBJECTS = 1 << 12

ACTION_KIND_MOVEMENT = 1
ACTION_KIND_COMBAT = 2
ACTION_KIND_PRAYER = 3
ACTION_KIND_ITEM = 4
ACTION_KIND_NPC = 5
ACTION_KIND_OBJECT = 6
ACTION_KIND_LOOT = 7
ACTION_KIND_MAGIC = 8

ROWS = [
    (0, "walk_to", ACTION_KIND_MOVEMENT, 0),
    (1, "run_to", ACTION_KIND_MOVEMENT, 0),
    (2, "attack_npc", ACTION_KIND_COMBAT, RC_SUB_COMBAT),
    (3, "set_prayer", ACTION_KIND_PRAYER, RC_SUB_PRAYER),
    (4, "eat", ACTION_KIND_ITEM, RC_SUB_CONSUMABLES | RC_SUB_INVENTORY),
    (5, "drink", ACTION_KIND_ITEM, RC_SUB_CONSUMABLES | RC_SUB_INVENTORY),
    (6, "equip", ACTION_KIND_ITEM, RC_SUB_EQUIPMENT | RC_SUB_INVENTORY),
    (7, "unequip", ACTION_KIND_ITEM, RC_SUB_EQUIPMENT | RC_SUB_INVENTORY),
    (8, "interact_npc", ACTION_KIND_NPC, 0),
    (9, "interact_object", ACTION_KIND_OBJECT, RC_SUB_OBJECTS),
    (10, "drop_item", ACTION_KIND_ITEM, RC_SUB_INVENTORY),
    (11, "pickup_item", ACTION_KIND_LOOT, RC_SUB_LOOT | RC_SUB_INVENTORY),
    (12, "select_spell", ACTION_KIND_MAGIC, RC_SUB_COMBAT),
]


def main() -> None:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", MAGIC, VERSION, len(ROWS)))
        for action_id, name, kind, subsystems in ROWS:
            b = name.encode("ascii")
            f.write(struct.pack("<BBHI", action_id, kind, len(b), subsystems))
            f.write(b)

    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(
        "Player-action data export\n\n"
        "status: READY_WITH_ACCEPTED_SIMPLIFICATIONS\n"
        f"runtime: {OUT.relative_to(ROOT)}\n"
        f"count: {len(ROWS)}\n"
        "source: rc-core/api.h player input surface plus subsystem gates\n"
        "notes:\n"
        "  This is the action boundary, not exact per-content behavior.\n"
        "  Exact effects remain owned by combat, items, objects, skills,\n"
        "  shops/storage, dialogue, and activity systems.\n"
    )
    print(f"{len(ROWS)} actions -> {OUT}", file=sys.stderr)


if __name__ == "__main__":
    main()
