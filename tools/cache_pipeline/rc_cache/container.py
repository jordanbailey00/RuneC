"""RS2 cache container decompression."""

from __future__ import annotations

import bz2
import gzip
import struct
from collections.abc import Sequence

from .xtea import normalize_xtea_key, xtea_decrypt

COMPRESSION_NONE = 0
COMPRESSION_BZIP2 = 1
COMPRESSION_GZIP = 2


def _maybe_decrypt_payload(data: bytes, xtea_key: Sequence[int] | None) -> bytes:
    payload = data[5:]
    if xtea_key is None:
        return payload

    key = normalize_xtea_key(xtea_key)
    if not any(key):
        return payload
    return xtea_decrypt(payload, key)


def decompress_container(data: bytes, xtea_key: Sequence[int] | None = None) -> bytes:
    """Decompress an RS2 cache container.

    If an XTEA key is supplied, the bytes after the 5-byte container header are
    decrypted before the compression payload is interpreted. A zero key is
    treated as absent, which matches how map keys are commonly represented.
    """
    if len(data) < 5:
        msg = f"container too small: {len(data)} bytes"
        raise ValueError(msg)

    compression = data[0]
    compressed_len = struct.unpack(">I", data[1:5])[0]
    payload_area = _maybe_decrypt_payload(data, xtea_key)

    if compression == COMPRESSION_NONE:
        if len(payload_area) < compressed_len:
            msg = (
                f"container payload too small: expected {compressed_len}, "
                f"got {len(payload_area)}"
            )
            raise ValueError(msg)
        return payload_area[:compressed_len]

    if len(payload_area) < 4:
        msg = f"compressed container missing decompressed length: {len(data)} bytes"
        raise ValueError(msg)

    decompressed_len = struct.unpack(">I", payload_area[:4])[0]
    payload = payload_area[4 : 4 + compressed_len]
    if len(payload) < compressed_len:
        msg = (
            f"compressed payload too small: expected {compressed_len}, "
            f"got {len(payload)}"
        )
        raise ValueError(msg)

    if compression == COMPRESSION_BZIP2:
        result = bz2.decompress(b"BZh1" + payload)
    elif compression == COMPRESSION_GZIP:
        result = gzip.decompress(payload)
    else:
        msg = f"unknown compression type: {compression}"
        raise ValueError(msg)

    if len(result) != decompressed_len:
        msg = (
            f"decompressed size mismatch: expected {decompressed_len}, "
            f"got {len(result)}"
        )
        raise ValueError(msg)

    return result
