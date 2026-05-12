"""Interface/component decoders for the modern OSRS cache."""

from __future__ import annotations

import io
from dataclasses import dataclass, field
from typing import Any

from .bytes import (
    read_i8,
    read_i16,
    read_i32,
    read_string,
    read_u8,
    read_u16,
    read_u24,
    read_u32,
)
from .config import INDEX_INTERFACES
from .store import RcCacheStore


IF3_LISTENER_NAMES = (
    "onLoad",
    "onMouseOver",
    "onMouseLeave",
    "onTargetLeave",
    "onTargetEnter",
    "onVarTransmit",
    "onInvTransmit",
    "onStatTransmit",
    "onTimer",
    "onOp",
    "onMouseRepeat",
    "onClick",
    "onClickRepeat",
    "onRelease",
    "onHold",
    "onDrag",
    "onDragComplete",
    "onScrollWheel",
)


def _missing_u16(value: int) -> int:
    return -1 if value == 0xFFFF else value


def _parent_id(component_id: int, raw_parent_id: int) -> int:
    if raw_parent_id == 0xFFFF:
        return -1
    return raw_parent_id + (component_id & ~0xFFFF)


@dataclass
class InterfaceDef:
    """Decoded IF1/IF3 component definition.

    The shape intentionally mirrors the fields consumed by RuneLite's cache
    decoder, but it is repo-local and keeps listener/trigger payloads available
    for the future C UI runtime.
    """

    id: int = -1
    group_id: int = -1
    file_id: int = -1
    is_if3: bool = False
    type: int = 0
    content_type: int = 0
    original_x: int = 0
    original_y: int = 0
    original_width: int = 0
    original_height: int = 0
    width_mode: int = 0
    height_mode: int = 0
    x_position_mode: int = 0
    y_position_mode: int = 0
    parent_id: int = -1
    is_hidden: bool = False
    scroll_width: int = 0
    scroll_height: int = 0
    no_click_through: bool = False
    sprite_id: int = -1
    texture_id: int = 0
    sprite_tiling: bool = False
    opacity: int = 0
    border_type: int = 0
    shadow_color: int = 0
    flipped_vertically: bool = False
    flipped_horizontally: bool = False
    model_type: int = 1
    model_id: int = -1
    offset_x2d: int = 0
    offset_y2d: int = 0
    rotation_x: int = 0
    rotation_y: int = 0
    rotation_z: int = 0
    model_zoom: int = 100
    animation: int = -1
    orthogonal: bool = False
    model_height_override: int = 0
    font_id: int = -1
    text: str = ""
    line_height: int = 0
    x_text_alignment: int = 0
    y_text_alignment: int = 0
    text_shadowed: bool = False
    text_color: int = 0
    filled: bool = False
    line_width: int = 1
    line_direction: bool = False
    click_mask: int = 0
    name: str = ""
    actions: list[str] = field(default_factory=list)
    drag_dead_zone: int = 0
    drag_dead_time: int = 0
    drag_render_behavior: bool = False
    target_verb: str = ""
    listeners: dict[str, list[int | str]] = field(default_factory=dict)
    triggers: dict[str, list[int]] = field(default_factory=dict)
    has_listener: bool = False

    # IF1-only fields.
    menu_type: int = 0
    hovered_sibling_id: int = -1
    alternate_operators: list[int] = field(default_factory=list)
    alternate_rhs: list[int] = field(default_factory=list)
    client_scripts: list[list[int]] = field(default_factory=list)
    item_ids: list[int] = field(default_factory=list)
    item_quantities: list[int] = field(default_factory=list)
    x_pitch: int = 0
    y_pitch: int = 0
    x_offsets: list[int] = field(default_factory=list)
    y_offsets: list[int] = field(default_factory=list)
    sprites: list[int] = field(default_factory=list)
    config_actions: list[str | None] = field(default_factory=list)
    alternate_text: str = ""
    alternate_text_color: int = 0
    hovered_text_color: int = 0
    alternate_hovered_text_color: int = 0
    alternate_sprite_id: int = -1
    alternate_model_id: int = -1
    alternate_animation: int = -1
    spell_name: str = ""
    tooltip: str = "Ok"

    byte_length: int = 0
    bytes_consumed: int = 0
    trailing_bytes: int = 0

    def to_debug_dict(self, component_name: str = "") -> dict[str, Any]:
        """Return the compact fields needed by manifest/debug tools."""
        listener_names = sorted(self.listeners.keys())
        trigger_names = sorted(self.triggers.keys())
        out: dict[str, Any] = {
            "id": self.id,
            "group_id": self.group_id,
            "file_id": self.file_id,
            "name": component_name,
            "if3": self.is_if3,
            "type": self.type,
            "content_type": self.content_type,
            "x": self.original_x,
            "y": self.original_y,
            "width": self.original_width,
            "height": self.original_height,
            "width_mode": self.width_mode,
            "height_mode": self.height_mode,
            "x_position_mode": self.x_position_mode,
            "y_position_mode": self.y_position_mode,
            "parent_id": self.parent_id,
            "hidden": self.is_hidden,
            "scroll_width": self.scroll_width,
            "scroll_height": self.scroll_height,
            "no_click_through": self.no_click_through,
            "click_mask": self.click_mask,
            "actions": self.actions,
            "target_verb": self.target_verb,
            "listeners": listener_names,
            "listener_payloads": self.listeners,
            "triggers": trigger_names,
            "trigger_payloads": self.triggers,
            "trailing_bytes": self.trailing_bytes,
        }
        if self.text:
            out["text"] = self.text
        if self.sprite_id != -1:
            out["sprite_id"] = self.sprite_id
            out["texture_id"] = self.texture_id
            out["sprite_tiling"] = self.sprite_tiling
            out["opacity"] = self.opacity
            out["border_type"] = self.border_type
            out["shadow_color"] = self.shadow_color
            out["flipped_vertically"] = self.flipped_vertically
            out["flipped_horizontally"] = self.flipped_horizontally
        if self.model_id != -1:
            out["model_type"] = self.model_type
            out["model_id"] = self.model_id
            out["offset_x2d"] = self.offset_x2d
            out["offset_y2d"] = self.offset_y2d
            out["model_zoom"] = self.model_zoom
            out["rotation_x"] = self.rotation_x
            out["rotation_y"] = self.rotation_y
            out["rotation_z"] = self.rotation_z
            out["animation"] = self.animation
        if self.font_id != -1:
            out["font_id"] = self.font_id
            out["text_color"] = self.text_color
            out["text_shadowed"] = self.text_shadowed
            out["line_height"] = self.line_height
            out["x_text_alignment"] = self.x_text_alignment
            out["y_text_alignment"] = self.y_text_alignment
        if self.type == 3:
            out["filled"] = self.filled
        if self.type == 9:
            out["line_width"] = self.line_width
            out["line_direction"] = self.line_direction
            out["text_color"] = self.text_color
        if self.item_ids:
            out["item_grid"] = {
                "slots": len(self.item_ids),
                "x_pitch": self.x_pitch,
                "y_pitch": self.y_pitch,
            }
        if self.config_actions:
            out["config_actions"] = self.config_actions
        if self.tooltip:
            out["tooltip"] = self.tooltip
        if self.spell_name:
            out["spell_name"] = self.spell_name
        return out


