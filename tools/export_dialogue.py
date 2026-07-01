#!/usr/bin/env python3
"""Emit `data/defs/dialogue.bin` from per-transcript TOMLs.

Runtime schema: `schema/defs/dialogue.schema.toml`.
"""
from __future__ import annotations

import struct
import sys
import tomllib
from pathlib import Path

from content_paths import content_read_path

ROOT = Path(__file__).resolve().parents[1]

CURATED = content_read_path("dialogue")
OUT = ROOT / "data/defs/dialogue.bin"

DLGX_MAGIC = 0x58474C44  # 'DLGX'
DLGX_VERSION = 1

KIND_MAP = {
    "speaker": 0, "option": 1, "select": 2, "cond": 3,
    "box": 4, "act": 5, "other": 6,
}


def pack_short(s: str, maxlen: int = 255) -> bytes:
    return (s or "").encode("utf-8", errors="replace")[:maxlen]


def pack_long(s: str, maxlen: int = 65535) -> bytes:
    return (s or "").encode("utf-8", errors="replace")[:maxlen]


def main():
    transcripts = []
    for p in sorted(CURATED.glob("*.toml")):
        with p.open("rb") as f:
            d = tomllib.load(f)
        transcripts.append({
            "slug": p.stem,
            "npcs": d.get("npcs", []),
            "nodes": d.get("nodes", []),
        })
    print(f"  {len(transcripts)} transcripts loaded", file=sys.stderr)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", DLGX_MAGIC, DLGX_VERSION,
                            len(transcripts)))
        for t in transcripts:
            slug_b = pack_short(t["slug"])
            f.write(struct.pack("<B", len(slug_b))); f.write(slug_b)
            npcs = t["npcs"]
            f.write(struct.pack("<B", min(255, len(npcs))))
            for npc in npcs[:255]:
                npc_b = pack_short(npc)
                f.write(struct.pack("<B", len(npc_b))); f.write(npc_b)
            nodes = t["nodes"]
            f.write(struct.pack("<H", min(65535, len(nodes))))
            for n in nodes[:65535]:
                nid = min(65535, n.get("id", 0))
                parent = max(-1, min(32767, n.get("parent", -1)))
                depth = min(255, max(0, n.get("depth", 0)))
                kind = KIND_MAP.get(n.get("kind", "other"), 6)
                is_terminal = 1 if n.get("is_terminal") else 0
                speaker_b = pack_short(n.get("speaker", ""))
                text_b = pack_long(n.get("text", ""))
                children = n.get("children", [])[:65535]
                f.write(struct.pack("<HhBBB", nid, parent, depth, kind,
                                    is_terminal))
                f.write(struct.pack("<B", len(speaker_b))); f.write(speaker_b)
                f.write(struct.pack("<H", len(text_b))); f.write(text_b)
                f.write(struct.pack("<H", len(children)))
                for c in children:
                    f.write(struct.pack("<H", min(65535, c)))
    print(f"  → {OUT} ({OUT.stat().st_size} bytes)", file=sys.stderr)


if __name__ == "__main__":
    main()
