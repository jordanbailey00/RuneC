#!/usr/bin/env python3
"""Extract a smaller viewer scene from existing RuneC runtime scene files."""

from __future__ import annotations

import argparse
import shutil
import struct
from pathlib import Path


TERR_MAGIC = 0x54455252
OBJS_MAGIC = 0x4F424A53
OBJ2_MAGIC = 0x4F424A32
OANM_MAGIC = 0x4D4E414F
OANM_VERSION = 1


def in_bounds(min_v: float, max_v: float, lower: float, upper: float) -> bool:
    return max_v >= lower and min_v <= upper


def companion_path(path: Path, suffix: str) -> Path:
    return path.with_suffix(suffix)


def output_path(prefix: Path, suffix: str) -> Path:
    return prefix.parent / f"{prefix.name}{suffix}"


def selected_triangles(
    vertices: memoryview,
    tri_count: int,
    origin_x: int,
    origin_y: int,
    width: int,
    height: int,
    pad: int,
    selection: str,
) -> list[int]:
    min_x = float(origin_x - pad)
    max_x = float(origin_x + width + pad)
    min_y = float(origin_y - pad)
    max_y = float(origin_y + height + pad)
    selected: list[int] = []
    for tri in range(tri_count):
        base = tri * 36
        x0, _y0, z0, x1, _y1, z1, x2, _y2, z2 = struct.unpack_from(
            "<fffffffff", vertices, base
        )
        tri_min_x = min(x0, x1, x2)
        tri_max_x = max(x0, x1, x2)
        wy0 = -z0
        wy1 = -z1
        wy2 = -z2
        tri_min_y = min(wy0, wy1, wy2)
        tri_max_y = max(wy0, wy1, wy2)
        if selection == "centroid":
            cx = (x0 + x1 + x2) / 3.0
            cy = (wy0 + wy1 + wy2) / 3.0
            if min_x <= cx < max_x and min_y <= cy < max_y:
                selected.append(tri)
        elif in_bounds(tri_min_x, tri_max_x, min_x, max_x) and in_bounds(
            tri_min_y, tri_max_y, min_y, max_y
        ):
            selected.append(tri)
    return selected


def write_section(out, data: memoryview, selected: list[int], stride: int) -> None:
    tri_stride = stride * 3
    for tri in selected:
        base = tri * tri_stride
        out.write(data[base : base + tri_stride])


def extract_terrain(
    src: Path,
    dst: Path,
    origin_x: int,
    origin_y: int,
    width: int,
    height: int,
    pad: int,
    selection: str,
) -> None:
    data = src.read_bytes()
    view = memoryview(data)
    magic, vert_count, region_count, _min_wx, _min_wy = struct.unpack_from(
        "<IIIii", view, 0
    )
    if magic != TERR_MAGIC:
        raise SystemExit(f"bad terrain magic: {src}")
    if vert_count % 3 != 0:
        raise SystemExit(f"terrain vertex count is not triangle aligned: {src}")

    vertices_off = 20
    vertices_size = vert_count * 12
    colors_off = vertices_off + vertices_size
    colors_size = vert_count * 4
    hm_off = colors_off + colors_size
    vertices = view[vertices_off:colors_off]
    colors = view[colors_off:hm_off]
    selected = selected_triangles(
        vertices,
        vert_count // 3,
        origin_x,
        origin_y,
        width,
        height,
        pad,
        selection,
    )

    dst.parent.mkdir(parents=True, exist_ok=True)
    with dst.open("wb") as out:
        out.write(
            struct.pack(
                "<IIIii",
                TERR_MAGIC,
                len(selected) * 3,
                region_count,
                origin_x,
                origin_y,
            )
        )
        write_section(out, vertices, selected, 12)
        write_section(out, colors, selected, 4)
        if len(view) >= hm_off + 16:
            hm_x, hm_y, hm_w, hm_h = struct.unpack_from("<iiII", view, hm_off)
            values_off = hm_off + 16
            values = view[values_off : values_off + hm_w * hm_h * 4]
            out_w = width + 1
            out_h = height + 1
            out.write(struct.pack("<iiII", origin_x, origin_y, out_w, out_h))
            for y in range(origin_y, origin_y + out_h):
                for x in range(origin_x, origin_x + out_w):
                    sx = x - hm_x
                    sy = y - hm_y
                    if 0 <= sx < hm_w and 0 <= sy < hm_h:
                        base = (sx + sy * hm_w) * 4
                        out.write(values[base : base + 4])
                    else:
                        out.write(struct.pack("<f", -2.0))
    print(f"wrote {dst}: {len(selected) * 3} terrain verts")


