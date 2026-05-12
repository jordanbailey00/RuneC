"""Archive/group file splitting."""

from __future__ import annotations

import io
import struct


def split_group(data: bytes, file_ids: list[int]) -> dict[int, bytes]:
    """Split one decompressed group into its file entries."""
    if not file_ids:
        return {}

    if len(file_ids) == 1:
        return {file_ids[0]: data}

    if not data:
        raise ValueError("empty multi-file group")

    file_count = len(file_ids)
    chunk_count = data[-1]
    if chunk_count < 1:
        msg = f"invalid chunk count: {chunk_count}"
        raise ValueError(msg)

    size_table_len = chunk_count * file_count * 4
    size_table_start = len(data) - 1 - size_table_len
    if size_table_start < 0:
        msg = f"group data too small for {chunk_count} chunks x {file_count} files"
        raise ValueError(msg)

    size_buf = io.BytesIO(data[size_table_start : len(data) - 1])
    chunk_sizes: list[list[int]] = []
    for _chunk in range(chunk_count):
        sizes = []
        chunk_size = 0
        for _file in range(file_count):
            raw = size_buf.read(4)
            if len(raw) != 4:
                raise ValueError("truncated group size table")
            delta = struct.unpack(">i", raw)[0]
            chunk_size += delta
            if chunk_size < 0:
                raise ValueError("negative group chunk size")
            sizes.append(chunk_size)
        chunk_sizes.append(sizes)

    file_buffers: dict[int, bytearray] = {fid: bytearray() for fid in file_ids}
    offset = 0
    for chunk_idx in range(chunk_count):
        for file_idx, fid in enumerate(file_ids):
            size = chunk_sizes[chunk_idx][file_idx]
            end = offset + size
            if end > size_table_start:
                raise ValueError("group chunk sizes exceed data payload")
            file_buffers[fid].extend(data[offset:end])
            offset = end

    if offset != size_table_start:
        raise ValueError("group chunk sizes do not consume payload exactly")

    return {fid: bytes(buf) for fid, buf in file_buffers.items()}
