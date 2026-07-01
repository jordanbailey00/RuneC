#!/usr/bin/env python3
"""Export renderable NPC meshes for the broad NDEF v3 corpus."""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
from legacy_external_source_paths import DATA_OSRS, MODEL_DUMP, OSRS_DUMPS
from source_paths import CACHE_DIR, require_cache_dir

PIPELINE = Path(__file__).resolve().parent / "cache_pipeline"
MODEL_DUMP_ROOT = MODEL_DUMP if MODEL_DUMP.is_dir() else OSRS_DUMPS
NPC_DUMP = MODEL_DUMP_ROOT / "config/dump.npc"
DEFAULT_CACHE = CACHE_DIR

sys.path.insert(0, str(PIPELINE))
sys.path.insert(0, str(Path(__file__).parent))

from rc_cache import (  # noqa: E402
    ModelData,
    CONFIG_NPC,
    RcCacheStore,
    decode_npc_definition,
    load_texture_average_colors,
    load_texture_sprites,
    load_model,
    merge_models,
    read_config_group,
    write_models_binary,
)
from export_textures import build_atlas  # noqa: E402
from export_npc_defs_full import iter_npc_dump, parse_int  # noqa: E402

NDEF_MAGIC = 0x4E444546
MDL2_MAGIC = 0x4D444C32


def read_exact(f, n: int) -> bytes:
    b = f.read(n)
    if len(b) != n:
        raise EOFError("short read")
    return b


def read_ndef_models(path: Path) -> dict[int, dict[str, Any]]:
    out: dict[int, dict[str, Any]] = {}
    with path.open("rb") as f:
        magic, version, count = struct.unpack("<III", read_exact(f, 12))
        if magic != NDEF_MAGIC or version < 3:
            raise ValueError("expected NDEF v3+")
        for _ in range(count):
            npc_id = struct.unpack("<I", read_exact(f, 4))[0]
            read_exact(f, 1 + 2 + 2 + 12 + 20)
            name_len = struct.unpack("<B", read_exact(f, 1))[0]
            name = read_exact(f, name_len).decode("latin-1", "replace")
            read_exact(f, 10)
            model_count = struct.unpack("<B", read_exact(f, 1))[0]
            models = [
                struct.unpack("<I", read_exact(f, 4))[0]
                for _ in range(model_count)
            ]
            if version >= 4:
                for _ in range(5):
                    option_len = struct.unpack("<B", read_exact(f, 1))[0]
                    read_exact(f, option_len)
            out[npc_id] = {"name": name, "models": models}
    return out


def dump_visuals(path: Path) -> dict[int, dict[str, Any]]:
    out: dict[int, dict[str, Any]] = {}
    if path.is_file():
        for npc_id, _symbol, fields in iter_npc_dump(path):
            recolors = []
            for i in range(1, 256):
                src = parse_int((fields.get(f"recol{i}s") or [None])[0])
                dst = parse_int((fields.get(f"recol{i}d") or [None])[0])
                if src is None or dst is None:
                    if i > 32:
                        break
                    continue
                recolors.append((src, dst))
            resize_h = parse_int((fields.get("resizeh") or [None])[0]) or 128
            resize_v = parse_int((fields.get("resizev") or [None])[0]) or 128
            out[npc_id] = {
                "recolors": recolors,
                "retextures": [],
                "resize_h": resize_h,
                "resize_v": resize_v,
            }
    for npc_path in sorted((DATA_OSRS / "npcids").glob("npcid=*.json")):
        for rec in json.loads(npc_path.read_text()):
            npc_id = int(rec["id"])
            if npc_id in out:
                continue
            recolors = [
                (int(r["original"]), int(r["replacement"]))
                for r in rec.get("colourReplacements", [])
                if "original" in r and "replacement" in r
            ]
            out[npc_id] = {
                "recolors": recolors,
                "retextures": [],
                "resize_h": 128,
                "resize_v": 128,
            }
    return out


def cache_visuals(cache_path: Path) -> dict[int, dict[str, Any]]:
    out: dict[int, dict[str, Any]] = {}
    if not cache_path.is_dir():
        return out
    files = read_config_group(RcCacheStore(cache_path), CONFIG_NPC)
    for npc_id, data in files.items():
        d = decode_npc_definition(npc_id, data)
        if not d.complete:
            continue
        out[npc_id] = {
            "recolors": list(zip(d.recolor_from, d.recolor_to)),
            "retextures": list(zip(d.retexture_from, d.retexture_to)),
            "resize_h": d.width_scale,
            "resize_v": d.height_scale,
        }
    return out


