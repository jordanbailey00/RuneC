#!/usr/bin/env python3
"""Validate Phase 3 runtime schema documents."""
from __future__ import annotations

import sys
import tomllib
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
SCHEMA_DIR = ROOT / "schema"

BINARY_SCHEMAS: dict[str, tuple[str, int, tuple[int, ...]]] = {
    "activity_mechanics": ("AMCH", 3, (1, 2, 3)),
    "activity_schemas": ("ASCH", 2, (1, 2)),
    "activity_spawns": ("ASPN", 1, (1,)),
    "activity_states": ("ASTA", 1, (1,)),
    "area_flags": ("AFLG", 1, (1,)),
    "collision_tiles": ("CTIL", 1, (1,)),
    "dialogue": ("DLGX", 1, (1,)),
    "drops": ("DROP", 1, (1,)),
    "encounters": ("ENCT", 12, tuple(range(1, 13))),
    "gathering_nodes": ("GNOD", 1, (1,)),
    "gdt": ("GDT_", 1, (1,)),
    "items": ("IDEF", 2, (1, 2)),
    "mrdt": ("MRDT", 1, (1,)),
    "normalization": ("NORM", 1, (1,)),
    "npc_defs": ("NDEF", 4, (1, 2, 3, 4)),
    "object_behaviors": ("OBHV", 2, (1, 2)),
    "object_defs": ("ODEF", 2, (1, 2)),
    "object_placements": ("OPLC", 2, (1, 2)),
    "object_transports": ("OTRP", 1, (1,)),
    "player_actions": ("PACT", 1, (1,)),
    "prayers": ("PRAY", 1, (1,)),
    "quests": ("QEST", 1, (1,)),
    "rdt": ("RDT_", 1, (1,)),
    "recipes": ("RCIP", 1, (1,)),
    "regular_npc_mechanics": ("RNME", 1, (1,)),
    "shops": ("SHOP", 1, (1,)),
    "skill_drops": ("SDRP", 1, (1,)),
    "slayer": ("SLAY", 3, (1, 2, 3)),
    "spells": ("SPEL", 2, (1, 2)),
    "spotanims": ("SPOT", 1, (1,)),
    "teleports": ("TELE", 2, (1, 2)),
    "traversal_edges": ("TRAV", 1, (1,)),
}

TSV_SCHEMAS = {
    "combat_visuals": {
        "schema_version": 1,
        "required_columns": ("kind", "key", "style", "attack_anim", "authority"),
    },
}

REQUIRED_KEYS = {
    "id",
    "format",
    "path",
    "producer",
    "consumers",
    "source_dataset",
    "compatibility",
}


def display_path(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def load_schema(schema_id: str) -> tuple[Path, dict[str, Any] | None]:
    path = SCHEMA_DIR / "defs" / f"{schema_id}.schema.toml"
    if not path.is_file():
        return path, None
    return path, tomllib.loads(path.read_text())


def require_keys(schema_id: str, doc: dict[str, Any], keys: set[str],
                 failures: list[str]) -> None:
    for key in sorted(keys):
        if key not in doc:
            failures.append(f"{schema_id}: missing key {key}")


def validate_binary(schema_id: str, magic: str, version: int,
                    accepted: tuple[int, ...], failures: list[str]) -> None:
    path, doc = load_schema(schema_id)
    if doc is None:
        failures.append(f"{schema_id}: missing schema {display_path(path)}")
        return
    require_keys(
        schema_id,
        doc,
        REQUIRED_KEYS | {
            "magic_label",
            "magic_u32",
            "current_version",
            "accepted_versions",
            "endianness",
            "header",
            "row_fields",
        },
        failures,
    )
    if doc.get("id") != schema_id:
        failures.append(f"{schema_id}: id mismatch")
    if doc.get("format") != "binary":
        failures.append(f"{schema_id}: format must be binary")
    if doc.get("magic_label") != magic:
        failures.append(f"{schema_id}: magic_label mismatch")
    if doc.get("current_version") != version:
        failures.append(f"{schema_id}: current_version mismatch")
    if tuple(doc.get("accepted_versions", ())) != accepted:
        failures.append(f"{schema_id}: accepted_versions mismatch")
    if doc.get("endianness") != "little":
        failures.append(f"{schema_id}: endianness must be little")
    header = doc.get("header", [])
    if not isinstance(header, list) or not any("magic" in str(v) for v in header):
        failures.append(f"{schema_id}: header must include magic")
    if not isinstance(header, list) or not any("version" in str(v) for v in header):
        failures.append(f"{schema_id}: header must include version")
    rows = doc.get("row_fields", [])
    if not isinstance(rows, list) or not rows:
        failures.append(f"{schema_id}: row_fields must be a non-empty list")


def validate_tsv(schema_id: str, spec: dict[str, Any],
                 failures: list[str]) -> None:
    path, doc = load_schema(schema_id)
    if doc is None:
        failures.append(f"{schema_id}: missing schema {display_path(path)}")
        return
    require_keys(
        schema_id,
        doc,
        REQUIRED_KEYS | {"schema_version", "required_columns"},
        failures,
    )
    if doc.get("id") != schema_id:
        failures.append(f"{schema_id}: id mismatch")
    if doc.get("format") != "tsv":
        failures.append(f"{schema_id}: format must be tsv")
    if doc.get("schema_version") != spec["schema_version"]:
        failures.append(f"{schema_id}: schema_version mismatch")
    columns = doc.get("required_columns", [])
    for col in spec["required_columns"]:
        if col not in columns:
            failures.append(f"{schema_id}: missing required column {col}")


def validate_pack_doc(failures: list[str]) -> None:
    path = SCHEMA_DIR / "packs/runec_pack_v1.md"
    if not path.is_file():
        failures.append("pack format doc missing")
        return
    text = path.read_text()
    for token in ("RCPK0002", "RCPI0001", "PACK_FORMAT_VERSION = 1"):
        if token not in text:
            failures.append(f"pack format doc missing {token}")


def main() -> int:
    failures: list[str] = []
    if not (SCHEMA_DIR / "README.md").is_file():
        failures.append("schema/README.md missing")
    for schema_id, (magic, version, accepted) in sorted(BINARY_SCHEMAS.items()):
        validate_binary(schema_id, magic, version, accepted, failures)
    for schema_id, spec in sorted(TSV_SCHEMAS.items()):
        validate_tsv(schema_id, spec, failures)
    validate_pack_doc(failures)

    if failures:
        print("schema validation failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print(
        f"schema validation passed: {len(BINARY_SCHEMAS)} binary schemas, "
        f"{len(TSV_SCHEMAS)} tsv schema, 1 pack doc"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
