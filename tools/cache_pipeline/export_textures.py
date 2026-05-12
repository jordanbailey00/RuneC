"""Build a texture atlas for raylib rendering from OSRS 317 cache sprite data.

Decodes IndexedImage sprites from cache index 5 (gzip-compressed), which contain
the actual vanilla OSRS texture pixel data. Each texture definition in textures.dat
references one or more sprite group IDs; the first sprite in each group is used.

The atlas is exported as raw RGBA bytes alongside a mapping of texture ID
to atlas UV coordinates, consumed by the object/model exporters.

Key discovery: sprite data lives in idx5, NOT idx4. The tarnish client's SwiftFUP
integration adds +1 to the index ID when making file requests — so texture sprites
requested as type 4 are stored in idx5.

Usage:
    from export_textures import build_atlas, load_all_texture_sprites
"""

import gzip
import io
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from export_collision_map import (
    CONFIG_INDEX,
    CacheReader,
    decode_archive,
    hash_archive_name,
)

TEXTURE_SIZE = 128  # atlas cell size (vanilla sprites are 64x64 or 128x128)
ATLAS_COLS = 16  # textures per row in atlas
SPRITE_CACHE_INDEX = 5  # idx5 holds IndexedImage sprite data (not idx4)
TEXTURE_ANIM_MAGIC = 0x4D4E4154  # TANM
TEXTURE_ANIM_VERSION = 1

# IndexedImage format flags
FLAG_VERTICAL = 0b01
FLAG_ALPHA = 0b10


@dataclass
class SpriteData:
    """Decoded sprite pixel data."""

    width: int = 0
    height: int = 0
    pixels: bytes = b""  # RGBA, row-major, width*height*4 bytes


@dataclass
class TextureAtlas:
    """Built texture atlas with UV mapping info."""

    width: int = 0
    height: int = 0
    pixels: bytes = b""  # RGBA, width*height*4
    # mapping: texture_id -> (u_offset, v_offset, u_size, v_size) in [0,1] range
    uv_map: dict[int, tuple[float, float, float, float]] = field(default_factory=dict)
    # mapping: texture_id -> (x, y, w, h, vertical_pad_px) for texture animation
    anim_map: dict[int, tuple[int, int, int, int, int]] = field(default_factory=dict)
    repeat_v_margin: float = 0.0
    # white pixel UV for non-textured faces
    white_u: float = 0.0
    white_v: float = 0.0


def _parse_textures_dat(cache: CacheReader) -> dict[int, list[int]]:
    """Parse textures.dat to get texture_id -> list of sprite fileIds.

    Mirrors the Texture constructor in tarnish: reads averageRGB, isTransparent,
    count, fileIds[], and other fields from each entry.
    """
    raw = cache.get(CONFIG_INDEX, 2)
    if raw is None:
        sys.exit("could not read config archive")
    archive = decode_archive(raw)
    key = hash_archive_name("textures.dat")
    tex_dat = archive.get(key)
    if tex_dat is None:
        sys.exit("textures.dat not found in archive")

    buf = io.BytesIO(tex_dat)
    highest_id = struct.unpack(">H", buf.read(2))[0]
    result: dict[int, list[int]] = {}

    for _ in range(highest_id + 1):
        tex_id = struct.unpack(">H", buf.read(2))[0]
        size = struct.unpack(">H", buf.read(2))[0]
        entry_data = buf.read(size)
        if len(entry_data) < 4:
            if tex_id >= highest_id:
                break
            continue

        # entry format: averageRGB(2) + isTransparent(1) + count(1) + fileIds(count*2) + ...
        eb = io.BytesIO(entry_data)
        _avg_rgb = struct.unpack(">H", eb.read(2))[0]
        _is_transparent = struct.unpack(">b", eb.read(1))[0]
        count = struct.unpack(">B", eb.read(1))[0]
        file_ids = []
        for _ in range(count):
            fid = struct.unpack(">H", eb.read(2))[0]
            file_ids.append(fid)
        result[tex_id] = file_ids

        if tex_id >= highest_id:
            break

    return result


