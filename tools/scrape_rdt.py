#!/usr/bin/env python3
"""Scrape OSRS wiki shared drop-table pages.

Three tables:
  - Rare drop table (RDT)      → `data/defs/rdt.bin`   magic 'RDT_'
  - Mega-rare drop table (MRDT) → `data/defs/mrdt.bin`  magic 'MRDT'
  - Gem drop table (GDT)       → `data/defs/gdt.bin`   magic 'GDT_'

Each table is a set of `{{DropsLine|name=X|quantity=Y|rarity=N/M}}`
templates on its page. Many NPCs roll into these tables via the
`rare_drop_table` flag in `dropsline`; `drops.bin` currently just
counts references. This binary fills in the table contents so the
runtime roll actually has data.

Binary format (same shape for all three):
  magic u32 | version u32 | count u32
  per entry:
    item_id u32
    qmin u16  (0 if unparseable / Nothing)
    qmax u16
    rarity_inv u32  (round(1/rarity); 0 if unparseable)
"""
from __future__ import annotations

import json
import re
import struct
import sys
from pathlib import Path

import mwparserfromhell as mw

sys.path.insert(0, str(Path(__file__).resolve().parent))
from wiki_pages import PageClient  # noqa: E402
from export_drops import (  # noqa: E402
    build_item_name_to_id, parse_rarity, parse_quantity,
)

OUT_DIR = Path("/home/joe/projects/RuneC_copy/data/defs")
REPORT = Path("/home/joe/projects/RuneC_copy/tools/reports/rdt_gdt.txt")

TABLES = [
    ("Rare drop table",      "rdt.bin",  0x5F544452, "RDT_"),
    ("Mega-rare drop table", "mrdt.bin", 0x5444524D, "MRDT"),
    ("Gem drop table",       "gdt.bin",  0x5F544447, "GDT_"),
]
TABLE_VERSION = 1


def extract_dropsline(wt: str) -> list[dict]:
    out = []
    code = mw.parse(wt)
    for t in code.filter_templates():
        if str(t.name).strip().lower() != "dropsline":
            continue
        name = str(t.get("name").value).strip() if t.has("name") else ""
        qty = str(t.get("quantity").value).strip() if t.has("quantity") else ""
        rarity = str(t.get("rarity").value).strip() if t.has("rarity") else ""
        if not name:
            continue
        out.append({"name": name, "qty": qty, "rarity": rarity})
    return out


def main():
    c = PageClient()
    c.probe()
    items = build_item_name_to_id()
    print(f"  resolver: {len(items)} item names", file=sys.stderr)

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    report_lines: list[str] = []

    for page, fname, magic, tag in TABLES:
        wt = c.wikitext(page)
        entries = extract_dropsline(wt)
        resolved = []
        unresolved: list[str] = []
        for e in entries:
            key = e["name"].split("#", 1)[0].lower()
            iid = items.get(key)
            if iid is None and "(" in key:
                iid = items.get(re.sub(r"\s*\([^)]*\)\s*$", "", key).strip())
            if iid is None:
                unresolved.append(e["name"])
                # Keep unresolved with item_id=0 so the rarity slot
                # stays accurate (e.g. "Nothing" 63/128 is a table
                # entry that rolls to nothing).
                iid = 0
            rarity = parse_rarity(e["rarity"])
            qty = parse_quantity(e["qty"]) or (0, 0)
            rarity_inv = 0 if not rarity else max(1, round(1 / rarity))
            resolved.append({
                "item_id": iid,
                "qmin": max(0, min(65535, qty[0])),
                "qmax": max(0, min(65535, qty[1])),
                "rarity_inv": max(0, min(0xFFFFFFFF, rarity_inv)),
                "name": e["name"],
                "rarity_raw": e["rarity"],
            })

        out_path = OUT_DIR / fname
        with out_path.open("wb") as f:
            f.write(struct.pack("<III", magic, TABLE_VERSION, len(resolved)))
            for r in resolved:
                f.write(struct.pack("<IHHI",
                                    r["item_id"], r["qmin"], r["qmax"],
                                    r["rarity_inv"]))

        print(f"  {tag}: {len(resolved)} entries "
              f"({len(unresolved)} unresolved names) → {out_path} "
              f"({out_path.stat().st_size} bytes)", file=sys.stderr)
        report_lines.append(f"=== {page} ({tag}) ===")
        report_lines.append(f"  entries:    {len(resolved)}")
        report_lines.append(f"  unresolved: {len(unresolved)}")
        for r in resolved:
            report_lines.append(
                f"  item_id={r['item_id']:<6} qty=[{r['qmin']},{r['qmax']}]"
                f" rarity_inv={r['rarity_inv']:<8} {r['name']!r} "
                f"(raw rarity: {r['rarity_raw']!r})"
            )
        if unresolved:
            report_lines.append(f"  unresolved names:")
            for n in unresolved:
                report_lines.append(f"    {n}")
        report_lines.append("")

    REPORT.write_text("\n".join(report_lines))
    print(f"  → {REPORT}", file=sys.stderr)


if __name__ == "__main__":
    main()
