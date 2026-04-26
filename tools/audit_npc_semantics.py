#!/usr/bin/env python3
"""Audit NPC semantic parity against wiki/cache/reference sources."""

from __future__ import annotations

import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REF = Path("/home/joe/projects/runescape-rl-reference")
DATA_OSRS = REF / "data_osrs"
WIKI_CACHE = ROOT / "tools/wiki_cache"

NDEF_MAGIC = 0x4E444546
VBIT_MAGIC = 0x54494256
VARP_MAGIC = 0x50524156


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
            read_exact(f, 20)
            name_len = struct.unpack("<B", read_exact(f, 1))[0]
            name = read_exact(f, name_len).decode("latin-1", "replace")
            rec = {
                "name": name,
                "size": size,
                "combat": combat,
                "hp": hp,
                "stats": stats,
                "max_hit": 0,
                "attack_speed": 0,
                "slayer_level": 1,
                "poison_immune": False,
                "venom_immune": False,
            }
            if version >= 2:
                aggr, max_hit, atk_spd, aggro_r, slayer, atk, weak, immu = struct.unpack(
                    "<BHBBHBBB", read_exact(f, 10)
                )
                rec.update({
                    "max_hit": max_hit,
                    "attack_speed": atk_spd,
                    "slayer_level": slayer,
                    "poison_immune": bool(immu & 1),
                    "venom_immune": bool(immu & 2),
                })
            if version >= 3:
                model_count = struct.unpack("<B", read_exact(f, 1))[0]
                read_exact(f, model_count * 4)
            defs[npc_id] = rec
    return defs


def scalar_int(v) -> int | None:
    if isinstance(v, int):
        return v
    if isinstance(v, str):
        m = re.search(r"-?\d+", v.replace(",", ""))
        return int(m.group(0)) if m else None
    return None


def max_hit_int(v) -> int | None:
    if v is None:
        return None
    vals = v if isinstance(v, list) else [v]
    best = 0
    found = False
    for item in vals:
        n = scalar_int(item)
        if n is not None:
            found = True
            best = max(best, n)
    return best if found else None


def attack_speed_int(v) -> int | None:
    n = scalar_int(v)
    return n if n is not None and n >= 0 else None


def immune(v) -> bool | None:
    if not isinstance(v, str):
        return None
    text = v.lower()
    if "not immune" in text:
        return False
    if "immune" in text:
        return True
    return None


def load_wiki_monsters() -> dict[int, list[dict]]:
    out: dict[int, list[dict]] = {}
    for path in WIKI_CACHE.glob("infobox_monster_*.json"):
        data = json.loads(path.read_text())
        for row in data.get("bucket", []):
            ids = row.get("id") or []
            if isinstance(ids, str):
                ids = [ids]
            for raw in ids:
                npc_id = scalar_int(raw)
                if npc_id is None:
                    continue
                out.setdefault(npc_id, []).append({
                    "name": row.get("name") or row.get("page_name") or "",
                    "combat": scalar_int(row.get("combat_level")),
                    "hp": scalar_int(row.get("hitpoints")),
                    "size": scalar_int(row.get("size")),
                    "stats": (
                        scalar_int(row.get("attack_level")),
                        scalar_int(row.get("defence_level")),
                        scalar_int(row.get("strength_level")),
                        scalar_int(row.get("hitpoints")),
                        scalar_int(row.get("ranged_level")),
                        scalar_int(row.get("magic_level")),
                    ),
                    "max_hit": max_hit_int(row.get("max_hit")),
                    "attack_speed": attack_speed_int(row.get("attack_speed")),
                    "slayer_level": scalar_int(row.get("slayer_level")),
                    "poison_immune": immune(row.get("poison_immune")),
                    "venom_immune": immune(row.get("venom_immune")),
                })
    return out


def wiki_values(rows: list[dict], field: str) -> set:
    return {row[field] for row in rows if row[field] is not None}


def wiki_stat_values(rows: list[dict], idx: int) -> set[int]:
    return {row["stats"][idx] for row in rows if row["stats"][idx] is not None}


def read_varbits(path: Path) -> set[int]:
    ids: set[int] = set()
    with path.open("rb") as f:
        magic, version, count = struct.unpack("<III", read_exact(f, 12))
        if magic != VBIT_MAGIC:
            raise ValueError("bad VBIT magic")
        for _ in range(count):
            idx, name_len = struct.unpack("<HB", read_exact(f, 3))
            read_exact(f, name_len)
            if version >= 2:
                read_exact(f, 4)
            ids.add(idx)
    return ids


def read_varps(path: Path) -> set[int]:
    ids: set[int] = set()
    if not path.is_file():
        return ids
    with path.open("rb") as f:
        magic, _version, count = struct.unpack("<III", read_exact(f, 12))
        if magic != VARP_MAGIC:
            raise ValueError("bad VARP magic")
        for _ in range(count):
            idx, _varp_type, name_len = struct.unpack("<HHB", read_exact(f, 5))
            read_exact(f, name_len)
            ids.add(idx)
    return ids


