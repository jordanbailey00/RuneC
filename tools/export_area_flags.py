#!/usr/bin/env python3
"""Export provisional area flags from the mirrored Near-Reality source."""
from __future__ import annotations

import argparse
import ast
import re
import struct
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

from source_paths import NEAR_REALITY_MAP_LOCATIONS, ROOT

OUT = ROOT / "data/defs/area_flags.bin"
REPORT = ROOT / "tools/reports/area_flags.txt"
SOURCE_URL = (
    "https://raw.githubusercontent.com/Ecoscape16/near-reality-server-new/"
    "master/cache/src/main/java/com/zenyte/utils/MapLocations.java"
)

AFLG_MAGIC = 0x474C4641
AFLG_VERSION = 1

FLAG_MULTICOMBAT = 1 << 0
FLAG_WILDERNESS = 1 << 1
FLAG_WILDERNESS_LEVEL_LINE = 1 << 2
FLAG_DEADMAN_SAFE = 1 << 3
FLAG_PVP_SAFE = 1 << 4
FLAG_SINGLES_PLUS = 1 << 5

SOURCE_NEAR_REALITY_ZENYTE = 1

CATEGORIES = {
    "MULTICOMBAT": (FLAG_MULTICOMBAT, 0),
    "NOT_MULTICOMBAT": (0, FLAG_MULTICOMBAT),
    "ROUGH_WILDERNESS": (FLAG_WILDERNESS, 0),
    "WILDERNESS_LEVEL_LINES": (FLAG_WILDERNESS_LEVEL_LINE, 0),
    "DEADMAN_SAFE_ZONES": (FLAG_DEADMAN_SAFE, 0),
    "PVP_WORLD_SAFE_ZONES": (FLAG_PVP_SAFE, 0),
    "SINGLES_PLUS_LIST": (FLAG_SINGLES_PLUS, 0),
}

FLAG_NAMES = {
    FLAG_MULTICOMBAT: "multicombat",
    FLAG_WILDERNESS: "wilderness",
    FLAG_WILDERNESS_LEVEL_LINE: "wilderness_level_line",
    FLAG_DEADMAN_SAFE: "deadman_safe",
    FLAG_PVP_SAFE: "pvp_safe",
    FLAG_SINGLES_PLUS: "singles_plus",
}


@dataclass(frozen=True)
class AreaRow:
    plane: int
    set_flags: int
    clear_flags: int
    value: int
    points: tuple[tuple[int, int], ...]


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//.*", "", text)


def split_args(raw: str) -> list[str]:
    args: list[str] = []
    start = depth = brace = 0
    for i, ch in enumerate(raw):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif ch == "{":
            brace += 1
        elif ch == "}":
            brace -= 1
        elif ch == "," and depth == 0 and brace == 0:
            args.append(raw[start:i].strip())
            start = i + 1
    tail = raw[start:].strip()
    if tail:
        args.append(tail)
    return args


def safe_eval_int(expr: str, names: dict[str, int] | None = None) -> int:
    tree = ast.parse(expr, mode="eval")
    names = names or {}

    def walk(node: ast.AST) -> int:
        if isinstance(node, ast.Expression):
            return walk(node.body)
        if isinstance(node, ast.Constant) and isinstance(node.value, int):
            return int(node.value)
        if isinstance(node, ast.Name) and node.id in names:
            return names[node.id]
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
            return -walk(node.operand)
        if isinstance(node, ast.BinOp):
            left = walk(node.left)
            right = walk(node.right)
            if isinstance(node.op, ast.Add):
                return left + right
            if isinstance(node.op, ast.Sub):
                return left - right
            if isinstance(node.op, ast.Mult):
                return left * right
            if isinstance(node.op, ast.LShift):
                return left << right
            if isinstance(node.op, ast.RShift):
                return left >> right
            if isinstance(node.op, ast.BitAnd):
                return left & right
        raise ValueError(expr)

    return walk(tree)


def parse_points(args: list[str], names: dict[str, int] | None = None) -> tuple[tuple[int, int], ...]:
    values = [safe_eval_int(arg, names) for arg in args]
    if len(values) < 6 or len(values) % 2:
        raise ValueError("bad polygon point count")
    return tuple((values[i], values[i + 1]) for i in range(0, len(values), 2))