def _decode_indexed_image(data: bytes) -> SpriteData | None:
    """Decode IndexedImage format from raw bytes (already decompressed).

    IndexedImage stores metadata at the END of the buffer:
    - last 2 bytes: sprite_count
    - before that: maxWidth(2), maxHeight(2), paletteLength(1), per-sprite offsets(8*count)
    - palette entries (3 bytes each, RGB) before the per-sprite metadata
    - pixel data at the START of the buffer

    Returns the first sprite as RGBA SpriteData, or None if invalid.
    """
    if len(data) < 9:
        return None

    # sprite_count from last 2 bytes
    sprite_count = (data[-2] << 8) | data[-1]
    if sprite_count == 0 or sprite_count > 100:
        return None

    # metadata position
    meta_pos = len(data) - 7 - sprite_count * 8
    if meta_pos < 0:
        return None

    max_width = (data[meta_pos] << 8) | data[meta_pos + 1]
    max_height = (data[meta_pos + 2] << 8) | data[meta_pos + 3]
    palette_length = (data[meta_pos + 4] & 0xFF) + 1

    if max_width < 1 or max_width > 512 or max_height < 1 or max_height > 512:
        return None
    if palette_length < 2 or palette_length > 256:
        return None

    # read per-sprite metadata (only first sprite used)
    # offsets: xOffset(2*count), yOffset(2*count), subWidth(2*count), subHeight(2*count)
    sprite_meta_start = meta_pos + 5
    buf = io.BytesIO(data)

    # x offsets
    buf.seek(sprite_meta_start)
    x_offsets = [struct.unpack(">H", buf.read(2))[0] for _ in range(sprite_count)]

    # y offsets
    y_offsets = [struct.unpack(">H", buf.read(2))[0] for _ in range(sprite_count)]

    # sub widths
    sub_widths = [struct.unpack(">H", buf.read(2))[0] for _ in range(sprite_count)]

    # sub heights
    sub_heights = [struct.unpack(">H", buf.read(2))[0] for _ in range(sprite_count)]

    # palette: positioned before the per-sprite metadata
    palette_start = meta_pos - (palette_length - 1) * 3
    if palette_start < 0:
        return None

    palette = [0] * palette_length  # index 0 = transparent
    buf.seek(palette_start)
    for i in range(1, palette_length):
        r = data[buf.tell()]
        g = data[buf.tell() + 1]
        b = data[buf.tell() + 2]
        buf.seek(buf.tell() + 3)
        rgb = (r << 16) | (g << 8) | b
        palette[i] = rgb if rgb != 0 else 1  # 0 means transparent, remap to 1

    # decode first sprite's pixel data from start of buffer
    sub_w = sub_widths[0]
    sub_h = sub_heights[0]
    dimension = sub_w * sub_h
    if dimension == 0:
        return None

    buf.seek(0)
    flags = struct.unpack(">B", buf.read(1))[0]

    palette_indices = bytearray(dimension)
    pixel_alphas = bytearray(dimension)

    # read palette indices
    if (flags & FLAG_VERTICAL) == 0:
        # horizontal
        for j in range(dimension):
            palette_indices[j] = struct.unpack(">b", buf.read(1))[0] & 0xFF
    else:
        # vertical
        for x in range(sub_w):
            for y in range(sub_h):
                palette_indices[sub_w * y + x] = struct.unpack(">b", buf.read(1))[0] & 0xFF

    # read alpha channel
    if (flags & FLAG_ALPHA) != 0:
        if (flags & FLAG_VERTICAL) == 0:
            for j in range(dimension):
                pixel_alphas[j] = struct.unpack(">b", buf.read(1))[0] & 0xFF
        else:
            for x in range(sub_w):
                for y in range(sub_h):
                    pixel_alphas[sub_w * y + x] = struct.unpack(">b", buf.read(1))[0] & 0xFF
    else:
        # non-zero palette index = fully opaque
        for j in range(dimension):
            if palette_indices[j] != 0:
                pixel_alphas[j] = 0xFF

    # build RGBA pixels, normalized to max_width x max_height
    # (sub-sprite may be smaller with offsets)
    x_off = x_offsets[0]
    y_off = y_offsets[0]
    rgba = bytearray(max_width * max_height * 4)  # starts as transparent black

    for y in range(sub_h):
        for x in range(sub_w):
            src_idx = y * sub_w + x
            pi = palette_indices[src_idx]
            alpha = pixel_alphas[src_idx]

            dst_x = x + x_off
            dst_y = y + y_off
            if dst_x >= max_width or dst_y >= max_height:
                continue

            dst_idx = (dst_y * max_width + dst_x) * 4
            rgb = palette[pi]
            rgba[dst_idx] = (rgb >> 16) & 0xFF      # R
            rgba[dst_idx + 1] = (rgb >> 8) & 0xFF    # G
            rgba[dst_idx + 2] = rgb & 0xFF            # B
            rgba[dst_idx + 3] = alpha                  # A

    return SpriteData(width=max_width, height=max_height, pixels=bytes(rgba))


