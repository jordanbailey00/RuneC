#!/usr/bin/env python3
"""Cache every boss `/Strategies` subpage.

Scans cached boss pages for `{{HasStrategy}}` templates + falls back
to the `{Boss}/Strategies` convention. Emits two reports: section-
header frequency and template frequency.

Output:
  - `tools/wiki_cache/pages/{Sanitized}.json` per strategy page
  - `tools/reports/strategies_sections.txt`
  - `tools/reports/strategies_templates.txt`
  - `tools/reports/strategies_missing.txt`
"""
from __future__ import annotations

import re
import sys
import time
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from wiki_pages import PageClient  # noqa: E402
from wiki_client import PageMissing  # noqa: E402

REPORT_DIR = Path(__file__).resolve().parent / "reports"
_SECTION_RE = re.compile(r"^(=+)\s*(.*?)\s*\1\s*$", re.MULTILINE)


def strategy_titles(c: PageClient, bosses: list[str]) -> list[str]:
    out: set[str] = set()
    for boss in bosses:
        tmpls = c.templates(boss)
        had_strategy = False
        for tmpl in tmpls:
            if str(tmpl.name).strip().lower() != "hasstrategy":
                continue
            had_strategy = True
            if tmpl.params:
                arg = str(tmpl.params[0].value).strip()
                if arg:
                    out.add(arg); continue
            out.add(f"{boss}/Strategies")
        if not had_strategy:
            out.add(f"{boss}/Strategies")
    return sorted(out)


def main():
    c = PageClient()
    c.probe()
    t0 = time.monotonic()

    bosses = c.category_members("Bosses", namespace=0)
    titles = strategy_titles(c, bosses)
    print(f"discovered {len(titles)} strategy candidates", file=sys.stderr)

    cached = 0
    missing: list[str] = []
    template_count: Counter[str] = Counter()
    section_count: Counter[str] = Counter()
    template_pages: dict[str, list[str]] = defaultdict(list)

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
        for tmpl in c.templates(t):
            name = str(tmpl.name).strip()
            template_count[name] += 1
            if len(template_pages[name]) < 6:
                template_pages[name].append(t)
        for m in _SECTION_RE.finditer(wt):
            if len(m.group(1)) == 2:
                section_count[m.group(2).strip()] += 1

    dt = time.monotonic() - t0
    print(f"cached {cached}/{len(titles)}, {len(missing)} missing, "
          f"in {dt/60:.1f} min", file=sys.stderr)

    REPORT_DIR.mkdir(parents=True, exist_ok=True)
    with (REPORT_DIR / "strategies_sections.txt").open("w") as f:
        f.write(f"strategy pages cached: {cached}\n")
        f.write(f"distinct section headers: {len(section_count)}\n\n")
        for sec, n in section_count.most_common(100):
            f.write(f"  {n:4}  {sec}\n")
    with (REPORT_DIR / "strategies_templates.txt").open("w") as f:
        f.write(f"strategy pages cached: {cached}\n")
        f.write(f"distinct templates: {len(template_count)}\n\n")
        for name, n in template_count.most_common(60):
            samples = ", ".join(template_pages[name][:3])
            f.write(f"  {n:4}  {name:<40.40}  {samples}\n")
    if missing:
        with (REPORT_DIR / "strategies_missing.txt").open("w") as f:
            for m in missing:
                f.write(m + "\n")


if __name__ == "__main__":
    main()
