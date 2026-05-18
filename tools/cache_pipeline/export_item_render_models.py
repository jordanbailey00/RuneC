#!/usr/bin/env python3
"""Export render-ready item/player equipment models for the Raylib viewer.

The viewer needs more than raw item model IDs. OSRS equipped appearance is built
from default identity-kit body parts plus equipped item model parts, with item
recolors/retextures applied before rendering. This script consumes RuneC's
`items.bin` for gameplay-facing item/equip metadata, reads cache item/kit/model
definitions for appearance details, and writes:

* `items.models`: MDL3 models with synthetic IDs for default body parts,
  recolored/retextured equipped item models, and recolored/retextured ground
  item models.
* `item_render.map`: a compact viewer-only item -> synthetic render model map.
"""

from __future__ import annotations

import argparse
import io
import struct
from dataclasses import dataclass
from pathlib import Path

from export_models import (
    BODY_PART_NAMES,
    DEFAULT_MALE_KITS,
    SIM_ITEM_IDS,
)
from export_textures import build_atlas, write_texture_anim_binary
from rc_cache import (
    ModelData,
    RcCacheStore,
    load_texture_average_colors,
    load_texture_definitions,
    load_texture_sprites,
    load_model as rc_load_model,
    merge_models,
    write_models_binary,
)

IDEF_MAGIC = 0x49444546
IDEF_V2 = 2
IREM_MAGIC = 0x4D455249  # "IREM"
IREM_V2 = 2
CONFIG_INDEX = 2
OBJ_GROUP = 10
IDK_GROUP = 3
MISSING_U32 = 0xFFFFFFFF
BODY_MODEL_BASE = 0xF0000
ITEM_EQUIP_MODEL_BASE = 0xE00000
ITEM_GROUND_MODEL_BASE = 0xD00000

IDEF_HAS_EQUIPMENT = 1 << 4
IDEF_STACKABLE = 1 << 0
IDEF_NOTED = 1 << 6
IDEF_PLACEHOLDER = 1 << 7

EQUIP_HEAD = 0
EQUIP_CAPE = 1
EQUIP_AMULET = 2
EQUIP_WEAPON = 3
EQUIP_BODY = 4
EQUIP_SHIELD = 5
EQUIP_LEGS = 6
EQUIP_GLOVES = 7
EQUIP_BOOTS = 8

WEARPOS_HAT = 0
WEARPOS_BACK = 1
WEARPOS_FRONT = 2
WEARPOS_RIGHT_HAND = 3
WEARPOS_TORSO = 4
WEARPOS_LEFT_HAND = 5
WEARPOS_ARMS = 6
WEARPOS_LEGS = 7
WEARPOS_HEAD = 8
WEARPOS_HANDS = 9
WEARPOS_FEET = 10
WEARPOS_JAW = 11
WEARPOS_RING = 12
WEARPOS_QUIVER = 13

BODY_HAIR = 1 << 0
BODY_JAW = 1 << 1
BODY_TORSO = 1 << 2
BODY_ARMS = 1 << 3
BODY_HANDS = 1 << 4
BODY_LEGS = 1 << 5
BODY_FEET = 1 << 6

RENDER_FLAG_TWO_HANDED = 1 << 0
RENDER_FLAG_WEARPOS_AUTHORITY = 1 << 1

# RSMod cache-enricher authority for the current showcase weapon. These fields
# mirror the params.bas_readyanim / params.bas_walk_f / params.bas_running
# values a real server syncs to the client for weapon-dependent appearance.
KNOWN_PLAYER_BAS = {
    11802: (7053, 7052, 7043),  # Armadyl godsword
}

COIN_STACK_ITEM_IDS = [995, 996, 997, 998, 999, 1000, 1001, 1002, 1003, 1004]
DEFAULT_ITEM_IDS = sorted(set(COIN_STACK_ITEM_IDS + SIM_ITEM_IDS))


@dataclass
class ItemRenderDef:
    item_id: int
    name: str
    flags: int
    linked_id_item: int
    linked_id_noted: int
    linked_id_placeholder: int
    ground_model_id: int
    male_model_ids: tuple[int, int, int]
    female_model_ids: tuple[int, int, int]
    equip_slot: int


