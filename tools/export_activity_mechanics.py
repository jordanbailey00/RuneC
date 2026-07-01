#!/usr/bin/env python3
"""Compile boss mechanics extracts into a loadable activity index."""

from __future__ import annotations

import re
import struct
import time
import tomllib
from pathlib import Path

from content_paths import content_read_path

ROOT = Path(__file__).resolve().parents[1]
MECH_DIR = content_read_path("mechanics")
ENCOUNTER_DIR = content_read_path("encounters")
OWNER_MAP = content_read_path("mechanics_owners.toml")
OUT = ROOT / "data/defs/activity_mechanics.bin"
REPORT = ROOT / "tools/reports/activity_mechanics.txt"

AMCH_MAGIC = 0x48434D41
AMCH_VERSION = 3

STATUS_ENCOUNTER_BACKED = 1
STATUS_ACTIVITY_INDEXED = 2
STATUS_OWNER_MAPPED = 3
STATUS_OWNER_EXECUTABLE = 4

FLAG_HAS_SECTIONS = 1 << 0
FLAG_MISSING_NPC_IDS = 1 << 1
FLAG_HAS_ENCOUNTER = 1 << 2
FLAG_BEHAVIOR_INCOMPLETE = 1 << 3
FLAG_HAS_OWNER = 1 << 4
FLAG_OWNER_ONLY = 1 << 5

BEHAVIOR_BITS = {
    "stat_drain": 1 << 0,
    "prayer_drain": 1 << 1,
    "heal_on_hit": 1 << 2,
    "poison": 1 << 3,
    "venom": 1 << 4,
    "enrage": 1 << 5,
    "area_pressure": 1 << 6,
    "damage_gate": 1 << 7,
    "protection_pierce": 1 << 8,
    "form_rotation": 1 << 9,
    "wave_boss": 1 << 10,
    "dragonfire": 1 << 11,
    "teleblock": 1 << 12,
    "freeze": 1 << 13,
}

PROFILE_IDS = {
    "barrows_ahrim": 1,
    "barrows_dharok": 2,
    "barrows_guthan": 3,
    "barrows_karil": 4,
    "barrows_torag": 5,
    "barrows_verac": 6,
    "araxxor": 7,
    "blood_moon": 8,
    "blue_moon": 9,
    "eclipse_moon": 10,
    "revenant_maledictus": 11,
    "shellbane_gryphon": 12,
    "tztok_jad": 13,
    "dawn": 14,
    "dusk": 15,
    "grotesque_guardians": 16,
    "brutus": 17,
    "demonic_brutus": 18,
    "royal_titans": 19,
    "moons_of_peril": 20,
}


def key(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "", value.lower())


def doc_npc_ids(doc: dict) -> set[int]:
    ids: set[int] = set()
    for raw in doc.get("npc_ids", []) or []:
        try:
            ids.add(int(raw))
        except (TypeError, ValueError):
            pass
    for boss in doc.get("bosses", []) or []:
        for field in ("npc_id", "id", "boss_npc_id"):
            if field not in boss:
                continue
            try:
                ids.add(int(boss[field]))
            except (TypeError, ValueError):
                pass
    return ids


def doc_activity_keys(doc: dict, slug: str) -> set[str]:
    values: list[object] = [slug, doc.get("name", ""), doc.get("slug", "")]
    values.extend(doc.get("source_pages", []) or [])
    for boss in doc.get("bosses", []) or []:
        values.extend((
            boss.get("name", ""),
            boss.get("boss_name", ""),
            boss.get("boss_npc", ""),
            boss.get("boss_npc_id", ""),
        ))
    for room in doc.get("rooms", []) or []:
        values.extend((
            room.get("id", ""),
            room.get("boss_name", ""),
            room.get("boss_npc", ""),
            room.get("boss_npc_id", ""),
        ))
        values.extend(room.get("bosses_npc", []) or [])
    return {key(str(v)) for v in values if str(v)}


def encounter_index() -> list[dict[str, object]]:
    out: list[dict[str, object]] = []
    for path in sorted(ENCOUNTER_DIR.glob("*.toml")):
        doc = tomllib.loads(path.read_text(errors="replace"))
        out.append({
            "slug": path.stem,
            "slug_key": key(path.stem),
            "ids": doc_npc_ids(doc),
            "keys": doc_activity_keys(doc, path.stem),
        })
    return out


