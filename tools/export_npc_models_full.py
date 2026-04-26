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
REF = Path("/home/joe/projects/runescape-rl-reference")
DATA_OSRS = REF / "data_osrs"
SCRIPTS = REF / "valo_envs/ocean/osrs/scripts"
MODEL_DUMP = REF / "model_dump/osrs-dumps/config/dump.npc"
DEFAULT_CACHE = REF / "current_fightcaves_demo/data/cache"

sys.path.insert(0, str(SCRIPTS))
sys.path.insert(0, str(Path(__file__).parent))

from dat2_cache_reader import Dat2CacheReader  # noqa: E402
from export_models import (  # noqa: E402
    ModelData,
    _decode_face_colors,
    _decode_face_priorities,
    _decode_face_textures_from_stream,
    _decode_faces,
    _decode_vertex_skins,
    _decode_vertices,
    _read_ubyte,
    _read_ushort,
    decode_model as ref_decode_model,
    expand_model,
    load_model_modern,
)
from export_npc_defs_full import iter_npc_dump, parse_int  # noqa: E402

NDEF_MAGIC = 0x4E444546
MDL2_MAGIC = 0x4D444C32


def decode_model(model_id: int, data: bytes) -> ModelData | None:
    """Decode model data with the b237 type-1 face-render bitfield fix."""
    if len(data) >= 23 and data[-2] == 0xFF and data[-1] == 0xFF:
        return decode_type1_b237(model_id, data)
    return ref_decode_model(model_id, data)


def decode_type1_b237(model_id: int, data: bytes) -> ModelData | None:
    """RuneLite type-1 layout; b237 uses bit 0 for face-render-type presence."""
    n = len(data)
    if n < 23:
        return None

    off = n - 23
    var9 = _read_ushort(data, off)
    var10 = _read_ushort(data, off + 2)
    var11 = _read_ubyte(data, off + 4)
    var12 = _read_ubyte(data, off + 5)
    var13 = _read_ubyte(data, off + 6)
    var14 = _read_ubyte(data, off + 7)
    var15 = _read_ubyte(data, off + 8)
    var16 = _read_ubyte(data, off + 9)
    var17 = _read_ubyte(data, off + 10)
    var18 = _read_ushort(data, off + 11)
    var19 = _read_ushort(data, off + 13)
    var20 = _read_ushort(data, off + 15)
    var21 = _read_ushort(data, off + 17)
    var22 = _read_ushort(data, off + 19)

    tex_type0 = 0
    tex_type13 = 0
    tex_type2 = 0
    for i in range(var11):
        t = data[i] if data[i] < 128 else data[i] - 256
        if t == 0:
            tex_type0 += 1
        if 1 <= t <= 3:
            tex_type13 += 1
        if t == 2:
            tex_type2 += 1

    has_face_render_types = (var12 & 1) == 1
    var26 = var11 + var9
    if has_face_render_types:
        var26 += var10

    var28 = var26
    var26 += var10

    var29 = var26
    if var13 == 255:
        var26 += var10

    var30 = var26
    if var15 == 1:
        var26 += var10

    var31 = var26
    if var17 == 1:
        var26 += var9

    var32 = var26
    if var14 == 1:
        var26 += var10

    var33 = var26
    var26 += var21

    var34 = var26
    if var16 == 1:
        var26 += var10 * 2

    var35 = var26
    var26 += var22

    var36 = var26
    var26 += var10 * 2

    var37 = var26
    var26 += var18
    var38 = var26
    var26 += var19
    var39 = var26
    var26 += var20

    # Keep offset math aligned with RuneLite even though texture coord streams
    # are not needed by RuneC's flat-color NPC meshes.
    var26 += tex_type0 * 6
    var26 += tex_type13 * 6
    var26 += tex_type13 * 6
    var26 += tex_type13 * 2
    var26 += tex_type13
    var26 += tex_type13 * 2 + tex_type2 * 2
    _ = (var30, var32, var35, var26)

    vx, vy, vz = _decode_vertices(data, var11, var37, var38, var39, var9)
    fa, fb, fc = _decode_faces(data, var33, var28, var10)
    colors = _decode_face_colors(data, var36, var10)
    face_textures = (
        _decode_face_textures_from_stream(data, var34, var10)
        if var16 == 1 else []
    )

    return ModelData(
        model_id=model_id,
        vertex_count=var9,
        face_count=var10,
        vertices_x=vx,
        vertices_y=vy,
        vertices_z=vz,
        face_a=fa,
        face_b=fb,
        face_c=fc,
        face_colors=colors,
        face_textures=face_textures,
        face_priorities=_decode_face_priorities(data, var29, var10, var13),
        vertex_skins=_decode_vertex_skins(data, var31, var9, var17),
    )


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
                "resize_h": 128,
                "resize_v": 128,
            }
    return out


