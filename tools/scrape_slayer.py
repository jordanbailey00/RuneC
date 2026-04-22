#!/usr/bin/env python3
"""Scrape slayer-master task assignment tables.

Each master's wiki page has a level-2 `==Tasks==` section containing
a wikitable. Rows list: monster, amount, weight (wrapped in a
`{{+=|weight|N|echo=2}}` template — we extract N).

Scope: only the (master, npc_name, weight) triples — just what the
fight / slayer-task code needs to pick an assignment. Skips amount
ranges, unlock requirements, alternatives, extended amounts (those
are UI / progression data outside our current scope).

Binary format — 'SLAY' magic:
  magic u32 | version u32 | master_count u32
  per master:
    name_len u8 + name[]
    task_count u16
    per task:
      weight u16
      name_len u8 + npc_name[]

npc_name is the canonical string (resolved at runtime against
`npc_id` bucket / infobox_monster for ID lookups).
"""
from __future__ import annotations

import json
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from wiki_pages import PageClient  # noqa: E402

OUT = Path("/home/joe/projects/RuneC_copy/data/defs/slayer.bin")
REPORT = Path("/home/joe/projects/RuneC_copy/tools/reports/slayer.txt")

SLAY_MAGIC = 0x59414C53  # 'SLAY'
SLAY_VERSION = 1

# Modern OSRS slayer masters (canonical page titles).
MASTERS = [
    "Turael",
    "Spria",
    "Mazchna",
    "Vannaka",
    "Chaeldar",
    "Nieve",
    "Steve",            # Nieve's replacement after Monkey Madness II
    "Konar quo Maten",
    "Duradel",
    "Krystilia",
    "Achtryn",          # Varlamore
    "Aya",              # Varlamore
]

# Weight template: {{+=|weight|N|echo=2}} → N
_WEIGHT_RE = re.compile(r"\{\{\s*\+=\s*\|\s*weight\s*\|\s*(\d+)", re.IGNORECASE)
# First [[link]] in a row — the monster name (may have |display form).
_FIRST_LINK_RE = re.compile(r"\[\[([^\]|#]+?)(?:\||#|\]\])")
# Transclusion of another page as the section body: {{:Pagename}}.
_TRANSCLUDE_RE = re.compile(r"\{\{\s*:([^|}]+?)\s*(?:\||\})", re.DOTALL)


def extract_tasks(wt: str, client: "PageClient | None" = None,
                  _visited: set[str] | None = None) -> list[tuple[str, int]]:
    """Return [(npc_name, weight), ...] from the ==Tasks== section.

    If the section is just a `{{:Other page}}` transclusion, follow it."""
    # Capture from "==Tasks==" until the next level-2 heading or EOF.
    m = re.search(r"==\s*Tasks\s*==(.*?)(?=\n==[^=]|\Z)",
                  wt, re.DOTALL | re.IGNORECASE)
    if not m:
        return []
    section = m.group(1)

    # If the section transcludes another page, recurse into it.
    if client is not None:
        tm = _TRANSCLUDE_RE.search(section)
        if tm:
            target = tm.group(1).strip()
            _visited = _visited or set()
            if target not in _visited and len(_visited) < 5:
                _visited.add(target)
                try:
                    sub_wt = client.wikitext(target)
                except Exception:
                    sub_wt = ""
                if sub_wt:
                    # Pretend the subpage is wrapped in ==Tasks== so the
                    # regex above matches on recurse.
                    return extract_tasks(
                        "==Tasks==\n" + sub_wt, client, _visited) or \
                        _extract_from_text(sub_wt)
    return _extract_from_text(section)


def _extract_from_text(section: str) -> list[tuple[str, int]]:
    out: list[tuple[str, int]] = []
    # Rows delimited by "|-". The first row is the header, skip it.
    rows = section.split("|-")
    for row in rows[1:]:
        weight_match = _WEIGHT_RE.search(row)
        if not weight_match:
            continue
        weight = int(weight_match.group(1))
        link_match = _FIRST_LINK_RE.search(row)
        if not link_match:
            continue
        npc_name = link_match.group(1).strip()
        # Skip non-monster rows (e.g. linked quests, skill cats).
        if not npc_name or npc_name.lower() in {"combat", "slayer",
                                                "attack", "defence"}:
            continue
        out.append((npc_name, weight))
    return out


def pack_short(s: str) -> bytes:
    return s.encode("latin-1", errors="replace")[:255]


def main():
    c = PageClient()
    c.probe()

    per_master: dict[str, list[tuple[str, int]]] = {}
    for m in MASTERS:
        try:
            wt = c.wikitext(m)
        except Exception as e:
            print(f"  skip {m}: {e}", file=sys.stderr)
            continue
        tasks = extract_tasks(wt, client=c)
        per_master[m] = tasks
        print(f"  {m}: {len(tasks)} tasks", file=sys.stderr)

    # Emit binary
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", SLAY_MAGIC, SLAY_VERSION,
                            len(per_master)))
        for master in sorted(per_master):
            mb = pack_short(master)
            tasks = per_master[master]
            f.write(struct.pack("<B", len(mb))); f.write(mb)
            f.write(struct.pack("<H", min(65535, len(tasks))))
            for npc_name, weight in tasks[:65535]:
                nb = pack_short(npc_name)
                f.write(struct.pack("<H", min(65535, weight)))
                f.write(struct.pack("<B", len(nb))); f.write(nb)
    print(f"  → {OUT} ({OUT.stat().st_size} bytes)", file=sys.stderr)

    # Report
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    with REPORT.open("w") as f:
        for master in sorted(per_master):
            tasks = per_master[master]
            total = sum(w for _, w in tasks)
            f.write(f"=== {master} — {len(tasks)} tasks, "
                    f"total weight {total} ===\n")
            for npc, w in sorted(tasks, key=lambda t: -t[1]):
                f.write(f"  {w:4}  {npc}\n")
            f.write("\n")
    print(f"  → {REPORT}", file=sys.stderr)


if __name__ == "__main__":
    main()
