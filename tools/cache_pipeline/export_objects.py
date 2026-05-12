"""Export placed map objects from the repo-local b237 OpenRS2 cache.

Reads object placements from each region's object file, resolves their 3D models
from object definitions, decodes model geometry, applies rotation and
positioning, and outputs a binary .objects file for the raylib viewer.

Object types exported:
  - Walls (types 0-3, 9): straight walls, corners, diagonal walls
  - Wall decorations (types 4-8): windows, torches, banners on walls
  - Props (types 10-11): buildings, trees, statues, interactive objects
  - Roofing (types 12-21): roof pieces
  - Ground decorations (type 22): flowers, grass patches, paving, dirt marks

Usage:
    python3 tools/cache_pipeline/export_objects.py \
        --regions "35,47 35,48" \
        --output data/zulrah.objects
"""

import argparse
import gzip
import io
import math
import struct
import sys
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from export_collision_map import (
    CONFIG_INDEX,
    MAP_INDEX,
    CacheReader,
    _read_string,
    decode_archive,
    hash_archive_name,
    load_map_index,
    read_smart,
)
from export_terrain import RegionTerrain, build_heightmap, parse_terrain_full
from export_models import (
    MODEL_INDEX,
    expand_model,
    load_texture_average_colors as load_legacy_texture_average_colors,
    write_models_binary,
)
from export_textures import (
    TextureAtlas,
    build_atlas,
    load_all_texture_sprites,
    write_atlas_binary,
    write_texture_anim_binary,
)
from export_terrain import stitch_region_edges
from modern_cache_reader import ModernCacheReader
from rc_cache import (
    ModelData,
    RcCacheStore,
    decode_model,
    find_map_region_files,
    hsl15_to_rgb,
    iter_location_placements,
    load_texture_average_colors as load_modern_texture_average_colors,
    load_texture_definitions,
    load_texture_sprites as load_modern_texture_sprites,
    load_model,
    merge_models,
    read_extended_smart,
    read_map_region_file,
)

DEFAULT_MODERN_CACHE = Path(__file__).resolve().parent / "source/current_fightcaves_demo/data/cache"

# --- object definition with model IDs ---

CONFIG_ARCHIVE = 2

# object types we want to render
WALL_TYPES = {0, 1, 2, 3, 9}
WALL_DECO_TYPES = {4, 5, 6, 7, 8}
PROP_TYPES = {10, 11}
ROOF_TYPES = {12, 13, 14, 15, 16, 17, 18, 19, 20, 21}
GROUND_DECO_TYPES = {22}
EXPORTED_TYPES = WALL_TYPES | WALL_DECO_TYPES | PROP_TYPES | ROOF_TYPES | GROUND_DECO_TYPES


@dataclass
class LocDef:
    """Object definition with model IDs and transform properties."""

    obj_id: int = 0
    name: str = ""
    width: int = 1
    length: int = 1
    solid: bool = True
    # model IDs and their associated type contexts
    model_ids: list[int] = field(default_factory=list)
    model_types: list[int] = field(default_factory=list)
    has_typed_models: bool = False  # True if opcode 1 was used (typed models)
    # transform
    model_size_x: int = 128  # scale factor (128 = 1.0x)
    model_size_h: int = 128
    model_size_y: int = 128
    offset_x: int = 0
    offset_h: int = 0
    offset_y: int = 0
    animation_id: int = -1
    # rendering flags
    rotated: bool = False  # mirror model horizontally
    contoured_ground: bool = False
    decor_offset: int = 16  # wall decoration displacement (type 5), default 16 OSRS units
    ambient: int = 0
    contrast: int = 0
    # color remapping
    recolor_from: list[int] = field(default_factory=list)
    recolor_to: list[int] = field(default_factory=list)
    retexture_from: list[int] = field(default_factory=list)
    retexture_to: list[int] = field(default_factory=list)


def decode_loc_definitions(cache: CacheReader) -> dict[int, LocDef]:
    """Decode object definitions from loc.dat/loc.idx, capturing model IDs.

    Uses the idx file to build an offset table for each definition (matching
    the Java client's streamIndices approach). Each definition is read from
    its own slice of loc.dat, preventing any opcode parsing bug from
    corrupting subsequent definitions.
    """
    raw = cache.get(CONFIG_INDEX, CONFIG_ARCHIVE)
    if raw is None:
        sys.exit("could not read config archive")

    archive = decode_archive(raw)
    loc_hash = hash_archive_name("loc.dat") & 0xFFFFFFFF
    idx_hash = hash_archive_name("loc.idx") & 0xFFFFFFFF

    loc_data = archive.get(loc_hash) or archive.get(hash_archive_name("loc.dat"))
    loc_idx = archive.get(idx_hash) or archive.get(hash_archive_name("loc.idx"))

    if loc_data is None or loc_idx is None:
        sys.exit("loc.dat/loc.idx not found in config archive")

    idx_buf = io.BytesIO(loc_idx)
    total = struct.unpack(">H", idx_buf.read(2))[0]

    # build offset table from idx (each entry is uint16 size)
    offsets: list[int] = []
    pos = 0
    for _ in range(total):
        size_bytes = idx_buf.read(2)
        if len(size_bytes) < 2:
            break
        size = struct.unpack(">H", size_bytes)[0]
        if size == 0xFFFF:
            break
        offsets.append(pos)
        pos += size

    defs: dict[int, LocDef] = {}

    for obj_id in range(len(offsets)):
        # read this definition's slice from loc.dat
        start = offsets[obj_id]
        end = offsets[obj_id + 1] if obj_id + 1 < len(offsets) else len(loc_data)
        buf = io.BytesIO(loc_data[start:end])

        d = LocDef(obj_id=obj_id)

        while True:
            opcode = buf.read(1)
            if not opcode:
                break
            opcode = opcode[0]

            if opcode == 0:
                break
            elif opcode == 1:
                # model IDs with type contexts (first opcode wins, skip if already set)
                count = buf.read(1)[0]
                if not d.model_ids:
                    d.has_typed_models = True
                    for _ in range(count):
                        mid = struct.unpack(">H", buf.read(2))[0]
                        mtype = buf.read(1)[0]
                        d.model_ids.append(mid)
                        d.model_types.append(mtype)
                else:
                    buf.read(count * 3)  # skip
            elif opcode == 2:
                d.name = _read_string(buf)
            elif opcode == 3:
                pass  # description: Java reads 0 bytes for this opcode
            elif opcode == 5:
                # model IDs without types (first opcode wins, skip if already set)
                count = buf.read(1)[0]
                if not d.model_ids:
                    d.has_typed_models = False
                    for _ in range(count):
                        mid = struct.unpack(">H", buf.read(2))[0]
                        d.model_ids.append(mid)
                        d.model_types.append(10)
                else:
                    buf.read(count * 2)  # skip
            elif opcode == 14:
                d.width = buf.read(1)[0]
            elif opcode == 15:
                d.length = buf.read(1)[0]
            elif opcode == 17:
                d.solid = False
            elif opcode == 18:
                pass  # impenetrable
            elif opcode == 19:
                buf.read(1)  # hasActions
            elif opcode == 21:
                d.contoured_ground = True
            elif opcode == 22:
                pass  # nonFlatShading
            elif opcode == 23:
                pass  # modelClipped
            elif opcode == 24:
                anim = struct.unpack(">H", buf.read(2))[0]
                d.animation_id = -1 if anim == 0xFFFF else anim
            elif opcode == 28:
                d.decor_offset = buf.read(1)[0]
            elif opcode == 29:
                d.ambient = struct.unpack(">b", buf.read(1))[0]
            elif opcode == 39:
                d.contrast = struct.unpack(">b", buf.read(1))[0] * 25
            elif 30 <= opcode <= 34:
                _read_string(buf)  # actions
            elif opcode == 40:
                # recolors: first short = modifiedModelColors (color to FIND in model)
                # second short = originalModelColors (color to REPLACE WITH)
                # naming is backwards in the OSRS client — "modified" is the source,
                # "original" is the replacement. confirmed via Model.recolor(found, replace)
                # call: model.recolor(modifiedModelColors[i], originalModelColors[i])
                count = buf.read(1)[0]
                for _ in range(count):
                    d.recolor_from.append(struct.unpack(">H", buf.read(2))[0])
                    d.recolor_to.append(struct.unpack(">H", buf.read(2))[0])
            elif opcode == 41:
                count = buf.read(1)[0]
                for _ in range(count):
                    d.retexture_from.append(struct.unpack(">H", buf.read(2))[0])
                    d.retexture_to.append(struct.unpack(">H", buf.read(2))[0])
            elif opcode == 60:
                buf.read(2)  # mapAreaId
            elif opcode == 61:
                buf.read(2)  # category
            elif opcode == 62:
                d.rotated = True
            elif opcode == 64:
                pass  # shadow=false
            elif opcode == 65:
                d.model_size_x = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 66:
                d.model_size_h = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 67:
                d.model_size_y = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 68:
                buf.read(2)  # mapscene
            elif opcode == 69:
                buf.read(1)  # surroundings
            elif opcode == 70:
                d.offset_x = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 71:
                d.offset_h = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 72:
                d.offset_y = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 73:
                pass  # obstructsGround
            elif opcode == 74:
                d.solid = False
            elif opcode == 75:
                buf.read(1)  # supportItems
            elif opcode == 77:
                buf.read(2)  # varbit
                buf.read(2)  # varp
                count = buf.read(1)[0]
                for _ in range(count + 1):
                    buf.read(2)
            elif opcode == 78:
                buf.read(2)  # ambient sound
                buf.read(1)
            elif opcode == 79:
                buf.read(2)
                buf.read(2)
                buf.read(1)
                count = buf.read(1)[0]
                for _ in range(count):
                    buf.read(2)
            elif opcode == 81:
                buf.read(1)  # contoured ground percent
            elif opcode == 82:
                buf.read(2)  # map icon
            elif opcode == 89:
                pass  # randomize animation
            elif opcode == 92:
                buf.read(2)  # varbit
                buf.read(2)  # varp
                buf.read(2)  # default
                count = buf.read(1)[0]
                for _ in range(count + 1):
                    buf.read(2)
            elif opcode == 249:
                count = buf.read(1)[0]
                for _ in range(count):
                    is_string = buf.read(1)[0] == 1
                    buf.read(3)  # 3-byte medium
                    if is_string:
                        _read_string(buf)
                    else:
                        buf.read(4)
            # unknown opcodes: skip (read 0 bytes), matching Java behavior

        if d.model_ids:
            defs[obj_id] = d

    return defs


