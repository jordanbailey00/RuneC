#!/usr/bin/env python3
"""Scrape slayer-master task assignment tables.

The runtime needs assignment weights plus exact eligibility gates, task
amounts, extended amounts, alternatives, and unlock text. Cached wiki pages
are preferred; network refetching is not required for normal exports.

Binary format v3 — 'SLAY' magic:
  magic u32 | version u32 | master_count u32
  per master:
    name_len u8 + name[]
    task_count u16
    req_slayer u8
    req_combat u8
    per task:
      weight u16
      amount_min u16
      amount_max u16
      extended_min u16
      extended_max u16
      unlock_flags u32
      req_slayer u8
      req_combat u8
      name_len u8 + npc_name[]
      alternatives_len u8 + alternatives[]
      requirements_len u8 + requirements[]
      progression_flags u64
      progression_any_flags u64
      task_flags u16
      locations_len u16 + "name|flags;..."[]
      boss_candidates_len u16 + "name|slayer|flags|any_flags;..."[]

npc_name is the canonical string (resolved at runtime against
`npc_id` bucket / infobox_monster for ID lookups).
"""
from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

sys.path.insert(0, str(Path(__file__).resolve().parent))
from wiki_pages import PageClient  # noqa: E402

OUT = ROOT / "data/defs/slayer.bin"
REPORT = ROOT / "tools/reports/slayer.txt"

SLAY_MAGIC = 0x59414C53  # 'SLAY'
SLAY_VERSION = 3

TASK_HAS_LOCATIONS = 1 << 0
TASK_BOSS_SECOND_ROLL = 1 << 1
TASK_HAS_ALTERNATIVES = 1 << 2

UNLOCK_FLAGS = {
    "Like a boss": 1 << 1,
    "Watch the birdie": 1 << 2,
    "Basilocked": 1 << 3,
    "Reptile got ripped": 1 << 4,
    "Seeing red": 1 << 5,
    "Hot stuff": 1 << 6,
    "Actual Vampyre Slayer": 1 << 7,
    "Warped Reality": 1 << 8,
    "Lured In": 1 << 9,
    "Wings Spread": 1 << 10,
}
QUEST_GATE_FLAG = 1 << 29
SAILING_FLAG = 1 << 30

PROGRESSION_FLAGS = {
    "Priest in Peril": 1 << 0,
    "Dragon Slayer I": 1 << 1,
    "Dragon Slayer II": 1 << 2,
    "Bone Voyage": 1 << 3,
    "Elemental Workshop I": 1 << 4,
    "Lost City": 1 << 5,
    "Olaf's Quest": 1 << 6,
    "Horror from the Deep": 1 << 7,
    "Mourning's End Part II": 1 << 8,
    "Desert Treasure I": 1 << 9,
    "Desert Treasure II - The Fallen Empire": 1 << 10,
    "Song of the Elves": 1 << 11,
    "Regicide": 1 << 12,
    "Perilous Moons": 1 << 13,
    "Waterfall Quest": 1 << 14,
    "Watchtower": 1 << 15,
    "Fairytale II - Cure a Queen": 1 << 16,
    "Barbarian Training": 1 << 17,
    "The Frozen Door": 1 << 18,
    "Secrets of the North": 1 << 19,
    "Death Plateau": 1 << 20,
    "Troubled Tortugans": 1 << 21,
    "Enter the Abyss": 1 << 22,
    "Cabin Fever": 1 << 23,
    "Rum Deal": 1 << 24,
    "Ernest the Chicken": 1 << 25,
    "Skippy and the Mogres": 1 << 26,
    "Death to the Dorgeshuun": 1 << 27,
    "Contact!": 1 << 28,
    "Royal Trouble": 1 << 29,
    "Legends' Quest": 1 << 30,
    "Lunar Diplomacy": 1 << 31,
    "A Porcine of Interest": 1 << 32,
    "Haunted Mine": 1 << 33,
    "Shadows of Custodia": 1 << 34,
    "The Fremennik Exiles": 1 << 35,
}

