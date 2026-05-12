#!/usr/bin/env python3
"""Decode b237 UI interfaces and export a core interface manifest."""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from rc_cache import (
    IF3_LISTENER_NAMES,
    INDEX_INTERFACES,
    InterfaceDef,
    RcCacheStore,
    decode_interface,
)

ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOT = Path(__file__).resolve().parent / "source"
DEFAULT_CACHE = SOURCE_ROOT / "current_fightcaves_demo/data/cache"
DEFAULT_DUMP = SOURCE_ROOT / "osrs-dumps"
DEFAULT_OUTPUT = ROOT / "data/ui/interface_manifest.json"
DEFAULT_DEBUG_OUTPUT = ROOT / "data/ui/interface_debug.txt"
DEFAULT_BINARY_OUTPUT = ROOT / "data/ui/interfaces.bin"
BIN_MAGIC = b"RCUIBIN2"
BIN_VERSION = 2
TRIGGER_NAMES: tuple[str, ...] = ("varTransmit", "invTransmit", "statTransmit")


CORE_INTERFACES: tuple[tuple[str, str], ...] = (
    ("toplevel_osrs_stretch", "toplevelinterface.sym"),
    ("chatbox", "overlayinterface.sym"),
    ("orbs", "interface.sym"),
    ("hpbar_hud", "interface.sym"),
    ("pvp_icons", "interface.sym"),
    ("buff_bar", "interface.sym"),
    ("stat_boosts_hud", "interface.sym"),
    ("pm_chat", "interface.sym"),
    ("xp_drops", "interface.sym"),
    ("inventory", "interface.sym"),
    ("stats", "interface.sym"),
    ("questjournal", "interface.sym"),
    ("side_journal", "interface.sym"),
    ("wornitems", "interface.sym"),
    ("prayerbook", "interface.sym"),
    ("magic_spellbook", "interface.sym"),
    ("friends", "interface.sym"),
    ("account", "interface.sym"),
    ("logout", "interface.sym"),
    ("settings_side", "interface.sym"),
    ("emote", "interface.sym"),
    ("music", "interface.sym"),
    ("side_channels", "interface.sym"),
    ("combat_interface", "interface.sym"),
    ("equipment", "interface.sym"),
    ("equipment_side", "interface.sym"),
    ("ge_pricechecker_side", "interface.sym"),
    ("deathkeep", "interface.sym"),
)


@dataclass(frozen=True)
class DumpComponent:
    group_id: int
    file_id: int
    name: str
    fields: dict[str, list[str]]


def parse_symbol_file(path: Path) -> dict[str, int]:
    symbols: dict[str, int] = {}
    if not path.is_file():
        return symbols
    for raw_line in path.read_text(errors="replace").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("//"):
            continue
        parts = line.split()
        if len(parts) < 2:
            continue
        id_token = parts[0].split(":")[-1]
        if not re.fullmatch(r"\d+", id_token):
            continue
        symbol = parts[1]
        symbols[symbol] = int(id_token)
    return symbols


def load_symbol_tables(dump_root: Path) -> dict[str, dict[str, int]]:
    symbols_dir = dump_root / "symbols"
    return {
        "interface.sym": parse_symbol_file(symbols_dir / "interface.sym"),
        "overlayinterface.sym": parse_symbol_file(symbols_dir / "overlayinterface.sym"),
        "toplevelinterface.sym": parse_symbol_file(
            symbols_dir / "toplevelinterface.sym"
        ),
    }


def find_dump_file(dump_root: Path, name: str) -> Path | None:
    interface_dir = dump_root / "interface"
    for suffix in (".if3", ".if"):
        path = interface_dir / f"{name}{suffix}"
        if path.is_file():
            return path
    return None


def parse_dump_components(path: Path | None) -> dict[int, DumpComponent]:
    if path is None or not path.is_file():
        return {}
    components: dict[int, DumpComponent] = {}
    current_group = -1
    current_file = -1
    current_name = ""
    current_fields: dict[str, list[str]] = {}

    def flush() -> None:
        if current_group >= 0 and current_file >= 0:
            components[current_file] = DumpComponent(
                group_id=current_group,
                file_id=current_file,
                name=current_name,
                fields={key: values[:] for key, values in current_fields.items()},
            )

    for raw_line in path.read_text(errors="replace").splitlines():
        line = raw_line.strip()
        if not line:
            continue
        match = re.fullmatch(r"//\s*(\d+):(\d+)", line)
        if match:
            flush()
            current_group = int(match.group(1))
            current_file = int(match.group(2))
            current_name = ""
            current_fields = {}
            continue
        if line.startswith("[") and line.endswith("]"):
            current_name = line[1:-1]
            continue
        if "=" in line:
            key, value = line.split("=", 1)
            current_fields.setdefault(key.strip(), []).append(value.strip())
    flush()
    return components


