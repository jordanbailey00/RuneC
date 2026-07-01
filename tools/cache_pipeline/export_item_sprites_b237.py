#!/usr/bin/env python3
"""Render b237 item inventory sprites into RuneC's viewer sprite layout.

This replaces the transitional reference-icon copier for release rebuilds. The
output remains compatible with the existing viewer and packer:

  data/sprites/items/item_<item_id>.png
  data/sprites/items/item_stack_variants.tsv

The renderer is intentionally conservative. It uses b237 item config metadata
for the inventory model, 2D rotations, offsets, recolors/retextures, resize
fields, and quantity display variants. It is not a pixel-perfect OSRS client
ItemSpriteFactory clone, but every generated icon is backed by the b237 cache
instead of an external PNG dump.
"""

from __future__ import annotations

import argparse
import copy
import math
import sys
from dataclasses import dataclass, field
from functools import lru_cache
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, str(Path(__file__).resolve().parent))

from export_models import hsl15_to_rgb  # noqa: E402
from rc_cache import RcCacheStore, load_texture_average_colors  # noqa: E402
from rc_cache.models import ModelData, load_model as rc_load_model  # noqa: E402

CONFIG_INDEX = 2
OBJ_GROUP = 10
MISSING_U16 = 0xFFFF
ICON_W = 36
ICON_H = 32
SUPERSAMPLE = 3


@dataclass
class ItemSpriteDef:
    item_id: int
    name: str = ""
    inventory_model: int = -1
    zoom_2d: int = 2000
    xan_2d: int = 0
    yan_2d: int = 0
    zan_2d: int = 0
    offset_x_2d: int = 0
    offset_y_2d: int = 0
    stackable: bool = False
    note_id: int = -1
    note_template_id: int = -1
    placeholder_id: int = -1
    placeholder_template_id: int = -1
    resize_x: int = 128
    resize_y: int = 128
    resize_z: int = 128
    ambient: int = 0
    contrast: int = 0
    recolor_src: list[int] = field(default_factory=list)
    recolor_dst: list[int] = field(default_factory=list)
    retexture_src: list[int] = field(default_factory=list)
    retexture_dst: list[int] = field(default_factory=list)
    count_obj: list[int] = field(default_factory=list)
    count_amt: list[int] = field(default_factory=list)


def _u8(data: bytes, pos: int) -> tuple[int, int]:
    return data[pos], pos + 1


def _i8(data: bytes, pos: int) -> tuple[int, int]:
    value = data[pos]
    if value >= 128:
        value -= 256
    return value, pos + 1


def _u16(data: bytes, pos: int) -> tuple[int, int]:
    return (data[pos] << 8) | data[pos + 1], pos + 2


def _i16_from_u16(value: int) -> int:
    return value - 65536 if value > 32767 else value


def _u32(data: bytes, pos: int) -> tuple[int, int]:
    return (
        (data[pos] << 24)
        | (data[pos + 1] << 16)
        | (data[pos + 2] << 8)
        | data[pos + 3],
        pos + 4,
    )


def _i32(data: bytes, pos: int) -> tuple[int, int]:
    value, pos = _u32(data, pos)
    if value >= 0x80000000:
        value -= 0x100000000
    return value, pos


def _read_string(data: bytes, pos: int) -> tuple[str, int]:
    end = data.find(b"\x00", pos)
    if end < 0:
        return "", len(data)
    return data[pos:end].decode("cp1252", "replace"), end + 1


def _id(raw: int) -> int:
    return -1 if raw in (MISSING_U16, 0xFFFFFFFF) else int(raw)


def _skip_params(data: bytes, pos: int) -> int:
    if pos >= len(data):
        return len(data)
    count = data[pos]
    pos += 1
    for _ in range(count):
        if pos + 4 > len(data):
            return len(data)
        is_string = data[pos]
        pos += 4
        if is_string:
            _value, pos = _read_string(data, pos)
        else:
            pos += 4
    return min(pos, len(data))


