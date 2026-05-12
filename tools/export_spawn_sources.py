#!/usr/bin/env python3
"""Build a unified spawn-source parity surface.

This does not replace world.npc-spawns.bin. It records static world
spawns, wiki locline NPC coordinates, curated activity spawns, dynamic
spawns, object anchors, and unresolved required spawn markers together
so spawn parity can be audited without conflating source classes.
"""
from __future__ import annotations

import json
import re
import struct
from collections import Counter
from pathlib import Path

try:
    import tomllib
except ModuleNotFoundError:  # pragma: no cover
    tomllib = None

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / "tools/wiki_cache"
OUT = ROOT / "data/defs/spawn_sources.bin"
REPORT = ROOT / "tools/reports/spawn_sources.txt"

SPAWN_MAGIC = 0x52505353  # SSPR
SPAWN_VERSION = 1

K_WORLD_STATIC = 1
K_WIKI_LOCLINE = 2
K_ACTIVITY_POINT = 3
K_ACTIVITY_WAVE = 4
K_ACTIVITY_DYNAMIC = 5
K_OBJECT_ANCHOR = 6
K_UNRESOLVED = 7
K_ACTIVITY_REGION = 8
K_ENCOUNTER_DYNAMIC = 9

F_INSTANCE = 1 << 0
F_REQUIRED = 1 << 1

MISSING_U32 = 0xFFFFFFFF
COORD_RE = re.compile(r"(?:x:)?(-?\d+)\s*,\s*(?:y:)?(-?\d+)")
NUMERIC_ID_RE = re.compile(r"^\d+$")


def load_bucket(bucket: str):
    for path in sorted(CACHE.glob(f"{bucket}_*.json")):
        data = json.loads(path.read_text())
        for row in data.get("bucket", []):
            yield row


def key_name(name: str) -> str:
    return re.sub(r"\s+", " ", (name or "").split("#", 1)[0]).strip().lower()


def read_exact(f, n: int) -> bytes:
    data = f.read(n)
    if len(data) != n:
        raise EOFError("short read")
    return data


def read_ndef_names(path: Path) -> dict[int, str]:
    out: dict[int, str] = {}
    if not path.is_file():
        return out
    with path.open("rb") as f:
        magic, version, count = struct.unpack("<III", read_exact(f, 12))
        if magic != 0x4E444546:
            return out
        for _ in range(count):
            npc_id = struct.unpack("<I", read_exact(f, 4))[0]
            read_exact(f, 1 + 2 + 2 + 12 + 20)
            name_len = struct.unpack("<B", read_exact(f, 1))[0]
            name = read_exact(f, name_len).decode("latin-1", "replace")
            if version >= 2:
                read_exact(f, 10)
            if version >= 3:
                model_count = struct.unpack("<B", read_exact(f, 1))[0]
                read_exact(f, model_count * 4)
            if version >= 4:
                for _ in range(5):
                    option_len = struct.unpack("<B", read_exact(f, 1))[0]
                    read_exact(f, option_len)
            out[npc_id] = name
    return out


def read_nspn(path: Path) -> list[dict]:
    rows: list[dict] = []
    with path.open("rb") as f:
        magic, version, count = struct.unpack("<III", read_exact(f, 12))
        if magic != 0x4E53504E:
            raise ValueError(f"bad NSPN magic: {path}")
        for _ in range(count):
            npc_id = struct.unpack("<I", read_exact(f, 4))[0]
            x = struct.unpack("<i", read_exact(f, 4))[0]
            y = struct.unpack("<i", read_exact(f, 4))[0]
            plane = struct.unpack("<B", read_exact(f, 1))[0]
            read_exact(f, 2)
            flags = struct.unpack("<B", read_exact(f, 1))[0] if version >= 2 else 0
            rows.append({"npc_id": npc_id, "x": x, "y": y,
                         "plane": plane, "flags": flags})
    return rows


def parse_ids(raw) -> list[int]:
    if raw is None:
        return []
    if not isinstance(raw, list):
        raw = [raw]
    out: list[int] = []
    for value in raw:
        text = str(value).strip()
        if NUMERIC_ID_RE.match(text):
            out.append(int(text))
    return out


