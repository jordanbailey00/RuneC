"""Export inferno NPC models, animations, and spotanim GFX from modern OSRS cache.

Reads NPC definitions for all inferno monsters (nibblers through Zuk), extracts
their model IDs and animation sequence IDs, exports 3D meshes to .models binary,
exports animations to .anims binary, and updates npc_models.h with mappings.

Also reads SpotAnim (GFX) configs for inferno projectiles.

Usage:
    uv run python scripts/export_inferno_npcs.py \
        --modern-cache /path/to/osrs-cache-modern \
        --output-dir data
"""

import argparse
import copy
import io
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from modern_cache_reader import (
    ModernCacheReader,
    read_big_smart,
    read_i32,
    read_string,
    read_u8,
    read_u16,
    read_u24,
    read_u32,
)
from export_models import (
    MDL2_MAGIC,
    ModelData,
    _merge_models,
    decode_model,
    expand_model,
    load_model_modern,
    write_models_binary,
)
from export_animations import (
    ANIM_MAGIC,
    FrameBaseDef,
    FrameDef,
    SequenceDef,
    _parse_normal_frame,
    load_modern_framebases,
    parse_modern_framebase,
    write_animations_binary,
)
from modern_cache_reader import parse_sequence as parse_modern_sequence

# modern cache layout
MODERN_NPC_CONFIG_GROUP = 9      # config index 2, group 9 = NPC definitions
MODERN_SPOTANIM_CONFIG_GROUP = 13  # config index 2, group 13 = SpotAnim/GFX
MODERN_SEQ_CONFIG_GROUP = 12     # config index 2, group 12 = sequences
MODERN_FRAME_INDEX = 0           # frame archives
MODERN_FRAMEBASE_INDEX = 1       # frame bases

# inferno NPC IDs from the OSRS wiki
INFERNO_NPC_IDS = {
    7691: "Jal-Nib (nibbler)",
    7692: "Jal-MejRah (bat)",
    7693: "Jal-Ak (blob)",
    7694: "Jal-Ak-Rek-Ket (blob melee split)",
    7695: "Jal-Ak-Rek-Xil (blob range split)",
    7696: "Jal-Ak-Rek-Mej (blob mage split)",
    7697: "Jal-ImKot (meleer)",
    7698: "Jal-Xil (ranger)",
    7699: "Jal-Zek (mager)",
    7700: "JalTok-Jad",
    7701: "Yt-HurKot (jad healer)",
    7706: "TzKal-Zuk",
    7707: "Zuk shield",
    7708: "Jal-MejJak (zuk healer)",
}

# known inferno projectile/effect GFX IDs to check
# from OSRS wiki inferno page and runelite inferno plugin
INFERNO_SPOTANIM_IDS = {
    # jad attacks
    447: "Jad ranged projectile (fireball)",
    448: "Jad magic projectile",
    451: "Jad ranged hit",
    157: "Jad magic hit",
    # mager
    1379: "Mager magic projectile",
    1380: "Mager magic hit",
    # ranger
    1377: "Ranger ranged projectile",
    1378: "Ranger ranged hit",
    # zuk
    1375: "Zuk magic projectile",
    1376: "Zuk ranged projectile",
    1381: "Zuk typeless hit (falling rocks?)",
    # bat
    1374: "Bat ranged projectile",
    # blob
    1382: "Blob melee",
    1383: "Blob ranged",
    1384: "Blob magic",
    # healer
    1385: "Healer magic attack",
    # player projectiles (needed for tbow in inferno)
    942: "Dragon arrow projectile (twisted bow)",
}


@dataclass
class NpcDef:
    """NPC definition from modern OSRS cache."""

    npc_id: int = 0
    name: str = ""
    model_ids: list[int] = field(default_factory=list)
    chathead_model_ids: list[int] = field(default_factory=list)
    size: int = 1
    idle_anim: int = -1
    walk_anim: int = -1
    run_anim: int = -1
    turn_180_anim: int = -1
    turn_cw_anim: int = -1
    turn_ccw_anim: int = -1
    attack_anim: int = -1  # from wiki/runelite, not in def directly
    death_anim: int = -1   # from wiki/runelite, not in def directly
    combat_level: int = 0
    width_scale: int = 128
    height_scale: int = 128
    recolor_src: list[int] = field(default_factory=list)
    recolor_dst: list[int] = field(default_factory=list)
    retexture_src: list[int] = field(default_factory=list)
    retexture_dst: list[int] = field(default_factory=list)


