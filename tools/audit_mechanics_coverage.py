#!/usr/bin/env python3
"""Summarize boss and regular-NPC mechanics database coverage."""

from __future__ import annotations

import re
import tomllib
from pathlib import Path

from content_paths import content_read_path

ROOT = Path(__file__).resolve().parents[1]
MECH_DIR = content_read_path("mechanics")
ENCOUNTER_DIR = content_read_path("encounters")
MECH_EXTRACT_REPORT = ROOT / "tools/reports/mechanics_extract.txt"
ENCOUNTER_REPORT = ROOT / "tools/reports/encounters.txt"
REGULAR_REPORT = ROOT / "tools/reports/regular_npc_mechanics.txt"
ACTIVITY_REPORT = ROOT / "tools/reports/activity_mechanics.txt"
ACTIVITY_STATE_REPORT = ROOT / "tools/reports/activity_states.txt"
ACTIVITY_STATE_SRC = content_read_path("activity_state_machines.toml")
OWNER_MAP = content_read_path("mechanics_owners.toml")
SCRIPT_STUBS = ROOT / "rc-content/encounters/scripts.c"
OUT = ROOT / "tools/reports/mechanics_coverage.txt"


def read_text(path: Path) -> str:
    return path.read_text(errors="replace") if path.is_file() else ""


def first_int(pattern: str, text: str) -> int:
    m = re.search(pattern, text, re.MULTILINE)
    return int(m.group(1)) if m else 0


def slug_key(slug: str) -> str:
    return re.sub(r"[^a-z0-9]+", "", slug.lower())


def authored_scripts() -> tuple[int, set[str]]:
    count = 0
    names: set[str] = set()
    for path in ENCOUNTER_DIR.glob("*.toml"):
        try:
            doc = tomllib.loads(path.read_text(errors="replace"))
        except tomllib.TOMLDecodeError:
            continue
        for phase in doc.get("phases") or []:
            script = phase.get("script")
            if script:
                count += 1
                names.add(str(script))
        for mech in doc.get("mechanics") or []:
            script = mech.get("script")
            if script:
                count += 1
                names.add(str(script))
    return count, names


def routed_scripts() -> set[str]:
    text = read_text(SCRIPT_STUBS)
    return set(re.findall(r'"([A-Za-z0-9_]+)"', text))


def typed_activity_source_keys() -> set[str]:
    if not ACTIVITY_STATE_SRC.is_file():
        return set()
    doc = tomllib.loads(ACTIVITY_STATE_SRC.read_text(errors="replace"))
    keys: set[str] = set()
    for entry in doc.get("activities", []) or []:
        keys.add(slug_key(str(entry.get("slug") or "")))
        for row in entry.get("source_rows", []) or []:
            keys.add(slug_key(str(row)))
    return {k for k in keys if k}


def encounter_source_keys() -> set[str]:
    keys: set[str] = set()
    for path in ENCOUNTER_DIR.glob("*.toml"):
        try:
            doc = tomllib.loads(path.read_text(errors="replace"))
        except tomllib.TOMLDecodeError:
            continue
        values: list[object] = [path.stem, doc.get("slug", ""),
                                doc.get("name", "")]
        values.extend(doc.get("source_pages", []) or [])
        for boss in doc.get("bosses", []) or []:
            values.extend((boss.get("name", ""), boss.get("boss_name", "")))
        for room in doc.get("rooms", []) or []:
            values.extend((room.get("id", ""), room.get("boss_name", ""),
                           room.get("boss_npc", "")))
            values.extend(room.get("bosses_npc", []) or [])
        for value in values:
            k = slug_key(str(value))
            if k:
                keys.add(k)
    return keys


def deferred_mechanics_keys() -> set[str]:
    if not OWNER_MAP.is_file():
        return set()
    doc = tomllib.loads(OWNER_MAP.read_text(errors="replace"))
    out: set[str] = set()
    for entry in doc.get("owners", []) or []:
        owner = str(entry.get("owner") or "")
        if owner in {"quest_bosses", "the_ides_of_milk"}:
            out.add(slug_key(str(entry.get("slug") or "")))
    return {k for k in out if k}