def load_owner_map() -> tuple[dict[str, dict[str, object]],
                              dict[str, list[str]], dict[str, str]]:
    if not OWNER_MAP.is_file():
        return {}, {}, {}
    doc = tomllib.loads(OWNER_MAP.read_text(errors="replace"))
    owners: dict[str, dict[str, object]] = {}
    behavior_tags = {
        str(k): [str(v) for v in values]
        for k, values in (doc.get("behavior_tags", {}) or {}).items()
    }
    behavior_profiles = {
        str(k): str(v)
        for k, v in (doc.get("behavior_profiles", {}) or {}).items()
    }
    for entry in doc.get("owners", []) or []:
        slug = str(entry.get("slug") or "")
        owner = str(entry.get("owner") or "")
        status = str(entry.get("status") or "")
        if not slug or not owner or status != "behavior_incomplete":
            raise ValueError(f"bad mechanics owner row: {entry}")
        if slug in owners:
            raise ValueError(f"duplicate mechanics owner row: {slug}")
        owners[slug] = {
            "owner": owner,
            "owner_type": str(entry.get("owner_type") or ""),
            "reason": str(entry.get("reason") or ""),
            "behavior_tags": behavior_tags.get(slug, []),
            "behavior_profile": behavior_profiles.get(slug, ""),
        }
    npc_id_unions = {
        str(k): [str(v) for v in values]
        for k, values in (doc.get("npc_id_unions", {}) or {}).items()
    }
    owner_only_reasons = {
        str(k): str(v)
        for k, v in (doc.get("owner_only_reasons", {}) or {}).items()
    }
    missing = sorted(set(behavior_tags) - set(owners))
    if missing:
        raise ValueError(f"behavior tags without owner row: {', '.join(missing)}")
    missing_profiles = sorted(set(behavior_profiles) - set(owners))
    if missing_profiles:
        raise ValueError(
            "behavior profiles without owner row: " +
            ", ".join(missing_profiles))
    bad_profiles = sorted(
        f"{slug}={profile}"
        for slug, profile in behavior_profiles.items()
        if profile not in PROFILE_IDS)
    if bad_profiles:
        raise ValueError(
            "unknown behavior profile: " + ", ".join(bad_profiles))
    return owners, npc_id_unions, owner_only_reasons


def behavior_bits(tags: list[str]) -> int:
    bits = 0
    for tag in tags:
        if tag not in BEHAVIOR_BITS:
            raise ValueError(f"unknown activity behavior tag: {tag}")
        bits |= BEHAVIOR_BITS[tag]
    return bits


def match_encounter(slug: str, doc: dict, encounters: list[dict[str, object]]
                    ) -> str:
    ids = {int(x) for x in doc.get("npc_ids", []) or []}
    keys = {key(slug), key(str(doc.get("name") or slug))}
    keys.update(key(str(x)) for x in doc.get("source_pages", []) or [])
    for enc in encounters:
        if key(slug) == enc["slug_key"]:
            return str(enc["slug"])
        if ids and ids & enc["ids"]:
            return str(enc["slug"])
        if keys & enc["keys"]:
            return str(enc["slug"])
    return ""


def pstr(value: str) -> bytes:
    raw = value.encode("utf-8")
    if len(raw) > 255:
        raise ValueError(f"string too long for pstr: {value[:80]}")
    return struct.pack("<B", len(raw)) + raw


def fnv1a32(text: str) -> int:
    h = 0x811C9DC5
    for b in text.encode("utf-8", "replace"):
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


