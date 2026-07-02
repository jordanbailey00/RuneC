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
import os
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from export_item_render_models import (  # noqa: E402
    parse_item_ids as parse_render_item_ids,
    parse_items_bin,
)


DEFAULT_REFERENCE = (
    Path(raw) if (raw := os.environ.get("RUNEC_REFERENCE_ITEM_ICONS")) else None
)
DEFAULT_OUTPUT = Path("data/sprites/items")
DEFAULT_STACKED_ITEMS = (
    Path(raw) if (raw := os.environ.get("RUNEC_REFERENCE_STACKED_ITEMS")) else None
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


def parse_explicit_item_ids(raw: str | None) -> list[int]:
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


def resolve_item_ids(raw: str | None, args: argparse.Namespace) -> list[int]:
    if args.all:
        return parse_all_item_ids(args.reference)
    normalized = (raw or "").strip().lower()
    if normalized in {"combat-validation", "all-equippable", "default"}:
        items = parse_items_bin(args.items)
        return parse_render_item_ids(
            normalized,
            items,
            args.dev_validation_source,
        )
    return parse_explicit_item_ids(raw)


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
    parser.add_argument("--items", type=Path, default=Path("data/defs/items.bin"),
                        help="items.bin used when --item-ids is a named set")
    parser.add_argument("--dev-validation-source", type=Path,
                        default=Path("rc-viewer/dev_validation.c"),
                        help="source file used by the combat-validation item set")
    parser.add_argument("--fail-on-missing", action="store_true",
                        help="return nonzero if any requested reference icon is absent")
    parser.add_argument(
        "--all",
        action="store_true",
        help="copy every numeric item PNG from the reference icon directory",
    )
    parser.add_argument("--stacked-items", type=Path, default=DEFAULT_STACKED_ITEMS)
    parser.add_argument("--variants-output", type=Path, default=DEFAULT_VARIANTS_OUTPUT)
    args = parser.parse_args(argv)

    if args.reference is None:
        raise SystemExit(
            "reference item icon directory required; pass --reference or set "
            "RUNEC_REFERENCE_ITEM_ICONS"
        )
    if not args.reference.is_dir():
        raise SystemExit(f"reference item icon directory not found: {args.reference}")
    args.output.mkdir(parents=True, exist_ok=True)

    item_ids = resolve_item_ids(args.item_ids, args)
    copied = 0
    missing = 0
    for item_id in item_ids:
        if copy_icon(args.reference, args.output, item_id):
            copied += 1
        else:
            missing += 1

    coin = args.output / "item_995.png"
    if coin.exists():
        for quantity in COIN_VISUAL_QUANTITIES:
            shutil.copy2(coin, args.output / f"item_995_{quantity}.png")

    variants = (
        write_stack_variants(args.stacked_items, args.variants_output)
        if args.stacked_items is not None else 0
    )
    print(f"copied {copied} item icons to {args.output}")
    if missing:
        print(f"missing {missing} requested item icons")
    if variants:
        print(f"wrote {variants} stack icon variants to {args.variants_output}")
    if args.fail_on_missing and missing:
        return 1
    return 0 if copied > 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