def add_ids(out: dict[str, set[int]], name: str, ids: list[int]):
    key = key_name(name)
    if key and ids:
        out.setdefault(key, set()).update(ids)


def npc_ids_by_name() -> dict[str, list[int]]:
    out: dict[str, set[int]] = {}
    for row in load_bucket("infobox_monster"):
        ids = parse_ids(row.get("id"))
        for field in ("name", "page_name"):
            add_ids(out, row.get(field) or "", ids)
    for row in load_bucket("npc_id"):
        ids = parse_ids(row.get("id"))
        for field in ("page_name", "page_name_sub"):
            add_ids(out, row.get(field) or "", ids)
    return {name: sorted(ids) for name, ids in out.items()}


def choose_npc_id(ids: list[int], world_ids: set[int]) -> int:
    for npc_id in ids:
        if npc_id in world_ids:
            return npc_id
    return min(ids)


def parse_coord(value: str) -> tuple[int, int] | None:
    match = COORD_RE.search(str(value))
    if not match:
        return None
    return int(match.group(1)), int(match.group(2))


def add(rows: list[dict], kind: int, name: str, x: int, y: int, plane: int,
        npc_id: int = MISSING_U32, object_id: int = MISSING_U32,
        mapid: int = 0, flags: int = 0, activity: str = ""):
    rows.append({
        "kind": kind,
        "flags": flags,
        "npc_id": npc_id,
        "object_id": object_id,
        "x": x,
        "y": y,
        "plane": plane,
        "mapid": mapid,
        "activity": activity,
        "name": name,
    })


def add_points(rows: list[dict], kind: int, activity: str, entity: str,
               table: dict, npc_id: int = MISSING_U32):
    points = table.get("points")
    if isinstance(points, list):
        for point in points:
            add(rows, kind, entity, int(point.get("x", 0)),
                int(point.get("y", 0)), int(point.get("plane", 0)),
                int(point.get("npc_id", npc_id)), MISSING_U32, 0, 0, activity)
        return
    if "x" in table and "y" in table:
        add(rows, kind, entity, int(table.get("x", 0)), int(table.get("y", 0)),
            int(table.get("plane", 0)), int(table.get("npc_id", npc_id)),
            MISSING_U32, 0, 0, activity)


def add_region(rows: list[dict], activity: str, table: dict):
    min_x = int(table.get("min_x", table.get("x", 0)))
    max_x = int(table.get("max_x", min_x))
    min_y = int(table.get("min_y", table.get("y", 0)))
    max_y = int(table.get("max_y", min_y))
    plane = int(table.get("plane", 0))
    name = (f"{table.get('key', '')} [{min_x},{min_y}-{max_x},{max_y}]"
            .strip())
    add(rows, K_ACTIVITY_REGION, name, (min_x + max_x) // 2,
        (min_y + max_y) // 2, plane, MISSING_U32, MISSING_U32, 0, 0,
        activity)


