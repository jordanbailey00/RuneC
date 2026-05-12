#!/usr/bin/env python3
"""Export selected OSRS cache sprites as PNG assets.

The viewer UI needs a small, stable set of gameframe sprites. This script can
read the local Jagex dat2/idx cache layout used by the reference clients and
exports decoded sprite groups from cache index 8 into data/sprites/ui/.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError as exc:  # pragma: no cover
    raise SystemExit("Pillow is required to export PNG sprites") from exc

from rc_cache import RcCacheStore, decode_sprite_group

try:
    from modern_cache_reader import ModernCacheReader, decompress_container
except ImportError:
    ModernCacheReader = None
    decompress_container = None

SPRITE_INDEX = 8
GRAPHIC_SYMBOLS = Path("tools/cache_pipeline/source/osrs-dumps/symbols/graphic.sym")


def sanitize_name(name: str) -> str:
    return (
        name.strip()
        .replace(",", "_")
        .replace(" ", "_")
        .replace("-", "_")
        .replace("/", "_")
        .replace("(", "")
        .replace(")", "")
    )


def _add(mapping: dict[int, list[str]], sprite_id: int, *names: str) -> None:
    bucket = mapping.setdefault(sprite_id, [])
    for name in names:
        safe = sanitize_name(name)
        if safe not in bucket:
            bucket.append(safe)


def add_graphic_symbol_aliases(mapping: dict[int, list[str]]) -> None:
    """Add b237 dump aliases for dynamic UI sprites such as spell icons."""

    if not GRAPHIC_SYMBOLS.exists():
        return
    for raw_line in GRAPHIC_SYMBOLS.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or "\t" not in line:
            continue
        sprite_id_text, symbol = line.split("\t", 1)
        try:
            sprite_id = int(sprite_id_text)
        except ValueError:
            continue
        if symbol.startswith("2x_standard_spells_on,"):
            frame = symbol.rsplit(",", 1)[1]
            _add(mapping, sprite_id, f"standard_spell_on_{frame}")
        elif symbol.startswith("2x_standard_spells_off,"):
            frame = symbol.rsplit(",", 1)[1]
            _add(mapping, sprite_id, f"standard_spell_off_{frame}")


def build_sprite_map() -> dict[int, list[str]]:
    sprites: dict[int, list[str]] = {}

    _add(sprites, 897, "tradebacking_dark")
    _add(sprites, 1017, "chatbox_bg")
    _add(sprites, 1018, "main_stones_bottom")
    _add(sprites, 1031, "side_background")
    _add(sprites, 1032, "side_background_bottom")
    _add(sprites, 1033, "side_background_left1")
    _add(sprites, 1034, "side_background_left2")
    _add(sprites, 1035, "side_background_right")
    _add(sprites, 1036, "side_background_top")
    for frame, sprite_id in enumerate(range(1026, 1031)):
        _add(sprites, sprite_id, f"side_stone_highlights_{frame}")
    _add(sprites, 1173, "osrs_stretch_side_topbottom_0")
    _add(sprites, 1174, "osrs_stretch_side_topbottom_1")
    _add(sprites, 1175, "osrs_stretch_side_columns_0")
    _add(sprites, 1176, "osrs_stretch_side_columns_1")
    _add(sprites, 169, "compass")
    _add(sprites, 1037, "mini_left")
    _add(sprites, 1038, "mini_right")
    _add(sprites, 1441, "mini_topright")
    _add(sprites, 1177, "osrs_stretch_mapsurround")
    _add(sprites, 1178, "resize_map_mask")
    _add(sprites, 1179, "resize_compass_mask")
    _add(sprites, 1182, "fixed_minimap_cover")
    _add(sprites, 1183, "fixed_map_mask")
    _add(sprites, 1184, "fixed_compass_mask")
    _add(sprites, 1200, "fixed_map_clickmask")
    _add(sprites, 1611, "mini_bottom")
    _add(sprites, 5813, "compass_outline")
    _add(sprites, 5832, "border_map_compass")

    side_icons = {
        168: ("side_icons_0", "side_icon_combat"),
        898: ("side_icons_1", "side_icon_stats"),
        899: ("side_icons_2", "side_icon_quests"),
        900: ("side_icons_3", "side_icon_inventory"),
        901: ("side_icons_4", "side_icon_equipment"),
        902: ("side_icons_5", "side_icon_prayer"),
        903: ("side_icons_6", "side_icon_magic"),
        904: ("side_icons_7", "side_icon_clan"),
        905: ("side_icons_8", "side_icon_friends"),
        907: ("side_icons_10", "side_icon_logout"),
        908: ("side_icons_11", "side_icon_options"),
        909: ("side_icons_12", "side_icon_emotes"),
        910: ("side_icons_13", "side_icon_music"),
        1709: ("side_icons_22", "side_icon_grouping"),
        3560: ("side_icons_39", "side_icon_logout_modern"),
    }
    for sprite_id, names in side_icons.items():
        _add(sprites, sprite_id, *names)

    for frame, sprite_id in enumerate(range(293, 297)):
        _add(sprites, sprite_id, f"combatboxes_{frame}")
    for frame, sprite_id in ((0, 653), (1, 654)):
        _add(sprites, sprite_id, f"combatboxes_large_{frame}")
    for frame, sprite_id in ((0, 655), (1, 656)):
        _add(sprites, sprite_id, f"combatboxes_very_large_{frame}")
    _add(sprites, 657, "combatboxes_special_attack")
    _add(sprites, 760, "combat_shield")
    for frame, sprite_id in enumerate(range(233, 253)):
        _add(sprites, sprite_id, f"combaticons_{frame}")
    for frame, sprite_id in enumerate(range(253, 273)):
        _add(sprites, sprite_id, f"combaticons2_{frame}")
    for frame, sprite_id in enumerate(range(273, 293)):
        _add(sprites, sprite_id, f"combaticons3_{frame}")
    for frame, sprite_id in enumerate(range(774, 788)):
        _add(sprites, sprite_id, f"sideicons_interface_{frame}")
    _add(sprites, 1052, "sideicons_interface_14")
    _add(sprites, 1053, "sideicons_interface_15")
    _add(sprites, 1299, "sideicons_interface_16")

    _add(sprites, 675, "options_icons_16")
    _add(sprites, 912, "options_icons_18")
    _add(sprites, 1090, "options_icons_28")
    _add(sprites, 1343, "whistle")

    for frame, sprite_id in ((0, 1071), (1, 1072), (2, 2140)):
        _add(sprites, sprite_id, f"orb_frame_{frame}")
    for frame, sprite_id in {
        0: 1059,
        1: 1060,
        2: 1061,
        3: 1062,
        4: 1063,
        5: 1064,
        6: 1065,
        7: 1066,
        8: 1102,
        9: 1607,
        10: 1608,
        11: 1609,
        12: 1636,
        13: 1637,
        14: 2208,
    }.items():
        _add(sprites, sprite_id, f"orb_filler_{frame}")
    for frame, sprite_id in {
        0: 1067,
        1: 1068,
        2: 1069,
        3: 1070,
        4: 1058,
        5: 1092,
        6: 1610,
        7: 1668,
        8: 3015,
        9: 3016,
        10: 3017,
        11: 3018,
        12: 3019,
        13: 3020,
        14: 3021,
        15: 3022,
    }.items():
        _add(sprites, sprite_id, f"orb_icon_{frame}")
    _add(sprites, 2138, "ring_34_0")
    _add(sprites, 5791, "tli_button01_orbinfo_65x34_0")
    _add(sprites, 5792, "tli_button01_orbinfo_65x34_1")
    _add(sprites, 5793, "tli_button01_orbinfo_65x34_2")
    _add(sprites, 5794, "tli_button01_orb01_34x34_0")
    _add(sprites, 5795, "tli_button01_orb01_34x34_1")
    _add(sprites, 5796, "tli_button01_orb01_34x34_2")
    _add(sprites, 1196, "orb_xp_0")
    _add(sprites, 1438, "ring_30")
    _add(sprites, 1439, "worldmap_icon_0")
    _add(sprites, 2420, "wiki_icon_0")
    _add(sprites, 189, "189", "stats_total_left")
    _add(sprites, 190, "190", "stats_total_right")
    _add(sprites, 191, "191", "stats_total_middle")
    for frame, sprite_id in enumerate(range(197, 215)):
        _add(sprites, sprite_id, f"staticons_{frame}", f"skill_icon_{frame}")
    for frame, sprite_id in enumerate(range(215, 223)):
        _add(sprites, sprite_id, f"staticons2_{frame}")
    staticons2_skill_aliases = {
        215: "skill_icon_18",  # Runecraft
        216: "skill_icon_19",  # Slayer
        217: "skill_icon_20",  # Farming
        220: "skill_icon_21",  # Hunter
        221: "skill_icon_22",  # Construction
        222: "skill_icon_23",  # Sailing
    }
    for sprite_id, name in staticons2_skill_aliases.items():
        _add(sprites, sprite_id, name)

    for frame, sprite_id in enumerate(range(3051, 3057)):
        _add(sprites, sprite_id, f"chat_tab_button_{frame}")

    for frame, sprite_id in enumerate(range(156, 168)):
        _add(sprites, sprite_id, f"wornicons_{frame}")
    for frame, sprite_id in enumerate(range(170, 185)):
        _add(sprites, sprite_id, f"miscgraphics_{frame}")

    for frame in range(50):
        _add(sprites, 15 + frame, f"magicon_{frame}")
        _add(sprites, 65 + frame, f"magicoff_{frame}")
    for frame in range(20):
        _add(sprites, 115 + frame, f"prayeron_{frame}")
        _add(sprites, 135 + frame, f"prayeroff_{frame}")
    for frame, sprite_id in enumerate(range(502, 506), start=20):
        _add(sprites, sprite_id, f"prayeron_{frame}")
    for frame, sprite_id in enumerate(range(506, 510), start=20):
        _add(sprites, sprite_id, f"prayeroff_{frame}")
    _add(sprites, 944, "prayeron_24")
    _add(sprites, 948, "prayeroff_24")
    add_graphic_symbol_aliases(sprites)

    return sprites


class JagexCacheReader:
    """Reader for main_file_cache.dat2 + main_file_cache.idx* caches."""

    def __init__(self, cache_dir: Path):
        self.cache_dir = Path(cache_dir)
        self.dat_path = self.cache_dir / "main_file_cache.dat2"
        if not self.dat_path.exists():
            raise FileNotFoundError(self.dat_path)

    @staticmethod
    def _u24(raw: bytes) -> int:
        return (raw[0] << 16) | (raw[1] << 8) | raw[2]

    def read_group_container(self, index_id: int, group_id: int) -> bytes:
        idx_path = self.cache_dir / f"main_file_cache.idx{index_id}"
        with idx_path.open("rb") as idx:
            idx.seek(group_id * 6)
            entry = idx.read(6)
        if len(entry) != 6:
            raise KeyError(f"missing index entry {index_id}:{group_id}")

        length = self._u24(entry[0:3])
        sector = self._u24(entry[3:6])
        if length <= 0 or sector <= 0:
            raise KeyError(f"empty index entry {index_id}:{group_id}")

        out = bytearray()
        part = 0
        with self.dat_path.open("rb") as dat:
            while len(out) < length:
                dat.seek(sector * 520)
                if group_id > 0xFFFF:
                    header = dat.read(10)
                    if len(header) != 10:
                        raise EOFError(f"truncated sector header {sector}")
                    got_group, got_part = struct.unpack(">IH", header[:6])
                    next_sector = self._u24(header[6:9])
                    payload_size = 510
                else:
                    header = dat.read(8)
                    if len(header) != 8:
                        raise EOFError(f"truncated sector header {sector}")
                    got_group, got_part = struct.unpack(">HH", header[:4])
                    next_sector = self._u24(header[4:7])
                    payload_size = 512

                if got_group != group_id or got_part != part:
                    raise ValueError(
                        f"bad sector chain for {index_id}:{group_id}: "
                        f"got group={got_group} part={got_part}, expected part={part}"
                    )

                need = min(payload_size, length - len(out))
                payload = dat.read(need)
                if len(payload) != need:
                    raise EOFError(f"truncated sector payload {sector}")
                out.extend(payload)
                sector = next_sector
                part += 1
                if sector == 0 and len(out) < length:
                    raise EOFError(f"early end of sector chain {index_id}:{group_id}")

        return bytes(out)

    def read_group(self, index_id: int, group_id: int) -> bytes:
        raw = self.read_group_container(index_id, group_id)
        if decompress_container is None:
            raise RuntimeError("modern_cache_reader.decompress_container is unavailable")
        return decompress_container(raw)


class OpenRs2CompatReader:
    def __init__(self, cache_dir: Path):
        if ModernCacheReader is None:
            raise RuntimeError("ModernCacheReader is unavailable")
        self.reader = ModernCacheReader(cache_dir)

    def read_group(self, index_id: int, group_id: int) -> bytes:
        for method_name in ("read_group", "read_archive", "get_group"):
            method = getattr(self.reader, method_name, None)
            if method:
                return method(index_id, group_id)
        method = getattr(self.reader, "read_file", None)
        if method:
            return method(index_id, group_id, 0)
        raise RuntimeError("ModernCacheReader has no known group read method")


def decode_sprites(data: bytes) -> list[Image.Image]:
    images: list[Image.Image] = []
    for sprite in decode_sprite_group(data):
        images.append(Image.frombytes("RGBA", (sprite.width, sprite.height), sprite.pixels))

    return images


def select_reader(cache_dir: Path):
    return RcCacheStore(cache_dir)


def export_sprite(reader, output_dir: Path, sprite_id: int, names: list[str]) -> int:
    if isinstance(reader, RcCacheStore):
        data = reader.read_container(SPRITE_INDEX, sprite_id)
        if data is None:
            raise KeyError(f"missing sprite group {sprite_id}")
    else:
        data = reader.read_group(SPRITE_INDEX, sprite_id)
    images = decode_sprites(data)
    written = 0
    for idx, image in enumerate(images):
        stems: list[str] = []
        if len(images) == 1:
            stems.extend(names)
            stems.append(str(sprite_id))
        else:
            stems.extend(f"{name}_{idx}" for name in names)
            stems.append(f"{sprite_id}_{idx}")

        seen = set()
        for stem in stems:
            if stem in seen:
                continue
            seen.add(stem)
            image.save(output_dir / f"{stem}.png")
            written += 1
    return written


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)

    args.output.mkdir(parents=True, exist_ok=True)
    reader = select_reader(args.cache)
    sprites = build_sprite_map()

    ok = 0
    failed = 0
    for sprite_id, names in sorted(sprites.items()):
        try:
            written = export_sprite(reader, args.output, sprite_id, names)
            ok += 1
            print(f"{sprite_id}: {', '.join(names)} ({written} png)")
        except Exception as exc:
            failed += 1
            print(f"{sprite_id}: failed: {exc}", file=sys.stderr)

    print(f"exported {ok} sprite groups to {args.output} ({failed} failed)")
    return 1 if ok == 0 else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