@dataclass
class CacheItemAppearance:
    item_id: int
    name: str = ""
    inv_model: int = -1
    male_model_ids: tuple[int, int, int] = (-1, -1, -1)
    female_model_ids: tuple[int, int, int] = (-1, -1, -1)
    male_offset: int = 0
    female_offset: int = 0
    wearpos1: int = -1
    wearpos2: int = -1
    wearpos3: int = -1
    recolor_src: tuple[int, ...] = ()
    recolor_dst: tuple[int, ...] = ()
    retexture_src: tuple[int, ...] = ()
    retexture_dst: tuple[int, ...] = ()


@dataclass
class IdentityKitDef:
    kit_id: int
    body_part_id: int = -1
    body_models: tuple[int, ...] = ()
    recolor_src: tuple[int, ...] = ()
    recolor_dst: tuple[int, ...] = ()
    retexture_src: tuple[int, ...] = ()
    retexture_dst: tuple[int, ...] = ()


def _u8(buf: bytes, pos: int) -> tuple[int, int]:
    return buf[pos], pos + 1


def _u16(buf: bytes, pos: int) -> tuple[int, int]:
    return struct.unpack_from("<H", buf, pos)[0], pos + 2


def _u32(buf: bytes, pos: int) -> tuple[int, int]:
    return struct.unpack_from("<I", buf, pos)[0], pos + 4


def _be_u16(buf: bytes, pos: int) -> tuple[int, int]:
    return struct.unpack_from(">H", buf, pos)[0], pos + 2


def _be_i8(buf: bytes, pos: int) -> tuple[int, int]:
    return struct.unpack_from(">b", buf, pos)[0], pos + 1


def _be_u32(buf: bytes, pos: int) -> tuple[int, int]:
    return struct.unpack_from(">I", buf, pos)[0], pos + 4


def _read_string(buf: bytes, pos: int) -> tuple[str, int]:
    end = buf.find(b"\x00", pos)
    if end < 0:
        return "", len(buf)
    return buf[pos:end].decode("cp1252", "replace"), end + 1


def _id(raw: int) -> int:
    return -1 if raw == MISSING_U32 else int(raw)


def parse_items_bin(path: Path) -> dict[int, ItemRenderDef]:
    raw = path.read_bytes()
    pos = 0
    magic, version, count = struct.unpack_from("<III", raw, pos)
    pos += 12
    if magic != IDEF_MAGIC or version != IDEF_V2:
        raise SystemExit(f"{path} is not an IDEF v2 file")

    out: dict[int, ItemRenderDef] = {}
    for _ in range(count):
        rec_len, pos = _u32(raw, pos)
        rec = raw[pos : pos + rec_len]
        pos += rec_len

        rpos = 0
        item_id, rpos = _u32(rec, rpos)
        flags, rpos = _u16(rec, rpos)
        _kind, rpos = _u8(rec, rpos)
        name_len, rpos = _u8(rec, rpos)
        name = rec[rpos : rpos + name_len].decode("utf-8", "replace")
        rpos += name_len

        rpos += 2  # weight_cg
        rpos += 4  # highalch
        rpos += 4  # lowalch
        rpos += 4  # value
        linked_id_item, rpos = _u32(rec, rpos)
        linked_id_noted, rpos = _u32(rec, rpos)
        linked_id_placeholder, rpos = _u32(rec, rpos)
        rpos += 4  # buy_limit

        models: list[int] = []
        for _model_slot in range(7):
            model_id, rpos = _u32(rec, rpos)
            models.append(_id(model_id))

        equip_slot = -1
        if flags & IDEF_HAS_EQUIPMENT:
            slot, rpos = _u8(rec, rpos)
            req_count, rpos = _u8(rec, rpos)
            equip_slot = -1 if slot == 0xFF else int(slot)
            rpos += req_count * 2
            rpos += 14 * 2

        out[int(item_id)] = ItemRenderDef(
            item_id=int(item_id),
            name=name,
            flags=flags,
            linked_id_item=_id(linked_id_item),
            linked_id_noted=_id(linked_id_noted),
            linked_id_placeholder=_id(linked_id_placeholder),
            ground_model_id=models[0],
            male_model_ids=(models[1], models[2], models[3]),
            female_model_ids=(models[4], models[5], models[6]),
            equip_slot=equip_slot,
        )
    return out


