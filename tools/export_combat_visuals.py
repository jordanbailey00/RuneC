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
    "proj_length_adjustment|proj_progress|proj_step_multiplier|note|"
    "projectile_count|alt_proj_start_height|alt_proj_end_height|"
    "alt_proj_delay|alt_proj_angle|alt_proj_length_adjustment|"
    "alt_proj_progress|alt_proj_step_multiplier|aux_travel_spotanim|"
    "aux_impact_spotanim|aux_projectile_model|aux_projectile_anim|"
    "impact_on_last_only|double_launch_spotanim|stance_idx"
)
HEADER_COLS = HEADER.split("|")

RSMOD_ATTACK_TYPES: dict[str, tuple[str | None, str | None, str | None, str | None]] = {
    "Unarmed": ("crush", "crush", None, "crush"),
    "Axe": ("slash", "slash", "crush", "slash"),
    "Blunt": ("crush", "crush", None, "crush"),
    "Bow": ("ranged", "ranged", None, "ranged"),
    "Claw": ("slash", "slash", "stab", "slash"),
    "Crossbow": ("ranged", "ranged", None, "ranged"),
    "Salamander": ("slash", "ranged", "magic", None),
    "Chinchompas": ("ranged", "ranged", None, "ranged"),
    "Gun": (None, "crush", None, None),
    "SlashSword": ("slash", "slash", "stab", "slash"),
    "TwoHandedSword": ("slash", "slash", "crush", "slash"),
    "Pickaxe": ("stab", "stab", "crush", "stab"),
    "Polearm": ("stab", "slash", None, "stab"),
    "Polestaff": ("crush", "crush", None, "crush"),
    "Scythe": ("slash", "slash", "crush", "slash"),
    "Spear": ("stab", "slash", "crush", "stab"),
    "Spiked": ("crush", "crush", "stab", "crush"),
    "StabSword": ("stab", "stab", "slash", "stab"),
    "Staff": ("crush", "crush", None, "crush"),
    "Thrown": ("ranged", "ranged", None, "ranged"),
    "Whip": ("slash", "slash", None, "slash"),
    "BladedStaff": ("stab", "slash", None, "crush"),
    "Banner": ("slash", "slash", "crush", "slash"),
    "GodSword": ("slash", "slash", "crush", "slash"),
    "PoweredStaff": ("magic", "magic", None, "magic"),
    "Bludgeon": ("crush", "crush", None, "crush"),
    "Bulwark": ("crush", None, None, "crush"),
}


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


def resolve_ref(refs: dict[str, int], value: str | int | None) -> int:
    if value is None:
        return -1
    if isinstance(value, int):
        return value
    return refs.get(value, -1)


def named_spec(
    projanims: dict[str, ProjAnimSpec],
    name: str | ProjAnimSpec | None,
) -> ProjAnimSpec | None:
    if isinstance(name, ProjAnimSpec):
        return name
    if not name:
        return None
    return projanims.get(name)


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


def rsmod_attack_type_for_stance(category: str | None,
                                 stance_idx: int) -> str | None:
    if not category or stance_idx < 0 or stance_idx > 3:
        return None
    types = RSMOD_ATTACK_TYPES.get(category)
    if not types:
        return None
    return types[stance_idx]