def main() -> int:
    boss_mechanics = sorted(p.stem for p in MECH_DIR.glob("*.toml"))
    encounter_specs = sorted(p.stem for p in ENCOUNTER_DIR.glob("*.toml"))
    encounter_keys = encounter_source_keys()
    mechanics_keys = {slug_key(slug): slug for slug in boss_mechanics}
    mechanics_only = sorted(
        slug for slug in boss_mechanics if slug_key(slug) not in encounter_keys
    )
    typed_keys = typed_activity_source_keys()
    typed_covered = sorted(
        slug for slug in mechanics_only if slug_key(slug) in typed_keys
    )
    deferred_keys = deferred_mechanics_keys()
    deferred_only = sorted(
        slug for slug in mechanics_only
        if slug_key(slug) not in typed_keys and slug_key(slug) in deferred_keys
    )
    mechanics_uncovered = sorted(
        slug for slug in mechanics_only
        if slug_key(slug) not in typed_keys and slug_key(slug) not in deferred_keys
    )
    encounters_without_extract = sorted(
        slug for slug in encounter_specs if slug_key(slug) not in mechanics_keys
    )

    mech_extract = read_text(MECH_EXTRACT_REPORT)
    encounter_report = read_text(ENCOUNTER_REPORT)
    regular_report = read_text(REGULAR_REPORT)
    activity_report = read_text(ACTIVITY_REPORT)
    activity_state_report = read_text(ACTIVITY_STATE_REPORT)

    no_fight = first_int(r"no fight sections:\s+(\d+)", mech_extract)
    no_npc_id = first_int(r"no NPC-id resolution:\s+(\d+)",
                          mech_extract)
    deferred = first_int(r"warnings:\s+(\d+)", encounter_report)
    encoded = first_int(r"encounters encoded:\s+(\d+)", encounter_report)
    regular_families = first_int(r"families:\s+(\d+)", regular_report)
    regular_links = first_int(r"family NPC ID links:\s+(\d+)",
                              regular_report)
    activity_rows = first_int(r"mechanics TOMLs:\s+(\d+)", activity_report)
    activity_indexed = first_int(r"activity-indexed rows:\s+(\d+)",
                                 activity_report)
    owner_exec = first_int(r"owner-executable rows:\s+(\d+)",
                           activity_report)
    owner_mapped = first_int(r"owner-mapped behavior-incomplete rows:\s+(\d+)",
                             activity_report)
    activity_missing_ids = first_int(r"rows missing NPC IDs:\s+(\d+)",
                                     activity_report)
    owner_only = first_int(r"owner-only rows:\s+(\d+)", activity_report)
    derived_ids = first_int(r"derived NPC ID rows:\s+(\d+)",
                            activity_report)
    activity_state_rows = first_int(r"activity rows:\s+(\d+)",
                                    activity_state_report)
    activity_state_v1 = first_int(r"v1-required rows:\s+(\d+)",
                                  activity_state_report)
    activity_state_nodes = first_int(r"states:\s+(\d+)",
                                     activity_state_report)
    activity_state_transitions = first_int(r"transitions:\s+(\d+)",
                                           activity_state_report)
    script_fields, script_names = authored_scripts()
    routed = routed_scripts()
    unrouted = sorted(script_names - routed)
    routed_count = len(script_names & routed)

    lines = [
        "Mechanics coverage audit",
        "",
        "Boss mechanics prose",
        f"- extracted mechanics TOMLs: {len(boss_mechanics)}",
        f"- bosses with no fight sections in extract report: {no_fight}",
        f"- bosses with no NPC-id resolution in extract report: {no_npc_id}",
        "- status: READY_WITH_ACCEPTED_SIMPLIFICATIONS",
        "",
        "Executable boss encounters",
        f"- encounter TOMLs: {len(encounter_specs)}",
        f"- encoded encounters report count: {encoded}",
        f"- boss mechanics TOMLs without encounter TOML: {len(mechanics_only)}",
        f"- mechanics-only rows covered by typed activity states: {len(typed_covered)}",
        f"- mechanics-only rows deferred as quest/nice-to-have: {len(deferred_only)}",
        f"- v1 mechanics-only rows still lacking encounter or typed state: {len(mechanics_uncovered)}",
        f"- encounter TOMLs without mechanics extract: {len(encounters_without_extract)}",
        f"- unresolved trigger bindings: {deferred}",
        f"- authored script fields: {script_fields}",
        f"- distinct authored script names: {len(script_names)}",
        f"- script names routed through rc-content registry: {routed_count}",
        f"- script names not routed: {len(unrouted)}",
        f"- owner-executable TOMLs: {owner_exec}",
        f"- owner-mapped behavior-incomplete TOMLs: {owner_mapped}",
        f"- activity-indexed mechanics-only TOMLs: {activity_indexed}",
        f"- activity-backed mechanics TOMLs: {activity_rows - activity_indexed - owner_mapped}",
        f"- activity mechanics binary rows: {activity_rows}",
        f"- activity rows missing NPC IDs: {activity_missing_ids}",
        f"- owner-only activity rows: {owner_only}",
        f"- derived NPC ID rows: {derived_ids}",
        f"- typed activity state-machine rows: {activity_state_rows}",
        f"- v1-required typed activity rows: {activity_state_v1}",
        f"- typed activity states: {activity_state_nodes}",
        f"- typed activity transitions: {activity_state_transitions}",
        "- status: BLOCKS_PARITY" if mechanics_uncovered else "- status: READY",
        "",
        "Regular combat NPC mechanics",
        f"- runtime-indexed families: {regular_families}",
        f"- family NPC ID links: {regular_links}",
        "- status: READY_WITH_ACCEPTED_SIMPLIFICATIONS",
        "- live consumers: damage gates, finisher gates, breath mitigation, "
        "poison/venom/disease status, revenant heal/teleblock/freeze, "
        "lizardman shaman jump/area/minion pressure, slayer protection "
        "checks, auto-finisher unlock, activity owner generic/profile effects, "
        "and slayer task kill-credit decrement",
        "- slayer consumers: SLAY v3 task amounts, extended amounts, "
        "level gates, reward unlocks, exact progression gates, Konar "
        "location rolls, boss-task second rolls, block/prefer filters, "
        "and regular/boss alternative kill-credit decrement",
        "",
        "Next closure work",
        "- Keep expanding bespoke encounter/activity consumers where typed "
        "state-machine flow or AMCH profiles are still too broad.",
        "- Continue non-mechanics database lanes: objects/interactables, "
        "collision/area flags, prayers/spells/actions, shops/storage, "
        "transportation, skills/gathering, activity schemas, and "
        "normalization.",
    ]
    if mechanics_uncovered:
        lines.extend([
            "- Promote v1-required mechanics-only rows to executable "
            "encounter TOMLs or typed activity rows.",
        ])
    if unrouted:
        lines.extend(["", "Unrouted script names"])
        lines.extend(f"- {name}" for name in unrouted)
    if typed_covered:
        lines.extend(["", "Mechanics-only rows covered by typed activity states"])
        lines.extend(f"- {slug}" for slug in typed_covered[:80])
        if len(typed_covered) > 80:
            lines.append(f"- ... {len(typed_covered) - 80} more")
    if deferred_only:
        lines.extend(["", "Mechanics-only rows deferred as quest/nice-to-have"])
        lines.extend(f"- {slug}" for slug in deferred_only[:80])
        if len(deferred_only) > 80:
            lines.append(f"- ... {len(deferred_only) - 80} more")
    if mechanics_uncovered:
        lines.extend(["", "V1 mechanics TOMLs without executable encounter or typed activity row"])
        lines.extend(f"- {slug}" for slug in mechanics_uncovered[:80])
        if len(mechanics_uncovered) > 80:
            lines.append(f"- ... {len(mechanics_uncovered) - 80} more")

    OUT.write_text("\n".join(lines) + "\n")
    print(f"wrote {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
