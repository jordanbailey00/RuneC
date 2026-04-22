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

import json
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

CACHE = Path("/home/joe/projects/RuneC_copy/tools/wiki_cache")
OUT = Path("/home/joe/projects/RuneC_copy/data/defs/shops.bin")

SHOP_MAGIC = 0x504F4853
SHOP_VERSION = 1
INFINITE_STOCK = 0xFFFF


def load_bucket(bucket: str):
    rows = []
    for p in sorted(CACHE.glob(f"{bucket}_*.json")):
        rows.extend(json.loads(p.read_text()).get("bucket", []))
    return rows


def item_map() -> dict[str, int]:
    out: dict[str, int] = {}
    for r in load_bucket("infobox_item"):
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
            k = name.lower()
            if k not in out or iid < out[k]:
                out[k] = iid
    return out


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


def pack_short(s: str, maxlen: int = 255) -> bytes:
    return (s or "").encode("latin-1", errors="replace")[:maxlen]


def main():
    items = item_map()
    shops_meta: dict[str, dict] = {}
    for r in load_bucket("infobox_shop"):
        pn = (r.get("page_name") or "").strip()
        if pn:
            shops_meta[pn] = r

    stock: dict[str, list[dict]] = defaultdict(list)
    resolved = 0
    unresolved = 0
    for r in load_bucket("storeline"):
        pn = (r.get("page_name") or "").strip()
        item_name = (r.get("sold_item") or "").strip()
        if not pn or not item_name:
            continue
        iid = items.get(item_name.split("#", 1)[0].lower())
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

    print(f"{len(shops_meta)} shops meta; {len(stock)} shops with stock; "
          f"{resolved} resolved, {unresolved} unresolved",
          file=sys.stderr)

    all_names = sorted(set(shops_meta) | set(stock))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    def u32(v): return max(0, min(0xFFFFFFFF, int(v)))
    def u16(v): return max(0, min(0xFFFF, int(v)))
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", SHOP_MAGIC, SHOP_VERSION, len(all_names)))
        for name in all_names:
            meta = shops_meta.get(name, {})
            lines = stock.get(name, [])
            for (s, maxlen) in [(name, None), (meta.get("owner"), None),
                                (meta.get("location"), None),
                                (meta.get("specialty"), None)]:
                b = pack_short(s or "")
                f.write(struct.pack("<B", len(b))); f.write(b)
            f.write(struct.pack("<B", 1 if meta.get("is_members_only") else 0))
            f.write(struct.pack("<H", min(65535, len(lines))))
            for ln in lines[:65535]:
                f.write(struct.pack("<IIIHHHH",
                                    u32(ln["item_id"]), u32(ln["buy"]),
                                    u32(ln["sell"]), u16(ln["stock"]),
                                    u16(ln["buy_mult"]), u16(ln["sell_mult"]),
                                    u16(ln["restock"])))
    print(f"  → {OUT} ({OUT.stat().st_size} bytes)", file=sys.stderr)


if __name__ == "__main__":
    main()
