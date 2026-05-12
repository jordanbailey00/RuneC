#!/usr/bin/env python3
"""Copy known-good OSRS item icon PNGs into the local viewer asset tree.

This is a transitional exporter. The long-term target is a repo-local
ItemSpriteFactory-equivalent renderer driven by b237 item 2D metadata. Until
that renderer exists, use the local runescape-rl-reference item icon dump so the
runtime does not fabricate inventory sprites from arbitrary 3D snapshots.
"""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path


DEFAULT_REFERENCE = Path(
    "/home/joe/projects/runescape-rl-reference/osrsreboxed-db/docs/items-icons"
)
DEFAULT_OUTPUT = Path("data/sprites/items")
DEFAULT_STACKED_ITEMS = Path(
    "/home/joe/projects/runescape-rl-reference/osrsreboxed-db/data/items/items-stacked.json"
)
DEFAULT_VARIANTS_OUTPUT = DEFAULT_OUTPUT / "item_stack_variants.tsv"

DEFAULT_ITEM_IDS = (
    995,
    554,
    555,
    556,
    557,
    558,
    560,
    562,
    861,
    892,
    1381,
    1038,
    1040,
    1042,
    1044,
    1046,
    1048,
    4151,
    11802,
    11832,
    11834,
    26382,
    26384,
    26386,
    10350,
    10348,
    10346,
    10352,
)

COIN_VISUAL_QUANTITIES = (1, 2, 3, 4, 5, 25, 100, 250, 1000, 10000)


def parse_item_ids(raw: str | None) -> list[int]:
    if not raw:
        return list(DEFAULT_ITEM_IDS)
    return [int(part.strip()) for part in raw.replace(",", " ").split() if part.strip()]


def parse_all_item_ids(reference_dir: Path) -> list[int]:
    item_ids: list[int] = []
    for path in reference_dir.glob("*.png"):
        try:
            item_ids.append(int(path.stem))
        except ValueError:
            continue
    item_ids.sort()
    return item_ids


def copy_icon(reference_dir: Path, output_dir: Path, item_id: int) -> bool:
    src = reference_dir / f"{item_id}.png"
    if not src.exists():
        print(f"warning: missing reference icon for item {item_id}: {src}")
        return False
    dst = output_dir / f"item_{item_id}.png"
    shutil.copy2(src, dst)
    return True


def write_stack_variants(stacked_items_path: Path, output_path: Path) -> int:
    if not stacked_items_path.is_file():
        print(f"warning: stacked item metadata not found: {stacked_items_path}")
        return 0

    data = json.loads(stacked_items_path.read_text())
    rows: list[tuple[int, int, int]] = []
    for variant_id_raw, entry in data.items():
        try:
            variant_id = int(variant_id_raw)
            base_id = int(entry["id"])
            count = int(entry["count"])
        except (KeyError, TypeError, ValueError):
            continue
        if base_id > 0 and variant_id > 0 and count > 1:
            rows.append((base_id, count, variant_id))

    rows.sort()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8") as f:
        f.write("# base_item_id\tcount_threshold\tdisplay_item_id\n")
        for base_id, count, variant_id in rows:
            f.write(f"{base_id}\t{count}\t{variant_id}\n")
    return len(rows)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--reference", type=Path, default=DEFAULT_REFERENCE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--item-ids", help="space/comma separated item ids")
    parser.add_argument(
        "--all",
        action="store_true",
        help="copy every numeric item PNG from the reference icon directory",
    )
    parser.add_argument("--stacked-items", type=Path, default=DEFAULT_STACKED_ITEMS)
    parser.add_argument("--variants-output", type=Path, default=DEFAULT_VARIANTS_OUTPUT)
    args = parser.parse_args(argv)

    if not args.reference.is_dir():
        raise SystemExit(f"reference item icon directory not found: {args.reference}")
    args.output.mkdir(parents=True, exist_ok=True)

    item_ids = parse_all_item_ids(args.reference) if args.all else parse_item_ids(args.item_ids)
    copied = 0
    for item_id in item_ids:
        if copy_icon(args.reference, args.output, item_id):
            copied += 1

    coin = args.output / "item_995.png"
    if coin.exists():
        for quantity in COIN_VISUAL_QUANTITIES:
            shutil.copy2(coin, args.output / f"item_995_{quantity}.png")

    variants = write_stack_variants(args.stacked_items, args.variants_output)
    print(f"copied {copied} item icons to {args.output}")
    if variants:
        print(f"wrote {variants} stack icon variants to {args.variants_output}")
    return 0 if copied > 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
