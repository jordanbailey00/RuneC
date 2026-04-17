#!/usr/bin/env python3
"""Export NPC definitions, models, and spawns for a region.

NPC spawn data source:
  The b237 OSRS client cache does NOT contain NPC spawn positions — RSMod's
  own `MapNpcListEncoder.kt` confirms: "Map npc spawns are a server-only group".

  We therefore read authoritative spawns from 2011Scape's per-region Kotlin
  spawn plugins (`game/plugins/.../areas/spawns/spawns_{regionId}.plugin.kts`).
  Each call looks like:
      spawn_npc(npc = Npcs.BANKER_CLASSIC_MALE_PURPLE_44, x=3251, z=3418,
                height=0, walkRadius=5, direction=Direction.NORTH)
  The `_NNN` suffix on NPC names is the 2011-era cache ID (≠ b237 ID), so we
  match by BASE NAME (stripping variant descriptors + numeric suffix) against
  b237 NPC display names. Named characters like AUBURY, LOWE keep their exact
  name; generic roles like BANKER_*, GUARD_*, MAN_* strip to the base role.

  Definitions, stats, anims, models, recolors come from the b237 cache —
  only the (name, x, y, plane, walkRadius, direction) tuple comes from 2011Scape.

Writes three binary files:
  - data/defs/npc_defs.bin         — NDEF (NPC definitions)
  - data/regions/*.npc-spawns.bin  — NSPN (NPC spawns, world coords)
  - data/models/npcs.models        — MDL2 (one composite mesh per unique NPC)
"""
import sys, struct, re, argparse, io
from pathlib import Path

SCRIPTS = Path("/home/joe/projects/runescape-rl-reference/valo_envs/ocean/osrs/scripts")
SCAPE_2011 = Path("/home/joe/projects/runescape-rl-reference/2011Scape-game")
SPAWNS_DIR = SCAPE_2011 / "game/plugins/src/main/kotlin/gg/rsmod/plugins/content/areas/spawns"
sys.path.insert(0, str(SCRIPTS))

from modern_cache_reader import ModernCacheReader, read_u8, read_u16, read_u32
from export_models import decode_model, load_model_modern, expand_model
from export_terrain import load_texture_average_colors_modern

NDEF_MAGIC = 0x4E444546
NSPN_MAGIC = 0x4E53504E
MDL2_MAGIC = 0x4D444C32


# ----- b237 cache: scan all NPC defs for name → ID map -----

def build_b237_name_map(reader):
    files = reader.read_group(2, 9)
    name_map = {}
    id_to_name = {}
    for nid, data in files.items():
        try:
            d = parse_npc_def(data)
            if d["name"]:
                name_map.setdefault(d["name"].lower(), []).append(nid)
                id_to_name[nid] = d["name"]
        except Exception:
            pass
    return name_map, id_to_name


# Kotlin-style descriptor tokens that appear in 2011Scape NPC names but are
# NOT part of the b237 display name. Strip these when collapsing to a base.
VARIANT_TOKENS = {
    "CLASSIC", "LATEST", "MODERN",
    "MALE", "FEMALE",
    "PURPLE", "GREY", "GRAY", "BLACKSUIT", "RED", "BLUE", "GREEN",
    "HANDSBEHIND", "HANDSINPOCKETS", "SPIKEYHAIR",
    "STANDING", "SITTING", "WALKING",
}

NUM_SUFFIX_RE = re.compile(r"_(\d+)$")


def kotlin_name_to_display(kname):
    """`BANKER_CLASSIC_MALE_PURPLE_44` → ('banker', 44).

    Returns (base_display_name_lowercased, variant_id_hint_or_None).
    The variant_id_hint is the trailing 2011 cache ID for potential direct use.
    """
    m = NUM_SUFFIX_RE.search(kname)
    hint_id = int(m.group(1)) if m else None
    stem = kname[: m.start()] if m else kname

    # Keep named chars intact: single-word or has no VARIANT_TOKENS.
    tokens = stem.split("_")
    filtered = [t for t in tokens if t.upper() not in VARIANT_TOKENS]
    if filtered:
        display = " ".join(filtered).lower()
    else:
        display = tokens[0].lower()
    return display, hint_id


