#!/usr/bin/env python3
"""Validate RuneC combat visual profiles against exported asset bundles."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]

DEFAULT_VISUALS = ROOT / "data/defs/combat_visuals.tsv"
DEFAULT_SPOTANIMS = ROOT / "data/defs/spotanims.bin"
DEFAULT_MODEL_BUNDLES = (
    ROOT / "data/models/projectiles.models",
    ROOT / "data/models/items.models",
    ROOT / "data/models/npcs.models",
)
DEFAULT_ANIM_CACHES = (
    ROOT / "data/anims/player.anims",
    ROOT / "data/anims/npcs.anims",
    ROOT / "data/anims/all.anims",
)

SPOTANIM_MAGIC = 0x544F5053
SPOTANIM_VERSION = 1
SPOTANIM_ROW = struct.Struct("<IiiIIIii")
MDL2_MAGIC = 0x4D444C32
MDL3_MAGIC = 0x4D444C33
ANIM_LEGACY_MAGIC = 0x414E494D
ANIM2_MAGIC = 0x324D4E41
ANIM_FORMAT_VERSION = 2
ANIM_HEADER_SIZE_V2 = 24
MODEL_ID_SPOTANIM_BASE = 0xA2000000

BASE_COLUMNS = (
    "kind",
    "key",
    "style",
    "attack_anim",
    "launch_spotanim",
    "travel_spotanim",
    "impact_spotanim",
    "projectile_model",
    "projectile_anim",
    "hit_delay",
    "client_delay",
    "proj_start_height",
    "proj_end_height",
    "proj_delay",
    "proj_angle",
    "proj_length_adjustment",
    "proj_progress",
    "proj_step_multiplier",
    "note",
)
SPOTANIM_COLUMNS = (
    "launch_spotanim",
    "travel_spotanim",
    "impact_spotanim",
    "aux_travel_spotanim",
    "aux_impact_spotanim",
    "double_launch_spotanim",
)
ANIMATION_COLUMNS = (
    "attack_anim",
    "projectile_anim",
    "aux_projectile_anim",
)
MODEL_COLUMNS = (
    "projectile_model",
    "aux_projectile_model",
)
RICH_COLUMNS = (
    "primitive_type",
    "source_attachment",
    "target_attachment",
    "launch_attachment",
    "impact_attachment",
    "authority",
)
PRIMITIVES = {
    "none",
    "launch_effect",
    "travel_projectile",
    "target_impact",
    "fixed_tile_impact",
    "ground_effect",
    "area_effect",
    "multi_projectile",
}
ATTACHMENTS = {
    "none",
    "source_actor",
    "source_center",
    "source_tile",
    "target_actor",
    "target_tile",
    "fixed_tile",
    "ground_tile",
}
KNOWN_KINDS = {"item", "spell", "npc", "special"}
KNOWN_STYLES = {
    "any",
    "melee",
    "stab",
    "slash",
    "crush",
    "ranged",
    "magic",
}


@dataclass(frozen=True)
class SpotAnimDef:
    gfx_id: int
    model_id: int
    animation_id: int


@dataclass
class VisualRow:
    line_no: int
    values: dict[str, str]

    @property
    def kind(self) -> str:
        return self.value("kind")

    @property
    def key(self) -> str:
        return self.value("key")

    @property
    def style(self) -> str:
        return self.value("style")

    @property
    def label(self) -> str:
        return f"{self.kind}:{self.key}:{self.style}"

    @property
    def note(self) -> str:
        return self.value("note")

    @property
    def authority(self) -> str:
        return self.value("authority")

    @property
    def primitive(self) -> str:
        explicit = self.value("primitive_type")
        if explicit != "-":
            return explicit
        return infer_primitive_type(self)

    def value(self, name: str) -> str:
        value = self.values.get(name, "-").strip()
        return value if value else "-"

    def int_value(self, name: str) -> int:
        return parse_int(self.value(name))


@dataclass(frozen=True)
class Issue:
    severity: str
    line_no: int
    label: str
    message: str


@dataclass(frozen=True)
class RowSelector:
    kind: str
    key: str
    style: str | None = None

    def matches(self, row: VisualRow) -> bool:
        if self.kind != row.kind or self.key != row.key:
            return False
        return self.style is None or self.style == row.style

    @property
    def label(self) -> str:
        if self.style is None:
            return f"{self.kind}:{self.key}"
        return f"{self.kind}:{self.key}:{self.style}"


class BinaryReader:
    def __init__(self, data: bytes, path: Path):
        self.data = data
        self.path = path
        self.pos = 0

    def need(self, count: int) -> None:
        if self.pos + count > len(self.data):
            raise ValueError(f"truncated {self.path}")

    def read_u8(self) -> int:
        self.need(1)
        value = self.data[self.pos]
        self.pos += 1
        return value

    def read_i8(self) -> int:
        value = self.read_u8()
        return value - 256 if value > 127 else value

    def read_u16(self) -> int:
        self.need(2)
        value = struct.unpack_from("<H", self.data, self.pos)[0]
        self.pos += 2
        return value

    def read_u32(self) -> int:
        self.need(4)
        value = struct.unpack_from("<I", self.data, self.pos)[0]
        self.pos += 4
        return value

    def skip(self, count: int) -> None:
        self.need(count)
        self.pos += count


def parse_int(value: str) -> int:
    if not value or value == "-":
        return -1
    try:
        return int(value, 0)
    except ValueError:
        return -1


def row_has_travel_asset(row: VisualRow) -> bool:
    return (
        row.int_value("travel_spotanim") >= 0
        or row.int_value("projectile_model") >= 0
        or row.int_value("projectile_anim") >= 0
    )


def row_has_aux_projectile(row: VisualRow) -> bool:
    return (
        row.int_value("aux_travel_spotanim") >= 0
        or row.int_value("aux_projectile_model") >= 0
        or row.int_value("aux_projectile_anim") >= 0
    )


def infer_primitive_type(row: VisualRow) -> str:
    if row.int_value("projectile_count") > 1 or row_has_aux_projectile(row):
        return "multi_projectile"
    if (
        not row_has_travel_asset(row)
        and row.int_value("impact_spotanim") >= 0
        and row.int_value("proj_start_height") >= 256
        and row.int_value("proj_end_height") >= 0
    ):
        return "fixed_tile_impact"
    if row_has_travel_asset(row):
        return "travel_projectile"
    if row.int_value("impact_spotanim") >= 0:
        return "target_impact"
    if (
        row.int_value("launch_spotanim") >= 0
        or row.int_value("double_launch_spotanim") >= 0
    ):
        return "launch_effect"
    return "none"


def read_visual_rows(path: Path) -> tuple[list[str], list[VisualRow]]:
    if not path.exists():
        raise FileNotFoundError(path)
    header: list[str] | None = None
    rows: list[VisualRow] = []
    for line_no, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = [part.strip() for part in line.split("|")]
        if header is None:
            header = parts
            continue
        values = {
            name: parts[idx] if idx < len(parts) and parts[idx] else "-"
            for idx, name in enumerate(header)
        }
        rows.append(VisualRow(line_no=line_no, values=values))
    if header is None:
        raise ValueError(f"{path} has no header")
    return header, rows


def read_spotanims(path: Path) -> dict[int, SpotAnimDef]:
    data = path.read_bytes()
    if len(data) < 12:
        raise ValueError(f"spotanim file too small: {path}")
    magic, version, count = struct.unpack_from("<III", data, 0)
    if magic != SPOTANIM_MAGIC or version != SPOTANIM_VERSION:
        raise ValueError(f"bad spotanim header in {path}")
    pos = 12
    out: dict[int, SpotAnimDef] = {}
    for _ in range(count):
        if pos + SPOTANIM_ROW.size > len(data):
            raise ValueError(f"truncated spotanim row in {path}")
        gfx_id, model_id, animation_id, *_ = SPOTANIM_ROW.unpack_from(data, pos)
        out[int(gfx_id)] = SpotAnimDef(int(gfx_id), int(model_id), int(animation_id))
        pos += SPOTANIM_ROW.size
    return out


def read_model_ids(path: Path) -> set[int]:
    out: set[int] = set()
    with path.open("rb") as f:
        header = f.read(8)
        if len(header) != 8:
            raise ValueError(f"model file too small: {path}")
        magic, count = struct.unpack("<II", header)
        if magic not in {MDL2_MAGIC, MDL3_MAGIC}:
            raise ValueError(f"bad model magic in {path}")
        offsets_raw = f.read(count * 4)
        if len(offsets_raw) != count * 4:
            raise ValueError(f"truncated model offsets in {path}")
        offsets = struct.unpack(f"<{count}I", offsets_raw) if count else ()
        for offset in offsets:
            f.seek(offset)
            model_id_raw = f.read(4)
            if len(model_id_raw) != 4:
                raise ValueError(f"truncated model id in {path}")
            out.add(struct.unpack("<I", model_id_raw)[0])
    return out


def read_anim_sequence_ids(path: Path) -> set[int]:
    reader = BinaryReader(path.read_bytes(), path)
    magic = reader.read_u32()
    if magic == ANIM2_MAGIC:
        version = reader.read_u16()
        header_size = reader.read_u16()
        if version != ANIM_FORMAT_VERSION or header_size < ANIM_HEADER_SIZE_V2:
            raise ValueError(f"unsupported ANM2 header in {path}")
        base_count = reader.read_u32()
        seq_count = reader.read_u32()
        declared_frames = reader.read_u32()
        reader.read_u32()
        if header_size > ANIM_HEADER_SIZE_V2:
            reader.skip(header_size - ANIM_HEADER_SIZE_V2)
    elif magic == ANIM_LEGACY_MAGIC:
        base_count = reader.read_u16()
        seq_count = reader.read_u16()
        declared_frames = -1
    else:
        raise ValueError(f"bad animation magic in {path}")

    for _ in range(base_count):
        reader.read_u16()
        slot_count = reader.read_u8()
        reader.skip(slot_count)
        for _slot in range(slot_count):
            map_len = reader.read_u8()
            reader.skip(map_len)

    out: set[int] = set()
    sequence_frames = 0
    for _ in range(seq_count):
        seq_id = reader.read_u16()
        out.add(seq_id)
        frame_count = reader.read_u16()
        interleave_count = reader.read_u8()
        reader.skip(interleave_count)
        reader.read_i8()
        for _frame in range(frame_count):
            reader.read_u16()
            reader.read_u16()
            transform_count = reader.read_u8()
            reader.skip(transform_count * 7)
            sequence_frames += 1
    if declared_frames >= 0 and declared_frames != sequence_frames:
        raise ValueError(
            f"animation frame count mismatch in {path}: "
            f"declared={declared_frames} read={sequence_frames}"
        )
    return out


def load_many(paths: list[Path], loader, label: str):
    out = None
    loaded = 0
    for path in paths:
        if not path.exists():
            raise FileNotFoundError(path)
        value = loader(path)
        loaded += 1
        if isinstance(value, dict):
            if out is None:
                out = {}
            out.update(value)
        else:
            if out is None:
                out = set()
            out.update(value)
    if out is None:
        raise ValueError(f"no {label} loaded")
    return out, loaded


def parse_selector(raw: str) -> RowSelector:
    parts = raw.split(":")
    if len(parts) not in {2, 3}:
        raise argparse.ArgumentTypeError(
            "row selectors must look like kind:key or kind:key:style"
        )
    kind, key = parts[0], parts[1]
    style = parts[2] if len(parts) == 3 else None
    if kind not in KNOWN_KINDS:
        raise argparse.ArgumentTypeError(f"unknown selector kind: {kind}")
    return RowSelector(kind, key, style)


def selected_rows(rows: list[VisualRow],
                  selectors: list[RowSelector]) -> list[VisualRow]:
    if not selectors:
        return rows
    return [row for row in rows if any(selector.matches(row) for selector in selectors)]


def is_critical(row: VisualRow, authorities: set[str],
                selectors: list[RowSelector]) -> bool:
    return row.authority in authorities or any(selector.matches(row)
                                               for selector in selectors)


def issue_severity(row: VisualRow, mode: str, critical: bool) -> str:
    if mode == "strict" or critical:
        return "error"
    return "warning"


def add_issue(issues: list[Issue], severity: str, row: VisualRow,
              message: str) -> None:
    issues.append(Issue(severity, row.line_no, row.label, message))


def require_int_field(issues: list[Issue], row: VisualRow, name: str,
                      mode: str, critical: bool) -> None:
    if row.int_value(name) < 0:
        add_issue(
            issues,
            issue_severity(row, mode, critical),
            row,
            f"{row.primitive} requires {name}",
        )


def validate_schema(header: list[str], rows: list[VisualRow], mode: str,
                    issues: list[Issue]) -> None:
    missing_base = [name for name in BASE_COLUMNS if name not in header]
    if missing_base:
        dummy = VisualRow(0, {"kind": "file", "key": "-", "style": "-"})
        add_issue(
            issues,
            "error",
            dummy,
            "missing required columns: " + ", ".join(missing_base),
        )
    missing_rich = [name for name in RICH_COLUMNS if name not in header]
    if missing_rich:
        dummy = VisualRow(0, {"kind": "file", "key": "-", "style": "-"})
        add_issue(
            issues,
            "error" if mode == "strict" else "warning",
            dummy,
            "missing rich profile columns: " + ", ".join(missing_rich),
        )

    for row in rows:
        if row.kind not in KNOWN_KINDS:
            add_issue(issues, "error", row, f"unknown kind {row.kind!r}")
        if row.style not in KNOWN_STYLES and parse_int(row.style) < 0:
            add_issue(issues, "error", row, f"unknown style {row.style!r}")
        primitive = row.primitive
        if primitive not in PRIMITIVES:
            add_issue(issues, "error", row, f"unknown primitive {primitive!r}")
        for name in (
            "source_attachment",
            "target_attachment",
            "launch_attachment",
            "impact_attachment",
        ):
            value = row.value(name)
            if value != "-" and value not in ATTACHMENTS:
                add_issue(issues, "error", row, f"unknown {name} {value!r}")


def validate_primitive_requirements(
    rows: list[VisualRow],
    mode: str,
    critical_authorities: set[str],
    critical_selectors: list[RowSelector],
    issues: list[Issue],
) -> None:
    for row in rows:
        primitive = row.primitive
        critical = is_critical(row, critical_authorities, critical_selectors)
        if critical and row.authority == "-":
            add_issue(issues, "error", row, "critical row is missing authority")

        if primitive == "launch_effect":
            if row.int_value("launch_spotanim") < 0 and row.int_value(
                "double_launch_spotanim"
            ) < 0:
                add_issue(
                    issues,
                    issue_severity(row, mode, critical),
                    row,
                    "launch_effect requires launch_spotanim or double_launch_spotanim",
                )
        elif primitive == "travel_projectile":
            if not row_has_travel_asset(row):
                add_issue(
                    issues,
                    issue_severity(row, mode, critical),
                    row,
                    "travel_projectile requires travel_spotanim, projectile_model, "
                    "or projectile_anim",
                )
            for name in ("hit_delay", "client_delay"):
                require_int_field(issues, row, name, mode, critical)
            for name in (
                "proj_start_height",
                "proj_end_height",
                "proj_delay",
                "proj_angle",
                "proj_progress",
                "proj_step_multiplier",
            ):
                if row.int_value(name) < 0:
                    add_issue(
                        issues,
                        issue_severity(row, mode, critical),
                        row,
                        f"travel_projectile has incomplete timing field {name}",
                    )
        elif primitive == "target_impact":
            require_int_field(issues, row, "impact_spotanim", mode, critical)
        elif primitive == "fixed_tile_impact":
            require_int_field(issues, row, "impact_spotanim", mode, critical)
            for name in (
                "hit_delay",
                "client_delay",
                "proj_start_height",
                "proj_end_height",
            ):
                require_int_field(issues, row, name, mode, critical)
            if row.value("impact_attachment") not in {"fixed_tile", "-"}:
                add_issue(
                    issues,
                    issue_severity(row, mode, critical),
                    row,
                    "fixed_tile_impact should use impact_attachment=fixed_tile",
                )
        elif primitive in {"ground_effect", "area_effect"}:
            if row.int_value("impact_spotanim") < 0 and row.int_value(
                "projectile_model"
            ) < 0:
                add_issue(
                    issues,
                    issue_severity(row, mode, critical),
                    row,
                    f"{primitive} requires impact_spotanim or projectile_model",
                )
        elif primitive == "multi_projectile":
            if row.int_value("projectile_count") <= 1 and not row_has_aux_projectile(row):
                add_issue(
                    issues,
                    issue_severity(row, mode, critical),
                    row,
                    "multi_projectile requires projectile_count > 1 or aux projectile fields",
                )
            if not row_has_travel_asset(row) and not row_has_aux_projectile(row):
                add_issue(
                    issues,
                    issue_severity(row, mode, critical),
                    row,
                    "multi_projectile requires a primary or auxiliary travel asset",
                )


def validate_references(
    rows: list[VisualRow],
    spotanims: dict[int, SpotAnimDef],
    model_ids: set[int],
    anim_ids: set[int],
    mode: str,
    critical_authorities: set[str],
    critical_selectors: list[RowSelector],
    issues: list[Issue],
) -> dict[str, tuple[int, int]]:
    checks = {
        "animation_refs": [0, 0],
        "spotanim_refs": [0, 0],
        "model_refs": [0, 0],
        "spotanim_model_refs": [0, 0],
        "spotanim_animation_refs": [0, 0],
    }
    seen_spotanim_models: set[tuple[int, int]] = set()
    seen_spotanim_anims: set[tuple[int, int]] = set()

    for row in rows:
        critical = is_critical(row, critical_authorities, critical_selectors)
        severity = issue_severity(row, mode, critical)
        for name in ANIMATION_COLUMNS:
            anim_id = row.int_value(name)
            if anim_id < 0:
                continue
            checks["animation_refs"][1] += 1
            if anim_id in anim_ids:
                checks["animation_refs"][0] += 1
            else:
                add_issue(issues, severity, row, f"{name} {anim_id} missing from animation bundles")

        for name in MODEL_COLUMNS:
            model_id = row.int_value(name)
            if model_id < 0:
                continue
            checks["model_refs"][1] += 1
            if model_id in model_ids:
                checks["model_refs"][0] += 1
            else:
                add_issue(issues, severity, row, f"{name} {model_id} missing from model bundles")

        for name in SPOTANIM_COLUMNS:
            spotanim_id = row.int_value(name)
            if spotanim_id < 0:
                continue
            checks["spotanim_refs"][1] += 1
            spot = spotanims.get(spotanim_id)
            if spot is None:
                add_issue(issues, severity, row, f"{name} {spotanim_id} missing from spotanims.bin")
                continue
            checks["spotanim_refs"][0] += 1

            if spot.model_id >= 0 and (spotanim_id, spot.model_id) not in seen_spotanim_models:
                seen_spotanim_models.add((spotanim_id, spot.model_id))
                checks["spotanim_model_refs"][1] += 1
                synthetic_id = MODEL_ID_SPOTANIM_BASE + spotanim_id
                if spot.model_id in model_ids or synthetic_id in model_ids:
                    checks["spotanim_model_refs"][0] += 1
                else:
                    add_issue(
                        issues,
                        severity,
                        row,
                        f"{name} {spotanim_id} model {spot.model_id} missing "
                        f"from model bundles",
                    )

            if spot.animation_id >= 0 and (
                spotanim_id,
                spot.animation_id,
            ) not in seen_spotanim_anims:
                seen_spotanim_anims.add((spotanim_id, spot.animation_id))
                checks["spotanim_animation_refs"][1] += 1
                if spot.animation_id in anim_ids:
                    checks["spotanim_animation_refs"][0] += 1
                else:
                    add_issue(
                        issues,
                        severity,
                        row,
                        f"{name} {spotanim_id} animation {spot.animation_id} "
                        "missing from animation bundles",
                    )

    return {name: (ok, total) for name, (ok, total) in checks.items()}


def apply_preset(args: argparse.Namespace) -> None:
    if args.preset != "fight_caves_jad":
        return
    jad_rows = [
        RowSelector("npc", "3127", "magic"),
        RowSelector("npc", "3127", "ranged"),
        RowSelector("npc", "3127", "melee"),
    ]
    args.only_row.extend([RowSelector("npc", "3127", None)])
    args.require_row.extend(jad_rows)
    args.critical_row.extend(jad_rows)


def print_issues(issues: list[Issue], max_issues: int) -> None:
    for issue in issues[:max_issues]:
        line = f":{issue.line_no}" if issue.line_no else ""
        print(f"{issue.severity}: {issue.label}{line}: {issue.message}")
    remaining = len(issues) - max_issues
    if remaining > 0:
        print(f"... {remaining} more issues hidden; increase --max-issues to show them")


def pct(ok: int, total: int) -> str:
    if total <= 0:
        return "n/a"
    return f"{(ok * 100.0 / total):.1f}%"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="validate combat_visuals.tsv references and primitive metadata"
    )
    parser.add_argument("--visuals", type=Path, default=DEFAULT_VISUALS)
    parser.add_argument("--spotanims", type=Path, default=DEFAULT_SPOTANIMS)
    parser.add_argument(
        "--model-bundle",
        type=Path,
        action="append",
        default=[],
        help="MDL2/MDL3 model bundle to validate against; defaults to viewer bundles",
    )
    parser.add_argument(
        "--animation-cache",
        type=Path,
        action="append",
        default=[],
        help="ANM1/ANM2 cache to validate against; defaults to viewer animation bundles",
    )
    parser.add_argument("--mode", choices=("broad", "strict"), default="broad")
    parser.add_argument(
        "--preset",
        choices=("none", "fight_caves_jad"),
        default="none",
        help="apply a scoped validation preset",
    )
    parser.add_argument(
        "--only-row",
        type=parse_selector,
        action="append",
        default=[],
        help="validate only rows matching kind:key or kind:key:style",
    )
    parser.add_argument(
        "--require-row",
        type=parse_selector,
        action="append",
        default=[],
        help="fail if the selected profile row is absent",
    )
    parser.add_argument(
        "--critical-row",
        type=parse_selector,
        action="append",
        default=[],
        help="treat matching rows as hard failures even in broad mode",
    )
    parser.add_argument(
        "--critical-authority",
        action="append",
        default=["curated"],
        help="authority value that should fail hard in broad mode",
    )
    parser.add_argument("--max-issues", type=int, default=80)
    args = parser.parse_args()
    apply_preset(args)

    model_paths = args.model_bundle or list(DEFAULT_MODEL_BUNDLES)
    anim_paths = args.animation_cache or list(DEFAULT_ANIM_CACHES)
    critical_authorities = set(args.critical_authority)

    try:
        header, rows = read_visual_rows(args.visuals)
        rows_to_validate = selected_rows(rows, args.only_row)
        missing_required = [
            selector for selector in args.require_row
            if not any(selector.matches(row) for row in rows)
        ]
        spotanims = read_spotanims(args.spotanims)
        model_ids, model_bundle_count = load_many(model_paths, read_model_ids, "model bundles")
        anim_ids, anim_cache_count = load_many(anim_paths, read_anim_sequence_ids, "animation caches")
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    issues: list[Issue] = []
    for selector in missing_required:
        add_issue(
            issues,
            "error",
            VisualRow(0, {"kind": "profile", "key": selector.label, "style": "-"}),
            f"required row {selector.label} is missing",
        )

    validate_schema(header, rows_to_validate, args.mode, issues)
    validate_primitive_requirements(
        rows_to_validate,
        args.mode,
        critical_authorities,
        args.critical_row,
        issues,
    )
    coverage = validate_references(
        rows_to_validate,
        spotanims,
        model_ids,
        anim_ids,
        args.mode,
        critical_authorities,
        args.critical_row,
        issues,
    )

    error_count = sum(1 for issue in issues if issue.severity == "error")
    warning_count = sum(1 for issue in issues if issue.severity == "warning")
    critical_count = sum(
        1 for row in rows_to_validate
        if is_critical(row, critical_authorities, args.critical_row)
    )

    print(
        f"combat visual validation: rows={len(rows_to_validate)}/{len(rows)} "
        f"critical={critical_count} mode={args.mode}"
    )
    print(
        f"assets: spotanims={len(spotanims)} model_ids={len(model_ids)} "
        f"model_bundles={model_bundle_count} anim_ids={len(anim_ids)} "
        f"anim_caches={anim_cache_count}"
    )
    for name, (ok, total) in coverage.items():
        print(f"coverage: {name} {ok}/{total} ({pct(ok, total)})")
    print(f"issues: errors={error_count} warnings={warning_count}")

    issues.sort(key=lambda issue: (issue.severity != "error", issue.line_no, issue.message))
    print_issues(issues, max(0, args.max_issues))
    return 1 if error_count else 0


if __name__ == "__main__":
    raise SystemExit(main())