def _read_modern_obj_string(buf: io.BytesIO) -> str:
    """Read null-terminated string from modern object definition."""
    chars = []
    while True:
        b = buf.read(1)
        if not b or b[0] == 0:
            break
        chars.append(chr(b[0]))
    return "".join(chars)


def decode_loc_definitions_modern(reader: ModernCacheReader) -> dict[int, LocDef]:
    """Decode object definitions from modern cache, capturing model IDs and transforms.

    Group 6 in config index (2) contains object definitions. Each definition is
    stored as a separate file within the group, keyed by object ID.
    """
    files = reader.read_group(2, 6)
    if files is None:
        sys.exit("could not read object definitions from modern cache")

    defs: dict[int, LocDef] = {}

    for obj_id, data in files.items():
        d = LocDef(obj_id=obj_id)
        buf = io.BytesIO(data)

        while True:
            raw = buf.read(1)
            if not raw:
                break
            opcode = raw[0]

            if opcode == 0:
                break
            elif opcode == 1:
                count = buf.read(1)[0]
                d.has_typed_models = True
                for _ in range(count):
                    mid = struct.unpack(">H", buf.read(2))[0]
                    mtype = buf.read(1)[0]
                    d.model_ids.append(mid)
                    d.model_types.append(mtype)
            elif opcode == 2:
                d.name = _read_modern_obj_string(buf)
            elif opcode == 5:
                count = buf.read(1)[0]
                d.has_typed_models = False
                for _ in range(count):
                    mid = struct.unpack(">H", buf.read(2))[0]
                    d.model_ids.append(mid)
                    d.model_types.append(10)
            elif opcode == 6:
                count = buf.read(1)[0]
                d.has_typed_models = True
                for _ in range(count):
                    mid = struct.unpack(">I", buf.read(4))[0]
                    mtype = buf.read(1)[0]
                    d.model_ids.append(mid)
                    d.model_types.append(mtype)
            elif opcode == 7:
                count = buf.read(1)[0]
                d.has_typed_models = False
                for _ in range(count):
                    mid = struct.unpack(">I", buf.read(4))[0]
                    d.model_ids.append(mid)
                    d.model_types.append(10)
            elif opcode == 14:
                d.width = buf.read(1)[0]
            elif opcode == 15:
                d.length = buf.read(1)[0]
            elif opcode == 17:
                d.solid = False
            elif opcode == 18:
                pass  # impenetrable
            elif opcode == 19:
                buf.read(1)  # interactType
            elif opcode == 21:
                d.contoured_ground = True
            elif opcode == 22:
                pass  # nonFlatShading
            elif opcode == 23:
                pass  # modelClipped
            elif opcode == 24:
                anim = struct.unpack(">H", buf.read(2))[0]
                d.animation_id = -1 if anim == 0xFFFF else anim
            elif opcode == 27:
                pass  # clipType = 1
            elif opcode == 28:
                d.decor_offset = buf.read(1)[0]
            elif opcode == 29:
                d.ambient = struct.unpack(">b", buf.read(1))[0]
            elif opcode in range(30, 35):
                _read_modern_obj_string(buf)  # actions
            elif opcode == 39:
                d.contrast = struct.unpack(">b", buf.read(1))[0] * 25
            elif opcode == 40:
                count = buf.read(1)[0]
                for _ in range(count):
                    d.recolor_from.append(struct.unpack(">H", buf.read(2))[0])
                    d.recolor_to.append(struct.unpack(">H", buf.read(2))[0])
            elif opcode == 41:
                count = buf.read(1)[0]
                for _ in range(count):
                    d.retexture_from.append(struct.unpack(">H", buf.read(2))[0])
                    d.retexture_to.append(struct.unpack(">H", buf.read(2))[0])
            elif opcode == 60:
                buf.read(2)  # mapAreaId
            elif opcode == 61:
                buf.read(2)  # category
            elif opcode == 62:
                d.rotated = True
            elif opcode == 64:
                pass  # shadow=false
            elif opcode == 65:
                d.model_size_x = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 66:
                d.model_size_h = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 67:
                d.model_size_y = struct.unpack(">H", buf.read(2))[0]
            elif opcode == 68:
                buf.read(2)  # mapscene
            elif opcode == 69:
                buf.read(1)  # surroundings
            elif opcode == 70:
                d.offset_x = struct.unpack(">h", buf.read(2))[0]
            elif opcode == 71:
                d.offset_h = struct.unpack(">h", buf.read(2))[0]
            elif opcode == 72:
                d.offset_y = struct.unpack(">h", buf.read(2))[0]
            elif opcode == 73:
                pass  # obstructsGround
            elif opcode == 74:
                d.solid = False
            elif opcode == 75:
                buf.read(1)  # supportItems
            elif opcode == 77:
                buf.read(2)  # varbit
                buf.read(2)  # varp
                count = buf.read(1)[0]
                for _ in range(count + 1):
                    buf.read(2)
            elif opcode == 78:
                buf.read(2)  # ambient sound
                buf.read(1)  # distance
                buf.read(1)  # retain
            elif opcode == 79:
                buf.read(2)
                buf.read(2)
                buf.read(1)  # distance
                buf.read(1)  # retain
                count = buf.read(1)[0]
                for _ in range(count):
                    buf.read(2)
            elif opcode == 81:
                buf.read(1)  # contoured ground percent
            elif opcode == 82:
                buf.read(2)  # map icon
            elif opcode == 89:
                pass  # randomize animation
            elif opcode == 90:
                pass  # fixLocAnimAfterLocChange
            elif opcode == 91:
                buf.read(1)  # bgsoundDropoffEasing
            elif opcode == 92:
                buf.read(2)  # varbit
                buf.read(2)  # varp
                buf.read(2)  # default
                count = buf.read(1)[0]
                for _ in range(count + 1):
                    buf.read(2)
            elif opcode == 93:
                buf.read(1)
                buf.read(2)
                buf.read(1)
                buf.read(2)
            elif opcode == 94:
                pass  # unknown94
            elif opcode == 95:
                buf.read(1)  # crossWorldSound
            elif opcode == 96:
                buf.read(1)  # thickness/raise
            elif opcode == 249:
                count = buf.read(1)[0]
                for _ in range(count):
                    is_string = buf.read(1)[0] == 1
                    buf.read(3)  # 3-byte key
                    if is_string:
                        _read_modern_obj_string(buf)
                    else:
                        buf.read(4)
            else:
                # unknown opcode — stop parsing to avoid desync
                break

        if d.model_ids:
            defs[obj_id] = d

    print(f"  {len(defs)} definitions with models (from {len(files)} total)")
    return defs