def read_mechanics() -> list[dict[str, object]]:
    encounters = encounter_index()
    encounter_ids_by_slug = {
        str(enc["slug"]): sorted(int(x) for x in enc["ids"])
        for enc in encounters
    }
    owners, npc_id_unions, owner_only_reasons = load_owner_map()
    rows: list[dict[str, object]] = []
    docs = {
        path.stem: tomllib.loads(path.read_text(errors="replace"))
        for path in sorted(MECH_DIR.glob("*.toml"))
    }
    raw_ids = {slug: sorted(doc_npc_ids(doc)) for slug, doc in docs.items()}
    for slug, children in npc_id_unions.items():
        if slug not in docs:
            raise ValueError(f"npc ID union without mechanics TOML: {slug}")
        missing_children = sorted(set(children) - set(docs))
        if missing_children:
            raise ValueError(
                f"npc ID union {slug} references missing rows: " +
                ", ".join(missing_children))
        ids: set[int] = set()
        for child in children:
            ids.update(raw_ids[child])
        if not ids:
            raise ValueError(f"npc ID union {slug} resolved no IDs")
        raw_ids[slug] = sorted(ids)
    missing_owner_only = sorted(set(owner_only_reasons) - set(docs))
    if missing_owner_only:
        raise ValueError(
            "owner-only rows without mechanics TOML: " +
            ", ".join(missing_owner_only))
    for slug, doc in sorted(docs.items()):
        encounter_slug = match_encounter(slug, doc, encounters)
        owner = owners.get(slug, {})
        owner_slug = str(owner.get("owner") or "")
        bits = behavior_bits(list(owner.get("behavior_tags", [])))
        profile_id = PROFILE_IDS.get(str(owner.get("behavior_profile") or ""),
                                     0)
        npc_ids = list(raw_ids[slug])
        if not npc_ids and encounter_slug:
            npc_ids = list(encounter_ids_by_slug.get(encounter_slug, []))
        owner_only = slug in owner_only_reasons
        sections = doc.get("sections", {}) or {}
        if encounter_slug:
            status = STATUS_ENCOUNTER_BACKED
            owner_field = encounter_slug
        elif owner_slug and bits:
            status = STATUS_OWNER_EXECUTABLE
            owner_field = owner_slug
        elif owner_slug:
            status = STATUS_OWNER_MAPPED
            owner_field = owner_slug
        else:
            status = STATUS_ACTIVITY_INDEXED
            owner_field = ""
        flags = 0
        if sections:
            flags |= FLAG_HAS_SECTIONS
        if owner_only:
            flags |= FLAG_OWNER_ONLY
        if not npc_ids and not owner_only:
            flags |= FLAG_MISSING_NPC_IDS
        if encounter_slug:
            flags |= FLAG_HAS_ENCOUNTER
        elif owner_slug and bits:
            flags |= FLAG_HAS_OWNER
        elif owner_slug:
            flags |= FLAG_HAS_OWNER | FLAG_BEHAVIOR_INCOMPLETE
        else:
            flags |= FLAG_BEHAVIOR_INCOMPLETE
        rows.append({
            "slug": slug,
            "name": str(doc.get("name") or slug.replace("_", " ")),
            "source_pages": ";".join(str(x) for x in doc.get("source_pages", [])),
            "encounter_slug": owner_field,
            "status": status,
            "flags": flags,
            "behavior_bits": bits,
            "profile_id": profile_id,
            "npc_ids": npc_ids,
            "npc_id_source": ",".join(npc_id_unions.get(slug, [])),
            "owner_only_reason": owner_only_reasons.get(slug, ""),
            "sections": [
                (str(title), len(text.encode("utf-8", "replace")), fnv1a32(str(text)))
                for title, text in sorted(sections.items())
            ],
        })
    missing = sorted(set(owners) - {str(r["slug"]) for r in rows})
    if missing:
        raise ValueError(f"mechanics owner rows without TOML: {', '.join(missing)}")
    return rows


def write_bin(rows: list[dict[str, object]]) -> None:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", AMCH_MAGIC, AMCH_VERSION, len(rows)))
        for row in rows:
            npc_ids = row["npc_ids"]
            sections = row["sections"]
            assert isinstance(npc_ids, list)
            assert isinstance(sections, list)
            f.write(struct.pack("<BBHH", row["status"], row["flags"],
                                len(npc_ids), len(sections)))
            f.write(pstr(str(row["slug"])))
            f.write(pstr(str(row["name"])))
            f.write(pstr(str(row["source_pages"])))
            f.write(pstr(str(row["encounter_slug"])))
            f.write(struct.pack("<Q", int(row["behavior_bits"])))
            f.write(struct.pack("<H", int(row["profile_id"])))
            for npc_id in npc_ids:
                f.write(struct.pack("<I", npc_id))
            for title, text_len, text_hash in sections:
                f.write(pstr(title))
                f.write(struct.pack("<II", text_hash, text_len))