def resolve_kotlin_to_b237(kname, b237_name_map, b237_id_to_name):
    """Return a b237 NPC ID for a 2011Scape Npcs.X constant name, or None."""
    display, hint_id = kotlin_name_to_display(kname)

    # 1) Direct ID hint — only trust if b237 NPC at that ID has a matching name.
    if hint_id is not None:
        b237_name = b237_id_to_name.get(hint_id, "").lower()
        if b237_name and (set(display.split()) & set(b237_name.split())):
            return hint_id

    # 2) Exact display-name match.
    ids = b237_name_map.get(display)
    if ids:
        return ids[0]

    # 3) Progressively shorter prefix match.
    parts = display.split()
    while len(parts) > 1:
        parts = parts[:-1]
        trial = " ".join(parts)
        if trial in b237_name_map:
            return b237_name_map[trial][0]
    return None


# ----- 2011Scape .kts spawn parser -----

# spawn_npc(npc = Npcs.FOO, x = 123, z = 456, height = 1, walkRadius = 5,
#           direction = Direction.SOUTH, static = true)
SPAWN_RE = re.compile(
    r"spawn_npc\s*\(\s*npc\s*=\s*Npcs\.([A-Z0-9_]+)\s*,"
    r"(?P<body>[^)]*)"
    r"\)", re.MULTILINE
)
ATTR_RE = re.compile(r"(\w+)\s*=\s*(?:Direction\.([A-Z_]+)|(-?\d+)|(true|false))")

DIR_NAMES = {"NORTH": 0, "EAST": 1, "SOUTH": 2, "WEST": 3,
             "NORTH_EAST": 4, "SOUTH_EAST": 5, "SOUTH_WEST": 6, "NORTH_WEST": 7}


def parse_2011scape_spawns(spawns_dir, x_min, x_max, y_min, y_max):
    """Parse every spawns_*.plugin.kts, filter by world tile bounds."""
    out = []
    for path in sorted(spawns_dir.glob("spawns_*.plugin.kts")):
        try:
            text = path.read_text(errors="replace")
        except Exception:
            continue
        for m in SPAWN_RE.finditer(text):
            kname = m.group(1)
            body = m.group("body")
            attrs = {"x": None, "z": None, "height": 0, "walkRadius": 0,
                     "direction": "SOUTH"}
            for am in ATTR_RE.finditer(body):
                k = am.group(1)
                if am.group(2) is not None: attrs[k] = am.group(2)
                elif am.group(3) is not None: attrs[k] = int(am.group(3))
                elif am.group(4) is not None: attrs[k] = am.group(4) == "true"
            if attrs["x"] is None or attrs["z"] is None: continue
            if not (x_min <= attrs["x"] <= x_max and y_min <= attrs["z"] <= y_max):
                continue
            out.append({
                "kname": kname,
                "x": attrs["x"], "y": attrs["z"],
                "plane": attrs["height"] if isinstance(attrs["height"], int) else 0,
                "walk_radius": attrs["walkRadius"] if isinstance(attrs["walkRadius"], int) else 0,
                "direction": attrs["direction"] if isinstance(attrs["direction"], str) else "SOUTH",
            })
    return out


# ----- NPC definition parser (RuneLite NpcLoader.decodeValues, b237) -----

