#!/usr/bin/env python3
"""Extract fight-mechanics prose from cached boss wikitexts.

Reads `tools/wiki_cache/pages/*.json` (boss main + /Strategies) and
emits `data/curated/mechanics/{slug}.toml` containing ONLY sections
that describe the fight — mechanics, attacks, phases, etc.

Player-loadout sections (Inventory, Equipment, Suggested skills,
Transportation, Requirements, Recommended equipment) are excluded by
whitelist rule: a section is kept only if its header matches a
fight-relevant keyword (see `IN_SCOPE_KEYWORDS`). See `ignore.md` §15.

Boss NPC-id resolution is best-effort via `infobox_monster` bucket:
we match `page_name` → `id[]`. Some bosses have multiple ids (phases,
variants); the TOML lists all of them.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from wiki_pages import PageClient  # noqa: E402

CACHE = Path("/home/joe/projects/RuneC_copy/tools/wiki_cache")
PAGES = CACHE / "pages"
OUT_DIR = Path("/home/joe/projects/RuneC_copy/data/curated/mechanics")
REPORT = Path("/home/joe/projects/RuneC_copy/tools/reports/mechanics_extract.txt")

# Section header regex: matches `== Header ==` (level-2 only).
_SECTION_RE = re.compile(r"^(==)\s*(.*?)\s*==\s*$", re.MULTILINE)
# Sanitization used by wiki_pages.py — need to invert mapping from file
# names back to page titles.
_SANITIZE_RE = re.compile(r"[^A-Za-z0-9._-]+")

# Keywords that qualify a section as fight-relevant. Matched
# case-insensitively against the section header (substring match).
IN_SCOPE_KEYWORDS = (
    "mechanic", "attack", "ability", "fight", "phase", "form",
    "awakened", "weakness", "dragonfire", "prayer info",
    "overview",  # "Fight overview", "Overview"
    "special",   # "Special attack", "Specials"
)

# Explicit OUT list — if header matches any of these exactly (case-
# insensitively), skip it even if it contains a whitelist keyword.
# Catches edge cases like "Used in recommended equipment".
OUT_EXACT = {
    "used in recommended equipment",
    "combat stats",       # duplicates infobox_monster data
    "combat achievements", # out per ignore.md
    "drops",              # covered by dropsline bucket
    "inventory",
    "inventory setups",
    "inventory recommendations",
    "equipment",
    "recommended equipment",
    "setups",
    "transportation",
    "suggested skills",
    "requirements",
    "plugins",
    "references",
    "trivia",
    "gallery",
    "changes",
    "history",
    "dialogue",
    "quotes",
    "location",
    "locations",
    "music",
    "money making",
    "money making guide",
    "rewards",
    "historical",
    "development history",
    "concept art",
    "notes",
    "tips",
    "tips and tricks",
    "see also",
    "recommendations",
    "official worlds",
    "members' worlds drops",
    "free-to-play worlds drops",
    "treasure trails",
    "contracts",
    "products",
    "creation",
    "stat restoration",
}


def in_scope(header: str) -> bool:
    h = header.lower().strip()
    if h in OUT_EXACT:
        return False
    return any(kw in h for kw in IN_SCOPE_KEYWORDS)


def sections(wt: str) -> dict[str, str]:
    """Split wikitext into `{level-2 header: body}` map."""
    out: dict[str, str] = {}
    matches = list(_SECTION_RE.finditer(wt))
    for i, m in enumerate(matches):
        header = m.group(2).strip()
        body_start = m.end()
        body_end = matches[i + 1].start() if i + 1 < len(matches) else len(wt)
        body = wt[body_start:body_end].strip()
        out[header] = body
    return out


def load_cached(title: str) -> str | None:
    p = PAGES / f"{_sanitize(title)}.json"
    if not p.exists():
        return None
    return json.loads(p.read_text()).get("wikitext") or None


def _sanitize(title: str) -> str:
    s = _SANITIZE_RE.sub("_", title).strip("_")
    return s[:180] or "_"


def load_npc_id_map() -> dict[str, list[int]]:
    """page_name (lowercased) → [npc_id, ...] from infobox_monster."""
    out: dict[str, list[int]] = {}
    for p in sorted(CACHE.glob("infobox_monster_*.json")):
        d = json.loads(p.read_text())
        for r in d.get("bucket", []):
            pn = (r.get("page_name") or r.get("name") or "").strip()
            if not pn:
                continue
            ids = r.get("id") or []
            if not isinstance(ids, list):
                ids = [ids]
            key = pn.lower()
            for iid in ids:
                try:
                    iid = int(iid)
                except (TypeError, ValueError):
                    continue
                out.setdefault(key, [])
                if iid not in out[key]:
                    out[key].append(iid)
    return out


def toml_escape(s: str) -> str:
    # Multi-line TOML string escape: backslash + triple-quote close.
    return s.replace("\\", "\\\\").replace('"""', '\\"\\"\\"')