def split_cache_group(data: bytes, file_ids: list[int]) -> dict[int, bytes]:
    if len(file_ids) <= 1:
        return {file_ids[0] if file_ids else 0: data}
    if not data:
        return {}

    file_count = len(file_ids)
    chunk_count = data[-1]
    table_size = chunk_count * file_count * 4
    table_start = len(data) - 1 - table_size
    if table_start < 0:
        raise ValueError("invalid multi-file cache group table")

    sizes = [0] * file_count
    pos = table_start
    for _chunk in range(chunk_count):
        chunk_size = 0
        for file_idx in range(file_count):
            delta = struct.unpack_from(">i", data, pos)[0]
            pos += 4
            chunk_size += delta
            sizes[file_idx] += chunk_size

    files = [bytearray() for _ in range(file_count)]
    offsets = [0] * file_count
    data_pos = 0
    pos = table_start
    for _chunk in range(chunk_count):
        chunk_size = 0
        for file_idx in range(file_count):
            delta = struct.unpack_from(">i", data, pos)[0]
            pos += 4
            chunk_size += delta
            files[file_idx].extend(data[data_pos : data_pos + chunk_size])
            offsets[file_idx] += chunk_size
            data_pos += chunk_size

    return {file_ids[i]: bytes(files[i]) for i in range(file_count)}


def read_config_group_files(store: RcCacheStore, group_id: int) -> dict[int, bytes]:
    return store.read_group(CONFIG_INDEX, group_id)


def skip_params(buf: bytes, pos: int) -> int:
    if pos >= len(buf):
        return len(buf)
    count = buf[pos]
    pos += 1
    for _ in range(count):
        if pos + 4 > len(buf):
            return len(buf)
        is_string = buf[pos]
        pos += 4  # type + 3-byte key
        if is_string:
            _value, pos = _read_string(buf, pos)
        else:
            pos += 4
    return min(pos, len(buf))


