"""Shared model loading and decoding helpers for b237 visual exporters."""

from __future__ import annotations

import sys
from pathlib import Path

from .config import INDEX_MODELS
from .store import RcCacheStore

_PIPELINE = Path(__file__).resolve().parents[1]
if str(_PIPELINE) not in sys.path:
    sys.path.insert(0, str(_PIPELINE))

from export_models import (  # noqa: E402
    ModelData,
    _decode_face_alphas,
    _decode_face_colors,
    _decode_face_priorities,
    _decode_face_render_types,
    _decode_face_texture_coords_from_stream,
    _decode_face_textures_from_stream,
    _decode_faces,
    _decode_texture_render_types,
    _decode_texture_triangle_indices,
    _decode_vertex_skins,
    _decode_vertices,
    _merge_models as merge_models,
    _read_ubyte,
    _read_ushort,
    decode_model as _decode_model,
    expand_model,
    hsl15_to_rgb,
    write_models_binary,
)


def load_model_bytes(store: RcCacheStore, model_id: int) -> bytes | None:
    return store.read_container(INDEX_MODELS, model_id)


def load_model(store: RcCacheStore, model_id: int) -> ModelData | None:
    data = load_model_bytes(store, model_id)
    if data is None:
        return None
    return decode_model(model_id, data)


def decode_model(model_id: int, data: bytes) -> ModelData | None:
    """Decode model data with the b237 type-1 face-render bitfield fix."""
    if len(data) >= 23 and data[-2] == 0xFF and data[-1] == 0xFF:
        return decode_type1_b237(model_id, data)
    return _decode_model(model_id, data)


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

    texture_render_types = _decode_texture_render_types(data, var11)
    tex_type0 = 0
    tex_type13 = 0
    tex_type2 = 0
    for t in texture_render_types:
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

    var40 = var26
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
    face_tex_coords = (
        _decode_face_texture_coords_from_stream(data, var35, face_textures, var11)
        if var16 == 1 and var11 > 0 else []
    )
    tex_u, tex_v, tex_w = _decode_texture_triangle_indices(
        data, var40, var11, texture_render_types,
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
        face_alphas=_decode_face_alphas(data, var32, var10, var14),
        face_render_types=_decode_face_render_types(data, var28, var10, has_face_render_types),
        vertex_skins=_decode_vertex_skins(data, var31, var9, var17),
        face_tex_coords=face_tex_coords,
        tex_u=tex_u,
        tex_v=tex_v,
        tex_w=tex_w,
        tex_face_count=var11,
    )