LOCATION_FLAGS = {
    "Brine Rat Cavern": PROGRESSION_FLAGS["Olaf's Quest"],
    "Evil Chicken's Lair": PROGRESSION_FLAGS["Dragon Slayer I"],
    "Fossil Island": PROGRESSION_FLAGS["Bone Voyage"],
    "Iorwerth Dungeon": PROGRESSION_FLAGS["Song of the Elves"],
    "Jormungand's Prison": PROGRESSION_FLAGS["The Fremennik Exiles"],
    "Lithkren Vault": PROGRESSION_FLAGS["Dragon Slayer II"],
    "Mourner Tunnels": PROGRESSION_FLAGS["Mourning's End Part II"],
    "Myths' Guild Dungeon": PROGRESSION_FLAGS["Dragon Slayer II"],
    "Neypotzli": PROGRESSION_FLAGS["Perilous Moons"],
    "Ogre Enclave": PROGRESSION_FLAGS["Watchtower"],
    "Poison Waste Dungeon": PROGRESSION_FLAGS["Regicide"],
    "Smoke Dungeon": PROGRESSION_FLAGS["Desert Treasure I"],
    "Waterfall Dungeon": PROGRESSION_FLAGS["Waterfall Quest"],
    "Wyvern Cave": (
        PROGRESSION_FLAGS["Bone Voyage"] |
        PROGRESSION_FLAGS["Elemental Workshop I"]
    ),
    "Zanaris": PROGRESSION_FLAGS["Lost City"],
}

MASTER_REQUIREMENTS = {
    "Turael": (0, 0),
    "Spria": (0, 0),
    "Mazchna": (0, 20),
    "Vannaka": (0, 40),
    "Chaeldar": (0, 70),
    "Nieve": (0, 85),
    "Steve": (0, 85),
    "Konar quo Maten": (0, 75),
    "Duradel": (50, 100),
    "Krystilia": (0, 0),
    "Achtryn": (0, 0),
    "Aya": (0, 0),
}

# Modern OSRS slayer masters (canonical page titles).
MASTERS = [
    "Turael",
    "Spria",
    "Mazchna",
    "Vannaka",
    "Chaeldar",
    "Nieve",
    "Steve",            # Nieve's replacement after Monkey Madness II
    "Konar quo Maten",
    "Duradel",
    "Krystilia",
    "Achtryn",          # Varlamore
    "Aya",              # Varlamore
]

# Weight template: {{+=|weight|N|echo=2}} → N
_WEIGHT_RE = re.compile(r"\{\{\s*\+=\s*\|\s*weight\s*\|\s*(\d+)", re.IGNORECASE)
# First [[link]] in a row — the monster name (may have |display form).
_FIRST_LINK_RE = re.compile(r"\[\[([^\]|#]+?)(?:\||#|\]\])")
# Transclusion of another page as the section body: {{:Pagename}}.
_TRANSCLUDE_RE = re.compile(r"\{\{\s*:([^|}]+?)\s*(?:\||\})", re.DOTALL)


def extract_tasks(wt: str, client: "PageClient | None" = None,
                  _visited: set[str] | None = None,
                  boss_candidates: str = ""
                  ) -> list[dict[str, object]]:
    """Return [(npc_name, weight), ...] from the ==Tasks== section.

    If the section is just a `{{:Other page}}` transclusion, follow it."""
    # Capture from "==Tasks==" until the next level-2 heading or EOF.
    m = re.search(r"==\s*Tasks\s*==(.*?)(?=\n==[^=]|\Z)",
                  wt, re.DOTALL | re.IGNORECASE)
    if not m:
        return []
    section = m.group(1)

    # If the section transcludes another page, recurse into it.
    if client is not None:
        tm = _TRANSCLUDE_RE.search(section)
        if tm:
            target = tm.group(1).strip()
            _visited = _visited or set()
            if target not in _visited and len(_visited) < 5:
                _visited.add(target)
                try:
                    sub_wt = client.wikitext(target)
                except Exception:
                    sub_wt = ""
                if sub_wt:
                    # Pretend the subpage is wrapped in ==Tasks== so the
                    # regex above matches on recurse.
                    return extract_tasks(
                        "==Tasks==\n" + sub_wt, client, _visited,
                        boss_candidates) or \
                        _extract_from_text(sub_wt, boss_candidates)
    return _extract_from_text(section, boss_candidates)


def _strip_markup(text: str) -> str:
    text = re.sub(r"<!--.*?-->", "", text, flags=re.DOTALL)
    text = re.sub(r"<ref\b[^>/]*?>.*?</ref>", "", text, flags=re.DOTALL)
    text = re.sub(r"<ref\b[^/]*/>", "", text)
    text = re.sub(r"\{\{NA(?:\|[^}]*)?\}\}", "", text, flags=re.IGNORECASE)
    text = re.sub(r"\{\{SCP\|([^|}]+)\|?([^}]*)\}\}", r"\1 \2", text)
    text = re.sub(r"\{\{[^{}]*\}\}", "", text)
    text = re.sub(r"<[^>]+>", "", text)
    text = re.sub(r"\s+", " ", text)
    return text.strip(" |")


