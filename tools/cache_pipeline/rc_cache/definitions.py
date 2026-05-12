"""Focused b237 config definition decoders used by validation and exporters."""

from __future__ import annotations

import io
from dataclasses import dataclass, field

from .bytes import (
    read_i8,
    read_i32,
    read_i16,
    read_string,
    read_u8,
    read_u16,
    read_u32,
)

U16_MISSING = 0xFFFF


def u16_or_missing(value: int) -> int:
    return -1 if value == U16_MISSING else value


@dataclass
class DefinitionParam:
    key: int
    is_string: bool
    int_value: int = 0
    string_value: str = ""


def _read_params(buf: io.BytesIO) -> list[DefinitionParam]:
    params: list[DefinitionParam] = []
    count = read_u8(buf)
    for _ in range(count):
        is_string = read_u8(buf) == 1
        key = int.from_bytes(buf.read(3), "big")
        if is_string:
            params.append(DefinitionParam(
                key=key,
                is_string=True,
                string_value=read_string(buf),
            ))
        else:
            params.append(DefinitionParam(
                key=key,
                is_string=False,
                int_value=read_i32(buf),
            ))
    return params


def _skip_params(buf: io.BytesIO) -> None:
    _read_params(buf)


def _read_u16_list(buf: io.BytesIO, count: int) -> list[int]:
    return [read_u16(buf) for _ in range(count)]


def _read_i32_list(buf: io.BytesIO, count: int) -> list[int]:
    return [read_i32(buf) for _ in range(count)]


def _read_big_smart2(buf: io.BytesIO) -> int:
    pos = buf.tell()
    peek = buf.read(1)
    if not peek:
        return -1
    buf.seek(pos)
    if peek[0] >= 128:
        return read_i32(buf) & 0x7FFFFFFF
    value = read_u16(buf)
    return -1 if value == 32767 else value


def _read_unsigned_short_smart_minus_one(buf: io.BytesIO) -> int:
    pos = buf.tell()
    peek = buf.read(1)
    if not peek:
        return -1
    buf.seek(pos)
    if peek[0] < 128:
        return read_u8(buf) - 1
    return read_u16(buf) - 0x8001


def _skip_entity_subop(buf: io.BytesIO) -> None:
    read_u8(buf)
    read_u8(buf)
    read_string(buf)


def _skip_conditional_op(buf: io.BytesIO) -> None:
    read_u8(buf)
    read_u16(buf)
    read_u16(buf)
    read_i32(buf)
    read_i32(buf)
    read_string(buf)


def _skip_conditional_subop(buf: io.BytesIO) -> None:
    read_u8(buf)
    read_u16(buf)
    read_u16(buf)
    read_u16(buf)
    read_i32(buf)
    read_i32(buf)
    read_string(buf)


@dataclass
class DecodeSummary:
    unknown_opcode: int | None = None

    @property
    def complete(self) -> bool:
        return self.unknown_opcode is None


@dataclass
class LocationDef(DecodeSummary):
    loc_id: int = 0
    name: str = ""
    width: int = 1
    length: int = 1
    interact_type: int = 2
    solid: bool = True
    impenetrable: bool = True
    blocks_projectile: bool = True
    model_clipped: bool = False
    obstructs_ground: bool = False
    hollow: bool = False
    rotated: bool = False
    contoured: bool = False
    wall_or_door: int = -1
    animation_id: int = -1
    category: int = -1
    map_icon: int = -1
    force_approach: int = 0
    supports_items: int = -1
    ambient_sound_id: int = -1
    ambient_sound_distance: int = 0
    ambient_sound_retain: int = 0
    ambient_sound_change_ticks_min: int = 0
    ambient_sound_change_ticks_max: int = 0
    ambient_sound_ids: list[int] = field(default_factory=list)
    randomize_anim_start: bool = False
    defer_anim_change: bool = False
    varbit: int = -1
    varp: int = -1
    model_ids: list[int] = field(default_factory=list)
    transforms: list[int] = field(default_factory=list)
    params: list[DefinitionParam] = field(default_factory=list)
    actions: list[str] = field(default_factory=lambda: [""] * 5)