def add_activity_rows(rows: list[dict]) -> tuple[Counter, list[str], Counter]:
    if tomllib is None:
        return Counter(), [], Counter()
    path = ROOT / "data/curated/activity_spawns.toml"
    if not path.is_file():
        return Counter(), [], Counter()
    data = tomllib.loads(path.read_text())
    status = Counter()
    shape_counts = Counter()
    unresolved: list[str] = []
    for activity in data.get("activities", []):
        slug = activity.get("slug", "")
        status[activity.get("source_status", "unknown")] += 1
        region_keys = {r.get("key", "") for r in activity.get("spawn_regions", [])}
        for table in activity.get("spawn_regions", []):
            add_region(rows, slug, table)
            shape_counts["spawn_regions"] += 1
        for table in activity.get("spawn_points", []):
            npc_id = int(table.get("npc_id", MISSING_U32))
            add_points(rows, K_ACTIVITY_POINT, slug, table.get("entity", ""),
                       table, npc_id)
            shape_counts["spawn_points"] += 1
        for table in activity.get("wave_spawns", []):
            npc_id = int(table.get("npc_id", MISSING_U32))
            add_points(rows, K_ACTIVITY_WAVE, slug, table.get("entity", ""),
                       table, npc_id)
            refs = table.get("rotation_regions") or []
            if isinstance(refs, list):
                for ref in refs:
                    if ref and ref not in region_keys:
                        unresolved.append(f"{slug}:missing_region:{ref}")
            shape_counts["wave_spawns"] += 1
        for table in activity.get("dynamic_spawns", []):
            npc_id = int(table.get("npc_id", MISSING_U32))
            add_points(rows, K_ACTIVITY_DYNAMIC, slug, table.get("entity", ""),
                       table, npc_id)
            shape_counts["dynamic_spawns"] += 1
        for table in activity.get("object_anchors", []):
            add(rows, K_OBJECT_ANCHOR, table.get("entity", ""),
                int(table.get("x", 0)), int(table.get("y", 0)),
                int(table.get("plane", 0)), MISSING_U32,
                int(table.get("object_id", MISSING_U32)), 0, 0, slug)
            shape_counts["object_anchors"] += 1
        for table in activity.get("unresolved_spawns", []):
            flags = F_REQUIRED if table.get("required") else 0
            add(rows, K_UNRESOLVED, table.get("entity", ""), 0, 0, 0,
                MISSING_U32, MISSING_U32, 0, flags, slug)
            unresolved.append(f"{slug}:{table.get('key', '')}")
            shape_counts["unresolved_spawns"] += 1
    return status, unresolved, shape_counts


def add_encounter_dynamic_rows(rows: list[dict]) -> Counter:
    if tomllib is None:
        return Counter()
    out = Counter()
    for path in sorted((ROOT / "data/curated/encounters").glob("*.toml")):
        data = tomllib.loads(path.read_text())
        slug = data.get("slug") or path.stem
        for mech in data.get("mechanics", []):
            primitive = str(mech.get("primitive", ""))
            params = mech.get("params") or {}
            if "spawn" not in primitive and "summon" not in primitive:
                continue
            name = (params.get("npc_name") or params.get("npc")
                    or params.get("healer_npc") or "")
            if not name:
                continue
            count = (params.get("count") or params.get("count_per_player")
                     or params.get("healer_count") or 1)
            try:
                count_int = int(count)
            except (TypeError, ValueError):
                count_int = 1
            add(rows, K_ENCOUNTER_DYNAMIC, str(name), 0, 0, 0,
                MISSING_U32, count_int, 0, 0, slug)
            out[primitive] += 1
    return out


def write_rows(rows: list[dict]):
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", SPAWN_MAGIC, SPAWN_VERSION, len(rows)))
        for row in rows:
            activity = row["activity"].encode("latin-1", "replace")[:255]
            name = row["name"].encode("latin-1", "replace")[:255]
            f.write(struct.pack(
                "<BBHIIiiBiBB",
                row["kind"], row["flags"], 0, row["npc_id"],
                row["object_id"], row["x"], row["y"], row["plane"] & 0xFF,
                row["mapid"], len(activity), len(name),
            ))
            f.write(activity)
            f.write(name)


