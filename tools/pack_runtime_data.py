#!/usr/bin/env python3
"""Build RuneC runtime data packs.

This is a data-factory tool: it reads the loose generated runtime data under
data/ and writes binary .pak files plus dist-data/manifest.json. It does not
read or package source corpora, exporter caches, reports, or local planning
docs.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
import struct
import tomllib
from typing import Iterable
import zlib

from collision_tile_index import (
    iter_collision_tiles,
    read_header as read_collision_tile_header,
)
from object_placement_index import (
    iter_object_placements,
    read_header as read_object_placement_header,
)
from spawn_index import (
    GROUND_ITEM_INDEX_MAGIC,
    NPC_SPAWN_INDEX_MAGIC,
    read_ground_item_spawns,
    read_indexed_header,
    read_npc_spawns,
)


PACK_MAGIC = b"RCPK0002"
INDEX_MAGIC = b"RCPI0001"
PACK_HEADER = struct.Struct("<8sQQQ32s")
PACK_FORMAT_VERSION = 1
DEFAULT_MAX_PACK_BYTES = 512 * 1024 * 1024
IO_CHUNK_BYTES = 1024 * 1024
INDEX_COUNT = struct.Struct("<8sQ")
INDEX_PATH_LEN = struct.Struct("<H")
INDEX_ENTRY_FIXED = struct.Struct("<BQQQ32s")
MAPSQUARE_CATALOG_HEADER = struct.Struct("<II")
MAPSQUARE_CATALOG_MAGIC = 0x3151534D
MAPSQUARE_OANIM_HEADER = struct.Struct("<III")
MAPSQUARE_OANIM_MAGIC = 0x4D4E414F
MAPSQUARE_OANIM_VERSION = 1
# b241 map index (b237 had 2870) plus the mapsquares composed from it by
# content/world/composed_maps.toml.
CACHE_MAPSQUARE_REGIONS = 2950
COMPOSED_MAPSQUARE_REGIONS = 4
EXPECTED_MAPSQUARE_REGIONS = CACHE_MAPSQUARE_REGIONS + COMPOSED_MAPSQUARE_REGIONS
MAPSQUARE_PLANES = range(4)
MAPSQUARE_SHARED_FILES = (
    "mapsquare.catalog",
    "mapsquare.materials.atlas",
    "mapsquare.materials.tanim",
)
OBJECT_PLACEMENT_INDEX_FILE = "world.object-placements.indexed.bin"
COLLISION_TILE_INDEX_FILE = "world.collision-tiles.indexed.bin"
MAPSQUARE_ASSET_RE = re.compile(
    r"^(?P<x>\d{1,3})_(?P<y>\d{1,3})\.p(?P<plane>[0-3])\."
    r"(?P<kind>terrain|objects|oanim|object_anim\.models|minimap\.png)$"
)

STORE_EXTENSIONS = {
    ".jpg",
    ".jpeg",
    ".mp3",
    ".ogg",
    ".flac",
    ".png",
}
STORE_SUFFIXES = (".indexed.bin",)

HITSPLAT_DEFINITION_IDS = (
    0, 1, 3, 4, 5, 6, 7,
    26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42,
    48, 49, 50, 51, 52, 56, 57, 58, 62, 63, 64, 67, 68, 69, 70, 71, 72,
    73, 74, 75, 76, 78, 79, 80, 81,
)
HITSPLAT_LOGICAL_PATHS = tuple(
    f"sprites/ui/hitsplat_{definition_id}.png"
    for definition_id in HITSPLAT_DEFINITION_IDS
)

REQUIRED_LOGICAL_PATHS = (
    "defs/hitsplats.bin",
    *HITSPLAT_LOGICAL_PATHS,
    "defs/npc_defs.bin",
    "spawns/world.npc-spawns.indexed.bin",
    "spawns/world.ground-items.indexed.bin",
    "defs/varbits.bin",
    "defs/varps.bin",
    "defs/items.bin",
    "defs/normalization.bin",
    "defs/drops.bin",
    "defs/skill_drops.bin",
    "defs/rdt.bin",
    "defs/gdt.bin",
    "defs/mrdt.bin",
    "defs/recipes.bin",
    "defs/gathering_nodes.bin",
    "defs/quests.bin",
    "defs/dialogue.bin",
    "defs/shops.bin",
    "defs/slayer.bin",
    "defs/prayers.bin",
    "defs/spells.bin",
    "defs/combat_visuals.tsv",
    "defs/npc_attack_anims.tsv",
    "defs/player_actions.bin",
    "defs/regular_npc_mechanics.bin",
    "defs/activity_schemas.bin",
    "defs/activity_spawns.bin",
    "defs/activity_mechanics.bin",
    "defs/activity_states.bin",
    "defs/object_defs.bin",
    "regions/world.object-placements.indexed.bin",
    "defs/object_behaviors.bin",
    "regions/world.collision-tiles.indexed.bin",
    "defs/area_flags.bin",
    "defs/traversal_edges.bin",
    "defs/encounters.bin",
    "regions/mapsquare.catalog",
    "regions/mapsquare.materials.atlas",
    "regions/mapsquare.materials.tanim",
)

PROVISIONAL_REQUIRED_LOGICAL_PATHS = (
    {
        "path": "defs/area_flags.bin",
        "dataset": "area_flags",
        "reason": (
            "Runtime currently requires area flags for region semantics, but "
            "multicombat, wilderness, safe-zone, and singles-plus authority "
            "is still tracked as a source gap."
        ),
        "release_gate": "not_publishable_source_gaps",
    },
)

OPTIONAL_LOGICAL_PATHS = (
    "audio/",
    "maps/",
    "music/",
)


@dataclass(frozen=True)
class PackSpec:
    stem: str
    files: tuple[Path, ...]
    numbered: bool = False
    chunkable: bool = True


@dataclass(frozen=True)
class PackChunk:
    spec: PackSpec
    index: int
    files: tuple[Path, ...]
    uncompressed_size: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Package generated RuneC runtime data into .pak files."
    )
    parser.add_argument(
        "--data-root",
        default="data",
        help="Loose runtime data root. Defaults to ./data.",
    )
    parser.add_argument(
        "--output",
        default="dist-data",
        help="Output directory. Defaults to ./dist-data.",
    )
    parser.add_argument(
        "--version",
        default="v1",
        help="Data version slug used in pack names and manifest. Example: v1.",
    )
    parser.add_argument(
        "--max-pack-bytes",
        type=parse_size,
        default=DEFAULT_MAX_PACK_BYTES,
        help="Target max uncompressed bytes per numbered pack. Default: 512MiB.",
    )
    parser.add_argument(
        "--compression-level",
        type=int,
        default=6,
        help="zlib compression level for non-store assets, 0-9. Default: 6.",
    )
    parser.add_argument(
        "--store-extensions",
        default=",".join(sorted(STORE_EXTENSIONS)),
        help="Comma-separated extensions to store without zlib compression.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print the pack plan without writing packs.",
    )
    parser.add_argument(
        "--list-assets",
        action="store_true",
        help="In dry-run mode, print every asset assigned to each pack.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite existing output files.",
    )
    parser.add_argument(
        "--allow-partial-mapsquares",
        action="store_true",
        help=(
            "Allow a development pack whose mapsquare files cover only part "
            "of mapsquare.catalog. Production runtime-data packs must omit "
            "this flag."
        ),
    )
    return parser.parse_args()


def parse_size(value: str) -> int:
    text = value.strip().lower()
    multipliers = {
        "k": 1024,
        "kb": 1024,
        "kib": 1024,
        "m": 1024**2,
        "mb": 1024**2,
        "mib": 1024**2,
        "g": 1024**3,
        "gb": 1024**3,
        "gib": 1024**3,
    }
    for suffix, multiplier in sorted(multipliers.items(), key=lambda item: -len(item[0])):
        if text.endswith(suffix):
            return int(float(text[: -len(suffix)]) * multiplier)
    return int(text)


def version_slug(raw: str) -> str:
    slug = raw.strip()
    if not slug:
        raise SystemExit("--version cannot be empty")
    return slug if slug.startswith("v") else f"v{slug}"


def iter_files(root: Path) -> Iterable[Path]:
    if not root.exists():
        return ()
    return (p for p in root.rglob("*") if p.is_file())


def sorted_files(paths: Iterable[Path], data_root: Path) -> tuple[Path, ...]:
    return tuple(sorted(paths, key=lambda p: logical_path(data_root, p)))


def logical_path(data_root: Path, path: Path) -> str:
    return path.relative_to(data_root).as_posix()


def portable_path(path: Path) -> str:
    resolved = path.resolve()
    try:
        return resolved.relative_to(Path.cwd().resolve()).as_posix()
    except ValueError:
        return path.as_posix()


def add_unique(
    assigned: dict[str, str],
    spec_name: str,
    paths: Iterable[Path],
    data_root: Path,
) -> list[Path]:
    out: list[Path] = []
    for path in sorted_files((p for p in paths if p.is_file()), data_root):
        key = logical_path(data_root, path)
        previous = assigned.get(key)
        if previous:
            raise SystemExit(
                f"asset assigned twice: {key} in {previous} and {spec_name}"
            )
        assigned[key] = spec_name
        out.append(path)
    return out


def is_region_runtime_file(path: Path) -> bool:
    if "scene_cache" in path.parts or "varrock_chunks" in path.parts:
        return False
    name = path.name
    return (
        name in MAPSQUARE_SHARED_FILES
        or name == OBJECT_PLACEMENT_INDEX_FILE
        or name == COLLISION_TILE_INDEX_FILE
        or MAPSQUARE_ASSET_RE.fullmatch(name) is not None
    )


def load_mapsquare_catalog(path: Path) -> tuple[tuple[int, int], ...]:
    try:
        payload = path.read_bytes()
    except FileNotFoundError as exc:
        raise SystemExit(f"missing mapsquare catalog: {path}") from exc
    if len(payload) < MAPSQUARE_CATALOG_HEADER.size:
        raise SystemExit(f"truncated mapsquare catalog: {path}")
    magic, count = MAPSQUARE_CATALOG_HEADER.unpack_from(payload)
    expected_size = MAPSQUARE_CATALOG_HEADER.size + count * 2
    if magic != MAPSQUARE_CATALOG_MAGIC or len(payload) != expected_size:
        raise SystemExit(f"invalid mapsquare catalog: {path}")
    regions = tuple(
        (payload[offset], payload[offset + 1])
        for offset in range(MAPSQUARE_CATALOG_HEADER.size, len(payload), 2)
    )
    ordered = tuple(
        sorted(set(regions), key=lambda region: (region[1], region[0]))
    )
    if regions != ordered:
        raise SystemExit(f"mapsquare catalog is not sorted and unique: {path}")
    return regions


def mapsquare_oanim_count(path: Path) -> int:
    try:
        with path.open("rb") as handle:
            header = handle.read(MAPSQUARE_OANIM_HEADER.size)
    except OSError as exc:
        raise SystemExit(f"cannot read mapsquare animation sidecar: {path}") from exc
    if len(header) != MAPSQUARE_OANIM_HEADER.size:
        raise SystemExit(f"truncated mapsquare animation sidecar: {path}")
    magic, version, count = MAPSQUARE_OANIM_HEADER.unpack(header)
    if magic != MAPSQUARE_OANIM_MAGIC or version != MAPSQUARE_OANIM_VERSION:
        raise SystemExit(f"invalid mapsquare animation sidecar: {path}")
    return count


def validate_mapsquare_runtime_assets(
    data_root: Path,
    *,
    allow_partial: bool,
    expected_catalog_regions: int | None = None,
) -> dict[str, int | bool | str]:
    regions_root = data_root / "regions"
    missing_shared = [
        name for name in MAPSQUARE_SHARED_FILES
        if not (regions_root / name).is_file()
    ]
    if missing_shared:
        raise SystemExit(
            "missing mapsquare runtime assets: "
            + ", ".join(f"regions/{name}" for name in missing_shared)
        )

    regions = load_mapsquare_catalog(regions_root / "mapsquare.catalog")
    catalog_complete = (
        expected_catalog_regions is None
        or len(regions) == expected_catalog_regions
    )
    if not catalog_complete and not allow_partial:
        raise SystemExit(
            "incomplete mapsquare catalog: "
            f"found {len(regions)}, expected {expected_catalog_regions}"
        )
    catalog_set = set(regions)
    for path in iter_files(regions_root):
        match = MAPSQUARE_ASSET_RE.fullmatch(path.name)
        if not match:
            continue
        coordinate = (int(match.group("x")), int(match.group("y")))
        if coordinate not in catalog_set:
            raise SystemExit(
                f"mapsquare asset is absent from the catalog: {path}"
            )

    missing_count = 0
    missing_sample: list[str] = []
    complete_region_planes = 0
    animated_model_assets = 0
    for region_x, region_y in regions:
        for plane in MAPSQUARE_PLANES:
            stem = f"{region_x}_{region_y}.p{plane}"
            plane_missing = False
            for suffix in ("terrain", "objects", "oanim", "minimap.png"):
                path = regions_root / f"{stem}.{suffix}"
                if path.is_file():
                    continue
                plane_missing = True
                missing_count += 1
                if len(missing_sample) < 8:
                    missing_sample.append(f"regions/{path.name}")

            oanim_path = regions_root / f"{stem}.oanim"
            if oanim_path.is_file() and mapsquare_oanim_count(oanim_path) > 0:
                models_path = regions_root / f"{stem}.object_anim.models"
                if models_path.is_file():
                    animated_model_assets += 1
                else:
                    plane_missing = True
                    missing_count += 1
                    if len(missing_sample) < 8:
                        missing_sample.append(f"regions/{models_path.name}")
            if not plane_missing:
                complete_region_planes += 1

    complete = catalog_complete and missing_count == 0
    if not complete and not allow_partial:
        sample = ", ".join(missing_sample)
        raise SystemExit(
            "incomplete prebuilt mapsquare runtime data: "
            f"{missing_count} required assets missing across "
            f"{len(regions)} catalog regions; first missing: {sample}. "
            "Run the full region export or use --allow-partial-mapsquares "
            "only for a development pack."
        )
    return {
        "catalog_regions": len(regions),
        "expected_catalog_regions": (
            expected_catalog_regions
            if expected_catalog_regions is not None
            else len(regions)
        ),
        "required_region_planes": len(regions) * len(MAPSQUARE_PLANES),
        "complete_region_planes": complete_region_planes,
        "missing_asset_count": missing_count,
        "animated_model_assets": animated_model_assets,
        "complete": complete,
        "scope": "full" if complete else "partial",
    }


def is_audio_file(path: Path) -> bool:
    return path.suffix.lower() in {".wav", ".ogg", ".flac"}


def is_music_file(path: Path) -> bool:
    return path.suffix.lower() in {".ogg", ".mp3", ".flac"}


def is_map_file(path: Path) -> bool:
    return path.suffix.lower() in {".png", ".bin", ".json"}


def build_specs(data_root: Path) -> tuple[PackSpec, ...]:
    assigned: dict[str, str] = {}
    specs: list[PackSpec] = []

    def add_spec(
        stem: str,
        paths: Iterable[Path],
        numbered: bool = False,
        chunkable: bool = True,
    ) -> None:
        files = tuple(add_unique(assigned, stem, paths, data_root))
        if files:
            specs.append(
                PackSpec(
                    stem=stem,
                    files=files,
                    numbered=numbered,
                    chunkable=chunkable,
                )
            )

    defs_root = data_root / "defs"
    add_spec(
        "runec-defs",
        (
            p
            for p in defs_root.glob("*")
            if p.suffix in {".bin", ".tsv"}
            and p.name != "collision_tiles.bin"
            and p.is_file()
        ),
    )

    add_spec(
        "runec-spawns",
        (
            data_root / "spawns/world.npc-spawns.indexed.bin",
            data_root / "spawns/world.ground-items.indexed.bin",
        ),
    )

    add_spec(
        "runec-regions",
        (p for p in iter_files(data_root / "regions") if is_region_runtime_file(p)),
        numbered=True,
    )

    models_root = data_root / "models"
    model_files = tuple(p for p in models_root.glob("*") if p.is_file())
    add_spec(
        "runec-models-items",
        (
            p
            for p in model_files
            if p.name == "item_render.map" or p.name.startswith("items.")
        ),
    )
    add_spec(
        "runec-models-ground",
        (p for p in iter_files(models_root / "ground") if p.suffix == ".models"),
        numbered=True,
    )
    add_spec(
        "runec-models-npcs",
        (
            p for p in model_files
            if p.name.startswith("npcs.") or p.name.startswith("npcs_")
        ),
        numbered=True,
        chunkable=False,
    )
    add_spec(
        "runec-models-player",
        (p for p in model_files if p.name.startswith("player.")),
    )
    add_spec(
        "runec-models-projectiles",
        (p for p in model_files if p.name.startswith("projectiles.")),
    )
    add_spec(
        "runec-models-spawn-objects",
        (p for p in model_files if p.name.startswith("spawn_objects.")),
    )
    add_spec(
        "runec-models-extra",
        (
            p
            for p in model_files
            if logical_path(data_root, p) not in assigned
            and p.suffix in {".models", ".atlas", ".tanim", ".map"}
        ),
    )

    add_spec("runec-anims", (data_root / "anims").glob("*.anims"))
    add_spec(
        "runec-sprites-ui",
        (data_root / "sprites" / "ui").glob("*.png"),
    )
    add_spec(
        "runec-sprites-items",
        list((data_root / "sprites" / "items").glob("item_*.png"))
        + list((data_root / "sprites" / "items").glob("item_stack_variants.tsv")),
        numbered=True,
    )
    add_spec("runec-fonts", (data_root / "fonts").glob("*.ttf"))
    add_spec(
        "runec-ui",
        (
            p
            for p in [
                data_root / "ui" / "interfaces.bin",
                data_root / "ui" / "interface_manifest.json",
            ]
            if p.exists()
        ),
    )

    add_spec(
        "runec-audio",
        (p for p in iter_files(data_root / "audio") if is_audio_file(p)),
        numbered=True,
    )
    add_spec(
        "runec-music",
        (p for p in iter_files(data_root / "music") if is_music_file(p)),
        numbered=True,
    )
    add_spec(
        "runec-maps",
        (p for p in iter_files(data_root / "maps") if is_map_file(p)),
        numbered=True,
    )

    return tuple(specs)


def chunk_spec(spec: PackSpec, max_pack_bytes: int) -> tuple[PackChunk, ...]:
    chunks: list[PackChunk] = []
    current: list[Path] = []
    current_size = 0
    index = 0

    for path in spec.files:
        size = path.stat().st_size
        if (
            spec.numbered
            and spec.chunkable
            and current
            and current_size + size > max_pack_bytes
        ):
            chunks.append(
                PackChunk(
                    spec=spec,
                    index=index,
                    files=tuple(current),
                    uncompressed_size=current_size,
                )
            )
            index += 1
            current = []
            current_size = 0
        current.append(path)
        current_size += size

    if current:
        chunks.append(
            PackChunk(
                spec=spec,
                index=index,
                files=tuple(current),
                uncompressed_size=current_size,
            )
        )
    return tuple(chunks)


def pack_name(chunk: PackChunk, version: str, chunk_count: int) -> str:
    if chunk.spec.numbered or chunk_count > 1:
        return f"{chunk.spec.stem}-{version}-{chunk.index:03d}.pak"
    return f"{chunk.spec.stem}-{version}.pak"


def human_size(size: int) -> str:
    value = float(size)
    for unit in ("B", "KiB", "MiB", "GiB", "TiB"):
        if value < 1024.0 or unit == "TiB":
            if unit == "B":
                return f"{int(value)} {unit}"
            return f"{value:.1f} {unit}"
        value /= 1024.0
    return f"{size} B"


def compression_for(path: Path, store_extensions: set[str], compression_level: int) -> str:
    if (compression_level == 0
            or path.suffix.lower() in store_extensions
            or path.name.endswith(STORE_SUFFIXES)):
        return "store"
    return "zlib"


def write_asset(
    src: Path,
    out,
    method: str,
    compression_level: int,
) -> tuple[int, int, str]:
    sha = hashlib.sha256()
    uncompressed_size = 0
    packed_size = 0

    with src.open("rb") as f:
        if method == "store":
            for block in iter(lambda: f.read(IO_CHUNK_BYTES), b""):
                sha.update(block)
                uncompressed_size += len(block)
                out.write(block)
                packed_size += len(block)
        elif method == "zlib":
            compressor = zlib.compressobj(compression_level)
            for block in iter(lambda: f.read(IO_CHUNK_BYTES), b""):
                sha.update(block)
                uncompressed_size += len(block)
                compressed = compressor.compress(block)
                if compressed:
                    out.write(compressed)
                    packed_size += len(compressed)
            compressed = compressor.flush()
            if compressed:
                out.write(compressed)
                packed_size += len(compressed)
        else:
            raise ValueError(f"unsupported compression method: {method}")

    return uncompressed_size, packed_size, sha.hexdigest()


def file_sha256(path: Path) -> str:
    sha = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(IO_CHUNK_BYTES), b""):
            sha.update(block)
    return sha.hexdigest()


def optional_file_sha256(path: Path) -> str | None:
    return file_sha256(path) if path.exists() else None


def source_authority_policy_hash() -> str:
    sha = hashlib.sha256()
    for path in (
        Path("data-sources/sources.lock"),
        Path("tools/validate_source_authority.py"),
        Path("tools/validate_sources.py"),
    ):
        if path.exists():
            sha.update(path.as_posix().encode("utf-8"))
            sha.update(b"\0")
            with path.open("rb") as f:
                for block in iter(lambda: f.read(IO_CHUNK_BYTES), b""):
                    sha.update(block)
            sha.update(b"\0")
    return sha.hexdigest()


def load_schema_versions() -> dict[str, int]:
    versions: dict[str, int] = {}
    schema_root = Path("schema/defs")
    for path in sorted(schema_root.glob("*.schema.toml")):
        try:
            with path.open("rb") as f:
                data = tomllib.load(f)
        except tomllib.TOMLDecodeError as exc:
            raise SystemExit(f"cannot parse schema {path}: {exc}") from exc
        schema_id = data.get("id") or path.stem.removesuffix(".schema")
        current = data.get("current_version")
        if isinstance(current, int):
            versions[str(schema_id)] = current
    return versions


def asset_lookup(assets: list[dict]) -> dict[str, dict]:
    return {entry["path"]: entry for entry in assets}


def loose_asset_checksums(assets: list[dict]) -> dict[str, dict]:
    by_path = asset_lookup(assets)
    out: dict[str, dict] = {}
    for logical in REQUIRED_LOGICAL_PATHS:
        entry = by_path.get(logical)
        if not entry:
            continue
        out[logical] = {
            "bytes": entry["size"],
            "sha256": entry["sha256"],
        }
    return out


def pack_index_bytes(version: str, name: str, entries: list[dict]) -> bytes:
    out = bytearray()
    out.extend(INDEX_COUNT.pack(INDEX_MAGIC, len(entries)))
    for entry in entries:
        path_bytes = entry["path"].encode("utf-8")
        if len(path_bytes) > 65535:
            raise SystemExit(f"asset path too long for pack index: {entry['path']}")
        compression = 0 if entry["compression"] == "store" else 1
        out.extend(INDEX_PATH_LEN.pack(len(path_bytes)))
        out.extend(path_bytes)
        out.extend(
            INDEX_ENTRY_FIXED.pack(
                compression,
                entry["offset"],
                entry["size"],
                entry["packed_size"],
                bytes.fromhex(entry["sha256"]),
            )
        )
    return bytes(out)


def write_pack(
    chunk: PackChunk,
    chunk_count: int,
    data_root: Path,
    packs_dir: Path,
    version: str,
    compression_level: int,
    store_extensions: set[str],
    force: bool,
) -> tuple[dict, list[dict]]:
    name = pack_name(chunk, version, chunk_count)
    path = packs_dir / name
    tmp_path = path.with_name(f"{path.name}.tmp")
    if path.exists() and not force:
        raise SystemExit(f"refusing to overwrite existing pack: {path}")
    if tmp_path.exists():
        tmp_path.unlink()

    entries: list[dict] = []
    with tmp_path.open("w+b") as out:
        out.write(bytes(PACK_HEADER.size))
        for src in chunk.files:
            offset = out.tell()
            method = compression_for(src, store_extensions, compression_level)
            size, packed_size, sha = write_asset(src, out, method, compression_level)
            entries.append(
                {
                    "path": logical_path(data_root, src),
                    "pack": name,
                    "offset": offset,
                    "size": size,
                    "packed_size": packed_size,
                    "sha256": sha,
                    "compression": method,
                }
            )

        index_offset = out.tell()
        index_bytes = pack_index_bytes(version, name, entries)
        index_sha = hashlib.sha256(index_bytes).digest()
        out.write(index_bytes)
        out.seek(0)
        out.write(
            PACK_HEADER.pack(
                PACK_MAGIC,
                index_offset,
                len(index_bytes),
                len(entries),
                index_sha,
            )
        )

    if path.exists():
        path.unlink()
    tmp_path.replace(path)

    pack_info = {
        "name": name,
        "path": f"packs/{name}",
        "bytes": path.stat().st_size,
        "size": path.stat().st_size,
        "sha256": file_sha256(path),
        "asset_count": len(entries),
        "uncompressed_size": sum(e["size"] for e in entries),
        "packed_payload_size": sum(e["packed_size"] for e in entries),
        "index_offset": entries[-1]["offset"] + entries[-1]["packed_size"]
        if entries
        else PACK_HEADER.size,
    }
    return pack_info, entries


def build_manifest(
    version: str,
    data_root: Path,
    specs: tuple[PackSpec, ...],
    packs: list[dict],
    assets: list[dict],
    max_pack_bytes: int,
    mapsquare_visuals: dict[str, int | bool | str],
    spawn_index: dict[str, int],
    object_placement_index: dict[str, int],
) -> dict:
    source_lock_path = Path("data-sources/sources.lock")
    content_catalog_path = Path("content/catalog.toml")
    by_path = asset_lookup(assets)
    missing_required = [
        logical for logical in REQUIRED_LOGICAL_PATHS if logical not in by_path
    ]
    if missing_required:
        raise SystemExit(
            "missing required runtime logical paths: "
            + ", ".join(missing_required)
        )
    return {
        "format": "runec-data-manifest-v1",
        "pack_format": {
            "magic": PACK_MAGIC.decode("ascii"),
            "index_magic": INDEX_MAGIC.decode("ascii"),
            "version": PACK_FORMAT_VERSION,
            "header": "little-endian <8sQQQ32s>",
            "index": (
                "binary RCPI0001 index at offset stored in header; each row is "
                "path_len, path, compression, offset, size, packed_size, sha256"
            ),
        },
        "data_version": version,
        "generated_utc": datetime.now(timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z"),
        "source": {
            "data_root": portable_path(data_root),
            "runtime_subset_doc": "docs/archive/data_runtime_subsets.md",
            "source_lockfile": source_lock_path.as_posix(),
            "source_lockfile_sha256": optional_file_sha256(source_lock_path),
            "source_authority_policy_sha256": source_authority_policy_hash(),
            "content_catalog": content_catalog_path.as_posix(),
            "content_catalog_sha256": optional_file_sha256(content_catalog_path),
        },
        "schema_versions": load_schema_versions(),
        "required_logical_paths": list(REQUIRED_LOGICAL_PATHS),
        "provisional_required_logical_paths": list(PROVISIONAL_REQUIRED_LOGICAL_PATHS),
        "optional_logical_paths": list(OPTIONAL_LOGICAL_PATHS),
        "mapsquare_visuals": mapsquare_visuals,
        "spawn_index": spawn_index,
        "object_placement_index": object_placement_index,
        "loose_asset_checksums": loose_asset_checksums(assets),
        "target_max_pack_bytes": max_pack_bytes,
        "packs": packs,
        "assets": sorted(assets, key=lambda entry: entry["path"]),
        "subsets": [
            {
                "pack_stem": spec.stem,
                "asset_count": len(spec.files),
                "uncompressed_size": sum(p.stat().st_size for p in spec.files),
            }
            for spec in specs
        ],
        "explicit_excludes": [
            "data/source/**",
            "data/curated/**",
            "docs/archive/database_cleanup.md",
            "docs/archive/data_runtime_subsets.md",
            "data/ui/interface_debug.txt",
            "**/*.md",
            "**/*.log",
            "**/*.tmp",
            "**/*.bak",
            "**/*.old",
        ],
    }


def dry_run(
    specs: tuple[PackSpec, ...],
    chunks_by_spec: dict[str, tuple[PackChunk, ...]],
    data_root: Path,
    version: str,
    list_assets: bool,
    mapsquare_visuals: dict[str, int | bool | str],
) -> None:
    total_files = sum(len(spec.files) for spec in specs)
    total_size = sum(sum(p.stat().st_size for p in spec.files) for spec in specs)
    print(f"RuneC runtime data pack plan ({version})")
    print(f"data root: {data_root}")
    print(f"runtime assets: {total_files} files, {human_size(total_size)}")
    print(
        "mapsquare visuals: "
        f"{mapsquare_visuals['scope']} "
        f"({mapsquare_visuals['complete_region_planes']}/"
        f"{mapsquare_visuals['required_region_planes']} region-planes)"
    )
    print()
    for spec in specs:
        chunks = chunks_by_spec[spec.stem]
        spec_size = sum(p.stat().st_size for p in spec.files)
        print(f"{spec.stem}: {len(spec.files)} files, {human_size(spec_size)}")
        for chunk in chunks:
            name = pack_name(chunk, version, len(chunks))
            print(
                f"  {name}: {len(chunk.files)} files, "
                f"{human_size(chunk.uncompressed_size)}"
            )
            if list_assets:
                for path in chunk.files:
                    print(
                        f"    {logical_path(data_root, path)} "
                        f"({human_size(path.stat().st_size)})"
                    )
        print()


def validate_required_specs(specs: tuple[PackSpec, ...]) -> None:
    present = {spec.stem for spec in specs if spec.files}
    required = {
        "runec-defs",
        "runec-spawns",
        "runec-regions",
        "runec-models-items",
        "runec-models-npcs",
        "runec-models-player",
        "runec-models-projectiles",
        "runec-models-spawn-objects",
        "runec-anims",
        "runec-sprites-ui",
        "runec-sprites-items",
        "runec-fonts",
        "runec-ui",
    }
    missing = sorted(required - present)
    if missing:
        raise SystemExit(
            "missing required runtime data subsets: " + ", ".join(missing)
        )


def validate_spawn_runtime_assets(data_root: Path) -> dict[str, int]:
    npc_path = data_root / "spawns/world.npc-spawns.indexed.bin"
    ground_path = data_root / "spawns/world.ground-items.indexed.bin"
    try:
        npc_rows = read_npc_spawns(npc_path)
        ground_rows = read_ground_item_spawns(ground_path)
        npc_header = read_indexed_header(npc_path, NPC_SPAWN_INDEX_MAGIC)
        ground_header = read_indexed_header(
            ground_path, GROUND_ITEM_INDEX_MAGIC
        )
    except (OSError, ValueError) as exc:
        raise SystemExit(f"invalid indexed spawn runtime data: {exc}") from exc
    return {
        "npc_rows": len(npc_rows),
        "npc_mapsquares": int(npc_header[4]),
        "ground_item_rows": len(ground_rows),
        "ground_item_mapsquares": int(ground_header[4]),
    }


def validate_object_placement_runtime_asset(data_root: Path) -> dict[str, int]:
    path = data_root / "regions/world.object-placements.indexed.bin"
    try:
        header = read_object_placement_header(path)
        row_count = sum(1 for _ in iter_object_placements(path))
    except (OSError, ValueError) as exc:
        raise SystemExit(
            f"invalid indexed object placement runtime data: {exc}"
        ) from exc
    if row_count != header[2]:
        raise SystemExit("indexed object placement row count mismatch")
    return {
        "rows": row_count,
        "mapsquares": int(header[4]),
    }


def validate_collision_runtime_asset(data_root: Path) -> dict[str, int]:
    path = data_root / f"regions/{COLLISION_TILE_INDEX_FILE}"
    try:
        header = read_collision_tile_header(path)
        row_count = sum(1 for _ in iter_collision_tiles(path))
    except (OSError, ValueError) as exc:
        raise SystemExit(
            f"invalid indexed collision runtime data: {exc}"
        ) from exc
    if row_count != header[2]:
        raise SystemExit("indexed collision row count mismatch")
    return {
        "rows": row_count,
        "mapsquares": int(header[4]),
    }


def main() -> int:
    args = parse_args()
    version = version_slug(args.version)
    data_root = Path(args.data_root).resolve()
    output_root = Path(args.output).resolve()
    packs_dir = output_root / "packs"
    compression_level = max(0, min(9, args.compression_level))
    store_extensions = {
        ext if ext.startswith(".") else f".{ext}"
        for ext in (part.strip().lower() for part in args.store_extensions.split(","))
        if ext
    }

    if not data_root.exists():
        raise SystemExit(f"data root does not exist: {data_root}")

    mapsquare_visuals = validate_mapsquare_runtime_assets(
        data_root,
        allow_partial=args.allow_partial_mapsquares,
        expected_catalog_regions=EXPECTED_MAPSQUARE_REGIONS,
    )
    spawn_index = validate_spawn_runtime_assets(data_root)
    object_placement_index = validate_object_placement_runtime_asset(data_root)
    collision_index = validate_collision_runtime_asset(data_root)
    specs = build_specs(data_root)
    validate_required_specs(specs)
    chunks_by_spec = {
        spec.stem: chunk_spec(spec, args.max_pack_bytes) for spec in specs
    }

    if args.dry_run:
        dry_run(
            specs,
            chunks_by_spec,
            data_root,
            version,
            args.list_assets,
            mapsquare_visuals,
        )
        print(
            "indexed spawns: "
            f"NPCs={spawn_index['npc_rows']} in "
            f"{spawn_index['npc_mapsquares']} mapsquares, "
            f"ground_items={spawn_index['ground_item_rows']} in "
            f"{spawn_index['ground_item_mapsquares']} mapsquares"
        )
        print(
            "indexed object placements: "
            f"rows={object_placement_index['rows']} in "
            f"{object_placement_index['mapsquares']} mapsquares"
        )
        print(
            "indexed collision: "
            f"rows={collision_index['rows']} in "
            f"{collision_index['mapsquares']} mapsquares"
        )
        return 0

    manifest_path = output_root / "manifest.json"
    if manifest_path.exists() and not args.force:
        raise SystemExit(f"refusing to overwrite existing manifest: {manifest_path}")
    packs_dir.mkdir(parents=True, exist_ok=True)

    packs: list[dict] = []
    assets: list[dict] = []
    current_pack_names: set[str] = set()
    for spec in specs:
        chunks = chunks_by_spec[spec.stem]
        for chunk in chunks:
            name = pack_name(chunk, version, len(chunks))
            current_pack_names.add(name)
            print(
                f"writing {name}: {len(chunk.files)} files, "
                f"{human_size(chunk.uncompressed_size)}",
                flush=True,
            )
            pack_info, pack_assets = write_pack(
                chunk=chunk,
                chunk_count=len(chunks),
                data_root=data_root,
                packs_dir=packs_dir,
                version=version,
                compression_level=compression_level,
                store_extensions=store_extensions,
                force=args.force,
            )
            packs.append(pack_info)
            assets.extend(pack_assets)

    if args.force:
        for stale in sorted(packs_dir.glob("*.pak")):
            if stale.name not in current_pack_names:
                print(f"removing stale pack {stale.name}", flush=True)
                stale.unlink()

    manifest = build_manifest(
        version=version,
        data_root=data_root,
        specs=specs,
        packs=packs,
        assets=assets,
        max_pack_bytes=args.max_pack_bytes,
        mapsquare_visuals=mapsquare_visuals,
        spawn_index=spawn_index,
        object_placement_index=object_placement_index,
    )
    tmp_manifest = manifest_path.with_name(f"{manifest_path.name}.tmp")
    with tmp_manifest.open("w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)
        f.write("\n")
    if manifest_path.exists():
        manifest_path.unlink()
    tmp_manifest.replace(manifest_path)

    print(f"wrote {manifest_path}")
    print(f"wrote {len(packs)} packs containing {len(assets)} assets")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
