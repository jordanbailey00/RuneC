"""Export b237 terrain mesh from the repo-local OpenRS2 cache.

For each region in the export area:
  1. Parse terrain data: heightmap, underlay IDs, overlay IDs, shapes, settings
  2. Decode floor definitions (underlays/overlays) for tile colors
  3. Compute per-vertex lighting (directional light + shadow map blur)
  4. Blend underlay colors in an 11x11 window (smooth terrain gradients)
  5. Output vertex-colored terrain mesh as binary .terrain file

The terrain binary is loaded by rc-viewer terrain code.

Usage:
    python3 tools/cache_pipeline/export_terrain.py \
        --regions "35,47 35,48" \
        --output data/zulrah.terrain
"""

import argparse
import gzip
import io
import math
import struct
import sys
from dataclasses import dataclass, field
from functools import lru_cache
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from export_collision_map import (
    CONFIG_INDEX,
    MAP_INDEX,
    MANIFEST_ARCHIVE,
    CacheReader,
    _read_string,
    decode_archive,
    hash_archive_name,
    load_map_index,
)
from modern_cache_reader import ModernCacheReader
from rc_cache import RcCacheStore, find_all_map_region_files, read_map_region_file
from rc_cache.terrain import calculate_height
from source_inputs import B237_CACHE, require_path

# --- OSRS noise functions (from MapRegion.java) ---

# --- floor definition decoder ---


@dataclass
class FloorDef:
    """Floor definition (underlay or overlay)."""

    floor_id: int = 0
    rgb: int = 0
    texture: int = -1
    hide_underlay: bool = True
    secondary_rgb: int = -1
    # computed HSL fields
    hue: int = 0
    saturation: int = 0
    lightness: int = 0
    secondary_hue: int = 0
    secondary_saturation: int = 0
    secondary_lightness: int = 0
    # blend fields (for underlays)
    blend_hue: int = 0
    blend_hue_multiplier: int = 0
    luminance: int = 0
    hsl16: int = 0


def _rgb_to_hsl(rgb: int, flo: FloorDef) -> None:
    """Convert RGB to HSL fields on a FloorDef (MapRegion.rgbToHsl / FloorDefinition.setHsl)."""
    r = ((rgb >> 16) & 0xFF) / 256.0
    g = ((rgb >> 8) & 0xFF) / 256.0
    b = (rgb & 0xFF) / 256.0

    mn = min(r, g, b)
    mx = max(r, g, b)

    h = 0.0
    s = 0.0
    l = (mn + mx) / 2.0

    if mn != mx:
        if l < 0.5:
            s = (mx - mn) / (mx + mn)
        else:
            s = (mx - mn) / (2.0 - mx - mn)

        if r == mx:
            h = (g - b) / (mx - mn)
        elif g == mx:
            h = 2.0 + (b - r) / (mx - mn)
        elif b == mx:
            h = 4.0 + (r - g) / (mx - mn)

    h /= 6.0

    flo.hue = max(0, min(255, int(h * 256.0)))
    flo.saturation = max(0, min(255, int(s * 256.0)))
    flo.lightness = max(0, min(255, int(l * 256.0)))
    flo.luminance = flo.lightness

    if l > 0.5:
        flo.blend_hue_multiplier = int((1.0 - l) * s * 512.0)
    else:
        flo.blend_hue_multiplier = int(l * s * 512.0)

    if flo.blend_hue_multiplier < 1:
        flo.blend_hue_multiplier = 1

    flo.blend_hue = int(h * flo.blend_hue_multiplier)
    flo.hsl16 = _hsl24to16(flo.hue, flo.saturation, flo.luminance)


