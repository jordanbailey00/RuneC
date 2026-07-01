Deprecated local source area.

Phase 2 source locking moved raw cache/source inputs out of the RuneC repo
root. Do not place OpenRS2 caches, decoded dumps, wiki caches, or external repo
mirrors here.

Maintainer rebuild tools now require explicit inputs such as:

  RUNEC_B237_CACHE=/abs/path/to/cache
  RUNEC_B237_DUMP=/abs/path/to/decoded-dump

The approved source inventory lives in data-sources/sources.lock.
