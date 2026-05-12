#!/usr/bin/env python3
"""Emit object/interactable definitions from the local b237 cache.

Output: data/defs/object_defs.bin ('ODEF' v2).
This is gameplay metadata. Region *.objects remains render mesh data.
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
import time
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

from source_paths import CACHE_DIR

PIPELINE = Path(__file__).resolve().parent / "cache_pipeline"
sys.path.insert(0, str(PIPELINE))

from rc_cache import (  # noqa: E402
    CONFIG_OBJECT,
    INDEX_CONFIGS,
    RcCacheStore,
    decode_location_definition,
)

ROOT = Path(__file__).resolve().parents[1]
WIKI = ROOT / "tools/wiki_cache"
OUT = ROOT / "data/defs/object_defs.bin"
REPORT = ROOT / "tools/reports/object_defs.txt"

ODEF_MAGIC = 0x4645444F  # ODEF
ODEF_VERSION = 2

FLAG_CACHE_DEF = 1 << 0
FLAG_WIKI_OBJECT_ID = 1 << 1
FLAG_WIKI_SCENERY = 1 << 2
FLAG_HAS_MODELS = 1 << 3
FLAG_HAS_ACTIONS = 1 << 4
FLAG_SOLID = 1 << 5
FLAG_IMPENETRABLE = 1 << 6
FLAG_OBSTRUCTS_GROUND = 1 << 7
FLAG_TRANSFORMS = 1 << 8
FLAG_ROTATED = 1 << 9
FLAG_CONTOURED = 1 << 10
FLAG_HAS_MAP_ICON = 1 << 11
FLAG_HAS_CATEGORY = 1 << 12
FLAG_INTERACTIVE = 1 << 13
FLAG_BLOCKS_PROJECTILE = 1 << 14
FLAG_MODEL_CLIPPED = 1 << 15
FLAG_HOLLOW = 1 << 16
FLAG_RANDOMIZE_ANIM_START = 1 << 17
FLAG_DEFER_ANIM_CHANGE = 1 << 18
FLAG_HAS_PARAMS = 1 << 19
FLAG_HAS_AMBIENT_SOUND = 1 << 20

CLIP_BLOCKS_PROJECTILE = 1 << 0
CLIP_MODEL_CLIPPED = 1 << 1
CLIP_HOLLOW = 1 << 2
CLIP_RANDOMIZE_ANIM_START = 1 << 3
CLIP_DEFER_ANIM_CHANGE = 1 << 4


@dataclass
class ObjParam:
    key: int
    value: int

@dataclass
class ObjDef:
    obj_id: int
    name: str = ""
    width: int = 1
    length: int = 1
    interact_type: int = 2
    solid: bool = True
    impenetrable: bool = True
    blocks_projectile: bool = True
    model_clipped: bool = False
    obstructs_ground: bool = False
    hollow: bool = False
    rotated: bool = False
    contoured: bool = False
    animation_id: int = -1
    map_icon: int = -1
    category: int = -1
    force_approach: int = 0
    supports_items: int = -1
    ambient_sound_id: int = -1
    ambient_sound_distance: int = 0
    ambient_sound_retain: int = 0
    varbit: int = -1
    varp: int = -1
    actions: list[str] = field(default_factory=lambda: [""] * 5)
    model_ids: list[int] = field(default_factory=list)
    transforms: list[int] = field(default_factory=list)
    params: list[ObjParam] = field(default_factory=list)
    randomize_anim_start: bool = False
    defer_anim_change: bool = False
    source_flags: int = FLAG_CACHE_DEF


def convert_location_def(loc) -> ObjDef:
    return ObjDef(
        obj_id=loc.loc_id,
        name=loc.name,
        width=loc.width,
        length=loc.length,
        interact_type=loc.interact_type,
        solid=loc.solid,
        impenetrable=loc.impenetrable,
        blocks_projectile=loc.blocks_projectile,
        model_clipped=loc.model_clipped,
        obstructs_ground=loc.obstructs_ground,
        hollow=loc.hollow,
        rotated=loc.rotated,
        contoured=loc.contoured,
        animation_id=loc.animation_id,
        map_icon=loc.map_icon,
        category=loc.category,
        force_approach=loc.force_approach,
        supports_items=loc.supports_items,
        ambient_sound_id=loc.ambient_sound_id,
        ambient_sound_distance=loc.ambient_sound_distance,
        ambient_sound_retain=loc.ambient_sound_retain,
        varbit=loc.varbit,
        varp=loc.varp,
        actions=list(loc.actions),
        model_ids=list(loc.model_ids),
        transforms=list(loc.transforms),
        params=[
            ObjParam(p.key, p.int_value)
            for p in loc.params
            if not p.is_string
        ],
        randomize_anim_start=loc.randomize_anim_start,
        defer_anim_change=loc.defer_anim_change,
    )


def decode_cache(cache_dir: Path) -> tuple[list[ObjDef], Counter[int]]:
    files = RcCacheStore(cache_dir).read_group(INDEX_CONFIGS, CONFIG_OBJECT)
    unknown: Counter[int] = Counter()
    rows = []
    for obj_id, data in files.items():
        loc = decode_location_definition(obj_id, data)
        if loc.unknown_opcode is not None:
            unknown[loc.unknown_opcode] += 1
        rows.append(convert_location_def(loc))
    return sorted(rows, key=lambda r: r.obj_id), unknown


def read_wiki_ids(bucket: str) -> tuple[set[int], int, int]:
    ids: set[int] = set()
    pages: set[str] = set()
    rows = 0
    for path in sorted(WIKI.glob(f"{bucket}_*.json")):
        data = json.loads(path.read_text())
        for row in data.get("bucket", []):
            rows += 1
            page = row.get("page_name")
            if page:
                pages.add(str(page))
            raw_ids = row.get("id") if bucket == "object_id" else row.get("object_id")
            if not isinstance(raw_ids, list):
                raw_ids = [raw_ids]
            for raw in raw_ids:
                try:
                    ids.add(int(raw))
                except (TypeError, ValueError):
                    pass
    return ids, rows, len(pages)


def flags_for(row: ObjDef) -> int:
    flags = row.source_flags
    if row.model_ids:
        flags |= FLAG_HAS_MODELS
    if any(row.actions):
        flags |= FLAG_HAS_ACTIONS
    if row.solid:
        flags |= FLAG_SOLID
    if row.impenetrable:
        flags |= FLAG_IMPENETRABLE
    if row.obstructs_ground:
        flags |= FLAG_OBSTRUCTS_GROUND
    if row.transforms:
        flags |= FLAG_TRANSFORMS
    if row.rotated:
        flags |= FLAG_ROTATED
    if row.contoured:
        flags |= FLAG_CONTOURED
    if row.map_icon >= 0:
        flags |= FLAG_HAS_MAP_ICON
    if row.category >= 0:
        flags |= FLAG_HAS_CATEGORY
    if row.interact_type > 0 or any(row.actions):
        flags |= FLAG_INTERACTIVE
    if row.blocks_projectile:
        flags |= FLAG_BLOCKS_PROJECTILE
    if row.model_clipped:
        flags |= FLAG_MODEL_CLIPPED
    if row.hollow:
        flags |= FLAG_HOLLOW
    if row.randomize_anim_start:
        flags |= FLAG_RANDOMIZE_ANIM_START
    if row.defer_anim_change:
        flags |= FLAG_DEFER_ANIM_CHANGE
    if row.params:
        flags |= FLAG_HAS_PARAMS
    if row.ambient_sound_id >= 0:
        flags |= FLAG_HAS_AMBIENT_SOUND
    return flags


def clip_flags_for(row: ObjDef) -> int:
    flags = 0
    if row.blocks_projectile:
        flags |= CLIP_BLOCKS_PROJECTILE
    if row.model_clipped:
        flags |= CLIP_MODEL_CLIPPED
    if row.hollow:
        flags |= CLIP_HOLLOW
    if row.randomize_anim_start:
        flags |= CLIP_RANDOMIZE_ANIM_START
    if row.defer_anim_change:
        flags |= CLIP_DEFER_ANIM_CHANGE
    return flags


def write_pstr(f, value: str, limit: int = 65535) -> None:
    raw = value.encode("latin-1", errors="replace")[:limit]
    f.write(struct.pack("<H", len(raw)))
    f.write(raw)


def write_bin(rows: list[ObjDef]) -> None:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", ODEF_MAGIC, ODEF_VERSION, len(rows)))
        for row in rows:
            actions = row.actions[:5] + [""] * max(0, 5 - len(row.actions))
            models = row.model_ids[:255]
            transforms = row.transforms[:255]
            params = row.params[:65535]
            f.write(struct.pack(
                "<IHHBBBBBiiiII",
                row.obj_id,
                row.width & 0xFFFF,
                row.length & 0xFFFF,
                row.interact_type & 0xFF,
                sum(1 for a in actions if a) & 0xFF,
                len(models) & 0xFF,
                len(transforms) & 0xFF,
                row.force_approach & 0xFF,
                row.varbit,
                row.varp,
                row.animation_id,
                row.map_icon & 0xFFFFFFFF,
                flags_for(row),
            ))
            f.write(struct.pack(
                "<BBHiHH",
                (row.supports_items if row.supports_items >= 0 else 255) & 0xFF,
                clip_flags_for(row) & 0xFF,
                len(params) & 0xFFFF,
                row.ambient_sound_id,
                row.ambient_sound_distance & 0xFFFF,
                row.ambient_sound_retain & 0xFFFF,
            ))
            write_pstr(f, row.name)
            for action in actions[:5]:
                write_pstr(f, action, 255)
            for model_id in models:
                f.write(struct.pack("<I", model_id & 0xFFFFFFFF))
            for target in transforms:
                f.write(struct.pack("<i", target))
            for param in params:
                f.write(struct.pack("<Ii", param.key & 0xFFFFFFFF,
                                    param.value))


def write_report(rows: list[ObjDef], unknown: Counter[int],
                 wiki_object_ids: set[int], wiki_scenery_ids: set[int],
                 wiki_object_rows: int, wiki_object_pages: int,
                 wiki_scenery_rows: int, wiki_scenery_pages: int,
                 elapsed_ms: float) -> None:
    cache_ids = {row.obj_id for row in rows}
    action_counts: Counter[str] = Counter()
    for row in rows:
        for action in row.actions:
            if action:
                action_counts[action] += 1

    lines = [
        "Object definition / interactable catalog",
        "",
        f"status: READY_WITH_ACCEPTED_SIMPLIFICATIONS",
        f"output binary: data/defs/object_defs.bin ({OUT.stat().st_size} bytes)",
        f"cache source: tools/cache_pipeline/source/current_fightcaves_demo/data/cache",
        "OpenRS2 layout reference: https://archive.openrs2.org/api",
        f"elapsed_ms: {elapsed_ms:.2f}",
        "",
        "counts:",
        f"  cache object defs:             {len(rows)}",
        f"  wiki object_id rows/pages:     {wiki_object_rows}/{wiki_object_pages}",
        f"  wiki object_id unique ids:     {len(wiki_object_ids)}",
        f"  wiki scenery rows/pages:       {wiki_scenery_rows}/{wiki_scenery_pages}",
        f"  wiki scenery unique ids:       {len(wiki_scenery_ids)}",
        f"  wiki ids missing cache defs:   {len((wiki_object_ids | wiki_scenery_ids) - cache_ids)}",
        f"  cache ids missing wiki ids:    {len(cache_ids - (wiki_object_ids | wiki_scenery_ids))}",
        "",
        "cache feature coverage:",
        f"  named defs:                    {sum(1 for r in rows if r.name)}",
        f"  model-linked defs:             {sum(1 for r in rows if r.model_ids)}",
        f"  action-bearing defs:           {sum(1 for r in rows if any(r.actions))}",
        f"  interactive defs:              {sum(1 for r in rows if flags_for(r) & FLAG_INTERACTIVE)}",
        f"  transform/morph defs:          {sum(1 for r in rows if r.transforms)}",
        f"  map-icon defs:                 {sum(1 for r in rows if r.map_icon >= 0)}",
        f"  category defs:                 {sum(1 for r in rows if r.category >= 0)}",
        f"  param-bearing defs:            {sum(1 for r in rows if r.params)}",
        f"  total int params:              {sum(len(r.params) for r in rows)}",
        f"  ambient-sound defs:            {sum(1 for r in rows if r.ambient_sound_id >= 0)}",
        f"  model-clipped defs:            {sum(1 for r in rows if r.model_clipped)}",
        f"  hollow defs:                   {sum(1 for r in rows if r.hollow)}",
        "",
        "top actions:",
    ]
    for action, count in action_counts.most_common(30):
        lines.append(f"  {action:<24} {count}")
    lines.extend(["", "unknown opcode warnings:"])
    if unknown:
        for opcode, count in sorted(unknown.items()):
            lines.append(f"  opcode {opcode:<3} {count}")
    else:
        lines.append("  none")
    lines.extend([
        "",
        "scope:",
        "  - binary is a definition/interactable catalog, not full region placement ownership",
        "  - int params, clip flags, ambient sound ids, transforms, actions, and model ids are retained for interaction scripts",
        "  - string params and full sound variant lists remain exporter/report-only until a runtime consumer needs them",
        "  - region render meshes remain in data/regions/*.objects and are not duplicated here",
        "",
        "remaining object/interactable parity work:",
        "  - refine uncommon object-specific consumers as exact source data lands",
        "  - add source-backed door replacement pairing when an authoritative pair table is available",
        "  - keep object definition metadata separate from renderer mesh ownership",
    ])
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text("\n".join(lines) + "\n")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", type=Path, default=CACHE_DIR)
    args = ap.parse_args()

    start = time.perf_counter()
    rows, unknown = decode_cache(args.cache)
    wiki_object_ids, wiki_object_rows, wiki_object_pages = read_wiki_ids("object_id")
    wiki_scenery_ids, wiki_scenery_rows, wiki_scenery_pages = read_wiki_ids("infobox_scenery")
    for row in rows:
        if row.obj_id in wiki_object_ids:
            row.source_flags |= FLAG_WIKI_OBJECT_ID
        if row.obj_id in wiki_scenery_ids:
            row.source_flags |= FLAG_WIKI_SCENERY
    write_bin(rows)
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    write_report(rows, unknown, wiki_object_ids, wiki_scenery_ids,
                 wiki_object_rows, wiki_object_pages,
                 wiki_scenery_rows, wiki_scenery_pages, elapsed_ms)
    print(REPORT.read_text())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