def listener_counts(components: dict[int, InterfaceDef]) -> Counter[str]:
    counts: Counter[str] = Counter()
    for component in components.values():
        counts.update(component.listeners.keys())
    return counts


def trigger_counts(components: dict[int, InterfaceDef]) -> Counter[str]:
    counts: Counter[str] = Counter()
    for component in components.values():
        counts.update(component.triggers.keys())
    return counts


def summarize_group(
    group_id: int,
    name: str,
    components: dict[int, InterfaceDef],
    dump_components: dict[int, DumpComponent],
) -> dict[str, Any]:
    type_counts = Counter(str(component.type) for component in components.values())
    if3_count = sum(1 for component in components.values() if component.is_if3)
    sprite_refs = sorted(
        {
            component.sprite_id
            for component in components.values()
            if component.sprite_id != -1
        }
    )
    model_refs = sorted(
        {
            component.model_id
            for component in components.values()
            if component.model_id != -1
        }
    )
    trailing = [
        component.id
        for component in components.values()
        if component.trailing_bytes != 0
    ]
    return {
        "id": group_id,
        "name": name,
        "component_count": len(components),
        "dump_component_count": len(dump_components),
        "if3_count": if3_count,
        "if1_count": len(components) - if3_count,
        "type_counts": dict(sorted(type_counts.items())),
        "listener_counts": dict(sorted(listener_counts(components).items())),
        "trigger_counts": dict(sorted(trigger_counts(components).items())),
        "sprite_refs": sprite_refs,
        "model_refs": model_refs,
        "components_with_trailing_bytes": trailing,
        "components": [
            component.to_debug_dict(dump_components.get(file_id, DumpComponent(
                group_id=group_id,
                file_id=file_id,
                name="",
                fields={},
            )).name)
            for file_id, component in sorted(components.items())
        ],
    }


def decode_group(
    store: RcCacheStore,
    group_id: int,
    *,
    rev237: bool,
) -> dict[int, InterfaceDef]:
    files = store.read_group(INDEX_INTERFACES, group_id)
    out: dict[int, InterfaceDef] = {}
    for file_id, data in sorted(files.items()):
        out[file_id] = decode_interface(
            (group_id << 16) | file_id,
            data,
            rev237=rev237,
        )
    return out


def decode_all_groups(
    store: RcCacheStore,
    *,
    rev237: bool,
) -> tuple[dict[int, dict[int, InterfaceDef]], list[str]]:
    manifest = store.read_index_manifest(INDEX_INTERFACES)
    groups: dict[int, dict[int, InterfaceDef]] = {}
    errors: list[str] = []
    for group_id in manifest.group_ids:
        try:
            groups[group_id] = decode_group(store, group_id, rev237=rev237)
        except Exception as exc:  # pragma: no cover - validation path
            errors.append(f"group {group_id}: {exc}")
    return groups, errors


def write_debug_report(path: Path, groups: list[dict[str, Any]]) -> None:
    lines: list[str] = []
    for group in groups:
        lines.append(
            f"[{group['id']} {group['name']}] "
            f"components={group['component_count']} "
            f"dump={group['dump_component_count']} "
            f"if3={group['if3_count']} if1={group['if1_count']}"
        )
        lines.append(
            "  types="
            + ", ".join(
                f"{key}:{value}" for key, value in group["type_counts"].items()
            )
        )
        if group["listener_counts"]:
            lines.append(
                "  listeners="
                + ", ".join(
                    f"{key}:{value}"
                    for key, value in group["listener_counts"].items()
                )
            )
        if group["trigger_counts"]:
            lines.append(
                "  triggers="
                + ", ".join(
                    f"{key}:{value}" for key, value in group["trigger_counts"].items()
                )
            )
        for component in group["components"]:
            bits = [
                f"  {component['id'] >> 16}:{component['file_id']}",
                component["name"] or "<unnamed>",
                f"type={component['type']}",
                f"parent={component['parent_id']}",
                f"mask=0x{component['click_mask']:06x}",
            ]
            if component["actions"]:
                bits.append(f"actions={component['actions']}")
            if "sprite_id" in component:
                bits.append(f"sprite={component['sprite_id']}")
            if "model_id" in component:
                bits.append(f"model={component['model_id']}")
            if component["listeners"]:
                bits.append(f"listeners={component['listeners']}")
            if component["triggers"]:
                bits.append(f"triggers={component['triggers']}")
            if component["trailing_bytes"]:
                bits.append(f"trailing={component['trailing_bytes']}")
            lines.append(" ".join(bits))
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def _write_u8(out: bytearray, value: int) -> None:
    out.extend(struct.pack("<B", value & 0xFF))