@dataclass
class SpotAnimDef:
    """SpotAnim (GFX) definition from modern OSRS cache."""

    id: int = 0
    model_id: int = -1
    seq_id: int = -1
    recolor_src: list[int] = field(default_factory=list)
    recolor_dst: list[int] = field(default_factory=list)
    width_scale: int = 128
    height_scale: int = 128
    rotation: int = 0
    ambient: int = 0
    contrast: int = 0


def parse_modern_npc_def(npc_id: int, data: bytes) -> NpcDef:
    """Parse modern OSRS NPC definition from opcode stream.

    Opcode reference from RuneLite NpcLoader (modern revisions):
      1: model IDs (u8 count, u16[count])
      2: name (string)
      12: size (u8)
      13: idle animation (u16)
      14: walk animation (u16)
      15: turn 180 animation (u16)
      16: turn CW animation (u16, modern split from old 17)
      17: turn CCW animation (u16)
      18: unused / walk backward (u16)
      19: unused (u8 from modern, or actions in old)
      30-34: actions (string each)
      40: recolor pairs (u8 count, u16+u16 per pair)
      41: retexture pairs (u8 count, u16+u16 per pair)
      60: chathead model IDs (u8 count, u16[count])
      93: drawMapDot = false (flag)
      95: combat level (u16)
      97: width scale (u16)
      98: height scale (u16)
      99: hasRenderPriority (flag)
      100: ambient (u8)
      101: contrast (u8)
      102: head icon (bitfield + smart pairs)
      103: rotation (u16)
      106: morph (varbit+varp+count+children)
      107: isInteractable = false (flag)
      108: isPet = false (modern)
      109: isClickable = false (flag)
      111: isFollower (flag)
      114-118: various transform/morph opcodes
      249: params map
    """
    d = NpcDef(npc_id=npc_id)
    buf = io.BytesIO(data)

    while True:
        opcode_byte = buf.read(1)
        if not opcode_byte:
            break
        opcode = opcode_byte[0]

        if opcode == 0:
            break
        elif opcode == 1:
            count = read_u8(buf)
            d.model_ids = [read_u16(buf) for _ in range(count)]
        elif opcode == 2:
            d.name = read_string(buf)
        elif opcode == 3:
            read_string(buf)  # description (removed in modern, but handle gracefully)
        elif opcode == 5:
            # pre-modern: another model list? skip
            count = read_u8(buf)
            for _ in range(count):
                read_u16(buf)
        elif opcode == 12:
            d.size = read_u8(buf)
        elif opcode == 13:
            d.idle_anim = read_u16(buf)
        elif opcode == 14:
            d.walk_anim = read_u16(buf)
        elif opcode == 15:
            d.turn_180_anim = read_u16(buf)  # idleRotateLeftAnimation
        elif opcode == 16:
            d.turn_cw_anim = read_u16(buf)  # idleRotateRightAnimation
        elif opcode == 17:
            # walk + rotate180 + rotateLeft + rotateRight (4 x u16)
            d.walk_anim = read_u16(buf)
            d.turn_180_anim = read_u16(buf)
            d.turn_cw_anim = read_u16(buf)
            d.turn_ccw_anim = read_u16(buf)
        elif opcode == 18:
            read_u16(buf)  # category
        elif 30 <= opcode <= 34:
            read_string(buf)  # actions[0..4]
        elif opcode == 40:
            count = read_u8(buf)
            for _ in range(count):
                d.recolor_src.append(read_u16(buf))
                d.recolor_dst.append(read_u16(buf))
        elif opcode == 41:
            count = read_u8(buf)
            for _ in range(count):
                d.retexture_src.append(read_u16(buf))
                d.retexture_dst.append(read_u16(buf))
        elif opcode == 60:
            count = read_u8(buf)
            d.chathead_model_ids = [read_u16(buf) for _ in range(count)]
        elif 74 <= opcode <= 79:
            read_u16(buf)  # stats[opcode - 74] (attack/def/str/range/magic/hp)
        elif opcode == 93:
            pass  # drawMapDot = false
        elif opcode == 95:
            d.combat_level = read_u16(buf)
        elif opcode == 97:
            d.width_scale = read_u16(buf)
        elif opcode == 98:
            d.height_scale = read_u16(buf)
        elif opcode == 99:
            pass  # hasRenderPriority
        elif opcode == 100:
            read_u8(buf)  # ambient
        elif opcode == 101:
            read_u8(buf)  # contrast
        elif opcode == 102:
            # head icon sprite — u8 bitfield, per set bit: BigSmart2 + UnsignedShortSmartMinusOne
            bitfield = read_u8(buf)
            bit_count = 0
            tmp = bitfield
            while tmp != 0:
                bit_count += 1
                tmp >>= 1
            for i in range(bit_count):
                if bitfield & (1 << i):
                    # BigSmart2: if first byte < 128, read u16; else read i32 & 0x7FFFFFFF
                    pos = buf.tell()
                    peek = buf.read(1)
                    if peek and peek[0] < 128:
                        buf.seek(pos)
                        read_u16(buf)
                    else:
                        buf.seek(pos)
                        read_i32(buf)
                    # UnsignedShortSmartMinusOne: same as big_smart but -1
                    pos2 = buf.tell()
                    peek2 = buf.read(1)
                    if peek2 and peek2[0] < 128:
                        buf.seek(pos2)
                        read_u16(buf)
                    else:
                        buf.seek(pos2)
                        read_i32(buf)
        elif opcode == 103:
            read_u16(buf)  # rotation
        elif opcode == 106:
            # morph: u16 varbit, u16 varp, u8 length, (length+1) u16 configs
            read_u16(buf)  # varbitId
            read_u16(buf)  # varpIndex
            length = read_u8(buf)
            for _ in range(length + 1):
                read_u16(buf)  # configs
        elif opcode == 107:
            pass  # isInteractable = false
        elif opcode == 108:
            pass  # isPet (modern)
        elif opcode == 109:
            pass  # isClickable = false
        elif opcode == 111:
            pass  # isFollower
        elif opcode == 114:
            read_u16(buf)  # runSequence
        elif opcode == 115:
            read_u16(buf)  # runSequence
            read_u16(buf)  # runBackSequence
            read_u16(buf)  # runRightSequence
            read_u16(buf)  # runLeftSequence
        elif opcode == 116:
            read_u16(buf)  # crawlSequence
        elif opcode == 117:
            read_u16(buf)  # crawlBackSequence
            read_u16(buf)  # crawlRightSequence
            read_u16(buf)  # crawlLeftSequence
        elif opcode == 118:
            # morph2: u16 varbit, u16 varp, u16 default, u8 length, (length+1) u16 configs
            read_u16(buf)  # varbitId
            read_u16(buf)  # varpIndex
            read_u16(buf)  # default child (var)
            length = read_u8(buf)
            for _ in range(length + 1):
                read_u16(buf)  # configs
        elif opcode == 122:
            pass  # isFollower
        elif opcode == 123:
            pass  # lowPriorityFollowerOps
        elif opcode == 124:
            read_u16(buf)  # height
        elif opcode == 125:
            read_u8(buf)   # unknown
        elif opcode == 126:
            read_u16(buf)  # footprintSize
        elif opcode == 128:
            read_u8(buf)   # unknown
        elif opcode == 129:
            pass  # unknown flag
        elif opcode == 130:
            pass  # idleAnimRestart
        elif opcode == 145:
            pass  # canHideForOverlap
        elif opcode == 146:
            read_u16(buf)  # overlapTintHSL
        elif opcode == 147:
            pass  # zbuf = false
        elif opcode == 249:
            count_val = read_u8(buf)
            for _ in range(count_val):
                is_string = read_u8(buf)
                read_u24(buf)  # key (medium)
                if is_string:
                    read_string(buf)
                else:
                    read_u32(buf)
        else:
            print(f"  warning: unknown npc opcode {opcode} at npc {npc_id}, pos {buf.tell()}", file=sys.stderr)
            break

    return d


