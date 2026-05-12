#!/usr/bin/env python3
"""Export combat visual selection rows for RuneC.

This consumes RSMod's cache-enricher/content definitions as an authoring
source, then resolves b237 ids through the local Joshua-F dump. The generated
TSV is loaded by rc-core and stays a local data artifact under data/defs.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import re
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DUMP = ROOT / "tools/cache_pipeline/source/osrs-dumps"
DEFAULT_RSMOD = Path("/home/joe/projects/runescape-rl-reference/rsmod")
DEFAULT_OUT = ROOT / "data/defs/combat_visuals.tsv"

OBJ_ENRICHER = (
    "api/cache-enricher/src/main/resources/org/rsmod/api/cache/enricher/"
    "obj/objs.toml"
)
PROJANIMS = "api/config/src/main/kotlin/org/rsmod/api/config/builders/ProjAnimBuilds.kt"
ELEMENTAL = (
    "content/skills/magic/spell-attacks/src/main/kotlin/org/rsmod/content/"
    "skills/magic/spell/attacks/standard/ElementalSpells.kt"
)

HEADER = (
    "kind|key|style|attack_anim|launch_spotanim|travel_spotanim|"
    "impact_spotanim|projectile_model|projectile_anim|hit_delay|"
    "client_delay|proj_start_height|proj_end_height|proj_delay|proj_angle|"
    "proj_length_adjustment|proj_progress|proj_step_multiplier|note"
)


@dataclass(frozen=True)
class ProjAnimSpec:
    start_height: int
    end_height: int
    delay: int
    angle: int
    length_adjustment: int
    progress: int
    step_multiplier: int

    def end_time(self, distance: int) -> int:
        return self.delay + self.length_adjustment + self.step_multiplier * distance

    def server_cycles(self, distance: int) -> int:
        return 1 + max(0, self.end_time(distance)) // 30


def read_symbol_map(path: Path) -> dict[str, int]:
    out: dict[str, int] = {}
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(None, 1)
        if len(parts) != 2:
            continue
        try:
            ident = int(parts[0])
        except ValueError:
            continue
        out[parts[1].strip()] = ident
    return out


def read_spot_names(path: Path) -> dict[str, int]:
    out: dict[str, int] = {}
    current_id: int | None = None
    for line in path.read_text().splitlines():
        m_id = re.match(r"//\s*(\d+)\s*$", line)
        if m_id:
            current_id = int(m_id.group(1))
            continue
        m_name = re.match(r"\[([^\]]+)\]\s*$", line)
        if m_name and current_id is not None:
            out[m_name.group(1)] = current_id
            current_id = None
    return out


def read_spot_defs(path: Path, seq_ids: dict[str, int]) -> dict[int, tuple[int, int]]:
    out: dict[int, tuple[int, int]] = {}
    current_id: int | None = None
    current_model = -1
    current_anim = -1

    def flush() -> None:
        nonlocal current_id, current_model, current_anim
        if current_id is not None:
            out[current_id] = (current_model, current_anim)
        current_id = None
        current_model = -1
        current_anim = -1

    for raw in path.read_text().splitlines():
        line = raw.strip()
        m_id = re.match(r"//\s*(\d+)\s*$", line)
        if m_id:
            flush()
            current_id = int(m_id.group(1))
            continue
        if current_id is None:
            continue
        if line.startswith("model=model_"):
            try:
                current_model = int(line.removeprefix("model=model_"))
            except ValueError:
                current_model = -1
        elif line.startswith("anim="):
            current_anim = seq_ids.get(line.removeprefix("anim="), -1)
    flush()
    return out


def parse_projanims(path: Path) -> dict[str, ProjAnimSpec]:
    text = path.read_text()
    out: dict[str, ProjAnimSpec] = {}
    pattern = re.compile(r'build\("([^"]+)"\)\s*\{(.*?)\n\s*\}', re.S)
    for name, body in pattern.findall(text):
        values: dict[str, int] = {}
        for key in (
            "startHeight",
            "endHeight",
            "delay",
            "angle",
            "lengthAdjustment",
            "progress",
            "stepMultiplier",
        ):
            m = re.search(rf"\b{key}\s*=\s*(-?\d+)", body)
            if m:
                values[key] = int(m.group(1))
        if {"startHeight", "endHeight", "delay", "angle",
                "lengthAdjustment", "progress", "stepMultiplier"} <= values.keys():
            out[name] = ProjAnimSpec(
                values["startHeight"],
                values["endHeight"],
                values["delay"],
                values["angle"],
                values["lengthAdjustment"],
                values["progress"],
                values["stepMultiplier"],
            )
    return out


def value_or_dash(value: int | None) -> str:
    return "-" if value is None or value < 0 else str(value)


def append_row(rows: list[list[str]], *values: object) -> None:
    rows.append([str(v) for v in values])


def projectile_columns(spec: ProjAnimSpec | None) -> list[str]:
    if spec is None:
        return ["-"] * 7
    return [
        str(spec.start_height),
        str(spec.end_height),
        str(spec.delay),
        str(spec.angle),
        str(spec.length_adjustment),
        str(spec.progress),
        str(spec.step_multiplier),
    ]


def export_item_rows(
    rows: list[list[str]],
    rsmod_root: Path,
    obj_ids: dict[str, int],
    spot_defs: dict[int, tuple[int, int]],
    projanims: dict[str, ProjAnimSpec],
) -> None:
    data = tomllib.loads((rsmod_root / OBJ_ENRICHER).read_text())
    for config in data.get("config", []):
        obj_name = config.get("obj")
        item_id = obj_ids.get(obj_name or "")
        if item_id is None:
            continue

        attack_anim = config.get("anim_stance1")
        if isinstance(attack_anim, int):
            spec = projanims.get(str(config.get("proj_anim")))
            hit_delay = spec.server_cycles(4) if spec else "-"
            client_delay = hit_delay
            append_row(
                rows, "item", item_id, "any", attack_anim, "-", "-", "-",
                "-", "-", hit_delay, client_delay, *projectile_columns(spec),
                f"rsmod:{obj_name}:attack",
            )

        launch = config.get("projectile_launch")
        travel = config.get("projectile_travel")
        if not isinstance(launch, int) and not isinstance(travel, int):
            continue
        proj_name = config.get("proj_anim")
        spec = projanims.get(str(proj_name))
        hit_delay = spec.server_cycles(4) if spec else 2
        client_delay = hit_delay
        travel_id = travel if isinstance(travel, int) else -1
        model_id, anim_id = spot_defs.get(travel_id, (-1, -1))
        append_row(
            rows, "item", item_id, "ranged", "-", value_or_dash(launch),
            value_or_dash(travel), "-", value_or_dash(model_id),
            value_or_dash(anim_id), hit_delay, client_delay,
            *projectile_columns(spec),
            f"rsmod:{obj_name}:projectile",
        )


def spell_title(internal: str) -> str:
    name = internal.removeprefix("spell_")
    return " ".join(part.capitalize() for part in name.split("_"))


def export_elemental_spell_rows(
    rows: list[list[str]],
    rsmod_root: Path,
    seq_ids: dict[str, int],
    spot_ids: dict[str, int],
    spot_defs: dict[int, tuple[int, int]],
    projanims: dict[str, ProjAnimSpec],
) -> None:
    text = (rsmod_root / ELEMENTAL).read_text()
    pattern = re.compile(
        r"spell\s*=\s*objs\.([a-zA-Z0-9_]+).*?"
        r"staffAnim\s*=\s*seqs\.([a-zA-Z0-9_]+).*?"
        r"launch\s*=\s*spotanims\.([a-zA-Z0-9_]+).*?"
        r"travel\s*=\s*spotanims\.([a-zA-Z0-9_]+).*?"
        r"impact\s*=\s*spotanims\.([a-zA-Z0-9_]+)",
        re.S,
    )
    spec = projanims.get("magic_spell")
    hit_delay = spec.server_cycles(4) if spec else 2
    client_delay = hit_delay
    for spell_obj, staff_anim, launch, travel, impact in pattern.findall(text):
        attack_id = seq_ids.get(staff_anim)
        launch_id = spot_ids.get(launch)
        travel_id = spot_ids.get(travel)
        impact_id = spot_ids.get(impact)
        if attack_id is None or launch_id is None or travel_id is None:
            continue
        model_id, anim_id = spot_defs.get(travel_id, (-1, -1))
        append_row(
            rows, "spell", spell_title(spell_obj), "magic", attack_id,
            launch_id, travel_id, value_or_dash(impact_id),
            value_or_dash(model_id), value_or_dash(anim_id),
            hit_delay, client_delay, *projectile_columns(spec),
            f"rsmod:{spell_obj}",
        )


def write_tsv(path: Path, rows: list[list[str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    dedup: dict[tuple[str, str, str, str], list[str]] = {}
    for row in rows:
        key = (row[0], row[1], row[2], row[3])
        dedup.setdefault(key, row)
    ordered = sorted(
        dedup.values(),
        key=lambda r: (r[0], int(r[1]) if r[1].isdigit() else 1 << 30, r[1], r[2], r[3]),
    )
    path.write_text(HEADER + "\n" + "\n".join("|".join(r) for r in ordered) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rsmod-root", type=Path, default=DEFAULT_RSMOD)
    parser.add_argument("--dump", type=Path, default=DEFAULT_DUMP)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()

    if not args.rsmod_root.exists():
        raise SystemExit(f"missing RSMod checkout: {args.rsmod_root}")
    if not args.dump.exists():
        raise SystemExit(f"missing osrs dump: {args.dump}")

    symbols = args.dump / "symbols"
    config = args.dump / "config"
    obj_ids = read_symbol_map(symbols / "obj.sym")
    seq_ids = read_symbol_map(symbols / "seq.sym")
    spot_ids = read_spot_names(config / "dump.spot")
    spot_defs = read_spot_defs(config / "dump.spot", seq_ids)
    projanims = parse_projanims(args.rsmod_root / PROJANIMS)

    rows: list[list[str]] = []
    export_item_rows(rows, args.rsmod_root, obj_ids, spot_defs, projanims)
    export_elemental_spell_rows(
        rows, args.rsmod_root, seq_ids, spot_ids, spot_defs, projanims)
    write_tsv(args.output, rows)
    print(f"wrote {len(rows)} source rows to {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
