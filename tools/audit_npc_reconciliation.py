#!/usr/bin/env python3
"""NPC/boss/monster reconciliation report.

Checks identity, model links, renderable model meshes, drops, spawns,
morph/state hooks, and mechanics coverage against the current RuneC
NPC definition export.
"""

from __future__ import annotations

import json
import re
import struct
from pathlib import Path

try:
    import tomllib
except ModuleNotFoundError:  # pragma: no cover
    tomllib = None

ROOT = Path(__file__).resolve().parents[1]
REF = Path("/home/joe/projects/runescape-rl-reference")
DATA_OSRS = REF / "data_osrs"
MODEL_DUMP = REF / "model_dump/osrs-dumps"
RUNELITE_NPC_ID = REF / "runelite/runelite-api/src/main/java/net/runelite/api/NpcID.java"

NDEF_MAGIC = 0x4E444546
DROP_MAGIC = 0x504F5244
NSPN_MAGIC = 0x4E53504E
MDL2_MAGIC = 0x4D444C32


def read_exact(f, n: int) -> bytes:
    b = f.read(n)
    if len(b) != n:
        raise EOFError("short read")
    return b


def read_ndef(path: Path) -> dict[int, dict]:
    defs: dict[int, dict] = {}
    with path.open("rb") as f:
        magic, version, count = struct.unpack("<III", read_exact(f, 12))
        if magic != NDEF_MAGIC:
            raise ValueError("bad NDEF magic")
        for _ in range(count):
            npc_id = struct.unpack("<I", read_exact(f, 4))[0]
            size = struct.unpack("<B", read_exact(f, 1))[0]
            combat = struct.unpack("<h", read_exact(f, 2))[0]
            hp = struct.unpack("<H", read_exact(f, 2))[0]
            stats = struct.unpack("<6H", read_exact(f, 12))
            anims = struct.unpack("<5i", read_exact(f, 20))
            name_len = struct.unpack("<B", read_exact(f, 1))[0]
            name = read_exact(f, name_len).decode("latin-1", "replace")
            rec = {
                "id": npc_id,
                "name": name,
                "size": size,
                "combat": combat,
                "hp": hp,
                "stats": stats,
                "anims": anims,
                "models": [],
            }
            if version >= 2:
                read_exact(f, 10)
            if version >= 3:
                model_count = struct.unpack("<B", read_exact(f, 1))[0]
                rec["models"] = [
                    struct.unpack("<I", read_exact(f, 4))[0]
                    for _ in range(model_count)
                ]
            defs[npc_id] = rec
    return defs


def runelite_ids() -> dict[int, str]:
    ids: dict[int, str] = {}
    if not RUNELITE_NPC_ID.is_file():
        return ids
    for line in RUNELITE_NPC_ID.read_text(errors="replace").splitlines():
        m = re.search(r"public static final int ([A-Z0-9_]+) = (\d+);", line)
        if not m:
            continue
        ids[int(m.group(2))] = m.group(1)
    return ids


def npc_symbols() -> dict[int, str]:
    path = MODEL_DUMP / "symbols/npc.sym"
    out: dict[int, str] = {}
    if not path.is_file():
        return out
    for line in path.read_text(errors="replace").splitlines():
        parts = line.split(None, 1)
        if len(parts) == 2 and parts[0].isdigit():
            out[int(parts[0])] = parts[1]
    return out


def drop_table_ids(path: Path) -> set[int]:
    out: set[int] = set()
    with path.open("rb") as f:
        magic, _version, count = struct.unpack("<III", read_exact(f, 12))
        if magic != DROP_MAGIC:
            raise ValueError("bad DROP magic")
        for _ in range(count):
            npc_id = struct.unpack("<I", read_exact(f, 4))[0]
            out.add(npc_id)
            always = struct.unpack("<B", read_exact(f, 1))[0]
            read_exact(f, always * 8)
            main = struct.unpack("<B", read_exact(f, 1))[0]
            read_exact(f, main * 12)
            tertiary = struct.unpack("<B", read_exact(f, 1))[0]
            read_exact(f, tertiary * 12)
            read_exact(f, 4)
    return out


def spawn_ids(path: Path) -> tuple[set[int], set[int], int]:
    all_ids: set[int] = set()
    instance_ids: set[int] = set()
    rows = 0
    with path.open("rb") as f:
        magic, version, count = struct.unpack("<III", read_exact(f, 12))
        if magic != NSPN_MAGIC:
            raise ValueError("bad NSPN magic")
        for _ in range(count):
            npc_id = struct.unpack("<I", read_exact(f, 4))[0]
            read_exact(f, 4 + 4 + 1 + 1 + 1)
            flags = struct.unpack("<B", read_exact(f, 1))[0] if version >= 2 else 0
            rows += 1
            all_ids.add(npc_id)
            if flags & 1:
                instance_ids.add(npc_id)
    return all_ids, instance_ids, rows


def model_mesh_ids(path: Path) -> set[int]:
    if not path.is_file():
        return set()
    ids: set[int] = set()
    with path.open("rb") as f:
        magic, count = struct.unpack("<II", read_exact(f, 8))
        if magic != MDL2_MAGIC:
            return ids
        offsets = struct.unpack(f"<{count}I", read_exact(f, 4 * count))
        for off in offsets:
            f.seek(off)
            ids.add(struct.unpack("<I", read_exact(f, 4))[0])
    return ids


def model_export_summary(path: Path) -> dict[str, int]:
    out: dict[str, int] = {}
    if not path.is_file():
        return out
    for line in path.read_text(errors="replace").splitlines():
        if ":" not in line:
            continue
        key, val = line.split(":", 1)
        key = key.strip().lower().replace(" ", "_").replace("-", "_")
        m = re.search(r"\d+", val)
        if m:
            out[key] = int(m.group(0))
    return out


