#!/usr/bin/env python3
"""Emit data/defs/music.bin from the `music` Bucket cache.

Each row: `{title, number, cacheid, duration, composer,
is_members_only, is_jingle, is_event, release_date}`. Track `cacheid`
is the cache music-group ID used by rc-core's music loader.

Binary format — 'MUSC' magic:
  magic u32 | version u32 | count u32
  per track:
    cache_id u32
    number u16
    flags u8  (bit0=members, bit1=jingle, bit2=event)
    duration_sec u16
    title_len u8 + title[]
    composer_len u8 + composer[]   (first composer only)
"""
from __future__ import annotations

import json
import re
import struct
import sys
from pathlib import Path

CACHE = Path("/home/joe/projects/RuneC_copy/tools/wiki_cache")
OUT = Path("/home/joe/projects/RuneC_copy/data/defs/music.bin")

MUSC_MAGIC = 0x4353554D
MUSC_VERSION = 1


def load_rows() -> list[dict]:
    rows = []
    for p in sorted(CACHE.glob("music_*.json")):
        d = json.loads(p.read_text())
        rows.extend(d.get("bucket", []))
    return rows


def parse_duration(s: str) -> int:
    if not s:
        return 0
    m = re.match(r"^\s*(\d+):(\d+)\s*$", s)
    if not m:
        m2 = re.match(r"^\s*(\d+):(\d+):(\d+)\s*$", s)
        if m2:
            return int(m2.group(1)) * 3600 + int(m2.group(2)) * 60 + int(m2.group(3))
        try:
            return int(s)
        except ValueError:
            return 0
    return int(m.group(1)) * 60 + int(m.group(2))


def pack_short(b: str) -> bytes:
    return b.encode("latin-1", errors="replace")[:255]


def main():
    rows = load_rows()
    by_cid: dict[int, dict] = {}
    skipped = 0
    for r in rows:
        cid = r.get("cacheid")
        if cid is None:
            skipped += 1
            continue
        try:
            cid = int(cid)
        except (TypeError, ValueError):
            skipped += 1
            continue
        if cid in by_cid and not r.get("title"):
            continue
        by_cid[cid] = r
    print(f"loaded {len(rows)} rows → {len(by_cid)} unique tracks "
          f"(skipped={skipped})", file=sys.stderr)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", MUSC_MAGIC, MUSC_VERSION, len(by_cid)))
        for cid in sorted(by_cid):
            r = by_cid[cid]
            title = pack_short(r.get("title") or "")
            composer_list = r.get("composer") or []
            if not isinstance(composer_list, list):
                composer_list = [composer_list]
            composer = pack_short(composer_list[0] if composer_list else "")
            number = int(r.get("number") or 0) & 0xFFFF
            flags = (1 if r.get("is_members_only") else 0) \
                  | (2 if r.get("is_jingle") else 0) \
                  | (4 if r.get("is_event") else 0)
            duration = parse_duration(r.get("duration") or "")
            f.write(struct.pack("<IHBH",
                                cid & 0xFFFFFFFF, number, flags & 0xFF,
                                duration & 0xFFFF))
            f.write(struct.pack("<B", len(title))); f.write(title)
            f.write(struct.pack("<B", len(composer))); f.write(composer)
    print(f"  → {OUT} ({OUT.stat().st_size} bytes)", file=sys.stderr)


if __name__ == "__main__":
    main()
