#!/usr/bin/env python3
"""Extract `==Walkthrough==` section text from each quest's wiki page.

Scope per `work.md` Phase 6: produce reference TOML per quest with
the walkthrough split into sub-section steps. NOT a runnable state
machine — those are per-quest hand-authored in Phase 4 territory.

This tool:
1. Enumerates quest titles from the `quest` bucket cache.
2. For each quest, fetches its main wiki page (cached).
3. Extracts the `==Walkthrough==` section.
4. Splits the walkthrough into sub-sections (level-3 `=== ... ===`
   headers within the section).
5. For each sub-section, captures prose + referenced items +
   referenced NPCs + referenced locations (by parsing [[links]]).

Output: `data/curated/quests/{slug}/steps.toml` per quest.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from wiki_pages import PageClient  # noqa: E402
from wiki_client import PageMissing  # noqa: E402

CACHE = Path("/home/joe/projects/RuneC_copy/tools/wiki_cache")
OUT_BASE = Path("/home/joe/projects/RuneC_copy/data/curated/quests")
REPORT = Path("/home/joe/projects/RuneC_copy/tools/reports/quest_steps.txt")

_SANITIZE_RE = re.compile(r"[^A-Za-z0-9._-]+")
_LINK_RE = re.compile(r"\[\[([^\]|#]+?)(?:\||#|\]\])")


def _sanitize(title: str) -> str:
    s = _SANITIZE_RE.sub("_", title).strip("_")
    return s[:180] or "_"


def quest_titles_from_bucket() -> list[str]:
    out: set[str] = set()
    for p in sorted(CACHE.glob("quest_*.json")):
        d = json.loads(p.read_text())
        for r in d.get("bucket", []):
            blob = r.get("json")
            if blob:
                try:
                    j = json.loads(blob)
                    name = j.get("name")
                    if name:
                        out.add(name)
                        continue
                except (json.JSONDecodeError, TypeError):
                    pass
            pn = r.get("page_name")
            if pn:
                out.add(pn)
    return sorted(out)


def extract_walkthrough(wt: str) -> str | None:
    m = re.search(r"==\s*Walkthrough\s*==(.*?)(?=\n==[^=]|\Z)",
                  wt, re.DOTALL | re.IGNORECASE)
    return m.group(1) if m else None


def split_steps(walkthrough: str) -> list[dict]:
    """Split walkthrough into sub-sections (level-3 headings).

    If no sub-sections, return a single step containing all text.
    """
    steps = []
    parts = list(re.finditer(r"^===\s*(.*?)\s*===\s*$",
                             walkthrough, re.MULTILINE))
    if not parts:
        return [{"name": "Main", "text": walkthrough.strip()}]
    for i, m in enumerate(parts):
        body_start = m.end()
        body_end = parts[i + 1].start() if i + 1 < len(parts) else \
            len(walkthrough)
        steps.append({
            "name": m.group(1).strip(),
            "text": walkthrough[body_start:body_end].strip(),
        })
    return steps


def links_in(text: str) -> list[str]:
    seen: set[str] = set()
    out: list[str] = []
    for m in _LINK_RE.finditer(text):
        link = m.group(1).strip()
        if link and link not in seen and not link.startswith(
                ("File:", "Category:", "Update:")):
            seen.add(link)
            out.append(link)
    return out


def toml_escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"""', '\\"\\"\\"')


def write_steps_toml(path: Path, *, quest: str, steps: list[dict]):
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        f'quest = "{toml_escape(quest)}"',
        f'step_count = {len(steps)}',
        '',
    ]
    for i, s in enumerate(steps):
        lines.append('[[steps]]')
        lines.append(f'index = {i}')
        lines.append(f'name = "{toml_escape(s["name"])}"')
        lines.append(f'links = {json.dumps(s.get("links", []))}')
        lines.append('text = """')
        lines.append(toml_escape(s["text"]))
        lines.append('"""')
        lines.append('')
    path.write_text("\n".join(lines))


def main():
    c = PageClient()
    c.probe()

    titles = quest_titles_from_bucket()
    print(f"  {len(titles)} quest titles from bucket cache", file=sys.stderr)

    emitted = 0
    no_walkthrough: list[str] = []
    missing: list[str] = []
    total_steps = 0

    for title in titles:
        try:
            wt = c.wikitext(title)
        except PageMissing:
            missing.append(title); continue
        if not wt:
            missing.append(title); continue
        walk = extract_walkthrough(wt)
        if not walk:
            no_walkthrough.append(title); continue
        steps = split_steps(walk)
        for s in steps:
            s["links"] = links_in(s["text"])
        slug = _sanitize(title)
        out_path = OUT_BASE / slug / "steps.toml"
        write_steps_toml(out_path, quest=title, steps=steps)
        emitted += 1
        total_steps += len(steps)

    REPORT.parent.mkdir(parents=True, exist_ok=True)
    with REPORT.open("w") as f:
        f.write(f"quest titles processed: {len(titles)}\n")
        f.write(f"emitted step TOMLs:    {emitted}\n")
        f.write(f"no walkthrough:        {len(no_walkthrough)}\n")
        f.write(f"missing page:          {len(missing)}\n")
        f.write(f"total steps:           {total_steps}\n")
        f.write(f"avg steps/quest:       {total_steps / max(1, emitted):.1f}\n")
        if no_walkthrough:
            f.write("\n--- no walkthrough ---\n")
            for t in no_walkthrough:
                f.write(f"  {t}\n")
        if missing:
            f.write("\n--- missing pages ---\n")
            for t in missing:
                f.write(f"  {t}\n")
    print(f"  → {OUT_BASE}/ ({emitted} quest step TOMLs, "
          f"{total_steps} steps)", file=sys.stderr)
    print(f"  → {REPORT}", file=sys.stderr)


if __name__ == "__main__":
    main()