def _append_model(model_ids: list[int], model_id: int) -> None:
    if model_id not in model_ids:
        model_ids.append(model_id)


def decode_location_definition(loc_id: int, data: bytes) -> LocationDef:
    d = LocationDef(loc_id=loc_id)
    buf = io.BytesIO(data)
    while True:
        op = read_u8(buf)
        if op == 0:
            break
        if op == 1:
            for _ in range(read_u8(buf)):
                _append_model(d.model_ids, read_u16(buf))
                read_u8(buf)
        elif op == 2:
            d.name = read_string(buf)
        elif op == 5:
            for _ in range(read_u8(buf)):
                _append_model(d.model_ids, read_u16(buf))
        elif op == 6:
            for _ in range(read_u8(buf)):
                _append_model(d.model_ids, read_u32(buf))
                read_u8(buf)
        elif op == 7:
            for _ in range(read_u8(buf)):
                _append_model(d.model_ids, read_u32(buf))
        elif op == 14:
            d.width = read_u8(buf)
        elif op == 15:
            d.length = read_u8(buf)
        elif op == 17:
            d.interact_type = 0
            d.impenetrable = False
            d.blocks_projectile = False
        elif op == 18:
            d.impenetrable = False
            d.blocks_projectile = False
        elif op == 19:
            d.wall_or_door = read_u8(buf)
        elif op == 21:
            d.contoured = True
        elif op in (22, 64, 94):
            pass
        elif op == 23:
            d.model_clipped = True
        elif op == 89:
            d.randomize_anim_start = True
        elif op == 90:
            d.defer_anim_change = True
        elif op == 24:
            d.animation_id = u16_or_missing(read_u16(buf))
        elif op == 27:
            d.interact_type = 1
        elif op == 28:
            read_u8(buf)
        elif op == 29:
            read_i8(buf)
        elif 30 <= op <= 34:
            action = read_string(buf)
            if action and action.lower() != "hidden":
                d.actions[op - 30] = action
        elif op == 39:
            read_i8(buf)
        elif op in (40, 41):
            for _ in range(read_u8(buf)):
                read_u16(buf)
                read_u16(buf)
        elif op == 60:
            read_u16(buf)
        elif op == 61:
            d.category = u16_or_missing(read_u16(buf))
        elif op == 62:
            d.rotated = True
        elif op in (65, 66, 67, 68):
            read_u16(buf)
        elif op == 69:
            d.force_approach = read_u8(buf)
        elif op in (70, 71, 72):
            read_i16(buf)
        elif op == 73:
            d.obstructs_ground = True
        elif op == 74:
            d.solid = False
            d.hollow = True
        elif op == 75:
            d.supports_items = read_u8(buf)
        elif op in (77, 92):
            d.varbit = u16_or_missing(read_u16(buf))
            d.varp = u16_or_missing(read_u16(buf))
            default_transform = -1
            if op == 92:
                default_transform = u16_or_missing(read_u16(buf))
            count = read_u8(buf)
            for _ in range(count + 1):
                d.transforms.append(u16_or_missing(read_u16(buf)))
            if op == 92:
                d.transforms.append(default_transform)
        elif op == 78:
            d.ambient_sound_id = u16_or_missing(read_u16(buf))
            d.ambient_sound_distance = read_u8(buf)
            d.ambient_sound_retain = read_u8(buf)
        elif op == 79:
            d.ambient_sound_change_ticks_min = read_u16(buf)
            d.ambient_sound_change_ticks_max = read_u16(buf)
            d.ambient_sound_distance = read_u8(buf)
            d.ambient_sound_retain = read_u8(buf)
            d.ambient_sound_ids = [
                u16_or_missing(v) for v in _read_u16_list(buf, read_u8(buf))
            ]
        elif op == 81:
            read_u8(buf)
        elif op == 82:
            d.map_icon = u16_or_missing(read_u16(buf))
        elif op == 91:
            read_u8(buf)
        elif op == 93:
            read_u8(buf)
            read_u16(buf)
            read_u8(buf)
            read_u16(buf)
        elif op in (95, 96):
            read_u8(buf)
        elif op == 100:
            idx = read_u8(buf)
            read_u8(buf)
            action = read_string(buf)
            if (
                0 <= idx < len(d.actions)
                and action
                and action.lower() != "hidden"
                and not d.actions[idx]
            ):
                d.actions[idx] = action
        elif op in (101, 102):
            idx = read_u8(buf)
            if op == 102:
                read_u16(buf)
            read_u16(buf)
            read_u16(buf)
            read_u32(buf)
            read_u32(buf)
            action = read_string(buf)
            if (
                0 <= idx < len(d.actions)
                and action
                and action.lower() != "hidden"
                and not d.actions[idx]
            ):
                d.actions[idx] = action
        elif op == 249:
            d.params = _read_params(buf)
        else:
            d.unknown_opcode = op
            break
    return d