def export_model(reader, npc_id: int, model_ids: list[int], visual: dict[str, Any]):
    verts_out: list[float] = []
    colors_out: list[int] = []
    base_out: list[int] = []
    skins_out: list[int] = []
    face_indices: list[int] = []
    priorities_out: list[int] = []
    base_off = 0
    face_count = 0
    loaded_parts = 0
    recolors = visual.get("recolors") or []
    resize_h = int(visual.get("resize_h") or 128)
    resize_v = int(visual.get("resize_v") or 128)

    for model_id in model_ids:
        raw = load_model_modern(reader, model_id)
        if raw is None:
            continue
        md = decode_model(model_id, raw)
        if md is None:
            continue
        loaded_parts += 1
        for i, color in enumerate(md.face_colors):
            for src, dst in recolors:
                if color == src:
                    md.face_colors[i] = dst
                    break
        if resize_h != 128 or resize_v != 128:
            for i in range(md.vertex_count):
                md.vertices_x[i] = int(md.vertices_x[i] * resize_h / 128)
                md.vertices_y[i] = int(md.vertices_y[i] * resize_v / 128)
                md.vertices_z[i] = int(md.vertices_z[i] * resize_h / 128)
        # Keep priorities in MDL2, but do not bake them as vertex offsets.
        # OSRS uses priorities for draw order; offsets create visible seams.
        face_priorities = md.face_priorities
        md.face_priorities = []
        verts, colors, _uvs = expand_model(md, tex_colors=None)
        md.face_priorities = face_priorities
        verts_out.extend(verts)
        for r, g, b, a in colors:
            colors_out.extend([r, g, b, a])
        for i in range(md.vertex_count):
            base_out.extend([md.vertices_x[i], md.vertices_y[i], md.vertices_z[i]])
            skins_out.append(md.vertex_skins[i] if i < len(md.vertex_skins) else 0)
        for i in range(md.face_count):
            face_indices.extend([
                md.face_a[i] + base_off,
                md.face_b[i] + base_off,
                md.face_c[i] + base_off,
            ])
            pri = md.face_priorities[i] if i < len(md.face_priorities) else 0
            priorities_out.append(pri)
        base_off += md.vertex_count
        face_count += md.face_count

    if not verts_out:
        return None, loaded_parts
    if (len(verts_out) // 3 > 65535 or base_off > 65535 or face_count > 65535
            or any(v < -32768 or v > 32767 for v in base_out)
            or any(i < 0 or i > 65535 for i in face_indices)):
        return "oversized", loaded_parts
    return {
        "id": npc_id,
        "expanded_verts": verts_out,
        "colors": colors_out,
        "base_verts": base_out,
        "skins": skins_out,
        "face_indices": face_indices,
        "priorities": priorities_out,
        "face_count": face_count,
        "base_vert_count": base_off,
    }, loaded_parts


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
    args = ap.parse_args()

    defs = read_ndef_models(args.defs)
    visuals = dump_visuals(MODEL_DUMP)
    linked = [(i, d) for i, d in sorted(defs.items()) if d["models"]]
    if args.limit > 0:
        linked = linked[:args.limit]

    reader = Dat2CacheReader(args.cache)
    models = []
    missing_parts = []
    empty = []
    oversized = []
    for idx, (npc_id, rec) in enumerate(linked, 1):
        model, loaded_parts = export_model(reader, npc_id, rec["models"],
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

    write_models(args.out, models)
    lines = [
        "Full NPC model export",
        "",
        f"defs read: {len(defs)}",
        f"linked defs considered: {len(linked)}",
        f"renderable meshes exported: {len(models)}",
        f"empty after model load/decode: {len(empty)}",
        f"oversized for MDL2 u16 shape: {len(oversized)}",
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
