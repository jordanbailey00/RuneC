#!/usr/bin/env python3
"""Rewrite .models bundles with stacked-face offsets (MOF1).

write_models_binary stacks overlapping coplanar faces in client draw order
(overlay_face_offsets), bakes the offsets into the expanded vertices and
records them in a trailing MOF1 block, which the viewer re-applies after
posing. Bundles written before that carry neither. This rewrites a bundle the
same way from what it already stores: each model's expanded positions are
rebuilt from its base vertices (dropping the older uniform priority bias) with
the offsets applied, and its MOF1 block is replaced. Colors, texture
coordinates and the other blocks are copied unchanged, and a rewritten bundle
matches what write_models_binary writes for the same models.
"""

from __future__ import annotations

import argparse
import math
import os
import struct
import sys
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from export_models import (  # noqa: E402
    MDL2_MAGIC,
    MDL3_MAGIC,
    MFA1_MAGIC,
    MOF1_MAGIC,
    MUV1_MAGIC,
    ModelData,
    overlay_face_offsets,
)

BLOCK_ITEM_BYTES = {
    MUV1_MAGIC: struct.calcsize("<BxxxHHHfffff"),
    MFA1_MAGIC: struct.calcsize("<BBH"),
    MOF1_MAGIC: struct.calcsize("<HHf"),
}


@dataclass
class StackStats:
    models: int = 0
    faces: int = 0
    stacked: int = 0      # faces with an offset
    moved: int = 0        # faces whose expanded positions changed
    unbiased: int = 0     # moved faces without an offset (old priority bias)
    degenerate: int = 0   # zero triangles kept for out-of-range faces
    changed_models: list[int] = field(default_factory=list)

    def add(self, other: "StackStats") -> None:
        self.models += other.models
        self.faces += other.faces
        self.stacked += other.stacked
        self.moved += other.moved
        self.unbiased += other.unbiased
        self.degenerate += other.degenerate
        self.changed_models.extend(other.changed_models)


def _face_positions(base: tuple[int, ...], a: int, b: int, c: int,
                    offset: float) -> list[float]:
    """Expanded positions of one face as expand_model writes them."""
    pts = [float(v) for i in (a, b, c)
           for v in (base[i * 3], -base[i * 3 + 1], base[i * 3 + 2])]
    if offset > 0.0:
        e1 = [pts[3 + k] - pts[k] for k in range(3)]
        e2 = [pts[6 + k] - pts[k] for k in range(3)]
        n = (e1[1] * e2[2] - e1[2] * e2[1],
             e1[2] * e2[0] - e1[0] * e2[2],
             e1[0] * e2[1] - e1[1] * e2[0])
        length = math.sqrt(sum(c * c for c in n))
        if length > 0.001:
            bias = offset / length
            for k in range(9):
                pts[k] -= n[k % 3] * bias
    return pts


def stack_model_record(record: bytes, textured: bool) -> tuple[bytes, StackStats]:
    """Return one model record rewritten with stacked-face offsets."""
    model_id, evc, fc, bvc = struct.unpack_from("<IHHH", record, 0)
    if evc != fc * 3:
        raise ValueError(f"model {model_id}: {evc} expanded vertices for {fc} faces")
    pos = 10
    verts = record[pos:pos + evc * 12]
    pos += evc * 12
    colors_at = pos
    pos += evc * 4 + (evc * 8 if textured else 0)
    base = struct.unpack_from(f"<{bvc * 3}h", record, pos)
    pos += bvc * 6 + bvc
    faces = struct.unpack_from(f"<{fc * 3}H", record, pos)
    pos += fc * 6
    priorities = record[pos:pos + fc]
    pos += fc
    core_end = pos

    blocks = []
    while pos + 8 <= len(record):
        magic, count = struct.unpack_from("<II", record, pos)
        item = BLOCK_ITEM_BYTES.get(magic)
        if item is None:
            raise ValueError(f"model {model_id}: unknown block {magic:#010x}")
        end = pos + 8 + count * item
        if magic != MOF1_MAGIC:
            blocks.append(record[pos:end])
        pos = end
    if pos != len(record):
        raise ValueError(f"model {model_id}: {len(record) - pos} stray bytes")

    def hidden(fi: int) -> bool:
        # expand_model gives skipped faces (render type 2, alpha -1) alpha 0
        # and writes faces with out-of-range indices as zero triangles.
        return (all(record[colors_at + (fi * 3 + k) * 4 + 3] == 0 for k in range(3))
                or not any(verts[fi * 36:fi * 36 + 36]))

    model = ModelData(
        model_id=model_id,
        vertex_count=bvc,
        face_count=fc,
        vertices_x=list(base[0::3]),
        vertices_y=list(base[1::3]),
        vertices_z=list(base[2::3]),
        face_a=list(faces[0::3]),
        face_b=list(faces[1::3]),
        face_c=list(faces[2::3]),
        face_priorities=list(priorities),
        face_alphas=[0xFF if hidden(fi) else 0 for fi in range(fc)],
    )
    offsets = overlay_face_offsets(model)

    stats = StackStats(models=1, faces=fc)
    out_verts = bytearray(verts)
    for fi in range(fc):
        a, b, c = faces[fi * 3:fi * 3 + 3]
        if max(a, b, c) >= bvc:
            continue
        old = verts[fi * 36:fi * 36 + 36]
        new = struct.pack("<9f", *_face_positions(base, a, b, c, offsets[fi]))
        if offsets[fi] > 0.0:
            stats.stacked += 1
        if new == old:
            continue
        if not any(old):
            # expand_model writes a zero triangle for an out-of-range face.
            stats.degenerate += 1
            continue
        out_verts[fi * 36:fi * 36 + 36] = new
        stats.moved += 1
        if offsets[fi] == 0.0:
            stats.unbiased += 1

    stacked = [(fi, offset) for fi, offset in enumerate(offsets) if offset > 0.0]
    mof1 = b""
    if stacked:
        mof1 = struct.pack("<II", MOF1_MAGIC, len(stacked)) + b"".join(
            struct.pack("<HHf", fi, 0, offset) for fi, offset in stacked)
    rewritten = b"".join((record[:10], bytes(out_verts),
                          record[colors_at:core_end], *blocks, mof1))
    if rewritten != record:
        stats.changed_models.append(model_id)
    return rewritten, stats


