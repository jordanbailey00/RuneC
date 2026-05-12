#!/usr/bin/env python3
"""Emit object transport edges from data_osrs transport rows."""
from __future__ import annotations

import json
import struct
from collections import Counter
from pathlib import Path

from source_paths import DATA_OSRS

ROOT = Path(__file__).resolve().parents[1]
SRC = DATA_OSRS / "transports_osrs.json"
OUT = ROOT / "data/defs/object_transports.bin"
REPORT = ROOT / "tools/reports/object_transports.txt"

OTRP_MAGIC = 0x5052544F  # OTRP
OTRP_VERSION = 1


def option_index(menu_action: str) -> int:
    names = [
        "GAME_OBJECT_FIRST_OPTION",
        "GAME_OBJECT_SECOND_OPTION",
        "GAME_OBJECT_THIRD_OPTION",
        "GAME_OBJECT_FOURTH_OPTION",
        "GAME_OBJECT_FIFTH_OPTION",
    ]
    return names.index(menu_action) if menu_action in names else 0xFF


def is_non_transport_bank_row(row: dict) -> bool:
    action = str(row.get("menuOption", "")).strip().lower()
    target = str(row.get("menuTarget", "")).strip().lower()
    if action in {"bank", "collect", "deposit", "deposit-box"}:
        return True
    return "bank" in target and action in {"use", "bank", "collect", "deposit"}


def read_rows() -> list[dict]:
    rows: list[dict] = []
    if not SRC.is_file():
        return rows
    for row in json.loads(SRC.read_text()):
        if row.get("Category") != "GAME_OBJECT":
            continue
        if is_non_transport_bank_row(row):
            continue
        try:
            obj_id = int(row["id"])
            start = row["start"]
            sx, sy, sp = int(start["x"]), int(start["y"]), int(start["p"])
        except (KeyError, TypeError, ValueError):
            continue
        opt = option_index(str(row.get("menuAction", "")))
        action = str(row.get("menuOption", ""))[:64]
        target = str(row.get("menuTarget", ""))[:64]
        flags = (1 if row.get("fromInstance") else 0) \
              | (2 if row.get("toInstance") else 0)
        for dest in row.get("destinations") or []:
            try:
                rows.append({
                    "obj_id": obj_id,
                    "sx": sx,
                    "sy": sy,
                    "sp": sp,
                    "dx": int(dest["x"]),
                    "dy": int(dest["y"]),
                    "dp": int(dest["p"]),
                    "option": opt,
                    "flags": flags,
                    "action": action,
                    "target": target,
                })
            except (KeyError, TypeError, ValueError):
                continue
    return sorted(rows, key=lambda r: (
        r["obj_id"], r["sx"], r["sy"], r["sp"], r["option"],
        r["dx"], r["dy"], r["dp"],
    ))


def write_pstr(f, value: str) -> None:
    raw = value.encode("latin-1", errors="replace")[:255]
    f.write(struct.pack("<B", len(raw)))
    f.write(raw)


def main() -> int:
    rows = read_rows()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", OTRP_MAGIC, OTRP_VERSION, len(rows)))
        for row in rows:
            f.write(struct.pack(
                "<IHHHHHBBBB",
                row["obj_id"],
                row["sx"], row["sy"], row["dx"], row["dy"],
                ((row["sp"] & 0x3) << 14) | ((row["dp"] & 0x3) << 12),
                row["option"] & 0xFF,
                row["flags"] & 0xFF,
                0,
                0,
            ))
            write_pstr(f, row["action"])
            write_pstr(f, row["target"])

    by_action = Counter(r["action"] for r in rows)
    by_id = Counter(r["obj_id"] for r in rows)
    lines = [
        "Object transport edges",
        "",
        "status: READY_WITH_ACCEPTED_SIMPLIFICATIONS",
        f"output binary: data/defs/object_transports.bin ({OUT.stat().st_size} bytes)",
        f"source: {SRC.relative_to(ROOT)}",
        f"transport edges: {len(rows)}",
        f"unique object ids: {len(by_id)}",
        "",
        "top actions:",
    ]
    for action, count in by_action.most_common(30):
        lines.append(f"  {action:<24} {count}")
    lines.extend(["", "top object ids:"])
    for obj_id, count in by_id.most_common(30):
        lines.append(f"  {obj_id:<6} {count}")
    lines.extend([
        "",
        "accepted simplifications:",
        "  - edges are source coordinates plus destination choices; route selection is a later action-system concern",
        "  - instance-local dynamic transports remain activity-schema work",
    ])
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text("\n".join(lines) + "\n")
    print(REPORT.read_text())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
