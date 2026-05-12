"""Location/object placement decoding for modern OSRS map archives."""

from __future__ import annotations

import io
from dataclasses import dataclass
from typing import Iterator

from .bytes import read_smart


@dataclass(frozen=True)
class LocationPlacement:
    object_id: int
    local_x: int
    local_y: int
    world_x: int
    world_y: int
    plane: int
    shape: int
    rotation: int
    mapsquare: int


def read_extended_smart(buf: io.BytesIO) -> int:
    """Read the chained smart used by modern location object-id deltas."""
    total = 0
    value = read_smart(buf)
    while value == 32767:
        total += 32767
        value = read_smart(buf)
    return total + value


def iter_location_placements(
    data: bytes,
    region_x: int,
    region_y: int,
) -> Iterator[LocationPlacement]:
    """Yield all raw location placements for one 64x64 mapsquare."""
    buf = io.BytesIO(data)
    object_id = -1
    base_x = region_x * 64
    base_y = region_y * 64
    mapsquare = (region_x << 8) | region_y

    while True:
        object_delta = read_extended_smart(buf)
        if object_delta == 0:
            return
        object_id += object_delta
        packed_position = 0

        while True:
            position_delta = read_smart(buf)
            if position_delta == 0:
                break
            packed_position += position_delta - 1

            raw = buf.read(1)
            if not raw:
                return
            info = raw[0]

            local_y = packed_position & 0x3F
            local_x = (packed_position >> 6) & 0x3F
            plane = (packed_position >> 12) & 0x3

            yield LocationPlacement(
                object_id=object_id,
                local_x=local_x,
                local_y=local_y,
                world_x=base_x + local_x,
                world_y=base_y + local_y,
                plane=plane,
                shape=info >> 2,
                rotation=info & 0x3,
                mapsquare=mapsquare,
            )
