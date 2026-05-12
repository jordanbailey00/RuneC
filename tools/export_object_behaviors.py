#!/usr/bin/env python3
"""Emit typed object behavior flags from object defs + transport rows."""
from __future__ import annotations

import json
import re
import struct
from collections import Counter
from pathlib import Path

from source_paths import DATA_OSRS

ROOT = Path(__file__).resolve().parents[1]
ODEF = ROOT / "data/defs/object_defs.bin"
OUT = ROOT / "data/defs/object_behaviors.bin"
REPORT = ROOT / "tools/reports/object_behaviors.txt"
TRANSPORTS = DATA_OSRS / "transports_osrs.json"
DUMP_LOC = ROOT / "tools/cache_pipeline/source/osrs-dumps/config/dump.loc"
DUMP_SEQ = ROOT / "tools/cache_pipeline/source/osrs-dumps/config/dump.seq"

OBHV_MAGIC = 0x5648424F  # OBHV
OBHV_VERSION = 2

B_DOOR = 1 << 0
B_LADDER = 1 << 1
B_STAIR = 1 << 2
B_BANK = 1 << 3
B_ALTAR = 1 << 4
B_RESOURCE = 1 << 5
B_TRANSPORT = 1 << 6
B_STORAGE = 1 << 7
B_PAIR_LEFT = 1 << 8
B_PAIR_RIGHT = 1 << 9

SKILL_NONE = 0
SKILL_WOODCUTTING = 1
SKILL_MINING = 2
SKILL_FISHING = 3
SKILL_FARMING = 4
SKILL_PRAYER = 5

COMMENT_RE = re.compile(r"^//\s+(\d+)")
SYMBOL_RE = re.compile(r"^\[([^\]]+)\]")


def read_odef() -> list[dict]:
    data = ODEF.read_bytes()
    magic, version, count = struct.unpack_from("<III", data, 0)
    if magic != 0x4645444F:
        raise SystemExit("bad object_defs.bin")
    if version not in (1, 2):
        raise SystemExit(f"unsupported object_defs.bin version {version}")
    pos = 12
    fmt = "<IHHBBBBBiiiII"
    size = struct.calcsize(fmt)
    extra_fmt = "<BBHiHH"
    extra_size = struct.calcsize(extra_fmt)
    rows: list[dict] = []
    for _ in range(count):
        rec = struct.unpack_from(fmt, data, pos)
        pos += size
        obj_id, width, length, interact_type, action_count, model_count, transform_count = rec[:7]
        varbit, varp, animation_id, map_icon, source_flags = rec[8:]
        param_count = 0
        if version >= 2:
            _supports_items, _clip_flags, param_count, _ambient_sound_id, \
                _ambient_sound_distance, _ambient_sound_retain = \
                struct.unpack_from(extra_fmt, data, pos)
            pos += extra_size
        name_len = struct.unpack_from("<H", data, pos)[0]
        pos += 2
        name = data[pos:pos + name_len].decode("latin-1", errors="replace")
        pos += name_len
        actions: list[str] = []
        for _slot in range(5):
            act_len = struct.unpack_from("<H", data, pos)[0]
            pos += 2
            actions.append(data[pos:pos + act_len].decode("latin-1", errors="replace"))
            pos += act_len
        pos += model_count * 4 + transform_count * 4
        pos += param_count * 8
        rows.append({
            "id": obj_id,
            "name": name,
            "width": width,
            "length": length,
            "interact_type": interact_type,
            "action_count": action_count,
            "varbit": varbit,
            "varp": varp,
            "animation_id": animation_id,
            "map_icon": map_icon,
            "source_flags": source_flags,
            "actions": actions,
        })
    return rows


def read_dump_entries(path: Path) -> list[dict]:
    if not path.is_file():
        return []
    entries: list[dict] = []
    current: dict | None = None

    def finish() -> None:
        if current is not None and "id" in current:
            entries.append(current.copy())

    for raw in path.read_text(errors="replace").splitlines():
        line = raw.strip()
        m = COMMENT_RE.match(line)
        if m:
            finish()
            current = {
                "id": int(m.group(1)),
                "symbol": "",
                "name": "",
                "actions": [""] * 5,
            }
            continue
        if current is None:
            continue
        m = SYMBOL_RE.match(line)
        if m and not current["symbol"]:
            current["symbol"] = m.group(1)
            continue
        if line.startswith("name="):
            current["name"] = line[5:]
            continue
        if line.startswith("op") and "=" in line:
            lhs, rhs = line.split("=", 1)
            try:
                slot = int(lhs[2:]) - 1
            except ValueError:
                continue
            if 0 <= slot < 5:
                current["actions"][slot] = rhs
    finish()
    return entries


