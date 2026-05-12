"""Sprite decoding helpers for modern OSRS cache groups."""

from __future__ import annotations

from dataclasses import dataclass

from .config import INDEX_SPRITES
from .store import RcCacheStore

FLAG_VERTICAL = 0x01
FLAG_ALPHA = 0x02


@dataclass
class SpriteData:
    """RGBA sprite pixels normalized onto the cache sprite canvas."""

    width: int = 0
    height: int = 0
    pixels: bytes = b""


def decode_sprite_group(data: bytes) -> list[SpriteData]:
    """Decode the indexed sprite-group format used by cache index 8."""
    if len(data) < 9:
        raise ValueError("sprite group too small")

    sprite_count = (data[-2] << 8) | data[-1]
    if sprite_count <= 0 or sprite_count > 1000:
        raise ValueError("invalid sprite count")

    meta_pos = len(data) - 7 - sprite_count * 8
    if meta_pos < 0:
        raise ValueError("invalid sprite metadata offset")

    max_width = (data[meta_pos] << 8) | data[meta_pos + 1]
    max_height = (data[meta_pos + 2] << 8) | data[meta_pos + 3]
    palette_length = (data[meta_pos + 4] & 0xFF) + 1
    if max_width <= 0 or max_height <= 0:
        raise ValueError("invalid sprite canvas dimensions")
    if palette_length < 1 or palette_length > 256:
        raise ValueError("invalid sprite palette length")

    pos = meta_pos + 5
    x_offsets = [0] * sprite_count
    y_offsets = [0] * sprite_count
    sub_widths = [0] * sprite_count
    sub_heights = [0] * sprite_count
    for i in range(sprite_count):
        x_offsets[i] = (data[pos] << 8) | data[pos + 1]
        pos += 2
    for i in range(sprite_count):
        y_offsets[i] = (data[pos] << 8) | data[pos + 1]
        pos += 2
    for i in range(sprite_count):
        sub_widths[i] = (data[pos] << 8) | data[pos + 1]
        pos += 2
    for i in range(sprite_count):
        sub_heights[i] = (data[pos] << 8) | data[pos + 1]
        pos += 2

    palette_start = meta_pos - (palette_length - 1) * 3
    if palette_start < 0:
        raise ValueError("invalid sprite palette offset")

    palette = [0] * palette_length
    pos = palette_start
    for i in range(1, palette_length):
        rgb = (data[pos] << 16) | (data[pos + 1] << 8) | data[pos + 2]
        palette[i] = rgb if rgb != 0 else 1
        pos += 3

    pixel_pos = 0
    sprites: list[SpriteData] = []
    for i in range(sprite_count):
        width = sub_widths[i]
        height = sub_heights[i]
        dimension = width * height
        if dimension <= 0:
            canvas_width = max(max_width, width + x_offsets[i], 1)
            canvas_height = max(max_height, height + y_offsets[i], 1)
            sprites.append(
                SpriteData(
                    width=canvas_width,
                    height=canvas_height,
                    pixels=bytes(canvas_width * canvas_height * 4),
                )
            )
            continue
        if pixel_pos >= palette_start:
            raise ValueError("sprite pixels overlap metadata")

        flags = data[pixel_pos]
        pixel_pos += 1
        indices = bytearray(dimension)
        alphas = bytearray(dimension)

        if flags & FLAG_VERTICAL:
            for x in range(width):
                for y in range(height):
                    indices[y * width + x] = data[pixel_pos]
                    pixel_pos += 1
        else:
            indices[:] = data[pixel_pos : pixel_pos + dimension]
            pixel_pos += dimension

        if flags & FLAG_ALPHA:
            if flags & FLAG_VERTICAL:
                for x in range(width):
                    for y in range(height):
                        alphas[y * width + x] = data[pixel_pos]
                        pixel_pos += 1
            else:
                alphas[:] = data[pixel_pos : pixel_pos + dimension]
                pixel_pos += dimension
        else:
            for j, idx in enumerate(indices):
                if idx:
                    alphas[j] = 0xFF
        for j, idx in enumerate(indices):
            if idx:
                alphas[j] = 0xFF

        canvas_width = max(max_width, width + x_offsets[i], 1)
        canvas_height = max(max_height, height + y_offsets[i], 1)
        rgba = bytearray(canvas_width * canvas_height * 4)
        x_off = x_offsets[i]
        y_off = y_offsets[i]

        for y in range(height):
            for x in range(width):
                src = y * width + x
                pal_idx = indices[src]
                alpha = alphas[src]
                if pal_idx >= len(palette):
                    rgb = 0xFF00FF
                else:
                    rgb = palette[pal_idx]
                dst_x = x + x_off
                dst_y = y + y_off
                if dst_x >= canvas_width or dst_y >= canvas_height:
                    continue
                dst = (dst_y * canvas_width + dst_x) * 4
                rgba[dst] = (rgb >> 16) & 0xFF
                rgba[dst + 1] = (rgb >> 8) & 0xFF
                rgba[dst + 2] = rgb & 0xFF
                rgba[dst + 3] = alpha

        sprites.append(
            SpriteData(
                width=canvas_width,
                height=canvas_height,
                pixels=bytes(rgba),
            )
        )

    return sprites


def load_sprite_group(store: RcCacheStore, sprite_id: int) -> list[SpriteData]:
    data = store.read_container(INDEX_SPRITES, sprite_id)
    if data is None:
        raise KeyError(f"sprite group not found: {sprite_id}")
    return decode_sprite_group(data)


def load_sprite(store: RcCacheStore, sprite_id: int, frame: int = 0) -> SpriteData:
    sprites = load_sprite_group(store, sprite_id)
    if frame >= len(sprites):
        raise KeyError(f"sprite frame not found: {sprite_id}:{frame}")
    return sprites[frame]