def write_report(rows: list[dict[str, object]]) -> None:
    backed = [r for r in rows if r["status"] == STATUS_ENCOUNTER_BACKED]
    owner_mapped = [r for r in rows if r["status"] == STATUS_OWNER_MAPPED]
    owner_exec = [r for r in rows if r["status"] == STATUS_OWNER_EXECUTABLE]
    indexed = [r for r in rows if r["status"] == STATUS_ACTIVITY_INDEXED]
    missing_ids = [r for r in rows if r["flags"] & FLAG_MISSING_NPC_IDS]
    owner_only = [r for r in rows if r["flags"] & FLAG_OWNER_ONLY]
    derived_ids = [r for r in rows if r["npc_id_source"]]
    section_count = sum(len(r["sections"]) for r in rows)
    lines = [
        "Activity mechanics export",
        "",
        f"mechanics TOMLs: {len(rows)}",
        f"encounter-backed rows: {len(backed)}",
        f"owner-executable rows: {len(owner_exec)}",
        f"owner-mapped behavior-incomplete rows: {len(owner_mapped)}",
        f"activity-indexed rows: {len(indexed)}",
        f"rows missing NPC IDs: {len(missing_ids)}",
        f"owner-only rows: {len(owner_only)}",
        f"derived NPC ID rows: {len(derived_ids)}",
        f"section descriptors: {section_count}",
        f"output: {OUT.relative_to(ROOT)}",
        "",
        "Status",
        "READY: all mechanics extracts are represented in a loadable binary index.",
        "READY_WITH_ACCEPTED_SIMPLIFICATIONS: owner-executable rows have generic/profile runtime effects; full state machines can still refine them.",
        "BLOCKS_PARITY: activity-indexed rows preserve mechanics coverage but have no owner or executable behavior.",
        "",
        "Owner-executable rows",
    ]
    lines.extend(
        f"- {r['slug']} -> {r['encounter_slug']} "
        f"bits=0x{int(r['behavior_bits']):x} profile={int(r['profile_id'])}"
        for r in owner_exec
    )
    lines.extend([
        "",
        "Owner-mapped behavior-incomplete rows",
    ]
    )
    if owner_mapped:
        lines.extend(
            f"- {r['slug']} -> {r['encounter_slug']}" for r in owner_mapped
        )
    else:
        lines.append("- none")
    lines.extend([
        "",
        "Activity-indexed mechanics-only rows",
    ])
    if indexed:
        lines.extend(f"- {r['slug']}" for r in indexed)
    else:
        lines.append("- none")
    if missing_ids:
        lines.extend(["", "Rows missing NPC IDs"])
        lines.extend(f"- {r['slug']}" for r in missing_ids)
    lines.extend(["", "Owner-only rows"])
    if owner_only:
        lines.extend(
            f"- {r['slug']}: {r['owner_only_reason']}"
            for r in owner_only
        )
    else:
        lines.append("- none")
    lines.extend(["", "Derived NPC ID rows"])
    if derived_ids:
        lines.extend(
            f"- {r['slug']}: {len(r['npc_ids'])} IDs from {r['npc_id_source']}"
            for r in derived_ids
        )
    else:
        lines.append("- none")
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text("\n".join(lines) + "\n")


def main() -> int:
    start = time.perf_counter()
    rows = read_mechanics()
    write_bin(rows)
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    write_report(rows)
    indexed = sum(1 for r in rows if r["status"] == STATUS_ACTIVITY_INDEXED)
    owned = sum(1 for r in rows
                if r["status"] in (STATUS_OWNER_MAPPED,
                                   STATUS_OWNER_EXECUTABLE))
    executable = sum(1 for r in rows
                     if r["status"] == STATUS_OWNER_EXECUTABLE)
    print(f"exported {len(rows)} activity mechanics rows "
          f"({executable}/{owned} owner-executable, "
          f"{indexed} activity-indexed) "
          f"in {elapsed_ms:.3f} ms")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