def npc_morph_state_ids() -> tuple[set[int], set[int], int]:
    varbits: set[int] = set()
    varps: set[int] = set()
    rows = 0
    for p in (DATA_OSRS / "npcids").glob("npcid=*.json"):
        for row in json.loads(p.read_text()):
            rows += 1
            vb = int(row.get("varbitId", -1))
            vp = int(row.get("varpId", -1))
            if vb >= 0:
                varbits.add(vb)
            if vp >= 0:
                varps.add(vp)
    return varbits, varps, rows


def main() -> int:
    defs = read_ndef(ROOT / "data/defs/npc_defs.bin")
    wiki = load_wiki_monsters()
    wiki_ids = set(wiki)
    def_ids = set(defs)

    mismatches: list[str] = []
    fields = ["combat", "hp", "size", "max_hit", "attack_speed", "slayer_level"]
    stat_names = ["attack", "defence", "strength", "hitpoints", "ranged", "magic"]
    for npc_id in sorted(wiki_ids & def_ids):
        d = defs[npc_id]
        rows = wiki[npc_id]
        for field in fields:
            vals = wiki_values(rows, field)
            if vals and d[field] not in vals:
                known = "/".join(str(v) for v in sorted(vals))
                mismatches.append(f"{npc_id} {d['name']}: {field} ndef={d[field]} wiki={known}")
        for idx, name in enumerate(stat_names):
            vals = wiki_stat_values(rows, idx)
            if vals and d["stats"][idx] not in vals:
                known = "/".join(str(v) for v in sorted(vals))
                mismatches.append(
                    f"{npc_id} {d['name']}: {name} ndef={d['stats'][idx]} wiki={known}"
                )
        for field in ["poison_immune", "venom_immune"]:
            vals = wiki_values(rows, field)
            if vals and d[field] not in vals:
                known = "/".join(str(v) for v in sorted(vals))
                mismatches.append(f"{npc_id} {d['name']}: {field} ndef={d[field]} wiki={known}")

    ndef_combat = {i for i, d in defs.items() if d["combat"] > 0}
    wiki_missing_from_ndef = sorted(wiki_ids - def_ids)
    ndef_combat_missing_wiki = sorted(ndef_combat - wiki_ids)
    npc_varbits, npc_varps, npclist_rows = npc_morph_state_ids()
    exported_varbits = read_varbits(ROOT / "data/defs/varbits.bin")
    exported_varps = read_varps(ROOT / "data/defs/varps.bin")
    missing_varbits = sorted(npc_varbits - exported_varbits)
    missing_varps = sorted(npc_varps - exported_varps)

    lines = [
        "NPC semantic audit",
        "",
        f"npc_defs rows: {len(defs)}",
        f"wiki monster IDs: {len(wiki_ids)}",
        f"wiki monster IDs missing npc_defs: {len(wiki_missing_from_ndef)}",
        f"combat npc_defs IDs missing wiki monster infobox: {len(ndef_combat_missing_wiki)}",
        f"wiki-backed field mismatches: {len(mismatches)}",
        "",
        "Morph state hooks",
        f"data_osrs NPCList rows: {npclist_rows}",
        f"unique NPC morph varbit IDs: {len(npc_varbits)}",
        f"unique NPC morph varp IDs: {len(npc_varps)}",
        f"NPC morph varbits missing data/defs/varbits.bin: {len(missing_varbits)}",
        f"NPC morph varps missing data/defs/varps.bin: {len(missing_varps)}",
        "",
        "Status",
        "READY: broad NPC ID/export coverage",
        "READY_WITH_ACCEPTED_SIMPLIFICATIONS: combat fields are compact NDEF fields, not full wiki bonuses yet",
    ]
    if missing_varps:
        lines.append("BLOCKS_PARITY: varps.bin misses NPC morph varps from data_osrs")
    else:
        lines.append("READY: NPC morph varp IDs are exported")
    if missing_varbits:
        lines.append("BLOCKS_PARITY: varbits.bin misses NPC morph varbits from data_osrs")
    else:
        lines.append("READY: NPC morph varbit IDs are exported")
    lines.append("BLOCKS_PARITY: NPC morph state still needs runtime transform evaluation")
    if mismatches:
        lines += ["", "Sample wiki-backed field mismatches:"]
        lines.extend(f"  {m}" for m in mismatches[:40])
    if wiki_missing_from_ndef:
        lines += ["", "Sample wiki monster IDs missing npc_defs:"]
        lines.extend(f"  {i}: {wiki[i][0]['name']}" for i in wiki_missing_from_ndef[:40])
    if ndef_combat_missing_wiki:
        lines += ["", "Sample combat npc_defs IDs missing wiki monster infobox:"]
        lines.extend(f"  {i}: {defs[i]['name']}" for i in ndef_combat_missing_wiki[:40])
    if missing_varbits:
        lines += ["", "Sample missing NPC morph varbits:"]
        lines.extend(f"  {i}" for i in missing_varbits[:60])
    if missing_varps:
        lines += ["", "Sample missing NPC morph varps:"]
        lines.extend(f"  {i}" for i in missing_varps[:60])

    out = ROOT / "tools/reports/npc_semantics.txt"
    out.write_text("\n".join(lines) + "\n")
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
