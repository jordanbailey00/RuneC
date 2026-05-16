#!/usr/bin/env python3
"""Expand RuneC runtime .pak files into loose local runtime data.

The .pak files are the transport format used by GitHub Releases. This tool is
the install step: it reads data/manifest.json plus data/packs/*.pak and writes
the manifest assets back to their runtime paths under data/. The viewer then
uses the existing loose-file fast path without needing runtime code changes.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import sys
import zlib


IO_CHUNK_BYTES = 1024 * 1024


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extract RuneC runtime data packs into loose local data."
    )
    parser.add_argument(
        "--data-dir",
        default="data",
        help="Runtime data directory to populate. Defaults to ./data.",
    )
    parser.add_argument(
        "--manifest",
        default=None,
        help="Manifest path. Defaults to <data-dir>/manifest.json.",
    )
    parser.add_argument(
        "--packs-dir",
        default=None,
        help="Pack directory. Defaults to <data-dir>/packs.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Rewrite assets even when an existing file already matches.",
    )
    return parser.parse_args()


def file_sha256(path: Path) -> str:
    sha = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(IO_CHUNK_BYTES), b""):
            sha.update(block)
    return sha.hexdigest()


def existing_matches(path: Path, size: int, sha256: str) -> bool:
    if not path.is_file():
        return False
    try:
        if path.stat().st_size != size:
            return False
    except OSError:
        return False
    return file_sha256(path) == sha256


def safe_asset_path(data_dir: Path, asset_path: str) -> Path:
    rel = Path(asset_path)
    if rel.is_absolute() or any(part in ("", ".", "..") for part in rel.parts):
        raise SystemExit(f"unpack-data: unsafe asset path in manifest: {asset_path}")
    target = (data_dir / rel).resolve()
    root = data_dir.resolve()
    if target != root and root not in target.parents:
        raise SystemExit(f"unpack-data: asset path escapes data dir: {asset_path}")
    return target


def copy_exact(src, dst, byte_count: int, sha: "hashlib._Hash") -> int:
    remaining = byte_count
    total = 0
    while remaining > 0:
        block = src.read(min(IO_CHUNK_BYTES, remaining))
        if not block:
            raise SystemExit("unpack-data: unexpected EOF while reading stored asset")
        dst.write(block)
        sha.update(block)
        total += len(block)
        remaining -= len(block)
    return total


def inflate_exact(src, dst, byte_count: int, sha: "hashlib._Hash") -> int:
    remaining = byte_count
    total = 0
    inflater = zlib.decompressobj()
    while remaining > 0:
        block = src.read(min(IO_CHUNK_BYTES, remaining))
        if not block:
            raise SystemExit("unpack-data: unexpected EOF while reading zlib asset")
        remaining -= len(block)
        out = inflater.decompress(block)
        if out:
            dst.write(out)
            sha.update(out)
            total += len(out)
    out = inflater.flush()
    if out:
        dst.write(out)
        sha.update(out)
        total += len(out)
    if inflater.unused_data:
        raise SystemExit("unpack-data: unexpected trailing zlib data")
    return total


def extract_asset(pack, data_dir: Path, entry: dict) -> str:
    target = safe_asset_path(data_dir, entry["path"])
    target.parent.mkdir(parents=True, exist_ok=True)
    tmp = target.parent / f".{target.name}.tmp"
    if tmp.exists():
        tmp.unlink()

    pack.seek(int(entry["offset"]))
    sha = hashlib.sha256()
    with tmp.open("wb") as out:
        compression = entry["compression"]
        if compression == "store":
            size = copy_exact(pack, out, int(entry["packed_size"]), sha)
        elif compression == "zlib":
            size = inflate_exact(pack, out, int(entry["packed_size"]), sha)
        else:
            raise SystemExit(
                f"unpack-data: unsupported compression for {entry['path']}: "
                f"{compression}"
            )

    expected_size = int(entry["size"])
    expected_sha = entry["sha256"]
    actual_sha = sha.hexdigest()
    if size != expected_size or actual_sha != expected_sha:
        tmp.unlink(missing_ok=True)
        raise SystemExit(
            f"unpack-data: extracted asset mismatch for {entry['path']}: "
            f"size {size}/{expected_size}, sha {actual_sha}/{expected_sha}"
        )

    tmp.replace(target)
    return entry["path"]


def load_manifest(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as f:
        manifest = json.load(f)
    if manifest.get("format") != "runec-data-manifest-v1":
        raise SystemExit("unpack-data: unsupported manifest format")
    return manifest


def main() -> int:
    args = parse_args()
    data_dir = Path(args.data_dir)
    manifest_path = Path(args.manifest) if args.manifest else data_dir / "manifest.json"
    packs_dir = Path(args.packs_dir) if args.packs_dir else data_dir / "packs"

    manifest = load_manifest(manifest_path)
    assets = manifest.get("assets", [])
    if not assets:
        raise SystemExit("unpack-data: manifest contains no assets")

    pack_order = [pack["name"] for pack in manifest.get("packs", [])]
    grouped: dict[str, list[dict]] = defaultdict(list)
    for asset in assets:
        grouped[asset["pack"]].append(asset)

    extracted = 0
    skipped = 0
    total_bytes = 0
    for pack_name in pack_order:
        entries = grouped.get(pack_name, [])
        if not entries:
            continue
        entries.sort(key=lambda entry: int(entry["offset"]))
        pack_path = packs_dir / pack_name
        if not pack_path.is_file():
            raise SystemExit(f"unpack-data: missing pack: {pack_path}")

        pack_extracted = 0
        pack_skipped = 0
        print(f"unpack-data: extracting {pack_name} ({len(entries)} assets)")
        with pack_path.open("rb") as pack:
            for entry in entries:
                target = safe_asset_path(data_dir, entry["path"])
                size = int(entry["size"])
                if not args.force and existing_matches(target, size, entry["sha256"]):
                    skipped += 1
                    pack_skipped += 1
                    continue
                extract_asset(pack, data_dir, entry)
                extracted += 1
                pack_extracted += 1
                total_bytes += size
        if pack_skipped:
            print(
                f"unpack-data: {pack_name}: extracted {pack_extracted}, "
                f"already current {pack_skipped}"
            )

    marker = data_dir / ".runtime-unpacked"
    marker.write_text(
        f"{manifest.get('data_version', 'unknown')}\n"
        f"{file_sha256(manifest_path)}\n",
        encoding="utf-8",
    )
    print(
        "unpack-data: ready "
        f"({extracted} extracted, {skipped} already current, "
        f"{total_bytes} bytes written)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
