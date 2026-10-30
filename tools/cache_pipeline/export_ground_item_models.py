#!/usr/bin/env python3
"""Export cache item appearances as individually loadable ground models."""

import argparse
import copy
from pathlib import Path

from export_item_sprites_b237 import decode_item_sprite_def, apply_item_overrides
from export_textures import build_atlas
from rc_cache import (RcCacheStore, load_model, load_texture_average_colors,
                      load_texture_sprites, write_models_binary)


def ground_appearance(item_id, definitions):
    item = copy.copy(definitions[item_id])
    for kind, template_id, base_id in (
        ("note", item.note_template_id, item.note_id),
        ("bought", item.bought_template_id, item.bought_id),
        ("placeholder", item.placeholder_template_id, item.placeholder_id),
    ):
        if template_id < 0:
            continue
        template = definitions[template_id]
        colors = definitions[base_id] if kind == "bought" else template
        item.inventory_model = template.inventory_model
        for field in ("recolor_src", "recolor_dst", "retexture_src", "retexture_dst"):
            setattr(item, field, getattr(colors, field))
        break
    return item


def ground_model(item_id, definitions, loader):
    item = ground_appearance(item_id, definitions)
    if item.inventory_model < 0:
        return None
    model = copy.deepcopy(loader(item.inventory_model))
    if model is None:
        raise ValueError(f"ground item {item_id}: missing cache model {item.inventory_model}")
    apply_item_overrides(model, item)
    model.model_id = item_id
    model._export_ambient = item.ambient + 64
    model._export_contrast = item.contrast * 5 + 768
    return model


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("data/models/ground"))
    args = parser.parse_args()
    store = RcCacheStore(args.cache)
    definitions = {i: decode_item_sprite_def(i, data)
                   for i, data in store.read_group(2, 10).items()}
    # Same layout as items.atlas, shared by equipped and ground models at runtime.
    atlas = build_atlas(load_texture_sprites(store), repeat_v_padding=128)
    colors = load_texture_average_colors(store)
    count = 0
    for item_id in sorted(definitions):
        model = ground_model(item_id, definitions, lambda mid: load_model(store, mid))
        if model is None:
            continue
        write_models_binary(args.output / f"{item_id}.models", [model],
                            tex_colors=colors, atlas=atlas, write_atlas_companion=False)
        count += 1
    print(f"ground items: exported {count} cache appearances to {args.output}")


if __name__ == "__main__":
    main()
