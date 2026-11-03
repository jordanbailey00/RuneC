#!/usr/bin/env python3
"""Bring the NPC definition snapshot's visual fields to the given cache.

Models and sequences must come from one cache: skeleton updates renumber a
model's vertex labels together with its animations, so a model from an older
cache flies apart under newer frames. This rewrites the model list and stand,
walk and run sequences of every definition the cache also has, keeps the
reviewed gameplay fields, and updates the tracked snapshot and its manifest.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/cache_pipeline"))
from rc_cache import (  # noqa: E402
    CONFIG_NPC,
    RcCacheStore,
    decode_npc_definition,
    read_config_group,
)

SNAPSHOT = ROOT / "content/runtime_snapshots/data/defs/npc_defs.bin"
MANIFEST = ROOT / "content/runtime_snapshots/manifest.json"
INSTALLED = ROOT / "data/defs/npc_defs.bin"
MAX_MODELS = 12
ANIMS_OFFSET = 4 + 1 + 2 + 2 + 12  # id, size, combat, hitpoints, stats


def refresh(data: bytes, cache: dict[int, object]) -> tuple[bytes, list[int]]:
    magic, version, count = struct.unpack_from("<III", data)
    if version != 5:
        raise SystemExit(f"expected NDEF v5, got {version}")
    out = bytearray(data[:12])
    changed: list[int] = []
    pos = 12
    for _ in range(count):
        start = pos
        npc_id = struct.unpack_from("<I", data, pos)[0]
        pos += ANIMS_OFFSET + 20
        pos += 1 + data[pos] + 10  # name, then the combat fields
        models_at = pos
        pos += 1 + data[pos] * 4
        for _option in range(5):
            pos += 1 + data[pos]
        transforms = struct.unpack_from("<H", data, pos + 19)[0]
        pos += 21 + transforms * 4

        head = bytearray(data[start:models_at])
        models = data[models_at:models_at + 1 + data[models_at] * 4]
        cache_def = cache.get(npc_id)
        if cache_def is not None:
            anims = [cache_def.stand_anim, cache_def.walk_anim, cache_def.run_anim]
            struct.pack_into("<3i", head, ANIMS_OFFSET, *anims)
            if cache_def.models:
                ids = cache_def.models[:MAX_MODELS]
                models = struct.pack(f"<B{len(ids)}I", len(ids), *ids)
        tail_at = models_at + 1 + data[models_at] * 4
        record = bytes(head) + models + data[tail_at:pos]
        if record != data[start:pos]:
            changed.append(npc_id)
        out += record
    return bytes(out), changed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, required=True)
    args = parser.parse_args()

    store = RcCacheStore(args.cache)
    cache = {}
    for npc_id, raw in read_config_group(store, CONFIG_NPC).items():
        definition = decode_npc_definition(npc_id, raw)
        if definition.complete:
            cache[npc_id] = definition

    data, changed = refresh(SNAPSHOT.read_bytes(), cache)
    SNAPSHOT.write_bytes(data)
    INSTALLED.write_bytes(data)
    manifest = json.loads(MANIFEST.read_text())
    for row in manifest["files"]:
        if row["path"] == "data/defs/npc_defs.bin":
            row["bytes"] = len(data)
            row["sha256"] = hashlib.sha256(data).hexdigest()
    MANIFEST.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print(f"refreshed {len(changed)} definitions from {args.cache}: {changed[:40]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