def write_toml(path: Path, *, name: str, source_pages: list[str],
               npc_ids: list[int], secs: dict[str, str]):
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        f'name = "{toml_escape(name)}"',
        f'source_pages = {json.dumps(source_pages)}',
        f'npc_ids = {json.dumps(npc_ids)}',
        '',
        '[sections]',
    ]
    for header in sorted(secs):
        body = secs[header].strip()
        if not body:
            continue
        # TOML multi-line basic string — keep raw wikitext.
        lines.append(f'"{toml_escape(header)}" = """')
        lines.append(toml_escape(body))
        lines.append('"""')
        lines.append('')
    path.write_text("\n".join(lines))


def main():
    ids_map = load_npc_id_map()

    # Restrict to Category:Bosses members — prevents us from emitting
    # mechanics TOMLs for weapon pages / other non-boss cached pages.
    client = PageClient(verbose=False)
    boss_titles = set(client.category_members("Bosses", namespace=0))
    print(f"Category:Bosses = {len(boss_titles)} members", file=sys.stderr)

    cached_titles: set[str] = set()
    for p in PAGES.glob("*.json"):
        d = json.loads(p.read_text())
        title = d.get("title")
        if title:
            cached_titles.add(title)

    # Main = boss titles (filtered to Category:Bosses) that are cached.
    main_pages = [t for t in sorted(cached_titles)
                  if "/Strategies" not in t and t in boss_titles]
    strategies_pages = {t.split("/Strategies")[0]: t
                        for t in cached_titles
                        if t.endswith("/Strategies")}

    print(f"{len(main_pages)} main pages cached; "
          f"{len(strategies_pages)} /Strategies siblings",
          file=sys.stderr)

    stats = {"emitted": 0, "no_sections": 0,
             "no_id_resolution": 0, "sections_kept": 0,
             "sections_cut": 0}
    no_resolution_titles: list[str] = []

    for title in main_pages:
        main_wt = load_cached(title) or ""
        strat_title = strategies_pages.get(title)
        strat_wt = load_cached(strat_title) if strat_title else ""

        merged: dict[str, str] = {}
        # Track which page each section came from — if main + strat
        # both have "Mechanics", prefer strategies (more detail) and
        # tag source in header.
        for src, wt in [("main", main_wt), ("strategies", strat_wt or "")]:
            if not wt:
                continue
            for header, body in sections(wt).items():
                if not in_scope(header):
                    stats["sections_cut"] += 1
                    continue
                stats["sections_kept"] += 1
                key = header
                if key in merged and src == "strategies":
                    # Replace main's version with strategies'.
                    merged[key] = body
                elif key not in merged:
                    merged[key] = body

        if not merged:
            stats["no_sections"] += 1
            continue

        npc_ids = ids_map.get(title.lower(), [])
        if not npc_ids:
            stats["no_id_resolution"] += 1
            if len(no_resolution_titles) < 40:
                no_resolution_titles.append(title)

        slug = _sanitize(title)
        out_path = OUT_DIR / f"{slug}.toml"
        source_pages = [title]
        if strat_title:
            source_pages.append(strat_title)
        write_toml(out_path,
                   name=title,
                   source_pages=source_pages,
                   npc_ids=npc_ids,
                   secs=merged)
        stats["emitted"] += 1

    REPORT.parent.mkdir(parents=True, exist_ok=True)
    with REPORT.open("w") as f:
        f.write(f"bosses emitted:         {stats['emitted']}\n")
        f.write(f"no fight sections:      {stats['no_sections']}\n")
        f.write(f"no NPC-id resolution:   {stats['no_id_resolution']}\n")
        f.write(f"sections kept:          {stats['sections_kept']}\n")
        f.write(f"sections cut (out of scope): {stats['sections_cut']}\n")
        if no_resolution_titles:
            f.write("\nPages without NPC-id resolution (first 40):\n")
            for t in no_resolution_titles:
                f.write(f"  {t}\n")
    print(f"  → {OUT_DIR}/ ({stats['emitted']} TOMLs)", file=sys.stderr)
    print(f"  → {REPORT}", file=sys.stderr)


if __name__ == "__main__":
    main()