def _link_targets(text: str) -> list[str]:
    out: list[str] = []
    for m in re.finditer(r"\[\[([^\]|#]+)(?:#[^\]|]*)?(?:\|[^\]]*)?\]\]",
                         text):
        target = m.group(1).strip()
        if target and target not in out:
            out.append(target)
    return out


def _progression_flags(text: str,
                       skip_access_clauses: bool = True
                       ) -> tuple[int, int]:
    flags = 0
    any_flags = 0
    seen: list[int] = []
    for m in re.finditer(r"\[\[([^\]|#]+)(?:#[^\]|]*)?(?:\|[^\]]*)?\]\]",
                         text):
        target = m.group(1).strip()
        bit = PROGRESSION_FLAGS.get(target, 0)
        if not bit:
            continue
        after = text[m.end():m.end() + 80].lower()
        if skip_access_clauses and after.lstrip().startswith("for access to"):
            continue
        seen.append(bit)

    plain = re.sub(r"'+", "", text)
    has_or = re.search(r"\]\].{0,48}\bor\b.{0,48}\[\[",
                       plain, re.IGNORECASE) is not None
    if has_or and len(seen) > 1:
        for bit in seen:
            any_flags |= bit
    else:
        for bit in seen:
            flags |= bit
    return flags, any_flags


def _location_tokens(text: str) -> str:
    tokens: list[str] = []
    for loc in _link_targets(text):
        flags = LOCATION_FLAGS.get(loc, 0)
        token = f"{loc}|0x{flags:x}"
        if token not in tokens:
            tokens.append(token)
    return ";".join(tokens)


def _row_cells(row: str) -> list[str]:
    cells: list[str] = []
    for raw in row.splitlines():
        line = raw.strip()
        if line.startswith("|}"):
            continue
        if line.startswith("|"):
            line = line[1:].strip()
            if line.startswith("data-sort-value") and "|" in line:
                line = line.split("|", 1)[1].strip()
            cells.append(line)
        elif cells and line:
            cells[-1] += "\n" + line
    return cells


def _header_columns(header: str) -> list[str]:
    cols: list[str] = []
    for raw in header.splitlines():
        line = raw.strip()
        if not line.startswith("!"):
            continue
        line = line[1:].strip()
        if "|" in line:
            line = line.split("|")[-1].strip()
        cols.append(_strip_markup(line).lower())
    return cols


def _range(text: str) -> tuple[int, int]:
    text = _strip_markup(text)
    m = re.search(r"(\d+)\s*-\s*(\d+)", text)
    if m:
        return int(m.group(1)), int(m.group(2))
    m = re.search(r"\b(\d+)\b", text)
    if m:
        value = int(m.group(1))
        return value, value
    return 0, 0


def _requirements(text: str) -> tuple[int, int, int, int, int, str]:
    req_slayer = 0
    req_combat = 0
    flags = 0
    for skill, value in re.findall(r"\{\{SCP\|([^|}]+)\|(\d+)", text):
        skill_l = skill.strip().lower()
        level = int(value)
        if skill_l == "slayer":
            req_slayer = max(req_slayer, level)
        elif skill_l == "combat":
            req_combat = max(req_combat, level)
        elif skill_l == "sailing":
            flags |= SAILING_FLAG
    for phrase, bit in UNLOCK_FLAGS.items():
        if phrase.lower() in text.lower():
            flags |= bit
    prog_flags, prog_any_flags = _progression_flags(text)
    has_progress_text = re.search(
        r"\b(completion|partial completion|started|progressed)\b",
        text, re.IGNORECASE) is not None
    if has_progress_text and "for access to" not in text.lower() and \
            not (prog_flags or prog_any_flags):
        flags |= QUEST_GATE_FLAG
    return (req_slayer, req_combat, flags, prog_flags, prog_any_flags,
            _strip_markup(text))


