#!/usr/bin/env python3
"""Patch `data/defs/npc_defs.bin` with wiki-authoritative stats.

The NDEF binary is produced by `export_npcs.py` which uses
`osrsreboxed-db` as its primary NPC-stat source. `osrsreboxed.max_hit`
reports base melee damage and misses breath / special attacks (Green
dragon osrsreboxed=8 vs wiki=50). This tool applies the wiki overlay
post-hoc without requiring a full cache re-export.

Overlaid fields (wiki wins when it has data):
 - max_hit  — wiki lists max damage per style; we take the max.
 - poison_immune / venom_immune  — wiki "Immune" / "Not immune" strings.

NDEF v2 record layout (see export_npcs.py write_ndef):
   fixed prefix (33 bytes) + name_len byte + name + v2 trailer (10 bytes)
   trailer: u8 aggressive, u16 max_hit, u8 attack_speed, u8 aggro_range,
            u16 slayer_level, u8 attack_types, u8 weakness, u8 immunities
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from database_sources import WikiMonsters

NPC_DEFS = Path("/home/joe/projects/RuneC_copy/data/defs/npc_defs.bin")
NDEF_MAGIC = 0x4E444546
NDEF_V2 = 2
# Layout per write_ndef: u32 id + u8 size + i16 cl + u16 hp + 6×u16 stats
# (opcodes 74-79 = attack / strength / defence / magic / ranged +
# one extra slot) + 5×i32 anims.
PREFIX_SIZE = 4 + 1 + 2 + 2 + 12 + 20  # = 41


def main():
    if not NPC_DEFS.exists():
        print(f"not found: {NPC_DEFS}", file=sys.stderr)
        sys.exit(1)
    buf = bytearray(NPC_DEFS.read_bytes())
    magic, version, count = struct.unpack_from("<III", buf, 0)
    if magic != NDEF_MAGIC or version != NDEF_V2:
        print(f"unexpected ndef header: magic={magic:08x} v={version}",
              file=sys.stderr)
        sys.exit(1)

    wiki = WikiMonsters()
    print(f"{count} NPC defs, wiki index has {len(wiki._by_id)} entries",
          file=sys.stderr)

    patched_maxhit = 0
    patched_poison = 0
    patched_venom = 0
    pos = 12   # after header

    for _ in range(count):
        rec_start = pos
        nid = struct.unpack_from("<I", buf, pos)[0]
        pos += PREFIX_SIZE
        name_len = buf[pos]
        pos += 1 + name_len
        trailer_pos = pos
        # Unpack trailer
        aggr, max_hit, atk_sp, aggro_r, slay_lvl, atk_t, weak, immu = \
            struct.unpack_from("<BHBBHBBB", buf, trailer_pos)
        pos = trailer_pos + 10

        wrec = wiki.by_id(nid)
        if wrec is None:
            continue
        # Wiki max_hit overlay
        wmax = WikiMonsters.parse_max_hit(wrec.get("max_hit"))
        new_max = max_hit
        if wmax is not None and wmax != max_hit:
            new_max = min(65535, max(0, wmax))
            patched_maxhit += 1
        # Immunity overlay
        new_immu = immu
        wpoison = WikiMonsters.parse_bool_immune(wrec.get("poison_immune"))
        if wpoison is not None:
            bit = 1
            if wpoison and not (immu & bit):
                new_immu |= bit; patched_poison += 1
            elif not wpoison and (immu & bit):
                new_immu &= ~bit; patched_poison += 1
        wvenom = WikiMonsters.parse_bool_immune(wrec.get("venom_immune"))
        if wvenom is not None:
            bit = 2
            if wvenom and not (immu & bit):
                new_immu |= bit; patched_venom += 1
            elif not wvenom and (immu & bit):
                new_immu &= ~bit; patched_venom += 1

        if new_max != max_hit or new_immu != immu:
            struct.pack_into("<BHBBHBBB", buf, trailer_pos,
                             aggr, new_max, atk_sp, aggro_r, slay_lvl,
                             atk_t, weak, new_immu)

    NPC_DEFS.write_bytes(bytes(buf))
    print(f"patched max_hit: {patched_maxhit}", file=sys.stderr)
    print(f"patched poison_immune: {patched_poison}", file=sys.stderr)
    print(f"patched venom_immune: {patched_venom}", file=sys.stderr)
    print(f"  → {NPC_DEFS} ({NPC_DEFS.stat().st_size} bytes)",
          file=sys.stderr)


if __name__ == "__main__":
    main()
