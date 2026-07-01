#!/usr/bin/env python3
"""Disabled legacy NPC exporter.

Use the current OSRS-native exporters instead:

- tools/export_npc_defs_full.py for NPC definitions
- tools/export_npc_models_full.py for NPC render models
- tools/export_spawns.py for static spawn rows

The old combined exporter depended on a wrong-game spawn corpus and is kept
only as a failing compatibility entry point so old commands stop loudly.
"""
from __future__ import annotations

import sys


def main() -> int:
    print(
        "tools/export_npcs.py is disabled. Use export_npc_defs_full.py, "
        "export_npc_models_full.py, and export_spawns.py instead.",
        file=sys.stderr,
    )
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