def parse_npc_def(data):
    d = {
        "name": "",
        "size": 1,
        "combat_level": -1,
        "stats": [1, 1, 1, 1, 1, 1],
        "stand_anim": -1,
        "walk_anim": -1,
        "run_anim": -1,
        "attack_anim": -1,
        "death_anim": -1,
        "model_ids": [],
        "chathead_model_ids": [],
        "is_interactable": True,
        "recolors": [],
    }

    def read_str(buf):
        s = []
        while True:
            c = buf.read(1)
            if not c or c[0] == 0: break
            s.append(c)
        return b"".join(s).decode("latin-1", errors="replace")

    def read_big_smart2(buf):
        b1 = buf.read(1)
        if not b1: return -1
        v = b1[0]
        if v >= 128:
            b2 = buf.read(1)
            v = ((v & 0x7F) << 8) | b2[0]
        return v

    buf = io.BytesIO(data)
    while True:
        raw = buf.read(1)
        if not raw: break
        op = raw[0]
        if op == 0: break
        elif op == 1:
            n = read_u8(buf)
            for _ in range(n): d["model_ids"].append(read_u16(buf))
        elif op == 2: d["name"] = read_str(buf)
        elif op == 12: d["size"] = read_u8(buf)
        elif op == 13: d["stand_anim"] = read_u16(buf)
        elif op == 14: d["walk_anim"] = read_u16(buf)
        elif op == 15: read_u16(buf)
        elif op == 16: read_u16(buf)
        elif op == 17:
            d["walk_anim"] = read_u16(buf)
            read_u16(buf); read_u16(buf); read_u16(buf)
        elif op == 18: read_u16(buf)
        elif 30 <= op < 35: read_str(buf)
        elif op == 40:
            n = read_u8(buf)
            for _ in range(n):
                find = read_u16(buf); rep = read_u16(buf)
                d["recolors"].append((find, rep))
        elif op == 41:
            n = read_u8(buf)
            for _ in range(n): read_u16(buf); read_u16(buf)
        elif op == 60:
            n = read_u8(buf)
            for _ in range(n): d["chathead_model_ids"].append(read_u16(buf))
        elif op == 61:
            n = read_u8(buf)
            for _ in range(n): d["model_ids"].append(read_u32(buf))
        elif op == 62:
            n = read_u8(buf)
            for _ in range(n): d["chathead_model_ids"].append(read_u32(buf))
        elif op == 74: d["stats"][0] = read_u16(buf)
        elif op == 75: d["stats"][1] = read_u16(buf)
        elif op == 76: d["stats"][2] = read_u16(buf)
        elif op == 77: d["stats"][3] = read_u16(buf)
        elif op == 78: d["stats"][4] = read_u16(buf)
        elif op == 79: d["stats"][5] = read_u16(buf)
        elif op == 93: pass
        elif op == 95: d["combat_level"] = read_u16(buf)
        elif op == 97: read_u16(buf)
        elif op == 98: read_u16(buf)
        elif op == 99: pass
        elif op == 100: buf.read(1)
        elif op == 101: buf.read(1)
        elif op == 102:
            bitfield = read_u8(buf)
            length = 0; v = bitfield
            while v != 0:
                length += 1; v >>= 1
            for i in range(length):
                if bitfield & (1 << i):
                    read_big_smart2(buf)
                    peek = buf.read(1)
                    if peek and peek[0] >= 128:
                        buf.read(1)
        elif op == 103: read_u16(buf)
        elif op == 106:
            read_u16(buf); read_u16(buf)
            n = read_u8(buf)
            for _ in range(n + 1): read_u16(buf)
        elif op == 107: d["is_interactable"] = False
        elif op == 109: pass
        elif op == 111: pass
        elif op == 114: d["run_anim"] = read_u16(buf)
        elif op == 115:
            d["run_anim"] = read_u16(buf)
            read_u16(buf); read_u16(buf); read_u16(buf)
        elif op == 116: read_u16(buf)
        elif op == 117:
            read_u16(buf); read_u16(buf); read_u16(buf); read_u16(buf)
        elif op == 118:
            read_u16(buf); read_u16(buf); read_u16(buf)
            n = read_u8(buf)
            for _ in range(n + 1): read_u16(buf)
        elif op == 122: pass
        elif op == 123: pass
        elif op == 124: read_u16(buf)
        elif op == 126: read_u16(buf)
        elif op == 129: pass
        elif op == 130: pass
        elif op == 145: pass
        elif op == 146: read_u16(buf)
        elif op == 147: pass
        elif op == 249:
            n = read_u8(buf)
            for _ in range(n):
                is_str = read_u8(buf)
                read_u8(buf); read_u8(buf); read_u8(buf)
                if is_str: read_str(buf)
                else: read_u32(buf)
        elif op == 251:
            sub = read_u8(buf)
            for _ in range(sub):
                read_u8(buf); read_str(buf)
        elif op == 252:
            cond = read_u8(buf)
            for _ in range(cond):
                read_u8(buf); read_u16(buf); read_str(buf)
        elif op == 253:
            cond = read_u8(buf)
            for _ in range(cond):
                read_u8(buf); read_u16(buf)
                sub = read_u8(buf)
                for _ in range(sub):
                    read_u8(buf); read_str(buf)
        else:
            break
    return d


def load_npc_defs(reader, npc_ids):
    files = reader.read_group(2, 9)
    defs = {}
    for nid in npc_ids:
        data = files.get(nid)
        if data is None: continue
        try:
            d = parse_npc_def(data)
            d["id"] = nid
            defs[nid] = d
        except Exception:
            pass
    return defs


# ----- NPC model export (composite body parts per NPC) -----

