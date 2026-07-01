#!/usr/bin/env python3
"""Emit data/defs/spells.bin and data/defs/teleports.bin.

Runtime schemas: `schema/defs/spells.schema.toml` and
`schema/defs/teleports.schema.toml`.
"""
from __future__ import annotations

import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

CACHE = ROOT / "tools/wiki_cache"
OUT_S = ROOT / "data/defs/spells.bin"
OUT_T = ROOT / "data/defs/teleports.bin"

SPEL_MAGIC = 0x4C455053
TELE_MAGIC = 0x454C4554
VERSION = 2

REPORT = ROOT / "tools/reports/spells.txt"

SPELLBOOK = {"normal": 0, "ancient": 1, "lunar": 2, "arceuus": 3, "all": 4}
SPELL_TYPE = {"combat": 1, "teleport": 2, "utility": 3,
              "skilling": 4, "curse": 5, "charging": 6,
              "summoning": 7, "enchantment": 3, "alchemy": 3}

EFFECT_DAMAGE = 1 << 0
EFFECT_FREEZE = 1 << 1
EFFECT_HEAL = 1 << 2
EFFECT_POISON = 1 << 3
EFFECT_DRAIN = 1 << 4
EFFECT_TELEPORT = 1 << 5

_RUNE_RE = re.compile(r"<sup>(\d+)</sup>\s*\[\[File:([^\]|]+?)\.png",
                      re.IGNORECASE)


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


def parse_runes(cost: str, items: dict[str, int]):
    out = []
    for m in _RUNE_RE.finditer(cost or ""):
        qty = int(m.group(1))
        rune = m.group(2).strip()
        iid = items.get(rune.lower())
        if iid is not None:
            out.append((iid, min(255, max(1, qty))))
    return out


def slugify(s: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", s.lower()).strip("_")


def combat_effects() -> dict[str, tuple[int, int]]:
    return {}


def parse_level(s) -> int:
    if s is None:
        return 1
    m = re.search(r"\d+", str(s).strip())
    return int(m.group(0)) if m else 1


def parse_xp_q1(s) -> int:
    if s is None:
        return 0
    s = str(s).strip()
    if not s or re.search(r"[a-zA-Z*]", s):
        return 0
    try:
        return max(0, min(65535, round(float(s) * 10)))
    except ValueError:
        return 0


def pack_short(s: str) -> bytes:
    return (s or "").encode("latin-1", errors="replace")[:255]


def write_bin(path: Path, magic: int, spells: list[dict]):
    with path.open("wb") as f:
        f.write(struct.pack("<III", magic, VERSION, len(spells)))
        for s in spells:
            nb = pack_short(s["name"])
            f.write(struct.pack("<B", len(nb))); f.write(nb)
            f.write(struct.pack("<BBBBHB",
                                s["book"], s["type"], s["level"],
                                s["slv"], s["xp_q1"], s["members"]))
            f.write(struct.pack("<HH", s["max_hit"], s["effect_flags"]))
            f.write(struct.pack("<B", len(s["runes"])))
            for iid, qty in s["runes"]:
                f.write(struct.pack("<IB", iid, qty))


def main():
    items = item_map()
    effects = combat_effects()
    spells = []
    teleports = []

    for r in load_bucket("infobox_spell"):
        name = (r.get("page_name") or "").strip()
        if not name:
            continue
        blob = r.get("json")
        if not blob:
            continue
        try:
            j = json.loads(blob)
        except (json.JSONDecodeError, TypeError):
            continue

        book = SPELLBOOK.get((r.get("spellbook") or j.get("spellbook")
                              or "").strip().lower(), 0)
        stype = SPELL_TYPE.get((j.get("type") or "").strip().lower(), 0)
        level = min(99, max(1, parse_level(j.get("level"))))
        slv_raw = j.get("slayerlevel")
        slv = parse_level(slv_raw)
        if slv == 1 and not str(slv_raw or "").strip():
            slv = 0
        max_hit, effect_flags = effects.get(slugify(name), (0, 0))
        if stype == 2:
            effect_flags |= EFFECT_TELEPORT
        s = {
            "name": name, "book": book, "type": stype,
            "level": level, "slv": min(99, slv),
            "xp_q1": parse_xp_q1(j.get("exp")),
            "members": 1 if r.get("is_members_only") else 0,
            "max_hit": max(0, min(65535, max_hit)),
            "effect_flags": max(0, min(65535, effect_flags)),
            "runes": parse_runes(j.get("cost") or "", items)[:255],
        }
        spells.append(s)
        if stype == 2:
            teleports.append(s)

    print(f"{len(spells)} spells, {len(teleports)} teleports",
          file=sys.stderr)
    OUT_S.parent.mkdir(parents=True, exist_ok=True)
    write_bin(OUT_S, SPEL_MAGIC, spells)
    write_bin(OUT_T, TELE_MAGIC, teleports)
    matched = sum(1 for s in spells if s["max_hit"] > 0)
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(
        "Spellbook export\n\n"
        "status: READY_WITH_ACCEPTED_SIMPLIFICATIONS\n"
        f"spells: {len(spells)}\n"
        f"teleports: {len(teleports)}\n"
        f"combat max-hit matches: {matched}\n"
        "source: OSRS Wiki infobox_spell cache\n"
        "notes:\n"
        "  Spell metadata and rune costs are wiki-derived. Combat max-hit\n"
        "  hints and exact spell effect state machines remain tracked source\n"
        "  gaps until backed by approved OSRS-native sources.\n"
    )
    print(f"  → {OUT_S} ({OUT_S.stat().st_size} bytes)", file=sys.stderr)
    print(f"  → {OUT_T} ({OUT_T.stat().st_size} bytes)", file=sys.stderr)


if __name__ == "__main__":
    main()