@dataclass
class ItemDef(DecodeSummary):
    item_id: int = 0
    name: str = ""
    inventory_model: int = -1
    cost: int = 1
    weight: int = 0
    stackable: bool = False
    tradeable: bool = False
    members: bool = False
    note_id: int = -1
    note_template_id: int = -1
    placeholder_id: int = -1
    placeholder_template_id: int = -1
    male_model_ids: list[int] = field(default_factory=lambda: [-1, -1, -1])
    female_model_ids: list[int] = field(default_factory=lambda: [-1, -1, -1])
    ground_actions: list[str | None] = field(
        default_factory=lambda: [None, None, "Take", None, None]
    )
    inventory_actions: list[str | None] = field(
        default_factory=lambda: [None, None, None, None, "Drop"]
    )


def decode_item_definition(item_id: int, data: bytes) -> ItemDef:
    d = ItemDef(item_id=item_id)
    buf = io.BytesIO(data)
    while True:
        op = read_u8(buf)
        if op == 0:
            break
        if op == 1:
            d.inventory_model = read_u16(buf)
        elif op == 2:
            d.name = read_string(buf)
        elif op == 3:
            read_string(buf)
        elif op in (4, 5, 6, 10, 21, 22, 66, 67, 68, 71, 73, 74, 76, 77):
            read_u16(buf)
        elif op in (7, 8, 97, 98, 148, 149):
            value = u16_or_missing(read_u16(buf))
            if op == 97:
                d.note_id = value
            elif op == 98:
                d.note_template_id = value
            elif op == 148:
                d.placeholder_id = value
            elif op == 149:
                d.placeholder_template_id = value
        elif op == 9:
            read_string(buf)
        elif op == 11:
            d.stackable = True
        elif op == 12:
            d.cost = read_i32(buf)
        elif op in (13, 14, 17, 18, 19, 20, 27, 28, 29, 42, 62, 69, 119, 120, 121, 122, 155, 157, 162, 163, 165):
            read_u8(buf)
        elif op == 15:
            pass
        elif op == 16:
            d.members = True
        elif op == 23:
            d.male_model_ids[0] = u16_or_missing(read_u16(buf))
            read_i8(buf)
        elif op == 24:
            d.male_model_ids[1] = u16_or_missing(read_u16(buf))
        elif op == 25:
            d.female_model_ids[0] = u16_or_missing(read_u16(buf))
            read_i8(buf)
        elif op == 26:
            d.female_model_ids[1] = u16_or_missing(read_u16(buf))
        elif op in (78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 90, 91, 92, 93, 94, 95, 110, 111, 112, 116, 117, 118, 139, 140, 156, 161):
            value = u16_or_missing(read_u16(buf))
            if op == 78:
                d.male_model_ids[2] = value
            elif op == 79:
                d.female_model_ids[2] = value
        elif 30 <= op < 35:
            action = read_string(buf)
            if action and action.lower() != "hidden":
                d.ground_actions[op - 30] = action
        elif 35 <= op < 40:
            action = read_string(buf)
            if action and action.lower() != "hidden":
                d.inventory_actions[op - 35] = action
        elif op in (40, 41):
            for _ in range(read_u8(buf)):
                read_u16(buf)
                read_u16(buf)
        elif op == 43:
            read_u8(buf)
            while True:
                subop = read_u8(buf) - 1
                if subop == -1:
                    break
                read_string(buf)
        elif op == 44:
            d.inventory_model = read_i32(buf)
        elif op == 45:
            d.male_model_ids[0] = read_i32(buf)
            read_u8(buf)
        elif op == 46:
            d.male_model_ids[1] = read_i32(buf)
        elif op == 47:
            d.male_model_ids[2] = read_i32(buf)
        elif op == 48:
            d.female_model_ids[0] = read_i32(buf)
            read_u8(buf)
        elif op == 49:
            d.female_model_ids[1] = read_i32(buf)
        elif op == 50:
            d.female_model_ids[2] = read_i32(buf)
        elif op in (51, 52, 53, 54):
            read_i32(buf)
        elif op == 64:
            pass
        elif op == 65:
            d.tradeable = True
        elif op == 75:
            d.weight = read_i16(buf)
        elif 100 <= op < 110:
            read_u16(buf)
            read_u16(buf)
        elif op in (113, 114, 115, 158, 159):
            read_u8(buf)
        elif op == 160:
            _read_u16_list(buf, read_u8(buf))
        elif op == 164:
            read_string(buf)
        elif op == 200:
            _skip_entity_subop(buf)
        elif op == 201:
            _skip_conditional_op(buf)
        elif op == 202:
            _skip_conditional_subop(buf)
        elif op == 211:
            _read_u16_list(buf, read_u8(buf))
        elif op == 249:
            _skip_params(buf)
        else:
            d.unknown_opcode = op
            break
    return d