def _decode_listener(buf: io.BytesIO) -> list[int | str] | None:
    count = read_u8(buf)
    if count == 0:
        return None
    values: list[int | str] = []
    for _ in range(count):
        value_type = read_u8(buf)
        if value_type == 0:
            values.append(read_i32(buf))
        elif value_type == 1:
            values.append(read_string(buf))
        else:
            # Unknown listener payloads are not expected for b237, but keeping a
            # tagged value makes validation failures easier to diagnose.
            values.append(f"<unknown:{value_type}>")
    return values


def _decode_triggers(buf: io.BytesIO) -> list[int] | None:
    count = read_u8(buf)
    if count == 0:
        return None
    return [read_i32(buf) for _ in range(count)]


def decode_interface(component_id: int, data: bytes, *, rev237: bool = True) -> InterfaceDef:
    if not data:
        raise ValueError(f"empty interface component: {component_id}")

    group_id = component_id >> 16
    file_id = component_id & 0xFFFF
    iface = InterfaceDef(
        id=component_id,
        group_id=group_id,
        file_id=file_id,
        byte_length=len(data),
    )
    buf = io.BytesIO(data)
    if data[0] == 0xFF:
        _decode_if3(iface, buf, rev237=rev237)
    else:
        _decode_if1(iface, buf, rev237=rev237)
    iface.bytes_consumed = buf.tell()
    iface.trailing_bytes = len(data) - iface.bytes_consumed
    return iface


