#!/usr/bin/env python3
"""Export animations referenced by our NPC definitions."""
import argparse, struct, sys
from pathlib import Path

PIPELINE = Path(__file__).resolve().parent / "cache_pipeline"
sys.path.insert(0, str(PIPELINE))

import export_animations as ea


def scan_npc_anim_ids(ndef_path):
    ids = set()
    with open(ndef_path, "rb") as f:
        magic, ver, count = struct.unpack("<III", f.read(12))
        assert magic == 0x4E444546, f"bad NDEF magic 0x{magic:08X}"
        for _ in range(count):
            f.read(4)                     # npc id
            f.read(1)                     # size
            f.read(2)                     # combat level
            f.read(2)                     # hp
            f.read(12)                    # stats[6]
            anims = struct.unpack("<5i", f.read(20))
            nl, = struct.unpack("<B", f.read(1))
            f.read(nl)
            for a in anims:
                if a >= 0:
                    ids.add(a)
            if ver >= 2:
                f.read(10)               # combat metadata
            if ver >= 3:
                mc, = struct.unpack("<B", f.read(1))
                f.read(4 * mc)           # model ids
            if ver >= 4:
                for _ in range(5):
                    ol, = struct.unpack("<B", f.read(1))
                    f.read(ol)           # option text
    return ids


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--cache", type=Path, required=True)
    p.add_argument("--npc-defs", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--include-player", action="store_true",
                   help="also include the reference exporter's player/combat anim set")
    args = p.parse_args()

    npc_ids = scan_npc_anim_ids(args.npc_defs)
    print(f"NPC defs reference {len(npc_ids)} unique anim IDs")

    needed = (set(ea.NEEDED_ANIMATIONS) | npc_ids) if args.include_player else npc_ids
    print(f"total animation IDs to export: {len(needed)}")
    ea.export_animations_from_modern_cache(args.cache, args.output, set(needed))


if __name__ == "__main__":
    main()
