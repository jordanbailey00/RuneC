#!/usr/bin/env python3
"""Scrape ==Special attack== sections from every weapon in
`Category:Weapons with Special attacks` (~122 pages).

Fetches each weapon's wikitext (cached under
`tools/wiki_cache/pages/`), extracts the level-2 section titled
"Special attack" (and any siblings like "Special attacks"), resolves
item IDs via `infobox_item` bucket, and emits
`data/curated/specials/{slug}.toml`.

No structured mechanic extraction yet — special-attack details are
prose. The TOML stores section text + item IDs so combat code can
cite them and a future narrower extractor can add parsed fields.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from wiki_pages import PageClient  # noqa: E402

CACHE = Path("/home/joe/projects/RuneC_copy/tools/wiki_cache")
OUT_DIR = Path("/home/joe/projects/RuneC_copy/data/curated/specials")
REPORT = Path("/home/joe/projects/RuneC_copy/tools/reports/item_specials.txt")

_SECTION_RE = re.compile(r"^(==)\s*(.*?)\s*==\s*$", re.MULTILINE)
_SANITIZE_RE = re.compile(r"[^A-Za-z0-9._-]+")


def _sanitize(title: str) -> str:
    s = _SANITIZE_RE.sub("_", title).strip("_")
    return s[:180] or "_"


def build_item_name_to_ids() -> dict[str, list[int]]:
    """page_name (lowercase) → [item_id, ...] from infobox_item."""
    out: dict[str, list[int]] = {}
    for p in sorted(CACHE.glob("infobox_item_*.json")):
        d = json.loads(p.read_text())
        for r in d.get("bucket", []):
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
                key = name.lower()
                out.setdefault(key, [])
                if iid not in out[key]:
                    out[key].append(iid)
    return out


def extract_special_sections(wt: str) -> dict[str, str]:
    """Return `{section_name: body}` for level-2 sections whose header
    starts with 'Special attack' (case-insensitive)."""
    out: dict[str, str] = {}
    matches = list(_SECTION_RE.finditer(wt))
    for i, m in enumerate(matches):
        header = m.group(2).strip()
        if not header.lower().startswith("special attack"):
            continue
        body_start = m.end()
        body_end = matches[i + 1].start() if i + 1 < len(matches) else len(wt)
        body = wt[body_start:body_end].strip()
        out[header] = body
    return out


def toml_escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"""', '\\"\\"\\"')


def write_toml(path: Path, *, name: str, item_ids: list[int],
               sections: dict[str, str]):
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        f'name = "{toml_escape(name)}"',
        f'item_ids = {json.dumps(item_ids)}',
        '',
        '[special]',
    ]
    for header in sorted(sections):
        body = sections[header].strip()
        if not body:
            continue
        lines.append(f'"{toml_escape(header)}" = """')
        lines.append(toml_escape(body))
        lines.append('"""')
        lines.append('')
    path.write_text("\n".join(lines))


def main():
    c = PageClient()
    c.probe()

    name_to_ids = build_item_name_to_ids()
    print(f"  resolver: {len(name_to_ids)} item names", file=sys.stderr)

    titles = c.category_members("Weapons with Special attacks", namespace=0)
    print(f"  category members: {len(titles)}", file=sys.stderr)

    emitted = 0
    no_section: list[str] = []
    no_ids: list[str] = []

    for title in titles:
        wt = c.wikitext(title)
        if not wt:
            continue
        sections = extract_special_sections(wt)
        if not sections:
            no_section.append(title)
            continue
        ids = name_to_ids.get(title.lower(), [])
        if not ids:
            no_ids.append(title)
        write_toml(OUT_DIR / f"{_sanitize(title)}.toml",
                   name=title, item_ids=sorted(ids), sections=sections)
        emitted += 1

    REPORT.parent.mkdir(parents=True, exist_ok=True)
    with REPORT.open("w") as f:
        f.write(f"emitted:           {emitted}\n")
        f.write(f"no Special section: {len(no_section)}\n")
        f.write(f"no item_id resolve: {len(no_ids)}\n")
        if no_section:
            f.write("\n--- pages missing ==Special attack== section ---\n")
            for t in no_section:
                f.write(f"  {t}\n")
        if no_ids:
            f.write("\n--- pages with unresolved item_ids ---\n")
            for t in no_ids:
                f.write(f"  {t}\n")
    print(f"  → {OUT_DIR}/ ({emitted} TOMLs)", file=sys.stderr)
    print(f"  → {REPORT}", file=sys.stderr)


if __name__ == "__main__":
    main()