def _decode_if3(iface: InterfaceDef, buf: io.BytesIO, *, rev237: bool) -> None:
    read_u8(buf)  # format marker/version
    iface.is_if3 = True
    iface.type = read_u8(buf)
    iface.content_type = read_u16(buf)
    iface.original_x = read_i16(buf)
    iface.original_y = read_i16(buf)
    iface.original_width = read_u16(buf)
    if iface.type == 9:
        iface.original_height = read_i16(buf)
    else:
        iface.original_height = read_u16(buf)
    iface.width_mode = read_i8(buf)
    iface.height_mode = read_i8(buf)
    iface.x_position_mode = read_i8(buf)
    iface.y_position_mode = read_i8(buf)
    iface.parent_id = _parent_id(iface.id, read_u16(buf))
    iface.is_hidden = read_u8(buf) == 1

    if iface.type == 0:
        iface.scroll_width = read_u16(buf)
        iface.scroll_height = read_u16(buf)
        iface.no_click_through = read_u8(buf) == 1

    if iface.type == 5:
        iface.sprite_id = read_i32(buf)
        iface.texture_id = read_u16(buf)
        iface.sprite_tiling = read_u8(buf) == 1
        iface.opacity = read_u8(buf)
        iface.border_type = read_u8(buf)
        iface.shadow_color = read_i32(buf)
        iface.flipped_vertically = read_u8(buf) == 1
        iface.flipped_horizontally = read_u8(buf) == 1

    if iface.type == 6:
        iface.model_type = 1
        if rev237:
            iface.model_id = read_i32(buf)
        else:
            iface.model_id = _missing_u16(read_u16(buf))
        iface.offset_x2d = read_i16(buf)
        iface.offset_y2d = read_i16(buf)
        iface.rotation_x = read_u16(buf)
        iface.rotation_z = read_u16(buf)
        iface.rotation_y = read_u16(buf)
        iface.model_zoom = read_u16(buf)
        iface.animation = _missing_u16(read_u16(buf))
        iface.orthogonal = read_u8(buf) == 1
        read_u16(buf)
        if iface.width_mode != 0:
            iface.model_height_override = read_u16(buf)
        if iface.height_mode != 0:
            read_u16(buf)

    if iface.type == 4:
        iface.font_id = _missing_u16(read_u16(buf))
        iface.text = read_string(buf)
        iface.line_height = read_u8(buf)
        iface.x_text_alignment = read_u8(buf)
        iface.y_text_alignment = read_u8(buf)
        iface.text_shadowed = read_u8(buf) == 1
        iface.text_color = read_i32(buf)

    if iface.type == 3:
        iface.text_color = read_i32(buf)
        iface.filled = read_u8(buf) == 1
        iface.opacity = read_u8(buf)

    if iface.type == 9:
        iface.line_width = read_u8(buf)
        iface.text_color = read_i32(buf)
        iface.line_direction = read_u8(buf) == 1

    iface.click_mask = read_u24(buf)
    iface.name = read_string(buf)
    action_count = read_u8(buf)
    iface.actions = [read_string(buf) for _ in range(action_count)]
    iface.drag_dead_zone = read_u8(buf)
    iface.drag_dead_time = read_u8(buf)
    iface.drag_render_behavior = read_u8(buf) == 1
    iface.target_verb = read_string(buf)

    for listener_name in IF3_LISTENER_NAMES:
        listener = _decode_listener(buf)
        if listener is not None:
            iface.listeners[listener_name] = listener
            iface.has_listener = True

    for trigger_name in ("varTransmit", "invTransmit", "statTransmit"):
        trigger = _decode_triggers(buf)
        if trigger is not None:
            iface.triggers[trigger_name] = trigger