def _boss_slayer_candidates(client: "PageClient") -> str:
    try:
        wt = client.wikitext("Boss")
    except Exception:
        return ""
    marker = "The following bosses can be assigned:"
    start = wt.find(marker)
    if start < 0:
        return ""
    table_start = wt.find("{|", start)
    table_end = wt.find("|}", table_start)
    if table_start < 0 or table_end < 0:
        return ""
    table = wt[table_start:table_end]
    tokens: list[str] = []
    for row in table.split("|-")[1:]:
        cells = _row_cells(row)
        if len(cells) < 4:
            continue
        links = _link_targets(cells[0])
        if not links:
            continue
        name = links[0]
        req_slayer, _, _, prog, any_prog, _ = _requirements(cells[3])
        slayer_from_col, _ = _range(cells[2])
        req_slayer = max(req_slayer, slayer_from_col)
        token = f"{name}|{req_slayer}|0x{prog:x}|0x{any_prog:x}"
        if token not in tokens:
            tokens.append(token)
    return ";".join(tokens)


def _extract_from_text(section: str,
                       boss_candidates: str = "") -> list[dict[str, object]]:
    out: list[dict[str, object]] = []
    first_row = section.find("|-")
    header = section[:first_row] if first_row >= 0 else section
    cols = _header_columns(header)
    amount_idx = next((i for i, c in enumerate(cols)
                       if "amount" in c and "extended" not in c), 1)
    location_idx = next((i for i, c in enumerate(cols)
                         if "location" in c), -1)
    ext_idx = next((i for i, c in enumerate(cols)
                    if "extended" in c), -1)
    req_idx = next((i for i, c in enumerate(cols)
                    if "unlock" in c), 3 if ext_idx >= 0 else 2)
    alt_idx = next((i for i, c in enumerate(cols)
                    if "alternative" in c), 4 if ext_idx >= 0 else 3)
    # Rows delimited by "|-". The first row is the header, skip it.
    rows = section.split("|-")
    for row in rows[1:]:
        weight_match = _WEIGHT_RE.search(row)
        if not weight_match:
            continue
        weight = int(weight_match.group(1))
        cells = _row_cells(row)
        if len(cells) < 5:
            continue
        link_match = _FIRST_LINK_RE.search(cells[0])
        if not link_match:
            continue
        npc_name = link_match.group(1).strip()
        # Skip non-monster rows (e.g. linked quests, skill cats).
        if not npc_name or npc_name.lower() in {"combat", "slayer",
                                                "attack", "defence"}:
            continue
        required_idx = max(amount_idx, req_idx, alt_idx, ext_idx)
        if len(cells) <= required_idx:
            continue
        req_cell = cells[req_idx]
        alt_cell = cells[alt_idx]
        req_slayer, req_combat, flags, prog, prog_any, req_text = \
            _requirements(req_cell)
        amount_min, amount_max = _range(cells[amount_idx])
        ext_min, ext_max = _range(cells[ext_idx]) if ext_idx >= 0 else (0, 0)
        alternatives = ", ".join(_link_targets(alt_cell))
        locations = (_location_tokens(cells[location_idx])
                     if location_idx >= 0 and location_idx < len(cells)
                     else "")
        task_flags = 0
        if locations:
            task_flags |= TASK_HAS_LOCATIONS
        if alternatives:
            task_flags |= TASK_HAS_ALTERNATIVES
        task_boss_candidates = ""
        if "boss" in npc_name.lower():
            task_flags |= TASK_BOSS_SECOND_ROLL
            task_boss_candidates = boss_candidates
        out.append({
            "name": npc_name,
            "weight": weight,
            "amount_min": amount_min,
            "amount_max": amount_max,
            "extended_min": ext_min,
            "extended_max": ext_max,
            "req_slayer": req_slayer,
            "req_combat": req_combat,
            "unlock_flags": flags,
            "progression_flags": prog,
            "progression_any_flags": prog_any,
            "task_flags": task_flags,
            "alternatives": alternatives,
            "locations": locations,
            "boss_candidates": task_boss_candidates,
            "requirements": req_text,
        })
    return out


def pack_short(s: str, limit: int = 255) -> bytes:
    return s.encode("latin-1", errors="replace")[:limit]


def write_str16(f, s: str, limit: int) -> None:
    b = pack_short(s, limit)
    f.write(struct.pack("<H", len(b)))
    f.write(b)