def load_all_texture_sprites(
    cache: CacheReader,
    texture_ids: set[int] | None = None,
) -> dict[int, SpriteData]:
    """Load texture sprites by decoding IndexedImage data from cache idx5.

    For each texture ID defined in textures.dat:
    1. Look up the sprite group fileId(s)
    2. Read from idx5, gzip decompress, decode as IndexedImage
    3. Convert to RGBA SpriteData

    Args:
        cache: CacheReader for reading textures.dat and sprite data.
        texture_ids: optional set of texture IDs to load. If None, loads all.

    Returns:
        texture_id -> SpriteData mapping for all successfully decoded textures.
    """
    tex_defs = _parse_textures_dat(cache)

    if texture_ids is not None:
        ids_to_load = texture_ids
    else:
        ids_to_load = set(tex_defs.keys())

    sprites: dict[int, SpriteData] = {}
    decoded_count = 0
    failed_count = 0

    for tex_id in sorted(ids_to_load):
        file_ids = tex_defs.get(tex_id)
        if not file_ids:
            continue

        # use first sprite group ID (most textures have just one)
        sprite_group_id = file_ids[0]
        raw = cache.get(SPRITE_CACHE_INDEX, sprite_group_id)
        if raw is None:
            failed_count += 1
            continue

        # gzip decompress
        try:
            decompressed = gzip.decompress(raw)
        except Exception:
            failed_count += 1
            continue

        sprite = _decode_indexed_image(decompressed)
        if sprite is None:
            failed_count += 1
            continue

        sprites[tex_id] = sprite
        decoded_count += 1

    print(
        f"  textures: {decoded_count} decoded from cache idx5, {failed_count} failed",
        file=sys.stderr,
    )
    return sprites


def build_atlas(
    sprites: dict[int, SpriteData],
    cell_size: int = TEXTURE_SIZE,
    repeat_v_padding: int = 0,
) -> TextureAtlas:
    """Build a texture atlas from decoded sprites.

    Layout: grid of cell_size x cell_size cells.
    Slot 0: solid white (for non-textured faces -- vertex colors show through).
    Slots 1..N: actual texture sprites, resized to cell_size if needed.
    repeat_v_padding adds wrapped vertical guard pixels around each texture
    cell so projected model UVs can emulate RuneLite's V/T repeat behavior
    without sampling a neighboring atlas cell.

    Returns TextureAtlas with RGBA pixel data and UV mapping.
    """
    # sort texture IDs for deterministic layout
    tex_ids = sorted(sprites.keys())
    total_slots = 1 + len(tex_ids)  # slot 0 = white

    repeat_v_padding = max(0, int(repeat_v_padding))
    slot_w = cell_size
    slot_h = cell_size + repeat_v_padding * 2
    cols = ATLAS_COLS
    rows = (total_slots + cols - 1) // cols

    atlas_w = cols * slot_w
    atlas_h = rows * slot_h
    atlas_pixels = bytearray(atlas_w * atlas_h * 4)

    # slot 0: solid white
    for y in range(slot_h):
        for x in range(slot_w):
            idx = (y * atlas_w + x) * 4
            atlas_pixels[idx] = 255
            atlas_pixels[idx + 1] = 255
            atlas_pixels[idx + 2] = 255
            atlas_pixels[idx + 3] = 255

    uv_map: dict[int, tuple[float, float, float, float]] = {}
    anim_map: dict[int, tuple[int, int, int, int, int]] = {}

    for slot_idx, tex_id in enumerate(tex_ids, start=1):
        sprite = sprites[tex_id]
        col = slot_idx % cols
        row = slot_idx // cols
        ax = col * slot_w
        ay = row * slot_h

        # copy sprite pixels into atlas cell, resizing if needed
        _blit_sprite_to_atlas(
            atlas_pixels, atlas_w, ax, ay, cell_size, sprite,
            repeat_v_padding=repeat_v_padding,
        )

        # UV mapping: normalized coordinates
        u_off = ax / atlas_w
        v_off = (ay + repeat_v_padding) / atlas_h
        u_size = cell_size / atlas_w
        v_size = cell_size / atlas_h
        uv_map[tex_id] = (u_off, v_off, u_size, v_size)
        anim_map[tex_id] = (ax, ay, cell_size, slot_h, repeat_v_padding)

    # white pixel UV (center of slot 0)
    white_u = 0.5 * slot_w / atlas_w
    white_v = (repeat_v_padding + 0.5 * cell_size) / atlas_h

    return TextureAtlas(
        width=atlas_w,
        height=atlas_h,
        pixels=bytes(atlas_pixels),
        uv_map=uv_map,
        anim_map=anim_map,
        repeat_v_margin=(repeat_v_padding / cell_size) if cell_size > 0 else 0.0,
        white_u=white_u,
        white_v=white_v,
    )