def export_model(store: RcCacheStore, npc_id: int, model_ids: list[int], visual: dict[str, Any]):
    loaded_parts = 0
    recolors = visual.get("recolors") or []
    retextures = visual.get("retextures") or []
    resize_h = int(visual.get("resize_h") or 128)
    resize_v = int(visual.get("resize_v") or 128)
    parts: list[ModelData] = []

    for model_id in model_ids:
        md = load_model(store, model_id)
        if md is None:
            continue
        loaded_parts += 1
        for i, color in enumerate(md.face_colors):
            for src, dst in recolors:
                if color == src:
                    md.face_colors[i] = dst
                    break
        if md.face_textures:
            for src, dst in retextures:
                for i, texture in enumerate(md.face_textures):
                    if texture == src:
                        md.face_textures[i] = dst
        if resize_h != 128 or resize_v != 128:
            for i in range(md.vertex_count):
                md.vertices_x[i] = int(md.vertices_x[i] * resize_h / 128)
                md.vertices_y[i] = int(md.vertices_y[i] * resize_v / 128)
                md.vertices_z[i] = int(md.vertices_z[i] * resize_h / 128)
        if len(md.vertex_skins) != md.vertex_count:
            md.vertex_skins = [0] * md.vertex_count
        parts.append(md)

    if not parts:
        return None, loaded_parts
    merged = parts[0] if len(parts) == 1 else merge_models(parts)
    merged.model_id = npc_id

    if (merged.face_count * 3 > 65535
            or merged.vertex_count > 65535
            or merged.face_count > 65535
            or any(v < -32768 or v > 32767 for v in merged.vertices_x)
            or any(v < -32768 or v > 32767 for v in merged.vertices_y)
            or any(v < -32768 or v > 32767 for v in merged.vertices_z)
            or any(i < 0 or i > 65535 for i in merged.face_a)
            or any(i < 0 or i > 65535 for i in merged.face_b)
            or any(i < 0 or i > 65535 for i in merged.face_c)):
        return "oversized", loaded_parts
    return merged, loaded_parts


def write_models(path: Path, models: list[dict[str, Any]]):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as f:
        f.write(struct.pack("<II", MDL2_MAGIC, len(models)))
        offsets_pos = f.tell()
        f.write(b"\0\0\0\0" * len(models))
        offsets = []
        for model in models:
            offsets.append(f.tell())
            evc = len(model["expanded_verts"]) // 3
            f.write(struct.pack("<IHHH", model["id"], evc, model["face_count"],
                                model["base_vert_count"]))
            for v in model["expanded_verts"]:
                f.write(struct.pack("<f", float(v)))
            for c in model["colors"]:
                f.write(struct.pack("B", int(c) & 0xFF))
            for v in model["base_verts"]:
                f.write(struct.pack("<h", int(v)))
            for skin in model["skins"]:
                f.write(struct.pack("B", int(skin) & 0xFF))
            for idx in model["face_indices"]:
                f.write(struct.pack("<H", int(idx)))
            for pri in model["priorities"]:
                f.write(struct.pack("B", int(pri) & 0xFF))
        end = f.tell()
        f.seek(offsets_pos)
        for off in offsets:
            f.write(struct.pack("<I", off))
        f.seek(end)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", type=Path, default=DEFAULT_CACHE)
    ap.add_argument("--defs", type=Path, default=ROOT / "data/defs/npc_defs.bin")
    ap.add_argument("--out", type=Path, default=ROOT / "data/models/npcs.models")
    ap.add_argument("--report", type=Path,
                    default=ROOT / "tools/reports/npc_models_full.txt")
    ap.add_argument("--limit", type=int, default=0,
                    help="debug limit; 0 exports all linked NPCs")
    ap.add_argument("--model-lighting", choices=("client", "unlit"),
                    default="unlit",
                    help=("vertex color lighting mode for exported NPC models; "
                          "default stays unlit until client-lit broad exports "
                          "are visually approved"))
    args = ap.parse_args()

    defs = read_ndef_models(args.defs)
    visuals = dump_visuals(NPC_DUMP)
    cache_dir = require_cache_dir(args.cache)
    visuals.update(cache_visuals(cache_dir))
    linked = [(i, d) for i, d in sorted(defs.items()) if d["models"]]
    if args.limit > 0:
        linked = linked[:args.limit]

    store = RcCacheStore(cache_dir)
    tex_colors = load_texture_average_colors(store)
    atlas = build_atlas(load_texture_sprites(store))
    models = []
    missing_parts = []
    empty = []
    oversized = []
    for idx, (npc_id, rec) in enumerate(linked, 1):
        model, loaded_parts = export_model(store, npc_id, rec["models"],
                                           visuals.get(npc_id, {}))
        if loaded_parts < len(rec["models"]):
            missing_parts.append((npc_id, rec["name"], len(rec["models"]), loaded_parts))
        if model is None:
            empty.append((npc_id, rec["name"]))
            continue
        if model == "oversized":
            oversized.append((npc_id, rec["name"]))
            continue
        models.append(model)
        if idx % 500 == 0:
            print(f"exported {len(models)}/{idx} linked NPC meshes", file=sys.stderr)

    write_models_binary(
        args.out,
        models,
        tex_colors=tex_colors,
        atlas=atlas,
        bake_priority_offsets=False,
        model_lighting=args.model_lighting,
    )
    lines = [
        "Full NPC model export",
        "",
        f"defs read: {len(defs)}",
        f"linked defs considered: {len(linked)}",
        f"renderable meshes exported: {len(models)}",
        f"empty after model load/decode: {len(empty)}",
        f"oversized for MDL3 u16 shape: {len(oversized)}",
        f"defs with at least one missing model part: {len(missing_parts)}",
        "",
        "Sample empty:",
    ]
    lines += [f"  {i}: {name}" for i, name in empty[:30]] or ["  none"]
    lines += ["", "Sample oversized:"]
    lines += [f"  {i}: {name}" for i, name in oversized[:30]] or ["  none"]
    lines += ["", "Sample partial model loads:"]
    lines += [
        f"  {i}: {name} ({loaded}/{total} parts)"
        for i, name, total, loaded in missing_parts[:30]
    ] or ["  none"]
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text("\n".join(lines) + "\n")
    print(f"wrote {len(models)} models to {args.out}")
    print(f"wrote report to {args.report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
