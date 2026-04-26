#!/usr/bin/env python3
"""Emit data/defs/varbits.bin from Wiki names plus cache varbit defs.

Each row: `{page_name, name, index}`. Many rows share a page_name
(wiki groups varbits by topic). `name` is the SCREAMING_SNAKE
semantic identifier; `index` is the numeric varbit ID used by the
client cache. We emit a compact name→index map so rc-core can look
up varbits by symbolic name rather than magic number.

Binary format — 'VBIT' magic, version 2:
  magic u32 | version u32 | count u32
  per row: index u16 | name_len u8 | name[name_len] |
           base_varp u16 | lsb u8 | msb u8

All names are ASCII uppercase + digits + underscores in practice;
stored as raw bytes, no terminator.
"""
from __future__ import annotations

import json
import struct
import sys
import argparse
from pathlib import Path

REF = Path("/home/joe/projects/runescape-rl-reference")
SCRIPTS = REF / "valo_envs/ocean/osrs/scripts"
sys.path.insert(0, str(SCRIPTS))

from dat2_cache_reader import Dat2CacheReader  # noqa: E402

CACHE = Path("/home/joe/projects/RuneC_copy/tools/wiki_cache")
OUT = Path("/home/joe/projects/RuneC_copy/data/defs/varbits.bin")
CACHE_DIR = REF / "current_fightcaves_demo/data/cache"

VBIT_MAGIC = 0x54494256  # 'VBIT' little-endian
VBIT_VERSION = 2
CONFIG_INDEX = 2
VARBIT_GROUPS = (14, 32)  # b237/newer caches use 14; local ref cache uses 32.


def load_rows() -> list[dict]:
    rows = []
    for p in sorted(CACHE.glob("varbit_*.json")):
        d = json.loads(p.read_text())
        rows.extend(d.get("bucket", []))
    return rows


def wiki_names(rows: list[dict]) -> dict[int, str]:
    out: dict[int, str] = {}
    for r in rows:
        idx = r.get("index")
        name = (r.get("name") or "").strip()
        try:
            idx = int(idx)
        except (TypeError, ValueError):
            continue
        if name:
            out[idx] = name
    return out


def decode_varbit(data: bytes) -> tuple[int, int, int] | None:
    pos = 0
    base = lsb = msb = 0
    while pos < len(data):
        op = data[pos]
        pos += 1
        if op == 0:
            return base, lsb, msb
        if op == 1 and pos + 4 <= len(data):
            base = (data[pos] << 8) | data[pos + 1]
            lsb = data[pos + 2]
            msb = data[pos + 3]
            pos += 4
        else:
            return None
    return None


def cache_varbits(cache_dir: Path) -> dict[int, tuple[int, int, int]]:
    reader = Dat2CacheReader(cache_dir)
    out: dict[int, tuple[int, int, int]] = {}
    for group_id in VARBIT_GROUPS:
        try:
            files = reader.read_group(CONFIG_INDEX, group_id)
        except Exception:
            continue
        decoded = 0
        for idx, data in files.items():
            rec = decode_varbit(data)
            if rec is not None:
                out[int(idx)] = rec
                decoded += 1
        if decoded:
            break
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", type=Path, default=CACHE_DIR)
    args = ap.parse_args()
    rows = load_rows()
    names = wiki_names(rows)
    cache_defs = cache_varbits(args.cache)
    by_index: dict[int, tuple[str, int, int, int]] = {}
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
        if idx in names and names[idx] != name:
            collisions += 1
        if idx not in cache_defs:
            by_index[idx] = (name, 0, 0, 0)

    for idx, (base, lsb, msb) in cache_defs.items():
        by_index[idx] = (names.get(idx, f"VARBIT_{idx}"), base, lsb, msb)

    print(f"loaded {len(rows)} wiki rows + {len(cache_defs)} cache defs "
          f"→ {len(by_index)} unique varbits "
          f"(name_collisions={collisions}, skipped={skipped})", file=sys.stderr)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", VBIT_MAGIC, VBIT_VERSION, len(by_index)))
        for idx in sorted(by_index):
            raw_name, base, lsb, msb = by_index[idx]
            name = raw_name.encode("ascii", errors="replace")[:255]
            f.write(struct.pack("<HB", idx & 0xFFFF, len(name)))
            f.write(name)
            f.write(struct.pack("<HBB", base & 0xFFFF, lsb & 0xFF, msb & 0xFF))
    print(f"  → {OUT} ({OUT.stat().st_size} bytes)", file=sys.stderr)


if __name__ == "__main__":
    main()