@dataclass
class NpcDef(DecodeSummary):
    npc_id: int = 0
    name: str = ""
    size: int = 1
    combat_level: int = -1
    stats: list[int] = field(default_factory=lambda: [1, 1, 1, 1, 1, 1])
    models: list[int] = field(default_factory=list)
    chathead_models: list[int] = field(default_factory=list)
    actions: list[str] = field(default_factory=lambda: [""] * 5)
    stand_anim: int = -1
    walk_anim: int = -1
    idle_rotate_left_anim: int = -1
    idle_rotate_right_anim: int = -1
    rotate_180_anim: int = -1
    rotate_left_anim: int = -1
    rotate_right_anim: int = -1
    run_anim: int = -1
    run_rotate_180_anim: int = -1
    run_rotate_left_anim: int = -1
    run_rotate_right_anim: int = -1
    crawl_anim: int = -1
    crawl_rotate_180_anim: int = -1
    crawl_rotate_left_anim: int = -1
    crawl_rotate_right_anim: int = -1
    idle_anim_restart: bool = False
    recolor_from: list[int] = field(default_factory=list)
    recolor_to: list[int] = field(default_factory=list)
    retexture_from: list[int] = field(default_factory=list)
    retexture_to: list[int] = field(default_factory=list)
    minimap_visible: bool = True
    width_scale: int = 128
    height_scale: int = 128
    render_priority: int = 0
    ambient: int = 0
    contrast: int = 0
    rotation_speed: int = 32
    varbit: int = -1
    varp: int = -1
    transforms: list[int] = field(default_factory=list)
    interactable: bool = True
    rotation_flag: bool = True
    follower: bool = False
    low_priority_follower_ops: bool = False
    category: int = -1
    height: int = -1
    footprint_size: int = -1
    unknown1: bool = False
    can_hide_for_overlap: bool = False
    overlap_tint_hsl: int = 39188
    zbuf: bool = True


