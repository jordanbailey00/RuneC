#!/usr/bin/env python3
"""Cross-validate osrsreboxed-db against the OSRS Wiki Bucket data.

Two sources claim authority over the same facts; this tool flags where
they disagree so a human can arbitrate. No binary output — just a
report file that Phase 4/5 polishing can consume.

Reports:
  - tools/reports/xvalidate_monsters.txt
      osrsreboxed-db monster stats vs `infobox_monster` bucket.
      Matches by ID (osrsreboxed) vs id-list (bucket).
      Checks: hitpoints, max_hit, attack_speed, combat_level,
      size, slayer_level, immune_poison/venom,
      attack/strength/defence/magic/ranged levels.

  - tools/reports/xvalidate_bonuses.txt
      osrsreboxed-db item equipment vs `infobox_bonuses` bucket.
      Matches by item-name (bucket page_name) → item-id (infobox_item)
      → osrsreboxed.item(id).
      Checks: all 13 equipment bonuses.

Mismatch entries print both values side-by-side. Matches are silent.
"""
from __future__ import annotations

import json
import re
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
from database_sources import OsrsreboxedDB  # noqa: E402

CACHE = Path("/home/joe/projects/RuneC_copy/tools/wiki_cache")
REPORTS = Path("/home/joe/projects/RuneC_copy/tools/reports")
REPORTS.mkdir(parents=True, exist_ok=True)


def load_bucket(bucket: str):
    rows = []
    for p in sorted(CACHE.glob(f"{bucket}_*.json")):
        d = json.loads(p.read_text())
        rows.extend(d.get("bucket", []))
    return rows


def parse_int(v, default=None):
    if v is None:
        return default
    if isinstance(v, (int, float)):
        return int(v)
    s = str(v).strip()
    if not s:
        return default
    m = re.search(r"-?\d+", s)
    return int(m.group(0)) if m else default


def parse_bool_immune(s) -> bool | None:
    """Wiki immunity fields are "Immune" / "Not immune" / blank."""
    if s is None:
        return None
    s = str(s).strip().lower()
    if not s:
        return None
    if "not immune" in s or s == "false":
        return False
    if "immune" in s or s == "true":
        return True
    return None


def xval_monsters(db: OsrsreboxedDB):
    """Per-NPC-id diff. Bucket rows carry id as a list (one page_name has
    multiple variants → multiple IDs). Index them per-id."""
    by_id: dict[int, dict] = {}
    for r in load_bucket("infobox_monster"):
        ids = r.get("id") or []
        if not isinstance(ids, list):
            ids = [ids]
        for nid in ids:
            try:
                nid = int(nid)
            except (TypeError, ValueError):
                continue
            # Prefer first occurrence; don't overwrite.
            by_id.setdefault(nid, r)

    out_lines = []
    mismatch_count = 0
    matched = 0

    def check(nid, field, reb_val, wiki_val, row_name):
        nonlocal mismatch_count
        if reb_val is None or wiki_val is None:
            return
        if reb_val != wiki_val:
            mismatch_count += 1
            out_lines.append(
                f"  id={nid:<6} {row_name:<30.30} {field:<22} "
                f"reboxed={reb_val!r:<10} wiki={wiki_val!r}")

    for nid, wiki in by_id.items():
        reb = db.monster(nid)
        if not reb:
            continue
        matched += 1
        nm = wiki.get("name") or reb.get("name") or f"#{nid}"

        check(nid, "hitpoints", reb.get("hitpoints"),
              parse_int(wiki.get("hitpoints")), nm)
        check(nid, "combat_level", reb.get("combat_level"),
              parse_int(wiki.get("combat_level")), nm)
        check(nid, "attack_speed", reb.get("attack_speed"),
              parse_int(wiki.get("attack_speed")), nm)
        check(nid, "size", reb.get("size"),
              parse_int(wiki.get("size")), nm)
        # Levels
        for field in ("attack_level", "strength_level", "defence_level",
                      "ranged_level", "magic_level"):
            check(nid, field, reb.get(field),
                  parse_int(wiki.get(field)), nm)
        # Slayer
        check(nid, "slayer_level", reb.get("slayer_level"),
              parse_int(wiki.get("slayer_level")), nm)
        # max_hit: wiki is a list of "attack_type:N" strings; take max int
        wh = wiki.get("max_hit")
        if wh:
            if not isinstance(wh, list):
                wh = [wh]
            maxes = [parse_int(x) for x in wh]
            maxes = [m for m in maxes if m is not None]
            if maxes:
                check(nid, "max_hit", reb.get("max_hit"),
                      max(maxes), nm)
        # Immunities
        for field in ("poison_immune", "venom_immune"):
            reb_key = "immune_poison" if field == "poison_immune" \
                      else "immune_venom"
            reb_v = reb.get(reb_key)
            wiki_v = parse_bool_immune(wiki.get(field))
            if reb_v is not None and wiki_v is not None and reb_v != wiki_v:
                mismatch_count += 1
                out_lines.append(
                    f"  id={nid:<6} {nm:<30.30} {field:<22} "
                    f"reboxed={reb_v} wiki={wiki_v}")

    path = REPORTS / "xvalidate_monsters.txt"
    with path.open("w") as f:
        f.write(f"monsters checked:  {matched}\n")
        f.write(f"mismatches:        {mismatch_count}\n\n")
        if mismatch_count == 0:
            f.write("(no mismatches)\n")
        else:
            f.write("--- mismatches ---\n")
            f.writelines(l + "\n" for l in out_lines)
    print(f"  → {path}: {matched} checked, {mismatch_count} mismatches",
          file=sys.stderr)