def parse_modern_spotanim(spotanim_id: int, data: bytes) -> SpotAnimDef:
    """Parse modern SpotAnim/GFX definition from opcode stream.

    Opcode reference from RuneLite SpotAnimLoader:
      1: model ID (u16)
      2: sequence ID (u16)
      4: width scale (u16)
      5: height scale (u16)
      6: rotation (u16)
      7: ambient (u8)
      8: contrast (u8)
      40: recolor pairs (u8 count, u16+u16)
      41: retexture pairs (u8 count, u16+u16)
    """
    d = SpotAnimDef(id=spotanim_id)
    buf = io.BytesIO(data)

    while True:
        opcode_byte = buf.read(1)
        if not opcode_byte:
            break
        opcode = opcode_byte[0]

        if opcode == 0:
            break
        elif opcode == 1:
            d.model_id = read_u16(buf)
        elif opcode == 2:
            d.seq_id = read_u16(buf)
        elif opcode == 4:
            d.width_scale = read_u16(buf)
        elif opcode == 5:
            d.height_scale = read_u16(buf)
        elif opcode == 6:
            d.rotation = read_u16(buf)
        elif opcode == 7:
            d.ambient = read_u8(buf)
        elif opcode == 8:
            d.contrast = read_u8(buf)
        elif opcode == 40:
            count = read_u8(buf)
            for _ in range(count):
                d.recolor_src.append(read_u16(buf))
                d.recolor_dst.append(read_u16(buf))
        elif opcode == 41:
            count = read_u8(buf)
            for _ in range(count):
                read_u16(buf)
                read_u16(buf)
        else:
            print(f"  warning: unknown spotanim opcode {opcode} at gfx {spotanim_id}", file=sys.stderr)
            break

    return d


