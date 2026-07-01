#!/usr/bin/env python3
"""Emit cache-backed player varp definitions used by state transforms."""

from __future__ import annotations

import struct
import sys
import argparse
from pathlib import Path

from source_paths import CACHE_DIR, require_cache_dir

PIPELINE = Path(__file__).resolve().parent / "cache_pipeline"
sys.path.insert(0, str(PIPELINE))

from rc_cache import CONFIG_VARP, INDEX_CONFIGS, RcCacheStore, decode_varp_definition  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "data/defs/varps.bin"

VARP_MAGIC = 0x50524156
VARP_VERSION = 1


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", type=Path, default=CACHE_DIR)
    args = ap.parse_args()
    store = RcCacheStore(require_cache_dir(args.cache))
    files = store.read_group(INDEX_CONFIGS, CONFIG_VARP)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", VARP_MAGIC, VARP_VERSION, len(files)))
        for idx in sorted(files):
            varp_type = decode_varp_definition(int(idx), files[idx]).varp_type
            name = f"VARP_{idx}".encode("ascii")
            f.write(struct.pack("<HHB", idx & 0xFFFF, varp_type & 0xFFFF, len(name)))
            f.write(name)
    print(f"loaded {len(files)} cache varps", file=sys.stderr)
    print(f"  -> {OUT} ({OUT.stat().st_size} bytes)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
