#!/usr/bin/env python3
"""Build the high-level activity schema index."""

from __future__ import annotations

import struct
import time
import tomllib
from pathlib import Path

from export_activity_mechanics import read_mechanics

ROOT = Path(__file__).resolve().parents[1]
ENCOUNTERS = ROOT / "data/curated/encounters"
STATES = ROOT / "data/curated/activity_state_machines.toml"
SPAWNS = ROOT / "data/curated/activity_spawns.toml"
OUT = ROOT / "data/defs/activity_schemas.bin"
REPORT = ROOT / "tools/reports/activity_schemas.txt"

ASCH_MAGIC = 0x48435341
ASCH_VERSION = 2

READY = 1
READY_SIMPLIFIED = 2
BLOCKS_PARITY = 3

CLASS = {
    "unknown": 0,
    "encounter": 1,
    "arena_local": 2,
    "wave_regions": 3,
    "wave_table": 3,
    "wave_boss": 3,
    "object_activity": 4,
    "script_spawn_arena": 5,
}

KIND = {
    "wave_activity": 1,
    "multi_boss": 2,
    "dual_boss": 3,
    "skilling_boss": 4,
    "crypt_activity": 5,
    "single_boss": 6,
}

FLAG_ENCOUNTER = 1 << 0
FLAG_STATE = 1 << 1
FLAG_SPAWN = 1 << 2
FLAG_MECH = 1 << 3
FLAG_WAVES = 1 << 4
FLAG_ROOMS = 1 << 5
FLAG_REWARDS = 1 << 6
FLAG_REQS = 1 << 7
FLAG_COMPLETE = 1 << 8
FLAG_INSTANCE = 1 << 9
FLAG_OBJECTS = 1 << 10
FLAG_UNRESOLVED = 1 << 11
FLAG_V1 = 1 << 12

MECH_OWNER_ALIASES = {
    "fight_caves": "tzhaar_fight_cave",
    "barbarian_assault": "penance_queen",
}


def blank(slug: str) -> dict[str, object]:
    return {
        "slug": slug,
        "name": slug.replace("_", " ").title(),
        "status": READY_SIMPLIFIED,
        "class_id": CLASS["unknown"],
        "kind": 0,
        "flags": 0,
        "npc_ids": set(),
        "object_ids": set(),
        "spawn_point_count": 0,
        "spawn_region_count": 0,
        "dynamic_spawn_count": 0,
        "wave_spawn_count": 0,
        "object_anchor_count": 0,
        "safe_tile_count": 0,
        "unresolved_count": 0,
        "state_count": 0,
        "transition_count": 0,
        "param_count": 0,
        "attack_count": 0,
        "phase_count": 0,
        "mechanic_count": 0,
        "room_count": 0,
        "reward_count": 0,
        "requirement_count": 0,
        "points": [],
    }


def row(rows: dict[str, dict[str, object]], slug: str) -> dict[str, object]:
    return rows.setdefault(slug, blank(slug))


def add_point(r: dict[str, object], item: dict[str, object]) -> None:
    try:
        x = int(item["x"])
        y = int(item["y"])
        plane = int(item.get("plane", 0))
    except (KeyError, TypeError, ValueError):
        return
    r["points"].append((x, y, plane))


def add_points(r: dict[str, object], items: list[object]) -> None:
    for item in items or []:
        if isinstance(item, dict):
            add_point(r, item)


def pstr(value: str) -> bytes:
    raw = value.encode("utf-8", "replace")[:255]
    return struct.pack("<B", len(raw)) + raw


def read_list_ids(value: object) -> set[int]:
    out: set[int] = set()
    for raw in value or []:
        try:
            out.add(int(raw))
        except (TypeError, ValueError):
            pass
    return out