def _skip_entity_subop(data: bytes, pos: int) -> int:
    while pos < len(data):
        subop = data[pos]
        pos += 1
        if subop == 0:
            break
        if subop in (1, 3):
            _value, pos = _read_string(data, pos)
        elif subop in (2,):
            pos += 4
        elif subop in (4, 5):
            pos += 1
        else:
            break
    return min(pos, len(data))


def _skip_conditional_op(data: bytes, pos: int) -> int:
    if pos >= len(data):
        return len(data)
    count = data[pos]
    pos += 1
    for _ in range(count):
        if pos >= len(data):
            break
        pos += 1
        if pos + 4 <= len(data):
            pos += 4
    return min(pos, len(data))


def _skip_conditional_subop(data: bytes, pos: int) -> int:
    while pos < len(data):
        subop = data[pos]
        pos += 1
        if subop == 0:
            break
        if subop in (1, 2):
            pos += 4
        elif subop == 3:
            _value, pos = _read_string(data, pos)
        else:
            break
    return min(pos, len(data))


def decode_item_sprite_def(item_id: int, data: bytes) -> ItemSpriteDef:
    d = ItemSpriteDef(item_id=item_id)
    pos = 0
    while pos < len(data):
        opcode = data[pos]
        pos += 1
        if opcode == 0:
            break
        if opcode == 1:
            d.inventory_model, pos = _u16(data, pos)
        elif opcode == 2:
            d.name, pos = _read_string(data, pos)
        elif opcode == 3:
            _desc, pos = _read_string(data, pos)
        elif opcode == 4:
            d.zoom_2d, pos = _u16(data, pos)
        elif opcode == 5:
            d.yan_2d, pos = _u16(data, pos)
        elif opcode == 6:
            d.xan_2d, pos = _u16(data, pos)
        elif opcode == 7:
            value, pos = _u16(data, pos)
            d.offset_x_2d = _i16_from_u16(value)
        elif opcode == 8:
            value, pos = _u16(data, pos)
            d.offset_y_2d = _i16_from_u16(value)
        elif opcode == 9:
            _unknown, pos = _read_string(data, pos)
        elif opcode in (10, 21, 22, 66, 67, 68, 71, 73, 74, 76, 77, 80, 81,
                        82, 83, 84, 85, 86, 87, 90, 91, 92, 93, 94, 116, 117,
                        118, 139, 140, 156, 161):
            pos += 2
        elif opcode == 11:
            d.stackable = True
        elif opcode == 12:
            _cost, pos = _i32(data, pos)
        elif opcode in (13, 14, 17, 18, 19, 20, 27, 28, 29, 42, 62, 69, 115,
                        119, 120, 121, 122, 155, 157, 158, 159, 162, 163, 165):
            pos += 1
        elif opcode in (15, 16, 64, 65):
            pass
        elif opcode == 23:
            pos += 3
        elif opcode in (24, 26, 78, 79):
            pos += 2
        elif opcode == 25:
            pos += 3
        elif 30 <= opcode < 40:
            _action, pos = _read_string(data, pos)
        elif opcode == 40:
            count, pos = _u8(data, pos)
            for _ in range(count):
                src, pos = _u16(data, pos)
                dst, pos = _u16(data, pos)
                d.recolor_src.append(src)
                d.recolor_dst.append(dst)
        elif opcode == 41:
            count, pos = _u8(data, pos)
            for _ in range(count):
                src, pos = _u16(data, pos)
                dst, pos = _u16(data, pos)
                d.retexture_src.append(src)
                d.retexture_dst.append(dst)
        elif opcode == 43:
            if pos < len(data):
                pos += 1
            while pos < len(data):
                subop = data[pos] - 1
                pos += 1
                if subop == -1:
                    break
                _text, pos = _read_string(data, pos)
        elif opcode == 44:
            d.inventory_model, pos = _u32(data, pos)
        elif opcode in (45, 48):
            pos += 5
        elif opcode in (46, 47, 49, 50, 51, 52, 53, 54):
            pos += 4
        elif opcode == 75:
            pos += 2
        elif opcode == 95:
            d.zan_2d, pos = _u16(data, pos)
        elif opcode == 97:
            value, pos = _u16(data, pos)
            d.note_id = _id(value)
        elif opcode == 98:
            value, pos = _u16(data, pos)
            d.note_template_id = _id(value)
        elif 100 <= opcode < 110:
            obj, pos = _u16(data, pos)
            amt, pos = _u16(data, pos)
            if obj and amt:
                d.count_obj.append(obj)
                d.count_amt.append(amt)
        elif opcode == 110:
            d.resize_x, pos = _u16(data, pos)
        elif opcode == 111:
            d.resize_y, pos = _u16(data, pos)
        elif opcode == 112:
            d.resize_z, pos = _u16(data, pos)
        elif opcode == 113:
            d.ambient, pos = _i8(data, pos)
        elif opcode == 114:
            d.contrast, pos = _i8(data, pos)
        elif opcode == 148:
            value, pos = _u16(data, pos)
            d.placeholder_id = _id(value)
        elif opcode == 149:
            value, pos = _u16(data, pos)
            d.placeholder_template_id = _id(value)
        elif opcode == 160:
            count, pos = _u8(data, pos)
            pos += count * 2
        elif opcode == 164:
            _unknown, pos = _read_string(data, pos)
        elif opcode == 200:
            pos = _skip_entity_subop(data, pos)
        elif opcode == 201:
            pos = _skip_conditional_op(data, pos)
        elif opcode == 202:
            pos = _skip_conditional_subop(data, pos)
        elif opcode == 211:
            count, pos = _u8(data, pos)
            pos += count * 2
        elif opcode == 249:
            pos = _skip_params(data, pos)
        else:
            raise ValueError(f"item {item_id}: unknown opcode {opcode} at {pos - 1}")
    return d