def export_npc_model(reader, npc_id, model_ids, recolors=None, tex_colors=None):
    all_verts, all_colors, all_base, all_skins, all_fi = [], [], [], [], []
    base_off, face_count = 0, 0
    loaded_parts = 0

    for mid in model_ids:
        raw = load_model_modern(reader, mid)
        if not raw: continue
        md = decode_model(mid, raw)
        if not md: continue
        loaded_parts += 1
        if recolors:
            for i in range(len(md.face_colors)):
                for find, rep in recolors:
                    if md.face_colors[i] == find:
                        md.face_colors[i] = rep; break
        verts, colors, uvs = expand_model(md, tex_colors=tex_colors)
        all_verts.extend(verts)
        for r, g, b, a in colors:
            all_colors.extend([r, g, b, a])
        for i in range(md.vertex_count):
            all_base.extend([int(md.vertices_x[i]), int(md.vertices_y[i]), int(md.vertices_z[i])])
            all_skins.append(int(md.vertex_skins[i]) if i < len(md.vertex_skins) else 0)
        for i in range(md.face_count):
            all_fi.extend([md.face_a[i] + base_off, md.face_b[i] + base_off, md.face_c[i] + base_off])
        base_off += md.vertex_count
        face_count += md.face_count

    if not all_verts: return None, loaded_parts
    return {
        "id": npc_id,
        "expanded_verts": all_verts,
        "colors": all_colors,
        "base_verts": all_base,
        "skins": all_skins,
        "face_indices": all_fi,
        "face_count": face_count,
        "base_vert_count": base_off,
    }, loaded_parts


# ----- Binary writers -----

def write_ndef(path, defs):
    with open(path, "wb") as f:
        f.write(struct.pack("<III", NDEF_MAGIC, 1, len(defs)))
        for nid in sorted(defs.keys()):
            d = defs[nid]
            f.write(struct.pack("<I", nid))
            f.write(struct.pack("<B", d["size"]))
            f.write(struct.pack("<h", d["combat_level"]))
            f.write(struct.pack("<H", d["stats"][3]))
            for s in d["stats"]: f.write(struct.pack("<H", s))
            f.write(struct.pack("<i", d["stand_anim"]))
            f.write(struct.pack("<i", d["walk_anim"]))
            f.write(struct.pack("<i", d["run_anim"]))
            f.write(struct.pack("<i", d["attack_anim"]))
            f.write(struct.pack("<i", d["death_anim"]))
            name = d["name"].encode("latin-1")[:63]
            f.write(struct.pack("<B", len(name))); f.write(name)


def write_nspn(path, spawns):
    with open(path, "wb") as f:
        f.write(struct.pack("<III", NSPN_MAGIC, 1, len(spawns)))
        for s in spawns:
            f.write(struct.pack("<I", s["npc_id"]))
            f.write(struct.pack("<i", s["x"]))
            f.write(struct.pack("<i", s["y"]))
            f.write(struct.pack("<B", s.get("level", 0)))
            f.write(struct.pack("<B", s.get("direction_code", 2)))
            f.write(struct.pack("<B", s.get("wander_range", 5)))


def write_npc_models(path, models):
    with open(path, "wb") as f:
        f.write(struct.pack("<II", MDL2_MAGIC, len(models)))
        offsets_pos = f.tell()
        for _ in models: f.write(struct.pack("<I", 0))
        offsets = []
        for m in models:
            offsets.append(f.tell())
            evc = len(m["expanded_verts"]) // 3
            fc = m["face_count"]; bvc = m["base_vert_count"]
            f.write(struct.pack("<I", m["id"]))
            f.write(struct.pack("<HHH", evc, fc, bvc))
            for v in m["expanded_verts"]: f.write(struct.pack("<f", float(v)))
            for c in m["colors"]: f.write(struct.pack("B", int(c)))
            for v in m["base_verts"]: f.write(struct.pack("<h", int(v)))
            for s in m["skins"]: f.write(struct.pack("B", int(s)))
            for idx in m["face_indices"]: f.write(struct.pack("<H", int(idx)))
            for _ in range(fc): f.write(struct.pack("B", 0))
        end = f.tell()
        f.seek(offsets_pos)
        for off in offsets: f.write(struct.pack("<I", off))
        f.seek(end)