def decode_npc_definition(npc_id: int, data: bytes) -> NpcDef:
    d = NpcDef(npc_id=npc_id)
    buf = io.BytesIO(data)
    while True:
        op = read_u8(buf)
        if op == 0:
            break
        if op == 1:
            d.models = _read_u16_list(buf, read_u8(buf))
        elif op == 2:
            d.name = read_string(buf)
        elif op == 3:
            read_string(buf)
        elif op == 5:
            _read_u16_list(buf, read_u8(buf))
        elif op == 12:
            d.size = read_u8(buf)
        elif op == 13:
            d.stand_anim = u16_or_missing(read_u16(buf))
        elif op == 14:
            d.walk_anim = u16_or_missing(read_u16(buf))
        elif op == 15:
            d.idle_rotate_left_anim = u16_or_missing(read_u16(buf))
        elif op == 16:
            d.idle_rotate_right_anim = u16_or_missing(read_u16(buf))
        elif op == 18:
            d.category = u16_or_missing(read_u16(buf))
        elif op == 95:
            value = read_u16(buf)
            d.combat_level = value
        elif op == 97:
            d.width_scale = read_u16(buf)
        elif op == 98:
            d.height_scale = read_u16(buf)
        elif op == 103:
            d.rotation_speed = read_u16(buf)
        elif op == 114:
            d.run_anim = u16_or_missing(read_u16(buf))
        elif op == 116:
            d.crawl_anim = u16_or_missing(read_u16(buf))
        elif op == 124:
            d.height = read_u16(buf)
        elif op == 17:
            d.walk_anim = u16_or_missing(read_u16(buf))
            d.rotate_180_anim = u16_or_missing(read_u16(buf))
            d.rotate_left_anim = u16_or_missing(read_u16(buf))
            d.rotate_right_anim = u16_or_missing(read_u16(buf))
        elif 30 <= op <= 34:
            action = read_string(buf)
            if action and action.lower() != "hidden":
                d.actions[op - 30] = action
        elif op == 40:
            for _ in range(read_u8(buf)):
                d.recolor_from.append(read_u16(buf))
                d.recolor_to.append(read_u16(buf))
        elif op == 41:
            for _ in range(read_u8(buf)):
                d.retexture_from.append(read_u16(buf))
                d.retexture_to.append(read_u16(buf))
        elif op == 60:
            d.chathead_models = _read_u16_list(buf, read_u8(buf))
        elif op == 61:
            d.models = _read_i32_list(buf, read_u8(buf))
        elif op == 62:
            d.chathead_models = _read_i32_list(buf, read_u8(buf))
        elif 74 <= op <= 79:
            d.stats[op - 74] = read_u16(buf)
        elif op == 93:
            d.minimap_visible = False
        elif op == 99:
            d.render_priority = 1
        elif op == 100:
            d.ambient = read_i8(buf)
        elif op == 101:
            d.contrast = read_i8(buf)
        elif op == 102:
            bitfield = read_u8(buf)
            bit_count = 0
            value = bitfield
            while value:
                bit_count += 1
                value >>= 1
            for idx in range(bit_count):
                if bitfield & (1 << idx):
                    _read_big_smart2(buf)
                    _read_unsigned_short_smart_minus_one(buf)
        elif op in (106, 118):
            d.varbit = u16_or_missing(read_u16(buf))
            d.varp = u16_or_missing(read_u16(buf))
            default_transform = -1
            if op == 118:
                default_transform = u16_or_missing(read_u16(buf))
            count = read_u8(buf)
            d.transforms = [u16_or_missing(read_u16(buf)) for _ in range(count + 1)]
            d.transforms.append(default_transform)
        elif op == 107:
            d.interactable = False
        elif op == 108:
            pass
        elif op == 109:
            d.rotation_flag = False
        elif op == 111:
            d.render_priority = 2
        elif op == 115:
            d.run_anim = u16_or_missing(read_u16(buf))
            d.run_rotate_180_anim = u16_or_missing(read_u16(buf))
            d.run_rotate_left_anim = u16_or_missing(read_u16(buf))
            d.run_rotate_right_anim = u16_or_missing(read_u16(buf))
        elif op == 117:
            d.crawl_anim = u16_or_missing(read_u16(buf))
            d.crawl_rotate_180_anim = u16_or_missing(read_u16(buf))
            d.crawl_rotate_left_anim = u16_or_missing(read_u16(buf))
            d.crawl_rotate_right_anim = u16_or_missing(read_u16(buf))
        elif op == 122:
            d.follower = True
        elif op == 123:
            d.low_priority_follower_ops = True
        elif op == 125:
            read_u8(buf)
        elif op in (126, 146):
            value = read_u16(buf)
            if op == 126:
                d.footprint_size = value
            else:
                d.overlap_tint_hsl = value
        elif op == 128:
            read_u8(buf)
        elif op == 129:
            d.unknown1 = True
        elif op == 130:
            d.idle_anim_restart = True
        elif op == 145:
            d.can_hide_for_overlap = True
        elif op == 147:
            d.zbuf = False
        elif op == 249:
            _skip_params(buf)
        elif op == 251:
            _skip_entity_subop(buf)
        elif op == 252:
            _skip_conditional_op(buf)
        elif op == 253:
            _skip_conditional_subop(buf)
        else:
            d.unknown_opcode = op
            break
    return d