def _stack_slice(args: tuple[str, bool, list[tuple[int, int]]]) -> tuple[list[bytes], StackStats]:
    path, textured, spans = args
    stats = StackStats()
    records = []
    with open(path, "rb") as f:
        for start, end in spans:
            f.seek(start)
            record, model_stats = stack_model_record(f.read(end - start), textured)
            records.append(record)
            stats.add(model_stats)
    return records, stats


def stack_bundle(path: Path, write: bool = True, jobs: int = 1) -> StackStats:
    """Rewrite one bundle in place; return what changed."""
    size = path.stat().st_size
    with open(path, "rb") as f:
        magic, count = struct.unpack("<II", f.read(8))
        if magic not in (MDL2_MAGIC, MDL3_MAGIC):
            raise ValueError(f"{path}: not an MDL2/MDL3 bundle")
        offsets = list(struct.unpack(f"<{count}I", f.read(count * 4)))
    spans = [(start, offsets[i + 1] if i + 1 < count else size)
             for i, start in enumerate(offsets)]
    textured = magic == MDL3_MAGIC
    chunk = max(1, min(256, len(spans) // max(1, jobs * 8) or 1))
    work = [(str(path), textured, spans[i:i + chunk])
            for i in range(0, len(spans), chunk)]

    stats = StackStats()
    temporary = path.with_name(f".{path.name}.stacking.tmp")
    out = open(temporary, "wb") if write else None
    try:
        new_offsets = []
        cursor = 8 + count * 4
        if out:
            out.write(struct.pack("<II", magic, count))
            out.write(b"\x00" * (count * 4))

        def consume(results) -> None:
            nonlocal cursor
            for records, slice_stats in results:
                stats.add(slice_stats)
                for record in records:
                    new_offsets.append(cursor)
                    cursor += len(record)
                    if out:
                        out.write(record)

        if jobs <= 1:
            consume(map(_stack_slice, work))
        else:
            with ProcessPoolExecutor(max_workers=jobs) as pool:
                consume(pool.map(_stack_slice, work))
        if out:
            out.seek(8)
            out.write(struct.pack(f"<{count}I", *new_offsets))
            out.close()
            out = None
            if stats.changed_models:
                os.replace(temporary, path)
            else:
                temporary.unlink()
    finally:
        if out:
            out.close()
            temporary.unlink(missing_ok=True)
    return stats


def _stack_whole_bundle(args: tuple[Path, bool]) -> StackStats:
    path, write = args
    return stack_bundle(path, write=write)


def bundle_paths(paths: list[Path]) -> list[Path]:
    found = []
    for path in paths:
        found.extend(sorted(path.rglob("*.models")) if path.is_dir() else [path])
    return found


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="add stacked-face offsets (MOF1) to .models bundles in place")
    parser.add_argument("paths", nargs="+", type=Path,
                        help=".models bundles, or directories searched for them")
    parser.add_argument("--check", action="store_true",
                        help="report what would change without writing")
    parser.add_argument("--jobs", type=int, default=1,
                        help="worker processes: across bundles when given "
                             "several, else within the bundle (default: 1)")
    args = parser.parse_args(argv)

    total = StackStats()
    bundles = bundle_paths(args.paths)
    changed_bundles = 0
    if args.jobs > 1 and len(bundles) > 1:
        pool = ProcessPoolExecutor(max_workers=args.jobs)
        results = pool.map(_stack_whole_bundle,
                           [(path, not args.check) for path in bundles],
                           chunksize=32)
    else:
        pool = None
        results = (stack_bundle(path, write=not args.check, jobs=args.jobs)
                   for path in bundles)
    for path, stats in zip(bundles, results):
        total.add(stats)
        changed_bundles += bool(stats.changed_models)
        if stats.changed_models and len(bundles) <= 20:
            print(f"{path}: {len(stats.changed_models)}/{stats.models} models, "
                  f"{stats.stacked} stacked faces, {stats.moved} moved "
                  f"({stats.unbiased} unbiased), {stats.degenerate} degenerate kept")
    if pool:
        pool.shutdown()
    verb = "would change" if args.check else "changed"
    print(f"{verb} {changed_bundles}/{len(bundles)} bundles: "
          f"{len(total.changed_models)}/{total.models} models, "
          f"{total.stacked}/{total.faces} faces stacked, {total.moved} moved "
          f"({total.unbiased} unbiased), {total.degenerate} degenerate kept")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
