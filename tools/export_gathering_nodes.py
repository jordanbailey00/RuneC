#!/usr/bin/env python3
"""Emit placed resource nodes from object behavior + placement data."""
from __future__ import annotations

import struct
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OBHV = ROOT / "data/defs/object_behaviors.bin"
OPLC = ROOT / "data/defs/object_placements.bin"
OUT = ROOT / "data/defs/gathering_nodes.bin"
REPORT = ROOT / "tools/reports/gathering_nodes.txt"

GNOD_MAGIC = 0x444F4E47  # GNOD
GNOD_VERSION = 1
RESOURCE_FLAG = 1 << 5

SKILL_NAMES = {
    1: "woodcutting",
    2: "mining",
    3: "fishing",
    4: "farming",
    5: "prayer",
}


def read_behaviors() -> dict[int, tuple[int, int, int]]:
    data = OBHV.read_bytes()
    magic, version, count = struct.unpack_from("<III", data, 0)
    if magic != 0x5648424F or version not in (1, 2):
        raise SystemExit("bad object_behaviors.bin")
    pos = 12
    out: dict[int, tuple[int, int, int]] = {}
    for _ in range(count):
        if version == 1:
            obj_id, flags, action_mask, skill, _pad = struct.unpack_from(
                "<IIBBH", data, pos)
            pos += 12
        else:
            obj_id, flags, _next_stage, _open_sound, _close_sound, \
                _climb_anim, action_mask, skill, _pad = struct.unpack_from(
                    "<IIiiiiBBH", data, pos)
            pos += struct.calcsize("<IIiiiiBBH")
        if flags & RESOURCE_FLAG:
            out[obj_id] = (action_mask, skill, flags)
    return out


def build_rows() -> list[tuple[int, int, int, int, int, int, int, int, int, int]]:
    resources = read_behaviors()
    data = OPLC.read_bytes()
    magic, version, count, _region_count = struct.unpack_from("<IIII", data, 0)
    if magic != 0x434C504F or version not in (1, 2):
        raise SystemExit("bad object_placements.bin")
    pos = 16
    rows = []
    for _ in range(count):
        obj_id = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        if version >= 2:
            pos += 8
        x, y, mapsquare = struct.unpack_from("<HHH", data, pos)
        plane, typ, rotation, place_flags = struct.unpack_from(
            "<BBBB", data, pos + 6)
        pos += 10
        behavior = resources.get(obj_id)
        if not behavior:
            continue
        action_mask, skill, behavior_flags = behavior
        rows.append((
            mapsquare, x, y, obj_id, plane, skill, action_mask,
            typ, rotation, place_flags | (behavior_flags & 0xFF),
        ))
    return sorted(rows)


def main() -> int:
    rows = build_rows()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", GNOD_MAGIC, GNOD_VERSION, len(rows)))
        for row in rows:
            mapsquare, x, y, obj_id, plane, skill, action_mask, typ, rotation, flags = row
            f.write(struct.pack(
                "<IHHHBBBBBBH",
                obj_id, x, y, mapsquare, plane, skill, action_mask,
                typ, rotation, flags, 0,
            ))

    by_skill = Counter(row[5] for row in rows)
    by_region = Counter(row[0] for row in rows)
    lines = [
        "Gathering node index",
        "",
        "status: READY_WITH_ACCEPTED_SIMPLIFICATIONS",
        f"output binary: data/defs/gathering_nodes.bin ({OUT.stat().st_size} bytes)",
        f"source object behaviors: {OBHV.relative_to(ROOT)}",
        f"source object placements: {OPLC.relative_to(ROOT)}",
        f"nodes: {len(rows)}",
        "",
        "by skill:",
    ]
    for skill, count in sorted(by_skill.items()):
        lines.append(f"  {SKILL_NAMES.get(skill, str(skill)):<12} {count}")
    lines.extend(["", "top regions:"])
    for mapsquare, count in by_region.most_common(20):
        rx, ry = mapsquare >> 8, mapsquare & 0xFF
        lines.append(f"  {mapsquare:5} ({rx:02d},{ry:02d}) {count}")
    lines.extend([
        "",
        "accepted simplifications:",
        "  - nodes are object-backed resources only; hunter targets, stalls, "
        "and activity-only nodes stay with activity schemas",
        "  - level/tool requirements come from recipes and future per-skill "
        "action rules",
        "  - runtime now owns generic depletion/respawn; exact success rolls "
        "remain per-skill action work",
    ])
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text("\n".join(lines) + "\n")
    print(REPORT.read_text())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