def xval_bonuses(db: OsrsreboxedDB):
    """Equipment bonus diff. Bucket rows key on page_name (item name).
    Resolve name → item_id via infobox_item, then osrsreboxed.item(id)."""
    # name → id
    name_to_id: dict[str, int] = {}
    for r in load_bucket("infobox_item"):
        name = (r.get("item_name") or r.get("page_name") or "").strip()
        if not name:
            continue
        ids = r.get("item_id") or []
        if not isinstance(ids, list):
            ids = [ids]
        for iid in ids:
            try:
                iid = int(iid)
            except (TypeError, ValueError):
                continue
            k = name.lower()
            if k not in name_to_id or iid < name_to_id[k]:
                name_to_id[k] = iid

    # bonuses field → osrsreboxed equipment key
    field_map = {
        "stab_attack_bonus":  "attack_stab",
        "slash_attack_bonus": "attack_slash",
        "crush_attack_bonus": "attack_crush",
        "magic_attack_bonus": "attack_magic",
        "range_attack_bonus": "attack_ranged",
        "stab_defence_bonus": "defence_stab",
        "slash_defence_bonus":"defence_slash",
        "crush_defence_bonus":"defence_crush",
        "magic_defence_bonus":"defence_magic",
        "range_defence_bonus":"defence_ranged",
        "strength_bonus":     "melee_strength",
        "ranged_strength_bonus":"ranged_strength",
        "prayer_bonus":       "prayer",
    }

    out_lines = []
    matched = 0
    mismatch_count = 0
    unresolved_name = 0

    for r in load_bucket("infobox_bonuses"):
        name = (r.get("page_name") or "").strip()
        if not name:
            continue
        iid = name_to_id.get(name.lower())
        if iid is None:
            unresolved_name += 1
            continue
        reb = db.item(iid)
        if not reb or not reb.get("equipment"):
            continue
        matched += 1
        eq = reb["equipment"]
        for wiki_field, reb_field in field_map.items():
            wv = parse_int(r.get(wiki_field))
            rv = eq.get(reb_field)
            if wv is None or rv is None:
                continue
            try:
                rv = int(rv)
            except (TypeError, ValueError):
                continue
            if rv != wv:
                mismatch_count += 1
                out_lines.append(
                    f"  id={iid:<6} {name:<30.30} {wiki_field:<22} "
                    f"reboxed={rv!r:<8} wiki={wv!r}")

    path = REPORTS / "xvalidate_bonuses.txt"
    with path.open("w") as f:
        f.write(f"items matched:          {matched}\n")
        f.write(f"mismatches:             {mismatch_count}\n")
        f.write(f"name resolution misses: {unresolved_name}\n\n")
        if mismatch_count == 0:
            f.write("(no mismatches)\n")
        else:
            f.write("--- mismatches ---\n")
            f.writelines(l + "\n" for l in out_lines[:5000])
            if len(out_lines) > 5000:
                f.write(f"\n... ({len(out_lines) - 5000} more omitted)\n")
    print(f"  → {path}: {matched} checked, {mismatch_count} mismatches "
          f"({unresolved_name} unresolved names)", file=sys.stderr)


def main():
    db = OsrsreboxedDB()
    print("cross-validating monsters…", file=sys.stderr)
    xval_monsters(db)
    print("cross-validating item bonuses…", file=sys.stderr)
    xval_bonuses(db)


if __name__ == "__main__":
    main()