def _write_u32(out: bytearray, value: int) -> None:
    out.extend(struct.pack("<I", value & 0xFFFFFFFF))


def _write_i32(out: bytearray, value: int) -> None:
    out.extend(struct.pack("<i", int(value)))


def _write_string(out: bytearray, value: str) -> None:
    raw = value.encode("utf-8")
    if len(raw) > 0xFFFF:
        raw = raw[:0xFFFF]
    out.extend(struct.pack("<H", len(raw)))
    out.extend(raw)


def _write_listener_value(out: bytearray, value: int | str) -> None:
    if isinstance(value, str):
        _write_u8(out, 1)
        _write_string(out, value)
    else:
        _write_u8(out, 0)
        _write_i32(out, int(value))


def write_binary(path: Path, groups: list[dict[str, Any]]) -> None:
    out = bytearray()
    out.extend(BIN_MAGIC)
    _write_u32(out, BIN_VERSION)
    _write_u32(out, len(groups))

    for group in groups:
        _write_u32(out, group["id"])
        _write_string(out, group["name"])
        components = group["components"]
        _write_u32(out, len(components))
        for component in components:
            _write_u32(out, component["id"])
            _write_i32(out, component["parent_id"])
            _write_u32(out, component["group_id"])
            _write_u32(out, component["file_id"])
            _write_u8(out, 1 if component["if3"] else 0)
            _write_u8(out, component["type"])
            _write_u8(out, 1 if component["hidden"] else 0)
            _write_u8(out, 1 if component.get("sprite_tiling") else 0)
            _write_u8(out, 1 if component.get("filled") else 0)
            _write_u8(out, 1 if component.get("line_direction") else 0)
            _write_u8(out, 1 if component.get("text_shadowed") else 0)
            _write_u8(out, 1 if component.get("flipped_vertically") else 0)
            _write_u8(out, 1 if component.get("flipped_horizontally") else 0)
            _write_u8(out, 1 if component.get("no_click_through") else 0)
            _write_u8(out, component.get("opacity", 0))
            _write_u8(out, component.get("border_type", 0))
            _write_u8(out, component.get("line_width", 1))
            _write_u8(out, component.get("line_height", 0))
            _write_i32(out, component["content_type"])
            _write_i32(out, component["x"])
            _write_i32(out, component["y"])
            _write_i32(out, component["width"])
            _write_i32(out, component["height"])
            _write_i32(out, component["width_mode"])
            _write_i32(out, component["height_mode"])
            _write_i32(out, component["x_position_mode"])
            _write_i32(out, component["y_position_mode"])
            _write_i32(out, component.get("scroll_width", 0))
            _write_i32(out, component.get("scroll_height", 0))
            _write_i32(out, component.get("sprite_id", -1))
            _write_i32(out, component.get("texture_id", 0))
            _write_i32(out, component.get("shadow_color", 0))
            _write_i32(out, component.get("model_id", -1))
            _write_i32(out, component.get("model_type", 1))
            _write_i32(out, component.get("font_id", -1))
            _write_i32(out, component.get("text_color", 0))
            _write_u32(out, component["click_mask"])
            _write_i32(out, component.get("x_text_alignment", 0))
            _write_i32(out, component.get("y_text_alignment", 0))
            _write_string(out, component.get("name", ""))
            _write_string(out, component.get("text", ""))
            _write_string(out, component.get("target_verb", ""))
            actions = component.get("actions", [])
            _write_u8(out, min(len(actions), 255))
            for action in actions[:255]:
                _write_string(out, action)
            listeners = component.get("listener_payloads", {})
            listener_items = [
                (idx, listeners[name])
                for idx, name in enumerate(IF3_LISTENER_NAMES)
                if name in listeners
            ]
            _write_u8(out, min(len(listener_items), 255))
            for kind, values in listener_items[:255]:
                _write_u8(out, kind)
                _write_u8(out, min(len(values), 255))
                for value in values[:255]:
                    _write_listener_value(out, value)
            triggers = component.get("trigger_payloads", {})
            trigger_items = [
                (idx, triggers[name])
                for idx, name in enumerate(TRIGGER_NAMES)
                if name in triggers
            ]
            _write_u8(out, min(len(trigger_items), 255))
            for kind, values in trigger_items[:255]:
                _write_u8(out, kind)
                _write_u8(out, min(len(values), 255))
                for value in values[:255]:
                    _write_i32(out, int(value))

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(out)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache", type=Path, default=DEFAULT_CACHE)
    parser.add_argument("--dump-root", type=Path, default=DEFAULT_DUMP)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--debug-output", type=Path, default=DEFAULT_DEBUG_OUTPUT)
    parser.add_argument("--binary-output", type=Path, default=DEFAULT_BINARY_OUTPUT)
    parser.add_argument(
        "--skip-all",
        action="store_true",
        help="decode only core interface groups instead of the full interface index",
    )
    parser.add_argument(
        "--print-debug",
        action="store_true",
        help="print the debug report to stdout after writing it",
    )
    parser.add_argument(
        "--legacy-model-ids",
        action="store_true",
        help="decode IF1/IF3 model ids with pre-b237 unsigned-short behavior",
    )
    args = parser.parse_args(argv)

    store = RcCacheStore(args.cache)
    index_manifest = store.read_index_manifest(INDEX_INTERFACES)
    rev237 = not args.legacy_model_ids
    symbols = load_symbol_tables(args.dump_root)

    core_groups: list[dict[str, Any]] = []
    errors: list[str] = []
    warnings: list[str] = []

    if args.skip_all:
        decoded_groups: dict[int, dict[int, InterfaceDef]] = {}
    else:
        decoded_groups, decode_errors = decode_all_groups(store, rev237=rev237)
        errors.extend(decode_errors)

    for name, symbol_file in CORE_INTERFACES:
        symbol_table = symbols.get(symbol_file, {})
        group_id = symbol_table.get(name)
        if group_id is None:
            errors.append(f"core interface symbol not found: {symbol_file}:{name}")
            continue
        dump_file = find_dump_file(args.dump_root, name)
        dump_components = parse_dump_components(dump_file)
        if group_id in decoded_groups:
            components = decoded_groups[group_id]
        else:
            try:
                components = decode_group(store, group_id, rev237=rev237)
            except Exception as exc:
                errors.append(f"core interface decode failed {name} ({group_id}): {exc}")
                continue
        if dump_components and len(dump_components) != len(components):
            errors.append(
                f"{name} ({group_id}): cache has {len(components)} components, "
                f"dump has {len(dump_components)}"
            )
        missing_dump_names = [
            file_id
            for file_id in components
            if dump_components and file_id not in dump_components
        ]
        if missing_dump_names:
            warnings.append(
                f"{name} ({group_id}): {len(missing_dump_names)} decoded components "
                "are missing from dump file"
            )
        core_groups.append(summarize_group(group_id, name, components, dump_components))

    if not args.skip_all:
        all_component_count = sum(len(group) for group in decoded_groups.values())
        all_if3_count = sum(
            1
            for group in decoded_groups.values()
            for component in group.values()
            if component.is_if3
        )
        all_trailing = [
            component.id
            for group in decoded_groups.values()
            for component in group.values()
            if component.trailing_bytes != 0
        ]
    else:
        all_component_count = sum(group["component_count"] for group in core_groups)
        all_if3_count = sum(group["if3_count"] for group in core_groups)
        all_trailing = [
            component["id"]
            for group in core_groups
            for component in group["components"]
            if component["trailing_bytes"] != 0
        ]

    manifest: dict[str, Any] = {
        "cache": str(args.cache),
        "cache_layout": store.layout,
        "dump_root": str(args.dump_root),
        "index": INDEX_INTERFACES,
        "index_protocol": index_manifest.protocol,
        "index_revision": index_manifest.revision,
        "rev237_model_ids": rev237,
        "all_groups_decoded": not args.skip_all,
        "interface_group_count": len(index_manifest.group_ids),
        "decoded_component_count": all_component_count,
        "decoded_if3_count": all_if3_count,
        "decoded_if1_count": all_component_count - all_if3_count,
        "components_with_trailing_bytes": all_trailing,
        "core_groups": core_groups,
        "warnings": warnings,
        "errors": errors,
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, indent=2, sort_keys=True), encoding="utf-8")
    write_debug_report(args.debug_output, core_groups)
    write_binary(args.binary_output, core_groups)

    print(
        f"decoded {all_component_count} interface components "
        f"({all_if3_count} IF3, {all_component_count - all_if3_count} IF1)"
    )
    print(f"wrote {args.output}")
    print(f"wrote {args.debug_output}")
    print(f"wrote {args.binary_output}")
    for warning in warnings:
        print(f"warning: {warning}", file=sys.stderr)
    for error in errors:
        print(f"error: {error}", file=sys.stderr)
    if args.print_debug:
        print(args.debug_output.read_text(encoding="utf-8"))
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
