"""Export render templates for locs the game spawns at runtime.

Map squares only carry the locs placed in the cache map data. Mechanics can
place others (Vorkath's acid pools, ...): the admitted encounter documents
name each such loc in an `[[objects]]` row (B237 object id, expected name,
placement type). This exporter renders every (loc, type) at the four
rotations through the same recolor/retexture/scale/mirror/rotate path as the
animated map objects and writes:

  data/models/spawn_objects.models  one model per (loc, type, rotation);
                                    MDL3 plus a sibling atlas when any face
                                    is textured, MDL2 otherwise
  data/models/spawn_objects.oanim   OANM rows flagged SPAWN_TEMPLATE whose
                                    positions are relative to the placement
                                    tile's south-west corner (plus ground);
                                    CONTOURED when the model follows the
                                    terrain (contoured locs and type 22), as
                                    the map exporter contours placed locs

The viewer draws a spawned loc state from the row matching its loc id,
type and rotation; the animation export scans the rows for sequence ids.
"""

from __future__ import annotations

import argparse
import sys
import tomllib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from content_paths import content_read_path
from export_objects import (
    OANM_FLAG_CONTOURED,
    OANM_FLAG_SPAWN_TEMPLATE,
    AnimatedObjectPlacement,
    _clone_model_data,
    load_modern_object_definitions,
    load_modern_object_materials,
    mirror_model,
    model_key_for_type,
    prepare_loc_model_set,
    resolve_visual_loc,
    rotate_model_90,
    write_object_anim_binary,
)
from export_models import write_models_binary
from rc_cache import RcCacheStore, load_model
from source_inputs import B237_CACHE

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = ROOT / "data/models/spawn_objects.models"
SPAWN_OBJECT_MODEL_BASE = 0xA3000000
# Placement types whose model sits centred on its footprint: centrepieces
# and ground decoration. Walls and wall decoration need edge offsets the
# runtime spawn path does not place.
SUPPORTED_TYPES = {10, 11, 22}


def encounter_spawn_locs(encounters_dir: Path) -> dict[tuple[int, int], str]:
    """(loc id, type) -> expected name for every admitted encounter's objects."""
    out: dict[tuple[int, int], str] = {}
    for path in sorted(encounters_dir.glob("*.toml")):
        if path.name.startswith("_"):
            continue
        with path.open("rb") as f:
            doc = tomllib.load(f)
        if "support" not in doc:
            continue
        for row in doc.get("objects", []):
            key = (int(row["object_id"]), int(row["type"]))
            name = str(row["name"])
            if out.setdefault(key, name) != name:
                raise SystemExit(f"{path.name}: object {key[0]} named "
                                 f"{name!r}, elsewhere {out[key]!r}")
    return out


def template_position(loc, rotation: int) -> tuple[float, float, float]:
    """Model origin relative to the tile's south-west corner (OANM axes)."""
    scale = 1.0 / 128.0
    w, l = loc.width, loc.length
    if rotation in (1, 3):
        w, l = l, w
    return (w / 2.0 + loc.offset_x * scale,
            loc.offset_h * scale,
            -(l / 2.0) - loc.offset_y * scale)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--cache", type=Path, default=B237_CACHE,
                        help="B237 OpenRS2 cache directory")
    parser.add_argument("--encounters", type=Path,
                        default=content_read_path("encounters"),
                        help="encounter documents naming spawned locs")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()

    wanted = encounter_spawn_locs(args.encounters)
    reader = RcCacheStore(args.cache)
    loc_defs = load_modern_object_definitions(reader)
    tex_colors, atlas, _texture_defs = load_modern_object_materials(reader)
    model_cache: dict = {}
    loader = lambda model_id: load_model(reader, model_id)

    rows: list[AnimatedObjectPlacement] = []
    models = []
    failures: list[str] = []
    for (loc_id, obj_type), name in sorted(wanted.items()):
        source = loc_defs.get(loc_id)
        if source is None or source.name != name:
            failures.append(f"loc {loc_id} is {getattr(source, 'name', None)!r}, "
                            f"expected {name!r}")
            continue
        if obj_type not in SUPPORTED_TYPES:
            failures.append(f"loc {loc_id}: placement type {obj_type} is not "
                            f"spawnable (supported: {sorted(SUPPORTED_TYPES)})")
            continue
        loc = resolve_visual_loc(source, loc_defs, obj_type)
        model_ids = model_key_for_type(loc, obj_type) if loc else ()
        base = prepare_loc_model_set(loc, model_ids, loader, model_cache) \
            if model_ids else None
        if base is None:
            failures.append(f"loc {loc_id} type {obj_type}: no model decodes")
            continue
        flags = OANM_FLAG_SPAWN_TEMPLATE
        if loc.contoured_ground or obj_type == 22:
            flags |= OANM_FLAG_CONTOURED
        for rotation in range(4):
            md = _clone_model_data(base)
            if loc.rotated:
                mirror_model(md)
            for _ in range(rotation):
                rotate_model_90(md)
            md.model_id = SPAWN_OBJECT_MODEL_BASE + len(models)
            models.append(md)
            pos_x, pos_y, pos_z = template_position(loc, rotation)
            rows.append(AnimatedObjectPlacement(
                model_id=md.model_id, obj_id=loc_id,
                animation_id=loc.animation_id, world_x=0, world_y=0, plane=0,
                obj_type=obj_type, rotation=rotation,
                flags=flags, pos_x=pos_x, pos_y=pos_y,
                pos_z=pos_z, phase_ticks=0.0))
        print(f"  loc {loc_id} {name!r} type {obj_type}: models "
              f"{list(model_ids)}, sequence {loc.animation_id}, "
              f"contoured {bool(flags & OANM_FLAG_CONTOURED)}")
    if failures:
        for failure in failures:
            print(f"error: {failure}", file=sys.stderr)
        return 1

    textured = any(t >= 0 for md in models for t in md.face_textures)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    atlas_path = args.output.with_suffix(".atlas")
    write_models_binary(args.output, models, tex_colors=tex_colors,
                        atlas=atlas if textured else None,
                        atlas_path=atlas_path, write_atlas_companion=textured)
    if not textured:
        atlas_path.unlink(missing_ok=True)
    templates = args.output.with_suffix(".oanim")
    write_object_anim_binary(templates, rows)
    print(f"wrote {len(models)} models to {args.output} and {len(rows)} "
          f"templates to {templates} ({len(wanted)} spawned locs)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