@dataclass
class PlacedObject:
    """A single placed object in the world."""

    obj_id: int = 0
    world_x: int = 0
    world_y: int = 0
    height: int = 0
    obj_type: int = 0  # 0-22
    rotation: int = 0  # 0-3 (W/N/E/S = 0/90/180/270 degrees)


def _terrain_setting_at(
    terrain: dict[tuple[int, int], RegionTerrain],
    world_x: int,
    world_y: int,
    plane: int,
) -> int:
    if plane < 0 or plane > 3:
        return 0
    rx, ry = world_x // 64, world_y // 64
    rt = terrain.get((rx, ry))
    if rt is None or not rt.settings:
        return 0
    return rt.settings[plane][world_x % 64][world_y % 64]


def _rsmod_visual_level(
    terrain: dict[tuple[int, int], RegionTerrain],
    world_x: int,
    world_y: int,
    plane: int,
) -> int:
    """Return the client draw-level for a placed loc.

    This mirrors the client render rule, not the server collision bridge rule.
    Plane-1 LINK_BELOW lowers upper bridge/interior objects visually, but plane
    0 objects still draw on level 0. Using collision lowering here drops the
    lower wall/ramp layer in linked-below basements.
    """
    current_flags = _terrain_setting_at(terrain, world_x, world_y, plane)
    if current_flags & 0x8:
        return 0
    if plane > 0 and (_terrain_setting_at(terrain, world_x, world_y, 1) & 0x2):
        return plane - 1
    return plane


def _modern_placement_from_location(
    placement,
    terrain: dict[tuple[int, int], RegionTerrain],
    use_rsmod_visual_levels: bool,
    target_scene_plane: int,
) -> PlacedObject | None:
    if placement.shape not in EXPORTED_TYPES:
        return None

    if use_rsmod_visual_levels:
        visual_level = _rsmod_visual_level(
            terrain,
            placement.world_x,
            placement.world_y,
            placement.plane,
        )
        if visual_level != target_scene_plane:
            return None
    elif placement.plane != target_scene_plane:
        return None

    return PlacedObject(
        obj_id=placement.object_id,
        world_x=placement.world_x,
        world_y=placement.world_y,
        height=placement.plane,
        obj_type=placement.shape,
        rotation=placement.rotation,
    )


def parse_object_placements(
    data: bytes,
    base_x: int,
    base_y: int,
) -> list[PlacedObject]:
    """Parse object placement binary for one region."""
    buf = io.BytesIO(data)
    obj_id = -1
    placements: list[PlacedObject] = []

    while True:
        obj_id_offset = read_smart(buf)
        if obj_id_offset == 0:
            break
        obj_id += obj_id_offset
        obj_pos_info = 0

        while True:
            pos_offset = read_smart(buf)
            if pos_offset == 0:
                break
            obj_pos_info += pos_offset - 1

            raw_byte = buf.read(1)
            if not raw_byte:
                return placements
            info = raw_byte[0]

            local_y = obj_pos_info & 0x3F
            local_x = (obj_pos_info >> 6) & 0x3F
            height = (obj_pos_info >> 12) & 0x3

            obj_type = info >> 2
            rotation = info & 0x3

            if obj_type not in EXPORTED_TYPES:
                continue

            # only export plane 0 for now — plane 1+ objects (upper floors, roofing)
            # need corresponding floor/ceiling geometry to look right.
            # the heightmaps dict already supports multi-plane; just widen this
            # filter when floor rendering is added.
            if height != 0:
                continue

            placements.append(
                PlacedObject(
                    obj_id=obj_id,
                    world_x=base_x + local_x,
                    world_y=base_y + local_y,
                    height=height,
                    obj_type=obj_type,
                    rotation=rotation,
                )
            )

    return placements


def parse_object_placements_modern(
    data: bytes,
    base_x: int,
    base_y: int,
) -> list[PlacedObject]:
    """Parse modern-format object placement data for one region.

    Uses extended smart for object ID deltas (IDs > 32767).
    """
    buf = io.BytesIO(data)
    obj_id = -1
    placements: list[PlacedObject] = []

    while True:
        obj_id_offset = read_extended_smart(buf)
        if obj_id_offset == 0:
            break
        obj_id += obj_id_offset
        obj_pos_info = 0

        while True:
            pos_offset = read_smart(buf)
            if pos_offset == 0:
                break
            obj_pos_info += pos_offset - 1

            raw_byte = buf.read(1)
            if not raw_byte:
                return placements
            info = raw_byte[0]

            local_y = obj_pos_info & 0x3F
            local_x = (obj_pos_info >> 6) & 0x3F
            height = (obj_pos_info >> 12) & 0x3

            obj_type = info >> 2
            rotation = info & 0x3

            if obj_type not in EXPORTED_TYPES:
                continue
            if height != 0:
                continue

            placements.append(
                PlacedObject(
                    obj_id=obj_id,
                    world_x=base_x + local_x,
                    world_y=base_y + local_y,
                    height=height,
                    obj_type=obj_type,
                    rotation=rotation,
                )
            )

    return placements