def parse_cache_item(item_id: int, data: bytes) -> CacheItemAppearance:
    name = ""
    inv_model = -1
    male = [-1, -1, -1]
    female = [-1, -1, -1]
    male_offset = 0
    female_offset = 0
    wearpos1 = -1
    wearpos2 = -1
    wearpos3 = -1
    recolor_src: list[int] = []
    recolor_dst: list[int] = []
    retexture_src: list[int] = []
    retexture_dst: list[int] = []

    pos = 0
    while pos < len(data):
        opcode = data[pos]
        pos += 1
        if opcode == 0:
            break
        if opcode == 1:
            inv_model, pos = _be_u16(data, pos)
        elif opcode == 2:
            name, pos = _read_string(data, pos)
        elif opcode == 3:
            _desc, pos = _read_string(data, pos)
        elif opcode in (4, 5, 6, 7, 8, 10, 21, 22, 66, 67, 68, 71, 73, 74, 75, 76, 77,
                        80, 81, 82, 83, 84, 85, 86, 87, 94, 95, 97, 98, 110, 111,
                        112, 139, 140, 148, 149, 156, 161, 202):
            pos += 2
        elif opcode == 9:
            _unknown, pos = _read_string(data, pos)
        elif opcode in (11, 15, 16, 64, 65):
            pass
        elif opcode in (12,):
            pos += 4
        elif opcode == 13:
            wearpos1, pos = _u8(data, pos)
        elif opcode == 14:
            wearpos2, pos = _u8(data, pos)
        elif opcode == 27:
            wearpos3, pos = _u8(data, pos)
        elif opcode in (17, 18, 19, 20, 28, 29, 42, 62, 69, 113, 114,
                        115, 119, 120, 121, 122, 155, 157, 158, 159, 162, 163,
                        165):
            pos += 1
        elif opcode == 23:
            male[0], pos = _be_u16(data, pos)
            male_offset, pos = _be_i8(data, pos)
        elif opcode == 24:
            male[1], pos = _be_u16(data, pos)
        elif opcode == 25:
            female[0], pos = _be_u16(data, pos)
            female_offset, pos = _be_i8(data, pos)
        elif opcode == 26:
            female[1], pos = _be_u16(data, pos)
        elif 30 <= opcode < 40:
            _action, pos = _read_string(data, pos)
        elif opcode == 40:
            count = data[pos]
            pos += 1
            for _ in range(count):
                src, pos = _be_u16(data, pos)
                dst, pos = _be_u16(data, pos)
                recolor_src.append(src)
                recolor_dst.append(dst)
        elif opcode == 41:
            count = data[pos]
            pos += 1
            for _ in range(count):
                src, pos = _be_u16(data, pos)
                dst, pos = _be_u16(data, pos)
                retexture_src.append(src)
                retexture_dst.append(dst)
        elif opcode == 43:
            if pos >= len(data):
                break
            pos += 1  # op id
            while pos < len(data):
                subop = data[pos] - 1
                pos += 1
                if subop == -1:
                    break
                _subop, pos = _read_string(data, pos)
        elif opcode == 44:
            inv_model, pos = _be_u32(data, pos)
        elif opcode == 45:
            male[0], pos = _be_u32(data, pos)
            male_offset, pos = _be_i8(data, pos)
        elif opcode == 46:
            male[1], pos = _be_u32(data, pos)
        elif opcode == 47:
            male[2], pos = _be_u32(data, pos)
        elif opcode == 48:
            female[0], pos = _be_u32(data, pos)
            female_offset, pos = _be_i8(data, pos)
        elif opcode == 49:
            female[1], pos = _be_u32(data, pos)
        elif opcode == 50:
            female[2], pos = _be_u32(data, pos)
        elif opcode in (51, 52, 53, 54):
            pos += 4
        elif opcode == 78:
            male[2], pos = _be_u16(data, pos)
        elif opcode == 79:
            female[2], pos = _be_u16(data, pos)
        elif opcode in (90, 91, 92, 93):
            pos += 2
        elif 100 <= opcode < 110:
            pos += 4
        elif opcode in (116, 117, 118):
            pos += 2
        elif opcode == 160:
            count = data[pos]
            pos += 1 + count * 2
        elif opcode == 164:
            _unknown, pos = _read_string(data, pos)
        elif opcode == 211:
            count = data[pos]
            pos += 1 + count * 2
        elif opcode == 249:
            pos = skip_params(data, pos)
        else:
            print(f"warning: unknown item opcode {opcode} for {item_id} at {pos - 1}")
            break

    return CacheItemAppearance(
        item_id=item_id,
        name=name,
        inv_model=inv_model,
        male_model_ids=tuple(male),
        female_model_ids=tuple(female),
        male_offset=male_offset,
        female_offset=female_offset,
        wearpos1=wearpos1,
        wearpos2=wearpos2,
        wearpos3=wearpos3,
        recolor_src=tuple(recolor_src),
        recolor_dst=tuple(recolor_dst),
        retexture_src=tuple(retexture_src),
        retexture_dst=tuple(retexture_dst),
    )


