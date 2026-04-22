#!/usr/bin/env python3
"""Run every Bucket scrape we need for Phase 1.

Serial (MediaWiki API:Etiquette). Each bucket is a dict of
(name, fields[], optional where[]). Results land under
`tools/wiki_cache/` keyed by query hash — re-runs are free.

Order is by decreasing priority / increasing size: small fully-in-scope
tables first so any schema surprise is cheap to diagnose.
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from wiki_bucket import BucketClient  # noqa: E402


# (bucket_name, fields)  — in run order
PLAN: list[tuple[str, list[str]]] = [
    # Already done above but cheap to re-run:
    ("quest",
     ["page_name", "description", "official_difficulty", "official_length",
      "requirements", "start_point", "items_required", "enemies_to_defeat",
      "json"]),
    ("varbit",
     ["page_name", "name", "index"]),
    ("music",
     ["page_name", "title", "number", "duration", "composer",
      "is_members_only", "release_date", "cacheid", "is_jingle", "is_event",
      "unlock_hint", "unlock_detail"]),

    # Medium-size, high-value:
    ("infobox_bonuses",
     ["page_name", "stab_attack_bonus", "slash_attack_bonus",
      "crush_attack_bonus", "range_attack_bonus", "magic_attack_bonus",
      "stab_defence_bonus", "slash_defence_bonus", "crush_defence_bonus",
      "range_defence_bonus", "magic_defence_bonus", "strength_bonus",
      "ranged_strength_bonus", "prayer_bonus", "magic_damage_bonus",
      "equipment_slot", "weapon_attack_speed", "weapon_attack_range",
      "combat_style"]),

    ("infobox_monster",
     ["page_name", "name", "id", "combat_level", "hitpoints", "max_hit",
      "attack_level", "strength_level", "defence_level", "ranged_level",
      "magic_level", "attack_bonus", "strength_bonus", "range_strength_bonus",
      "magic_damage_bonus", "stab_attack_bonus", "slash_attack_bonus",
      "crush_attack_bonus", "magic_attack_bonus", "range_attack_bonus",
      "stab_defence_bonus", "slash_defence_bonus", "crush_defence_bonus",
      "magic_defence_bonus", "range_defence_bonus",
      "light_range_defence_bonus", "standard_range_defence_bonus",
      "heavy_range_defence_bonus", "attack_speed", "size",
      "poison_immune", "venom_immune", "thrall_immune", "cannon_immune",
      "burn_immune", "freeze_resistance", "elemental_weakness",
      "elemental_weakness_percent", "slayer_level", "slayer_experience",
      "slayer_category", "assigned_by", "uses_skill", "attack_style",
      "attribute", "examine", "is_members_only", "release_date",
      "version_anchor", "default_version", "poisonous", "flat_armour",
      "experience_bonus", "league_region"]),

    # Big:
    ("dropsline",
     ["page_name", "item_name", "drop_json", "rare_drop_table"]),
    ("recipe",
     ["page_name", "uses_material", "uses_tool", "uses_facility",
      "uses_skill", "is_members_only", "is_boostable",
      "production_json", "source_template"]),
    ("infobox_item",
     ["page_name", "item_id", "item_name", "examine", "is_members_only",
      "release_date"]),

    # World/scenery:
    ("infobox_scenery",
     ["page_name"]),   # field list unknown — probe first; scrape driver re-runs cheap
    ("locline",
     ["page_name", "members", "mapid", "plane", "coordinates",
      "leagueregion"]),
    ("infobox_shop",
     ["page_name"]),
    ("storeline",
     ["page_name"]),
    ("infobox_spell",
     ["page_name"]),

    # ID lookups:
    ("npc_id",     ["page_name"]),
    ("item_id",    ["page_name"]),
    ("object_id",  ["page_name"]),

    # Dialogue — large but high value for quest NPCs:
    ("transcript", ["page_name"]),
]


def main():
    c = BucketClient()
    c.probe()
    t0 = time.monotonic()
    for i, (bucket, fields) in enumerate(PLAN, 1):
        print(f"\n[{i}/{len(PLAN)}] {bucket} ({len(fields)} fields)",
              file=sys.stderr)
        try:
            total = sum(1 for _ in c.fetch(bucket, fields))
        except Exception as e:
            print(f"  ERROR: {e}", file=sys.stderr)
            continue
        print(f"  {bucket}: {total} rows", file=sys.stderr)
    dt = time.monotonic() - t0
    print(f"\nAll scrapes done in {dt/60:.1f} min", file=sys.stderr)


if __name__ == "__main__":
    main()
