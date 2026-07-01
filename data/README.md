# data/

Ignored local runtime-data install.

Do not place source-of-truth content, raw cache inputs, decoded dumps, wiki
caches, or external repo mirrors here. Runtime files under this directory are
local generated/downloaded artifacts and should be safe to delete and reinstall.

Source ownership and maintainer rebuild inputs are tracked under
`data-sources/`.

Use the Phase 4 pipeline entrypoint for local generated runtime data:

```bash
python3 tools/data_pipeline.py all
python3 tools/data_pipeline.py --check
```

The pipeline writes `data/manifest.json` for the local loose-file install and
`dist-data/manifest.json` plus `dist-data/packs/` for distributable runtime
packs. These outputs remain ignored generated artifacts.