def parse_identity_kit(kit_id: int, data: bytes) -> IdentityKitDef:
    body_part_id = -1
    body_models: list[int] = []
    recolor_src: list[int] = []
    recolor_dst: list[int] = []
    retexture_src: list[int] = []
    retexture_dst: list[int] = []
    pos = 0
    while pos < len(data):
        opcode = data[pos]
        pos += 1
        if opcode == 0:
            break
        if opcode == 1:
            body_part_id = data[pos]
            pos += 1
        elif opcode == 2:
            count = data[pos]
            pos += 1
            body_models = []
            for _ in range(count):
                model_id, pos = _be_u16(data, pos)
                body_models.append(model_id)
        elif opcode == 3:
            pass
        elif opcode == 5:
            count = data[pos]
            pos += 1
            body_models = []
            for _ in range(count):
                model_id, pos = _be_u32(data, pos)
                body_models.append(model_id)
        elif opcode == 40:
            count = data[pos]
            pos += 1
            for _ in range(count):
                src, pos = _be_u16(data, pos)
                dst, pos = _be_u16(data, pos)
                recolor_src.append(src)
                recolor_dst.append(dst)
        elif opcode == 41:
            count = data[pos]
            pos += 1
            for _ in range(count):
                src, pos = _be_u16(data, pos)
                dst, pos = _be_u16(data, pos)
                retexture_src.append(src)
                retexture_dst.append(dst)
        elif 60 <= opcode < 70:
            pos += 2
        elif 70 <= opcode < 80:
            pos += 4
        else:
            print(f"warning: unknown identity-kit opcode {opcode} for {kit_id}")
            break
    return IdentityKitDef(
        kit_id=kit_id,
        body_part_id=body_part_id,
        body_models=tuple(body_models),
        recolor_src=tuple(recolor_src),
        recolor_dst=tuple(recolor_dst),
        retexture_src=tuple(retexture_src),
        retexture_dst=tuple(retexture_dst),
    )


def apply_appearance_overrides(
    model: ModelData,
    recolor_src: tuple[int, ...],
    recolor_dst: tuple[int, ...],
    retexture_src: tuple[int, ...],
    retexture_dst: tuple[int, ...],
) -> None:
    for src, dst in zip(recolor_src, recolor_dst):
        for fi in range(model.face_count):
            if model.face_colors[fi] == src:
                model.face_colors[fi] = dst
    if model.face_textures:
        for src, dst in zip(retexture_src, retexture_dst):
            for fi in range(min(model.face_count, len(model.face_textures))):
                if model.face_textures[fi] == src:
                    model.face_textures[fi] = dst


def translate_model_y(model: ModelData, dy: int) -> None:
    if dy == 0:
        return
    for i in range(model.vertex_count):
        model.vertices_y[i] += dy


def has_wearpos_data(appearance: CacheItemAppearance) -> bool:
    return appearance.wearpos1 >= 0 or appearance.wearpos2 >= 0 or appearance.wearpos3 >= 0


def render_hide_mask(item: ItemRenderDef, appearance: CacheItemAppearance) -> int:
    mask = 0

    for wearpos in (appearance.wearpos1, appearance.wearpos2, appearance.wearpos3):
        if wearpos == WEARPOS_TORSO:
            mask |= BODY_TORSO
        elif wearpos == WEARPOS_ARMS:
            mask |= BODY_ARMS
        elif wearpos == WEARPOS_LEGS:
            mask |= BODY_LEGS
        elif wearpos == WEARPOS_HEAD:
            mask |= BODY_HAIR
        elif wearpos == WEARPOS_HANDS:
            mask |= BODY_HANDS
        elif wearpos == WEARPOS_FEET:
            mask |= BODY_FEET
        elif wearpos == WEARPOS_JAW:
            mask |= BODY_JAW

    # If the cache did not provide client wearpos metadata, fall back only to
    # structurally safe slot replacement. Headwear intentionally does not hide
    # hair/jaw here: partyhats and other partial hats must keep the face/head.
    if mask == 0 and not has_wearpos_data(appearance):
        if item.equip_slot == EQUIP_BODY:
            mask |= BODY_TORSO
        elif item.equip_slot == EQUIP_LEGS:
            mask |= BODY_LEGS
        elif item.equip_slot == EQUIP_GLOVES:
            mask |= BODY_HANDS
        elif item.equip_slot == EQUIP_BOOTS:
            mask |= BODY_FEET
    return mask


def render_flags(item: ItemRenderDef, appearance: CacheItemAppearance) -> int:
    flags = 0
    if has_wearpos_data(appearance):
        flags |= RENDER_FLAG_WEARPOS_AUTHORITY
    if item.equip_slot == EQUIP_WEAPON and (
        appearance.wearpos2 == WEARPOS_LEFT_HAND
        or appearance.wearpos3 == WEARPOS_LEFT_HAND
    ):
        flags |= RENDER_FLAG_TWO_HANDED
    return flags


