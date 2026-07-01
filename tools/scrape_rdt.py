#!/usr/bin/env python3
"""Build the shared rare, gem, and mega-rare drop tables.

Runtime schemas: `schema/defs/rdt.schema.toml`,
`schema/defs/gdt.schema.toml`, and `schema/defs/mrdt.schema.toml`.
"""
from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

import mwparserfromhell as mw

sys.path.insert(0, str(Path(__file__).resolve().parent))
from wiki_pages import PageClient  # noqa: E402
from acquisition_common import build_item_name_to_id, resolve_name  # noqa: E402
from export_drops import (  # noqa: E402
    parse_rarity, parse_quantity,
)

OUT_DIR = ROOT / "data/defs"
REPORT = ROOT / "tools/reports/rdt_gdt.txt"

TABLES = [
    ("Rare drop table", "Rare drop table", "Rare drop table",
     "rdt.bin", 0x5F544452, "RDT_"),
    ("Gem drop table", "Gem drop table", "Gem Drop Table",
     "gdt.bin", 0x5F544447, "GDT_"),
    ("Mega-rare drop table", "Rare drop table", "Mega-rare drop table",
     "mrdt.bin", 0x5444524D, "MRDT"),
]
TABLE_VERSION = 1


def extract_section(wt: str, heading: str) -> str:
    target = heading.strip().lower()
    starts = list(re.finditer(r"^==(?!=)\s*(.*?)\s*==(?!=)\s*$", wt, re.M))
    for i, match in enumerate(starts):
        if match.group(1).strip().lower() != target:
            continue
        end = starts[i + 1].start() if i + 1 < len(starts) else len(wt)
        return wt[match.start():end]
    return wt


def extract_dropsline(wt: str) -> list[dict]:
    out = []
    code = mw.parse(wt)
    for t in code.filter_templates():
        template = str(t.name).strip().lower()
        if template not in ("dropsline", "dropslinereward"):
            continue
        name = str(t.get("name").value).strip() if t.has("name") else ""
        qty = str(t.get("quantity").value).strip() if t.has("quantity") else ""
        rarity = str(t.get("rarity").value).strip() if t.has("rarity") else ""
        if not name:
            continue
        out.append({"name": name, "qty": qty, "rarity": rarity,
                    "template": template})
    return out


def main():
    c = PageClient()
    items = build_item_name_to_id()
    print(f"  resolver: {len(items)} item names", file=sys.stderr)

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    report_lines: list[str] = []

    table_refs = {
        "gem drop table": "GDT_",
        "mega-rare drop table": "MRDT",
        "rare drop table": "RDT_",
    }

    for logical_name, page, section, fname, magic, tag in TABLES:
        wt = extract_section(c.wikitext(page), section)
        entries = extract_dropsline(wt)
        resolved = []
        unresolved: list[str] = []
        for e in entries:
            name_key = e["name"].split("#", 1)[0].strip().lower()
            row_kind = "item"
            table_target = ""
            iid = 0
            if name_key == "nothing":
                row_kind = "nothing"
            elif name_key in table_refs:
                row_kind = "table_ref"
                table_target = table_refs[name_key]
            else:
                found = resolve_name(e["name"], items)
                if found is not None:
                    iid = found
            if row_kind == "item" and iid == 0:
                unresolved.append(e["name"])
            rarity = parse_rarity(e["rarity"])
            qty = (0, 0) if row_kind == "nothing" else (
                parse_quantity(e["qty"]) or (0, 0)
            )
            rarity_inv = 0 if not rarity else max(1, round(1 / rarity))
            resolved.append({
                "item_id": iid,
                "qmin": max(0, min(65535, qty[0])),
                "qmax": max(0, min(65535, qty[1])),
                "rarity_inv": max(0, min(0xFFFFFFFF, rarity_inv)),
                "name": e["name"],
                "rarity_raw": e["rarity"],
                "kind": row_kind,
                "table_target": table_target,
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
        report_lines.append(f"=== {logical_name} ({tag}) ===")
        report_lines.append(f"  entries:    {len(resolved)}")
        report_lines.append(f"  unresolved: {len(unresolved)}")
        for r in resolved:
            report_lines.append(
                f"  item_id={r['item_id']:<6} qty=[{r['qmin']},{r['qmax']}]"
                f" rarity_inv={r['rarity_inv']:<8} kind={r['kind']:<9} "
                f"table={r['table_target']:<4} {r['name']!r} "
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
