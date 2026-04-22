#!/usr/bin/env python3
"""Emit data/defs/varbits.bin from the `varbit` Bucket cache.

Each row: `{page_name, name, index}`. Many rows share a page_name
(wiki groups varbits by topic). `name` is the SCREAMING_SNAKE
semantic identifier; `index` is the numeric varbit ID used by the
client cache. We emit a compact name→index map so rc-core can look
up varbits by symbolic name rather than magic number.

Binary format — 'VBIT' magic:
  magic u32 | version u32 | count u32
  per row: index u16 | name_len u8 | name[name_len]

All names are ASCII uppercase + digits + underscores in practice;
stored as raw bytes, no terminator.
"""
from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

CACHE = Path("/home/joe/projects/RuneC_copy/tools/wiki_cache")
OUT = Path("/home/joe/projects/RuneC_copy/data/defs/varbits.bin")

VBIT_MAGIC = 0x54494256  # 'VBIT' little-endian
VBIT_VERSION = 1


def load_rows() -> list[dict]:
    rows = []
    for p in sorted(CACHE.glob("varbit_*.json")):
        d = json.loads(p.read_text())
        rows.extend(d.get("bucket", []))
    return rows


def main():
    rows = load_rows()
    # Dedupe: some varbits may appear in multiple cache pages if offsets overlap.
    by_index: dict[int, str] = {}
    collisions = 0
    skipped = 0
    for r in rows:
        idx = r.get("index")
        name = (r.get("name") or "").strip()
        if idx is None or not name:
            skipped += 1
            continue
        try:
            idx = int(idx)
        except (TypeError, ValueError):
            skipped += 1
            continue
        if idx in by_index and by_index[idx] != name:
            collisions += 1
        by_index[idx] = name

    print(f"loaded {len(rows)} rows → {len(by_index)} unique varbits "
          f"(collisions={collisions}, skipped={skipped})", file=sys.stderr)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", VBIT_MAGIC, VBIT_VERSION, len(by_index)))
        for idx in sorted(by_index):
            name = by_index[idx].encode("ascii", errors="replace")[:255]
            f.write(struct.pack("<HB", idx & 0xFFFF, len(name)))
            f.write(name)
    print(f"  → {OUT} ({OUT.stat().st_size} bytes)", file=sys.stderr)


if __name__ == "__main__":
    main()
