"""Procedural terrain heights for modern OSRS map archives.

Tiles without an explicit height take one from this noise (MapRegion.calculate
in the client); upper planes sit 240 units above the plane below.
"""

from __future__ import annotations

import math

JAGEX_CIRCULAR_ANGLE = 2048
JAGEX_RADIAN = 2.0 * math.pi / JAGEX_CIRCULAR_ANGLE
COS_TABLE = [int(65536.0 * math.cos(i * JAGEX_RADIAN)) for i in range(JAGEX_CIRCULAR_ANGLE)]


def noise(x: int, y: int) -> int:
    """OSRS procedural noise (MapRegion.noise)."""
    n = x + y * 57
    n ^= (n << 13) & 0xFFFFFFFF
    n &= 0xFFFFFFFF
    val = (n * (n * n * 15731 + 789221) + 1376312589) & 0x7FFFFFFF
    return (val >> 19) & 0xFF


def smoothed_noise(x: int, y: int) -> int:
    """Smoothed noise with corner/side/center weighting."""
    corners = noise(x - 1, y - 1) + noise(x + 1, y - 1) + noise(x - 1, y + 1) + noise(x + 1, y + 1)
    sides = noise(x - 1, y) + noise(x + 1, y) + noise(x, y - 1) + noise(x, y + 1)
    center = noise(x, y)
    return center // 4 + sides // 8 + corners // 16


def interpolate(a: int, b: int, x: int, y: int) -> int:
    """Cosine interpolation using Jagex COS table."""
    f = (0x10000 - COS_TABLE[(1024 * x // y) % JAGEX_CIRCULAR_ANGLE]) >> 1
    return (f * b >> 16) + (a * (0x10000 - f) >> 16)


def interpolate_noise(x: int, y: int, frequency: int) -> int:
    """Multi-octave interpolated noise."""
    int_x = x // frequency
    frac_x = x % frequency
    int_y = y // frequency
    frac_y = y % frequency
    v1 = smoothed_noise(int_x, int_y)
    v2 = smoothed_noise(int_x + 1, int_y)
    v3 = smoothed_noise(int_x, int_y + 1)
    v4 = smoothed_noise(int_x + 1, int_y + 1)
    i1 = interpolate(v1, v2, frac_x, frequency)
    i2 = interpolate(v3, v4, frac_x, frequency)
    return interpolate(i1, i2, frac_y, frequency)


def calculate_height(x: int, y: int) -> int:
    """Procedural height for tiles without explicit height (MapRegion.calculate)."""
    n = (
        interpolate_noise(x + 45365, y + 91923, 4)
        - 128
        + ((interpolate_noise(10294 + x, y + 37821, 2) - 128) >> 1)
        + ((interpolate_noise(x, y, 1) - 128) >> 2)
    )
    n = 35 + int(n * 0.3)
    if n < 10:
        n = 10
    elif n > 60:
        n = 60
    return n
