# RuneC Authored Content

`content/` is the tracked source of truth for reviewed, human-authored RuneC
content tables.

These files replace the former `data/curated/` location. Exporters should read
from `content/` by default and may temporarily fall back to `data/curated/`
with a warning while older local checkouts are migrated.

Rules:

- Keep authored TOML here, not under ignored `data/`.
- Generated runtime binaries still go under `data/defs`, `data/spawns`,
  `data/regions`, or release packs.
- Missing source facts become generated source-gap reports, not private-source
  fallbacks.
- Add each authored dataset to `content/catalog.toml`.