def _hsl24to16(h: int, s: int, l: int) -> int:
    """Convert 24-bit HSL to 16-bit packed HSL (FloorDefinition.hsl24to16)."""
    if l > 179:
        s //= 2
    if l > 192:
        s //= 2
    if l > 217:
        s //= 2
    if l > 243:
        s //= 2
    return ((h // 4) << 10) + ((s // 32) << 7) + l // 2


def decode_floor_definitions(cache: CacheReader) -> tuple[dict[int, FloorDef], dict[int, FloorDef]]:
    """Decode underlay and overlay floor definitions from cache."""
    raw = cache.get(CONFIG_INDEX, 2)  # CONFIG_ARCHIVE
    if raw is None:
        sys.exit("could not read config archive")

    archive = decode_archive(raw)

    underlays: dict[int, FloorDef] = {}
    overlays: dict[int, FloorDef] = {}

    # decode underlays
    ukey = hash_archive_name("underlays.dat") & 0xFFFFFFFF
    udata = archive.get(ukey) or archive.get(hash_archive_name("underlays.dat"))
    if udata:
        buf = io.BytesIO(udata)
        count = struct.unpack(">H", buf.read(2))[0]
        for _ in range(count + 1):
            raw_id = buf.read(2)
            if len(raw_id) < 2:
                break
            fid = struct.unpack(">H", raw_id)[0]
            if fid == 0xFFFF:
                continue
            raw_len = buf.read(2)
            if len(raw_len) < 2:
                break
            length = struct.unpack(">H", raw_len)[0]
            entry_data = buf.read(length)

            flo = FloorDef(floor_id=fid)
            # parse underlay opcodes
            ebuf = io.BytesIO(entry_data)
            while True:
                op = ebuf.read(1)
                if not op:
                    break
                op = op[0]
                if op == 0:
                    break
                elif op == 1:
                    flo.rgb = (ebuf.read(1)[0] << 16) + (ebuf.read(1)[0] << 8) + ebuf.read(1)[0]

            # generate HSL + blend fields
            if flo.secondary_rgb != -1:
                _rgb_to_hsl(flo.secondary_rgb, flo)
            _rgb_to_hsl(flo.rgb, flo)
            underlays[fid] = flo

    # decode overlays
    okey = hash_archive_name("overlays.dat") & 0xFFFFFFFF
    odata = archive.get(okey) or archive.get(hash_archive_name("overlays.dat"))
    if odata:
        buf = io.BytesIO(odata)
        count = struct.unpack(">H", buf.read(2))[0]
        for _ in range(count + 1):
            raw_id = buf.read(2)
            if len(raw_id) < 2:
                break
            fid = struct.unpack(">H", raw_id)[0]
            if fid == 0xFFFF:
                continue
            raw_len = buf.read(2)
            if len(raw_len) < 2:
                break
            length = struct.unpack(">H", raw_len)[0]
            entry_data = buf.read(length)

            flo = FloorDef(floor_id=fid)
            ebuf = io.BytesIO(entry_data)
            while True:
                op = ebuf.read(1)
                if not op:
                    break
                op = op[0]
                if op == 0:
                    break
                elif op == 1:
                    flo.rgb = (ebuf.read(1)[0] << 16) + (ebuf.read(1)[0] << 8) + ebuf.read(1)[0]
                elif op == 2:
                    flo.texture = ebuf.read(1)[0]
                elif op == 5:
                    flo.hide_underlay = False
                elif op == 7:
                    flo.secondary_rgb = (ebuf.read(1)[0] << 16) + (ebuf.read(1)[0] << 8) + ebuf.read(1)[0]

            # post-decode HSL
            if flo.secondary_rgb != -1:
                _rgb_to_hsl(flo.secondary_rgb, flo)
                flo.secondary_hue = flo.hue
                flo.secondary_saturation = flo.saturation
                flo.secondary_lightness = flo.lightness
            _rgb_to_hsl(flo.rgb, flo)
            overlays[fid] = flo

    return underlays, overlays


# --- modern cache floor definition + texture color decoders ---

# modern cache config index groups for floor definitions
MODERN_UNDERLAY_GROUP = 1
MODERN_OVERLAY_GROUP = 4


def _decode_underlay_entry(fid: int, data: bytes) -> FloorDef:
    """Decode a single underlay floor definition from modern cache opcode stream.

    Underlay opcodes: 1=rgb(3 bytes), 0=terminator.
    """
    flo = FloorDef(floor_id=fid)
    buf = io.BytesIO(data)
    while True:
        op = buf.read(1)
        if not op or op[0] == 0:
            break
        if op[0] == 1:
            flo.rgb = (buf.read(1)[0] << 16) + (buf.read(1)[0] << 8) + buf.read(1)[0]
    if flo.secondary_rgb != -1:
        _rgb_to_hsl(flo.secondary_rgb, flo)
    _rgb_to_hsl(flo.rgb, flo)
    return flo


def _decode_overlay_entry(fid: int, data: bytes) -> FloorDef:
    """Decode a single overlay floor definition from modern cache opcode stream.

    Overlay opcodes (matching RuneLite OverlayDefinition):
      1=rgb(3 bytes), 2=texture(1 byte), 5=hide_underlay=false (no data),
      7=secondary_rgb(3 bytes), 0=terminator.
    """
    flo = FloorDef(floor_id=fid)
    buf = io.BytesIO(data)
    while True:
        op = buf.read(1)
        if not op or op[0] == 0:
            break
        opcode = op[0]
        if opcode == 1:
            flo.rgb = (buf.read(1)[0] << 16) + (buf.read(1)[0] << 8) + buf.read(1)[0]
        elif opcode == 2:
            flo.texture = buf.read(1)[0]
        elif opcode == 5:
            flo.hide_underlay = False
        elif opcode == 7:
            flo.secondary_rgb = (buf.read(1)[0] << 16) + (buf.read(1)[0] << 8) + buf.read(1)[0]
    # post-decode HSL
    if flo.secondary_rgb != -1:
        _rgb_to_hsl(flo.secondary_rgb, flo)
        flo.secondary_hue = flo.hue
        flo.secondary_saturation = flo.saturation
        flo.secondary_lightness = flo.lightness
    _rgb_to_hsl(flo.rgb, flo)
    return flo


def decode_floor_definitions_modern(
    reader: ModernCacheReader,
) -> tuple[dict[int, FloorDef], dict[int, FloorDef]]:
    """Decode underlay and overlay floor definitions from modern cache.

    Modern cache stores floor defs in config index 2:
      group 1 = underlays, group 4 = overlays.
    Each entry is a separate file within the group.
    """
    underlays: dict[int, FloorDef] = {}
    overlays: dict[int, FloorDef] = {}

    # underlays: config index 2, group 1
    underlay_files = reader.read_group(2, MODERN_UNDERLAY_GROUP)
    for fid, data in underlay_files.items():
        underlays[fid] = _decode_underlay_entry(fid, data)

    # overlays: config index 2, group 4
    overlay_files = reader.read_group(2, MODERN_OVERLAY_GROUP)
    for fid, data in overlay_files.items():
        overlays[fid] = _decode_overlay_entry(fid, data)

    return underlays, overlays


def load_texture_average_colors_modern(reader: ModernCacheReader) -> dict[int, int]:
    """Load per-texture average color (HSL16) from modern cache.

    Modern OSRS (rev233+) texture format in index 9, group 0:
      u16 fileId (sprite group in index 8)
      u16 missingColor (HSL16 fallback — used as the texture's average color)
      u8  field1778
      u8  animationDirection
      u8  animationSpeed

    The missingColor field is the precomputed average color of the texture
    sprite, stored as 16-bit packed HSL. This is what the OSRS client uses
    for terrain tile coloring.
    """
    tex_files = reader.read_group(9, 0)
    result: dict[int, int] = {}
    for tex_id, data in tex_files.items():
        if len(data) < 4:
            continue
        missing_color = struct.unpack(">H", data[2:4])[0]
        result[tex_id] = missing_color
    return result


# --- terrain parser ---


@dataclass
class RegionTerrain:
    """Parsed terrain data for one 64x64 region."""

    region_x: int = 0
    region_y: int = 0
    # [4][64+1][64+1] - tile corner heights (need +1 for neighbor corners)
    heights: list = field(default_factory=list)
    # [4][64][64]
    underlay_ids: list = field(default_factory=list)
    overlay_ids: list = field(default_factory=list)
    shapes: list = field(default_factory=list)
    rotations: list = field(default_factory=list)
    settings: list = field(default_factory=list)


def parse_terrain_full(
    data: bytes, region_chunk_x: int, region_chunk_y: int
) -> RegionTerrain:
    """Parse terrain binary to extract heights, underlays, overlays, shapes, settings."""
    rt = RegionTerrain()
    rt.heights = [[[0 for _ in range(65)] for _ in range(65)] for _ in range(4)]
    rt.underlay_ids = [[[0 for _ in range(64)] for _ in range(64)] for _ in range(4)]
    rt.overlay_ids = [[[0 for _ in range(64)] for _ in range(64)] for _ in range(4)]
    rt.shapes = [[[0 for _ in range(64)] for _ in range(64)] for _ in range(4)]
    rt.rotations = [[[0 for _ in range(64)] for _ in range(64)] for _ in range(4)]
    rt.settings = [[[0 for _ in range(64)] for _ in range(64)] for _ in range(4)]

    buf = io.BytesIO(data)

    for plane in range(4):
        for x in range(64):
            for y in range(64):
                rt.settings[plane][x][y] = 0
                while True:
                    raw = buf.read(2)
                    if len(raw) < 2:
                        break
                    attr = struct.unpack(">H", raw)[0]

                    if attr == 0:
                        # procedural height
                        if plane == 0:
                            rt.heights[0][x][y] = -calculate_height(
                                0xE3B7B + x + region_chunk_x,
                                0x87CCE + y + region_chunk_y,
                            ) * 8
                        else:
                            rt.heights[plane][x][y] = rt.heights[plane - 1][x][y] - 240
                        break
                    elif attr == 1:
                        height = buf.read(1)[0]
                        if height == 1:
                            height = 0
                        if plane == 0:
                            rt.heights[0][x][y] = -height * 8
                        else:
                            rt.heights[plane][x][y] = rt.heights[plane - 1][x][y] - height * 8
                        break
                    elif attr <= 49:
                        rt.overlay_ids[plane][x][y] = struct.unpack(">h", buf.read(2))[0]
                        rt.shapes[plane][x][y] = (attr - 2) // 4
                        rt.rotations[plane][x][y] = (attr - 2) & 3
                    elif attr <= 81:
                        rt.settings[plane][x][y] = attr - 49
                    else:
                        rt.underlay_ids[plane][x][y] = attr - 81

    return rt


# --- lighting + color blending (mirrors method171) ---


def hsl_encode(hue: int, sat: int, light: int) -> int:
    """Encode HSL to 16-bit (method177 / hsl24to16)."""
    if light > 179:
        sat //= 2
    if light > 192:
        sat //= 2
    if light > 217:
        sat //= 2
    if light > 243:
        sat //= 2
    return ((hue // 4) << 10) + ((sat // 32) << 7) + light // 2


def apply_light(hsl16: int, light: int) -> int:
    """Apply lighting intensity to an HSL16 color (method187)."""
    if hsl16 == -1:
        return 0xBC614E
    l = (light * (hsl16 & 0x7F)) // 128
    if l < 2:
        l = 2
    elif l > 126:
        l = 126
    return (hsl16 & 0xFF80) + l


def hsl16_to_rgb(hsl16: int) -> tuple[int, int, int]:
    """Convert 16-bit packed HSL to RGB.

    16-bit HSL: (hue << 10) | (sat << 7) | lightness
    hue: 6 bits (0-63), sat: 3 bits (0-7), lightness: 7 bits (0-127)
    Same as the Rasterizer3D palette lookup.
    """
    if hsl16 == 0xBC614E or hsl16 < 0:
        return (0, 0, 0)

    h_raw = (hsl16 >> 10) & 0x3F
    s_raw = (hsl16 >> 7) & 0x7
    l_raw = hsl16 & 0x7F

    hue_f = h_raw / 64.0 + 0.0078125
    sat_f = s_raw / 8.0 + 0.0625
    light_f = l_raw / 128.0

    r, g, b = light_f, light_f, light_f

    if sat_f != 0.0:
        if light_f < 0.5:
            q = light_f * (1.0 + sat_f)
        else:
            q = light_f + sat_f - light_f * sat_f
        p = 2.0 * light_f - q

        h_r = hue_f + 1.0 / 3.0
        if h_r > 1.0:
            h_r -= 1.0
        h_g = hue_f
        h_b = hue_f - 1.0 / 3.0
        if h_b < 0.0:
            h_b += 1.0

        r = _hue_channel(p, q, h_r)
        g = _hue_channel(p, q, h_g)
        b = _hue_channel(p, q, h_b)

    ri = max(0, min(255, int(r * 256.0)))
    gi = max(0, min(255, int(g * 256.0)))
    bi = max(0, min(255, int(b * 256.0)))
    return (ri, gi, bi)


def _hue_channel(p: float, q: float, t: float) -> float:
    if 6.0 * t < 1.0:
        return p + (q - p) * 6.0 * t
    if 2.0 * t < 1.0:
        return q
    if 3.0 * t < 2.0:
        return p + (q - p) * (2.0 / 3.0 - t) * 6.0
    return p


# --- terrain mesh builder ---


@dataclass
class TerrainVertex:
    """A single terrain vertex with position and color."""

    x: float = 0.0
    y: float = 0.0  # height (OSRS Y = up)
    z: float = 0.0
    r: int = 0
    g: int = 0
    b: int = 0


def stitch_region_edges(
    regions: dict[tuple[int, int], RegionTerrain],
) -> None:
    """Stitch height values at region boundaries.

    Each region's height array is [65][65] but only indices 0..63 are computed.
    Index 64 (the far edge) defaults to 0, causing visible seams. Fix by copying
    heights from the neighboring region's index 0 to the current region's index 64.

    Also computes edge heights for regions without a neighbor by extrapolating
    from the nearest interior values.
    """
    for (rx, ry), rt in regions.items():
        for plane in range(4):
            # stitch X=64 edge from neighbor (rx+1, ry)
            neighbor_x = regions.get((rx + 1, ry))
            for y in range(65):
                if neighbor_x and y < 65:
                    # use neighbor's x=0 column
                    ny = min(y, 64)
                    rt.heights[plane][64][y] = neighbor_x.heights[plane][0][ny]
                elif y <= 63:
                    # no neighbor: extrapolate from x=63
                    rt.heights[plane][64][y] = rt.heights[plane][63][y]

            # stitch Y=64 edge from neighbor (rx, ry+1)
            neighbor_y = regions.get((rx, ry + 1))
            for x in range(65):
                if neighbor_y and x < 65:
                    nx = min(x, 64)
                    rt.heights[plane][x][64] = neighbor_y.heights[plane][nx][0]
                elif x <= 63:
                    rt.heights[plane][x][64] = rt.heights[plane][x][63]

            # corner (64, 64): prefer diagonal neighbor
            diag = regions.get((rx + 1, ry + 1))
            if diag:
                rt.heights[plane][64][64] = diag.heights[plane][0][0]
            elif neighbor_x:
                rt.heights[plane][64][64] = neighbor_x.heights[plane][0][min(64, 63)]
            elif neighbor_y:
                rt.heights[plane][64][64] = neighbor_y.heights[plane][min(64, 63)][0]
            else:
                rt.heights[plane][64][64] = rt.heights[plane][63][63]


def visual_source_plane(rt: RegionTerrain, tx: int, ty: int, target_plane: int) -> int:
    if target_plane < 3 and rt.settings[target_plane + 1][tx][ty] & 0x2:
        return target_plane + 1
    return target_plane


def _context_tile(
    regions: dict[tuple[int, int], RegionTerrain],
    region_x: int,
    region_y: int,
    tile_x: int,
    tile_y: int,
) -> tuple[RegionTerrain, int, int] | None:
    offset_x, local_x = divmod(tile_x, 64)
    offset_y, local_y = divmod(tile_y, 64)
    terrain = regions.get((region_x + offset_x, region_y + offset_y))
    if terrain is None:
        return None
    return terrain, local_x, local_y


def _context_height(
    regions: dict[tuple[int, int], RegionTerrain],
    fallback: RegionTerrain,
    region_x: int,
    region_y: int,
    plane: int,
    corner_x: int,
    corner_y: int,
) -> int:
    sample = _context_tile(
        regions, region_x, region_y, corner_x, corner_y)
    if sample is not None:
        terrain, local_x, local_y = sample
        return terrain.heights[plane][local_x][local_y]
    local_x = max(0, min(64, corner_x))
    local_y = max(0, min(64, corner_y))
    return fallback.heights[plane][local_x][local_y]


TILE_SHAPE_POINTS: tuple[tuple[int, ...], ...] = (
    (1, 3, 5, 7),
    (1, 3, 5, 7),
    (1, 3, 5, 7),
    (1, 3, 5, 7, 6),
    (1, 3, 5, 7, 6),
    (1, 3, 5, 7, 6),
    (1, 3, 5, 7, 6),
    (1, 3, 5, 7, 2, 6),
    (1, 3, 5, 7, 2, 8),
    (1, 3, 5, 7, 2, 8),
    (1, 3, 5, 7, 11, 12),
    (1, 3, 5, 7, 11, 12),
    (1, 3, 5, 7, 13, 14),
)

TILE_SHAPE_PATHS: tuple[tuple[int, ...], ...] = (
    (0, 1, 2, 3, 0, 0, 1, 3),
    (1, 1, 2, 3, 1, 0, 1, 3),
    (0, 1, 2, 3, 1, 0, 1, 3),
    (0, 0, 1, 2, 0, 0, 2, 4, 1, 0, 4, 3),
    (0, 0, 1, 4, 0, 0, 4, 3, 1, 1, 2, 4),
    (0, 0, 4, 3, 1, 0, 1, 2, 1, 0, 2, 4),
    (0, 1, 2, 4, 1, 0, 1, 4, 1, 0, 4, 3),
    (0, 4, 1, 2, 0, 4, 2, 5, 1, 0, 4, 5, 1, 0, 5, 3),
    (0, 4, 1, 2, 0, 4, 2, 3, 0, 4, 3, 5, 1, 0, 4, 5),
    (0, 0, 4, 5, 1, 4, 1, 2, 1, 4, 2, 3, 1, 4, 3, 5),
    (
        0, 0, 1, 5, 0, 1, 4, 5, 0, 1, 2, 4,
        1, 0, 5, 3, 1, 5, 4, 3, 1, 4, 2, 3,
    ),
    (
        1, 0, 1, 5, 1, 1, 4, 5, 1, 1, 2, 4,
        0, 0, 5, 3, 0, 5, 4, 3, 0, 4, 2, 3,
    ),
    (
        1, 0, 5, 4, 1, 0, 1, 5, 0, 0, 4, 3,
        0, 4, 5, 3, 0, 5, 2, 3, 0, 1, 2, 5,
    ),
)


def _rotate_scene_tile_point(point: int, rotation: int) -> int:
    rotation &= 3
    if (point & 1) == 0 and point <= 8:
        return ((point - rotation - rotation - 1) & 7) + 1
    if 8 < point <= 12:
        return ((point - 9 - rotation) & 3) + 9
    if 12 < point <= 16:
        return ((point - 13 - rotation) & 3) + 13
    return point


def _scene_tile_vertex(
    point: int,
    wx: float,
    wz: float,
    heights: tuple[int, int, int, int],
    y_offset: float = 0.0,
) -> tuple[float, float, float]:
    h_sw, h_se, h_ne, h_nw = heights
    scale_h = -1.0 / 128.0
    table = {
        1: (0.0, 0.0, h_sw),
        2: (0.5, 0.0, (h_sw + h_se) * 0.5),
        3: (1.0, 0.0, h_se),
        4: (1.0, 0.5, (h_se + h_ne) * 0.5),
        5: (1.0, 1.0, h_ne),
        6: (0.5, 1.0, (h_ne + h_nw) * 0.5),
        7: (0.0, 1.0, h_nw),
        8: (0.0, 0.5, (h_nw + h_sw) * 0.5),
        9: (0.5, 0.25, (h_sw + h_se) * 0.5),
        10: (0.75, 0.5, (h_se + h_ne) * 0.5),
        11: (0.5, 0.75, (h_ne + h_nw) * 0.5),
        12: (0.25, 0.5, (h_nw + h_sw) * 0.5),
        13: (0.25, 0.25, h_sw),
        14: (0.75, 0.25, h_se),
        15: (0.75, 0.75, h_ne),
        16: (0.25, 0.75, h_nw),
    }
    fx, fy, height = table.get(point, table[1])
    return wx + fx, float(height * scale_h + y_offset), -(wz + fy)


def _emit_terrain_quad(
    verts: list[float],
    colors: list[int],
    wx: float,
    wz: float,
    heights: tuple[int, int, int, int],
    rgb: tuple[int, int, int],
    x0: float = 0.0,
    y0: float = 0.0,
    x1: float = 1.0,
    y1: float = 1.0,
    y_offset: float = 0.0,
) -> None:
    h_sw, h_se, h_ne, h_nw = heights
    scale_h = -1.0 / 128.0

    def height_at(fx: float, fy: float) -> float:
        south = h_sw * (1.0 - fx) + h_se * fx
        north = h_nw * (1.0 - fx) + h_ne * fx
        return float((south * (1.0 - fy) + north * fy) * scale_h + y_offset)

    def point(fx: float, fy: float) -> tuple[float, float, float]:
        return wx + fx, height_at(fx, fy), -(wz + fy)

    p_sw = point(x0, y0)
    p_se = point(x1, y0)
    p_ne = point(x1, y1)
    p_nw = point(x0, y1)
    r, g, b = rgb

    for p in (p_sw, p_se, p_ne, p_sw, p_ne, p_nw):
        verts.extend(p)
        colors.extend([r, g, b, 255])


def _emit_terrain_triangle(
    verts: list[float],
    colors: list[int],
    points: tuple[tuple[float, float, float], ...],
    rgb: tuple[int, int, int],
    y_offset: float = 0.0,
) -> None:
    r, g, b = rgb
    for x, y, z in points:
        verts.extend((x, y + y_offset, z))
        colors.extend([r, g, b, 255])


def _emit_shaped_terrain_tile(
    verts: list[float],
    colors: list[int],
    wx: float,
    wz: float,
    heights: tuple[int, int, int, int],
    shape: int,
    rotation: int,
    base_rgb: tuple[int, int, int] | None,
    overlay_rgb: tuple[int, int, int] | None,
) -> int:
    if shape < 0 or shape >= len(TILE_SHAPE_POINTS):
        return 0

    rotation &= 3
    points = tuple(
        _scene_tile_vertex(
            _rotate_scene_tile_point(point, rotation),
            wx,
            wz,
            heights,
        )
        for point in TILE_SHAPE_POINTS[shape]
    )
    emitted = 0
    path = TILE_SHAPE_PATHS[shape]
    for i in range(0, len(path), 4):
        color_kind = path[i]
        ia, ib, ic = path[i + 1], path[i + 2], path[i + 3]
        if ia < 4:
            ia = (ia - rotation) & 3
        if ib < 4:
            ib = (ib - rotation) & 3
        if ic < 4:
            ic = (ic - rotation) & 3

        rgb = base_rgb if color_kind == 0 else overlay_rgb
        if rgb is None:
            continue
        if ia >= len(points) or ib >= len(points) or ic >= len(points):
            continue
        _emit_terrain_triangle(
            verts,
            colors,
            (points[ia], points[ib], points[ic]),
            rgb,
            0.0,
        )
        emitted += 1
    return emitted


def _lit_underlay_rgb(
    regions: dict[tuple[int, int], RegionTerrain],
    region_x: int,
    region_y: int,
    z: int,
    tx: int,
    ty: int,
    underlays: dict[int, FloorDef],
    avg_light: int,
) -> tuple[int, int, int] | None:
    tile = _context_tile(regions, region_x, region_y, tx, ty)
    if tile is None:
        return None
    terrain, local_x, local_y = tile
    uid = terrain.underlay_ids[z][local_x][local_y]
    if uid <= 0:
        return None

    blend_h, blend_s, blend_l, blend_m, blend_c = 0, 0, 0, 0, 0
    for bx in range(tx - 5, tx + 6):
        for by in range(ty - 5, ty + 6):
            sample = _context_tile(regions, region_x, region_y, bx, by)
            if sample is None:
                continue
            sample_terrain, sample_x, sample_y = sample
            uid2 = sample_terrain.underlay_ids[z][sample_x][sample_y]
            if uid2 > 0:
                flo = underlays.get(uid2 - 1)
                if flo:
                    blend_h += flo.blend_hue
                    blend_s += flo.saturation
                    blend_l += flo.luminance
                    blend_m += flo.blend_hue_multiplier
                    blend_c += 1

    if blend_c <= 0 or blend_m <= 0:
        return None
    avg_hue = (blend_h * 256) // blend_m
    avg_sat = blend_s // blend_c
    avg_lum = blend_l // blend_c
    return hsl16_to_rgb(apply_light(hsl_encode(avg_hue, avg_sat, avg_lum),
                                    avg_light))


def overlay_hidden(oflo: FloorDef | None) -> bool:
    """An untextured overlay coloured 0xFF00FF: the client gives it colour
    12345678 and never draws it (OSRS's invisible floor)."""
    return oflo is not None and oflo.texture < 0 and oflo.rgb == 0xFF00FF


def _lit_overlay_rgb(
    oflo: FloorDef | None,
    tex_colors: dict[int, int] | None,
    avg_light: int,
) -> tuple[int, int, int] | None:
    if not oflo or oflo.rgb == 0xFF00FF:
        return None
    if oflo.texture >= 0 and tex_colors:
        tex_hsl = tex_colors.get(oflo.texture)
        if tex_hsl is not None:
            return hsl16_to_rgb(apply_light(tex_hsl, avg_light))
    overlay_hsl16 = hsl_encode(oflo.hue, oflo.saturation, oflo.lightness)
    return hsl16_to_rgb(apply_light(overlay_hsl16, avg_light))


def build_terrain_mesh(
    regions: dict[tuple[int, int], RegionTerrain],
    underlays: dict[int, FloorDef],
    overlays: dict[int, FloorDef],
    target_plane: int = 0,
    tex_colors: dict[int, int] | None = None,
    brightness: float = 1.0,
    context_regions: dict[tuple[int, int], RegionTerrain] | None = None,
) -> tuple[list[float], list[int]]:
    """Build a vertex-colored terrain mesh from parsed regions.

    Returns (vertices[N*3], colors[N*4]) as flat lists.
    Each tile = 2 triangles = 6 vertices.
    """
    verts: list[float] = []
    colors: list[int] = []
    context = context_regions or regions

    # we work region by region. for each tile, emit 2 triangles.
    # world coords: base_x = region_x * 64, base_y = region_y * 64
    # OSRS tile = 128 world units. we'll use 1 tile = 1 unit for simplicity.

    for (rx, ry), rt in sorted(regions.items()):
        base_wx = rx * 64
        base_wy = ry * 64

        # compute lighting per source plane (mirrors method171 lighting pass)
        # intensity[plane][65][65] for tile corners
        base_intensity = int(96 * brightness)
        intensities: dict[int, list[list[int]]] = {}

        light_x, light_y, light_z = -50, -10, -50
        light_len = int(math.sqrt(light_x * light_x + light_y * light_y + light_z * light_z))
        distribution = (768 * light_len) >> 8

        for z in {target_plane, min(target_plane + 1, 3)}:
            intensity = [[base_intensity] * 65 for _ in range(65)]
            for ty in range(65):
                for tx in range(65):
                    dx = _context_height(
                        context, rt, rx, ry, z, tx + 1, ty
                    ) - _context_height(
                        context, rt, rx, ry, z, tx - 1, ty
                    )
                    dy = _context_height(
                        context, rt, rx, ry, z, tx, ty + 1
                    ) - _context_height(
                        context, rt, rx, ry, z, tx, ty - 1
                    )
                    length = int(math.sqrt(dx * dx + 256 * 256 + dy * dy))
                    if length == 0:
                        length = 1
                    nx = (dx << 8) // length
                    ny = (256 << 8) // length
                    nz = (dy << 8) // length
                    intensity[tx][ty] = base_intensity + (light_x * nx + light_y * ny + light_z * nz) // distribution
            intensities[z] = intensity

        # compute blended underlay colors using 11x11 window
        # for simplicity, compute per-tile center color
        for ty in range(64):
            for tx in range(64):
                z = visual_source_plane(rt, tx, ty, target_plane)
                oid = rt.overlay_ids[z][tx][ty] & 0xFFFF
                shape = rt.shapes[z][tx][ty]
                rotation = rt.rotations[z][tx][ty]

                # get 4 corner heights
                h_sw = _context_height(context, rt, rx, ry, z, tx, ty)
                h_se = _context_height(context, rt, rx, ry, z, tx + 1, ty)
                h_ne = _context_height(
                    context, rt, rx, ry, z, tx + 1, ty + 1)
                h_nw = _context_height(context, rt, rx, ry, z, tx, ty + 1)
                heights = (h_sw, h_se, h_ne, h_nw)

                # corner lighting
                intensity = intensities[z]
                l_sw = intensity[tx][ty]
                l_se = intensity[min(tx + 1, 64)][ty]
                l_ne = intensity[min(tx + 1, 64)][min(ty + 1, 64)]
                l_nw = intensity[tx][min(ty + 1, 64)]
                avg_light = (l_sw + l_se + l_ne + l_nw) // 4

                underlay_rgb = _lit_underlay_rgb(
                    context, rx, ry, z, tx, ty, underlays, avg_light)
                oflo = overlays.get(oid - 1) if oid > 0 else None
                overlay_rgb = _lit_overlay_rgb(oflo, tex_colors, avg_light)

                base_rgb = underlay_rgb
                tile_rgb = overlay_rgb or base_rgb
                if tile_rgb is None:
                    continue

                wx = float(base_wx + tx)
                wz = float(base_wy + ty)

                if overlay_hidden(oflo):
                    # The client never draws this overlay: a full tile is a
                    # hole (a chasm's black void), a shaped one keeps only
                    # its underlay part.
                    if shape > 0 and base_rgb:
                        _emit_shaped_terrain_tile(verts, colors, wx, wz,
                                                  heights, shape + 1,
                                                  rotation, base_rgb, None)
                    continue

                if oid <= 0 or not overlay_rgb:
                    _emit_terrain_quad(verts, colors, wx, wz, heights,
                                       tile_rgb)
                    continue

                scene_shape = shape + 1
                if scene_shape == 1:
                    _emit_terrain_quad(verts, colors, wx, wz, heights,
                                       tile_rgb)
                    continue

                if base_rgb:
                    if _emit_shaped_terrain_tile(
                        verts,
                        colors,
                        wx,
                        wz,
                        heights,
                        scene_shape,
                        rotation,
                        base_rgb,
                        overlay_rgb,
                    ):
                        continue
                    _emit_terrain_quad(verts, colors, wx, wz, heights,
                                       tile_rgb)
                    continue

                if not _emit_shaped_terrain_tile(
                    verts,
                    colors,
                    wx,
                    wz,
                    heights,
                    scene_shape,
                    rotation,
                    None,
                    overlay_rgb,
                ):
                    _emit_terrain_quad(verts, colors, wx, wz, heights,
                                       overlay_rgb)

    return verts, colors


# --- binary output ---

TERR_MAGIC = 0x54455252  # "TERR"


def build_heightmap(
    regions: dict[tuple[int, int], RegionTerrain],
    target_plane: int = 0,
    resolve_link_below: bool = False,
    context_regions: dict[tuple[int, int], RegionTerrain] | None = None,
) -> tuple[int, int, int, int, list[float]]:
    """Build a corner-height grid from parsed regions.

    Returns (min_x, min_y, width, height, heights[width*height]).
    Heights are in terrain units (OSRS height / 128.0).
    """
    min_rx = min(rx for rx, _ in regions.keys())
    max_rx = max(rx for rx, _ in regions.keys())
    min_ry = min(ry for _, ry in regions.keys())
    max_ry = max(ry for _, ry in regions.keys())

    min_wx = min_rx * 64
    min_wy = min_ry * 64
    width = (max_rx - min_rx + 1) * 64 + 1
    height = (max_ry - min_ry + 1) * 64 + 1
    context = context_regions or regions

    # Default to 0 (flat) for uncovered corners.
    hmap = [0.0] * (width * height)
    scale_h = -1.0 / 128.0

    def setting_at(plane: int, world_x: int, world_y: int) -> int:
        region_x, local_x = divmod(world_x, 64)
        region_y, local_y = divmod(world_y, 64)
        terrain = context.get((region_x, region_y))
        if terrain is None:
            return 0
        return terrain.settings[plane][local_x][local_y]

    def linked_bridge_corner(world_x: int, world_y: int) -> bool:
        link_plane = target_plane + 1
        if link_plane > 3:
            return False
        for ox, oy in ((0, 0), (-1, 0), (0, -1), (-1, -1)):
            if setting_at(link_plane, world_x + ox, world_y + oy) & 0x2:
                return True
        return False

    def corner_height(plane: int, world_x: int, world_y: int) -> int | None:
        region_x, local_x = divmod(world_x, 64)
        region_y, local_y = divmod(world_y, 64)
        terrain = context.get((region_x, region_y))
        if terrain is not None:
            return terrain.heights[plane][local_x][local_y]

        candidates: list[tuple[int, int, int, int]] = []
        if local_x == 0 and local_y == 0:
            candidates.append((region_x - 1, region_y - 1, 64, 64))
        if local_x == 0:
            candidates.append((region_x - 1, region_y, 64, local_y))
        if local_y == 0:
            candidates.append((region_x, region_y - 1, local_x, 64))
        for fallback_x, fallback_y, sample_x, sample_y in candidates:
            fallback = context.get((fallback_x, fallback_y))
            if fallback is not None:
                return fallback.heights[plane][sample_x][sample_y]
        return None

    for local_y in range(height):
        world_y = min_wy + local_y
        for local_x in range(width):
            world_x = min_wx + local_x
            plane = target_plane
            if resolve_link_below and linked_bridge_corner(world_x, world_y):
                plane = target_plane + 1
            raw_height = corner_height(plane, world_x, world_y)
            if raw_height is not None:
                hmap[local_x + local_y * width] = float(raw_height) * scale_h

    return min_wx, min_wy, width, height, hmap


def write_terrain_binary(
    output_path: Path,
    verts: list[float],
    colors: list[int],
    regions: dict[tuple[int, int], RegionTerrain],
    heightmap: tuple[int, int, int, int, list[float]],
) -> None:
    """Write terrain mesh to binary .terrain file.

    Format:
      magic: uint32 "TERR"
      vertex_count: uint32
      region_count: uint32
      min_world_x: int32  (for coordinate offset)
      min_world_y: int32
      vertices: float32[vertex_count * 3]  (x, y, z)
      colors: uint8[vertex_count * 4]  (r, g, b, a)
      heightmap_min_x: int32
      heightmap_min_y: int32
      heightmap_width: uint32
      heightmap_height: uint32
      heightmap: float32[width * height]
    """
    output_path.parent.mkdir(parents=True, exist_ok=True)
    vert_count = len(verts) // 3

    # find min world coords for centering
    min_wx = min(rx * 64 for rx, _ in regions.keys())
    min_wy = min(ry * 64 for _, ry in regions.keys())

    hm_min_x, hm_min_y, hm_w, hm_h, hm_data = heightmap

    with open(output_path, "wb") as f:
        f.write(struct.pack("<I", TERR_MAGIC))
        f.write(struct.pack("<I", vert_count))
        f.write(struct.pack("<I", len(regions)))
        f.write(struct.pack("<i", min_wx))
        f.write(struct.pack("<i", min_wy))

        # vertices
        for v in verts:
            f.write(struct.pack("<f", v))

        # colors
        for c in colors:
            f.write(struct.pack("B", c))

        # heightmap
        f.write(struct.pack("<i", hm_min_x))
        f.write(struct.pack("<i", hm_min_y))
        f.write(struct.pack("<I", hm_w))
        f.write(struct.pack("<I", hm_h))
        for h in hm_data:
            f.write(struct.pack("<f", h))


# --- main ---


def _main_317(args: argparse.Namespace) -> None:
    """Export terrain from 317-format cache."""
    if not args.cache.exists():
        sys.exit(f"cache directory not found: {args.cache}")

    print(f"reading cache from {args.cache}")
    cache = CacheReader(args.cache)

    print("loading floor definitions...")
    underlays, overlays_defs = decode_floor_definitions(cache)
    print(f"  {len(underlays)} underlays, {len(overlays_defs)} overlays")

    print("loading texture average colors...")
    from export_models import load_texture_average_colors
    tex_colors = load_texture_average_colors(cache)
    print(f"  {len(tex_colors)} texture colors")

    print("loading map index...")
    region_defs = load_map_index(cache)
    print(f"  {len(region_defs)} regions in map index")

    # determine target regions
    if args.regions:
        target_coords = set()
        for coord in args.regions.split():
            parts = coord.split(",")
            target_coords.add((int(parts[0]), int(parts[1])))
        target_regions = {
            (rd.region_x, rd.region_y): rd
            for rd in region_defs
            if (rd.region_x, rd.region_y) in target_coords
        }
        print(f"  exporting {len(target_regions)} specified regions")
    else:
        # fight area center: world (3071, 3544) -> regionX=47, regionY=55
        center_rx, center_ry = 47, 55
        r = args.radius
        target_regions = {
            (rd.region_x, rd.region_y): rd
            for rd in region_defs
            if center_rx - r <= rd.region_x <= center_rx + r
            and center_ry - r <= rd.region_y <= center_ry + r
        }
        print(f"  exporting {len(target_regions)} regions around ({center_rx}, {center_ry})")

    # parse terrain for each region
    parsed: dict[tuple[int, int], RegionTerrain] = {}
    errors = 0

    for (rx, ry), rd in sorted(target_regions.items()):
        terrain_data = cache.get(MAP_INDEX, rd.terrain_file)
        if terrain_data is None:
            errors += 1
            continue

        try:
            terrain_data = gzip.decompress(terrain_data)
        except Exception:
            errors += 1
            continue

        region_chunk_x = rx * 64
        region_chunk_y = ry * 64

        rt = parse_terrain_full(terrain_data, region_chunk_x, region_chunk_y)
        rt.region_x = rx
        rt.region_y = ry
        parsed[(rx, ry)] = rt

    print(f"  parsed {len(parsed)} regions, {errors} errors")
    _build_and_write(args, parsed, underlays, overlays_defs, tex_colors)


def _main_modern(args: argparse.Namespace) -> None:
    """Export terrain from modern OpenRS2 cache."""
    if not args.regions:
        sys.exit("--regions is required when using --modern-cache")
    export_modern_terrain(
        cache_dir=args.modern_cache,
        regions=parse_region_specs(args.regions),
        output=args.output,
        scene_plane=args.scene_plane,
        brightness=args.brightness,
    )


def parse_region_specs(regions: str) -> list[tuple[int, int]]:
    coords: list[tuple[int, int]] = []
    for coord in regions.split():
        parts = coord.split(",")
        if len(parts) != 2:
            raise ValueError(f"invalid region coordinate: {coord}")
        coords.append((int(parts[0]), int(parts[1])))
    return coords


def load_modern_region_terrain(
    reader: RcCacheStore,
    region_x: int,
    region_y: int,
) -> RegionTerrain | None:
    """Decode one mapsquare terrain payload from a modern cache."""
    terrain_data = read_map_region_file(reader, region_x, region_y, "terrain")
    if terrain_data is None:
        return None
    terrain = parse_terrain_full(terrain_data, region_x * 64, region_y * 64)
    terrain.region_x = region_x
    terrain.region_y = region_y
    return terrain


def export_modern_terrain(
    cache_dir: Path,
    regions: list[tuple[int, int]],
    output: Path,
    scene_plane: int = 0,
    brightness: float = 1.0,
) -> None:
    """Export one b237 terrain plane through the repo-local rc_cache path."""
    if not cache_dir.is_dir():
        sys.exit(f"modern cache directory not found: {cache_dir}")
    if not regions:
        sys.exit("at least one region is required")

    print(f"reading modern cache from {cache_dir}")
    reader = RcCacheStore(cache_dir)

    print("loading floor definitions from modern cache...")
    underlays, overlays_defs = decode_floor_definitions_modern(reader)
    print(f"  {len(underlays)} underlays, {len(overlays_defs)} overlays")

    print("loading texture average colors from modern cache...")
    tex_colors = load_texture_average_colors_modern(reader)
    print(f"  {len(tex_colors)} texture colors")

    print("resolving index 5 map regions...")
    map_regions = find_all_map_region_files(reader)
    print(f"  {len(map_regions)} regions found in index 5")

    # determine target regions
    target_coords = set(regions)

    print(f"  exporting {len(target_coords)} specified regions")

    parsed: dict[tuple[int, int], RegionTerrain] = {}
    errors = 0

    for rx, ry in sorted(target_coords):
        ms = (rx << 8) | ry
        if ms not in map_regions:
            print(f"  region ({rx},{ry}): not found in index 5")
            errors += 1
            continue

        entry = map_regions[ms]
        if not entry.has_terrain:
            print(f"  region ({rx},{ry}): no terrain group")
            errors += 1
            continue

        rt = load_modern_region_terrain(reader, rx, ry)
        if rt is None:
            print(f"  region ({rx},{ry}): failed to read terrain")
            errors += 1
            continue
        parsed[(rx, ry)] = rt

    print(f"  parsed {len(parsed)} regions, {errors} errors")
    write_args = argparse.Namespace(
        output=output,
        brightness=brightness,
        scene_plane=scene_plane,
    )
    _build_and_write(write_args, parsed, underlays, overlays_defs, tex_colors)


def export_modern_terrain_split(
    cache_dir: Path,
    regions: list[tuple[int, int]],
    output_dir: Path,
    planes: list[int] | tuple[int, ...] = (0, 1, 2, 3),
    brightness: float = 1.0,
) -> list[Path]:
    """Export bounded per-mapsquare terrain files with stitched cache edges."""
    if not cache_dir.is_dir():
        raise FileNotFoundError(f"modern cache directory not found: {cache_dir}")
    if not regions:
        raise ValueError("at least one region is required")
    if not planes or any(plane < 0 or plane > 3 for plane in planes):
        raise ValueError("planes must contain values in 0..3")

    reader = RcCacheStore(cache_dir)
    underlays, overlays_defs = decode_floor_definitions_modern(reader)
    tex_colors = load_texture_average_colors_modern(reader)
    map_regions = find_all_map_region_files(reader)
    target_coords = sorted(set(regions), key=lambda region: (region[1], region[0]))
    missing = [
        (rx, ry)
        for rx, ry in target_coords
        if (entry := map_regions.get((rx << 8) | ry)) is None or not entry.has_terrain
    ]
    if missing:
        sample = ", ".join(f"{rx},{ry}" for rx, ry in missing[:8])
        raise ValueError(f"{len(missing)} selected mapsquares lack terrain: {sample}")

    output_dir.mkdir(parents=True, exist_ok=True)

    # Keep full-cache builds bounded while retaining one mapsquare of context.
    @lru_cache(maxsize=16)
    def load_region(region_x: int, region_y: int) -> RegionTerrain | None:
        entry = map_regions.get((region_x << 8) | region_y)
        if entry is None or not entry.has_terrain:
            return None
        return load_modern_region_terrain(reader, region_x, region_y)

    generated: list[Path] = []
    for rx, ry in target_coords:
        halo: dict[tuple[int, int], RegionTerrain] = {}
        for dy in range(-1, 2):
            for dx in range(-1, 2):
                terrain = load_region(rx + dx, ry + dy)
                if terrain is not None:
                    halo[(rx + dx, ry + dy)] = terrain
        local = halo.get((rx, ry))
        if local is None:
            raise ValueError(f"failed to decode terrain for mapsquare {rx},{ry}")
        stitch_region_edges(halo)
        local_region = {(rx, ry): local}

        plane_vertices: list[str] = []
        for plane in planes:
            verts, colors = build_terrain_mesh(
                local_region,
                underlays,
                overlays_defs,
                target_plane=plane,
                tex_colors=tex_colors,
                brightness=brightness,
                context_regions=halo,
            )
            heightmap = build_heightmap(
                local_region,
                target_plane=plane,
                resolve_link_below=plane < 3,
                context_regions=halo,
            )
            output = output_dir / f"{rx}_{ry}.p{plane}.terrain"
            write_terrain_binary(output, verts, colors, local_region, heightmap)
            generated.append(output)
            plane_vertices.append(f"p{plane}={len(verts) // 3}")
        print(f"terrain mapsquare {rx},{ry}: " + " ".join(plane_vertices))

    return generated


def _build_and_write(
    args: argparse.Namespace,
    parsed: dict[tuple[int, int], RegionTerrain],
    underlays: dict[int, FloorDef],
    overlays_defs: dict[int, FloorDef],
    tex_colors: dict[int, int],
) -> None:
    """Build terrain mesh and heightmap, then write to binary output."""
    if not parsed:
        sys.exit("no regions parsed successfully")

    print("stitching region edges...")
    stitch_region_edges(parsed)

    print("building terrain mesh...")
    brightness = getattr(args, 'brightness', 1.0)
    target_plane = max(0, min(3, getattr(args, "scene_plane", 0)))
    verts, colors = build_terrain_mesh(
        parsed,
        underlays,
        overlays_defs,
        target_plane=target_plane,
        tex_colors=tex_colors,
        brightness=brightness,
    )
    vert_count = len(verts) // 3
    tri_count = vert_count // 3
    print(f"  {vert_count} vertices, {tri_count} triangles")

    print("building heightmap...")
    heightmap = build_heightmap(
        parsed,
        target_plane=target_plane,
        resolve_link_below=(target_plane < 3),
    )
    hm_min_x, hm_min_y, hm_w, hm_h, _ = heightmap
    print(f"  {hm_w}x{hm_h} tiles, origin ({hm_min_x}, {hm_min_y})")

    write_terrain_binary(args.output, verts, colors, parsed, heightmap)
    file_size = args.output.stat().st_size
    print(f"\nwrote {file_size:,} bytes to {args.output}")


def main() -> None:
    parser = argparse.ArgumentParser(description="export OSRS terrain mesh from cache")
    parser.add_argument(
        "--cache",
        type=Path,
        default=None,
        help="explicit legacy 317-format cache directory",
    )
    parser.add_argument(
        "--modern-cache",
        type=Path,
        default=B237_CACHE,
        help="path to the b237 OpenRS2 cache directory",
    )
    parser.add_argument(
        "--regions",
        type=str,
        default=None,
        help='space-separated region coordinates as rx,ry pairs (e.g. "35,47 35,48")',
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("data/wilderness.terrain"),
        help="output .terrain binary file",
    )
    parser.add_argument(
        "--radius",
        type=int,
        default=2,
        help="regions around wilderness center to export (317 mode only, default: 2)",
    )
    parser.add_argument(
        "--brightness",
        type=float,
        default=1.0,
        help="brightness multiplier for terrain lighting (e.g. 1.8 for caves, default: 1.0)",
    )
    parser.add_argument(
        "--scene-plane",
        type=int,
        default=0,
        help="cache plane to export as the visible terrain scene plane (0-3)",
    )
    args = parser.parse_args()

    if args.cache:
        _main_317(args)
    else:
        args.modern_cache = require_path(
            args.modern_cache,
            "--modern-cache",
            "RUNEC_B237_CACHE",
        )
        _main_modern(args)


if __name__ == "__main__":
    main()
