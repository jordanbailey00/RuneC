"""Name-hash helpers for named cache groups and files."""

from __future__ import annotations

from .index import IndexManifest

_U32_MASK = 0xFFFFFFFF


def _to_signed32(value: int) -> int:
    value &= _U32_MASK
    if value >= 0x80000000:
        value -= 0x100000000
    return value


def _to_u32(value: int) -> int:
    return value & _U32_MASK


def name_hash(name: str) -> int:
    """Return the signed 32-bit cache name hash used by reference tables."""
    h = 0
    for ch in name.lower():
        h = ((h << 5) - h + ord(ch)) & _U32_MASK
    return _to_signed32(h)


def find_group_id(manifest: IndexManifest, name: str) -> int | None:
    target = _to_u32(name_hash(name))
    for group_id, stored_hash in manifest.group_name_hashes.items():
        if _to_u32(stored_hash) == target:
            return group_id
    return None


def find_file_id(manifest: IndexManifest, group_id: int, name: str) -> int | None:
    target = _to_u32(name_hash(name))
    file_hashes = manifest.group_file_name_hashes.get(group_id, {})
    for file_id, stored_hash in file_hashes.items():
        if _to_u32(stored_hash) == target:
            return file_id
    return None
