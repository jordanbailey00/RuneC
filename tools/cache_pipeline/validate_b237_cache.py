#!/usr/bin/env python3
"""Validate an explicit b237 cache against an explicit decoded dump."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

from rc_cache import (
    CONFIG_ITEM,
    CONFIG_NPC,
    CONFIG_OBJECT,
    CONFIG_SEQUENCE,
    CONFIG_SPOTANIM,
    CONFIG_VARBIT,
    CONFIG_VARP,
    INDEX_CONFIGS,
    INDEX_MAPS,
    RcCacheStore,
    decode_item_definition,
    decode_location_definition,
    decode_npc_definition,
    decode_sequence_definition,
    decode_spotanim_definition,
    decode_varbit_definition,
    decode_varp_definition,
    find_all_map_region_files,
    find_map_region_files,
    read_map_region_file,
)
from source_inputs import B237_CACHE, B237_DUMP, require_path

ROOT = Path(__file__).resolve().parents[2]


@dataclass
class DumpRecord:
    record_id: int
    symbol: str = ""
    fields: dict[str, list[str]] = field(default_factory=dict)

    def first(self, key: str) -> str | None:
        values = self.fields.get(key)
        return values[0] if values else None


@dataclass(frozen=True)
class ConfigTarget:
    label: str
    group_id: int
    dump_file: str
    symbol_file: str | None
    samples: tuple[int, ...]
    validator: Callable[["ValidationContext", int, bytes, DumpRecord], None]


@dataclass
class ValidationContext:
    dump_root: Path
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)
    symbols: dict[str, dict[int, str]] = field(default_factory=dict)
    reverse_symbols: dict[str, dict[str, int]] = field(default_factory=dict)

    def require(self, condition: bool, message: str) -> None:
        if not condition:
            self.errors.append(message)

    def warn(self, message: str) -> None:
        self.warnings.append(message)

    def load_symbols(self, name: str) -> dict[int, str]:
        if name not in self.symbols:
            path = self.dump_root / "symbols" / name
            self.symbols[name] = parse_symbol_file(path)
            self.reverse_symbols[name] = {
                symbol: symbol_id for symbol_id, symbol in self.symbols[name].items()
            }
        return self.symbols[name]

    def resolve_symbol(self, symbol_file: str, symbol: str | None) -> int | None:
        if not symbol:
            return None
        self.load_symbols(symbol_file)
        return self.reverse_symbols[symbol_file].get(symbol)


def parse_dump_file(path: Path) -> dict[int, DumpRecord]:
    records: dict[int, DumpRecord] = {}
    current: DumpRecord | None = None
    for raw_line in path.read_text(errors="replace").splitlines():
        line = raw_line.strip()
        if not line:
            continue
        if line.startswith("//"):
            match = re.match(r"//\s*(\d+)\s*$", line)
            if not match:
                continue
            current = DumpRecord(record_id=int(match.group(1)))
            records[current.record_id] = current
            continue
        if current is None:
            continue
        if line.startswith("[") and line.endswith("]"):
            current.symbol = line[1:-1]
            continue
        if "=" in line:
            key, value = line.split("=", 1)
            current.fields.setdefault(key.strip(), []).append(value.strip())
    return records


def parse_symbol_file(path: Path) -> dict[int, str]:
    out: dict[int, str] = {}
    if not path.is_file():
        return out
    for line in path.read_text(errors="replace").splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0].isdigit():
            out[int(parts[0])] = parts[1]
    return out


def parse_int(value: str | None) -> int | None:
    if value is None:
        return None
    match = re.search(r"-?\d+", value)
    return int(match.group(0)) if match else None


def parse_bool(value: str | None) -> bool | None:
    if value is None:
        return None
    normalized = value.lower()
    if normalized in ("yes", "true"):
        return True
    if normalized in ("no", "false"):
        return False
    return None


def parse_model_ref(value: str | None) -> int | None:
    if value is None:
        return None
    match = re.search(r"\bmodel_(\d+)\b", value)
    return int(match.group(1)) if match else None


def require_symbol_match(
    ctx: ValidationContext,
    target: ConfigTarget,
    record_id: int,
    dump: DumpRecord,
) -> None:
    if target.symbol_file is None:
        return
    symbols = ctx.load_symbols(target.symbol_file)
    expected = symbols.get(record_id)
    ctx.require(
        expected == dump.symbol,
        (
            f"{target.label} {record_id}: symbol mismatch "
        f"dump={dump.symbol!r} symbols/{target.symbol_file}={expected!r}"
        ),
    )


def validate_location(
    ctx: ValidationContext,
    record_id: int,
    data: bytes,
    dump: DumpRecord,
) -> None:
    decoded = decode_location_definition(record_id, data)
    ctx.require(decoded.complete, f"loc {record_id}: unknown opcode {decoded.unknown_opcode}")
    if dump.first("name") is not None:
        ctx.require(decoded.name == dump.first("name"), f"loc {record_id}: name mismatch")
    if dump.first("width") is not None:
        ctx.require(decoded.width == parse_int(dump.first("width")), f"loc {record_id}: width mismatch")
    if dump.first("length") is not None:
        ctx.require(decoded.length == parse_int(dump.first("length")), f"loc {record_id}: length mismatch")
    if dump.first("op1") is not None:
        ctx.require(decoded.actions[0] == dump.first("op1"), f"loc {record_id}: op1 mismatch")


def validate_item(
    ctx: ValidationContext,
    record_id: int,
    data: bytes,
    dump: DumpRecord,
) -> None:
    decoded = decode_item_definition(record_id, data)
    ctx.require(decoded.complete, f"obj {record_id}: unknown opcode {decoded.unknown_opcode}")
    if dump.first("name") is not None:
        ctx.require(decoded.name == dump.first("name"), f"obj {record_id}: name mismatch")
    if dump.first("model") is not None:
        ctx.require(
            decoded.inventory_model == parse_model_ref(dump.first("model")),
            f"obj {record_id}: model mismatch",
        )
    if dump.first("cost") is not None:
        ctx.require(decoded.cost == parse_int(dump.first("cost")), f"obj {record_id}: cost mismatch")
    if dump.first("stackable") is not None:
        ctx.require(decoded.stackable == parse_bool(dump.first("stackable")), f"obj {record_id}: stackable mismatch")
    if dump.first("members") is not None:
        ctx.require(decoded.members == parse_bool(dump.first("members")), f"obj {record_id}: members mismatch")


def validate_npc(
    ctx: ValidationContext,
    record_id: int,
    data: bytes,
    dump: DumpRecord,
) -> None:
    decoded = decode_npc_definition(record_id, data)
    ctx.require(decoded.complete, f"npc {record_id}: unknown opcode {decoded.unknown_opcode}")
    if dump.first("name") is not None:
        ctx.require(decoded.name == dump.first("name"), f"npc {record_id}: name mismatch")
    if dump.first("size") is not None:
        ctx.require(decoded.size == parse_int(dump.first("size")), f"npc {record_id}: size mismatch")
    if dump.first("vislevel") is not None:
        ctx.require(
            decoded.combat_level == parse_int(dump.first("vislevel")),
            f"npc {record_id}: combat level mismatch",
        )
    if dump.first("model1") is not None and decoded.models:
        ctx.require(decoded.models[0] == parse_model_ref(dump.first("model1")), f"npc {record_id}: model1 mismatch")


def validate_varbit(
    ctx: ValidationContext,
    record_id: int,
    data: bytes,
    dump: DumpRecord,
) -> None:
    decoded = decode_varbit_definition(record_id, data)
    ctx.require(decoded.complete, f"varbit {record_id}: unknown opcode {decoded.unknown_opcode}")
    varp_symbol = ctx.load_symbols("varp.sym").get(decoded.base_varp)
    ctx.require(varp_symbol == dump.first("basevar"), f"varbit {record_id}: basevar mismatch")
    ctx.require(decoded.lsb == parse_int(dump.first("startbit")), f"varbit {record_id}: startbit mismatch")
    ctx.require(decoded.msb == parse_int(dump.first("endbit")), f"varbit {record_id}: endbit mismatch")


def validate_varp(
    ctx: ValidationContext,
    record_id: int,
    data: bytes,
    dump: DumpRecord,
) -> None:
    decoded = decode_varp_definition(record_id, data)
    ctx.require(decoded.complete, f"varp {record_id}: unknown opcode {decoded.unknown_opcode}")
    if dump.first("type") is not None:
        ctx.require(decoded.varp_type == parse_int(dump.first("type")), f"varp {record_id}: type mismatch")


def validate_spotanim(
    ctx: ValidationContext,
    record_id: int,
    data: bytes,
    dump: DumpRecord,
) -> None:
    decoded = decode_spotanim_definition(record_id, data)
    ctx.require(decoded.complete, f"spot {record_id}: unknown opcode {decoded.unknown_opcode}")
    if dump.first("model") is not None:
        ctx.require(decoded.model_id == parse_model_ref(dump.first("model")), f"spot {record_id}: model mismatch")
    if dump.first("anim") is not None:
        expected = ctx.resolve_symbol("seq.sym", dump.first("anim"))
        ctx.require(decoded.animation_id == expected, f"spot {record_id}: anim mismatch")


def validate_sequence(
    ctx: ValidationContext,
    record_id: int,
    data: bytes,
    dump: DumpRecord,
) -> None:
    decoded = decode_sequence_definition(record_id, data)
    ctx.require(decoded.complete, f"seq {record_id}: unknown opcode {decoded.unknown_opcode}")
    delay_fields = [key for key in dump.fields if key.startswith("delay")]
    if delay_fields:
        ctx.require(
            decoded.frame_count == len(delay_fields),
            f"seq {record_id}: frame count mismatch",
        )
        for idx, delay in enumerate(decoded.frame_delays[: len(delay_fields)], start=1):
            expected = parse_int(dump.first(f"delay{idx}"))
            ctx.require(delay == expected, f"seq {record_id}: delay{idx} mismatch")


TARGETS = (
    ConfigTarget("locations", CONFIG_OBJECT, "dump.loc", "loc.sym", (0, 1, 2, 6), validate_location),
    ConfigTarget("items", CONFIG_ITEM, "dump.obj", "obj.sym", (0, 1, 2, 4), validate_item),
    ConfigTarget("npcs", CONFIG_NPC, "dump.npc", "npc.sym", (0, 1, 2), validate_npc),
    ConfigTarget("sequences", CONFIG_SEQUENCE, "dump.seq", "seq.sym", (0, 1, 2), validate_sequence),
    ConfigTarget("spotanims", CONFIG_SPOTANIM, "dump.spot", None, (0, 1, 9), validate_spotanim),
    ConfigTarget("varbits", CONFIG_VARBIT, "dump.varbit", "varbit.sym", (0, 1, 6, 10), validate_varbit),
    ConfigTarget("varps", CONFIG_VARP, "dump.varp", "varp.sym", (0, 1, 318, 336, 340), validate_varp),
)


def validate_config_groups(store: RcCacheStore, ctx: ValidationContext) -> None:
    manifest = store.read_index_manifest(INDEX_CONFIGS)
    print(
        "config manifest: "
        f"protocol={manifest.protocol} groups={len(manifest.group_ids)} "
        f"named={manifest.has_names}"
    )

    for target in TARGETS:
        files = store.read_group(INDEX_CONFIGS, target.group_id)
        records = parse_dump_file(ctx.dump_root / "config" / target.dump_file)
        symbols = ctx.load_symbols(target.symbol_file) if target.symbol_file else {}
        symbol_count = len(symbols) if target.symbol_file else "n/a"
        print(
            f"{target.label}: cache={len(files)} "
            f"dump={len(records)} symbols={symbol_count} group=2/{target.group_id}"
        )
        ctx.require(len(files) == len(records), f"{target.label}: cache/dump count mismatch")
        if target.symbol_file:
            ctx.require(len(files) == len(symbols), f"{target.label}: cache/symbol count mismatch")

        for record_id in target.samples:
            ctx.require(record_id in files, f"{target.label} {record_id}: missing from cache")
            ctx.require(record_id in records, f"{target.label} {record_id}: missing from dump")
            if record_id not in files or record_id not in records:
                continue
            require_symbol_match(ctx, target, record_id, records[record_id])
            target.validator(ctx, record_id, files[record_id], records[record_id])


def parse_region(value: str) -> tuple[int, int]:
    match = re.match(r"^\s*(\d+)\s*,\s*(\d+)\s*$", value)
    if not match:
        msg = f"invalid region, expected X,Y: {value}"
        raise argparse.ArgumentTypeError(msg)
    return int(match.group(1)), int(match.group(2))


def validate_map_addressing(
    store: RcCacheStore,
    ctx: ValidationContext,
    regions: list[tuple[int, int]],
) -> None:
    manifest = store.read_index_manifest(INDEX_MAPS)
    all_regions = find_all_map_region_files(store)
    print(
        "map manifest: "
        f"protocol={manifest.protocol} groups={len(manifest.group_ids)} "
        f"named={manifest.has_names} resolved_regions={len(all_regions)}"
    )
    ctx.require(len(all_regions) > 0, "map index did not resolve any regions")

    for region_x, region_y in regions:
        entry = find_map_region_files(store, region_x, region_y)
        terrain = read_map_region_file(store, region_x, region_y, "terrain")
        locations = read_map_region_file(store, region_x, region_y, "locations")
        print(
            f"region {region_x},{region_y}: "
            f"mapsquare={entry.mapsquare} "
            f"terrain={entry.terrain_group_id}/{entry.terrain_file_id} "
            f"bytes={len(terrain) if terrain is not None else 0} "
            f"locations={entry.location_group_id}/{entry.location_file_id} "
            f"bytes={len(locations) if locations is not None else 0}"
        )
        ctx.require(entry.has_terrain, f"region {region_x},{region_y}: terrain missing")
        ctx.require(entry.has_locations, f"region {region_x},{region_y}: locations missing")
        ctx.require(bool(terrain), f"region {region_x},{region_y}: terrain file empty")
        ctx.require(bool(locations), f"region {region_x},{region_y}: locations file empty")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=B237_CACHE)
    parser.add_argument("--dump", type=Path, default=B237_DUMP)
    parser.add_argument(
        "--region",
        type=parse_region,
        action="append",
        default=[],
        help="Region to verify as X,Y. May be passed more than once.",
    )
    args = parser.parse_args(argv)
    cache = require_path(args.cache, "--cache", "RUNEC_B237_CACHE")
    dump = require_path(args.dump, "--dump", "RUNEC_B237_DUMP")

    if not cache.is_dir():
        print(f"cache directory not found: {cache}", file=sys.stderr)
        return 2
    if not dump.is_dir():
        print(f"dump directory not found: {dump}", file=sys.stderr)
        return 2

    store = RcCacheStore(cache)
    ctx = ValidationContext(dump)
    validate_config_groups(store, ctx)
    validate_map_addressing(store, ctx, args.region or [(50, 53)])

    for warning in ctx.warnings:
        print(f"warning: {warning}", file=sys.stderr)
    if ctx.errors:
        print("validation failed:", file=sys.stderr)
        for error in ctx.errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print("b237 cache validation: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
