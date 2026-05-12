#!/usr/bin/env python3
"""Emit unified traversal edges from data_osrs transports + teleports."""
from __future__ import annotations

import json
import struct
from collections import Counter
from pathlib import Path

from source_paths import DATA_OSRS

ROOT = Path(__file__).resolve().parents[1]
TRANSPORTS = DATA_OSRS / "transports_osrs.json"
TELEPORTS = DATA_OSRS / "teleports_osrs.json"
OUT = ROOT / "data/defs/traversal_edges.bin"
REPORT = ROOT / "tools/reports/traversal_edges.txt"

TRAV_MAGIC = 0x56415254  # TRAV
TRAV_VERSION = 1
NO_TILE = 0xFFFF

K_OBJECT = 1
K_ITEM = 2
K_SPELL = 3


def option_index(menu_action: str) -> int:
    for prefix in ("GAME_OBJECT", "ITEM"):
        names = [
            f"{prefix}_FIRST_OPTION",
            f"{prefix}_SECOND_OPTION",
            f"{prefix}_THIRD_OPTION",
            f"{prefix}_FOURTH_OPTION",
            f"{prefix}_FIFTH_OPTION",
        ]
        if menu_action in names:
            return names.index(menu_action)
    return 0xFF


def load_json(path: Path) -> list[dict]:
    return json.loads(path.read_text()) if path.is_file() else []


def is_non_transport_bank_row(row: dict) -> bool:
    action = str(row.get("menuOption", "")).strip().lower()
    target = str(row.get("menuTarget", "")).strip().lower()
    if action in {"bank", "collect", "deposit", "deposit-box"}:
        return True
    return "bank" in target and action in {"use", "bank", "collect", "deposit"}


def add_edge(rows: list[dict], kind: int, source_id: int, row: dict,
             dest: dict, start: dict | None) -> None:
    try:
        dx, dy, dp = int(dest["x"]), int(dest["y"]), int(dest["p"])
    except (KeyError, TypeError, ValueError):
        return
    sx = sy = NO_TILE
    sp = 0xFF
    if start:
        try:
            sx, sy, sp = int(start["x"]), int(start["y"]), int(start["p"])
        except (KeyError, TypeError, ValueError):
            return
    flags = (1 if row.get("fromInstance") else 0) \
          | (2 if row.get("toInstance") else 0)
    rows.append({
        "kind": kind,
        "source_id": source_id,
        "sx": sx,
        "sy": sy,
        "sp": sp,
        "dx": dx,
        "dy": dy,
        "dp": dp,
        "option": option_index(str(row.get("menuAction", ""))),
        "flags": flags,
        "action": str(row.get("menuOption", ""))[:64],
        "target": str(row.get("menuTarget", ""))[:64],
    })


def build_rows() -> list[dict]:
    rows: list[dict] = []
    for row in load_json(TRANSPORTS):
        if row.get("Category") != "GAME_OBJECT":
            continue
        if is_non_transport_bank_row(row):
            continue
        try:
            source_id = int(row["id"])
        except (KeyError, TypeError, ValueError):
            continue
        for dest in row.get("destinations") or []:
            add_edge(rows, K_OBJECT, source_id, row, dest, row.get("start"))

    for row in load_json(TELEPORTS):
        try:
            source_id = int(row["id"])
        except (KeyError, TypeError, ValueError):
            continue
        cat = row.get("Category")
        kind = K_ITEM if cat == "ITEM" else K_SPELL if cat == "TELEPORT_SPELL" else 0
        if not kind:
            continue
        for dest in row.get("destinations") or []:
            add_edge(rows, kind, source_id, row, dest, None)

    return sorted(rows, key=lambda r: (
        r["kind"], r["source_id"], r["sx"], r["sy"], r["sp"],
        r["option"], r["dx"], r["dy"], r["dp"], r["action"], r["target"],
    ))


def write_pstr(f, value: str) -> None:
    raw = value.encode("latin-1", errors="replace")[:255]
    f.write(struct.pack("<B", len(raw)))
    f.write(raw)


def main() -> int:
    rows = build_rows()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", TRAV_MAGIC, TRAV_VERSION, len(rows)))
        for row in rows:
            f.write(struct.pack(
                "<BBHIIHHHHBBBB",
                row["kind"],
                row["option"],
                ((row["sp"] & 0xFF) << 8) | (row["dp"] & 0xFF),
                row["source_id"],
                row["flags"],
                row["sx"],
                row["sy"],
                row["dx"],
                row["dy"],
                0,
                0,
                0,
                0,
            ))
            write_pstr(f, row["action"])
            write_pstr(f, row["target"])

    by_kind = Counter(r["kind"] for r in rows)
    by_action = Counter((r["kind"], r["action"]) for r in rows)
    kind_names = {K_OBJECT: "object", K_ITEM: "item", K_SPELL: "spell"}
    lines = [
        "Traversal edge index",
        "",
        "status: READY_WITH_ACCEPTED_SIMPLIFICATIONS",
        f"output binary: data/defs/traversal_edges.bin ({OUT.stat().st_size} bytes)",
        f"source object transports: {TRANSPORTS.relative_to(ROOT)}",
        f"source item/spell teleports: {TELEPORTS.relative_to(ROOT)}",
        f"edges: {len(rows)}",
        "",
        "by source kind:",
    ]
    for kind in (K_OBJECT, K_ITEM, K_SPELL):
        lines.append(f"  {kind_names[kind]:<8} {by_kind[kind]}")
    lines.extend(["", "top actions:"])
    for (kind, action), count in by_action.most_common(30):
        lines.append(f"  {kind_names.get(kind, str(kind)):<8} {action:<24} {count}")
    lines.extend([
        "",
        "accepted simplifications:",
        "  - each destination is a separate edge; action systems choose the valid destination",
        "  - item/spell teleports have no source tile and use 0xffff as the source-tile sentinel",
        "  - requirements, charges, rune costs, diaries, quest gates, and UI selection stay with their owning systems",
        "  - instance-local dynamic traversal remains activity-schema work",
    ])
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text("\n".join(lines) + "\n")
    print(REPORT.read_text())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