def synth_equip_model_id(item_id: int) -> int:
    return ITEM_EQUIP_MODEL_BASE + item_id


def synth_ground_model_id(item_id: int) -> int:
    return ITEM_GROUND_MODEL_BASE + item_id


def write_item_render_map(
    path: Path,
    body_model_ids: list[int],
    records: list[tuple[int, int, int, int, int, int, int, int, int, int, int, int, int]],
) -> None:
    def out_id(value: int) -> int:
        return value if value >= 0 else MISSING_U32

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as f:
        f.write(struct.pack("<IIII", IREM_MAGIC, IREM_V2, len(records), len(body_model_ids)))
        for model_id in body_model_ids:
            f.write(struct.pack("<I", out_id(model_id)))
        for (
            item_id,
            ground_model,
            male_model,
            female_model,
            hide_mask,
            equip_slot,
            wearpos1,
            wearpos2,
            wearpos3,
            flags,
            ready_anim,
            walk_anim,
            run_anim,
        ) in records:
            f.write(struct.pack(
                "<IIIIIIIIIIIII",
                item_id,
                out_id(ground_model),
                out_id(male_model),
                out_id(female_model),
                hide_mask,
                out_id(equip_slot),
                out_id(wearpos1),
                out_id(wearpos2),
                out_id(wearpos3),
                flags,
                out_id(ready_anim),
                out_id(walk_anim),
                out_id(run_anim),
            ))


def normalize_lookup_name(value: str) -> str:
    return "".join(
        ch.lower()
        for ch in value
        if ch not in {"'", " ", "-", "_"}
    )


def item_name_matches(a: str, b: str) -> bool:
    return normalize_lookup_name(a) == normalize_lookup_name(b)


def resolve_unnoted_item_id(
    items: dict[int, ItemRenderDef],
    name: str,
) -> int | None:
    aliases: dict[str, tuple[str | None, int | None]] = {
        "Rune arrows": ("Rune arrow", None),
        "Dragon arrows": ("Dragon arrow", None),
        "Amethyst arrows": ("Amethyst arrow", None),
        "Dragon darts": ("Dragon dart", None),
        "Amethyst darts": ("Amethyst dart", None),
        "Sunfire splinter": ("Sunfire splinters", None),
        "Granite maul (ornate handle)": (None, 12848),
    }
    for query, (actual, forced_id) in aliases.items():
        if not item_name_matches(name, query):
            continue
        if forced_id is not None:
            forced = items.get(forced_id)
            if forced and not (forced.flags & (IDEF_NOTED | IDEF_PLACEHOLDER)):
                return forced.item_id
        if actual:
            name = actual
        break

    fallback: int | None = None
    for item in items.values():
        if not item.name or not item_name_matches(item.name, name):
            continue
        if not (item.flags & (IDEF_NOTED | IDEF_PLACEHOLDER)):
            return item.item_id
        if item.flags & IDEF_NOTED and item.linked_id_item >= 0:
            linked = items.get(item.linked_id_item)
            if linked and not (linked.flags & (IDEF_NOTED | IDEF_PLACEHOLDER)):
                fallback = linked.item_id
    return fallback


def parse_dev_validation_item_names(path: Path) -> list[str]:
    src = path.read_text()
    arrays = (
        "ranged_items", "ranged_ammo", "mage_items", "melee_items",
        "pvp_items", "special_items", "special_stacks",
    )
    names: list[str] = []
    for name in arrays:
        marker = f"static const char *const {name}[] = {{"
        start = src.find(marker)
        if start < 0:
            continue
        start += len(marker)
        end = src.find("};", start)
        if end < 0:
            continue
        block = src[start:end]
        pos = 0
        while True:
            q0 = block.find('"', pos)
            if q0 < 0:
                break
            q1 = block.find('"', q0 + 1)
            if q1 < 0:
                break
            names.append(block[q0 + 1:q1])
            pos = q1 + 1
    return names