def read_symbol_id(path: Path, symbol: str, default: int = -1) -> int:
    for entry in read_dump_entries(path):
        if entry.get("symbol") == symbol:
            return int(entry["id"])
    return default


def action_mask(actions: list[str], words: set[str]) -> int:
    mask = 0
    for i, action in enumerate(actions[:5]):
        key = action.lower()
        if key in words:
            mask |= 1 << i
    return mask


def row_is_dynamic_doorish(row: dict) -> bool:
    name = str(row.get("name", "")).lower()
    actions = [str(a).lower() for a in row.get("actions", [])]
    return (
        "door" in name
        or "gate" in name
        or "trapdoor" in name
        or any(a in {"open", "close", "unlock", "lock"} for a in actions)
    )


def pair_flags_for_symbol(symbol: str) -> int:
    s = symbol.lower()
    if not any(token in s for token in ("gate", "doubledoor",
                                        "double_inner", "doubler_inner")):
        return 0
    if "doubler" in s or "doorr" in s or "_right" in s or s.endswith("_r") \
            or "_r_" in s:
        return B_PAIR_RIGHT
    if "double_inner" in s or "doorl" in s or "_left" in s \
            or s.endswith("_l") or "_l_" in s:
        return B_PAIR_LEFT
    return 0


def closed_candidates_for_open_symbol(symbol: str) -> list[str]:
    if "open" not in symbol or "unopen" in symbol:
        return []
    candidates: set[str] = set()
    if symbol.startswith("opened"):
        candidates.add(symbol[6:])
    if symbol.startswith("open"):
        candidates.add(symbol[4:])
    if symbol.endswith("_opened"):
        base = symbol[:-7]
        candidates.update({base, f"{base}_closed", f"{base}closed"})
    if symbol.endswith("_open"):
        base = symbol[:-5]
        candidates.update({base, f"{base}_closed", f"{base}closed"})
    if symbol.endswith("opened"):
        base = symbol[:-6]
        candidates.update({base, f"{base}_closed", f"{base}closed"})
    if symbol.endswith("open"):
        base = symbol[:-4]
        candidates.update({base, f"{base}_closed", f"{base}closed"})
    if symbol.endswith("_open_m"):
        candidates.add(f"{symbol[:-7]}_m")
    if symbol.endswith("open_m"):
        candidates.add(f"{symbol[:-6]}_m")
    for token in ("_opened", "_open", "opened", "open"):
        if token in symbol:
            base = symbol.replace(token, "", 1)
            candidates.update({
                base,
                base.replace("__", "_"),
                symbol.replace(token, "_closed", 1).replace("__", "_"),
                symbol.replace(token, "closed", 1).replace("__", "_"),
            })
    return [c for c in candidates if c and c != symbol]


def derive_next_loc_stage(rows: list[dict]) -> dict[int, int]:
    row_by_id = {int(row["id"]): row for row in rows}
    entries = read_dump_entries(DUMP_LOC)
    symbol_to_id = {
        str(entry["symbol"]): int(entry["id"])
        for entry in entries
        if entry.get("symbol")
    }
    id_to_symbol = {
        int(entry["id"]): str(entry["symbol"])
        for entry in entries
        if entry.get("symbol")
    }
    next_stage: dict[int, int] = {}

    def add_pair(a: int, b: int) -> None:
        if a == b or a not in row_by_id or b not in row_by_id:
            return
        if not row_is_dynamic_doorish(row_by_id[a]):
            return
        if not row_is_dynamic_doorish(row_by_id[b]):
            return
        next_stage.setdefault(a, b)
        next_stage.setdefault(b, a)

    explicit_pairs = (
        ("poordoor", "poordooropen"),
        ("poordoor_m", "poordooropen_m"),
        ("poshdoor", "poshdooropen"),
        ("castledoubledoorl", "opencastledoubledoorl"),
        ("castledoubledoorr", "opencastledoubledoorr"),
        ("fencegate_l", "openfencegate_l"),
        ("fencegate_r", "openfencegate_r"),
        ("rustic_fencegate_l", "rustic_openfencegate_l"),
        ("rustic_fencegate_r", "rustic_openfencegate_r"),
        ("qip_sheep_shearer_fencegate_l", "qip_sheep_shearer_openfencegate_l"),
        ("qip_sheep_shearer_fencegate_r", "qip_sheep_shearer_openfencegate_r"),
    )
    for closed, opened in explicit_pairs:
        if closed in symbol_to_id and opened in symbol_to_id:
            add_pair(symbol_to_id[closed], symbol_to_id[opened])

    for opened, open_id in symbol_to_id.items():
        for closed in closed_candidates_for_open_symbol(opened):
            closed_id = symbol_to_id.get(closed)
            if closed_id is not None:
                add_pair(closed_id, open_id)
                break

    for i, entry in enumerate(entries):
        obj_id = int(entry["id"])
        row = row_by_id.get(obj_id)
        if not row or not row_is_dynamic_doorish(row):
            continue
        actions = [str(a).lower() for a in row["actions"]]
        if "open" not in actions and "unlock" not in actions:
            continue
        for other in entries[i + 1:i + 5]:
            other_id = int(other["id"])
            other_row = row_by_id.get(other_id)
            if not other_row or not row_is_dynamic_doorish(other_row):
                continue
            other_actions = [str(a).lower() for a in other_row["actions"]]
            other_symbol = id_to_symbol.get(other_id, "")
            same_display_name = row["name"] == other_row["name"]
            if same_display_name and "close" in other_actions and "open" in other_symbol:
                add_pair(obj_id, other_id)
                break
    return next_stage