@dataclass
class VarbitDef(DecodeSummary):
    varbit_id: int = 0
    base_varp: int = 0
    lsb: int = 0
    msb: int = 0


def decode_varbit_definition(varbit_id: int, data: bytes) -> VarbitDef:
    d = VarbitDef(varbit_id=varbit_id)
    buf = io.BytesIO(data)
    while True:
        op = read_u8(buf)
        if op == 0:
            break
        if op == 1:
            d.base_varp = read_u16(buf)
            d.lsb = read_u8(buf)
            d.msb = read_u8(buf)
        else:
            d.unknown_opcode = op
            break
    return d


@dataclass
class VarpDef(DecodeSummary):
    varp_id: int = 0
    varp_type: int = 0


def decode_varp_definition(varp_id: int, data: bytes) -> VarpDef:
    d = VarpDef(varp_id=varp_id)
    buf = io.BytesIO(data)
    while True:
        op = read_u8(buf)
        if op == 0:
            break
        if op == 5:
            d.varp_type = read_u16(buf)
        else:
            d.unknown_opcode = op
            break
    return d


@dataclass
class SpotanimDef(DecodeSummary):
    spotanim_id: int = 0
    model_id: int = -1
    animation_id: int = -1
    resize_xy: int = 128
    resize_z: int = 128
    rotation: int = 0
    brightness: int = 0
    shadow: int = 0
    recolor_from: list[int] = field(default_factory=list)
    recolor_to: list[int] = field(default_factory=list)


def decode_spotanim_definition(spotanim_id: int, data: bytes) -> SpotanimDef:
    d = SpotanimDef(spotanim_id=spotanim_id)
    buf = io.BytesIO(data)
    while True:
        op = read_u8(buf)
        if op == 0:
            break
        if op == 1:
            d.model_id = read_u16(buf)
        elif op == 2:
            d.animation_id = read_u16(buf)
        elif op == 3:
            d.model_id = read_i32(buf)
        elif op == 4:
            d.resize_xy = read_u16(buf)
        elif op == 5:
            d.resize_z = read_u16(buf)
        elif op == 6:
            d.rotation = read_u16(buf)
        elif op == 7:
            d.brightness = read_u8(buf)
        elif op == 8:
            d.shadow = read_u8(buf)
        elif op == 9:
            read_string(buf)
        elif op == 40:
            for _ in range(read_u8(buf)):
                d.recolor_from.append(read_u16(buf))
                d.recolor_to.append(read_u16(buf))
        elif op == 41:
            for _ in range(read_u8(buf)):
                read_u16(buf)
                read_u16(buf)
        else:
            d.unknown_opcode = op
            break
    return d