def append_cache_row(
    rows: list[list[str]],
    *,
    kind: str,
    key: str | int,
    style: str,
    seq_ids: dict[str, int],
    spot_ids: dict[str, int],
    spot_defs: dict[int, tuple[int, int]],
    projanims: dict[str, ProjAnimSpec],
    attack: str | int | None = None,
    launch: str | int | None = None,
    travel: str | int | None = None,
    impact: str | int | None = None,
    proj_anim: str | ProjAnimSpec | None = None,
    note: str = "",
    extra: list[str] | None = None,
) -> bool:
    required = (
        ("attack", attack, seq_ids),
        ("launch", launch, spot_ids),
        ("travel", travel, spot_ids),
        ("impact", impact, spot_ids),
    )
    missing = [
        name for name, value, refs in required
        if isinstance(value, str) and value not in refs
    ]
    if missing:
        return False

    attack_id = resolve_ref(seq_ids, attack)
    launch_id = resolve_ref(spot_ids, launch)
    travel_id = resolve_ref(spot_ids, travel)
    impact_id = resolve_ref(spot_ids, impact)
    spec = named_spec(projanims, proj_anim)
    hit_delay: str | int = "-"
    client_delay: str | int = "-"
    if travel_id >= 0 or impact_id >= 0 or spec is not None:
        hit_delay = spec.server_cycles(4) if spec else 2
        client_delay = hit_delay
    model_id, anim_id = spot_defs.get(travel_id, (-1, -1))
    append_row(
        rows, kind, key, style, value_or_dash(attack_id),
        value_or_dash(launch_id), value_or_dash(travel_id),
        value_or_dash(impact_id), value_or_dash(model_id),
        value_or_dash(anim_id), hit_delay, client_delay,
        *projectile_columns(spec), note, *(extra or []),
    )
    return True


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

        weapon_category = config.get("weapon_category")
        spec = projanims.get(str(config.get("proj_anim")))
        hit_delay = spec.server_cycles(4) if spec else "-"
        client_delay = hit_delay
        for stance_idx in range(4):
            attack_anim = config.get(f"anim_stance{stance_idx + 1}")
            style = rsmod_attack_type_for_stance(weapon_category, stance_idx)
            if not isinstance(attack_anim, int) or style is None:
                continue
            append_row(
                rows, "item", item_id, style, attack_anim, "-", "-", "-",
                "-", "-", hit_delay, client_delay, *projectile_columns(spec),
                f"rsmod:{obj_name}:attack:stance{stance_idx + 1}",
                *["-"] * 14, str(stance_idx),
            )

        launch = config.get("projectile_launch")
        launch_double = config.get("projectile_launch_double")
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
            f"rsmod:{obj_name}:projectile", *["-"] * 13,
            value_or_dash(launch_double),
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


