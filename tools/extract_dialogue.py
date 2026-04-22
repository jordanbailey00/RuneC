#!/usr/bin/env python3
"""Parse cached `Transcript:{NPC}` wikitexts into dialogue state
machines.

Wiki transcript format uses nested bullet lists with these templates:
  {{Transcript|Quest}}       — page-header tag
  {{Transcript list|NPC}}    — NPC association header
  {{topt|text}}              — player option (dialogue choice button)
  {{tselect|prompt}}         — selection prompt header
  {{tcond|condition}}        — conditional branch
  {{tbox|pic=X|text}}        — info box
  {{tact|end}} / {{tact|other}} — dialogue terminators
  '''Speaker:''' text        — speaker + line

Nesting depth (asterisk count) encodes the tree.

Output: `data/curated/dialogue/{slug}.toml` per transcript, with a
list of `[[nodes]]` forming the state machine. Each node has:
  id, depth, kind, speaker, text, next (list of child node ids),
  is_terminal

This is a first-pass structural extract — good enough for the engine
to jump between nodes. Semantic cleanup (resolving {{tcond}}
branches into explicit checks) is future work.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

CACHE = Path("/home/joe/projects/RuneC_copy/tools/wiki_cache/pages")
OUT = Path("/home/joe/projects/RuneC_copy/data/curated/dialogue")
REPORT = Path("/home/joe/projects/RuneC_copy/tools/reports/dialogue.txt")

_SANITIZE_RE = re.compile(r"[^A-Za-z0-9._-]+")
# Match a list bullet: one or more asterisks followed by space.
_BULLET_RE = re.compile(r"^(\*+)\s*(.*)$")
# Speaker lines: '''Name:''' text
_SPEAKER_RE = re.compile(r"'''([^']+?):'''\s*(.*)")
# Template: {{name|args...}}
_TEMPLATE_RE = re.compile(r"\{\{([A-Za-z][^|}\n]*)\s*\|?\s*([^}]*)\}\}")


def _sanitize(title: str) -> str:
    s = _SANITIZE_RE.sub("_", title).strip("_")
    return s[:180] or "_"


def classify_line(body: str) -> tuple[str, str, str]:
    """Return (kind, speaker, text).

    Kinds: speaker, option, select, cond, box, act, other.
    """
    body = body.strip()
    m = _TEMPLATE_RE.match(body)
    if m:
        tmpl = m.group(1).strip().lower()
        args = m.group(2).strip()
        if tmpl == "topt":
            return "option", "", args
        if tmpl == "tselect":
            return "select", "", args
        if tmpl == "tcond":
            return "cond", "", args
        if tmpl == "tbox":
            return "box", "", args
        if tmpl == "tact":
            return "act", "", args
    m = _SPEAKER_RE.match(body)
    if m:
        return "speaker", m.group(1).strip(), m.group(2).strip()
    return "other", "", body


def parse_transcript(wt: str) -> list[dict]:
    """Parse wikitext into a flat list of nodes with depth + parent."""
    nodes: list[dict] = []
    # Strip page-header templates we don't care about.
    lines = wt.split("\n")
    # Track the latest-seen node at each depth so children can link
    # to parent.
    depth_stack: dict[int, int] = {}   # depth → last node id at that depth

    for lineno, raw in enumerate(lines):
        m = _BULLET_RE.match(raw)
        if not m:
            continue
        depth = len(m.group(1))
        body = m.group(2).strip()
        if not body:
            continue
        kind, speaker, text = classify_line(body)
        node = {
            "id": len(nodes),
            "depth": depth,
            "kind": kind,
            "speaker": speaker,
            "text": text,
            "line": lineno,
            "parent": depth_stack.get(depth - 1, -1),
        }
        nodes.append(node)
        depth_stack[depth] = node["id"]
        # Invalidate deeper stack entries — we're at a shallower
        # level, deeper siblings are no longer valid parents.
        for d in list(depth_stack):
            if d > depth:
                del depth_stack[d]

    # Resolve children for each node
    for n in nodes:
        n["children"] = [c["id"] for c in nodes if c["parent"] == n["id"]]
        n["is_terminal"] = (
            n["kind"] == "act" and n["text"].lower() in ("end", "other")
        )

    return nodes


def toml_escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


def write_toml(path: Path, *, title: str, npcs: list[str],
               nodes: list[dict]):
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        f'title = "{toml_escape(title)}"',
        f'npcs = {json.dumps(npcs)}',
        f'node_count = {len(nodes)}',
        '',
    ]
    for n in nodes:
        lines.append('[[nodes]]')
        lines.append(f'id = {n["id"]}')
        lines.append(f'depth = {n["depth"]}')
        lines.append(f'kind = "{n["kind"]}"')
        if n["speaker"]:
            lines.append(f'speaker = "{toml_escape(n["speaker"])}"')
        if n["text"]:
            # Multi-line safe basic string.
            text = n["text"].replace("\r", "").strip()
            if "\n" in text or len(text) > 200:
                lines.append('text = """')
                lines.append(toml_escape(text))
                lines.append('"""')
            else:
                lines.append(f'text = "{toml_escape(text)}"')
        lines.append(f'parent = {n["parent"]}')
        if n["children"]:
            lines.append(f'children = {json.dumps(n["children"])}')
        if n["is_terminal"]:
            lines.append('is_terminal = true')
        lines.append('')
    path.write_text("\n".join(lines))