# --- model transform helpers ---


def rotate_model_90(model: ModelData) -> None:
    """Rotate model 90 degrees clockwise (OSRS rotation direction).

    In OSRS coordinate space: new_x = z, new_z = -x (clockwise when viewed from above).
    """
    for i in range(model.vertex_count):
        old_x = model.vertices_x[i]
        old_z = model.vertices_z[i]
        model.vertices_x[i] = old_z
        model.vertices_z[i] = -old_x


def mirror_model(model: ModelData) -> None:
    """Mirror model on Z axis and swap face winding. Matches OSRS Model.mirror()."""
    for i in range(model.vertex_count):
        model.vertices_z[i] = -model.vertices_z[i]
    for i in range(model.face_count):
        model.face_a[i], model.face_c[i] = model.face_c[i], model.face_a[i]


def apply_recolors(model: ModelData, loc: LocDef) -> None:
    """Apply color remapping from object definition to model face colors."""
    if not loc.recolor_from:
        return
    remap = dict(zip(loc.recolor_from, loc.recolor_to))
    for i in range(model.face_count):
        if i < len(model.face_colors) and model.face_colors[i] in remap:
            model.face_colors[i] = remap[model.face_colors[i]]


def apply_retextures(model: ModelData, loc: LocDef) -> None:
    """Apply texture remapping from object definition to textured faces."""
    if not loc.retexture_from:
        return
    remap = dict(zip(loc.retexture_from, loc.retexture_to))
    for i in range(model.face_count):
        if i < len(model.face_textures) and model.face_textures[i] in remap:
            model.face_textures[i] = remap[model.face_textures[i]]


def scale_model(model: ModelData, size_x: int, size_h: int, size_y: int) -> None:
    """Scale model vertices (128 = 1.0x).

    Matches the OSRS client call: model.scale(modelSizeX, modelSizeY, modelHeight)
    where Model.scale(x, z, y) — second param is Z, third is Y.
    So: size_x→X, size_y→Z(depth), size_h→Y(height).
    """
    if size_x == 128 and size_h == 128 and size_y == 128:
        return
    for i in range(model.vertex_count):
        model.vertices_x[i] = model.vertices_x[i] * size_x // 128
        model.vertices_y[i] = model.vertices_y[i] * size_h // 128
        model.vertices_z[i] = model.vertices_z[i] * size_y // 128


def placement_type_to_model_type(obj_type: int) -> int:
    """Map placement type to the model type requested from the definition.

    Matches the OSRS client's addLocation → getModelSharelight calls:
    - Types 4-8 (wall decorations) all request model type 4
    - Type 11 requests model type 10 (same model, different scene height)
    - All other types request their own type number
    """
    if 4 <= obj_type <= 8:
        return 4
    if obj_type == 11:
        return 10
    return obj_type


def get_models_for_type(loc: LocDef, obj_type: int) -> list[int]:
    """Get all model IDs for a given placement type from the object definition.

    Matches the OSRS client's ObjectDefinition.model() logic:
    - If modelTypes is null (opcode 5 only), models are for type 10 (props) only.
    - If modelTypes exists (opcode 1), requires exact type matches.

    Several b237 locs use multiple model IDs for the same type. Those IDs form
    one composite loc model and must be merged before placement transforms; using
    only the first ID drops half-stairs, multi-part ramps, shelves, stalls, etc.
    """
    if not loc.model_ids:
        return []

    model_type = placement_type_to_model_type(obj_type)

    if not loc.has_typed_models:
        # opcode 5: untyped models, only valid for model type 10 (props)
        if model_type != 10:
            return []
        return list(loc.model_ids)

    # opcode 1: typed models, require exact match
    return [
        mid for mid, mtype in zip(loc.model_ids, loc.model_types)
        if mtype == model_type
    ]


def get_model_for_type(loc: LocDef, obj_type: int) -> int | None:
    """Compatibility helper for callers that only need a representative model."""
    models = get_models_for_type(loc, obj_type)
    return models[0] if models else None


def model_key_for_type(loc: LocDef, obj_type: int) -> tuple[int, ...]:
    return tuple(get_models_for_type(loc, obj_type))


# --- binary output format ---

OBJS_MAGIC = 0x4F424A53  # "OBJS"


@dataclass
class ExpandedPlacement:
    """A placed object with its expanded vertex data ready for output."""

    world_x: int = 0
    world_y: int = 0
    obj_type: int = 0
    vertex_count: int = 0
    face_count: int = 0
    vertices: list[float] = field(default_factory=list)  # flat x,y,z
    colors: list[tuple[int, int, int, int]] = field(default_factory=list)
    uvs: list[float] = field(default_factory=list)  # flat u,v per vertex


OBJ2_MAGIC = 0x4F424A32  # "OBJ2" — v2 format with texture coordinates
OANM_MAGIC = 0x4D4E414F  # "OANM"
OANM_VERSION = 1
OBJECT_ANIM_MODEL_BASE = 0xA1000000
OANM_FLAG_DYNAMIC_BASE = 1 << 0
OANM_FLAG_DYNAMIC_REPLACEMENT = 1 << 1
OBHV_DOOR = 1 << 0
OBHV_PAIR_LEFT = 1 << 8
OBHV_PAIR_RIGHT = 1 << 9

ROOT = Path(__file__).resolve().parents[2]
OBJECT_BEHAVIORS = ROOT / "data/defs/object_behaviors.bin"


def write_objects_binary(
    output_path: Path,
    placements: list[ExpandedPlacement],
    min_world_x: int,
    min_world_y: int,
    has_textures: bool = False,
) -> None:
    """Write placed objects to binary .objects file.

    v2 format (OBJ2, when has_textures=True):
      magic: uint32 "OBJ2"
      placement_count: uint32
      min_world_x: int32
      min_world_y: int32
      total_vertex_count: uint32
      vertices: float32[total_vertex_count * 3]
      colors: uint8[total_vertex_count * 4]
      texcoords: float32[total_vertex_count * 2]

    v1 format (OBJS, when has_textures=False):
      same as above without texcoords
    """
    output_path.parent.mkdir(parents=True, exist_ok=True)

    total_verts = sum(p.vertex_count for p in placements)
    magic = OBJ2_MAGIC if has_textures else OBJS_MAGIC

    with open(output_path, "wb") as f:
        f.write(struct.pack("<I", magic))
        f.write(struct.pack("<I", len(placements)))
        f.write(struct.pack("<i", min_world_x))
        f.write(struct.pack("<i", min_world_y))
        f.write(struct.pack("<I", total_verts))

        # write all vertices concatenated
        for p in placements:
            for v in p.vertices:
                f.write(struct.pack("<f", v))

        # write all colors concatenated
        for p in placements:
            for r, g, b, a in p.colors:
                f.write(struct.pack("4B", r, g, b, a))

        # write texture coordinates (v2 only)
        if has_textures:
            for p in placements:
                for uv in p.uvs:
                    f.write(struct.pack("<f", uv))


@dataclass
class AnimatedObjectPlacement:
    """A cache location animation row plus its local render model variant."""

    model_id: int
    obj_id: int
    animation_id: int
    world_x: int
    world_y: int
    plane: int
    obj_type: int
    rotation: int
    flags: int
    pos_x: float
    pos_y: float
    pos_z: float
    phase_ticks: float


@dataclass(frozen=True)
class DynamicObjectBehavior:
    next_stage: int
    flags: int