def export_curated_spell_rows(
    rows: list[list[str]],
    seq_ids: dict[str, int],
    spot_ids: dict[str, int],
    spot_defs: dict[int, tuple[int, int]],
    projanims: dict[str, ProjAnimSpec],
) -> None:
    # b237 has no XTEA gate on this cache line; these rows resolve directly
    # from the local dump names and RSMod projectile profiles.
    standard = [
        ("Confuse", "human_castconfuse_staff", "confuse_casting",
         "confuse_travel", "confuse_impact", "confuse"),
        ("Weaken", "human_castweaken_staff", "weaken_casting",
         "weaken_travel", "weaken_impact", "magic_spell"),
        ("Curse", "human_castcurse_staff", "curse_casting",
         "curse_travel", "curse_impact", "magic_spell"),
        ("Bind", "human_castentangle_staff", "entangle_casting",
         "entangle_travel", "bind_impact", "bind"),
        ("Snare", "human_castentangle_staff", "entangle_casting",
         "entangle_travel", "snare_impact", "bind"),
        ("Entangle", "human_castentangle_staff", "entangle_casting",
         "entangle_travel", "entangle_impact", "bind"),
        ("Crumble Undead", "human_castcrumbleundead_staff",
         "crumbleundead_casting", "crumbleundead_travel",
         "crumbleundead_impact", "crumble_undead"),
        ("Iban Blast", "human_castibanblast", "ibanblast_casting",
         "ibanblast_travel", "ibanblast_impact", "iban_blast"),
        ("Magic Dart", "slayer_magicdart_cast", None,
         "slayer_magicdart_travel", "slayer_magicdart_impact",
         "magic_spell"),
        ("Vulnerability", "human_casting", "vulnerability_casting",
         "vulnerability_travel", "vulnerability_impact",
         "vulnerability"),
        ("Enfeeble", "human_castenfeeble_staff", "enfeeble_casting",
         "enfeeble_travel", "enfeeble_impact", "enfeeble"),
        ("Stun", "human_caststun_staff", "stun_casting",
         "stun_travel", "stun_impact", "stun"),
        ("Tele Block", "human_casting_tele_block_staff", None,
         "tele_block_travel_forfail", "tele_block_impact", "magic_spell"),
    ]
    for name, attack, launch, travel, impact, profile in standard:
        append_cache_row(
            rows, kind="spell", key=name, style="magic", seq_ids=seq_ids,
            spot_ids=spot_ids, spot_defs=spot_defs, projanims=projanims,
            attack=attack, launch=launch, travel=travel, impact=impact,
            proj_anim=profile, note=f"b237:{name.lower().replace(' ', '_')}",
        )

    god_spells = [
        ("Saradomin Strike", "saradomin_lightning"),
        ("Flames of Zamorak", "zamorak_flame"),
        ("Claws of Guthix", "guthix_claw_green"),
    ]
    for name, impact in god_spells:
        append_cache_row(
            rows, kind="spell", key=name, style="magic", seq_ids=seq_ids,
            spot_ids=spot_ids, spot_defs=spot_defs, projanims=projanims,
            attack="human_casting", impact=impact,
            note=f"b237:{name.lower().replace(' ', '_')}:impact_only",
        )

    ancient_attack = "human_casting"
    ancient = [
        ("Smoke Rush", "smoke_rush_travel", "smoke_rush_impact"),
        ("Smoke Burst", "smoke_burst_travel", "smoke_burst_impact"),
        ("Smoke Blitz", "smoke_blitz_travel", "smoke_blitz_impact"),
        ("Smoke Barrage", "smoke_barrage_travel", "smoke_barrage_impact"),
        ("Shadow Rush", "shadow_rush_travel", "shadow_rush_impact"),
        ("Shadow Burst", None, "shadow_burst_impact"),
        ("Shadow Blitz", "shadow_blitz_travel", "shadow_blitz_impact"),
        ("Shadow Barrage", None, "shadow_barrage_impact"),
        ("Blood Rush", "blood_rush_travel", "blood_rush_impact"),
        ("Blood Burst", None, "spell_blood_burst_impact"),
        ("Blood Blitz", "blood_blitz_travel", "blood_blitz_impact"),
        ("Blood Barrage", None, "spell_blood_barrage_impact"),
        ("Ice Rush", "ice_rush_travel", "ice_rush_impact"),
        ("Ice Burst", "ice_burst_travel", "ice_burst_impact"),
        ("Ice Blitz", "ice_blitz_travel", "ice_blitz_impact"),
        ("Ice Barrage", "ice_barrage_travel", "ice_barrage_impact"),
    ]
    for name, travel, impact in ancient:
        profile = "magic_spell_low" if name.startswith("Ice ") else "magic_spell"
        append_cache_row(
            rows, kind="spell", key=name, style="magic", seq_ids=seq_ids,
            spot_ids=spot_ids, spot_defs=spot_defs, projanims=projanims,
            attack=ancient_attack, travel=travel, impact=impact,
            proj_anim=profile, note=f"b237:ancient:{name.lower().replace(' ', '_')}",
        )

    arceuus = [
        ("Ghostly Grasp", "human_spellcast_grasp",
         "ghostly_grasp_cast_spotanim", "ghostly_grasp_hit_spotanim"),
        ("Skeletal Grasp", "human_spellcast_grasp",
         "skeletal_grasp_cast_spotanim", "skeletal_grasp_hit_spotanim"),
        ("Undead Grasp", "human_spellcast_grasp",
         "undead_grasp_cast_spotanim", "undead_grasp_hit_spotanim"),
        ("Inferior Demonbane", "human_spellcast_demonbane",
         "inferior_demonbane_cast_spotanim", "inferior_demonbane_hit_spotanim"),
        ("Superior Demonbane", "human_spellcast_demonbane",
         "superior_demonbane_cast_spotanim", "superior_demonbane_hit_spotanim"),
        ("Dark Demonbane", "human_spellcast_demonbane",
         "dark_demonbane_cast_spotanim", "dark_demonbane_hit_spotanim"),
        ("Lesser Corruption", "human_spellcast_dmm",
         "lesser_corruption_cast_spotanim", "lesser_corruption_hit_spotanim"),
        ("Greater Corruption", "human_spellcast_dmm",
         "greater_corruption_cast_spotanim", "greater_corruption_hit_spotanim"),
    ]
    for name, attack, launch, impact in arceuus:
        append_cache_row(
            rows, kind="spell", key=name, style="magic", seq_ids=seq_ids,
            spot_ids=spot_ids, spot_defs=spot_defs, projanims=projanims,
            attack=attack, launch=launch, impact=impact,
            note=f"b237:arceuus:{name.lower().replace(' ', '_')}:impact_only",
        )


