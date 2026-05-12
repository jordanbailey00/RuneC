#!/usr/bin/env python3
"""Second-pass scrape: shallow buckets that Phase 1 captured with only
`page_name`. Now we know the full field lists — re-fetch.

Safe to re-run — cache keys differ from the first pass (different
fields → different qhash), so we're adding new cache files, not
overwriting old ones.
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from wiki_bucket import BucketClient  # noqa: E402

PLAN: list[tuple[str, list[str]]] = [
    ("infobox_scenery",
     ["page_name", "default_version", "image", "is_members_only",
      "league_region", "release", "object_id", "npc_id"]),
    ("infobox_shop",
     ["page_name", "shop_version", "specialty", "location", "owner",
      "is_members_only"]),
    ("infobox_spell",
     ["page_name", "image", "type", "spellbook", "uses_material", "json"]),
    ("transcript",
     ["page_name", "page_name_sub", "npcs"]),
    ("npc_id",
     ["page_name", "page_name_sub", "id"]),
    ("item_id",
     ["page_name", "id"]),
    ("object_id",
     ["page_name", "page_name_sub", "id"]),
]


def main():
    c = BucketClient()
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
    print(f"\nDone in {(time.monotonic() - t0)/60:.1f} min",
          file=sys.stderr)


if __name__ == "__main__":
    main()