@dataclass
class SequenceDef(DecodeSummary):
    sequence_id: int = 0
    frame_count: int = 0
    frame_delays: list[int] = field(default_factory=list)
    primary_frame_ids: list[int] = field(default_factory=list)
    frame_step: int = -1
    interleave_order: list[int] = field(default_factory=list)
    stretches: bool = False
    forced_priority: int = 5
    left_hand_item: int = -1
    right_hand_item: int = -1
    max_loops: int = 99
    precedence_animating: int = -1
    priority: int = -1
    reply_mode: int = -1
    anim_maya_id: int = -1
    anim_maya_start: int = 0
    anim_maya_end: int = 0
    vertical_offset: int = 0
    anim_maya_masks: list[int] = field(default_factory=list)
    sounds_cross_world_view: bool = False

    @property
    def seq_id(self) -> int:
        return self.sequence_id

    @property
    def walk_flag(self) -> int:
        return self.priority

    @property
    def run_flag(self) -> int:
        return self.precedence_animating


def _skip_frame_sound_rev226(buf: io.BytesIO) -> None:
    read_u16(buf)
    read_u8(buf)
    read_u8(buf)
    read_u8(buf)
    read_u8(buf)


def decode_sequence_definition(sequence_id: int, data: bytes) -> SequenceDef:
    d = SequenceDef(sequence_id=sequence_id)
    buf = io.BytesIO(data)
    while True:
        op = read_u8(buf)
        if op == 0:
            break
        if op == 1:
            d.frame_count = read_u16(buf)
            d.frame_delays = _read_u16_list(buf, d.frame_count)
            file_ids = _read_u16_list(buf, d.frame_count)
            group_ids = _read_u16_list(buf, d.frame_count)
            d.primary_frame_ids = [
                (group_ids[i] << 16) | file_ids[i] for i in range(d.frame_count)
            ]
        elif op == 2:
            d.frame_step = read_u16(buf)
        elif op == 3:
            count = read_u8(buf)
            d.interleave_order = [read_u8(buf) for _ in range(count)]
        elif op == 4:
            d.stretches = True
        elif op == 5:
            d.forced_priority = read_u8(buf)
        elif op == 6:
            d.left_hand_item = u16_or_missing(read_u16(buf))
        elif op == 7:
            d.right_hand_item = u16_or_missing(read_u16(buf))
        elif op == 8:
            d.max_loops = read_u8(buf)
        elif op == 9:
            d.precedence_animating = read_u8(buf)
        elif op == 10:
            d.priority = read_u8(buf)
        elif op == 11:
            d.reply_mode = read_u8(buf)
        elif op == 12:
            count = read_u8(buf)
            _read_u16_list(buf, count)
            _read_u16_list(buf, count)
        elif op == 13:
            d.anim_maya_id = read_i32(buf)
        elif op == 14:
            for _ in range(read_u16(buf)):
                read_u16(buf)
                _skip_frame_sound_rev226(buf)
        elif op == 15:
            d.anim_maya_start = read_u16(buf)
            d.anim_maya_end = read_u16(buf)
        elif op == 16:
            d.vertical_offset = read_i8(buf)
        elif op == 17:
            d.anim_maya_masks = [read_u8(buf) for _ in range(read_u8(buf))]
        elif op == 18:
            read_string(buf)
        elif op == 19:
            d.sounds_cross_world_view = True
        else:
            d.unknown_opcode = op
            break
    if d.frame_count == 0:
        d.frame_count = 1
        d.frame_delays = [-1]
        d.primary_frame_ids = [-1]
    return d
