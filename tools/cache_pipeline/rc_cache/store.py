"""Cache-store access for OpenRS2 flat files and Jagex dat2/idx caches."""

from __future__ import annotations

from collections.abc import Sequence
from pathlib import Path

from .container import decompress_container
from .groups import split_group
from .index import IndexManifest, parse_index_manifest
from .names import find_file_id, find_group_id


class RcCacheStore:
    """Read b237 cache containers through a stable repo-local API."""

    def __init__(self, cache_dir: str | Path, layout: str = "auto") -> None:
        self.cache_dir = Path(cache_dir)
        if not self.cache_dir.is_dir():
            msg = f"cache directory not found: {self.cache_dir}"
            raise FileNotFoundError(msg)

        self._jagex_dat2 = self.cache_dir / "main_file_cache.dat2"
        self._openrs2_layout = (self.cache_dir / "255").is_dir()

        if layout == "auto":
            self._jagex_layout = self._jagex_dat2.exists() and not self._openrs2_layout
        elif layout in ("openrs2", "flat", "numbered"):
            self._jagex_layout = False
        elif layout == "jagex":
            if not self._jagex_dat2.exists():
                msg = f"Jagex dat2 cache not found: {self._jagex_dat2}"
                raise FileNotFoundError(msg)
            self._jagex_layout = True
        else:
            msg = f"unknown cache layout: {layout}"
            raise ValueError(msg)

        self._manifest_cache: dict[int, IndexManifest] = {}

    @property
    def is_jagex_layout(self) -> bool:
        return self._jagex_layout

    @property
    def layout(self) -> str:
        if self._jagex_layout:
            return "jagex"
        return "openrs2"

    def _read_raw(self, index_id: int, group_id: int) -> bytes | None:
        if self._jagex_layout:
            return self._read_jagex_raw(index_id, group_id)

        index_dir = self.cache_dir / str(index_id)
        for path in (index_dir / f"{group_id}.dat", index_dir / str(group_id)):
            if path.exists():
                return path.read_bytes()
        return None

    def _read_jagex_raw(self, index_id: int, group_id: int) -> bytes | None:
        idx_path = self.cache_dir / f"main_file_cache.idx{index_id}"
        if not idx_path.exists():
            return None

        entry_offset = group_id * 6
        with idx_path.open("rb") as idx:
            idx.seek(0, 2)
            if idx.tell() < entry_offset + 6:
                return None
            idx.seek(entry_offset)
            entry = idx.read(6)

        if len(entry) != 6:
            return None
        size = (entry[0] << 16) | (entry[1] << 8) | entry[2]
        sector = (entry[3] << 16) | (entry[4] << 8) | entry[5]
        if size <= 0 or sector <= 0:
            return None

        header_len = 10 if group_id > 0xFFFF else 8
        payload_len = 520 - header_len
        data = bytearray()
        chunk = 0

        with self._jagex_dat2.open("rb") as dat:
            dat.seek(0, 2)
            dat_size = dat.tell()
            while len(data) < size:
                sector_offset = sector * 520
                if sector_offset + header_len > dat_size:
                    raise ValueError(
                        f"invalid dat2 sector {sector} for index={index_id} group={group_id}"
                    )

                dat.seek(sector_offset)
                header = dat.read(header_len)
                if len(header) != header_len:
                    raise ValueError(
                        f"short dat2 sector header for index={index_id} group={group_id}"
                    )

                if header_len == 10:
                    archive_id = (
                        (header[0] << 24)
                        | (header[1] << 16)
                        | (header[2] << 8)
                        | header[3]
                    )
                    chunk_id = (header[4] << 8) | header[5]
                    next_sector = (header[6] << 16) | (header[7] << 8) | header[8]
                    sector_index = header[9]
                else:
                    archive_id = (header[0] << 8) | header[1]
                    chunk_id = (header[2] << 8) | header[3]
                    next_sector = (header[4] << 16) | (header[5] << 8) | header[6]
                    sector_index = header[7]

                if archive_id != group_id or chunk_id != chunk or sector_index != index_id:
                    raise ValueError(
                        "corrupt dat2 sector chain "
                        f"index={index_id} group={group_id} sector={sector}"
                    )

                remaining = size - len(data)
                data.extend(dat.read(min(payload_len, remaining)))
                sector = next_sector
                chunk += 1

                if sector == 0 and len(data) < size:
                    raise ValueError(
                        f"truncated dat2 archive index={index_id} group={group_id}"
                    )

        return bytes(data)

    def read_container(
        self,
        index_id: int,
        group_id: int,
        xtea_key: Sequence[int] | None = None,
    ) -> bytes | None:
        raw = self._read_raw(index_id, group_id)
        if raw is None:
            return None
        return decompress_container(raw, xtea_key=xtea_key)

    def read_index_manifest(self, index_id: int) -> IndexManifest:
        if index_id in self._manifest_cache:
            return self._manifest_cache[index_id]

        data = self.read_container(255, index_id)
        if data is None:
            msg = f"manifest not found for index {index_id}"
            raise FileNotFoundError(msg)

        manifest = parse_index_manifest(data)
        self._manifest_cache[index_id] = manifest
        return manifest

    def read_group(
        self,
        index_id: int,
        group_id: int,
        xtea_key: Sequence[int] | None = None,
    ) -> dict[int, bytes]:
        manifest = self.read_index_manifest(index_id)
        if group_id not in manifest.group_file_ids:
            msg = f"group {group_id} not in index {index_id} manifest"
            raise KeyError(msg)

        data = self.read_container(index_id, group_id, xtea_key=xtea_key)
        if data is None:
            msg = f"group data not found: index={index_id} group={group_id}"
            raise FileNotFoundError(msg)

        return split_group(data, manifest.group_file_ids[group_id])

    def read_group_by_name(
        self,
        index_id: int,
        group_name: str,
        xtea_key: Sequence[int] | None = None,
    ) -> dict[int, bytes]:
        manifest = self.read_index_manifest(index_id)
        group_id = find_group_id(manifest, group_name)
        if group_id is None:
            msg = f"group name not found in index {index_id}: {group_name}"
            raise KeyError(msg)
        return self.read_group(index_id, group_id, xtea_key=xtea_key)

    def read_file_by_name(
        self,
        index_id: int,
        group_name: str,
        file_name: str,
        xtea_key: Sequence[int] | None = None,
    ) -> bytes:
        manifest = self.read_index_manifest(index_id)
        group_id = find_group_id(manifest, group_name)
        if group_id is None:
            msg = f"group name not found in index {index_id}: {group_name}"
            raise KeyError(msg)

        file_id = find_file_id(manifest, group_id, file_name)
        if file_id is None:
            msg = f"file name not found in group {group_name}: {file_name}"
            raise KeyError(msg)

        files = self.read_group(index_id, group_id, xtea_key=xtea_key)
        return files[file_id]

    def read_config_entry(self, group_id: int, entry_id: int) -> bytes:
        files = self.read_group(2, group_id)
        if entry_id not in files:
            msg = f"config entry {entry_id} not found in group {group_id}"
            raise KeyError(msg)
        return files[entry_id]


ModernCacheReader = RcCacheStore