def call_end(text: str, open_pos: int) -> int:
    depth = 0
    for i in range(open_pos, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return i
    return -1


def add_row(rows: list[AreaRow], category: str, planes: list[int],
            points: tuple[tuple[int, int], ...], value: int = 0) -> None:
    set_flags, clear_flags = CATEGORIES[category]
    for plane in planes:
        if 0 <= plane < 4:
            rows.append(AreaRow(plane, set_flags, clear_flags, value, points))


def add_rect(rows: list[AreaRow], category: str, planes: list[int],
             min_x: int, min_y: int, max_x: int, max_y: int,
             value: int = 0) -> None:
    points = ((min_x, min_y), (max_x, min_y), (max_x, max_y),
              (min_x, max_y))
    add_row(rows, category, planes, points, value)


def zone_bounds(rx0: int, ry0: int, lx0: int, ly0: int,
                rx1: int, ry1: int, lx1: int, ly1: int) -> tuple[int, int, int, int]:
    return rx0 * 64 + lx0, ry0 * 64 + ly0, rx1 * 64 + lx1, ry1 * 64 + ly1


def add_zone(rows: list[AreaRow], category: str, rx0: int, ry0: int,
             lx0: int, ly0: int, rx1: int, ry1: int, lx1: int, ly1: int,
             value: int = 0) -> None:
    add_rect(rows, category, [0, 1, 2, 3],
             *zone_bounds(rx0, ry0, lx0, ly0, rx1, ry1, lx1, ly1),
             value)


def add_zone_with_hole(rows: list[AreaRow], category: str,
                       outer: tuple[int, int, int, int],
                       hole: tuple[int, int, int, int],
                       value: int = 0) -> None:
    min_x, min_y, max_x, max_y = outer
    hx0, hy0, hx1, hy1 = hole
    if min_x <= hx0 - 1:
        add_rect(rows, category, [0, 1, 2, 3], min_x, min_y, hx0 - 1, max_y,
                 value)
    if hx1 + 1 <= max_x:
        add_rect(rows, category, [0, 1, 2, 3], hx1 + 1, min_y, max_x, max_y,
                 value)
    if min_y <= hy0 - 1:
        add_rect(rows, category, [0, 1, 2, 3], hx0, min_y, hx1, hy0 - 1,
                 value)
    if hy1 + 1 <= max_y:
        add_rect(rows, category, [0, 1, 2, 3], hx0, hy1 + 1, hx1, max_y,
                 value)


def trunc_div(numer: int, denom: int) -> int:
    sign = -1 if numer < 0 else 1
    return sign * (abs(numer) // denom)


def cs2_scale(value: int, base: int, scaled: int) -> int:
    return trunc_div(value * scaled, base)


def add_y_formula_rows(rows: list[AreaRow], min_x: int, min_y: int,
                       max_x: int, max_y: int, level_fn) -> None:
    start_y = min_y
    last_level = level_fn(min_y)
    for y in range(min_y + 1, max_y + 2):
        level = level_fn(y) if y <= max_y else None
        if level == last_level:
            continue
        add_rect(rows, "WILDERNESS_LEVEL_LINES", [0, 1, 2, 3],
                 min_x, start_y, max_x, y - 1, last_level)
        start_y = y
        last_level = level


def parse_region_id_rows(text: str, rows: list[AreaRow]) -> None:
    match = re.search(r"var regionIds = List\.of\((.*?)\);", text, re.S)
    if not match:
        return
    ids = [int(v) for v in re.findall(r"\b\d+\b", match.group(1))]
    for region_id in ids:
        rx = region_id >> 8
        ry = region_id & 0xFF
        points = ((rx << 6, ry << 6), ((rx + 1) << 6, ry << 6),
                  ((rx + 1) << 6, (ry + 1) << 6),
                  (rx << 6, (ry + 1) << 6))
        add_row(rows, "MULTICOMBAT", [0], points)


def parse_polygon_calls(text: str) -> list[AreaRow]:
    clean = strip_comments(text)
    rows: list[AreaRow] = []
    parse_region_id_rows(clean, rows)
    pattern = re.compile(r"\b(addPolygonOnPlanes|addPolygonOnPlane|addPolygonTo)\s*\(")
    for match in pattern.finditer(clean):
        name = match.group(1)
        end = call_end(clean, match.end() - 1)
        if end < 0:
            continue
        args = split_args(clean[match.end():end])
        if not args or args[0] not in CATEGORIES:
            continue
        try:
            if name == "addPolygonTo":
                add_row(rows, args[0], [0, 1, 2, 3], parse_points(args[1:]))
            elif name == "addPolygonOnPlane":
                add_row(rows, args[0], [safe_eval_int(args[1])],
                        parse_points(args[2:]))
            else:
                planes = [int(v) for v in re.findall(r"\d+", args[1])]
                add_row(rows, args[0], planes, parse_points(args[2:]))
        except ValueError:
            continue
    add_wilderness_level_lines(rows)
    return rows


def add_wilderness_level_lines(rows: list[AreaRow]) -> None:
    # Mirrors b237 [proc,wilderness_level].cs2; rows stay provisional
    # because the enclosing wilderness geometry is still private-server data.
    outer = zone_bounds(52, 62, 0, 0, 54, 64, 63, 63)
    hole = zone_bounds(53, 63, 21, 21, 53, 63, 42, 42)
    add_zone_with_hole(rows, "WILDERNESS_LEVEL_LINES", outer, hole, 5)

    min_x, min_y, max_x, max_y = zone_bounds(46, 55, 0, 0, 52, 67, 63, 63)
    add_y_formula_rows(rows, min_x, min_y, max_x, max_y,
                       lambda y: (y - 55 * 64) // 8 + 1)

    add_y_formula_rows(rows, *zone_bounds(47, 158, 0, 0, 47, 158, 63, 63),
                       lambda y: (y - 155 * 64) // 8 - 1)

    add_zone(rows, "WILDERNESS_LEVEL_LINES", 51, 159, 0, 0, 51, 159, 63, 63, 35)
    add_zone(rows, "WILDERNESS_LEVEL_LINES", 53, 159, 0, 0, 53, 159, 63, 63, 35)
    add_zone(rows, "WILDERNESS_LEVEL_LINES", 52, 161, 0, 0, 52, 161, 63, 63, 40)
    add_zone(rows, "WILDERNESS_LEVEL_LINES", 27, 180, 0, 0, 27, 180, 63, 63, 21)
    add_zone(rows, "WILDERNESS_LEVEL_LINES", 29, 180, 0, 0, 29, 180, 63, 63, 21)
    add_zone(rows, "WILDERNESS_LEVEL_LINES", 25, 180, 0, 0, 25, 180, 63, 63, 29)

    add_y_formula_rows(rows, *zone_bounds(52, 160, 0, 0, 52, 160, 63, 63),
                       lambda y: 33 + cs2_scale((y % 64) - 6, 50, 7))

    add_y_formula_rows(rows, *zone_bounds(46, 155, 0, 0, 53, 169, 63, 63),
                       lambda y: (y - 155 * 64) // 8 + 1)


def bounds(points: tuple[tuple[int, int], ...]) -> tuple[int, int, int, int]:
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    return min(xs), min(ys), max(xs), max(ys)


def flag_label(bits: int) -> str:
    names = [name for flag, name in FLAG_NAMES.items() if bits & flag]
    return ",".join(names) if names else "none"


def write_binary(rows: list[AreaRow], out: Path) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    point_count = sum(len(row.points) for row in rows)
    with out.open("wb") as f:
        f.write(struct.pack("<IIII", AFLG_MAGIC, AFLG_VERSION, len(rows), point_count))
        for row in rows:
            min_x, min_y, max_x, max_y = bounds(row.points)
            f.write(struct.pack(
                "<BBHIIHHHHHH",
                row.plane, 0, len(row.points), row.set_flags, row.clear_flags,
                min_x, min_y, max_x, max_y, row.value,
                SOURCE_NEAR_REALITY_ZENYTE,
            ))
            for x, y in row.points:
                f.write(struct.pack("<HH", x, y))


def write_report(rows: list[AreaRow], report: Path, source: Path, out: Path) -> None:
    set_counts: Counter[int] = Counter(row.set_flags for row in rows if row.set_flags)
    clear_counts: Counter[int] = Counter(row.clear_flags for row in rows if row.clear_flags)
    plane_counts: Counter[int] = Counter(row.plane for row in rows)
    max_vertices = max((len(row.points) for row in rows), default=0)
    report.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "Area flags catalog",
        "",
        "status: READY_WITH_ACCEPTED_SIMPLIFICATIONS",
        f"output binary: {out.relative_to(ROOT)} ({out.stat().st_size} bytes)",
        f"source file: {source.relative_to(ROOT)}",
        f"source URL: {SOURCE_URL}",
        "source authority: provisional private-server geometry",
        "authoritative_osrs: false for every row",
        f"rows: {len(rows)}",
        f"vertices: {sum(len(row.points) for row in rows)}",
        f"max vertices in one row: {max_vertices}",
        "",
        "rows by plane:",
    ]
    for plane in range(4):
        lines.append(f"  plane {plane}: {plane_counts[plane]}")
    lines.append("")
    lines.append("set flags:")
    for bits, count in sorted(set_counts.items()):
        lines.append(f"  {flag_label(bits)}: {count}")
    lines.append("clear flags:")
    for bits, count in sorted(clear_counts.items()):
        lines.append(f"  {flag_label(bits)}: {count}")
    lines.extend([
        "",
        "accepted simplifications:",
        "  - geometry is from Near-Reality/Zenyte MapLocations.java, not Jagex server data",
        "  - polygons are retained as source-space geometry and indexed by mapsquare at load time",
        "  - NOT_MULTICOMBAT rows clear the multicombat bit after positive rows match",
        "  - final parity sign-off still requires authoritative OSRS server geometry or live capture",
    ])
    report.write_text("\n".join(lines) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=NEAR_REALITY_MAP_LOCATIONS)
    parser.add_argument("--output", type=Path, default=OUT)
    parser.add_argument("--report", type=Path, default=REPORT)
    args = parser.parse_args()
    if not args.source.exists():
        raise SystemExit(f"missing source file: {args.source}")
    rows = parse_polygon_calls(args.source.read_text())
    if not rows:
        raise SystemExit("no area flag rows parsed")
    write_binary(rows, args.output)
    write_report(rows, args.report, args.source, args.output)
    print(args.report.read_text())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