def load_npcs_from_bucket() -> dict[str, list[str]]:
    """Map Transcript page_name → list of NPCs from bucket cache."""
    out: dict[str, list[str]] = {}
    cache = Path("/home/joe/projects/RuneC_copy/tools/wiki_cache")
    for p in sorted(cache.glob("transcript_*.json")):
        d = json.loads(p.read_text())
        for r in d.get("bucket", []):
            pn = r.get("page_name") or ""
            if not pn.startswith("Transcript:"):
                continue
            npcs = r.get("npcs") or []
            if not isinstance(npcs, list):
                npcs = [npcs]
            out.setdefault(pn, [])
            for n in npcs:
                if n and n not in out[pn]:
                    out[pn].append(n)
    return out


def main():
    npcs_map = load_npcs_from_bucket()
    emitted = 0
    skipped_empty = 0
    skipped_missing_cache = 0
    total_nodes = 0

    for title, npcs in sorted(npcs_map.items()):
        cache_path = CACHE / f"{_sanitize(title)}.json"
        if not cache_path.exists():
            skipped_missing_cache += 1
            continue
        d = json.loads(cache_path.read_text())
        wt = d.get("wikitext") or ""
        if not wt:
            skipped_empty += 1
            continue
        nodes = parse_transcript(wt)
        if not nodes:
            skipped_empty += 1
            continue
        slug = _sanitize(title.removeprefix("Transcript:"))
        out_path = OUT / f"{slug}.toml"
        write_toml(out_path, title=title, npcs=npcs, nodes=nodes)
        emitted += 1
        total_nodes += len(nodes)

    REPORT.parent.mkdir(parents=True, exist_ok=True)
    with REPORT.open("w") as f:
        f.write(f"transcripts in bucket: {len(npcs_map)}\n")
        f.write(f"emitted TOMLs: {emitted}\n")
        f.write(f"missing from page cache: {skipped_missing_cache}\n")
        f.write(f"empty wikitext: {skipped_empty}\n")
        f.write(f"total dialogue nodes: {total_nodes}\n")
        f.write(f"avg nodes per transcript: {total_nodes / max(1, emitted):.1f}\n")
    print(f"  → {OUT}/ ({emitted} TOMLs, {total_nodes} total nodes)",
          file=sys.stderr)
    print(f"  → {REPORT}", file=sys.stderr)


if __name__ == "__main__":
    main()
