#!/usr/bin/env python3
"""Validate RuneC static NPC spawn exports against their source rows."""
from __future__ import annotations

import argparse
import json
import struct
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

from export_spawns import (
    DEFAULT_DIRECTION,
    DEFAULT_WANDER,
    NPCLIST,
    NSPN_FLAG_INSTANCE,
    NSPN_MAGIC,
    VARROCK_BOUNDS,
    instance_only_names,
    load_locline_rows,
)

ROOT = Path(__file__).resolve().parents[1]
NDEF_MAGIC = 0x4E444546


@dataclass(frozen=True)
class SpawnRow:
    npc_id: int
    x: int
    y: int
    plane: int
    direction: int
    wander_range: int
    flags: int

    @property
    def region(self) -> tuple[int, int]:
        return self.x >> 6, self.y >> 6


def read_exact(f, n: int, path: Path) -> bytes:
    data = f.read(n)
    if len(data) != n:
        raise EOFError(f"short read in {path}")
    return data


def read_nspn(path: Path) -> list[SpawnRow]:
    rows: list[SpawnRow] = []
    with path.open("rb") as f:
        magic, version, count = struct.unpack("<III", read_exact(f, 12, path))
        if magic != NSPN_MAGIC:
            raise ValueError(f"{path}: bad NSPN magic")
        if version not in (1, 2):
            raise ValueError(f"{path}: unsupported NSPN version {version}")
        for _ in range(count):
            npc_id = struct.unpack("<I", read_exact(f, 4, path))[0]
            x = struct.unpack("<i", read_exact(f, 4, path))[0]
            y = struct.unpack("<i", read_exact(f, 4, path))[0]
            plane = struct.unpack("<B", read_exact(f, 1, path))[0]
            direction = struct.unpack("<B", read_exact(f, 1, path))[0]
            wander_range = struct.unpack("<B", read_exact(f, 1, path))[0]
            flags = struct.unpack("<B", read_exact(f, 1, path))[0] if version >= 2 else 0
            rows.append(SpawnRow(npc_id, x, y, plane, direction, wander_range, flags))
    return rows


def read_ndef_ids(path: Path) -> set[int]:
    ids: set[int] = set()
    with path.open("rb") as f:
        magic, version, count = struct.unpack("<III", read_exact(f, 12, path))
        if magic != NDEF_MAGIC:
            raise ValueError(f"{path}: bad NDEF magic")
        if version not in (1, 2, 3, 4):
            raise ValueError(f"{path}: unsupported NDEF version {version}")
        for _ in range(count):
            npc_id = struct.unpack("<I", read_exact(f, 4, path))[0]
            read_exact(f, 1 + 2 + 2 + 12 + 20, path)
            name_len = struct.unpack("<B", read_exact(f, 1, path))[0]
            read_exact(f, name_len, path)
            if version >= 2:
                read_exact(f, 10, path)
            if version >= 3:
                model_count = struct.unpack("<B", read_exact(f, 1, path))[0]
                read_exact(f, model_count * 4, path)
            if version >= 4:
                for _ in range(5):
                    option_len = struct.unpack("<B", read_exact(f, 1, path))[0]
                    read_exact(f, option_len, path)
            ids.add(npc_id)
    return ids


def source_rows(path: Path) -> list[SpawnRow]:
    raw = json.loads(path.read_text())
    locline = load_locline_rows()
    instance_names = instance_only_names(locline) if locline else set()
    rows: list[SpawnRow] = []
    for row in raw:
        name = str(row.get("name", "")).strip().lower()
        flags = NSPN_FLAG_INSTANCE if name in instance_names else 0
        rows.append(SpawnRow(
            int(row["id"]),
            int(row["x"]),
            int(row["y"]),
            int(row.get("p", 0)),
            DEFAULT_DIRECTION,
            DEFAULT_WANDER,
            flags,
        ))
    return rows


def compare_rows(name: str, expected: list[SpawnRow],
                 actual: list[SpawnRow], lines: list[str]) -> bool:
    ok = True
    lines.append(f"{name}: expected={len(expected)} actual={len(actual)}")
    if expected != actual:
        ok = False
        exp_counts = Counter(expected)
        act_counts = Counter(actual)
        missing = list((exp_counts - act_counts).elements())[:10]
        extra = list((act_counts - exp_counts).elements())[:10]
        lines.append(f"{name}: mismatch")
        if missing:
            lines.append(f"  missing first {len(missing)}: {missing}")
        if extra:
            lines.append(f"  extra first {len(extra)}: {extra}")
    return ok


def plane_counts(rows: list[SpawnRow]) -> Counter:
    return Counter(row.plane for row in rows)


def top_regions(rows: list[SpawnRow], limit: int) -> list[tuple[tuple[int, int], int]]:
    return Counter(row.region for row in rows).most_common(limit)


def validate(args: argparse.Namespace) -> tuple[bool, str]:
    lines: list[str] = []
    expected = source_rows(args.npclist)
    world = read_nspn(args.world)
    varrock = read_nspn(args.varrock)
    defs = read_ndef_ids(args.npc_defs)

    ok = compare_rows("world NSPN", expected, world, lines)
    expected_varrock = [
        row for row in expected
        if VARROCK_BOUNDS["x0"] <= row.x <= VARROCK_BOUNDS["x1"]
        and VARROCK_BOUNDS["y0"] <= row.y <= VARROCK_BOUNDS["y1"]
    ]
    ok = compare_rows("varrock NSPN", expected_varrock, varrock, lines) and ok

    static_world = [row for row in world if (row.flags & NSPN_FLAG_INSTANCE) == 0]
    missing_defs = sorted({row.npc_id for row in static_world if row.npc_id not in defs})
    if missing_defs:
        ok = False
        lines.append(f"static spawn ids missing NDEF rows: {len(missing_defs)}")
        lines.append("  first ids: " + ", ".join(str(i) for i in missing_defs[:30]))
    else:
        lines.append("static spawn ids missing NDEF rows: 0")

    instance_rows = [row for row in world if row.flags & NSPN_FLAG_INSTANCE]
    loadable = [row for row in static_world if row.npc_id in defs]
    lines.append(f"world static loadable rows: {len(loadable)}")
    lines.append(f"world instance-only rows: {len(instance_rows)}")
    lines.append("world planes: " + ", ".join(
        f"{plane}={count}" for plane, count in sorted(plane_counts(world).items())))
    lines.append("varrock planes: " + ", ".join(
        f"{plane}={count}" for plane, count in sorted(plane_counts(varrock).items())))
    lines.append("top static spawn regions:")
    for (rx, ry), count in top_regions(loadable, args.top_regions):
        lines.append(f"  {rx},{ry}: {count}")
    lines.append("spawn completeness validation: " + ("ok" if ok else "FAILED"))
    return ok, "\n".join(lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--npclist", type=Path, default=NPCLIST)
    ap.add_argument("--world", type=Path,
                    default=ROOT / "data/spawns/world.npc-spawns.bin")
    ap.add_argument("--varrock", type=Path,
                    default=ROOT / "data/regions/varrock.npc-spawns.bin")
    ap.add_argument("--npc-defs", type=Path,
                    default=ROOT / "data/defs/npc_defs.bin")
    ap.add_argument("--report", type=Path,
                    default=ROOT / "tools/reports/spawn_completeness.txt")
    ap.add_argument("--top-regions", type=int, default=12)
    args = ap.parse_args()

    ok, report = validate(args)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(report)
    print(report, end="")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
