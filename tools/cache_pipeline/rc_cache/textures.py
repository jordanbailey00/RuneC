"""Texture/material decoding for b237 cache index 9."""

from __future__ import annotations

from dataclasses import dataclass, field

from .config import INDEX_TEXTURES
from .sprites import SpriteData, load_sprite
from .store import RcCacheStore


@dataclass
class TextureDef:
    """Decoded material definition from cache index 9 group 0."""

    texture_id: int
    file_ids: list[int] = field(default_factory=list)
    average_color: int = 0
    opaque: bool = True
    animation_direction: int = 0
    animation_speed: int = 0


def decode_texture_definition(texture_id: int, data: bytes) -> TextureDef:
    """Decode a texture definition.

    b237 uses the RuneLite rev233 layout:
      u16 sprite_file_id
      u16 average_color
      u8  opaque_or_loaded_flag
      u8  animation_direction
      u8  animation_speed

    Older multi-sprite texture definitions are accepted as a fallback so this
    helper remains usable with adjacent caches.
    """
    if len(data) >= 7 and len(data) == 7:
        return TextureDef(
            texture_id=texture_id,
            file_ids=[(data[0] << 8) | data[1]],
            average_color=(data[2] << 8) | data[3],
            opaque=data[4] != 0,
            animation_direction=data[5],
            animation_speed=data[6],
        )

    if len(data) < 4:
        raise ValueError(f"texture definition too small: {texture_id}")

    pos = 0
    average_color = (data[pos] << 8) | data[pos + 1]
    pos += 2
    opaque = data[pos] != 0
    pos += 1
    count = data[pos]
    pos += 1
    if count <= 0:
        return TextureDef(
            texture_id=texture_id,
            average_color=average_color,
            opaque=opaque,
        )
    if pos + count * 2 > len(data):
        raise ValueError(f"truncated texture file id list: {texture_id}")

    file_ids = []
    for _ in range(count):
        file_ids.append((data[pos] << 8) | data[pos + 1])
        pos += 2

    if count > 1:
        pos += count - 1
    if count > 1:
        pos += count - 1
    pos += count * 4

    animation_direction = data[pos] if pos < len(data) else 0
    animation_speed = data[pos + 1] if pos + 1 < len(data) else 0
    return TextureDef(
        texture_id=texture_id,
        file_ids=file_ids,
        average_color=average_color,
        opaque=opaque,
        animation_direction=animation_direction,
        animation_speed=animation_speed,
    )


def load_texture_definitions(store: RcCacheStore) -> dict[int, TextureDef]:
    files = store.read_group(INDEX_TEXTURES, 0)
    defs: dict[int, TextureDef] = {}
    for texture_id, data in files.items():
        if not data:
            continue
        defs[texture_id] = decode_texture_definition(texture_id, data)
    return defs


def load_texture_average_colors(store: RcCacheStore) -> dict[int, int]:
    return {
        texture_id: texture.average_color
        for texture_id, texture in load_texture_definitions(store).items()
    }


def load_texture_sprites(
    store: RcCacheStore,
    texture_ids: set[int] | None = None,
) -> dict[int, SpriteData]:
    """Load the first sprite for each requested texture definition."""
    textures = load_texture_definitions(store)
    if texture_ids is None:
        ids_to_load = set(textures)
    else:
        ids_to_load = set(texture_ids)

    sprites: dict[int, SpriteData] = {}
    for texture_id in sorted(ids_to_load):
        texture = textures.get(texture_id)
        if texture is None or not texture.file_ids:
            continue
        try:
            sprite = load_sprite(store, texture.file_ids[0])
        except (KeyError, ValueError):
            continue
        sprites[texture_id] = sprite
    return sprites
