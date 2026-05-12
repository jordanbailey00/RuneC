"""Binary readers for RuneScape cache formats."""

from __future__ import annotations

import io
import struct


def read_u8(buf: io.BytesIO) -> int:
    b = buf.read(1)
    if not b:
        return 0
    return b[0]


def read_i8(buf: io.BytesIO) -> int:
    b = buf.read(1)
    if not b:
        return 0
    return struct.unpack(">b", b)[0]


def read_u16(buf: io.BytesIO) -> int:
    b = buf.read(2)
    if len(b) < 2:
        return 0
    return struct.unpack(">H", b)[0]


def read_i16(buf: io.BytesIO) -> int:
    b = buf.read(2)
    if len(b) < 2:
        return 0
    return struct.unpack(">h", b)[0]


def read_u24(buf: io.BytesIO) -> int:
    b = buf.read(3)
    if len(b) < 3:
        return 0
    return (b[0] << 16) | (b[1] << 8) | b[2]


def read_u32(buf: io.BytesIO) -> int:
    b = buf.read(4)
    if len(b) < 4:
        return 0
    return struct.unpack(">I", b)[0]


def read_i32(buf: io.BytesIO) -> int:
    b = buf.read(4)
    if len(b) < 4:
        return 0
    return struct.unpack(">i", b)[0]


def read_smart(buf: io.BytesIO) -> int:
    """Read the common unsigned 1-byte/2-byte cache smart."""
    pos = buf.tell()
    peek = buf.read(1)
    if not peek:
        return 0
    if peek[0] < 128:
        return peek[0]
    buf.seek(pos)
    return read_u16(buf) - 32768


def read_big_smart(buf: io.BytesIO) -> int:
    """Read the modern unsigned big smart used by protocol 7 tables."""
    pos = buf.tell()
    peek = buf.read(1)
    if not peek:
        return 0
    buf.seek(pos)
    if peek[0] < 128:
        return read_u16(buf)
    return read_i32(buf) & 0x7FFFFFFF


def read_string(buf: io.BytesIO, encoding: str = "cp1252") -> str:
    """Read a null-terminated cache string."""
    raw = bytearray()
    while True:
        b = buf.read(1)
        if not b or b[0] == 0:
            break
        raw.append(b[0])
    return raw.decode(encoding, errors="replace")