def extract_objects(
    src: Path,
    dst: Path,
    origin_x: int,
    origin_y: int,
    width: int,
    height: int,
    pad: int,
    selection: str,
) -> None:
    data = src.read_bytes()
    view = memoryview(data)
    magic, _placement_count, _min_wx, _min_wy, vert_count = struct.unpack_from(
        "<IIiiI", view, 0
    )
    if magic not in (OBJS_MAGIC, OBJ2_MAGIC):
        raise SystemExit(f"bad objects magic: {src}")
    if vert_count % 3 != 0:
        raise SystemExit(f"object vertex count is not triangle aligned: {src}")

    vertices_off = 20
    vertices_size = vert_count * 12
    colors_off = vertices_off + vertices_size
    colors_size = vert_count * 4
    tex_off = colors_off + colors_size
    tex_size = vert_count * 8 if magic == OBJ2_MAGIC else 0
    vertices = view[vertices_off:colors_off]
    colors = view[colors_off:tex_off]
    texcoords = view[tex_off : tex_off + tex_size] if tex_size else None
    selected = selected_triangles(
        vertices,
        vert_count // 3,
        origin_x,
        origin_y,
        width,
        height,
        pad,
        selection,
    )

    dst.parent.mkdir(parents=True, exist_ok=True)
    with dst.open("wb") as out:
        out.write(
            struct.pack(
                "<IIiiI",
                magic,
                len(selected),
                origin_x,
                origin_y,
                len(selected) * 3,
            )
        )
        write_section(out, vertices, selected, 12)
        write_section(out, colors, selected, 4)
        if texcoords is not None:
            write_section(out, texcoords, selected, 8)
    print(f"wrote {dst}: {len(selected) * 3} object verts")


def copy_if_present(src: Path, dst: Path) -> None:
    if src.is_file():
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dst)
        print(f"copied {dst}")


def extract_oanim(
    src: Path,
    dst: Path,
    origin_x: int,
    origin_y: int,
    width: int,
    height: int,
    pad: int,
) -> None:
    if not src.is_file():
        return
    data = src.read_bytes()
    view = memoryview(data)
    if len(view) < 12:
        return
    magic, version, count = struct.unpack_from("<III", view, 0)
    if magic != OANM_MAGIC or version != OANM_VERSION:
        return
    row_size = 40
    min_x = origin_x - pad
    max_x = origin_x + width + pad
    min_y = origin_y - pad
    max_y = origin_y + height + pad
    rows: list[memoryview] = []
    for idx in range(count):
        base = 12 + idx * row_size
        row = view[base : base + row_size]
        if len(row) != row_size:
            break
        world_x, world_y = struct.unpack_from("<ii", row, 12)
        if min_x <= world_x <= max_x and min_y <= world_y <= max_y:
            rows.append(row)
    dst.parent.mkdir(parents=True, exist_ok=True)
    with dst.open("wb") as out:
        out.write(struct.pack("<III", OANM_MAGIC, OANM_VERSION, len(rows)))
        for row in rows:
            out.write(row)
    print(f"wrote {dst}: {len(rows)} object anim rows")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="extract a smaller viewer scene from existing runtime files"
    )
    parser.add_argument("--terrain", type=Path, required=True)
    parser.add_argument("--objects", type=Path, required=True)
    parser.add_argument("--output-prefix", type=Path, required=True)
    parser.add_argument("--origin-x", type=int, required=True)
    parser.add_argument("--origin-y", type=int, required=True)
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    parser.add_argument("--pad-tiles", type=int, default=8)
    parser.add_argument(
        "--selection",
        choices=("overlap", "centroid"),
        default="overlap",
        help=(
            "Triangle selection mode. Use centroid for tiled chunk grids so "
            "each triangle is assigned to exactly one chunk."
        ),
    )
    parser.add_argument("--skip-terrain", action="store_true")
    parser.add_argument("--skip-companions", action="store_true")
    parser.add_argument("--skip-oanim", action="store_true")
    args = parser.parse_args()

    if args.width <= 0 or args.height <= 0:
        raise SystemExit("--width and --height must be positive")
    if args.pad_tiles < 0:
        raise SystemExit("--pad-tiles must be non-negative")

    terrain_out = output_path(args.output_prefix, ".terrain")
    objects_out = output_path(args.output_prefix, ".objects")
    if not args.skip_terrain:
        extract_terrain(
            args.terrain,
            terrain_out,
            args.origin_x,
            args.origin_y,
            args.width,
            args.height,
            args.pad_tiles,
            args.selection,
        )
    extract_objects(
        args.objects,
        objects_out,
        args.origin_x,
        args.origin_y,
        args.width,
        args.height,
        args.pad_tiles,
        args.selection,
    )
    if not args.skip_companions:
        copy_if_present(companion_path(args.objects, ".atlas"), output_path(args.output_prefix, ".atlas"))
        copy_if_present(companion_path(args.objects, ".tanim"), output_path(args.output_prefix, ".tanim"))
        copy_if_present(
            companion_path(args.objects, ".object_anim.atlas"),
            output_path(args.output_prefix, ".object_anim.atlas"),
        )
        copy_if_present(
            companion_path(args.objects, ".object_anim.models"),
            output_path(args.output_prefix, ".object_anim.models"),
        )
    if not args.skip_oanim:
        extract_oanim(
            companion_path(args.objects, ".oanim"),
            output_path(args.output_prefix, ".oanim"),
            args.origin_x,
            args.origin_y,
            args.width,
            args.height,
            args.pad_tiles,
        )


if __name__ == "__main__":
    main()