def apply_encounters(rows: dict[str, dict[str, object]]) -> None:
    for path in sorted(ENCOUNTERS.glob("*.toml")):
        doc = tomllib.loads(path.read_text(errors="replace"))
        slug = str(doc.get("slug") or path.stem)
        r = row(rows, slug)
        r["name"] = str(doc.get("name") or r["name"])
        r["status"] = READY
        r["class_id"] = CLASS["encounter"]
        r["flags"] = int(r["flags"]) | FLAG_ENCOUNTER | FLAG_COMPLETE
        ids = read_list_ids(doc.get("npc_ids"))
        for boss in doc.get("bosses", []) or []:
            ids |= read_list_ids([boss.get("npc_id"), boss.get("id"),
                                  boss.get("boss_npc_id")])
        r["npc_ids"].update(ids)
        r["attack_count"] = max(int(r["attack_count"]),
                                len(doc.get("attacks", []) or []))
        r["phase_count"] = max(int(r["phase_count"]),
                               len(doc.get("phases", []) or []))
        r["mechanic_count"] = max(int(r["mechanic_count"]),
                                  len(doc.get("mechanics", []) or []))
        r["room_count"] = max(int(r["room_count"]),
                              len(doc.get("rooms", []) or []))
        if doc.get("waves"):
            r["flags"] = int(r["flags"]) | FLAG_WAVES
            r["wave_spawn_count"] = max(int(r["wave_spawn_count"]),
                                        len(doc.get("waves", []) or []))
        if doc.get("rooms"):
            r["flags"] = int(r["flags"]) | FLAG_ROOMS
        if doc.get("mechanics"):
            r["flags"] = int(r["flags"]) | FLAG_MECH
        if any(str(s).lower().startswith("reward")
               for s in (doc.get("source_pages", []) or [])):
            r["flags"] = int(r["flags"]) | FLAG_REWARDS


def apply_states(rows: dict[str, dict[str, object]]) -> None:
    doc = tomllib.loads(STATES.read_text(errors="replace"))
    for entry in doc.get("activities", []) or []:
        slug = str(entry["slug"])
        r = row(rows, slug)
        r["name"] = str(entry.get("name") or r["name"])
        r["kind"] = KIND.get(str(entry.get("kind") or ""), 0)
        r["status"] = max(int(r["status"]), READY_SIMPLIFIED)
        r["flags"] = int(r["flags"]) | FLAG_STATE | FLAG_COMPLETE
        if entry.get("v1_required"):
            r["flags"] = int(r["flags"]) | FLAG_V1
        for flag in entry.get("flags", []) or []:
            if flag in ("wave_sequence",):
                r["flags"] = int(r["flags"]) | FLAG_WAVES
            elif flag in ("room_sequence", "room_local_state"):
                r["flags"] = int(r["flags"]) | FLAG_ROOMS
            elif flag in ("reward_chest",):
                r["flags"] = int(r["flags"]) | FLAG_REWARDS
            elif flag in ("activity_local_arena",):
                r["flags"] = int(r["flags"]) | FLAG_INSTANCE
            elif flag in ("object_driven",):
                r["flags"] = int(r["flags"]) | FLAG_OBJECTS
        r["npc_ids"].update(read_list_ids(entry.get("npc_ids")))
        states = entry.get("states", []) or []
        r["state_count"] = len(states)
        r["transition_count"] = len(entry.get("transitions", []) or [])
        r["param_count"] = len(entry.get("params", {}) or {})
        rewards = sum(1 for s in states if s.get("role") == "reward")
        reqs = sum(1 for s in states
                   if "required" in " ".join(s.get("flags", []) or []))
        r["reward_count"] = max(int(r["reward_count"]), rewards)
        r["requirement_count"] = max(int(r["requirement_count"]), reqs)


