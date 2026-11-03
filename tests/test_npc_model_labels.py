"""NPC models must match the cache their animations came from.

Vertex labels are renumbered with a skeleton, so a model from another cache
keeps its shape and flies apart under the new frames.
"""

import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/cache_pipeline"))

from rc_cache import (  # noqa: E402
    CONFIG_NPC,
    RcCacheStore,
    decode_npc_definition,
    load_model,
    merge_models,
    read_config_group,
    weld_model,
)

CACHE = next(iter(sorted((ROOT / "data" / "source").glob("b241-*/cache"))), ROOT)
MODELS = ROOT / "data/models/npcs.models"
MDL3_MAGIC = 0x4D444C33
GOBLINS = (3029, 3033, 655)  # Lumbridge goblins, multi-part with new labels


def model_labels(path: Path, wanted: set[int]) -> dict[int, bytes]:
    out: dict[int, bytes] = {}
    with path.open("rb") as f:
        magic, count = struct.unpack("<II", f.read(8))
        offsets = struct.unpack(f"<{count}I", f.read(count * 4))
        for offset in offsets:
            f.seek(offset)
            model_id, expanded, _faces, base = struct.unpack("<IHHH", f.read(10))
            if model_id not in wanted:
                continue
            texcoords = expanded * 8 if magic == MDL3_MAGIC else 0
            f.seek(expanded * 12 + expanded * 4 + texcoords + base * 6, 1)
            out[model_id] = f.read(base)
    return out


@unittest.skipUnless(CACHE != ROOT and MODELS.is_file(), "needs the cache and models")
class NpcModelLabelTests(unittest.TestCase):
    def test_exported_labels_match_the_cache(self):
        store = RcCacheStore(CACHE)
        files = read_config_group(store, CONFIG_NPC)
        labels = model_labels(MODELS, set(GOBLINS))
        for npc_id in GOBLINS:
            definition = decode_npc_definition(npc_id, files[npc_id])
            parts = [load_model(store, model) for model in definition.models]
            merged = weld_model(merge_models(parts))
            self.assertEqual(labels[npc_id], bytes(merged.vertex_skins), npc_id)


class WeldTests(unittest.TestCase):
    def test_shared_vertices_keep_the_first_label(self):
        sys.path.insert(0, str(ROOT / "tools/cache_pipeline"))
        from export_models import ModelData

        first = ModelData(vertex_count=3, face_count=1, vertices_x=[0, 1, 0],
                          vertices_y=[0, 0, 1], vertices_z=[0, 0, 0],
                          face_a=[0], face_b=[1], face_c=[2],
                          vertex_skins=[1, 2, 3])
        second = ModelData(vertex_count=3, face_count=1, vertices_x=[1, 2, 0],
                           vertices_y=[0, 0, 1], vertices_z=[0, 0, 0],
                           face_a=[0], face_b=[1], face_c=[2],
                           vertex_skins=[7, 8, 9])
        merged = weld_model(merge_models([first, second]))
        self.assertEqual(merged.vertex_count, 4)
        self.assertEqual(merged.vertex_skins, [1, 2, 3, 8])
        self.assertEqual((merged.face_a[1], merged.face_b[1], merged.face_c[1]),
                         (1, 3, 2))


if __name__ == "__main__":
    unittest.main()