def is_non_transport_bank_row(row: dict) -> bool:
    action = str(row.get("menuOption", "")).strip().lower()
    target = str(row.get("menuTarget", "")).strip().lower()
    if action in {"bank", "collect", "deposit", "deposit-box"}:
        return True
    return "bank" in target and action in {"use", "bank", "collect", "deposit"}


def load_transport_ids() -> set[int]:
    if not TRANSPORTS.is_file():
        return set()
    ids: set[int] = set()
    for row in json.loads(TRANSPORTS.read_text()):
        if row.get("Category") != "GAME_OBJECT":
            continue
        if is_non_transport_bank_row(row):
            continue
        try:
            ids.add(int(row.get("id")))
        except (TypeError, ValueError):
            pass
    return ids


def classify(row: dict, transport_ids: set[int]) -> tuple[int, int, int]:
    name = row["name"].lower()
    actions = [a.lower() for a in row["actions"]]
    flags = 0
    mask = 0
    skill = SKILL_NONE

    door_words = {"open", "close", "unlock", "lock"}
    ladder_words = {"climb", "climb-up", "climb-down"}
    bank_words = {"bank", "collect"}
    storage_words = {"deposit", "deposit-box", "store", "store-plunder",
                     "private", "shared"}
    altar_words = {"pray", "pray-at", "recharge", "offer", "worship"}
    wood_words = {"chop", "chop down", "chop-down", "cut"}
    mining_words = {"mine", "prospect"}
    fishing_words = {"fish", "net", "bait", "lure", "harpoon", "cage"}
    farming_words = {"pick", "harvest", "prune", "clear", "rake", "water"}
    transport_words = {
        "enter", "exit", "cross", "climb", "climb-up", "climb-down",
        "pass-through", "squeeze-through", "travel", "teleport",
        "board", "leave",
    }

    if "door" in name or "gate" in name or action_mask(actions, door_words):
        flags |= B_DOOR
        mask |= action_mask(actions, door_words)
    if "ladder" in name:
        flags |= B_LADDER | B_TRANSPORT
        mask |= action_mask(actions, ladder_words)
    if any(w in name for w in ("stair", "stairs", "staircase", "steps")):
        flags |= B_STAIR | B_TRANSPORT
        mask |= action_mask(actions, ladder_words | transport_words)
    bank_mask = action_mask(actions, bank_words)
    if any(w in name for w in ("bank booth", "bank chest", "bank box",
                               "bank counter", "grand exchange booth")):
        bank_mask |= action_mask(actions, bank_words | {"use"})
    if bank_mask:
        flags |= B_BANK
        mask |= bank_mask
    storage_mask = action_mask(actions, storage_words)
    if "storage" in name:
        storage_mask |= action_mask(actions, storage_words | {"open", "check",
                                                            "take"})
    if storage_mask or any(w in name for w in (
            "bank deposit", "deposit box", "deposit chest", "storage")):
        flags |= B_STORAGE
        mask |= storage_mask
    if "altar" in name or action_mask(actions, altar_words):
        flags |= B_ALTAR
        mask |= action_mask(actions, altar_words)
        if skill == SKILL_NONE:
            skill = SKILL_PRAYER
    if action_mask(actions, wood_words) or "tree" in name and "chop" in " ".join(actions):
        flags |= B_RESOURCE
        mask |= action_mask(actions, wood_words)
        skill = SKILL_WOODCUTTING
    if action_mask(actions, mining_words) or "rock" in name and "mine" in actions:
        flags |= B_RESOURCE
        mask |= action_mask(actions, mining_words)
        skill = SKILL_MINING
    if action_mask(actions, fishing_words):
        flags |= B_RESOURCE
        mask |= action_mask(actions, fishing_words)
        skill = SKILL_FISHING
    if action_mask(actions, farming_words):
        flags |= B_RESOURCE
        mask |= action_mask(actions, farming_words)
        if skill == SKILL_NONE:
            skill = SKILL_FARMING
    if row["id"] in transport_ids or action_mask(actions, transport_words):
        flags |= B_TRANSPORT
        mask |= action_mask(actions, transport_words)
    return flags, mask, skill


