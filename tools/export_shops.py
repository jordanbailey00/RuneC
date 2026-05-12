#!/usr/bin/env python3
"""Emit data/defs/shops.bin by joining infobox_shop + storeline.

Item resolution via `infobox_item` name → lowest item_id.

Binary format — 'SHOP' magic:
  magic u32 | version u32 | count u32
  per shop:
    name_len u8 + name[]
    owner_len u8 + owner[]
    location_len u8 + location[]
    specialty_len u8 + specialty[]
    members u8
    stock_count u16
    per stock:
      item_id u32 | buy u32 | sell u32
      stock_base u16 (0xFFFF=infinite)
      buy_mult u16 | sell_mult u16 | restock_ticks u16
"""
from __future__ import annotations

import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from acquisition_common import (  # noqa: E402
    build_item_name_to_id, load_bucket, resolve_name,
)

OUT = ROOT / "data/defs/shops.bin"
REPORT = ROOT / "tools/reports/shops.txt"

SHOP_MAGIC = 0x504F4853
SHOP_VERSION = 1
INFINITE_STOCK = 0xFFFF


def parse_int_price(s) -> int:
    if s is None:
        return 0
    s = str(s).strip()
    if not s or s.lower() in ("n/a", "not sold", "free"):
        return 0
    m = re.search(r"\d[\d,]*", s)
    return int(m.group(0).replace(",", "")) if m else 0


def parse_stock(s) -> int:
    if s is None:
        return 0
    s = str(s).strip()
    if s in ("\u221e", "inf", "infinity", "Infinite", "infinite"):
        return INFINITE_STOCK
    try:
        return min(INFINITE_STOCK - 1, max(0, int(s)))
    except ValueError:
        return 0


def parse_mult(s) -> int:
    if s is None:
        return 1000
    try:
        return max(0, min(65535, int(str(s).strip())))
    except ValueError:
        return 1000


def parse_restock_ticks(s) -> int:
    if s is None:
        return 0
    s = str(s).strip().lower()
    if s in ("", "n/a", "never", "none"):
        return 0
    if "instant" in s:
        return 1
    m = re.search(r"(\d+)\s*tick", s)
    if m: return min(65535, int(m.group(1)))
    m = re.search(r"(\d+)\s*(sec|s\b)", s)
    if m: return min(65535, int(m.group(1)) * 5 // 3)
    m = re.search(r"(\d+)\s*min", s)
    if m: return min(65535, int(m.group(1)) * 100)
    m = re.search(r"\d+", s)
    return min(65535, int(m.group(0))) if m else 0


def merge_meta(dst: dict, src: dict) -> dict:
    out = dict(dst)
    for key, value in src.items():
        if value not in (None, "") and out.get(key) in (None, ""):
            out[key] = value
    return out


def clean_markup(value) -> str:
    s = str(value or "")
    s = re.sub(r"\[\[[^|\]]+\|([^\]]+)\]\]", r"\1", s)
    s = re.sub(r"\[\[([^\]]+)\]\]", r"\1", s)
    return s.strip()


def pack_short(s: str, maxlen: int = 255) -> bytes:
    return (s or "").encode("latin-1", errors="replace")[:maxlen]


def main():
    items = build_item_name_to_id()
    shops_meta: dict[str, dict] = {}
    for r in load_bucket("infobox_shop"):
        pn = (r.get("page_name") or "").strip()
        if pn:
            shops_meta[pn] = merge_meta(shops_meta.get(pn, {}), r)

    stock: dict[str, list[dict]] = defaultdict(list)
    resolved = 0
    unresolved = 0
    for r in load_bucket("storeline"):
        pn = (r.get("page_name") or "").strip()
        item_name = (r.get("sold_item") or "").strip()
        if not pn or not item_name:
            continue
        iid = resolve_name(item_name, items)
        if iid is None:
            unresolved += 1
            continue
        resolved += 1
        stock[pn].append({
            "item_id": iid,
            "buy": parse_int_price(r.get("store_buy_price")),
            "sell": parse_int_price(r.get("store_sell_price")),
            "stock": parse_stock(r.get("store_stock")),
            "buy_mult": parse_mult(r.get("store_buy_multiplier")),
            "sell_mult": parse_mult(r.get("store_sell_multiplier")),
            "restock": parse_restock_ticks(r.get("restock_time")),
        })

    all_names = sorted(set(shops_meta) | set(stock))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    def u32(v): return max(0, min(0xFFFFFFFF, int(v)))
    def u16(v): return max(0, min(0xFFFF, int(v)))
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", SHOP_MAGIC, SHOP_VERSION, len(all_names)))
        for name in all_names:
            meta = shops_meta.get(name, {})
            lines = stock.get(name, [])
            for s in (name, meta.get("owner"), meta.get("location"),
                      meta.get("specialty")):
                b = pack_short(clean_markup(s))
                f.write(struct.pack("<B", len(b))); f.write(b)
            f.write(struct.pack("<B", 1 if meta.get("is_members_only") else 0))
            f.write(struct.pack("<H", min(65535, len(lines))))
            for ln in lines[:65535]:
                f.write(struct.pack("<IIIHHHH",
                                    u32(ln["item_id"]), u32(ln["buy"]),
                                    u32(ln["sell"]), u16(ln["stock"]),
                                    u16(ln["buy_mult"]), u16(ln["sell_mult"]),
                                    u16(ln["restock"])))

    missing_meta = sorted(name for name in stock if name not in shops_meta)
    no_stock = sorted(name for name in shops_meta if name not in stock)
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text("\n".join([
        "Shop stock index",
        "",
        "status: READY_WITH_ACCEPTED_SIMPLIFICATIONS",
        f"output binary: data/defs/shops.bin ({OUT.stat().st_size} bytes)",
        "source metadata: cached OSRS Wiki infobox_shop bucket",
        "source stock: cached OSRS Wiki storeline bucket",
        "item resolution: cached OSRS Wiki infobox_item aliases",
        f"shop rows: {len(all_names)}",
        f"shops with metadata: {len(shops_meta)}",
        f"shops with stock: {len(stock)}",
        f"stock item rows resolved: {resolved}",
        f"stock item rows unresolved: {unresolved}",
        f"stock shops missing metadata: {len(missing_meta)}",
        f"metadata shops missing stock: {len(no_stock)}",
        "",
        "accepted simplifications:",
        "  - binary stores static base stock/prices/restock ticks; per-world stock mutation lands with shop transactions",
        "  - wiki markup is stripped from owner/location/specialty labels for runtime display/search",
        "  - unresolved storeline item names stay in the report until the source alias table is widened",
    ]) + "\n")
    print(REPORT.read_text(), file=sys.stderr)


if __name__ == "__main__":
    main()