def write_object_anim_binary(
    output_path: Path,
    rows: list[AnimatedObjectPlacement],
) -> None:
    """Write animated location placement metadata for the viewer."""
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("wb") as f:
        f.write(struct.pack("<III", OANM_MAGIC, OANM_VERSION, len(rows)))
        for row in rows:
            f.write(struct.pack("<IIiiiBBBBffff",
                                row.model_id,
                                row.obj_id,
                                row.animation_id,
                                row.world_x,
                                row.world_y,
                                row.plane & 0xFF,
                                row.obj_type & 0xFF,
                                row.rotation & 0xFF,
                                row.flags & 0xFF,
                                row.pos_x,
                                row.pos_y,
                                row.pos_z,
                                row.phase_ticks))


# --- main pipeline ---


def load_dynamic_behaviors(
    path: Path = OBJECT_BEHAVIORS,
) -> dict[int, DynamicObjectBehavior]:
    """Read object behavior v2 next-stage pairs for dynamic loc sidecars."""
    if not path.is_file():
        return {}
    data = path.read_bytes()
    if len(data) < 12:
        return {}
    magic, version, count = struct.unpack_from("<III", data, 0)
    if magic != 0x5648424F or version < 2:
        return {}
    row_fmt = "<IIiiiiBBH"
    row_size = struct.calcsize(row_fmt)
    pos = 12
    out: dict[int, DynamicObjectBehavior] = {}
    for _ in range(count):
        if pos + row_size > len(data):
            break
        obj_id, flags, next_stage, _open_sound, _close_sound, _climb_anim, \
            _mask, _skill, _pad = struct.unpack_from(row_fmt, data, pos)
        pos += row_size
        if next_stage >= 0 or (flags & OBHV_DOOR):
            out[int(obj_id)] = DynamicObjectBehavior(int(next_stage),
                                                     int(flags))
    return out


def sample_heightmap(
    hm: tuple[int, int, int, int, list[float]],
    world_x: int,
    world_y: int,
) -> float:
    """Sample terrain height at a world tile corner. Returns height in world units."""
    hm_min_x, hm_min_y, hm_w, hm_h, heights = hm
    lx = world_x - hm_min_x
    ly = world_y - hm_min_y
    if 0 <= lx < hm_w and 0 <= ly < hm_h:
        return heights[lx + ly * hm_w]
    return 0.0


def sample_height_bilinear(
    hm: tuple[int, int, int, int, list[float]],
    world_x: float,
    world_y: float,
) -> float:
    """Bilinear interpolation of terrain height at fractional world coords.

    Matches the OSRS client's Model.hillskew() which interpolates between
    4 tile corner heights using sub-tile fractions.
    """
    hm_min_x, hm_min_y, hm_w, hm_h, heights = hm
    fx = world_x - hm_min_x
    fy = world_y - hm_min_y

    tx = int(fx)
    ty = int(fy)
    frac_x = fx - tx
    frac_y = fy - ty

    # clamp to valid range (edge tiles use nearest neighbor)
    tx = max(0, min(tx, hm_w - 2))
    ty = max(0, min(ty, hm_h - 2))

    h00 = heights[tx + ty * hm_w]
    h10 = heights[(tx + 1) + ty * hm_w]
    h01 = heights[tx + (ty + 1) * hm_w]
    h11 = heights[(tx + 1) + (ty + 1) * hm_w]

    h_south = h00 * (1.0 - frac_x) + h10 * frac_x
    h_north = h01 * (1.0 - frac_x) + h11 * frac_x
    return h_south * (1.0 - frac_y) + h_north * frac_y


def _load_model_317(cache: CacheReader, model_id: int) -> bytes | None:
    """Load raw model bytes from 317 cache."""
    raw = cache.get(MODEL_INDEX, model_id)
    if raw is None:
        return None
    try:
        return gzip.decompress(raw)
    except Exception:
        return None


def _clone_model_data(src: ModelData) -> ModelData:
    """Copy decoded model arrays before placement-specific transforms mutate them."""
    return ModelData(
        model_id=src.model_id,
        vertex_count=src.vertex_count,
        face_count=src.face_count,
        vertices_x=list(src.vertices_x),
        vertices_y=list(src.vertices_y),
        vertices_z=list(src.vertices_z),
        face_a=list(src.face_a),
        face_b=list(src.face_b),
        face_c=list(src.face_c),
        face_colors=list(src.face_colors),
        face_textures=list(src.face_textures),
        vertex_skins=list(src.vertex_skins),
        face_priorities=list(src.face_priorities),
        face_alphas=list(src.face_alphas),
        face_render_types=list(src.face_render_types),
        face_tex_coords=list(src.face_tex_coords),
        tex_u=list(src.tex_u),
        tex_v=list(src.tex_v),
        tex_w=list(src.tex_w),
        tex_face_count=src.tex_face_count,
    )