def export_curated_npc_rows(
    rows: list[list[str]],
    npc_ids: dict[str, int],
    seq_ids: dict[str, int],
    spot_ids: dict[str, int],
    spot_defs: dict[int, tuple[int, int]],
    projanims: dict[str, ProjAnimSpec],
) -> None:
    custom_profiles = {
        "graardor_slam": ProjAnimSpec(163, 146, 31, 16, 29, 11, 5),
        "steelwill_sonic": ProjAnimSpec(144, 124, 30, 16, 6, 64, 10),
        "dragonfire": ProjAnimSpec(172, 124, 41, 16, 0, 64, 5),
    }

    npc_rows = [
        ("godwars_bandos_avatar", "ranged", "godwars_bandos_ranged",
         "godwars_bandos_spot", "godwars_bandos_proj", "godwars_bandos_spot",
         custom_profiles["graardor_slam"], "voidps:bandos:graardor_ranged"),
        ("godwars_sergeant_goblin2", "magic",
         "godwars_sergeant_goblin2_sonic", "godwars_goblin2_sonic_attk_spot",
         "godwars_goblin2_sonic_attk_proj", "godwars_goblin2_sonic_impact",
         custom_profiles["steelwill_sonic"], "voidps:bandos:steelwill_magic"),
        ("godwars_sergeant_goblin3", "ranged",
         "godwars_sergeant_goblin3_ranged", "godwars_sergeant_goblin3_ranged",
         "godwars_goblin3_handaxe_proj", None, "thrown",
         "voidps:bandos:grimspike_ranged"),
        ("dagcave_magic_boss", "magic", "dragon_firebreath_attack",
         "firebreath_attack", "firebreath_travel", None,
         custom_profiles["dragonfire"], "voidps:kbd:dragonfire"),
        ("vorkath", "ranged", "ds2_vorkath_ranged", None,
         "vorkath_ranged_travel", "vorkath_ranged_impact", "bolt",
         "voidps:vorkath:ranged"),
        ("vorkath", "magic", "ds2_vorkath_ranged", None,
         "vorkath_magic_travel", "vorkath_magic_impact", "magic_spell",
         "voidps:vorkath:magic"),
        ("tzhaar_fightcave_swarm_boss", "magic", 2656,
         "tzhaar_firebreath_attack", "tzhaar_fire_launch_travel",
         "tzhaar_fire_launch_impact", custom_profiles["dragonfire"],
         "runelite:jad_magic"),
        ("tzhaar_fightcave_swarm_boss", "ranged", 2652,
         "tzhaar_ranged_fire_attack", "tzhaar_fire_spit_travel",
         "tzhaar_rock_smash", "thrown", "runelite:jad_ranged"),
    ]
    for npc_name, style, attack, launch, travel, impact, profile, note in npc_rows:
        npc_id = npc_ids.get(npc_name)
        if npc_id is None:
            continue
        append_cache_row(
            rows, kind="npc", key=npc_id, style=style, seq_ids=seq_ids,
            spot_ids=spot_ids, spot_defs=spot_defs, projanims=projanims,
            attack=attack, launch=launch, travel=travel, impact=impact,
            proj_anim=profile, note=note,
        )