# ----- Main -----

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--bounds", type=str, default="3072,3264,3392,3520",
                        help="x_min,x_max,y_min,y_max (Varrock default)")
    parser.add_argument("--defs-out", type=Path, required=True)
    parser.add_argument("--spawns-out", type=Path, required=True)
    parser.add_argument("--models-out", type=Path, required=True)
    args = parser.parse_args()

    x_min, x_max, y_min, y_max = map(int, args.bounds.split(","))
    reader = ModernCacheReader(str(args.cache))

    print(f"Scanning b237 cache for name → ID map...")
    b237_name_map, b237_id_to_name = build_b237_name_map(reader)
    print(f"  {sum(len(v) for v in b237_name_map.values())} NPCs, {len(b237_name_map)} unique names")

    if not SPAWNS_DIR.exists():
        print(f"ERROR: 2011Scape repo not found at {SCAPE_2011}")
        print("  Clone with: git clone --depth 1 https://github.com/2011Scape/game.git"
              f" {SCAPE_2011}")
        sys.exit(1)

    print(f"Parsing 2011Scape spawn plugins, bounds x=[{x_min},{x_max}] y=[{y_min},{y_max}]...")
    raw_spawns = parse_2011scape_spawns(SPAWNS_DIR, x_min, x_max, y_min, y_max)
    print(f"  {len(raw_spawns)} spawns (pre-resolution)")

    spawns = []
    unresolved = {}
    name_to_id_used = {}
    for s in raw_spawns:
        kname = s["kname"]
        nid = resolve_kotlin_to_b237(kname, b237_name_map, b237_id_to_name)
        if nid is None:
            unresolved[kname] = unresolved.get(kname, 0) + 1
            continue
        name_to_id_used.setdefault(kname, set()).add(nid)
        direction_code = DIR_NAMES.get(s["direction"], 2)
        spawns.append({
            "npc_id": nid,
            "name": kname,
            "x": s["x"],
            "y": s["y"],
            "level": s["plane"],
            "direction_code": direction_code,
            "wander_range": s["walk_radius"] if s["walk_radius"] > 0 else 5,
        })
    print(f"  {len(name_to_id_used)} unique Kotlin names resolved, "
          f"{len(unresolved)} unresolved ({sum(unresolved.values())} spawns dropped)")
    if unresolved:
        top = sorted(unresolved.items(), key=lambda x: -x[1])[:10]
        print(f"  top unresolved: {top}")

    unique_ids = sorted({s["npc_id"] for s in spawns})
    print(f"Loading {len(unique_ids)} unique NPC defs (by b237 ID) from cache...")
    defs = load_npc_defs(reader, unique_ids)
    print(f"  {len(defs)} defs loaded")

    spawns = [s for s in spawns if s["npc_id"] in defs]
    print(f"  {len(spawns)} spawns with valid defs")

    print("Loading texture average colors (for textured-face fallback)...")
    try:
        tex_colors = load_texture_average_colors_modern(reader)
        print(f"  {len(tex_colors)} texture colors")
    except Exception as e:
        print(f"  failed: {e}")
        tex_colors = {}

    print("Exporting NPC models (unique b237 IDs)...")
    models = []
    empty_model_ids = []
    no_parts_ids = []
    for nid in sorted(defs.keys()):
        d = defs[nid]
        if not d["model_ids"]:
            no_parts_ids.append(nid)
            continue
        m, loaded_parts = export_npc_model(reader, nid, d["model_ids"], d.get("recolors"), tex_colors)
        if m:
            models.append(m)
        else:
            empty_model_ids.append((nid, d["name"], len(d["model_ids"]), loaded_parts))
    print(f"  {len(models)} models exported")
    if no_parts_ids:
        print(f"  {len(no_parts_ids)} NPCs have no model parts in def (skipped)")
    if empty_model_ids:
        print(f"  {len(empty_model_ids)} NPCs had parts declared but none loaded:")
        for nid, name, declared, loaded in empty_model_ids[:8]:
            print(f"    id={nid} '{name}' declared={declared} loaded={loaded}")

    print()
    print("Per-NPC spawn count (top 20):")
    spawn_counts = {}
    for s in spawns:
        spawn_counts[s["npc_id"]] = spawn_counts.get(s["npc_id"], 0) + 1
    for nid, count in sorted(spawn_counts.items(), key=lambda x: -x[1])[:20]:
        name = defs[nid]["name"]
        has_model = any(m["id"] == nid for m in models)
        tag = "" if has_model else "  [NO MODEL!]"
        print(f"  {count:4d}× id={nid:5d} '{name}'{tag}")

    args.defs_out.parent.mkdir(parents=True, exist_ok=True)
    args.spawns_out.parent.mkdir(parents=True, exist_ok=True)
    args.models_out.parent.mkdir(parents=True, exist_ok=True)

    write_ndef(args.defs_out, defs)
    print(f"\n  wrote {args.defs_out} ({args.defs_out.stat().st_size} bytes)")
    write_nspn(args.spawns_out, spawns)
    print(f"  wrote {args.spawns_out} ({args.spawns_out.stat().st_size} bytes)")
    write_npc_models(args.models_out, models)
    print(f"  wrote {args.models_out} ({args.models_out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