def apply_spawns(rows: dict[str, dict[str, object]]) -> None:
    doc = tomllib.loads(SPAWNS.read_text(errors="replace"))
    for entry in doc.get("activities", []) or []:
        slug = str(entry["slug"])
        r = row(rows, slug)
        r["name"] = str(entry.get("name") or r["name"])
        cls = str(entry.get("class") or "unknown")
        r["class_id"] = CLASS.get(cls, CLASS["unknown"])
        r["flags"] = int(r["flags"]) | FLAG_SPAWN | FLAG_INSTANCE
        if cls.startswith("wave"):
            r["flags"] = int(r["flags"]) | FLAG_WAVES
        if cls == "object_activity":
            r["flags"] = int(r["flags"]) | FLAG_OBJECTS
        if cls == "script_spawn_arena":
            r["flags"] = int(r["flags"]) | FLAG_UNRESOLVED
        if str(entry.get("source_status", "")).startswith("partial"):
            r["status"] = BLOCKS_PARITY
        r["npc_ids"].update(read_list_ids(entry.get("npc_ids")))
        r["npc_ids"].update(read_list_ids(entry.get("related_npc_ids")))
        r["object_ids"].update(read_list_ids(entry.get("object_ids")))
        for key, flag in (
            ("spawn_points", 0), ("spawn_regions", FLAG_WAVES),
            ("dynamic_spawns", 0), ("wave_spawns", FLAG_WAVES),
            ("object_anchors", FLAG_OBJECTS), ("safe_tiles", 0),
            ("unresolved_spawns", FLAG_UNRESOLVED),
        ):
            values = entry.get(key, []) or []
            if key == "spawn_points":
                r["spawn_point_count"] += len(values)
            elif key == "spawn_regions":
                r["spawn_region_count"] += len(values)
            elif key == "dynamic_spawns":
                r["dynamic_spawn_count"] += len(values)
            elif key == "wave_spawns":
                r["wave_spawn_count"] += len(values)
            elif key == "object_anchors":
                r["object_anchor_count"] += len(values)
                for obj in values:
                    if isinstance(obj, dict) and "object_id" in obj:
                        r["object_ids"].add(int(obj["object_id"]))
            elif key == "safe_tiles":
                r["safe_tile_count"] += len(values)
            elif key == "unresolved_spawns":
                r["unresolved_count"] += len(values)
                if values:
                    r["status"] = BLOCKS_PARITY
            if flag and values:
                r["flags"] = int(r["flags"]) | flag
            for item in values:
                if not isinstance(item, dict):
                    continue
                add_point(r, item)
                add_points(r, item.get("points", []))
                if all(k in item for k in ("min_x", "max_x", "min_y", "max_y")):
                    add_point(r, {"x": item["min_x"], "y": item["min_y"],
                                  "plane": item.get("plane", 0)})
                    add_point(r, {"x": item["max_x"], "y": item["max_y"],
                                  "plane": item.get("plane", 0)})
        grid = entry.get("grid", {}) or {}
        for value in grid.values():
            if isinstance(value, dict):
                add_point(r, value)
        if int(r["unresolved_count"]) > 0:
            r["flags"] = int(r["flags"]) | FLAG_UNRESOLVED


def apply_mechanics(rows: dict[str, dict[str, object]]) -> None:
    for mech in read_mechanics():
        owner = str(mech.get("encounter_slug") or mech["slug"])
        slug = MECH_OWNER_ALIASES.get(owner, owner)
        r = row(rows, slug)
        r["flags"] = int(r["flags"]) | FLAG_MECH
        r["mechanic_count"] += 1
        r["npc_ids"].update(read_list_ids(mech.get("npc_ids")))


def finalize(rows: dict[str, dict[str, object]]) -> list[dict[str, object]]:
    out = []
    for r in rows.values():
        if int(r["status"]) == BLOCKS_PARITY:
            pass
        elif int(r["flags"]) & FLAG_UNRESOLVED:
            r["status"] = BLOCKS_PARITY
        elif int(r["flags"]) & (FLAG_STATE | FLAG_SPAWN):
            r["status"] = max(int(r["status"]), READY_SIMPLIFIED)
        else:
            r["status"] = READY
        pts = list(r["points"])
        if pts:
            xs = [p[0] for p in pts]
            ys = [p[1] for p in pts]
            ps = [p[2] for p in pts]
            r["min_x"], r["max_x"] = min(xs), max(xs)
            r["min_y"], r["max_y"] = min(ys), max(ys)
            r["min_plane"], r["max_plane"] = min(ps), max(ps)
        else:
            r["min_x"] = r["max_x"] = r["min_y"] = r["max_y"] = 0
            r["min_plane"] = r["max_plane"] = 0
        r["npc_ids"] = sorted(r["npc_ids"])
        r["object_count"] = len(r["object_ids"])
        if int(r["reward_count"]) > 0:
            r["flags"] = int(r["flags"]) | FLAG_REWARDS
        if int(r["requirement_count"]) > 0:
            r["flags"] = int(r["flags"]) | FLAG_REQS
        out.append(r)
    return sorted(out, key=lambda x: str(x["slug"]))


