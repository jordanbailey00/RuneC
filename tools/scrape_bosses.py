#!/usr/bin/env python3
"""Phase 3 first scrape: cache every OSRS boss page's wikitext.

Enumerates Category:Bosses via MediaWiki's category API, bulk-fetches
each page (wikitext cached to `tools/wiki_cache/pages/`), then
summarises template usage across the whole set so we can see what's
extractable.

Deliverables:
  - `tools/wiki_cache/pages/{Sanitized_Title}.json` per boss
  - `tools/reports/bosses_templates.txt` — template frequency + which
    bosses use each template, ordered by descending count. Feeds the
    per-category extractor we'll build next (Phase 3b).

Filtering:
  - Exclude pure redirects (wikitext starts with `#REDIRECT`).
  - Keep everything else — manual review after the fact is cheaper
    than pre-filtering "real" bosses from area/meta pages.
"""
from __future__ import annotations

import sys
import time
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from wiki_pages import PageClient  # noqa: E402

REPORT = Path(__file__).resolve().parent / "reports" / "bosses_templates.txt"


def main():
    c = PageClient()
    c.probe()
    t0 = time.monotonic()

    titles = c.category_members("Bosses", namespace=0)
    print(f"enumerated {len(titles)} boss titles", file=sys.stderr)

    # Bulk-fetch wikitext (cached — re-runs read from disk)
    template_count: Counter[str] = Counter()
    template_bosses: dict[str, list[str]] = defaultdict(list)
    redirect_titles: list[str] = []
    fetched = 0

    for i, title in enumerate(titles, 1):
        wt = c.wikitext(title)
        if not wt:
            continue
        if wt.lstrip().lower().startswith("#redirect"):
            redirect_titles.append(title)
            continue
        fetched += 1
        for tmpl in c.templates(title):
            name = str(tmpl.name).strip()
            template_count[name] += 1
            if len(template_bosses[name]) < 10:
                template_bosses[name].append(title)

    dt = time.monotonic() - t0
    print(f"cached {fetched} bosses, skipped {len(redirect_titles)} "
          f"redirects, in {dt/60:.1f} min", file=sys.stderr)

    REPORT.parent.mkdir(parents=True, exist_ok=True)
    with REPORT.open("w") as f:
        f.write(f"bosses scraped: {fetched}\n")
        f.write(f"redirects skipped: {len(redirect_titles)}\n")
        f.write(f"distinct templates seen: {len(template_count)}\n\n")
        f.write("top templates by usage (count, name, sample bosses):\n")
        for name, n in template_count.most_common(80):
            samples = ", ".join(template_bosses[name][:3])
            f.write(f"  {n:4}  {name:<40.40}  {samples}\n")
        if redirect_titles:
            f.write("\n--- skipped (redirects) ---\n")
            for t in redirect_titles:
                f.write(f"  {t}\n")
    print(f"  → {REPORT}", file=sys.stderr)


if __name__ == "__main__":
    main()
