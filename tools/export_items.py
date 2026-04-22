#!/usr/bin/env python3
"""Export item definitions with equipment bonuses → data/defs/items.bin (IDEF).

Sources:
  - osrsreboxed-db (primary) — per-item metadata, equipment bonuses, weapon
    attack speed + stances.
  - b237 cache (optional, for cross-validation only; osrsreboxed is already
    decoded from a modern cache).

Excluded per ignore.md:
  - No GE price field (single-player, no live market).
  - `cost` (shop base price) + `highalch` + `lowalch` are kept — those are
    static values baked into the cache, not a market.
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from database_sources import OsrsreboxedDB

IDEF_MAGIC = 0x49444546  # "IDEF"
IDEF_VERSION = 1

# Equipment slots — must match rc-core/types.h enum (TBD when combat lands)
SLOT_NAMES = {
    "head": 0, "cape": 1, "neck": 2, "ammo": 3, "weapon": 4, "body": 5,
    "shield": 6, "legs": 7, "hands": 8, "feet": 9, "ring": 10,
    "2h": 4,        # two-handed = weapon slot, flagged two_handed in flags
}

# Skill IDs for level requirements — must match rc-core/skills.c
SKILL_IDS = {
    "attack": 0, "defence": 1, "strength": 2, "hitpoints": 3, "ranged": 4,
    "prayer": 5, "magic": 6, "cooking": 7, "woodcutting": 8, "fletching": 9,
    "fishing": 10, "firemaking": 11, "crafting": 12, "smithing": 13,
    "mining": 14, "herblore": 15, "agility": 16, "thieving": 17,
    "slayer": 18, "farming": 19, "runecraft": 20, "runecrafting": 20,
    "hunter": 21, "construction": 22,
}

WEAPON_TYPES = {
    None: 0,
    "unarmed": 0, "2h_sword": 1, "axe": 2, "banner": 3, "blunt": 4,
    "bludgeon": 5, "bulwark": 6, "chinchompas": 7, "claw": 8, "crossbow": 9,
    "whip": 10, "fixed_device": 11, "gun": 12, "pickaxe": 13, "polearm": 14,
    "polestaff": 15, "powered_staff": 16, "scythe": 17, "slash_sword": 18,
    "spear": 19, "spiked": 20, "stab_sword": 21, "staff": 22, "thrown": 23,
    "two-handed_sword": 24, "bow": 25, "salamander": 26,
    "multi-style": 27, "powered_wand": 28, "bladed_staff": 29,
    "partisan": 30, "atlatl": 31,
}

STANCE_BITS = {  # bitfield
    "accurate": 1 << 0, "aggressive": 1 << 1, "controlled": 1 << 2,
    "defensive": 1 << 3, "rapid": 1 << 4, "longrange": 1 << 5,
    "autocast": 1 << 6, "defensive_autocast": 1 << 7,
}

# Flag bits for the u8 flags byte in IDEF
F_STACKABLE      = 1 << 0
F_TRADEABLE      = 1 << 1
F_MEMBERS        = 1 << 2
F_QUEST_ITEM     = 1 << 3
F_HAS_EQUIPMENT  = 1 << 4
F_HAS_WEAPON     = 1 << 5
F_NOTED          = 1 << 6
F_PLACEHOLDER    = 1 << 7


def int_or(v, default):
    return int(v) if v is not None else default


def pack_equipment(eq: dict) -> bytes:
    """Pack the 13-bonus + slot + up-to-4-reqs equipment block.
    Layout: slot u8, req_count u8, (req_skill u8 + req_level u8)*req_count,
            13 × i16 bonuses in order stab/slash/crush/magic_att/ranged_att/
            def_stab/def_slash/def_crush/def_magic/def_ranged/str/
            ranged_str/magic_dmg/prayer (14 total? that's 14. Let me
            recount): stab(att) slash(att) crush(att) magic(att) ranged(att)
            = 5 attack; stab(def) slash(def) crush(def) magic(def) ranged(def)
            = 5 defence; melee_str, ranged_str, magic_dmg, prayer = 4 extras
            = 14 total i16.
    """
    slot = SLOT_NAMES.get(eq.get("slot"), 0xFF)
    reqs = eq.get("requirements") or {}
    req_pairs = [(SKILL_IDS.get(s, 0xFF), lvl) for s, lvl in reqs.items()
                 if SKILL_IDS.get(s) is not None]
    req_pairs = req_pairs[:4]

    buf = bytearray()
    buf += struct.pack("<BB", slot & 0xFF, len(req_pairs))
    for sid, lvl in req_pairs:
        buf += struct.pack("<BB", sid, lvl)

    bonuses = [
        int_or(eq.get("attack_stab"), 0),
        int_or(eq.get("attack_slash"), 0),
        int_or(eq.get("attack_crush"), 0),
        int_or(eq.get("attack_magic"), 0),
        int_or(eq.get("attack_ranged"), 0),
        int_or(eq.get("defence_stab"), 0),
        int_or(eq.get("defence_slash"), 0),
        int_or(eq.get("defence_crush"), 0),
        int_or(eq.get("defence_magic"), 0),
        int_or(eq.get("defence_ranged"), 0),
        int_or(eq.get("melee_strength"), 0),
        int_or(eq.get("ranged_strength"), 0),
        int_or(eq.get("magic_damage"), 0),
        int_or(eq.get("prayer"), 0),
    ]
    for b in bonuses:
        buf += struct.pack("<h", max(-32768, min(32767, b)))
    return bytes(buf)


def pack_weapon(wp: dict) -> bytes:
    """Pack: attack_speed u8, weapon_type u8, stance_bits u8, stance_count u8,
       per stance: combat_style_name_len u8 + string (variable)."""
    atk_speed = int_or(wp.get("attack_speed"), 4)
    wt_id = WEAPON_TYPES.get(wp.get("weapon_type"), 0)
    stances = wp.get("stances") or []

    stance_bits = 0
    for s in stances:
        style = s.get("attack_style")
        if style in STANCE_BITS:
            stance_bits |= STANCE_BITS[style]

    buf = bytearray()
    buf += struct.pack("<BBBB", atk_speed & 0xFF, wt_id & 0xFF,
                       stance_bits & 0xFF, min(len(stances), 255))
    for s in stances:
        cs = (s.get("combat_style") or "").encode("ascii", errors="replace")[:31]
        buf += struct.pack("<B", len(cs))
        buf += cs
    return bytes(buf)


def build_record(rec: dict) -> bytes | None:
    """One IDEF record. Returns bytes or None if record should be skipped."""
    if rec.get("duplicate"):
        return None  # skip exact-dup items (they share id linked_id_item)
    if rec.get("incomplete"):
        return None

    flags = 0
    if rec.get("stackable"):       flags |= F_STACKABLE
    if rec.get("tradeable"):       flags |= F_TRADEABLE
    if rec.get("members"):         flags |= F_MEMBERS
    if rec.get("quest_item"):      flags |= F_QUEST_ITEM
    if rec.get("noted"):           flags |= F_NOTED
    if rec.get("placeholder"):     flags |= F_PLACEHOLDER
    eq = rec.get("equipment")
    wp = rec.get("weapon")
    if eq:                         flags |= F_HAS_EQUIPMENT
    if wp:                         flags |= F_HAS_WEAPON

    name = (rec.get("name") or "").encode("latin-1", errors="replace")[:63]

    buf = bytearray()
    buf += struct.pack("<I", rec["id"])
    buf += struct.pack("<B", flags)
    buf += struct.pack("<B", len(name))
    buf += name
    # Weight in centigrams (whole number 0.453 kg = 45 cg)
    weight_cg = int(round((rec.get("weight") or 0.0) * 100.0))
    buf += struct.pack("<H", max(0, min(65535, weight_cg)))
    buf += struct.pack("<I", int_or(rec.get("highalch"), 0))
    buf += struct.pack("<I", int_or(rec.get("lowalch"), 0))
    buf += struct.pack("<I", int_or(rec.get("cost"), 0))
    buf += struct.pack("<I", int_or(rec.get("linked_id_noted"), 0xFFFFFFFF))

    if eq:
        buf += pack_equipment(eq)
    if wp:
        buf += pack_weapon(wp)
    return bytes(buf)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--limit", type=int, default=0, help="debug: cap records")
    args = p.parse_args()

    db = OsrsreboxedDB()
    records = []
    skipped_dup = 0
    skipped_incomplete = 0

    total = 0
    for rec in db.iter_items():
        total += 1
        if rec.get("duplicate"):
            skipped_dup += 1
            continue
        if rec.get("incomplete"):
            skipped_incomplete += 1
            continue
        packed = build_record(rec)
        if packed is None:
            continue
        records.append((rec["id"], packed))
        if args.limit and len(records) >= args.limit:
            break

    records.sort(key=lambda x: x[0])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("wb") as f:
        f.write(struct.pack("<III", IDEF_MAGIC, IDEF_VERSION, len(records)))
        for _, packed in records:
            f.write(struct.pack("<I", len(packed)))
            f.write(packed)

    sz = args.output.stat().st_size
    print(f"items total scanned: {total}")
    print(f"  skipped duplicates: {skipped_dup}")
    print(f"  skipped incomplete: {skipped_incomplete}")
    print(f"  exported records: {len(records)}")
    print(f"wrote {args.output} ({sz:,} bytes, ~{sz//max(1,len(records))} B/item)")


if __name__ == "__main__":
    main()