def write_bin(rows: list[dict[str, object]]) -> None:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", ASCH_MAGIC, ASCH_VERSION, len(rows)))
        for r in rows:
            npc_ids = list(r["npc_ids"])[:64]
            object_ids = list(r["object_ids"])[:64]
            fields = (
                int(r["status"]), int(r["class_id"]), int(r["kind"]),
                int(r["flags"]), len(npc_ids), len(object_ids),
                int(r["spawn_point_count"]), int(r["spawn_region_count"]),
                int(r["dynamic_spawn_count"]), int(r["wave_spawn_count"]),
                int(r["object_anchor_count"]), int(r["safe_tile_count"]),
                int(r["unresolved_count"]), int(r["state_count"]),
                int(r["transition_count"]), int(r["param_count"]),
                int(r["attack_count"]), int(r["phase_count"]),
                int(r["mechanic_count"]), int(r["room_count"]),
                int(r["reward_count"]), int(r["requirement_count"]),
                int(r["min_x"]), int(r["max_x"]), int(r["min_y"]),
                int(r["max_y"]), int(r["min_plane"]), int(r["max_plane"]),
            )
            f.write(struct.pack("<BBBI" + "H" * 22 + "BB", *fields))
            f.write(pstr(str(r["slug"])))
            f.write(pstr(str(r["name"])))
            for npc_id in npc_ids:
                f.write(struct.pack("<I", int(npc_id)))
            for object_id in object_ids:
                f.write(struct.pack("<I", int(object_id)))


def write_report(rows: list[dict[str, object]], elapsed_ms: float) -> None:
    status_names = {
        READY: "READY",
        READY_SIMPLIFIED: "READY_WITH_ACCEPTED_SIMPLIFICATIONS",
        BLOCKS_PARITY: "BLOCKS_PARITY",
    }
    counts = {name: 0 for name in status_names.values()}
    for r in rows:
        counts[status_names[int(r["status"])]] += 1
    lines = [
        "Activity schema export",
        "",
        f"activity schemas: {len(rows)}",
        f"READY: {counts['READY']}",
        f"READY_WITH_ACCEPTED_SIMPLIFICATIONS: {counts['READY_WITH_ACCEPTED_SIMPLIFICATIONS']}",
        f"BLOCKS_PARITY: {counts['BLOCKS_PARITY']}",
        f"encounter-backed schemas: {sum(1 for r in rows if int(r['flags']) & FLAG_ENCOUNTER)}",
        f"state-machine schemas: {sum(1 for r in rows if int(r['flags']) & FLAG_STATE)}",
        f"spawn/arena schemas: {sum(1 for r in rows if int(r['flags']) & FLAG_SPAWN)}",
        f"schemas with mechanics coverage: {sum(1 for r in rows if int(r['flags']) & FLAG_MECH)}",
        f"schemas with unresolved required data: {sum(1 for r in rows if int(r['flags']) & FLAG_UNRESOLVED)}",
        f"output: {OUT.relative_to(ROOT)} ({OUT.stat().st_size} bytes)",
        f"elapsed_ms: {elapsed_ms:.3f}",
        "",
        "Status",
        "READY: authored encounter schema is loadable and executable at current encounter-runtime fidelity.",
        "READY_WITH_ACCEPTED_SIMPLIFICATIONS: schema loads, but exact per-activity tick consumers still refine behavior.",
        "BLOCKS_PARITY: schema records missing authoritative activity-local data that must be closed for 1:1 parity.",
        "",
        "BLOCKS_PARITY rows",
    ]
    blockers = [r for r in rows if int(r["status"]) == BLOCKS_PARITY]
    lines.extend(
        f"- {r['slug']}: unresolved={r['unresolved_count']} npcs={len(r['npc_ids'])}"
        for r in blockers
    )
    if not blockers:
        lines.append("- none")
    lines.extend(["", "Rows"])
    for r in rows:
        lines.append(
            f"- {r['slug']} status={status_names[int(r['status'])]} "
            f"npcs={len(r['npc_ids'])} objects={r['object_count']} "
            f"states={r['state_count']} mechanics={r['mechanic_count']} "
            f"spawns={r['spawn_point_count']}/{r['wave_spawn_count']} "
            f"unresolved={r['unresolved_count']}"
        )
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text("\n".join(lines) + "\n")


def main() -> int:
    start = time.perf_counter()
    rows: dict[str, dict[str, object]] = {}
    apply_encounters(rows)
    apply_states(rows)
    apply_spawns(rows)
    apply_mechanics(rows)
    final = finalize(rows)
    write_bin(final)
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    write_report(final, elapsed_ms)
    print(f"exported {len(final)} activity schemas in {elapsed_ms:.3f} ms")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
