#!/usr/bin/env python3
"""Compile typed activity state-machine rows into a runtime index."""

from __future__ import annotations

import struct
import time
import tomllib
from pathlib import Path

from content_paths import content_read_path

ROOT = Path(__file__).resolve().parents[1]
SRC = content_read_path("activity_state_machines.toml")
OUT = ROOT / "data/defs/activity_states.bin"
REPORT = ROOT / "tools/reports/activity_states.txt"

ASTA_MAGIC = 0x41545341
ASTA_VERSION = 1

KIND = {
    "wave_activity": 1,
    "multi_boss": 2,
    "dual_boss": 3,
    "skilling_boss": 4,
    "crypt_activity": 5,
    "single_boss": 6,
}

STATUS = {"typed_state_machine": 1}

ROLE = {
    "entry": 1,
    "combat": 2,
    "phase": 3,
    "boss": 4,
    "wave": 5,
    "resource": 6,
    "hazard": 7,
    "reward": 8,
    "fail": 9,
}

EVENT = {
    "tunnel_brother_selected": 1,
    "required_bosses_dead": 2,
    "wave_reached": 3,
    "boss_dead": 4,
    "hp_threshold": 5,
    "resource_zero": 6,
    "timer": 7,
}

FLAG_IDS: dict[str, int] = {}


def flag_idx(name: str) -> int:
    if name not in FLAG_IDS:
        if len(FLAG_IDS) >= 128:
            raise ValueError("too many activity-state flags")
        FLAG_IDS[name] = len(FLAG_IDS)
    return FLAG_IDS[name]


def flags(names: list[object]) -> tuple[int, int]:
    lo = 0
    hi = 0
    for name in names or []:
        idx = flag_idx(str(name))
        if idx < 64:
            lo |= 1 << idx
        else:
            hi |= 1 << (idx - 64)
    return lo, hi


def pstr(value: str) -> bytes:
    raw = value.encode("utf-8", "replace")
    if len(raw) > 255:
        raise ValueError(f"string too long: {value[:80]}")
    return struct.pack("<B", len(raw)) + raw


def read_rows() -> list[dict[str, object]]:
    doc = tomllib.loads(SRC.read_text(errors="replace"))
    rows: list[dict[str, object]] = []
    for entry in doc.get("activities", []) or []:
        slug = str(entry.get("slug") or "")
        kind = str(entry.get("kind") or "")
        status = str(entry.get("status") or "")
        if not slug or kind not in KIND or status not in STATUS:
            raise ValueError(f"bad activity state row: {entry}")
        params = []
        for key, value in sorted((entry.get("params", {}) or {}).items()):
            params.append((str(key), int(value)))
        row_flags = flags(list(entry.get("flags", []) or []))
        rows.append({
            "slug": slug,
            "name": str(entry.get("name") or slug),
            "kind": KIND[kind],
            "kind_name": kind,
            "status": STATUS[status],
            "flags_lo": row_flags[0],
            "flags_hi": row_flags[1],
            "source_rows": ";".join(str(x) for x in entry.get("source_rows", []) or []),
            "source_pages": ";".join(str(x) for x in entry.get("source_pages", []) or []),
            "npc_ids": [int(x) for x in entry.get("npc_ids", []) or []],
            "states": [],
            "transitions": [
                {
                    "from": str(t.get("from") or ""),
                    "to": str(t.get("to") or ""),
                    "event": EVENT[str(t.get("event") or "")],
                    "value": int(t.get("value", 0)),
                }
                for t in entry.get("transitions", []) or []
            ],
            "params": params,
        })
        for state in entry.get("states", []) or []:
            state_flags = flags(list(state.get("flags", []) or []))
            rows[-1]["states"].append({
                "id": str(state.get("id") or ""),
                "role": ROLE[str(state.get("role") or "")],
                "flags_lo": state_flags[0],
                "flags_hi": state_flags[1],
                "value": int(state.get("value", 0)),
            })
    return rows


def write_bin(rows: list[dict[str, object]]) -> None:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", ASTA_MAGIC, ASTA_VERSION, len(rows)))
        for row in rows:
            f.write(struct.pack(
                "<BBHHHHQQ",
                int(row["kind"]), int(row["status"]),
                len(row["npc_ids"]), len(row["states"]),
                len(row["transitions"]), len(row["params"]),
                int(row["flags_lo"]), int(row["flags_hi"])))
            f.write(pstr(str(row["slug"])))
            f.write(pstr(str(row["name"])))
            f.write(pstr(str(row["source_rows"])))
            f.write(pstr(str(row["source_pages"])))
            for npc_id in row["npc_ids"]:
                f.write(struct.pack("<I", int(npc_id)))
            for state in row["states"]:
                f.write(struct.pack("<BQQH", int(state["role"]),
                                    int(state["flags_lo"]),
                                    int(state["flags_hi"]),
                                    int(state["value"])))
                f.write(pstr(str(state["id"])))
            for transition in row["transitions"]:
                f.write(struct.pack("<BH", int(transition["event"]),
                                    int(transition["value"])))
                f.write(pstr(str(transition["from"])))
                f.write(pstr(str(transition["to"])))
            for key, value in row["params"]:
                f.write(pstr(key))
                f.write(struct.pack("<i", int(value)))


def write_report(rows: list[dict[str, object]]) -> None:
    by_kind: dict[str, int] = {}
    for row in rows:
        by_kind[str(row["kind_name"])] = by_kind.get(str(row["kind_name"]), 0) + 1
    v1_idx = FLAG_IDS.get("v1_required", -1)
    lines = [
        "Activity state-machine export",
        "",
        f"activity rows: {len(rows)}",
        f"v1-required rows: {sum(1 for r in rows if v1_idx >= 0 and ((int(r['flags_lo']) | (int(r['flags_hi']) << 64)) & (1 << v1_idx)))}",
        f"states: {sum(len(r['states']) for r in rows)}",
        f"transitions: {sum(len(r['transitions']) for r in rows)}",
        f"params: {sum(len(r['params']) for r in rows)}",
        f"output: {OUT.relative_to(ROOT)}",
        "",
        "Status",
        "READY_WITH_ACCEPTED_SIMPLIFICATIONS: rows load into RcActivityRun state flow; exact per-tick attack/object consumers land per activity.",
        "",
        "Rows",
    ]
    for row in rows:
        lines.append(
            f"- {row['slug']} kind={row['kind_name']} "
            f"npcs={len(row['npc_ids'])} states={len(row['states'])} "
            f"transitions={len(row['transitions'])}")
    lines.extend(["", "Kinds"])
    for kind, count in sorted(by_kind.items()):
        lines.append(f"- {kind}: {count}")
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text("\n".join(lines) + "\n")


def main() -> int:
    start = time.perf_counter()
    rows = read_rows()
    write_bin(rows)
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    write_report(rows)
    print(f"exported {len(rows)} activity state-machine rows "
          f"in {elapsed_ms:.3f} ms")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
