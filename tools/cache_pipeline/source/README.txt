Local-only cache pipeline source area.

This directory is intentionally ignored except for this note and its .gitignore.
Place large raw OpenRS2 caches, decoded cache dumps, key files, and map images
here for local exporter testing.

Current expected layout:

  current_fightcaves_demo/data/cache/
    OpenRS2 flat OSRS cache extracted from cache-oldschool-live-en-b237-2026-04-29-10-45-05-openrs2#2528.tar.gz

  current_fightcaves_demo/data/keys.json
    Matching b237 OpenRS2 keys file. For this cache it is an empty JSON list
    because b237 cache groups are not XTEA-encrypted.

  current_fightcaves_demo/data/map-oldschool-live-en-b236-2026-03-18-11-45-07-openrs2#2499.png
    b236 map PNG kept as a visual reference only.

  osrs-dumps/
    Shallow clone of Joshua-F/osrs-dumps at 2026-04-29-rev237, used as a
    decoded definition and symbol validation oracle.