def main() -> int:
    rows = read_odef()
    transport_ids = load_transport_ids()
    symbol_by_id = {
        int(entry["id"]): str(entry["symbol"])
        for entry in read_dump_entries(DUMP_LOC)
        if entry.get("symbol")
    }
    next_loc_stage = derive_next_loc_stage(rows)
    default_climb_anim = read_symbol_id(DUMP_SEQ, "human_reachforladder", 828)
    out_rows: list[tuple[int, int, int, int, int, int, int, int]] = []
    counts: Counter[str] = Counter()
    skill_counts: Counter[int] = Counter()
    next_stage_rows = 0
    climb_anim_rows = 0
    pair_rows = 0
    for row in rows:
        flags, mask, skill = classify(row, transport_ids)
        flags |= pair_flags_for_symbol(symbol_by_id.get(int(row["id"]), ""))
        if not flags:
            continue
        next_stage = next_loc_stage.get(int(row["id"]), -1)
        climb_anim = -1
        if flags & (B_LADDER | B_STAIR):
            climb_anim = default_climb_anim
        if next_stage >= 0:
            next_stage_rows += 1
        if climb_anim >= 0:
            climb_anim_rows += 1
        if flags & (B_PAIR_LEFT | B_PAIR_RIGHT):
            pair_rows += 1
        out_rows.append((row["id"], flags, next_stage, -1, -1, climb_anim,
                         mask, skill))
        if flags & B_DOOR:
            counts["door"] += 1
        if flags & B_LADDER:
            counts["ladder"] += 1
        if flags & B_STAIR:
            counts["stair"] += 1
        if flags & B_BANK:
            counts["bank"] += 1
        if flags & B_ALTAR:
            counts["altar"] += 1
        if flags & B_RESOURCE:
            counts["resource"] += 1
            skill_counts[skill] += 1
        if flags & B_TRANSPORT:
            counts["transport"] += 1
        if flags & B_STORAGE:
            counts["storage"] += 1
        if flags & B_PAIR_LEFT:
            counts["pair_left"] += 1
        if flags & B_PAIR_RIGHT:
            counts["pair_right"] += 1

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", OBHV_MAGIC, OBHV_VERSION, len(out_rows)))
        for obj_id, flags, next_stage, open_sound, close_sound, climb_anim, \
                mask, skill in sorted(out_rows):
            f.write(struct.pack("<IIiiiiBBH", obj_id, flags, next_stage,
                                open_sound, close_sound, climb_anim, mask,
                                skill, 0))

    skill_names = {
        SKILL_NONE: "none",
        SKILL_WOODCUTTING: "woodcutting",
        SKILL_MINING: "mining",
        SKILL_FISHING: "fishing",
        SKILL_FARMING: "farming",
        SKILL_PRAYER: "prayer",
    }
    lines = [
        "Object behavior rules",
        "",
        "status: READY_WITH_ACCEPTED_SIMPLIFICATIONS",
        f"output binary: data/defs/object_behaviors.bin ({OUT.stat().st_size} bytes)",
        f"source object defs: {ODEF.relative_to(ROOT)}",
        f"source transports: {TRANSPORTS.relative_to(ROOT) if TRANSPORTS.exists() else 'missing'}",
        f"behavior rows: {len(out_rows)}",
        f"next_loc_stage rows: {next_stage_rows}",
        f"climb_anim rows: {climb_anim_rows}",
        f"paired dynamic loc rows: {pair_rows}",
        "",
        "by behavior:",
    ]
    for key in ("door", "ladder", "stair", "bank", "altar", "resource",
                "transport", "storage", "pair_left", "pair_right"):
        lines.append(f"  {key:<12} {counts[key]}")
    lines.extend(["", "resource skills:"])
    for skill_id, count in sorted(skill_counts.items()):
        lines.append(f"  {skill_names.get(skill_id, str(skill_id)):<12} {count}")
    lines.extend([
        "",
        "accepted simplifications:",
        "  - behavior rows classify object capabilities and carry generic dynamic loc metadata",
        "  - next_loc_stage is derived from source dump symbols plus RSMod-style generic door/gate pairs",
        "  - open/close sound fields are reserved until a local synth-name source is available",
        "  - generic altar prayer restore, door state, and gathering-node action start/depletion/respawn are runtime-owned",
        "  - level/tool requirements and exact skilling success rolls land in the per-skill action rules",
        "  - paired door/gate state is derived from source dump symbols and applied placement-locally at runtime",
    ])
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text("\n".join(lines) + "\n")
    print(REPORT.read_text())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
