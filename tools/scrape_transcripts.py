#!/usr/bin/env python3
"""Bulk-fetch every `Transcript:{NPC}` page's wikitext to cache.

The `transcript` Bucket has 1,844 transcript-index rows (with npc
associations); the actual dialogue text lives on each page's wikitext.
This scraper fetches all distinct transcript page_names.

Output: `tools/wiki_cache/pages/Transcript_*.json` per page.
        `tools/reports/transcripts.txt` summary.
"""
from __future__ import annotations

import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from wiki_pages import PageClient  # noqa: E402
from wiki_client import PageMissing  # noqa: E402

CACHE = Path("/home/joe/projects/RuneC_copy/tools/wiki_cache")
REPORT = Path("/home/joe/projects/RuneC_copy/tools/reports/transcripts.txt")


def main():
    c = PageClient()
    c.probe()

    # Collect distinct Transcript: page names from bucket cache.
    titles: set[str] = set()
    for p in sorted(CACHE.glob("transcript_*.json")):
        d = json.loads(p.read_text())
        for r in d.get("bucket", []):
            pn = (r.get("page_name") or "").strip()
            if pn.startswith("Transcript:"):
                titles.add(pn)
    titles = sorted(titles)
    print(f"  {len(titles)} distinct Transcript: pages", file=sys.stderr)

    t0 = time.monotonic()
    cached = 0
    missing: list[str] = []
    for t in titles:
        try:
            wt = c.wikitext(t)
        except PageMissing:
            missing.append(t); continue
        if not wt:
            missing.append(t); continue
        if wt.lstrip().lower().startswith("#redirect"):
            missing.append(t + "  (redirect)"); continue
        cached += 1

    dt = time.monotonic() - t0
    print(f"  cached {cached}/{len(titles)}, {len(missing)} missing, "
          f"in {dt/60:.1f} min", file=sys.stderr)

    REPORT.parent.mkdir(parents=True, exist_ok=True)
    with REPORT.open("w") as f:
        f.write(f"transcripts cached: {cached}\n")
        f.write(f"missing / redirect: {len(missing)}\n")
        if missing:
            f.write("\n--- missing ---\n")
            for m in missing:
                f.write(f"  {m}\n")
    print(f"  → {REPORT}", file=sys.stderr)


if __name__ == "__main__":
    main()
