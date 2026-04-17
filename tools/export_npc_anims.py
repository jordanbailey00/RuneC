#!/usr/bin/env python3
"""Export animations referenced by our NPC definitions.

Scans data/defs/npc_defs.bin, collects every non-(-1) value of the five anim
slots (stand / walk / run / attack / death), merges them with the reference
exporter's NEEDED_ANIMATIONS set, and invokes the exporter's main(). Writes
a single .anims file containing every framebase + frame + sequence needed
to animate all NPCs in the current defs file.
"""
import argparse, struct, sys
from pathlib import Path

REF = Path("/home/joe/projects/runescape-rl-reference/valo_envs/ocean/osrs/scripts")
sys.path.insert(0, str(REF))

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

    if args.include_player:
        ea.NEEDED_ANIMATIONS = set(ea.NEEDED_ANIMATIONS) | npc_ids
    else:
        ea.NEEDED_ANIMATIONS = set(npc_ids)
    print(f"total NEEDED_ANIMATIONS now: {len(ea.NEEDED_ANIMATIONS)}")

    # Hand off to the reference exporter via sys.argv so it takes our paths.
    sys.argv = ["export_animations.py",
                "--modern-cache", str(args.cache),
                "--output", str(args.output)]
    ea.main()


if __name__ == "__main__":
    main()
