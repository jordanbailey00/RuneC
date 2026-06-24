"""Export projectile/spotanim model meshes from a Jagex dat2 cache.

This is intentionally narrow: projectile visuals are spotanim definitions
whose models live in cache index 7. The generated `.models` file uses the
same MDL2 format as the viewer's existing item/NPC model loaders.
"""

import argparse
import copy
import struct
from pathlib import Path

from export_textures import build_atlas
from rc_cache import (
    CONFIG_SPOTANIM,
    INDEX_CONFIGS,
    RcCacheStore,
    decode_spotanim_definition,
    load_model,
    load_texture_average_colors,
    load_texture_sprites,
    write_models_binary,
)

SPOTANIM_BIN_MAGIC = 0x544F5053
SPOTANIM_MODEL_BASE = 0xA2000000


def parse_ids(raw: str) -> list[int]:
    ids = []
    for part in raw.split(","):
        part = part.strip()
        if part:
            ids.append(int(part, 0))
    return ids


def read_spotanim_model_ids(path: Path, selected_ids: set[int] | None) -> list[int]:
    data = path.read_bytes()
    if len(data) < 12:
        raise ValueError(f"spotanim file too small: {path}")
    magic, _version, count = struct.unpack_from("<III", data, 0)
    if magic != SPOTANIM_BIN_MAGIC:
        raise ValueError(f"bad spotanim magic in {path}")
    pos = 12
    row_size = struct.calcsize("<IiiIIIii")
    model_ids: set[int] = set()
    for _ in range(count):
        if pos + row_size > len(data):
            raise ValueError(f"truncated spotanim row in {path}")
        gfx_id, model_id, _anim_id, _sx, _sz, _rot, _bright, _shadow = (
            struct.unpack_from("<IiiIIIii", data, pos)
        )
        pos += row_size
        if selected_ids is not None and gfx_id not in selected_ids:
            continue
        if model_id >= 0:
            model_ids.add(model_id)
    return sorted(model_ids)


def read_spotanim_ids(path: Path, selected_ids: set[int] | None) -> list[int]:
    data = path.read_bytes()
    if len(data) < 12:
        raise ValueError(f"spotanim file too small: {path}")
    magic, _version, count = struct.unpack_from("<III", data, 0)
    if magic != SPOTANIM_BIN_MAGIC:
        raise ValueError(f"bad spotanim magic in {path}")
    pos = 12
    row_size = struct.calcsize("<IiiIIIii")
    ids: set[int] = set()
    for _ in range(count):
        if pos + row_size > len(data):
            raise ValueError(f"truncated spotanim row in {path}")
        gfx_id, model_id, _anim_id, _sx, _sz, _rot, _bright, _shadow = (
            struct.unpack_from("<IiiIIIii", data, pos)
        )
        pos += row_size
        if selected_ids is not None and gfx_id not in selected_ids:
            continue
        if model_id >= 0:
            ids.add(gfx_id)
    return sorted(ids)


def selected_spotanim_defs(store: RcCacheStore, ids: list[int]) -> dict[int, object]:
    selected = set(ids)
    files = store.read_group(INDEX_CONFIGS, CONFIG_SPOTANIM)
    out = {}
    for gfx_id, data in files.items():
        if gfx_id in selected:
            out[gfx_id] = decode_spotanim_definition(gfx_id, data)
    return out


def apply_spotanim_recolors(model, spotanim) -> None:
    for src, dst in zip(spotanim.recolor_from, spotanim.recolor_to):
        for fi in range(model.face_count):
            if model.face_colors[fi] == src:
                model.face_colors[fi] = dst


def main() -> None:
    parser = argparse.ArgumentParser(
        description="export projectile model meshes from cache index 7"
    )
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--models",
                        help="comma-separated model IDs")
    parser.add_argument("--spotanims", type=Path,
                        help="SPOT binary whose model ids should be exported")
    parser.add_argument("--spotanim-ids",
                        help="comma-separated spotanim ids, or 'all' with --spotanims")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    explicit_model_ids = parse_ids(args.models) if args.models else []
    spotanim_model_ids: list[int] = []
    spotanim_ids: list[int] = []
    if args.spotanims:
        selected = None
        if args.spotanim_ids and args.spotanim_ids != "all":
            selected = set(parse_ids(args.spotanim_ids))
        spotanim_model_ids = read_spotanim_model_ids(args.spotanims, selected)
        spotanim_ids = read_spotanim_ids(args.spotanims, selected)
    if not explicit_model_ids and not spotanim_model_ids:
        raise SystemExit("provide --models and/or --spotanims")

    store = RcCacheStore(args.cache)
    tex_colors = load_texture_average_colors(store)
    atlas = build_atlas(load_texture_sprites(store))
    models = []
    for model_id in sorted(set(explicit_model_ids) | set(spotanim_model_ids)):
        model = load_model(store, model_id)
        if model is None:
            print(f"warning: model {model_id} not found or failed to decode")
            continue
        models.append(model)
        print(f"model {model_id}: {model.vertex_count} verts, {model.face_count} faces")

    if spotanim_ids:
        spot_defs = selected_spotanim_defs(store, spotanim_ids)
        for gfx_id in spotanim_ids:
            spot = spot_defs.get(gfx_id)
            if spot is None or spot.model_id < 0:
                continue
            model = load_model(store, spot.model_id)
            if model is None:
                print(
                    f"warning: spotanim {gfx_id} model {spot.model_id} "
                    "not found or failed to decode"
                )
                continue
            model = copy.deepcopy(model)
            model.model_id = SPOTANIM_MODEL_BASE + gfx_id
            apply_spotanim_recolors(model, spot)
            models.append(model)
            print(
                f"spotanim {gfx_id}: model {spot.model_id} -> synthetic "
                f"{model.model_id}"
            )

    write_models_binary(args.output, models, tex_colors=tex_colors, atlas=atlas)
    print(f"wrote {len(models)} projectile models to {args.output}")


if __name__ == "__main__":
    main()
