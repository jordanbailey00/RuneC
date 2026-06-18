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
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
import struct
from typing import Iterable
import zlib


PACK_MAGIC = b"RCPK0002"
INDEX_MAGIC = b"RCPI0001"
PACK_HEADER = struct.Struct("<8sQQQ32s")
PACK_FORMAT_VERSION = 1
DEFAULT_MAX_PACK_BYTES = 512 * 1024 * 1024
IO_CHUNK_BYTES = 1024 * 1024
INDEX_COUNT = struct.Struct("<8sQ")
INDEX_PATH_LEN = struct.Struct("<H")
INDEX_ENTRY_FIXED = struct.Struct("<BQQQ32s")

STORE_EXTENSIONS = {
    ".jpg",
    ".jpeg",
    ".mp3",
    ".ogg",
    ".flac",
    ".png",
}


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
    name = path.name
    if name.endswith(".npc-spawns.bin"):
        return False
    return (
        name.endswith(".terrain")
        or name.endswith(".objects")
        or name.endswith(".cmap")
        or name.endswith(".atlas")
        or name.endswith(".tanim")
        or name.endswith(".oanim")
        or name.endswith(".object_anim.models")
        or name.endswith(".object_anim.atlas")
        or name.endswith(".object_anim.tanim")
    )


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
            if p.suffix in {".bin", ".tsv"} and p.is_file()
        ),
    )

    add_spec(
        "runec-spawns",
        list((data_root / "spawns").glob("*.npc-spawns.bin"))
        + list((data_root / "regions").rglob("*.npc-spawns.bin")),
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
        "runec-models-npcs",
        (p for p in model_files if p.name.startswith("npcs.")),
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
    if compression_level == 0 or path.suffix.lower() in store_extensions:
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
) -> dict:
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
            "data_root": str(data_root.resolve()),
            "runtime_subset_doc": "docs/archive/data_runtime_subsets.md",
        },
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
) -> None:
    total_files = sum(len(spec.files) for spec in specs)
    total_size = sum(sum(p.stat().st_size for p in spec.files) for spec in specs)
    print(f"RuneC runtime data pack plan ({version})")
    print(f"data root: {data_root}")
    print(f"runtime assets: {total_files} files, {human_size(total_size)}")
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

    specs = build_specs(data_root)
    validate_required_specs(specs)
    chunks_by_spec = {
        spec.stem: chunk_spec(spec, args.max_pack_bytes) for spec in specs
    }

    if args.dry_run:
        dry_run(specs, chunks_by_spec, data_root, version, args.list_assets)
        return 0

    manifest_path = output_root / "manifest.json"
    if manifest_path.exists() and not args.force:
        raise SystemExit(f"refusing to overwrite existing manifest: {manifest_path}")
    packs_dir.mkdir(parents=True, exist_ok=True)

    packs: list[dict] = []
    assets: list[dict] = []
    for spec in specs:
        chunks = chunks_by_spec[spec.stem]
        for chunk in chunks:
            name = pack_name(chunk, version, len(chunks))
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

    manifest = build_manifest(
        version=version,
        data_root=data_root,
        specs=specs,
        packs=packs,
        assets=assets,
        max_pack_bytes=args.max_pack_bytes,
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
