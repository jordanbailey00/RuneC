#!/usr/bin/env python3
"""Integration-test + thin reader layer over the external data sources.

Confirms the reference repos cloned in Phase 0 are loadable and expose
the fields our exporters need.

Provides typed accessors that other exporters (`export_npcs.py`,
`export_items.py`, ...) consume:

    from database_sources import OsrsreboxedDB, DataOsrs
    db = OsrsreboxedDB()
    item = db.item(4151)          # Abyssal whip record
    npc = db.monster(3127)        # TzTok-Jad record

All reads are lazy + cached; loading the full per-id JSON dump is 25k +
3k files on disk, not worth pulling into memory at once.
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any

REF = Path("/home/joe/projects/runescape-rl-reference")
OSRSREBOXED = REF / "osrsreboxed-db"
DATA_OSRS = REF / "data_osrs"


class OsrsreboxedDB:
    """Reader for osrsreboxed-db — items + monsters + prayers.

    Fields we rely on per record:

      item (docs/items-json/{id}.json):
        id, name, examine, stackable, tradeable, members, weight,
        highalch, lowalch, cost, noted, noteable, placeholder,
        linked_id_{item,noted,placeholder}, quest_item, buy_limit,
        equipable, equipable_by_player, equipable_weapon,
        equipment: {attack_stab/slash/crush/magic/ranged,
                    defence_stab/slash/crush/magic/ranged,
                    melee_strength, ranged_strength, magic_damage,
                    prayer, slot, requirements: {skill: level}},
        weapon: {attack_speed, weapon_type, stances: [{combat_style,
                 attack_type, attack_style, experience, boosts}]}

      monster (docs/monsters-json/{id}.json):
        id, name, combat_level, hitpoints, max_hit, attack_type,
        attack_speed, aggressive, poisonous, venomous, immune_poison,
        immune_venom, attributes, category, weakness,
        slayer_level, slayer_monster, slayer_xp, slayer_masters, size,
        attack_level, strength_level, defence_level, ranged_level,
        magic_level,
        attack_{stab,slash,crush,magic,ranged},
        defence_{stab,slash,crush,magic,ranged},
        attack_bonus, strength_bonus, ranged_bonus, magic_bonus,
        drops: [{id, name, quantity, noted, rarity, rolls, drop_requirements}]
    """

    def __init__(self, root: Path = OSRSREBOXED):
        self.items_dir = root / "docs" / "items-json"
        self.monsters_dir = root / "docs" / "monsters-json"
        self.prayers_file = root / "docs" / "prayers-complete.json"
        assert self.items_dir.is_dir(), f"missing: {self.items_dir}"
        assert self.monsters_dir.is_dir(), f"missing: {self.monsters_dir}"
        self._item_cache: dict[int, dict[str, Any]] = {}
        self._monster_cache: dict[int, dict[str, Any]] = {}
        self._prayers: dict[str, Any] | None = None

    def item(self, id: int) -> dict[str, Any] | None:
        if id in self._item_cache:
            return self._item_cache[id]
        path = self.items_dir / f"{id}.json"
        if not path.is_file():
            return None
        with path.open() as f:
            rec = json.load(f)
        self._item_cache[id] = rec
        return rec

    def monster(self, id: int) -> dict[str, Any] | None:
        if id in self._monster_cache:
            return self._monster_cache[id]
        path = self.monsters_dir / f"{id}.json"
        if not path.is_file():
            return None
        with path.open() as f:
            rec = json.load(f)
        self._monster_cache[id] = rec
        return rec

    def prayers(self) -> dict[str, Any]:
        if self._prayers is None:
            with self.prayers_file.open() as f:
                self._prayers = json.load(f)
        return self._prayers

    def iter_items(self):
        """Yield every item record (~25k)."""
        for path in sorted(self.items_dir.glob("*.json")):
            with path.open() as f:
                yield json.load(f)

    def iter_monsters(self):
        """Yield every monster record (~3k)."""
        for path in sorted(self.monsters_dir.glob("*.json")):
            with path.open() as f:
                yield json.load(f)


class WikiMonsters:
    """Reader for cached OSRS Wiki `infobox_monster` bucket rows.

    The wiki's `max_hit` is authoritative for NPCs that use breath /
    special attacks (e.g. Green dragon wiki=50 vs osrsreboxed=8 —
    osrsreboxed reports base melee only). Keyed by cache NPC ID (the
    bucket `id` field is a repeated list).
    """

    CACHE = Path("/home/joe/projects/RuneC_copy/tools/wiki_cache")

    def __init__(self):
        self._by_id: dict[int, dict[str, Any]] = {}
        for p in sorted(self.CACHE.glob("infobox_monster_*.json")):
            d = json.loads(p.read_text())
            for row in d.get("bucket", []):
                ids = row.get("id") or []
                if not isinstance(ids, list):
                    ids = [ids]
                for iid in ids:
                    try:
                        iid = int(iid)
                    except (TypeError, ValueError):
                        continue
                    self._by_id.setdefault(iid, row)

    def by_id(self, npc_id: int) -> dict[str, Any] | None:
        return self._by_id.get(int(npc_id))

    @staticmethod
    def parse_int(v) -> int | None:
        import re
        if v is None:
            return None
        if isinstance(v, (int, float)):
            return int(v)
        s = str(v).strip()
        if not s:
            return None
        m = re.search(r"-?\d+", s)
        return int(m.group(0)) if m else None

    @staticmethod
    def parse_max_hit(v) -> int | None:
        """Wiki max_hit is a list of 'N (Style)' strings. Return the
        highest integer across all styles."""
        import re
        if v is None:
            return None
        if not isinstance(v, list):
            v = [v]
        values = [WikiMonsters.parse_int(x) for x in v]
        values = [x for x in values if x is not None]
        return max(values) if values else None

    @staticmethod
    def parse_bool_immune(v) -> bool | None:
        if v is None:
            return None
        s = str(v).strip().lower()
        if not s:
            return None
        if "not immune" in s or s == "false":
            return False
        if "immune" in s or s == "true":
            return True
        return None


class DataOsrs:
    """Reader for mejrs/data_osrs — NPC spawn list + teleports + transports.

    Structure (only the relevant files enumerated):
      NPCList_OSRS.json        — ~24k NPC spawn records with id, name, x, y, p
      teleports_osrs.json      — teleport destinations with source metadata
      transports_osrs.json     — transport routes (shortcuts, boats, etc.)
      location_configs/*.json  — per-loc-id location config dumps
      npcids/npcid=*.json      — per-npc-id metadata (morphs, variants)
    """

    def __init__(self, root: Path = DATA_OSRS):
        self.root = root
        self.npc_list_path = root / "NPCList_OSRS.json"
        self.teleports_path = root / "teleports_osrs.json"
        assert self.npc_list_path.is_file(), f"missing: {self.npc_list_path}"
        self._npc_list: list[dict[str, Any]] | None = None

    def npc_spawns(self) -> list[dict[str, Any]]:
        """All NPC spawn records. Each has id, name, x, y, p (plane).
        Use as supplementary source alongside 2011Scape + wiki SpawnLines."""
        if self._npc_list is None:
            with self.npc_list_path.open() as f:
                self._npc_list = json.load(f)
        return self._npc_list


# ---------------------------------------------------------------------------
# Smoke test — verify the clones are readable and the canonical fields resolve
# ---------------------------------------------------------------------------

_KNOWN_ITEM_ID = 4151      # Abyssal whip
_KNOWN_MONSTER_ID = 3127   # TzTok-Jad


def smoke_test() -> int:
    """Read one known-good record from each source. Returns exit code."""
    db = OsrsreboxedDB()
    do = DataOsrs()

    whip = db.item(_KNOWN_ITEM_ID)
    assert whip and whip["name"] == "Abyssal whip", f"item {_KNOWN_ITEM_ID} = {whip}"
    assert whip["equipment"]["attack_slash"] == 82, "whip attack_slash != 82"
    assert whip["weapon"]["attack_speed"] == 4, "whip attack_speed != 4"
    print(f"  item  {_KNOWN_ITEM_ID}: {whip['name']!r} "
          f"(slash={whip['equipment']['attack_slash']}, "
          f"speed={whip['weapon']['attack_speed']}) — OK")

    jad = db.monster(_KNOWN_MONSTER_ID)
    assert jad and jad["name"] == "TzTok-Jad", f"monster {_KNOWN_MONSTER_ID} = {jad}"
    assert jad["max_hit"] == 97, "jad max_hit != 97"
    assert jad["aggressive"] is True, "jad not aggressive"
    print(f"  npc   {_KNOWN_MONSTER_ID}: {jad['name']!r} "
          f"(max_hit={jad['max_hit']}, aggressive={jad['aggressive']}) — OK")

    prayers = db.prayers()
    assert "prayer_melee" in prayers or len(prayers) > 0
    print(f"  prayers dump: {len(prayers)} entries — OK")

    spawns = do.npc_spawns()
    print(f"  data_osrs NPC spawn list: {len(spawns)} records — OK")

    print("all sources readable")
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(smoke_test())
