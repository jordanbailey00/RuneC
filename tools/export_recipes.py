#!/usr/bin/env python3
"""Emit data/defs/recipes.bin from the `recipe` Bucket cache.

Keeps only rows where `source_template == "recipe"` with ≥1 skill
requirement. Item names → cache IDs via `infobox_item`.

Binary format — 'RCIP' magic:
  magic u32 | version u32 | count u32
  per recipe:
    name_len u8 + name[]
    skill_reqs_n u8 + (skill_id u8, level u8, xp_q1 u16)[]
    inputs_n u8 + (item_id u32, qty u16)[]
    tools_n u8 + (item_id u32)[]
    facility_len u8 + facility[]
    output_item u32 | output_qty u16 | ticks u16
    flags u8   (bit0=members)
"""
from __future__ import annotations

import json
import re
import struct
import sys
from pathlib import Path

CACHE = Path("/home/joe/projects/RuneC_copy/tools/wiki_cache")
OUT = Path("/home/joe/projects/RuneC_copy/data/defs/recipes.bin")

RCIP_MAGIC = 0x50494352
RCIP_VERSION = 1

SKILL_ID = {
    "attack": 0, "defence": 1, "strength": 2, "hitpoints": 3,
    "ranged": 4, "prayer": 5, "magic": 6, "cooking": 7,
    "woodcutting": 8, "fletching": 9, "fishing": 10, "firemaking": 11,
    "crafting": 12, "smithing": 13, "mining": 14, "herblore": 15,
    "agility": 16, "thieving": 17, "slayer": 18, "farming": 19,
    "runecraft": 20, "hunter": 21, "construction": 22,
}


def load_bucket(bucket: str):
    rows = []
    for p in sorted(CACHE.glob(f"{bucket}_*.json")):
        rows.extend(json.loads(p.read_text()).get("bucket", []))
    return rows


def item_map() -> dict[str, int]:
    out: dict[str, int] = {}
    for r in load_bucket("infobox_item"):
        name = (r.get("item_name") or r.get("page_name") or "").strip()
        if not name:
            continue
        ids = r.get("item_id") or []
        if not isinstance(ids, list):
            ids = [ids]
        for iid in ids:
            try:
                iid = int(iid)
            except (TypeError, ValueError):
                continue
            k = name.lower()
            if k not in out or iid < out[k]:
                out[k] = iid
    return out


def parse_int(s, default=1) -> int:
    if s is None:
        return default
    s = str(s).strip()
    if not s:
        return default
    m = re.search(r"\d+", s)
    return int(m.group(0)) if m else default


def parse_xp_q1(s) -> int:
    if s is None:
        return 0
    s = str(s).strip()
    if not s or s.lower() == "varies":
        return 0
    try:
        return max(0, min(65535, round(float(s) * 10)))
    except ValueError:
        return 0


def pack_short(s: str, maxlen: int = 255) -> bytes:
    return (s or "").encode("latin-1", errors="replace")[:maxlen]


def main():
    items = item_map()
    recipes = []
    skipped_tpl = 0
    skipped_noskill = 0

    for r in load_bucket("recipe"):
        if r.get("source_template") != "recipe":
            skipped_tpl += 1
            continue
        try:
            pj = json.loads(r.get("production_json") or "{}")
        except (json.JSONDecodeError, TypeError):
            continue
        if not pj.get("skills"):
            skipped_noskill += 1
            continue

        name = (pj.get("name") or r.get("page_name")
                or (pj.get("output", {}) or {}).get("name") or "").strip()
        if not name:
            continue

        skill_reqs = []
        for s in pj.get("skills", []):
            sid = SKILL_ID.get((s.get("name") or "").strip().lower())
            if sid is None:
                continue
            lvl = parse_int(s.get("level"), default=1)
            xp_q1 = parse_xp_q1(s.get("experience"))
            skill_reqs.append((sid, min(99, max(1, lvl)), xp_q1))

        inputs = []
        for m in pj.get("materials") or []:
            mname = (m.get("name") or "").strip()
            if not mname:
                continue
            iid = items.get(mname.split("#", 1)[0].lower())
            if iid is None:
                continue
            inputs.append((iid, parse_int(m.get("quantity"), default=1)))

        tools = []
        for t in r.get("uses_tool") or []:
            t = (t or "").strip()
            if not t:
                continue
            iid = items.get(t.split("#", 1)[0].lower())
            if iid is not None:
                tools.append(iid)

        facility = ""
        for f_ in r.get("uses_facility") or []:
            if f_:
                facility = f_.strip(); break
        if not facility:
            facility = (pj.get("facilities") or "").strip()

        out_item = 0; out_qty = 0
        ob = pj.get("output") or {}
        if isinstance(ob, dict):
            oname = (ob.get("name") or "").strip()
            if oname:
                iid = items.get(oname.split("#", 1)[0].lower())
                if iid is not None:
                    out_item = iid
                    out_qty = parse_int(ob.get("quantity"), default=1)

        ticks = parse_int(pj.get("ticks"), default=0)
        recipes.append({
            "name": name,
            "skill_reqs": skill_reqs[:255],
            "inputs": inputs[:255],
            "tools": tools[:255],
            "facility": facility,
            "out_item": out_item,
            "out_qty": out_qty,
            "ticks": min(65535, max(0, ticks)),
            "members": 1 if pj.get("members") else 0,
        })

    print(f"kept {len(recipes)} recipes (skipped {skipped_tpl} non-recipe, "
          f"{skipped_noskill} missing skills)", file=sys.stderr)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", RCIP_MAGIC, RCIP_VERSION, len(recipes)))
        for r in recipes:
            nb = pack_short(r["name"]); fb = pack_short(r["facility"])
            f.write(struct.pack("<B", len(nb))); f.write(nb)
            f.write(struct.pack("<B", len(r["skill_reqs"])))
            for sid, lvl, xp in r["skill_reqs"]:
                f.write(struct.pack("<BBH", sid, lvl, xp))
            f.write(struct.pack("<B", len(r["inputs"])))
            for iid, qty in r["inputs"]:
                f.write(struct.pack("<IH", iid, min(65535, qty)))
            f.write(struct.pack("<B", len(r["tools"])))
            for iid in r["tools"]:
                f.write(struct.pack("<I", iid))
            f.write(struct.pack("<B", len(fb))); f.write(fb)
            f.write(struct.pack("<IHH", r["out_item"], r["out_qty"], r["ticks"]))
            f.write(struct.pack("<B", r["members"]))
    print(f"  → {OUT} ({OUT.stat().st_size} bytes)", file=sys.stderr)


if __name__ == "__main__":
    main()