def process_placements(
    placements: list[PlacedObject],
    loc_defs: dict[int, LocDef],
    model_loader: "Callable[[int], ModelData | None]",
    model_cache: dict[int, ModelData],
    heightmaps: dict[int, tuple[int, int, int, int, list[float]]] | None = None,
    tex_colors: dict[int, int] | None = None,
    atlas: TextureAtlas | None = None,
    dynamic_behaviors: dict[int, DynamicObjectBehavior] | None = None,
) -> tuple[list[ExpandedPlacement], list[AnimatedObjectPlacement], list[ModelData]]:
    """Process raw placements into expanded vertex data ready for rendering.

    model_loader: callable(model_id) -> decoded model or None. abstracts 317 vs modern cache.
    """
    results: list[ExpandedPlacement] = []
    animated_rows: list[AnimatedObjectPlacement] = []
    animated_models: list[ModelData] = []
    animated_model_ids: dict[tuple[int, tuple[int, ...], int, int], int] = {}
    skipped = 0
    model_miss = 0
    dynamic_behaviors = dynamic_behaviors or {}

    def prepare_model_set(loc: LocDef, model_ids: tuple[int, ...]) -> ModelData | None:
        nonlocal model_miss
        parts: list[ModelData] = []
        for model_id in model_ids:
            if model_id not in model_cache:
                raw = model_loader(model_id)
                if raw is None:
                    model_miss += 1
                    return None
                model_cache[model_id] = raw
            parts.append(_clone_model_data(model_cache[model_id]))
        if not parts:
            return None
        md = parts[0] if len(parts) == 1 else merge_models(parts)
        md.model_id = model_ids[0]
        apply_recolors(md, loc)
        apply_retextures(md, loc)
        scale_model(md, loc.model_size_x, loc.model_size_h, loc.model_size_y)
        return md

    def sidecar_model_for_type(loc: LocDef, obj_type: int) -> tuple[tuple[int, ...], ModelData | None]:
        model_ids = model_key_for_type(loc, obj_type)
        if not model_ids:
            return model_ids, None
        return model_ids, prepare_model_set(loc, model_ids)

    def placement_position(loc: LocDef, po: PlacedObject) -> tuple[float, float, float]:
        model_scale = 1.0 / 128.0
        w = loc.width
        l = loc.length
        if po.rotation == 1 or po.rotation == 3:
            w, l = l, w
        wx = float(po.world_x) + w / 2.0
        wz = -(float(po.world_y) + l / 2.0)
        if po.obj_type == 5:
            deco_dir_x = (1, 0, -1, 0)
            deco_dir_y = (0, -1, 0, 1)
            deco_off = loc.decor_offset * model_scale
            wx += deco_dir_x[po.rotation] * deco_off
            wz -= deco_dir_y[po.rotation] * deco_off
        heightmap = heightmaps.get(po.height) if heightmaps else None
        ground_y = sample_heightmap(heightmap, po.world_x, po.world_y) if heightmap else 0.0
        ox = float(loc.offset_x) * model_scale
        oh = float(loc.offset_h) * model_scale
        oy = float(loc.offset_y) * model_scale
        return wx + ox, ground_y + oh, wz - oy

    def door_open_tile(po: PlacedObject) -> tuple[int, int]:
        x, y = po.world_x, po.world_y
        r = po.rotation & 3
        if po.obj_type == 0:
            if r == 0:
                x -= 1
            elif r == 1:
                y += 1
            elif r == 2:
                x += 1
            else:
                y -= 1
        elif po.obj_type == 1:
            if r == 0:
                y += 1
            elif r == 1:
                x += 1
            elif r == 2:
                y -= 1
            else:
                x -= 1
        return x, y

    def dynamic_open_transform(loc_def: LocDef, po: PlacedObject,
                               behavior: DynamicObjectBehavior | None
                               ) -> tuple[int, int, int]:
        if po.obj_type > 3:
            return po.world_x, po.world_y, po.rotation
        if behavior and behavior.flags & (OBHV_PAIR_LEFT | OBHV_PAIR_RIGHT):
            x, y = po.world_x, po.world_y
            if (behavior.flags & OBHV_PAIR_RIGHT) and "gate" in loc_def.name.lower() \
                    and po.obj_type == 0:
                r = po.rotation & 3
                if r == 0:
                    x -= 2
                    y -= 1
                elif r == 1:
                    x -= 1
                    y += 2
                elif r == 2:
                    x += 2
                    y += 1
                else:
                    x += 1
                    y -= 2
            else:
                x, y = door_open_tile(po)
            if "gate" in loc_def.name.lower():
                return x, y, (po.rotation + 3) & 3
            if behavior.flags & OBHV_PAIR_LEFT:
                return x, y, (po.rotation + 3) & 3
            return x, y, (po.rotation + 1) & 3
        x, y = door_open_tile(po)
        return x, y, (po.rotation + 1) & 3

    def emit_sidecar(loc: LocDef, po: PlacedObject, eff_rot: int,
                     half_idx: int, flags: int,
                     animation_id: int) -> int:
        model_ids, md = sidecar_model_for_type(loc, po.obj_type)
        if md is None:
            return 0
        if loc.rotated ^ (eff_rot > 3):
            mirror_model(md)
        for _ in range(eff_rot % 4):
            rotate_model_90(md)
        key = (loc.obj_id, model_ids, po.obj_type, eff_rot)
        anim_model_id = animated_model_ids.get(key)
        if anim_model_id is None:
            anim_model_id = OBJECT_ANIM_MODEL_BASE + len(animated_models)
            md.model_id = anim_model_id
            animated_model_ids[key] = anim_model_id
            animated_models.append(md)
        pos_x, pos_y, pos_z = placement_position(loc, po)
        phase = float((po.world_x * 13 + po.world_y * 17
                       + loc.obj_id * 3 + half_idx * 19) % 97)
        animated_rows.append(
            AnimatedObjectPlacement(
                model_id=anim_model_id,
                obj_id=loc.obj_id,
                animation_id=animation_id,
                world_x=po.world_x,
                world_y=po.world_y,
                plane=po.height,
                obj_type=po.obj_type,
                rotation=po.rotation,
                flags=flags,
                pos_x=pos_x,
                pos_y=pos_y,
                pos_z=pos_z,
                phase_ticks=phase,
            )
        )
        return 1

    for po in placements:
        loc = loc_defs.get(po.obj_id)
        if loc is None:
            skipped += 1
            continue

        model_ids = model_key_for_type(loc, po.obj_type)
        if not model_ids:
            skipped += 1
            continue

        md = prepare_model_set(loc, model_ids)
        if md is None:
            continue

        # for type 2 (diagonal walls), OSRS renders two model halves:
        #   half 1: rotation = 4 + r (mirrored + r rotations)
        #   half 2: rotation = (r + 1) & 3 (next rotation, no mirror)
        # for all other types: single model with rotation r
        if po.obj_type == 2:
            rotations_to_emit = [4 + po.rotation, (po.rotation + 1) & 3]
        else:
            rotations_to_emit = [po.rotation]

        all_verts: list[float] = []
        all_colors: list[tuple[int, int, int, int]] = []
        all_uvs: list[float] = []

        # Shared placement position. Static placements bake this into vertices;
        # animated placements keep it as a draw transform so ANM2 can deform the
        # cache model every frame.
        model_scale = 1.0 / 128.0
        w = loc.width
        l = loc.length
        if po.rotation == 1 or po.rotation == 3:
            w, l = l, w
        wx = float(po.world_x) + w / 2.0
        wz = -(float(po.world_y) + l / 2.0)
        if po.obj_type == 5:
            _deco_dir_x = (1, 0, -1, 0)
            _deco_dir_y = (0, -1, 0, 1)
            deco_off = loc.decor_offset * model_scale
            wx += _deco_dir_x[po.rotation] * deco_off
            wz -= _deco_dir_y[po.rotation] * deco_off

        heightmap = heightmaps.get(po.height) if heightmaps else None
        ground_y = sample_heightmap(heightmap, po.world_x, po.world_y) if heightmap else 0.0
        ox = float(loc.offset_x) * model_scale
        oh = float(loc.offset_h) * model_scale
        oy = float(loc.offset_y) * model_scale

        dynamic_behavior = dynamic_behaviors.get(po.obj_id)
        next_stage = dynamic_behavior.next_stage if dynamic_behavior else -1
        is_dynamic = dynamic_behavior is not None \
            and (po.obj_type <= 3 or next_stage >= 0)
        is_animated = loc.animation_id >= 0
        for half_idx, eff_rot in enumerate(rotations_to_emit):
            # deep copy per rotation variant
            md_copy = _clone_model_data(md)

            # mirror if rotation > 3 (XOR with definition's rotated flag)
            if loc.rotated ^ (eff_rot > 3):
                mirror_model(md_copy)

            # apply N * 90-degree rotations
            for _ in range(eff_rot % 4):
                rotate_model_90(md_copy)

            if is_animated or is_dynamic:
                flags = OANM_FLAG_DYNAMIC_BASE if is_dynamic else 0
                emit_sidecar(loc, po, eff_rot, half_idx, flags,
                             loc.animation_id)
                continue

            v, c, uv = expand_model(
                md_copy,
                tex_colors,
                atlas,
                skip_fully_transparent=True,
                ambient=loc.ambient + 64,
                contrast=loc.contrast + 768,
            )
            all_verts.extend(v)
            all_colors.extend(c)
            all_uvs.extend(uv)

        if is_dynamic:
            active_loc = loc_defs.get(next_stage) if next_stage >= 0 else loc
            if active_loc is not None:
                active_x, active_y, active_rotation = dynamic_open_transform(
                    loc, po, dynamic_behavior)
                active_po = PlacedObject(
                    obj_id=next_stage,
                    world_x=active_x,
                    world_y=active_y,
                    height=po.height,
                    obj_type=po.obj_type,
                    rotation=active_rotation,
                )
                if active_po.obj_type == 2:
                    active_rots = [4 + active_po.rotation,
                                   (active_po.rotation + 1) & 3]
                else:
                    active_rots = [active_po.rotation]
                for half_idx, eff_rot in enumerate(active_rots):
                    emit_sidecar(active_loc, active_po, eff_rot, half_idx,
                                 OANM_FLAG_DYNAMIC_REPLACEMENT,
                                 active_loc.animation_id)
            continue

        if is_animated:
            continue

        verts = all_verts
        colors = all_colors
        face_uvs = all_uvs

        if not verts:
            continue

        # contoured ground: per-vertex terrain height interpolation
        # matches OSRS Model.hillskew() — bilinear interpolation at each vertex
        use_contouring = heightmap and (loc.contoured_ground or po.obj_type == 22)

        # transform vertices to world space
        # negate model Z to match our world coords (OSRS +Z → our -Z)
        # and swap triangle winding to preserve face normals after Z flip
        for i in range(0, len(verts), 9):
            # swap vertex 0 and vertex 2 within each triangle (reverses winding)
            for c in range(3):
                verts[i + c], verts[i + 6 + c] = verts[i + 6 + c], verts[i + c]
            colors[i // 3], colors[i // 3 + 2] = colors[i // 3 + 2], colors[i // 3]
            # swap UVs for vertex 0 and vertex 2 (same winding fix)
            uv_base = (i // 3) * 2
            face_uvs[uv_base], face_uvs[uv_base + 4] = face_uvs[uv_base + 4], face_uvs[uv_base]
            face_uvs[uv_base + 1], face_uvs[uv_base + 5] = face_uvs[uv_base + 5], face_uvs[uv_base + 1]

        for i in range(0, len(verts), 3):
            vx = verts[i] * model_scale + wx + ox
            vz = -verts[i + 2] * model_scale + wz - oy
            verts[i] = vx
            verts[i + 2] = vz

            if use_contouring:
                # bilinear terrain height at this vertex's world position
                # our world X maps directly to OSRS tile X
                # our world -Z maps to OSRS tile Y (north)
                vy_ground = sample_height_bilinear(heightmap, vx, -vz)
                # ground decorations (type 22) get a small Y offset to prevent
                # z-fighting with coplanar terrain — baked into vertex data since
                # glPolygonOffset isn't reliable through raylib's draw pipeline
                decal_offset = 0.01 if po.obj_type == 22 else 0.0
                verts[i + 1] = verts[i + 1] * model_scale + vy_ground + oh + decal_offset
            else:
                verts[i + 1] = verts[i + 1] * model_scale + ground_y + oh

        results.append(
            ExpandedPlacement(
                world_x=po.world_x,
                world_y=po.world_y,
                obj_type=po.obj_type,
                vertex_count=len(verts) // 3,
                face_count=len(verts) // 9,
                vertices=verts,
                colors=colors,
                uvs=face_uvs,
            )
        )

    if skipped:
        print(f"  skipped {skipped} placements (no definition/model)", file=sys.stderr)
    if model_miss:
        print(f"  {model_miss} model decode failures", file=sys.stderr)

    return results, animated_rows, animated_models


def _build_and_write(
    args: argparse.Namespace,
    all_placements: list[PlacedObject],
    loc_defs: dict[int, LocDef],
    model_loader: Callable[[int], ModelData | None],
    terrain_parsed: dict[tuple[int, int], RegionTerrain],
    tex_colors: dict[int, int],
    atlas: TextureAtlas | None = None,
) -> None:
    """Build geometry and write .objects binary (shared by 317 and modern paths)."""
    # stitch region edges for smooth heightmap
    if terrain_parsed:
        stitch_region_edges(terrain_parsed)

    # build heightmaps per plane
    heightmaps: dict[int, tuple[int, int, int, int, list[float]]] = {}
    if terrain_parsed:
        for plane in range(4):
            hm = build_heightmap(terrain_parsed, target_plane=plane)
            heightmaps[plane] = hm
        hm0 = heightmaps[0]
        print(f"  heightmap: {hm0[2]}x{hm0[3]} tiles, origin ({hm0[0]}, {hm0[1]})")

    # count by type
    type_counts: dict[int, int] = {}
    for po in all_placements:
        type_counts[po.obj_type] = type_counts.get(po.obj_type, 0) + 1
    for t in sorted(type_counts):
        print(f"    type {t:2d}: {type_counts[t]}")

    # process placements into expanded vertex data
    print("decoding models and building geometry...")
    model_geom_cache: dict[int, ModelData] = {}
    dynamic_behaviors = load_dynamic_behaviors()
    if dynamic_behaviors:
        print(f"  dynamic loc pairs: {len(dynamic_behaviors)}")
    expanded, animated_rows, animated_models = process_placements(
        all_placements, loc_defs, model_loader, model_geom_cache,
        heightmaps=heightmaps or None, tex_colors=tex_colors, atlas=atlas,
        dynamic_behaviors=dynamic_behaviors,
    )
    print(f"  {len(expanded)} objects with geometry, {len(model_geom_cache)} unique models decoded")
    if animated_rows:
        print(f"  {len(animated_rows)} animated object placements, "
              f"{len(animated_models)} animated model variants")

    total_verts = sum(p.vertex_count for p in expanded)
    total_tris = sum(p.face_count for p in expanded)
    print(f"  {total_verts:,} vertices, {total_tris:,} triangles")

    # compute bounds
    min_wx = min((p.world_x for p in expanded), default=0)
    min_wy = min((p.world_y for p in expanded), default=0)

    # write output
    has_textures = atlas is not None
    write_objects_binary(args.output, expanded, min_wx, min_wy, has_textures=has_textures)
    file_size = args.output.stat().st_size
    print(f"\nwrote {file_size:,} bytes to {args.output}")

    anim_path = args.output.with_suffix(".oanim")
    anim_models_path = args.output.with_suffix(".object_anim.models")
    if animated_rows and animated_models:
        write_object_anim_binary(anim_path, animated_rows)
        write_models_binary(anim_models_path, animated_models,
                            tex_colors=tex_colors, atlas=atlas,
                            atlas_path=anim_models_path.with_suffix(".atlas"))
        print(f"wrote {len(animated_rows)} animated object rows to {anim_path}")
        print(f"wrote {len(animated_models)} animated object model variants to "
              f"{anim_models_path}")
    else:
        for stale in (anim_path, anim_models_path, anim_models_path.with_suffix(".atlas")):
            if stale.exists():
                stale.unlink()


def _main_317(args: argparse.Namespace) -> None:
    """Export objects from 317-format cache."""
    if not args.cache.exists():
        sys.exit(f"cache directory not found: {args.cache}")

    print(f"reading cache from {args.cache}")
    cache_reader = CacheReader(args.cache)

    print("loading object definitions...")
    loc_defs = decode_loc_definitions(cache_reader)
    print(f"  {len(loc_defs)} definitions with models")

    print("loading map index...")
    region_defs = load_map_index(cache_reader)
    print(f"  {len(region_defs)} regions in map index")

    # fight area center: region (47, 55)
    center_rx, center_ry = 47, 55
    r = args.radius

    target_regions = {
        (rd.region_x, rd.region_y): rd
        for rd in region_defs
        if center_rx - r <= rd.region_x <= center_rx + r
        and center_ry - r <= rd.region_y <= center_ry + r
    }
    print(f"  exporting {len(target_regions)} regions around ({center_rx}, {center_ry})")

    # parse all object placements
    all_placements: list[PlacedObject] = []
    errors = 0

    for (rx, ry), rd in sorted(target_regions.items()):
        obj_data = cache_reader.get(MAP_INDEX, rd.object_file)
        if obj_data is None:
            errors += 1
            continue

        try:
            obj_data = gzip.decompress(obj_data)
        except Exception:
            errors += 1
            continue

        base_x = rx * 64
        base_y = ry * 64
        placements = parse_object_placements(obj_data, base_x, base_y)
        all_placements.extend(placements)

    print(f"  {len(all_placements)} placements parsed, {errors} region errors")

    # parse terrain for heightmap (same regions)
    print("parsing terrain for ground heights...")
    terrain_parsed: dict[tuple[int, int], RegionTerrain] = {}
    for (rx, ry), rd in sorted(target_regions.items()):
        terrain_data = cache_reader.get(MAP_INDEX, rd.terrain_file)
        if terrain_data is None:
            continue
        try:
            terrain_data = gzip.decompress(terrain_data)
        except Exception:
            continue
        rt = parse_terrain_full(terrain_data, rx * 64, ry * 64)
        rt.region_x = rx
        rt.region_y = ry
        terrain_parsed[(rx, ry)] = rt

    # load texture average colors for textured face rendering (fallback)
    tex_colors = load_legacy_texture_average_colors(cache_reader)
    print(f"  loaded {len(tex_colors)} texture average colors")

    # load texture sprites and build atlas
    print("loading texture sprites...")
    sprites = load_all_texture_sprites(cache_reader)
    print(f"  loaded {len(sprites)} texture sprites from cache")

    atlas = None
    atlas_path = args.output.with_suffix(".atlas")
    if sprites:
        atlas = build_atlas(sprites)
        print(f"  atlas: {atlas.width}x{atlas.height}, {len(atlas.uv_map)} textures mapped")
        write_atlas_binary(atlas_path, atlas)
        atlas_size = atlas_path.stat().st_size
        print(f"  wrote {atlas_size:,} bytes to {atlas_path}")

    loader_317 = lambda mid: (
        None if (raw := _load_model_317(cache_reader, mid)) is None
        else decode_model(mid, raw)
    )
    _build_and_write(args, all_placements, loc_defs, loader_317, terrain_parsed,
                     tex_colors, atlas)


def _main_modern(args: argparse.Namespace) -> None:
    """Export objects from modern OpenRS2 cache."""
    if not args.regions:
        sys.exit("--regions is required when using --modern-cache")
    export_modern_objects(
        cache_dir=args.modern_cache,
        regions=parse_region_specs(args.regions),
        output=args.output,
        scene_plane=args.scene_plane,
        rsmod_visual_levels=args.rsmod_visual_levels,
    )


def parse_region_specs(regions: str) -> list[tuple[int, int]]:
    coords: list[tuple[int, int]] = []
    for coord in regions.split():
        parts = coord.split(",")
        if len(parts) != 2:
            raise ValueError(f"invalid region coordinate: {coord}")
        coords.append((int(parts[0]), int(parts[1])))
    return coords


def export_modern_objects(
    cache_dir: Path,
    regions: list[tuple[int, int]],
    output: Path,
    scene_plane: int = 0,
    rsmod_visual_levels: bool = True,
) -> None:
    """Export one b237 object plane through the repo-local rc_cache path."""
    if not cache_dir.exists():
        sys.exit(f"modern cache directory not found: {cache_dir}")
    if not regions:
        sys.exit("at least one region is required")

    print(f"reading modern cache from {cache_dir}")
    reader = RcCacheStore(cache_dir)

    print("loading object definitions from modern cache...")
    loc_defs = decode_loc_definitions_modern(reader)

    # determine target regions
    target_coords = set(regions)
    print(f"  exporting {len(target_coords)} specified regions")
    target_scene_plane = max(0, min(3, scene_plane))

    # parse object placements and terrain
    all_placements: list[PlacedObject] = []
    terrain_parsed: dict[tuple[int, int], RegionTerrain] = {}
    errors = 0

    for rx, ry in sorted(target_coords):
        region = find_map_region_files(reader, rx, ry)
        if not region.has_terrain and not region.has_locations:
            print(f"  region ({rx},{ry}): not found in index 5")
            errors += 1
            continue

        # parse terrain
        if region.has_terrain:
            terrain_data = read_map_region_file(reader, rx, ry, "terrain")
            if terrain_data:
                rt = parse_terrain_full(terrain_data, rx * 64, ry * 64)
                rt.region_x = rx
                rt.region_y = ry
                terrain_parsed[(rx, ry)] = rt

        # parse object placements
        if region.has_locations:
            loc_data = read_map_region_file(reader, rx, ry, "locations")
            if loc_data:
                placements = []
                for placement in iter_location_placements(loc_data, rx, ry):
                    placed = _modern_placement_from_location(
                        placement,
                        terrain_parsed,
                        rsmod_visual_levels,
                        target_scene_plane,
                    )
                    if placed is not None:
                        placements.append(placed)
                all_placements.extend(placements)
                print(f"  region ({rx},{ry}): {len(placements)} placements")
            else:
                print(f"  region ({rx},{ry}): could not read/decrypt loc data")
                errors += 1

    print(f"  {len(all_placements)} placements parsed, {errors} region errors")

    print("loading modern texture definitions...")
    tex_colors = load_modern_texture_average_colors(reader)
    print(f"  loaded {len(tex_colors)} texture average colors")

    print("loading modern texture sprites...")
    sprites = load_modern_texture_sprites(reader)
    print(f"  loaded {len(sprites)} texture sprites from cache")

    atlas = None
    atlas_path = output.with_suffix(".atlas")
    if sprites:
        atlas = build_atlas(sprites)
        print(f"  atlas: {atlas.width}x{atlas.height}, {len(atlas.uv_map)} textures mapped")
        write_atlas_binary(atlas_path, atlas)
        write_texture_anim_binary(atlas_path.with_suffix(".tanim"), atlas,
                                  load_texture_definitions(reader))
        atlas_size = atlas_path.stat().st_size
        print(f"  wrote {atlas_size:,} bytes to {atlas_path}")

    loader_modern = lambda mid: load_model(reader, mid)
    write_args = argparse.Namespace(output=output)
    _build_and_write(write_args, all_placements, loc_defs, loader_modern,
                     terrain_parsed, tex_colors, atlas)


def main() -> None:
    parser = argparse.ArgumentParser(description="export OSRS placed objects from cache")
    parser.add_argument(
        "--cache",
        type=Path,
        default=None,
        help="explicit legacy 317-format cache directory",
    )
    parser.add_argument(
        "--modern-cache",
        type=Path,
        default=DEFAULT_MODERN_CACHE,
        help="path to the repo-local b237 OpenRS2 cache directory",
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
        default=Path("data/wilderness.objects"),
        help="output .objects binary file",
    )
    parser.add_argument(
        "--radius",
        type=int,
        default=3,
        help="number of regions around the fight area center to export (default: 3)",
    )
    parser.add_argument(
        "--rsmod-visual-levels",
        action="store_true",
        help="apply RSMod LINK_BELOW visual-level filtering for modern object placement",
    )
    parser.add_argument(
        "--scene-plane",
        type=int,
        default=0,
        help="visible scene plane to export for modern object placement (0-3)",
    )
    args = parser.parse_args()

    if args.cache:
        _main_317(args)
    else:
        _main_modern(args)


if __name__ == "__main__":
    main()