def apply_item_overrides(model: ModelData, item: ItemSpriteDef) -> None:
    for src, dst in zip(item.recolor_src, item.recolor_dst):
        for idx, color in enumerate(model.face_colors):
            if color == src:
                model.face_colors[idx] = dst
    if model.face_textures:
        for src, dst in zip(item.retexture_src, item.retexture_dst):
            for idx, texture in enumerate(model.face_textures):
                if texture == src:
                    model.face_textures[idx] = dst
    if item.resize_x != 128 or item.resize_y != 128 or item.resize_z != 128:
        for idx in range(model.vertex_count):
            model.vertices_x[idx] = int(round(model.vertices_x[idx] * item.resize_x / 128.0))
            model.vertices_y[idx] = int(round(model.vertices_y[idx] * item.resize_y / 128.0))
            model.vertices_z[idx] = int(round(model.vertices_z[idx] * item.resize_z / 128.0))


def rotate_points(points: np.ndarray, item: ItemSpriteDef) -> np.ndarray:
    out = points.astype(np.float64, copy=True)
    for axis, angle_units in (("z", item.zan_2d), ("y", item.yan_2d), ("x", item.xan_2d)):
        angle = (angle_units & 2047) * math.pi / 1024.0
        if angle == 0.0:
            continue
        s = math.sin(angle)
        c = math.cos(angle)
        x = out[:, 0].copy()
        y = out[:, 1].copy()
        z = out[:, 2].copy()
        if axis == "x":
            out[:, 1] = y * c - z * s
            out[:, 2] = y * s + z * c
        elif axis == "y":
            out[:, 0] = x * c + z * s
            out[:, 2] = -x * s + z * c
        else:
            out[:, 0] = x * c - y * s
            out[:, 1] = x * s + y * c
    return out


def face_color(
    model: ModelData,
    face_idx: int,
    texture_average_colors: dict[int, int],
) -> tuple[int, int, int, int]:
    texture = (
        model.face_textures[face_idx]
        if face_idx < len(model.face_textures)
        else -1
    )
    if texture is not None and texture >= 0:
        color = texture_average_colors.get(texture, model.face_colors[face_idx])
    else:
        color = model.face_colors[face_idx]
    rgb = hsl15_to_rgb(color)
    alpha = 255
    if face_idx < len(model.face_alphas):
        alpha = max(0, min(255, 255 - model.face_alphas[face_idx]))
    return rgb[0], rgb[1], rgb[2], alpha


