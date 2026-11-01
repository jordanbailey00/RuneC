#!/usr/bin/env python3
"""Export the client's bitmap fonts for the viewer.

A font is a sprite group of 256 1-bit glyphs in cache index 8 plus 257 bytes in
index 13 under the same id: each glyph's advance, then the ascent. The viewer
lays text out from these as the client does (AbstractFont).

Format, little-endian: u32 magic, u32 font count, then per font:
  u32 font id, u32 ascent,
  256 glyph records of u8 advance, width, height, left, top,
  the glyphs' coverage bytes (0 or 255), width * height each, in glyph order.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from rc_cache import RcCacheStore
from rc_cache.config import INDEX_FONTS
from rc_cache.sprites import load_sprite_group

FONT_MAGIC = 0x54464352  # "RCFT"
# FontID: plain 11, plain 12, bold 12 and quill 8.
FONT_IDS = (494, 495, 496, 497)


def encode_font(store: RcCacheStore, font_id: int) -> bytes:
    metrics = store.read_container(INDEX_FONTS, font_id)
    if metrics is None or len(metrics) != 257:
        raise SystemExit(f"font {font_id}: expected 257 metric bytes")
    glyphs = load_sprite_group(store, font_id)
    if len(glyphs) != 256:
        raise SystemExit(f"font {font_id}: expected 256 glyphs, got {len(glyphs)}")
    out = bytearray(struct.pack("<II", font_id, metrics[256]))
    pixels = bytearray()
    for code, glyph in enumerate(glyphs):
        out += struct.pack("<5B", metrics[code], glyph.sub_width, glyph.sub_height,
                           glyph.x_offset, glyph.y_offset)
        for y in range(glyph.sub_height):
            row = (y + glyph.y_offset) * glyph.width + glyph.x_offset
            for x in range(glyph.sub_width):
                pixels.append(255 if glyph.pixels[(row + x) * 4 + 3] else 0)
    return bytes(out + pixels)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--cache", required=True, type=Path)
    parser.add_argument("--output", default=Path("data/fonts/osrs_fonts.bin"), type=Path)
    args = parser.parse_args()

    store = RcCacheStore(args.cache)
    data = struct.pack("<II", FONT_MAGIC, len(FONT_IDS))
    data += b"".join(encode_font(store, font_id) for font_id in FONT_IDS)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print(f"wrote {len(FONT_IDS)} fonts to {args.output} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