def parse_item_ids(
    raw: str | None,
    items: dict[int, ItemRenderDef],
    dev_validation_source: Path,
) -> list[int]:
    if not raw:
        return DEFAULT_ITEM_IDS
    if raw.strip().lower() == "default":
        return DEFAULT_ITEM_IDS
    if raw.strip().lower() == "all-equippable":
        return sorted(
            item.item_id
            for item in items.values()
            if item.flags & IDEF_HAS_EQUIPMENT
            and not (item.flags & (IDEF_NOTED | IDEF_PLACEHOLDER))
        )
    if raw.strip().lower() == "combat-validation":
        ids = set(DEFAULT_ITEM_IDS)
        missing: list[str] = []
        for name in parse_dev_validation_item_names(dev_validation_source):
            item_id = resolve_unnoted_item_id(items, name)
            if item_id is None:
                missing.append(name)
                continue
            ids.add(item_id)
        if missing:
            raise SystemExit(
                "combat-validation item names missing from items.bin: "
                + ", ".join(sorted(set(missing)))
            )
        return sorted(ids)
    return [int(part.strip()) for part in raw.split(",") if part.strip()]


def main() -> None:
    parser = argparse.ArgumentParser(
        description="export RuneC item ground/equipment render models"
    )
    parser.add_argument("--cache", type=Path, required=True,
                        help="path to Jagex dat2 cache directory")
    parser.add_argument("--items", type=Path, default=Path("data/defs/items.bin"),
                        help="RuneC IDEF v2 item definition binary")
    parser.add_argument("--output", type=Path,
                        default=Path("data/models/items.models"),
                        help="output MDL2 item render model file")
    parser.add_argument("--render-map", type=Path,
                        default=Path("data/models/item_render.map"),
                        help="output viewer item render mapping file")
    parser.add_argument("--item-ids", type=str, default="default",
                        help="comma-separated item IDs, 'default', "
                             "'combat-validation', or 'all-equippable'")
    parser.add_argument("--dev-validation-source", type=Path,
                        default=Path("rc-viewer/dev_validation.c"),
                        help="source file containing combat-validation bank names")
    args = parser.parse_args()

    items = parse_items_bin(args.items)
    item_ids = parse_item_ids(args.item_ids, items, args.dev_validation_source)
    store = RcCacheStore(args.cache)

    print("loading cache appearance definitions...")
    cache_item_files = read_config_group_files(store, OBJ_GROUP)
    cache_items = {
        item_id: parse_cache_item(item_id, cache_item_files[item_id])
        for item_id in item_ids
        if item_id in cache_item_files
    }

    print("loading default male identity kits...")
    idk_files = read_config_group_files(store, IDK_GROUP)
    idk_defs = {
        kit_id: parse_identity_kit(kit_id, idk_files[kit_id])
        for kit_id in DEFAULT_MALE_KITS.values()
        if kit_id in idk_files
    }

    def load_model(model_id: int) -> ModelData | None:
        model = rc_load_model(store, model_id)
        if model is None:
            print(f"warning: model {model_id} missing or failed decode")
        return model

    exported_models: list[ModelData] = []
    body_model_ids = [MISSING_U32] * len(BODY_PART_NAMES)
    for body_part_id, kit_id in sorted(DEFAULT_MALE_KITS.items()):
        kit = idk_defs.get(kit_id)
        if kit is None or not kit.body_models:
            print(f"warning: missing identity kit {kit_id} for {BODY_PART_NAMES[body_part_id]}")
            continue
        parts = [model for mid in kit.body_models if (model := load_model(mid)) is not None]
        if not parts:
            print(f"warning: no body models decoded for kit {kit_id}")
            continue
        merged = parts[0] if len(parts) == 1 else merge_models(parts)
        apply_appearance_overrides(
            merged,
            kit.recolor_src,
            kit.recolor_dst,
            kit.retexture_src,
            kit.retexture_dst,
        )
        merged.model_id = BODY_MODEL_BASE + body_part_id
        body_model_ids[body_part_id] = merged.model_id
        exported_models.append(merged)
        print(
            f"  body {BODY_PART_NAMES[body_part_id]}: kit={kit_id}, "
            f"models={list(kit.body_models)} -> synth={merged.model_id}"
        )

    records: list[tuple[int, int, int, int, int, int, int, int, int, int, int, int, int]] = []
    for item_id in item_ids:
        item = items.get(item_id)
        if item is None:
            print(f"warning: item {item_id} not present in {args.items}")
            continue

        appearance = cache_items.get(item_id, CacheItemAppearance(item_id=item_id))
        recolor_src = appearance.recolor_src
        recolor_dst = appearance.recolor_dst
        retexture_src = appearance.retexture_src
        retexture_dst = appearance.retexture_dst

        ground_synth = -1
        if item.ground_model_id >= 0:
            ground = load_model(item.ground_model_id)
            if ground:
                apply_appearance_overrides(ground, recolor_src, recolor_dst, retexture_src, retexture_dst)
                ground.model_id = synth_ground_model_id(item_id)
                ground_synth = ground.model_id
                exported_models.append(ground)

        male_synth = -1
        male_parts = [model for mid in item.male_model_ids if mid >= 0
                      if (model := load_model(mid)) is not None]
        if male_parts:
            male_model = male_parts[0] if len(male_parts) == 1 else merge_models(male_parts)
            apply_appearance_overrides(male_model, recolor_src, recolor_dst, retexture_src, retexture_dst)
            translate_model_y(male_model, appearance.male_offset)
            male_model.model_id = synth_equip_model_id(item_id)
            male_synth = male_model.model_id
            exported_models.append(male_model)

        female_synth = -1
        female_parts = [model for mid in item.female_model_ids if mid >= 0
                        if (model := load_model(mid)) is not None]
        if female_parts:
            female_model = female_parts[0] if len(female_parts) == 1 else merge_models(female_parts)
            apply_appearance_overrides(female_model, recolor_src, recolor_dst, retexture_src, retexture_dst)
            translate_model_y(female_model, appearance.female_offset)
            female_model.model_id = synth_equip_model_id(item_id) + 0x800000
            female_synth = female_model.model_id
            exported_models.append(female_model)

        hide_mask = render_hide_mask(item, appearance)
        flags = render_flags(item, appearance)
        ready_anim, walk_anim, run_anim = KNOWN_PLAYER_BAS.get(item_id, (-1, -1, -1))
        records.append((
            item_id,
            ground_synth,
            male_synth,
            female_synth,
            hide_mask,
            item.equip_slot,
            appearance.wearpos1,
            appearance.wearpos2,
            appearance.wearpos3,
            flags,
            ready_anim,
            walk_anim,
            run_anim,
        ))
        print(
            f"{item.name} ({item.item_id}): ground={item.ground_model_id}->{ground_synth}, "
            f"male={list(item.male_model_ids)}->{male_synth}, "
            f"wearpos=({appearance.wearpos1},{appearance.wearpos2},{appearance.wearpos3}), "
            f"flags=0x{flags:02x}, recolors={len(recolor_src)}, "
            f"retextures={len(retexture_src)}, hide=0x{hide_mask:02x}"
        )

    tex_colors = load_texture_average_colors(store)
    texture_defs = load_texture_definitions(store)
    atlas = build_atlas(load_texture_sprites(store), repeat_v_padding=128)
    # Face priorities are client draw-order metadata. The world/object exporters
    # may choose to bake a small geometric offset for coplanar map details, but
    # doing that to small wearable models visibly separates hat/shield faces.
    write_models_binary(
        args.output,
        exported_models,
        tex_colors=tex_colors,
        atlas=atlas,
        bake_priority_offsets=False,
        model_lighting="unlit",
    )
    write_texture_anim_binary(args.output.with_suffix(".tanim"), atlas,
                              texture_defs)
    write_item_render_map(args.render_map, body_model_ids, records)
    print(f"wrote {len(exported_models)} render models to {args.output}")
    print(f"wrote {len(records)} item render records to {args.render_map}")


if __name__ == "__main__":
    main()