def _decode_if1(iface: InterfaceDef, buf: io.BytesIO, *, rev237: bool) -> None:
    iface.is_if3 = False
    iface.type = read_u8(buf)
    iface.menu_type = read_u8(buf)
    iface.content_type = read_u16(buf)
    iface.original_x = read_i16(buf)
    iface.original_y = read_i16(buf)
    iface.original_width = read_u16(buf)
    iface.original_height = read_u16(buf)
    iface.opacity = read_u8(buf)
    iface.parent_id = _parent_id(iface.id, read_u16(buf))
    iface.hovered_sibling_id = _missing_u16(read_u16(buf))

    alternate_count = read_u8(buf)
    for _ in range(alternate_count):
        iface.alternate_operators.append(read_u8(buf))
        iface.alternate_rhs.append(read_u16(buf))

    script_count = read_u8(buf)
    for _ in range(script_count):
        script_len = read_u16(buf)
        script = []
        for _ in range(script_len):
            value = read_u16(buf)
            script.append(-1 if value == 0xFFFF else value)
        iface.client_scripts.append(script)

    if iface.type == 0:
        iface.scroll_height = read_u16(buf)
        iface.is_hidden = read_u8(buf) == 1

    if iface.type == 1:
        read_u16(buf)
        read_u8(buf)

    if iface.type == 2:
        slot_count = iface.original_width * iface.original_height
        iface.item_ids = [0] * slot_count
        iface.item_quantities = [0] * slot_count
        if read_u8(buf) == 1:
            iface.click_mask |= 268435456
        if read_u8(buf) == 1:
            iface.click_mask |= 1073741824
        if read_u8(buf) == 1:
            iface.click_mask |= 0x80000000
        if read_u8(buf) == 1:
            iface.click_mask |= 536870912
        iface.x_pitch = read_u8(buf)
        iface.y_pitch = read_u8(buf)
        iface.x_offsets = [0] * 20
        iface.y_offsets = [0] * 20
        iface.sprites = [-1] * 20
        for idx in range(20):
            if read_u8(buf) == 1:
                iface.x_offsets[idx] = read_i16(buf)
                iface.y_offsets[idx] = read_i16(buf)
                iface.sprites[idx] = read_i32(buf)
        iface.config_actions = [None] * 5
        for idx in range(5):
            action = read_string(buf)
            if action:
                iface.config_actions[idx] = action
                iface.click_mask |= 1 << (idx + 23)

    if iface.type == 3:
        iface.filled = read_u8(buf) == 1

    if iface.type in (1, 4):
        iface.x_text_alignment = read_u8(buf)
        iface.y_text_alignment = read_u8(buf)
        iface.line_height = read_u8(buf)
        iface.font_id = _missing_u16(read_u16(buf))
        iface.text_shadowed = read_u8(buf) == 1

    if iface.type == 4:
        iface.text = read_string(buf)
        iface.alternate_text = read_string(buf)

    if iface.type in (1, 3, 4):
        iface.text_color = read_i32(buf)

    if iface.type in (3, 4):
        iface.alternate_text_color = read_i32(buf)
        iface.hovered_text_color = read_i32(buf)
        iface.alternate_hovered_text_color = read_i32(buf)

    if iface.type == 5:
        iface.sprite_id = read_i32(buf)
        iface.alternate_sprite_id = read_i32(buf)

    if iface.type == 6:
        iface.model_type = 1
        if rev237:
            iface.model_id = read_i32(buf)
            iface.alternate_model_id = read_i32(buf)
        else:
            iface.model_id = _missing_u16(read_u16(buf))
            iface.alternate_model_id = _missing_u16(read_u16(buf))
        iface.animation = _missing_u16(read_u16(buf))
        iface.alternate_animation = _missing_u16(read_u16(buf))
        iface.model_zoom = read_u16(buf)
        iface.rotation_x = read_u16(buf)
        iface.rotation_z = read_u16(buf)

    if iface.type == 7:
        slot_count = iface.original_width * iface.original_height
        iface.item_ids = [0] * slot_count
        iface.item_quantities = [0] * slot_count
        iface.x_text_alignment = read_u8(buf)
        iface.font_id = _missing_u16(read_u16(buf))
        iface.text_shadowed = read_u8(buf) == 1
        iface.text_color = read_i32(buf)
        iface.x_pitch = read_i16(buf)
        iface.y_pitch = read_i16(buf)
        if read_u8(buf) == 1:
            iface.click_mask |= 1073741824
        iface.config_actions = [None] * 5
        for idx in range(5):
            action = read_string(buf)
            if action:
                iface.config_actions[idx] = action
                iface.click_mask |= 1 << (idx + 23)

    if iface.type == 8:
        iface.text = read_string(buf)

    if iface.menu_type == 2 or iface.type == 2:
        iface.target_verb = read_string(buf)
        iface.spell_name = read_string(buf)
        iface.click_mask |= (read_u16(buf) & 63) << 11

    if iface.menu_type in (1, 4, 5, 6):
        iface.tooltip = read_string(buf)
        if not iface.tooltip:
            if iface.menu_type == 1:
                iface.tooltip = "Ok"
            elif iface.menu_type in (4, 5):
                iface.tooltip = "Select"
            elif iface.menu_type == 6:
                iface.tooltip = "Continue"

    if iface.menu_type in (1, 4, 5):
        iface.click_mask |= 4194304
    if iface.menu_type == 6:
        iface.click_mask |= 1


def load_interface_group(
    store: RcCacheStore,
    group_id: int,
    *,
    rev237: bool = True,
) -> dict[int, InterfaceDef]:
    files = store.read_group(INDEX_INTERFACES, group_id)
    out: dict[int, InterfaceDef] = {}
    for file_id, data in sorted(files.items()):
        component_id = (group_id << 16) | file_id
        out[file_id] = decode_interface(component_id, data, rev237=rev237)
    return out


def load_all_interface_groups(
    store: RcCacheStore,
    *,
    rev237: bool = True,
) -> dict[int, dict[int, InterfaceDef]]:
    manifest = store.read_index_manifest(INDEX_INTERFACES)
    groups: dict[int, dict[int, InterfaceDef]] = {}
    for group_id in manifest.group_ids:
        groups[group_id] = load_interface_group(store, group_id, rev237=rev237)
    return groups
