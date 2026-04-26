#!/usr/bin/env python3
"""Emit cache-backed player varp definitions used by state transforms."""

from __future__ import annotations

import struct
import sys
import argparse
from pathlib import Path

REF = Path("/home/joe/projects/runescape-rl-reference")
SCRIPTS = REF / "valo_envs/ocean/osrs/scripts"
sys.path.insert(0, str(SCRIPTS))

from dat2_cache_reader import Dat2CacheReader  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
CACHE_DIR = REF / "current_fightcaves_demo/data/cache"
OUT = ROOT / "data/defs/varps.bin"

VARP_MAGIC = 0x50524156
VARP_VERSION = 1
CONFIG_INDEX = 2
VARP_GROUP = 16


def decode_varp(data: bytes) -> int:
    pos = 0
    varp_type = 0
    while pos < len(data):
        op = data[pos]
        pos += 1
        if op == 0:
            break
        if op == 5 and pos + 2 <= len(data):
            varp_type = (data[pos] << 8) | data[pos + 1]
            pos += 2
        else:
            break
    return varp_type


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", type=Path, default=CACHE_DIR)
    args = ap.parse_args()
    reader = Dat2CacheReader(args.cache)
    files = reader.read_group(CONFIG_INDEX, VARP_GROUP)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", VARP_MAGIC, VARP_VERSION, len(files)))
        for idx in sorted(files):
            varp_type = decode_varp(files[idx])
            name = f"VARP_{idx}".encode("ascii")
            f.write(struct.pack("<HHB", idx & 0xFFFF, varp_type & 0xFFFF, len(name)))
            f.write(name)
    print(f"loaded {len(files)} cache varps", file=sys.stderr)
    print(f"  -> {OUT} ({OUT.stat().st_size} bytes)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