def load_json(path: Path):
    return json.loads(path.read_text()) if path.is_file() else {}


def toml_names(path: Path) -> set[str]:
    return {p.stem.lower().replace("_", " ") for p in path.glob("*.toml")}


def main() -> int:
    defs = read_ndef(ROOT / "data/defs/npc_defs.bin")
    runelite = runelite_ids()
    symbols = npc_symbols()
    sailing = {i for i, s in symbols.items() if "sailing" in s.lower()}
    missing_runelite = sorted(set(runelite) - set(defs) - sailing)

    drop_ids = drop_table_ids(ROOT / "data/defs/drops.bin")
    spawn_all, spawn_instance, spawn_rows = spawn_ids(ROOT / "data/spawns/world.npc-spawns.bin")
    render_ids = model_mesh_ids(ROOT / "data/models/npcs.models")
    model_export = model_export_summary(ROOT / "tools/reports/npc_models_full.txt")
    morphs = load_json(DATA_OSRS / "npc_morph_collection.json")
    aliases = load_json(DATA_OSRS / "npc_name_collection.json")

    morph_parents = {int(k) for k in morphs}
    morph_targets = {int(v) for vals in morphs.values() for v in vals if int(v) >= 0}
    missing_morph_parents = morph_parents - set(defs) - sailing
    missing_morph_targets = morph_targets - set(defs) - sailing
    npclist_varbit = 0
    npclist_varp = 0
    for p in (DATA_OSRS / "npcids").glob("npcid=*.json"):
        for row in json.loads(p.read_text()):
            if int(row.get("varbitId", -1)) >= 0:
                npclist_varbit += 1
            if int(row.get("varpId", -1)) >= 0:
                npclist_varp += 1

    model_linked = {i for i, d in defs.items() if d["models"]}
    combat_ids = {i for i, d in defs.items() if d["combat"] > 0}
    mechanic_names = toml_names(ROOT / "data/curated/mechanics")
    encounter_names = toml_names(ROOT / "data/curated/encounters")
    special_path = ROOT / "data/curated/regular_npc_special_mechanics.toml"
    special_families = []
    if tomllib is not None and special_path.is_file():
        special_families = tomllib.loads(special_path.read_text()).get("families", [])

    missing_render = model_linked - render_ids
    oversized = model_export.get("oversized_for_mdl2_u16_shape", 0)
    blockers = []
    if missing_render or oversized:
        blockers.append(
            "- model decode/render coverage still misses linked NPC meshes"
        )
    blockers += [
        "- boss mechanics conversion and explicit name/group mapping from mechanics TOMLs into executable encounter/activity data",
        "- complete regular monster special-mechanics family details and runtime owner",
        "- exact Nex and Sol Heredit activity-spawn extraction from cache/server-script/activity configs",
    ]

    lines = [
        "NPC reconciliation report",
        "",
        f"npc_defs: {len(defs)}",
        f"runelite non-excluded missing: {len(missing_runelite)}",
        f"aliases in data_osrs name collection: {len(aliases)}",
        "",
        "Model coverage",
        f"defs with model ID links: {len(model_linked)}",
        f"renderable NPC mesh entries: {len(render_ids)}",
        f"model-linked defs without renderable mesh: {len(missing_render)}",
        f"oversized / unsupported by current MDL2 path: {oversized}",
        f"defs with at least one missing model part: {model_export.get('defs_with_at_least_one_missing_model_part', 0)}",
        "",
        "Drop / spawn coverage",
        f"drop tables: {len(drop_ids)}",
        f"drop table IDs missing npc_defs: {len(drop_ids - set(defs))}",
        f"world spawn rows: {spawn_rows}",
        f"world spawn NPC IDs: {len(spawn_all)}",
        f"world spawn IDs missing npc_defs: {len(spawn_all - set(defs))}",
        f"instance-flagged spawn NPC IDs: {len(spawn_instance)}",
        "",
        "Morph / state coverage",
        f"morph parent IDs: {len(morph_parents)}",
        f"morph target IDs: {len(morph_targets)}",
        f"non-excluded morph parent IDs missing npc_defs: {len(missing_morph_parents)}",
        f"non-excluded morph target IDs missing npc_defs: {len(missing_morph_targets)}",
        f"NPCList rows with varbitId: {npclist_varbit}",
        f"NPCList rows with varpId: {npclist_varp}",
        "",
        "Mechanics coverage",
        f"combat defs: {len(combat_ids)}",
        f"mechanics TOMLs: {len(mechanic_names)}",
        f"executable encounter TOMLs: {len(encounter_names)}",
        f"regular special-mechanics families tracked: {len(special_families)}",
        f"name-normalized mechanics TOMLs unmatched to encounter TOML: {len(mechanic_names - encounter_names)}",
        "",
        "Remaining blockers",
    ] + blockers
    if missing_runelite:
        lines += ["", "Sample non-excluded missing RuneLite IDs:"]
        for npc_id in missing_runelite[:30]:
            lines.append(f"  {npc_id}: {runelite[npc_id]}")
    else:
        lines += ["", "Non-excluded RuneLite ID coverage: complete"]
    missing_mechanics = sorted(mechanic_names - encounter_names)
    if missing_mechanics:
        lines += ["", "Sample name-normalized mechanics TOMLs unmatched to encounter TOML:"]
        for name in missing_mechanics[:40]:
            lines.append(f"  {name}")

    out = ROOT / "tools/reports/npc_reconciliation.txt"
    out.write_text("\n".join(lines) + "\n")
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