def export_curated_special_rows(
    rows: list[list[str]],
    obj_ids: dict[str, int],
    seq_ids: dict[str, int],
    spot_ids: dict[str, int],
    spot_defs: dict[int, tuple[int, int]],
    projanims: dict[str, ProjAnimSpec],
) -> None:
    specials = [
        ("dragon_longsword", "slash", "cleave", "sp_attack_cleave_spotanim",
         None, "rsmod:special:dragon_longsword"),
        ("ags", "slash", "ags_special_player", "godwars_godsword_armadyl_spot",
         None, "b237:special:armadyl_godsword"),
        ("bgs", "slash", "bgs_special_player", "godwars_godsword_bandos_spot",
         None, "b237:special:bandos_godsword"),
        ("sgs", "slash", "sgs_special_player",
         "godwars_godsword_saradomin_spot", None,
         "b237:special:saradomin_godsword"),
        ("zgs", "slash", "zgs_special_player", "godwars_godsword_zamorak_spot",
         None, "b237:special:zamorak_godsword"),
        ("toxic_blowpipe_loaded", "ranged", "snakeboss_blowpipe_attack",
         "toxic_blowpipe_specialattack", None,
         "b237:special:toxic_blowpipe"),
    ]
    for obj_name, style, attack, launch, travel, note in specials:
        obj_id = obj_ids.get(obj_name)
        if obj_id is None:
            continue
        append_cache_row(
            rows, kind="special", key=obj_id, style=style, seq_ids=seq_ids,
            spot_ids=spot_ids, spot_defs=spot_defs, projanims=projanims,
            attack=attack, launch=launch, travel=travel,
            note=note,
        )

    darkbow_weapons = [
        "darkbow",
        "darkbow_green",
        "darkbow_blue",
        "darkbow_yellow",
        "darkbow_white",
        "br_darkbow",
        "bh_darkbow_imbue",
    ]
    aux_travel = spot_ids.get("darkbow_generic_smoke_arrow_flight", -1)
    aux_impact = spot_ids.get("darkbow_smoke_arrow_impact", -1)
    aux_model, aux_anim = spot_defs.get(aux_travel, (-1, -1))
    base = named_spec(projanims, "doublearrow_one")
    alt = named_spec(projanims, "doublearrow_two")
    if base and alt:
        extra = [
            "2",
            *projectile_columns(alt),
            value_or_dash(aux_travel),
            value_or_dash(aux_impact),
            value_or_dash(aux_model),
            value_or_dash(aux_anim),
            "1",
        ]
        for obj_name in darkbow_weapons:
            obj_id = obj_ids.get(obj_name)
            if obj_id is None:
                continue
            append_cache_row(
                rows, kind="special", key=obj_id, style="ranged",
                seq_ids=seq_ids, spot_ids=spot_ids, spot_defs=spot_defs,
                projanims=projanims, attack="human_bow",
                proj_anim=base, note=f"rsmod:special:{obj_name}:darkbow",
                extra=extra,
            )


def write_tsv(path: Path, rows: list[list[str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    dedup: dict[tuple[str, str, str, str], list[str]] = {}
    for row in rows:
        stance = row[33] if len(row) > 33 else "-"
        key = (row[0], row[1], row[2], row[3], stance)
        dedup.setdefault(key, row)
    ordered = sorted(
        dedup.values(),
        key=lambda r: (r[0], int(r[1]) if r[1].isdigit() else 1 << 30, r[1], r[2], r[3]),
    )
    width = len(HEADER_COLS)
    padded = [r + ["-"] * max(0, width - len(r)) for r in ordered]
    path.write_text(HEADER + "\n" + "\n".join("|".join(r[:width]) for r in padded) + "\n")


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
    npc_ids = read_symbol_map(symbols / "npc.sym")
    seq_ids = read_symbol_map(symbols / "seq.sym")
    spot_ids = read_spot_names(config / "dump.spot")
    spot_defs = read_spot_defs(config / "dump.spot", seq_ids)
    projanims = parse_projanims(args.rsmod_root / PROJANIMS)

    rows: list[list[str]] = []
    export_item_rows(rows, args.rsmod_root, obj_ids, spot_defs, projanims)
    export_elemental_spell_rows(
        rows, args.rsmod_root, seq_ids, spot_ids, spot_defs, projanims)
    export_curated_spell_rows(rows, seq_ids, spot_ids, spot_defs, projanims)
    export_curated_npc_rows(
        rows, npc_ids, seq_ids, spot_ids, spot_defs, projanims)
    export_curated_special_rows(
        rows, obj_ids, seq_ids, spot_ids, spot_defs, projanims)
    write_tsv(args.output, rows)
    print(f"wrote {len(rows)} source rows to {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
