"""XTEA helpers for encrypted cache groups."""

from __future__ import annotations

import struct
from collections.abc import Sequence

_MASK = 0xFFFFFFFF
_DELTA = 0x9E3779B9


def normalize_xtea_key(key: Sequence[int]) -> tuple[int, int, int, int]:
    if len(key) != 4:
        msg = f"XTEA key must contain four 32-bit words, got {len(key)}"
        raise ValueError(msg)
    return tuple((int(word) & _MASK) for word in key)  # type: ignore[return-value]


def xtea_decrypt(data: bytes, key: Sequence[int], rounds: int = 32) -> bytes:
    """Decrypt full 8-byte blocks and preserve any trailing bytes."""
    k = normalize_xtea_key(key)
    out = bytearray(data)
    block_end = len(out) - (len(out) % 8)

    for offset in range(0, block_end, 8):
        v0, v1 = struct.unpack(">2I", out[offset : offset + 8])
        total = (_DELTA * rounds) & _MASK
        for _ in range(rounds):
            v1 = (
                v1
                - (
                    (((v0 << 4) ^ (v0 >> 5)) + v0)
                    ^ (total + k[(total >> 11) & 3])
                )
            ) & _MASK
            total = (total - _DELTA) & _MASK
            v0 = (
                v0
                - (
                    (((v1 << 4) ^ (v1 >> 5)) + v1)
                    ^ (total + k[total & 3])
                )
            ) & _MASK
        out[offset : offset + 8] = struct.pack(">2I", v0, v1)

    return bytes(out)