def apply_recolors(md: ModelData, src: list[int], dst: list[int]) -> None:
    """Apply recolor pairs to model face colors in-place."""
    for i, color in enumerate(md.face_colors):
        for s, d in zip(src, dst):
            if color == s:
                md.face_colors[i] = d
                break


def apply_scale(md: ModelData, width_scale: int, height_scale: int) -> None:
    """Apply NPC width/height scale to vertex positions in-place."""
    if width_scale == 128 and height_scale == 128:
        return
    ws = width_scale / 128.0
    hs = height_scale / 128.0
    for i in range(md.vertex_count):
        md.vertices_x[i] = int(md.vertices_x[i] * ws)
        md.vertices_y[i] = int(md.vertices_y[i] * hs)
        md.vertices_z[i] = int(md.vertices_z[i] * ws)


def main() -> None:
    """Export inferno NPC data from modern OSRS cache."""
    parser = argparse.ArgumentParser(description="export inferno NPC models + animations from modern cache")
    parser.add_argument(
        "--modern-cache", type=Path, required=True,
        help="path to modern OpenRS2 flat-file cache",
    )
    parser.add_argument(
        "--output-dir", type=Path, default=Path("data"),
        help="output directory for generated files",
    )
    args = parser.parse_args()

    reader = ModernCacheReader(args.modern_cache)
    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    # ================================================================
    # step 1: read NPC definitions from config index 2, group 9
    # ================================================================
    print("reading NPC definitions from modern cache (index 2, group 9)...")
    npc_files = reader.read_group(2, MODERN_NPC_CONFIG_GROUP)
    print(f"  {len(npc_files)} total NPC entries in group 9")

    npc_defs: dict[int, NpcDef] = {}
    all_model_ids: set[int] = set()
    all_anim_ids: set[int] = set()

    for npc_id, label in sorted(INFERNO_NPC_IDS.items()):
        if npc_id not in npc_files:
            print(f"  NPC {npc_id} ({label}): NOT FOUND in cache")
            continue

        npc = parse_modern_npc_def(npc_id, npc_files[npc_id])
        npc_defs[npc_id] = npc

        print(f"\n  NPC {npc_id} ({label}):")
        print(f"    name: {npc.name}")
        print(f"    models: {npc.model_ids}")
        print(f"    size: {npc.size}")
        print(f"    idle_anim: {npc.idle_anim}")
        print(f"    walk_anim: {npc.walk_anim}")
        print(f"    scale: {npc.width_scale}x{npc.height_scale}")
        if npc.recolor_src:
            print(f"    recolors: {list(zip(npc.recolor_src, npc.recolor_dst))}")
        if npc.retexture_src:
            print(f"    retextures: {list(zip(npc.retexture_src, npc.retexture_dst))}")

        all_model_ids.update(npc.model_ids)
        for anim_id in [npc.idle_anim, npc.walk_anim, npc.turn_180_anim, npc.turn_cw_anim, npc.turn_ccw_anim]:
            if anim_id >= 0:
                all_anim_ids.add(anim_id)

    # ================================================================
    # step 2: read SpotAnim/GFX definitions
    # ================================================================
    print("\n\nreading SpotAnim/GFX definitions (index 2, group 13)...")
    spotanim_files = reader.read_group(2, MODERN_SPOTANIM_CONFIG_GROUP)
    print(f"  {len(spotanim_files)} total spotanim entries")

    spotanim_defs: dict[int, SpotAnimDef] = {}
    for gfx_id, label in sorted(INFERNO_SPOTANIM_IDS.items()):
        if gfx_id not in spotanim_files:
            print(f"  GFX {gfx_id} ({label}): NOT FOUND in cache")
            continue

        sa = parse_modern_spotanim(gfx_id, spotanim_files[gfx_id])
        spotanim_defs[gfx_id] = sa

        print(f"  GFX {gfx_id} ({label}): model={sa.model_id}, seq={sa.seq_id}, "
              f"scale={sa.width_scale}x{sa.height_scale}")

        if sa.model_id >= 0:
            all_model_ids.add(sa.model_id)
        if sa.seq_id >= 0:
            all_anim_ids.add(sa.seq_id)

    print(f"\ntotal unique model IDs to export: {len(all_model_ids)}")
    print(f"  {sorted(all_model_ids)}")
    print(f"total unique animation IDs to export: {len(all_anim_ids)}")
    print(f"  {sorted(all_anim_ids)}")

    # ================================================================
    # step 3: export NPC models
    # ================================================================
    print("\n\nexporting NPC + GFX models...")
    all_models: list[ModelData] = []

    # for each NPC, merge sub-models, apply recolors/scale
    for npc_id, npc in sorted(npc_defs.items()):
        sub_models: list[ModelData] = []
        for mid in npc.model_ids:
            raw = load_model_modern(reader, mid)
            if raw is None:
                print(f"  warning: model {mid} not found for NPC {npc_id}")
                continue
            md = decode_model(mid, raw)
            if md is None:
                print(f"  warning: failed to decode model {mid} for NPC {npc_id}")
                continue
            sub_models.append(md)

        if not sub_models:
            print(f"  NPC {npc_id}: no models decoded")
            continue

        if len(sub_models) == 1:
            merged = sub_models[0]
        else:
            merged = _merge_models(sub_models)

        # apply recolors
        if npc.recolor_src:
            apply_recolors(merged, npc.recolor_src, npc.recolor_dst)

        # apply scale
        apply_scale(merged, npc.width_scale, npc.height_scale)

        # use NPC ID as model ID for lookup (synthetic: 0xC0000 + npc_id)
        merged.model_id = 0xC0000 + npc_id
        all_models.append(merged)
        print(f"  NPC {npc_id} ({npc.name}): {merged.vertex_count} verts, {merged.face_count} faces")

    # also export raw model IDs for GFX projectiles (no recolor for now, keep raw)
    gfx_model_ids = set()
    for gfx_id, sa in spotanim_defs.items():
        if sa.model_id >= 0:
            gfx_model_ids.add(sa.model_id)

    for mid in sorted(gfx_model_ids):
        raw = load_model_modern(reader, mid)
        if raw is None:
            print(f"  warning: GFX model {mid} not found")
            continue
        md = decode_model(mid, raw)
        if md is None:
            print(f"  warning: failed to decode GFX model {mid}")
            continue
        all_models.append(md)
        print(f"  GFX model {mid}: {md.vertex_count} verts, {md.face_count} faces")

    # write models binary
    models_path = output_dir / "inferno_npcs.models"
    write_models_binary(models_path, all_models)
    file_size = models_path.stat().st_size
    print(f"\nwrote {len(all_models)} models ({file_size:,} bytes) to {models_path}")

    # ================================================================
    # step 4: export animations
    # ================================================================
    print("\n\nexporting animations...")
    seq_files = reader.read_group(2, MODERN_SEQ_CONFIG_GROUP)

    sequences: dict[int, SequenceDef] = {}
    for seq_id in sorted(all_anim_ids):
        if seq_id not in seq_files:
            print(f"  warning: sequence {seq_id} not found in cache")
            continue
        modern_seq = parse_modern_sequence(seq_id, seq_files[seq_id])
        seq = SequenceDef(
            seq_id=modern_seq.seq_id,
            frame_count=modern_seq.frame_count,
            frame_delays=modern_seq.frame_delays,
            primary_frame_ids=modern_seq.primary_frame_ids,
            frame_step=modern_seq.frame_step,
            interleave_order=modern_seq.interleave_order,
            priority=modern_seq.forced_priority,
            loop_count=modern_seq.max_loops,
            walk_flag=modern_seq.priority,
            run_flag=modern_seq.precedence_animating,
        )
        sequences[seq_id] = seq
        print(f"  seq {seq_id}: {seq.frame_count} frames, delays={seq.frame_delays[:5]}{'...' if len(seq.frame_delays) > 5 else ''}")

    # collect needed frame groups
    needed_groups: set[int] = set()
    for seq_id in all_anim_ids & set(sequences.keys()):
        seq = sequences[seq_id]
        for fid in seq.primary_frame_ids:
            if fid != -1:
                needed_groups.add(fid >> 16)

    print(f"  loading {len(needed_groups)} frame archives...")

    # first pass: discover framebase IDs from frame data headers
    needed_base_ids: set[int] = set()
    raw_frame_data: dict[int, dict[int, bytes]] = {}
    for group_id in sorted(needed_groups):
        try:
            files = reader.read_group(MODERN_FRAME_INDEX, group_id)
        except (KeyError, FileNotFoundError):
            print(f"  warning: frame archive {group_id} not found")
            continue
        raw_frame_data[group_id] = files
        for file_data in files.values():
            if len(file_data) >= 2:
                fb_id = (file_data[0] << 8) | file_data[1]
                needed_base_ids.add(fb_id)

    print(f"  loading {len(needed_base_ids)} framebases...")
    framebases = load_modern_framebases(reader, needed_base_ids)
    print(f"  loaded {len(framebases)} framebases")

    # second pass: parse frames
    all_frames: dict[int, dict[int, FrameDef]] = {}
    for group_id, files in raw_frame_data.items():
        frames: dict[int, FrameDef] = {}
        for file_id, file_data in files.items():
            if len(file_data) < 3:
                continue
            frame = _parse_normal_frame(group_id, file_id, file_data, framebases)
            if frame is not None:
                frames[file_id] = frame
        if frames:
            all_frames[group_id] = frames

    total_frames = sum(len(v) for v in all_frames.values())
    print(f"  {len(all_frames)} frame archives, {total_frames} total frames")

    # write animations binary
    anims_path = output_dir / "inferno_npcs.anims"
    available_seqs = all_anim_ids & set(sequences.keys())
    write_animations_binary(anims_path, framebases, all_frames, sequences, available_seqs)

    # ================================================================
    # step 5: update npc_models.h
    # ================================================================
    print("\n\nupdating npc_models.h...")
    header_path = output_dir / "npc_models.h"

    # build NPC model mapping entries
    npc_entries = []
    for npc_id, npc in sorted(npc_defs.items()):
        # use the synthetic model ID we assigned (0xC0000 + npc_id)
        synth_model = 0xC0000 + npc_id
        idle = npc.idle_anim if npc.idle_anim >= 0 else 0xFFFF
        # we don't have attack anims from the def directly; set to 0xFFFF (unknown)
        attack = 0xFFFF
        label = INFERNO_NPC_IDS.get(npc_id, npc.name)
        npc_entries.append((npc_id, synth_model, idle, attack, label))

    # build spotanim entries for C header
    spotanim_entries = []
    for gfx_id, sa in sorted(spotanim_defs.items()):
        if sa.model_id >= 0:
            label = INFERNO_SPOTANIM_IDS.get(gfx_id, "unknown")
            spotanim_entries.append((gfx_id, sa.model_id, sa.seq_id, label))

    # write C header
    with open(header_path, "w") as f:
        f.write("/**\n")
        f.write(" * @fileoverview NPC model/animation mappings for encounter rendering.\n")
        f.write(" *\n")
        f.write(" * Maps NPC definition IDs to cache model IDs and animation sequence IDs.\n")
        f.write(" * Generated by scripts/export_inferno_npcs.py — do not edit.\n")
        f.write(" */\n\n")
        f.write("#ifndef NPC_MODELS_H\n")
        f.write("#define NPC_MODELS_H\n\n")
        f.write("#include <stdint.h>\n\n")

        f.write("typedef struct {\n")
        f.write("    uint16_t npc_id;\n")
        f.write("    uint32_t model_id;\n")
        f.write("    uint32_t idle_anim;\n")
        f.write("    uint32_t attack_anim;\n")
        f.write("} NpcModelMapping;\n\n")

        # zulrah entries (keep existing)
        f.write("/* zulrah forms + snakeling */\n")
        f.write("static const NpcModelMapping NPC_MODEL_MAP_ZULRAH[] = {\n")
        f.write("    {2042, 14408, 5069, 5068},  /* green zulrah (ranged) */\n")
        f.write("    {2043, 14409, 5069, 5068},  /* red zulrah (melee) */\n")
        f.write("    {2044, 14407, 5069, 5068},  /* blue zulrah (magic) */\n")
        f.write("};\n\n")

        # snakeling defines (keep existing)
        f.write("/* snakeling model + animations (NPC 2045 melee, 2046 magic — same model) */\n")
        f.write("#define SNAKELING_MODEL_ID 10415\n")
        f.write("#define SNAKELING_ANIM_IDLE    1721\n")
        f.write("#define SNAKELING_ANIM_MELEE   140   /* NPC 2045 melee attack */\n")
        f.write("#define SNAKELING_ANIM_MAGIC   185   /* NPC 2046 magic attack */\n")
        f.write("#define SNAKELING_ANIM_DEATH   138   /* NPC 2045 death */\n")
        f.write("#define SNAKELING_ANIM_WALK    2405  /* walk cycle */\n\n")

        # zulrah spotanim defines (keep existing)
        f.write("/* zulrah spotanim (projectile/cloud) model IDs */\n")
        f.write("#define GFX_RANGED_PROJ_MODEL  20390  /* GFX 1044 ranged projectile */\n")
        f.write("#define GFX_CLOUD_PROJ_MODEL   11221  /* GFX 1045 cloud projectile */\n")
        f.write("#define GFX_MAGIC_PROJ_MODEL   26593  /* GFX 1046 magic projectile */\n")
        f.write("#define GFX_TOXIC_CLOUD_MODEL   4086  /* object 11700 */\n")
        f.write("#define GFX_SNAKELING_SPAWN_MODEL 20390  /* GFX 1047 spawn orb */\n\n")

        # zulrah animation defines (keep existing)
        f.write("/* zulrah animation sequence IDs */\n")
        f.write("#define ZULRAH_ANIM_ATTACK   5068\n")
        f.write("#define ZULRAH_ANIM_IDLE     5069\n")
        f.write("#define ZULRAH_ANIM_DIVE     5072\n")
        f.write("#define ZULRAH_ANIM_SURFACE  5071\n")
        f.write("#define ZULRAH_ANIM_RISE     5073\n")
        f.write("#define ZULRAH_ANIM_5070     5070\n")
        f.write("#define ZULRAH_ANIM_5806     5806\n")
        f.write("#define ZULRAH_ANIM_5807     5807\n")
        f.write("#define GFX_SNAKELING_SPAWN_ANIM 5358\n\n")

        # inferno NPC model mappings
        f.write("/* ================================================================ */\n")
        f.write("/* inferno NPC model/animation mappings                              */\n")
        f.write("/* ================================================================ */\n\n")

        f.write("static const NpcModelMapping NPC_MODEL_MAP_INFERNO[] = {\n")
        for npc_id, synth_model, idle, attack, label in npc_entries:
            f.write(f"    {{{npc_id}, 0x{synth_model:X}, {idle}, {attack}}},  /* {label} */\n")
        f.write("};\n\n")

        # inferno NPC defines for walk anims and other useful data
        f.write("/* inferno NPC walk animation IDs */\n")
        for npc_id, npc in sorted(npc_defs.items()):
            safe_name = INFERNO_NPC_IDS[npc_id].split("(")[1].rstrip(")") if "(" in INFERNO_NPC_IDS[npc_id] else INFERNO_NPC_IDS[npc_id]
            safe_name = safe_name.replace(" ", "_").replace("-", "_").upper()
            if npc.walk_anim >= 0:
                f.write(f"#define INF_WALK_ANIM_{safe_name}  {npc.walk_anim}\n")
        f.write("\n")

        # inferno spotanim/GFX defines
        f.write("/* inferno spotanim (projectile/effect) model + animation IDs */\n")
        for gfx_id, model_id, seq_id, label in spotanim_entries:
            safe_label = label.replace(" ", "_").replace("(", "").replace(")", "").replace("?", "").upper()
            f.write(f"#define INF_GFX_{gfx_id}_MODEL  {model_id}  /* {label} */\n")
            if seq_id >= 0:
                f.write(f"#define INF_GFX_{gfx_id}_ANIM   {seq_id}\n")
        f.write("\n")

        # combined lookup function that searches both zulrah and inferno tables
        f.write("static const NpcModelMapping* npc_model_lookup(uint16_t npc_id) {\n")
        f.write("    for (int i = 0; i < (int)(sizeof(NPC_MODEL_MAP_ZULRAH) / sizeof(NPC_MODEL_MAP_ZULRAH[0])); i++) {\n")
        f.write("        if (NPC_MODEL_MAP_ZULRAH[i].npc_id == npc_id) return &NPC_MODEL_MAP_ZULRAH[i];\n")
        f.write("    }\n")
        f.write("    for (int i = 0; i < (int)(sizeof(NPC_MODEL_MAP_INFERNO) / sizeof(NPC_MODEL_MAP_INFERNO[0])); i++) {\n")
        f.write("        if (NPC_MODEL_MAP_INFERNO[i].npc_id == npc_id) return &NPC_MODEL_MAP_INFERNO[i];\n")
        f.write("    }\n")
        f.write("    return NULL;\n")
        f.write("}\n\n")

        f.write("#endif /* NPC_MODELS_H */\n")

    print(f"wrote {header_path}")

    # ================================================================
    # step 6: print encounter_inferno.h mapping table
    # ================================================================
    print("\n\n========================================")
    print("INF_NPC_DEF_IDS mapping table for encounter_inferno.h:")
    print("========================================")
    print("static const int INF_NPC_DEF_IDS[INF_NUM_NPC_TYPES] = {")

    inf_type_to_npc = {
        "INF_NPC_NIBBLER": 7691,
        "INF_NPC_BAT": 7692,
        "INF_NPC_BLOB": 7693,
        "INF_NPC_BLOB_MELEE": 7694,
        "INF_NPC_BLOB_RANGE": 7695,
        "INF_NPC_BLOB_MAGE": 7696,
        "INF_NPC_MELEER": 7697,
        "INF_NPC_RANGER": 7698,
        "INF_NPC_MAGER": 7699,
        "INF_NPC_JAD": 7700,
        "INF_NPC_ZUK": 7706,
        "INF_NPC_HEALER_JAD": 7701,
        "INF_NPC_HEALER_ZUK": 7708,
        "INF_NPC_ZUK_SHIELD": 7707,
    }
    for enum_name, npc_id in inf_type_to_npc.items():
        npc = npc_defs.get(npc_id)
        name = npc.name if npc else "UNKNOWN"
        print(f"    [{enum_name}] = {npc_id},  /* {name} */")
    print("};")

    print("\ndone.")


if __name__ == "__main__":
    main()
