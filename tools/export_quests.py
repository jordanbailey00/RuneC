#!/usr/bin/env python3
"""Emit data/defs/quests.bin from the `quest` Bucket cache.

Skill reqs parsed from wiki markup pattern: data-skill="X" data-level="N".

Binary format — 'QEST' magic:
  magic u32 | version u32 | count u32
  per quest:
    name_len u8 + name[]
    difficulty u8    (0=unknown, 1=Novice, 2=Intermediate, 3=Experienced,
                      4=Master, 5=Grandmaster, 6=Special)
    length u8        (0=unknown, 1=Very Short, 2=Short, 3=Medium,
                      4=Long, 5=Very Long)
    skill_reqs_n u8 + (skill_id u8, level u8)[skill_reqs_n]
"""
from __future__ import annotations

import json
import re
import struct
import sys
from pathlib import Path

CACHE = Path("/home/joe/projects/RuneC_copy/tools/wiki_cache")
OUT = Path("/home/joe/projects/RuneC_copy/data/defs/quests.bin")

QEST_MAGIC = 0x54534551
QEST_VERSION = 1

DIFFICULTY = {"novice": 1, "intermediate": 2, "experienced": 3,
              "master": 4, "grandmaster": 5, "special": 6}
LENGTH = {"very short": 1, "short": 2, "medium": 3, "long": 4,
          "very long": 5}

SKILL_ID = {
    "attack": 0, "defence": 1, "strength": 2, "hitpoints": 3,
    "ranged": 4, "prayer": 5, "magic": 6, "cooking": 7,
    "woodcutting": 8, "fletching": 9, "fishing": 10, "firemaking": 11,
    "crafting": 12, "smithing": 13, "mining": 14, "herblore": 15,
    "agility": 16, "thieving": 17, "slayer": 18, "farming": 19,
    "runecraft": 20, "hunter": 21, "construction": 22,
}
SKILL_RE = re.compile(r'data-skill="([^"]+)"\s+data-level="(\d+)"',
                      re.IGNORECASE)


def load_rows() -> list[dict]:
    rows = []
    for p in sorted(CACHE.glob("quest_*.json")):
        d = json.loads(p.read_text())
        rows.extend(d.get("bucket", []))
    return rows


def parse_skill_reqs(markup: str) -> list[tuple[int, int]]:
    seen: dict[int, int] = {}
    for m in SKILL_RE.finditer(markup or ""):
        sid = SKILL_ID.get(m.group(1).strip().lower())
        if sid is None:
            continue
        lvl = int(m.group(2))
        if sid in seen and seen[sid] >= lvl:
            continue
        seen[sid] = lvl
    return sorted(seen.items())


def quest_name(row: dict) -> str:
    blob = row.get("json")
    if blob:
        try:
            d = json.loads(blob)
            if d.get("name"):
                return d["name"]
        except (json.JSONDecodeError, TypeError):
            pass
    return row.get("page_name") or ""


def main():
    rows = load_rows()
    by_name: dict[str, dict] = {}
    for r in rows:
        n = quest_name(r)
        if not n:
            continue
        prior = by_name.get(n)
        if prior is None or sum(1 for v in r.values() if v) > sum(1 for v in prior.values() if v):
            by_name[n] = r
    print(f"loaded {len(rows)} rows → {len(by_name)} unique quests",
          file=sys.stderr)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", QEST_MAGIC, QEST_VERSION, len(by_name)))
        for name in sorted(by_name):
            r = by_name[name]
            diff = DIFFICULTY.get((r.get("official_difficulty") or "").strip().lower(), 0)
            length = LENGTH.get((r.get("official_length") or "").strip().lower(), 0)
            reqs = parse_skill_reqs(r.get("requirements") or "")
            nb = name.encode("latin-1", errors="replace")[:255]
            f.write(struct.pack("<B", len(nb))); f.write(nb)
            f.write(struct.pack("<BB", diff, length))
            f.write(struct.pack("<B", len(reqs)))
            for sid, lvl in reqs:
                f.write(struct.pack("<BB", sid, lvl))
    print(f"  → {OUT} ({OUT.stat().st_size} bytes)", file=sys.stderr)


if __name__ == "__main__":
    main()