def render_icon(
    item: ItemSpriteDef,
    model: ModelData | None,
    texture_average_colors: dict[int, int],
) -> Image.Image:
    if model is None or model.vertex_count == 0 or model.face_count == 0:
        return Image.new("RGBA", (ICON_W, ICON_H), (0, 0, 0, 0))

    points = np.array(
        [
            (model.vertices_x[i], -model.vertices_y[i], model.vertices_z[i])
            for i in range(model.vertex_count)
        ],
        dtype=np.float64,
    )
    rotated = rotate_points(points, item)

    min_xy = rotated[:, :2].min(axis=0)
    max_xy = rotated[:, :2].max(axis=0)
    span_x = max(1.0, max_xy[0] - min_xy[0])
    span_y = max(1.0, max_xy[1] - min_xy[1])

    canvas_w = ICON_W * SUPERSAMPLE
    canvas_h = ICON_H * SUPERSAMPLE
    # The cache zoom field is still used as a relative factor, but auto-fit is
    # the guardrail that keeps unusually shaped items inside the 36x32 icon box.
    zoom_factor = max(0.45, min(1.75, item.zoom_2d / 2000.0))
    scale = min((canvas_w - 8) / span_x, (canvas_h - 8) / span_y) * zoom_factor
    if scale * span_x > canvas_w - 2 or scale * span_y > canvas_h - 2:
        scale = min((canvas_w - 2) / span_x, (canvas_h - 2) / span_y)

    center_x = (canvas_w / 2.0) + item.offset_x_2d * SUPERSAMPLE / 16.0
    center_y = (canvas_h / 2.0) + item.offset_y_2d * SUPERSAMPLE / 16.0
    mid_x = (min_xy[0] + max_xy[0]) / 2.0
    mid_y = (min_xy[1] + max_xy[1]) / 2.0
    screen_x = (rotated[:, 0] - mid_x) * scale + center_x
    screen_y = -(rotated[:, 1] - mid_y) * scale + center_y

    faces: list[tuple[float, int]] = []
    for face_idx in range(model.face_count):
        a = model.face_a[face_idx]
        b = model.face_b[face_idx]
        c = model.face_c[face_idx]
        if a < 0 or b < 0 or c < 0:
            continue
        if a >= model.vertex_count or b >= model.vertex_count or c >= model.vertex_count:
            continue
        faces.append(((rotated[a, 2] + rotated[b, 2] + rotated[c, 2]) / 3.0, face_idx))

    image = Image.new("RGBA", (canvas_w, canvas_h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image, "RGBA")
    for _depth, face_idx in sorted(faces):
        color = face_color(model, face_idx, texture_average_colors)
        if color[3] <= 0:
            continue
        coords = [
            (float(screen_x[model.face_a[face_idx]]), float(screen_y[model.face_a[face_idx]])),
            (float(screen_x[model.face_b[face_idx]]), float(screen_y[model.face_b[face_idx]])),
            (float(screen_x[model.face_c[face_idx]]), float(screen_y[model.face_c[face_idx]])),
        ]
        draw.polygon(coords, fill=color)

    alpha = image.getchannel("A")
    if alpha.getbbox():
        outline_mask = alpha.filter(ImageFilter.MaxFilter(3))
        outline = Image.new("RGBA", image.size, (0, 0, 0, 130))
        outline.putalpha(outline_mask)
        outlined = Image.alpha_composite(outline, image)
        outlined.putalpha(Image.composite(alpha, outline_mask, alpha))
        image = outlined

    return image.resize((ICON_W, ICON_H), Image.Resampling.LANCZOS)


def item_ids_from_arg(raw: str | None, item_defs: dict[int, ItemSpriteDef]) -> list[int]:
    if raw is None or raw.strip().lower() == "all":
        return sorted(item_defs)
    return [int(part) for part in raw.replace(",", " ").split() if part]


def write_stack_variants(item_defs: dict[int, ItemSpriteDef], path: Path) -> int:
    rows: list[tuple[int, int, int]] = []
    for item in item_defs.values():
        for variant_id, threshold in zip(item.count_obj, item.count_amt):
            if item.item_id > 0 and threshold > 1 and variant_id > 0:
                rows.append((item.item_id, threshold, variant_id))
    # Preserve the viewer's existing explicit coin thresholds. The cache has the
    # display item IDs, but these thresholds are hard-coded in the viewer too.
    rows.extend(
        [
            (995, 2, 996),
            (995, 3, 997),
            (995, 4, 998),
            (995, 5, 999),
            (995, 25, 1000),
            (995, 100, 1001),
            (995, 250, 1002),
            (995, 1000, 1003),
            (995, 10000, 1004),
        ]
    )
    rows = sorted(set(rows))
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        f.write("# base_item_id\tcount_threshold\tdisplay_item_id\n")
        for base_id, threshold, variant_id in rows:
            f.write(f"{base_id}\t{threshold}\t{variant_id}\n")
    return len(rows)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("data/sprites/items"))
    parser.add_argument("--item-ids", help="item IDs to render, or 'all'")
    parser.add_argument("--clean", action="store_true",
                        help="remove existing item_*.png outputs first")
    parser.add_argument("--fail-on-decode-gap", action="store_true",
                        help="fail if any item config row cannot be decoded")
    args = parser.parse_args(argv)

    store = RcCacheStore(args.cache)
    raw_items = store.read_group(CONFIG_INDEX, OBJ_GROUP)
    item_defs: dict[int, ItemSpriteDef] = {}
    decode_errors: list[str] = []
    for item_id, data in raw_items.items():
        try:
            item_defs[item_id] = decode_item_sprite_def(item_id, data)
        except Exception as exc:  # noqa: BLE001 - keep row-level diagnostics.
            decode_errors.append(f"{item_id}: {exc}")

    if decode_errors and args.fail_on_decode_gap:
        raise SystemExit("item sprite decode gaps:\n" + "\n".join(decode_errors[:40]))
    for msg in decode_errors[:20]:
        print(f"warning: {msg}", file=sys.stderr)
    if len(decode_errors) > 20:
        print(f"warning: {len(decode_errors) - 20} additional item decode gaps", file=sys.stderr)

    item_ids = item_ids_from_arg(args.item_ids, item_defs)
    args.output.mkdir(parents=True, exist_ok=True)
    if args.clean:
        for path in args.output.glob("item_*.png"):
            path.unlink()
        variants = args.output / "item_stack_variants.tsv"
        if variants.exists():
            variants.unlink()

    texture_average_colors = load_texture_average_colors(store)

    @lru_cache(maxsize=4096)
    def load_base_model(model_id: int) -> ModelData | None:
        return rc_load_model(store, model_id)

    rendered = 0
    transparent = 0
    missing_model = 0
    for index, item_id in enumerate(item_ids, start=1):
        item = item_defs.get(item_id)
        if item is None:
            continue
        base_model = load_base_model(item.inventory_model) if item.inventory_model >= 0 else None
        model = copy.deepcopy(base_model) if base_model is not None else None
        if model is None:
            missing_model += 1
        else:
            apply_item_overrides(model, item)
        icon = render_icon(item, model, texture_average_colors)
        if not icon.getchannel("A").getbbox():
            transparent += 1
        icon.save(args.output / f"item_{item_id}.png")
        rendered += 1
        if rendered % 2500 == 0:
            print(f"rendered {rendered}/{len(item_ids)} item sprites")

    variants = write_stack_variants(item_defs, args.output / "item_stack_variants.tsv")
    print(
        f"rendered {rendered} b237 item sprites to {args.output} "
        f"({transparent} transparent, {missing_model} without inventory model)"
    )
    print(f"wrote {variants} stack icon variants")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
