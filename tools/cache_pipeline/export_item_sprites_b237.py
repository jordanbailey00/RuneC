#!/usr/bin/env python3
"""Render b237 item inventory sprites into RuneC's viewer sprite layout.

This replaces the transitional reference-icon copier for release rebuilds. The
output remains compatible with the existing viewer and packer:

  data/sprites/items/item_<item_id>.png
  data/sprites/items/item_stack_variants.tsv

Uses the client inventory camera, face ordering, lighting, textures, template
composition, and native 36x32 pixels. The existing RuneC lighting helpers are
shared with model export; no external client code or PNG dump is invoked.
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
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

from export_models import (  # noqa: E402
    _compute_normals, _lit_face_vertex_colors, _projected_face_uvs,
)
from rc_cache import RcCacheStore, load_texture_sprites  # noqa: E402
from rc_cache.models import ModelData, load_model as rc_load_model  # noqa: E402

CONFIG_INDEX = 2
OBJ_GROUP = 10
MISSING_U16 = 0xFFFF
ICON_W = 36
ICON_H = 32


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
    bought_id: int = -1
    bought_template_id: int = -1
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
            d.xan_2d, pos = _u16(data, pos)
        elif opcode == 6:
            d.yan_2d, pos = _u16(data, pos)
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
                        118, 156, 161):
            pos += 2
        elif opcode == 11:
            d.stackable = True
        elif opcode == 12:
            _cost, pos = _i32(data, pos)
        elif opcode in (13, 14, 17, 18, 19, 20, 27, 28, 29, 42, 59, 62, 69, 115,
                        119, 120, 121, 122, 155, 157, 158, 159, 162, 163, 165):
            pos += 1
        elif opcode in (15, 16, 64, 65, 160, 251):
            pass  # 160 carried a u16 list before b241
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
        elif opcode == 139:
            value, pos = _u16(data, pos)
            d.bought_id = _id(value)
        elif opcode == 140:
            value, pos = _u16(data, pos)
            d.bought_template_id = _id(value)
        elif opcode == 148:
            value, pos = _u16(data, pos)
            d.placeholder_id = _id(value)
        elif opcode == 149:
            value, pos = _u16(data, pos)
            d.placeholder_template_id = _id(value)
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
            model.vertices_x[idx] = int(model.vertices_x[idx] * item.resize_x / 128)
            model.vertices_y[idx] = int(model.vertices_y[idx] * item.resize_y / 128)
            model.vertices_z[idx] = int(model.vertices_z[idx] * item.resize_z / 128)


def project_vertices(model: ModelData, item: ItemSpriteDef, noted: bool = False) -> np.ndarray:
    # Client inventory camera: fixed-point Z/Y rotations, translation, then pitch.
    points = np.array([model.vertices_x, model.vertices_y, model.vertices_z],
                      dtype=np.int64).T.copy()
    def trig(angle):
        angle = (angle & 2047) * math.pi / 1024
        return int(math.sin(angle) * 65536), int(math.cos(angle) * 65536)

    s, c = trig(item.zan_2d)
    x, y = points[:, 0].copy(), points[:, 1].copy()
    points[:, 0], points[:, 1] = (y * s + x * c) >> 16, (y * c - x * s) >> 16
    s, c = trig(item.yan_2d)
    x, z = points[:, 0].copy(), points[:, 2].copy()
    points[:, 0], points[:, 2] = (z * s + x * c) >> 16, (z * c - x * s) >> 16
    s, c = trig(item.xan_2d)
    zoom = int(item.zoom_2d * 1.5) if noted else item.zoom_2d
    points += (item.offset_x_2d,
               max(0, -min(model.vertices_y)) // 2 + ((zoom * s) >> 16) + item.offset_y_2d,
               ((zoom * c) >> 16) + item.offset_y_2d)
    y, z = points[:, 1].copy(), points[:, 2].copy()
    points[:, 1], points[:, 2] = (y * c - z * s) >> 16, (y * s + z * c) >> 16
    depth = points[:, 2]
    if np.any(depth <= 0):
        raise ValueError(f"item {item.item_id}: inventory model crosses the camera")
    points[:, :2] = np.trunc(points[:, :2] * 512 / depth[:, None]).astype(np.int64) + 16
    return points


def ordered_faces(model: ModelData, points: np.ndarray) -> list[int]:
    faces = []
    for fi, (a, b, c) in enumerate(zip(model.face_a, model.face_b, model.face_c)):
        pa, pb, pc = points[[a, b, c]]
        if (pa[0] - pb[0]) * (pc[1] - pb[1]) - (pc[0] - pb[0]) * (pa[1] - pb[1]) > 0:
            faces.append((int(pa[2] + pb[2] + pc[2]), fi))
    faces.sort(key=lambda pair: pair[0], reverse=True)
    if not model.face_priorities:
        return [fi for _, fi in faces]
    groups = [[] for _ in range(12)]
    for depth, fi in faces:
        priority = model.face_priorities[fi]
        if not 0 <= priority < 12:
            raise ValueError(f"model {model.model_id}: invalid face priority {priority}")
        groups[priority].append((depth, fi))
    special = iter(groups[10] + groups[11])
    pending = next(special, None)
    result = []
    for priority in range(10):
        if priority in (0, 3, 5):
            pair = {0: (1, 2), 3: (3, 4), 5: (6, 8)}[priority]
            normal = groups[pair[0]] + groups[pair[1]]
            threshold = sum(d for d, _ in normal) / len(normal) if normal else 0
            while pending and pending[0] > threshold:
                result.append(pending[1])
                pending = next(special, None)
        result.extend(fi for _, fi in groups[priority])
    if pending:
        result.append(pending[1])
    result.extend(fi for _, fi in special)
    return result


def raster_face(canvas, points, colors, texture=None, uvs=None):
    left, top = np.maximum(points[:, :2].min(axis=0), (0, 0)).astype(int)
    right, bottom = np.minimum(points[:, :2].max(axis=0), (ICON_W - 1, ICON_H - 1)).astype(int)
    if left > right or top > bottom:
        return
    y, x = np.mgrid[top:bottom + 1, left:right + 1]
    x, y = x + 0.5, y + 0.5
    a, b, c = points[:, :2]
    area = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
    if not area:
        return
    w0 = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / area
    w1 = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / area
    weights = np.stack((w0, w1, 1 - w0 - w1), axis=-1)
    mask = np.all(weights >= -1e-9, axis=-1)
    if not mask.any():
        return
    color = weights[mask] @ np.asarray(colors, dtype=float)
    if texture is not None:
        perspective = weights[mask] / points[:, 2]
        uv = perspective @ np.asarray(uvs) / perspective.sum(axis=-1, keepdims=True)
        h, w = texture.shape[:2]
        texels = texture[(np.floor(uv[:, 1] * h).astype(int) % h),
                         (np.floor(uv[:, 0] * w).astype(int) % w)]
        color *= texels / 255.0
    region = canvas[top:bottom + 1, left:right + 1]
    previous = region[mask].astype(float)
    alpha = color[:, 3:4] / 255
    old_alpha = previous[:, 3:4] / 255 * (1 - alpha)
    combined = alpha + old_alpha
    rgb = (color[:, :3] * alpha + previous[:, :3] * old_alpha) / np.maximum(combined, 1e-9)
    region[mask] = np.rint(np.clip(np.concatenate((rgb, combined * 255), axis=1), 0, 255)).astype(np.uint8)


def render_icon(item, model, textures, noted=False) -> Image.Image:
    canvas = np.zeros((ICON_H, ICON_W, 4), dtype=np.uint8)
    if model is None or not model.vertex_count or not model.face_count:
        return Image.fromarray(canvas)
    points = project_vertices(model, item, noted)
    normals, face_normals = _compute_normals(model)
    for fi in ordered_faces(model, points):
        alpha = 255 - (model.face_alphas[fi] if model.face_alphas else 0)
        colors = _lit_face_vertex_colors(model, fi, alpha, item.ambient + 64,
                                         item.contrast + 768, (-50, -10, -50),
                                         normals, face_normals)
        if colors is None:
            continue
        texture_id = model.face_textures[fi] if model.face_textures else -1
        texture, uvs = None, None
        if texture_id >= 0:
            if texture_id not in textures:
                raise ValueError(f"item {item.item_id}: missing texture {texture_id}")
            texture = textures[texture_id]
            uvs = np.asarray(_projected_face_uvs(model, fi)).T
        vertices = points[[model.face_a[fi], model.face_b[fi], model.face_c[fi]]]
        raster_face(canvas, vertices, colors, texture, uvs)
    return Image.fromarray(canvas)


def outline_icon(image, border=True, shadow=True):
    pixels = np.array(image)
    mask = pixels[:, :, 3] != 0
    if border:
        neighbors = np.zeros_like(mask)
        neighbors[1:] |= mask[:-1]
        neighbors[:-1] |= mask[1:]
        neighbors[:, 1:] |= mask[:, :-1]
        neighbors[:, :-1] |= mask[:, 1:]
        pixels[neighbors & ~mask] = (0, 0, 1, 255)
    if shadow:
        mask = pixels[:, :, 3] != 0
        shifted = np.zeros_like(mask)
        shifted[1:, 1:] = mask[:-1, :-1]
        pixels[shifted & ~mask] = (48, 32, 32, 255)
    return Image.fromarray(pixels)


def render_item_icon(item_id, definitions, load_model, textures, quantity=1,
                     border=True, shadow=True, noted=False, ancestry=()):
    if item_id in ancestry:
        raise ValueError(f"item {item_id}: cyclic icon template")
    item = copy.copy(definitions[item_id])
    if quantity > 1:
        variants = [i for i, n in zip(item.count_obj, item.count_amt) if quantity >= n]
        if variants:
            return render_item_icon(variants[-1], definitions, load_model, textures,
                                    1, border, shadow, noted, ancestry + (item_id,))
    template_id, base_id, kind = -1, -1, ""
    for kind, template_id, base_id in (
        ("note", item.note_template_id, item.note_id),
        ("bought", item.bought_template_id, item.bought_id),
        ("placeholder", item.placeholder_template_id, item.placeholder_id),
    ):
        if template_id >= 0:
            break
    auxiliary = None
    if template_id >= 0:
        template = definitions[template_id]
        for field_name in ("inventory_model", "zoom_2d", "xan_2d", "yan_2d", "zan_2d",
                           "offset_x_2d", "offset_y_2d"):
            setattr(item, field_name, getattr(template, field_name))
        colors = definitions[base_id] if kind == "bought" else template
        for field_name in ("recolor_src", "recolor_dst", "retexture_src", "retexture_dst"):
            setattr(item, field_name, getattr(colors, field_name))
        auxiliary = render_item_icon(base_id, definitions, load_model, textures,
                                     10 if kind == "note" else quantity,
                                     kind != "placeholder" and border, False,
                                     kind == "note", ancestry + (item_id,))
    model = None
    if item.inventory_model >= 0:
        model = copy.deepcopy(load_model(item.inventory_model))
        if model is None:
            raise ValueError(f"item {item_id}: missing inventory model {item.inventory_model}")
        apply_item_overrides(model, item)
    image = render_icon(item, model, textures, noted)
    if auxiliary is not None:
        if kind == "placeholder":
            image = Image.alpha_composite(auxiliary, image)
        elif kind == "bought":
            image = Image.alpha_composite(image, auxiliary)
    image = outline_icon(image, border, shadow)
    if auxiliary is not None and kind == "note":
        image = Image.alpha_composite(image, auxiliary)
    return image


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

    if decode_errors:
        raise SystemExit("item sprite decode gaps:\n" + "\n".join(decode_errors[:40]))

    item_ids = item_ids_from_arg(args.item_ids, item_defs)
    args.output.mkdir(parents=True, exist_ok=True)
    if args.clean:
        for path in args.output.glob("item_*.png"):
            path.unlink()
        variants = args.output / "item_stack_variants.tsv"
        if variants.exists():
            variants.unlink()

    textures = {key: np.frombuffer(sprite.pixels, dtype=np.uint8).reshape(
        sprite.height, sprite.width, 4)
        for key, sprite in load_texture_sprites(store).items()}

    @lru_cache(maxsize=4096)
    def load_base_model(model_id: int) -> ModelData | None:
        return rc_load_model(store, model_id)

    rendered = 0
    transparent = []
    missing_model = 0
    for index, item_id in enumerate(item_ids, start=1):
        item = item_defs.get(item_id)
        if item is None:
            raise ValueError(f"item {item_id}: definition missing")
        if item.inventory_model < 0 and item.note_template_id < 0 and item.placeholder_template_id < 0:
            missing_model += 1
        icon = render_item_icon(item_id, item_defs, load_base_model, textures)
        if not icon.getchannel("A").getbbox():
            transparent.append(item_id)
        icon.save(args.output / f"item_{item_id}.png")
        rendered += 1
        if rendered % 2500 == 0:
            print(f"rendered {rendered}/{len(item_ids)} item sprites")

    variants = write_stack_variants(item_defs, args.output / "item_stack_variants.tsv")
    print(
        f"rendered {rendered} b237 item sprites to {args.output} "
        f"({len(transparent)} transparent, {missing_model} without inventory model)"
    )
    if transparent:
        print("transparent item IDs: " + ", ".join(map(str, transparent)))
    print(f"wrote {variants} stack icon variants")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
