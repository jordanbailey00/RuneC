#!/usr/bin/env python3
"""Export broad NPC definitions from structured local sources.

This does not need the b237 flat cache. It merges the widest local
structured surfaces we currently have:
- data_osrs npcids / NPCList
- model_dump/osrs-dumps NPC config dump
- osrsreboxed monster JSON
- cached OSRS Wiki infobox_monster rows
- RuneLite NpcID names
- curated activity_spawns edge-case IDs
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from pathlib import Path
from typing import Any

try:
    import tomllib
except ModuleNotFoundError:  # pragma: no cover - Python <3.11 fallback.
    tomllib = None

ROOT = Path(__file__).resolve().parents[1]
REF = Path("/home/joe/projects/runescape-rl-reference")
DATA_OSRS = REF / "data_osrs"
OSRSREBOXED = REF / "osrsreboxed-db"
RUNELITE_NPC_ID = REF / "runelite/runelite-api/src/main/java/net/runelite/api/NpcID.java"
WIKI_CACHE = ROOT / "tools/wiki_cache"
MODEL_DUMP = REF / "model_dump/osrs-dumps"
NPC_DUMP = MODEL_DUMP / "config/dump.npc"
SEQ_SYMBOLS = MODEL_DUMP / "symbols/seq.sym"
NPC_SYMBOLS = MODEL_DUMP / "symbols/npc.sym"

NDEF_MAGIC = 0x4E444546
NDEF_VERSION = 3
MAX_NPC_MODELS = 12

ATK_TYPE_BIT = {
    "stab": 0x01,
    "slash": 0x02,
    "crush": 0x04,
    "melee": 0x04,
    "magic": 0x08,
    "magical": 0x08,
    "ranged": 0x10,
    "range": 0x10,
}

WEAKNESS_BIT = {
    "fire": 0x01,
    "water": 0x02,
    "earth": 0x04,
    "air": 0x08,
    "stab": 0x10,
    "slash": 0x20,
    "crush": 0x40,
    "ranged": 0x80,
    "magic": 0x80,
}


def parse_int(v: Any) -> int | None:
    if v is None:
        return None
    if isinstance(v, bool):
        return int(v)
    if isinstance(v, (int, float)):
        return int(v)
    m = re.search(r"-?\d+", str(v))
    return int(m.group(0)) if m else None


def keep_int(value: int | None, fallback: int) -> int:
    return value if value is not None else fallback


def parse_max_hit(v: Any) -> int:
    if v is None:
        return 0
    vals = v if isinstance(v, list) else [v]
    hits = [parse_int(x) for x in vals]
    hits = [x for x in hits if x is not None]
    return max(hits) if hits else 0


def parse_immune(v: Any) -> bool | None:
    if v is None:
        return None
    s = str(v).strip().lower()
    if not s:
        return None
    if "not immune" in s or s == "false":
        return False
    if "immune" in s or s == "true":
        return True
    return None


def bits_from_list(values: Any, table: dict[str, int]) -> int:
    vals = values if isinstance(values, list) else [values]
    out = 0
    for value in vals:
        if value is None:
            continue
        text = str(value).lower()
        for key, bit in table.items():
            if key in text:
                out |= bit
    return out


def blank_def(npc_id: int) -> dict[str, Any]:
    return {
        "id": npc_id,
        "name": f"npc_{npc_id}",
        "size": 1,
        "combat_level": -1,
        "stats": [1, 1, 1, 1, 1, 1],
        "stand_anim": -1,
        "walk_anim": -1,
        "run_anim": -1,
        "attack_anim": -1,
        "death_anim": -1,
        "aggressive": False,
        "max_hit": 0,
        "attack_speed": 0,
        "aggro_range": 0,
        "slayer_level": 1,
        "attack_types": 0,
        "weakness": 0,
        "poison_immune": False,
        "venom_immune": False,
        "models": [],
        "sources": set(),
    }


def get_def(defs: dict[int, dict[str, Any]], npc_id: int) -> dict[str, Any]:
    if npc_id not in defs:
        defs[npc_id] = blank_def(npc_id)
    return defs[npc_id]


def set_name(d: dict[str, Any], name: Any):
    if not name:
        return
    name = str(name).strip()
    if name and (d["name"].startswith("npc_") or len(name) < len(d["name"]) + 48):
        d["name"] = name[:63]


def apply_data_osrs(defs: dict[int, dict[str, Any]]) -> int:
    count = 0
    for path in sorted((DATA_OSRS / "npcids").glob("npcid=*.json")):
        for rec in json.loads(path.read_text()):
            npc_id = int(rec["id"])
            d = get_def(defs, npc_id)
            set_name(d, rec.get("name"))
            d["size"] = int(rec.get("size") or d["size"] or 1)
            d["combat_level"] = int(rec.get("combatLevel", d["combat_level"]))
            d["stand_anim"] = int(rec.get("standingAnimation", d["stand_anim"]) or -1)
            d["walk_anim"] = int(rec.get("walkingAnimation", d["walk_anim"]) or -1)
            d["models"] = rec.get("models") or d["models"]
            d["sources"].add("data_osrs")
            count += 1
    return count


def load_symbol_ids(path: Path) -> dict[str, int]:
    out: dict[str, int] = {}
    if not path.is_file():
        return out
    for line in path.read_text(errors="replace").splitlines():
        parts = line.split(None, 1)
        if len(parts) == 2 and parts[0].isdigit():
            out[parts[1].strip()] = int(parts[0])
    return out


def load_id_symbols(path: Path) -> dict[int, str]:
    symbols: dict[int, str] = {}
    if not path.is_file():
        return symbols
    for line in path.read_text(errors="replace").splitlines():
        parts = line.split(None, 1)
        if len(parts) == 2 and parts[0].isdigit():
            symbols[int(parts[0])] = parts[1].strip()
    return symbols


def first_value(fields: dict[str, list[str]], key: str) -> str | None:
    vals = fields.get(key)
    return vals[0] if vals else None


def seq_id(value: str | None, seq_ids: dict[str, int]) -> int:
    if not value:
        return -1
    first = value.split(",", 1)[0].strip()
    if not first or first == "null":
        return -1
    if first.startswith("seq_"):
        parsed = parse_int(first)
        return parsed if parsed is not None else -1
    return seq_ids.get(first, -1)


def iter_npc_dump(path: Path):
    if not path.is_file():
        return
    npc_id: int | None = None
    symbol: str | None = None
    fields: dict[str, list[str]] = {}

    def flush():
        if npc_id is not None and symbol is not None:
            yield npc_id, symbol, fields

    for raw in path.read_text(errors="replace").splitlines():
        line = raw.strip()
        if line.startswith("//"):
            yield from flush()
            parsed = parse_int(line)
            npc_id = parsed
            symbol = None
            fields = {}
            continue
        if line.startswith("[") and line.endswith("]"):
            symbol = line[1:-1]
            continue
        if "=" in line:
            key, value = line.split("=", 1)
            fields.setdefault(key.strip(), []).append(value.strip())
    yield from flush()


def skip_model_dump_symbol(symbol: str, fields: dict[str, list[str]]) -> bool:
    text = f"{symbol} {first_value(fields, 'name') or ''}".lower()
    return "sailing" in text


def model_ids_from_fields(fields: dict[str, list[str]]) -> list[int]:
    models: list[int] = []
    seen: set[int] = set()
    for key, vals in fields.items():
        if not (key.startswith("model") and key[5:].isdigit()):
            continue
        for val in vals:
            parsed = parse_int(val)
            if parsed is not None and parsed not in seen:
                seen.add(parsed)
                models.append(parsed)
    return models[:MAX_NPC_MODELS]


def apply_model_dump(defs: dict[int, dict[str, Any]], seq_ids: dict[str, int]) -> tuple[int, int]:
    count = 0
    skipped = 0
    for npc_id, symbol, fields in iter_npc_dump(NPC_DUMP):
        if skip_model_dump_symbol(symbol, fields):
            skipped += 1
            continue
        d = get_def(defs, npc_id)
        set_name(d, first_value(fields, "name"))
        d["size"] = parse_int(first_value(fields, "size")) or d["size"] or 1
        d["combat_level"] = keep_int(
            parse_int(first_value(fields, "vislevel")),
            d["combat_level"],
        )

        hp = parse_int(first_value(fields, "hitpoints"))
        stats = d["stats"]
        stats[0] = keep_int(parse_int(first_value(fields, "attack")), stats[0])
        stats[1] = keep_int(parse_int(first_value(fields, "defence")), stats[1])
        stats[2] = keep_int(parse_int(first_value(fields, "strength")), stats[2])
        stats[3] = keep_int(hp, stats[3])
        stats[4] = keep_int(parse_int(first_value(fields, "ranged")), stats[4])
        stats[5] = keep_int(parse_int(first_value(fields, "magic")), stats[5])
        d["stats"] = stats

        stand_anim = seq_id(first_value(fields, "readyanim"), seq_ids)
        walk_anim = seq_id(first_value(fields, "walkanim"), seq_ids)
        if stand_anim >= 0:
            d["stand_anim"] = stand_anim
        if walk_anim >= 0:
            d["walk_anim"] = walk_anim
        run_anim = seq_id(first_value(fields, "runanim"), seq_ids)
        if run_anim >= 0:
            d["run_anim"] = run_anim
        models = model_ids_from_fields(fields)
        if models:
            # b237 model_dump matches the cache used by the model exporter.
            d["models"] = models
        d["sources"].add("model_dump")
        count += 1
    return count, skipped


def apply_osrsreboxed(defs: dict[int, dict[str, Any]]) -> int:
    count = 0
    for path in sorted((OSRSREBOXED / "docs/monsters-json").glob("*.json")):
        rec = json.loads(path.read_text())
        npc_id = int(rec["id"])
        d = get_def(defs, npc_id)
        set_name(d, rec.get("name"))
        d["size"] = int(rec.get("size") or d["size"] or 1)
        d["combat_level"] = keep_int(parse_int(rec.get("combat_level")), d["combat_level"])
        hp = keep_int(parse_int(rec.get("hitpoints")), d["stats"][3])
        d["stats"] = [
            keep_int(parse_int(rec.get("attack_level")), d["stats"][0]),
            keep_int(parse_int(rec.get("defence_level")), d["stats"][1]),
            keep_int(parse_int(rec.get("strength_level")), d["stats"][2]),
            hp,
            keep_int(parse_int(rec.get("ranged_level")), d["stats"][4]),
            keep_int(parse_int(rec.get("magic_level")), d["stats"][5]),
        ]
        d["max_hit"] = int(rec.get("max_hit") or d["max_hit"] or 0)
        d["attack_speed"] = int(rec.get("attack_speed") or d["attack_speed"] or 0)
        d["aggressive"] = bool(rec.get("aggressive", d["aggressive"]))
        d["aggro_range"] = 8 if d["aggressive"] else d["aggro_range"]
        d["slayer_level"] = int(rec.get("slayer_level") or d["slayer_level"] or 1)
        d["attack_types"] |= bits_from_list(rec.get("attack_type"), ATK_TYPE_BIT)
        weak = rec.get("weakness")
        if weak:
            d["weakness"] |= bits_from_list([weak], WEAKNESS_BIT)
        d["poison_immune"] = bool(rec.get("immune_poison", d["poison_immune"]))
        d["venom_immune"] = bool(rec.get("immune_venom", d["venom_immune"]))
        d["sources"].add("osrsreboxed")
        count += 1
    return count


def apply_wiki_monsters(defs: dict[int, dict[str, Any]]) -> int:
    count = 0
    for path in sorted(WIKI_CACHE.glob("infobox_monster_*.json")):
        data = json.loads(path.read_text())
        for rec in data.get("bucket", []):
            ids = rec.get("id") or []
            if not isinstance(ids, list):
                ids = [ids]
            for raw_id in ids:
                npc_id = parse_int(raw_id)
                if npc_id is None:
                    continue
                d = get_def(defs, npc_id)
                set_name(d, rec.get("name") or rec.get("page_name"))
                d["size"] = int(rec.get("size") or d["size"] or 1)
                d["combat_level"] = keep_int(parse_int(rec.get("combat_level")), d["combat_level"])
                hp = keep_int(parse_int(rec.get("hitpoints")), d["stats"][3])
                d["stats"] = [
                    keep_int(parse_int(rec.get("attack_level")), d["stats"][0]),
                    keep_int(parse_int(rec.get("defence_level")), d["stats"][1]),
                    keep_int(parse_int(rec.get("strength_level")), d["stats"][2]),
                    hp,
                    keep_int(parse_int(rec.get("ranged_level")), d["stats"][4]),
                    keep_int(parse_int(rec.get("magic_level")), d["stats"][5]),
                ]
                max_hit = parse_max_hit(rec.get("max_hit"))
                if max_hit > 0 or rec.get("max_hit") is not None:
                    d["max_hit"] = max_hit
                d["attack_speed"] = parse_int(rec.get("attack_speed")) or d["attack_speed"]
                d["slayer_level"] = keep_int(parse_int(rec.get("slayer_level")), d["slayer_level"])
                d["attack_types"] |= bits_from_list(rec.get("attack_style"), ATK_TYPE_BIT)
                poison = parse_immune(rec.get("poison_immune"))
                venom = parse_immune(rec.get("venom_immune"))
                if poison is not None:
                    d["poison_immune"] = poison
                if venom is not None:
                    d["venom_immune"] = venom
                d["sources"].add("wiki_monster")
                count += 1
    return count


def runelite_names() -> dict[int, str]:
    names: dict[int, str] = {}
    if not RUNELITE_NPC_ID.is_file():
        return names
    for line in RUNELITE_NPC_ID.read_text(errors="replace").splitlines():
        m = re.search(r"public static final int ([A-Z0-9_]+) = (\d+);", line)
        if not m:
            continue
        raw, npc_id = m.group(1), int(m.group(2))
        raw = re.sub(r"_\d+$", "", raw)
        special = {
            "TZKALZUK": "TzKal-Zuk",
            "TZTOKJAD": "TzTok-Jad",
            "JALMEJJAK": "Jal-MejJak",
            "NEX": "Nex",
        }
        name = special.get(raw, raw.replace("_", " ").title())
        names.setdefault(npc_id, name[:63])
    return names


def apply_activity_spawns(defs: dict[int, dict[str, Any]], names: dict[int, str]) -> int:
    if tomllib is None:
        return 0
    path = ROOT / "data/curated/activity_spawns.toml"
    if not path.is_file():
        return 0
    data = tomllib.loads(path.read_text())
    count = 0
    for activity in data.get("activities", []):
        activity_name = activity.get("name")
        for npc_id in activity.get("npc_ids", []) + activity.get("related_npc_ids", []):
            d = get_def(defs, int(npc_id))
            set_name(d, names.get(int(npc_id), activity_name))
            d["sources"].add("activity_spawns")
            count += 1
        for table in ("spawn_points", "wave_spawns", "dynamic_spawns"):
            for rec in activity.get(table, []):
                npc_id = rec.get("npc_id")
                if npc_id is None:
                    continue
                d = get_def(defs, int(npc_id))
                set_name(d, rec.get("entity") or names.get(int(npc_id)))
                d["sources"].add("activity_spawns")
                count += 1
    return count


def apply_runelite_name_fallback(defs: dict[int, dict[str, Any]], names: dict[int, str]) -> int:
    count = 0
    for npc_id, name in names.items():
        if npc_id in defs and defs[npc_id]["name"].startswith("npc_"):
            defs[npc_id]["name"] = name
            defs[npc_id]["sources"].add("runelite_name")
            count += 1
    return count


def write_ndef(path: Path, defs: dict[int, dict[str, Any]]):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as f:
        f.write(struct.pack("<III", NDEF_MAGIC, NDEF_VERSION, len(defs)))
        for npc_id in sorted(defs):
            d = defs[npc_id]
            stats = [max(0, min(65535, int(x))) for x in d["stats"]]
            name = str(d["name"]).encode("latin-1", errors="replace")[:63]
            immu = (1 if d["poison_immune"] else 0) | (2 if d["venom_immune"] else 0)
            f.write(struct.pack("<I", npc_id))
            f.write(struct.pack("<B", max(1, min(255, int(d["size"])))))
            f.write(struct.pack("<h", max(-32768, min(32767, int(d["combat_level"])))))
            f.write(struct.pack("<H", stats[3]))
            for s in stats:
                f.write(struct.pack("<H", s))
            for key in ("stand_anim", "walk_anim", "run_anim", "attack_anim", "death_anim"):
                f.write(struct.pack("<i", int(d[key])))
            f.write(struct.pack("<B", len(name)))
            f.write(name)
            f.write(struct.pack("<B", 1 if d["aggressive"] else 0))
            f.write(struct.pack("<H", max(0, min(65535, int(d["max_hit"])))))
            f.write(struct.pack("<B", max(0, min(255, int(d["attack_speed"])))))
            f.write(struct.pack("<B", max(0, min(255, int(d["aggro_range"])))))
            f.write(struct.pack("<H", max(0, min(65535, int(d["slayer_level"])))))
            f.write(struct.pack("<B", int(d["attack_types"]) & 0xFF))
            f.write(struct.pack("<B", int(d["weakness"]) & 0xFF))
            f.write(struct.pack("<B", immu & 0xFF))
            models = [int(x) for x in d["models"][:MAX_NPC_MODELS]]
            f.write(struct.pack("<B", len(models)))
            for model_id in models:
                f.write(struct.pack("<I", model_id))


def report(defs: dict[int, dict[str, Any]], names: dict[int, str],
           id_symbols: dict[int, str], model_dump_skipped: int) -> str:
    source_counts: dict[str, int] = {}
    for d in defs.values():
        for source in d["sources"]:
            source_counts[source] = source_counts.get(source, 0) + 1
    runelite_missing = sorted(set(names) - set(defs))
    runelite_missing_excluded = [
        npc_id for npc_id in runelite_missing
        if "sailing" in id_symbols.get(npc_id, "").lower()
    ]
    excluded_missing = set(runelite_missing_excluded)
    runelite_missing_other = [
        npc_id for npc_id in runelite_missing
        if npc_id not in excluded_missing
    ]
    known = [2042, 3127, 7706, 7707, 7708, 10572, 10574, 10575, 11278, 12821]
    lines = [
        "Full NPC definition export",
        "",
        f"defs exported: {len(defs)}",
        f"combat defs: {sum(1 for d in defs.values() if d['combat_level'] > 0)}",
        f"defs with model ids: {sum(1 for d in defs.values() if d['models'])}",
        f"defs with stand anim: {sum(1 for d in defs.values() if d['stand_anim'] >= 0)}",
        f"defs with walk anim: {sum(1 for d in defs.values() if d['walk_anim'] >= 0)}",
        f"model-dump Sailing NPCs skipped by v1 scope: {model_dump_skipped}",
        "",
        "source contribution:",
    ]
    for key, value in sorted(source_counts.items()):
        lines.append(f"  {key}: {value}")
    lines += [
        "",
        "known edge IDs:",
    ]
    for npc_id in known:
        d = defs.get(npc_id)
        if d:
            lines.append(f"  {npc_id}: {d['name']} ({','.join(sorted(d['sources']))})")
        else:
            lines.append(f"  {npc_id}: MISSING")
    lines += [
        "",
        f"RuneLite NpcID constants absent after v1 exclusions: {len(runelite_missing_other)}",
        f"RuneLite NpcID constants skipped as v1-excluded Sailing: {len(runelite_missing_excluded)}",
        "Sample missing RuneLite IDs:",
    ]
    for npc_id in runelite_missing_other[:40]:
        lines.append(f"  {npc_id}: {names[npc_id]}")
    if not runelite_missing_other:
        lines.append("  none")
    lines += [
        "",
        "Remaining blockers:",
        "- NPC definition export is whole-game for current in-scope ID sources.",
        "- Model rendering parity is tracked by tools/reports/npc_models_full.txt.",
        "- NPC semantic parity is tracked by tools/reports/npc_semantics.txt.",
        "- Attack/death/run animation coverage remains sparse until a deeper",
        "  cache-backed definition pass resolves those sequence links.",
    ]
    return "\n".join(lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=ROOT / "data/defs/npc_defs.bin")
    ap.add_argument("--report", type=Path, default=ROOT / "tools/reports/npc_defs_full.txt")
    args = ap.parse_args()

    defs: dict[int, dict[str, Any]] = {}
    seq_ids = load_symbol_ids(SEQ_SYMBOLS)
    id_symbols = load_id_symbols(NPC_SYMBOLS)
    data_osrs_count = apply_data_osrs(defs)
    model_dump_count, model_dump_skipped = apply_model_dump(defs, seq_ids)
    reboxed_count = apply_osrsreboxed(defs)
    wiki_count = apply_wiki_monsters(defs)
    names = runelite_names()
    activity_count = apply_activity_spawns(defs, names)
    fallback_count = apply_runelite_name_fallback(defs, names)

    write_ndef(args.out, defs)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(report(defs, names, id_symbols, model_dump_skipped))

    print(f"data_osrs rows merged: {data_osrs_count}")
    print(f"model_dump defs merged: {model_dump_count}")
    print(f"model_dump v1-excluded Sailing defs skipped: {model_dump_skipped}")
    print(f"osrsreboxed rows merged: {reboxed_count}")
    print(f"wiki monster id rows merged: {wiki_count}")
    print(f"activity IDs merged: {activity_count}")
    print(f"runelite fallback names used: {fallback_count}")
    print(f"wrote {len(defs)} defs to {args.out}")
    print(f"wrote report to {args.report}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