def main():
    c = PageClient()
    boss_candidates = _boss_slayer_candidates(c)

    per_master: dict[str, list[dict[str, object]]] = {}
    for m in MASTERS:
        try:
            wt = c.wikitext(m)
        except Exception as e:
            print(f"  skip {m}: {e}", file=sys.stderr)
            continue
        tasks = extract_tasks(wt, client=c, boss_candidates=boss_candidates)
        per_master[m] = tasks
        print(f"  {m}: {len(tasks)} tasks", file=sys.stderr)

    # Emit binary
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", SLAY_MAGIC, SLAY_VERSION,
                            len(per_master)))
        for master in sorted(per_master):
            mb = pack_short(master, 47)
            tasks = per_master[master]
            f.write(struct.pack("<B", len(mb))); f.write(mb)
            f.write(struct.pack("<H", min(65535, len(tasks))))
            req_slayer, req_combat = MASTER_REQUIREMENTS.get(master, (0, 0))
            f.write(struct.pack("<BB", req_slayer, req_combat))
            for task in tasks[:65535]:
                nb = pack_short(str(task["name"]), 63)
                ab = pack_short(str(task["alternatives"]), 127)
                rb = pack_short(str(task["requirements"]), 191)
                lb = str(task["locations"])
                bb = str(task["boss_candidates"])
                f.write(struct.pack(
                    "<HHHHHIBB",
                    min(65535, int(task["weight"])),
                    min(65535, int(task["amount_min"])),
                    min(65535, int(task["amount_max"])),
                    min(65535, int(task["extended_min"])),
                    min(65535, int(task["extended_max"])),
                    int(task["unlock_flags"]),
                    min(255, int(task["req_slayer"])),
                    min(255, int(task["req_combat"])),
                ))
                f.write(struct.pack("<B", len(nb))); f.write(nb)
                f.write(struct.pack("<B", len(ab))); f.write(ab)
                f.write(struct.pack("<B", len(rb))); f.write(rb)
                f.write(struct.pack(
                    "<QQH",
                    int(task["progression_flags"]),
                    int(task["progression_any_flags"]),
                    int(task["task_flags"]),
                ))
                write_str16(f, lb, 511)
                write_str16(f, bb, 2047)
    print(f"  → {OUT} ({OUT.stat().st_size} bytes)", file=sys.stderr)

    # Report
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    with REPORT.open("w") as f:
        broad_unknown = 0
        exact_progression = 0
        location_tasks = 0
        boss_tasks = 0
        for master in sorted(per_master):
            tasks = per_master[master]
            total = sum(int(t["weight"]) for t in tasks)
            req_slayer, req_combat = MASTER_REQUIREMENTS.get(master, (0, 0))
            f.write(f"=== {master} — {len(tasks)} tasks, "
                    f"total weight {total}, req slayer {req_slayer}, "
                    f"req combat {req_combat} ===\n")
            for task in sorted(tasks, key=lambda t: -int(t["weight"])):
                amount = f"{task['amount_min']}-{task['amount_max']}"
                ext = f"{task['extended_min']}-{task['extended_max']}"
                f.write(f"  {int(task['weight']):4}  {task['name']} "
                        f"amount={amount} extended={ext} "
                        f"req_slayer={task['req_slayer']} "
                        f"req_combat={task['req_combat']} "
                        f"flags=0x{int(task['unlock_flags']):x} "
                        f"prog=0x{int(task['progression_flags']):x} "
                        f"any=0x{int(task['progression_any_flags']):x} "
                        f"task_flags=0x{int(task['task_flags']):x}\n")
                if int(task["unlock_flags"]) & QUEST_GATE_FLAG:
                    broad_unknown += 1
                if int(task["progression_flags"]) or \
                        int(task["progression_any_flags"]):
                    exact_progression += 1
                if int(task["task_flags"]) & TASK_HAS_LOCATIONS:
                    location_tasks += 1
                if int(task["task_flags"]) & TASK_BOSS_SECOND_ROLL:
                    boss_tasks += 1
            f.write("\n")
        f.write("=== summary ===\n")
        f.write(f"exact progression-gated task rows: {exact_progression}\n")
        f.write(f"broad unknown progression rows: {broad_unknown}\n")
        f.write(f"Konar/location-token task rows: {location_tasks}\n")
        f.write(f"boss second-roll task rows: {boss_tasks}\n")
        f.write(f"boss second-roll candidates: "
                f"{len([x for x in boss_candidates.split(';') if x])}\n")
    print(f"  → {REPORT}", file=sys.stderr)


if __name__ == "__main__":
    main()
