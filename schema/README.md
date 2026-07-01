# RuneC Runtime Schemas

This directory owns the tracked contract for runtime data formats consumed by
`rc-core` and `rc-viewer`.

Unless a schema says otherwise:

- integer fields are little-endian
- binary tables start with an explicit magic value and schema version
- loaders must reject unsupported magic/version pairs
- generated runtime files stay under ignored `data/`
- schema files, exporters, loaders, and source locks stay tracked in this repo

`schema/defs/*.schema.toml` documents table-level runtime contracts.
`schema/packs/runec_pack_v1.md` documents the runtime pack container and index.

The schema documents are intentionally compact in Phase 3. They pin the
runtime contract that already exists in loaders/exporters and give Phase 4 a
stable target for manifest and pipeline validation.