def main() -> int:
    npc_names = read_ndef_names(ROOT / "data/defs/npc_defs.bin")
    world = read_nspn(ROOT / "data/spawns/world.npc-spawns.bin")
    varrock = read_nspn(ROOT / "data/regions/varrock.npc-spawns.bin")
    world_ids = {r["npc_id"] for r in world}
    rows: list[dict] = []

    for row in world:
        name = npc_names.get(row["npc_id"], "")
        flags = F_INSTANCE if row["flags"] & 1 else 0
        add(rows, K_WORLD_STATIC, name, row["x"], row["y"], row["plane"],
            row["npc_id"], MISSING_U32, 0, flags)

    ids_by_name = npc_ids_by_name()
    loc_rows = loc_coord_rows = loc_points = resolved_rows = resolved_points = 0
    unresolved_npc_loc_rows = unresolved_npc_loc_points = 0
    wiki_missing_by_name: dict[str, list[int]] = {}
    for row in load_bucket("locline"):
        loc_rows += 1
        coords = row.get("coordinates") or []
        if not isinstance(coords, list):
            coords = [coords]
        points = [p for p in (parse_coord(c) for c in coords) if p is not None]
        if points:
            loc_coord_rows += 1
            loc_points += len(points)
        ids = ids_by_name.get(key_name(row.get("page_name") or ""))
        if not ids:
            unresolved_npc_loc_rows += 1
            unresolved_npc_loc_points += len(points)
            continue
        npc_id = choose_npc_id(ids, world_ids)
        resolved_rows += 1
        resolved_points += len(points)
        mapid = int(row.get("mapid") or 0)
        plane = int(row.get("plane") or 0)
        flags = F_INSTANCE if mapid > 0 else 0
        for x, y in points:
            add(rows, K_WIKI_LOCLINE, row.get("page_name") or "", x, y,
                plane, npc_id, MISSING_U32, mapid, flags)
        if npc_id not in world_ids:
            rec = wiki_missing_by_name.setdefault(row.get("page_name") or "",
                                                  [npc_id, 0, 0])
            rec[1] += 1
            rec[2] += len(points)

    activity_status, unresolved, activity_shapes = add_activity_rows(rows)
    encounter_dynamic = add_encounter_dynamic_rows(rows)
    write_rows(rows)

    by_kind = Counter(row["kind"] for row in rows)
    kind_names = {
        K_WORLD_STATIC: "world_static",
        K_WIKI_LOCLINE: "wiki_locline",
        K_ACTIVITY_POINT: "activity_point",
        K_ACTIVITY_WAVE: "activity_wave",
        K_ACTIVITY_DYNAMIC: "activity_dynamic",
        K_OBJECT_ANCHOR: "object_anchor",
        K_UNRESOLVED: "unresolved_required",
        K_ACTIVITY_REGION: "activity_region",
        K_ENCOUNTER_DYNAMIC: "encounter_dynamic",
    }
    lines = [
        "Spawn source parity report",
        "",
        f"output binary: data/defs/spawn_sources.bin ({OUT.stat().st_size} bytes)",
        f"source rows:   {len(rows)}",
        f"data/source present: {(ROOT / 'data/source').is_dir()}",
        "",
        "compiled static spawns:",
        f"  world rows:            {len(world)}",
        f"  world NPC IDs:         {len(world_ids)}",
        f"  instance-flagged rows: {sum(1 for r in world if r['flags'] & 1)}",
        f"  varrock rows:          {len(varrock)}",
        "",
        "wiki locline:",
        f"  rows:                  {loc_rows}",
        f"  rows with coordinates: {loc_coord_rows}",
        f"  coordinate points:     {loc_points}",
        f"  resolved NPC rows:     {resolved_rows}",
        f"  resolved NPC points:   {resolved_points}",
        f"  non-NPC/unresolved rows: {unresolved_npc_loc_rows}",
        f"  non-NPC/unresolved pts: {unresolved_npc_loc_points}",
        f"  NPC names supplied only by wiki/source index: {len(wiki_missing_by_name)}",
        "",
        "activity_spawns.toml:",
    ]
    for status, count in sorted(activity_status.items()):
        lines.append(f"  {status:<44} {count}")
    for shape, count in sorted(activity_shapes.items()):
        lines.append(f"  {shape:<44} {count}")
    lines.extend([
        f"  unresolved required markers: {len(unresolved)}",
        "",
        "encounter-authored dynamic NPC spawns:",
    ])
    for primitive, count in sorted(encounter_dynamic.items()):
        lines.append(f"  {primitive:<44} {count}")
    lines.extend([
        "",
        "rows by source kind:",
    ])
    for kind in sorted(by_kind):
        lines.append(f"  {kind_names[kind]:<20} {by_kind[kind]}")
    lines.extend(["", "top wiki NPC names missing from world binary by coordinate count:"])
    for name, (npc_id, row_count, point_count) in sorted(
            wiki_missing_by_name.items(), key=lambda kv: -kv[1][2])[:40]:
        lines.append(f"  npc_id={npc_id:<6} rows={row_count:<4} "
                     f"points={point_count:<4} {name}")
    lines.extend(["", "unresolved required activity markers:"])
    for item in unresolved:
        lines.append(f"  {item}")
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text("\n".join(lines) + "\n")
    print(REPORT.read_text())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