def _blit_sprite_to_atlas(
    atlas: bytearray,
    atlas_w: int,
    ax: int,
    ay: int,
    cell_size: int,
    sprite: SpriteData,
    repeat_v_padding: int = 0,
) -> None:
    """Copy sprite pixels into an atlas cell, nearest-neighbor resize if needed."""
    sw = sprite.width
    sh = sprite.height
    sp = sprite.pixels
    slot_h = cell_size + repeat_v_padding * 2

    for dy in range(slot_h):
        logical_y = dy - repeat_v_padding
        wrapped_y = logical_y % cell_size
        for dx in range(cell_size):
            # source pixel (nearest neighbor)
            sx = dx * sw // cell_size
            sy = wrapped_y * sh // cell_size
            si = (sy * sw + sx) * 4

            # destination in atlas
            di = ((ay + dy) * atlas_w + (ax + dx)) * 4

            if si + 3 < len(sp):
                atlas[di] = sp[si]
                atlas[di + 1] = sp[si + 1]
                atlas[di + 2] = sp[si + 2]
                atlas[di + 3] = sp[si + 3]


def write_atlas_binary(path: Path, atlas: TextureAtlas) -> None:
    """Write texture atlas as raw RGBA binary with header.

    Format:
      uint32 magic = 0x41544C53 ("ATLS")
      uint32 width
      uint32 height
      uint8  pixels[width * height * 4]  (RGBA)
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as f:
        f.write(struct.pack("<I", 0x41544C53))  # "ATLS"
        f.write(struct.pack("<I", atlas.width))
        f.write(struct.pack("<I", atlas.height))
        f.write(atlas.pixels)


def write_texture_anim_binary(path: Path, atlas: TextureAtlas,
                              texture_defs: dict[int, object]) -> None:
    """Write atlas-cell animation metadata for cache animated textures."""
    rows: list[tuple[int, int, int, int, int, int, int]] = []
    for tex_id, uv in sorted(atlas.uv_map.items()):
        tex = texture_defs.get(tex_id)
        direction = int(getattr(tex, "animation_direction", 0)) if tex else 0
        speed = int(getattr(tex, "animation_speed", 0)) if tex else 0
        if direction == 0 or speed == 0:
            continue
        if atlas.anim_map and tex_id in atlas.anim_map:
            x, y, w, h, repeat_v_padding = atlas.anim_map[tex_id]
        else:
            u, v, uw, vh = uv
            x = round(u * atlas.width)
            y = round(v * atlas.height)
            w = round(uw * atlas.width)
            h = round(vh * atlas.height)
            repeat_v_padding = 0
        rows.append((
            tex_id,
            x,
            y,
            w,
            h,
            direction & 0xFF,
            speed & 0xFF,
            repeat_v_padding,
        ))

    with path.open("wb") as f:
        f.write(struct.pack("<III", TEXTURE_ANIM_MAGIC,
                            TEXTURE_ANIM_VERSION, len(rows)))
        for tex_id, x, y, w, h, direction, speed, repeat_v_padding in rows:
            f.write(struct.pack("<IHHHHBBH", tex_id, x, y, w, h,
                                direction, speed, repeat_v_padding))
