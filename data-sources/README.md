# RuneC Source Inputs

`sources.lock` is the Phase 2 source inventory for RuneC-owned data inputs and
blocked local mirrors.

Rules:

- Tracked authored inputs live in `content/` and are hashed in `sources.lock`.
- Reviewed first-release runtime snapshots under `content/runtime_snapshots/`
  are tracked RuneC-owned source inputs for migrated datasets that have not yet
  been normalized into semantic content tables.
- Raw b237 cache inputs are maintainer-supplied paths passed with
  `RUNEC_B237_CACHE` or explicit CLI arguments. They may point at an ignored
  local install under `data/source/` for maintainer rebuilds.
- Decoded b237 dump inputs remain maintainer-supplied paths outside this repo,
  passed with `RUNEC_B237_DUMP` or explicit CLI arguments.
- External repositories and wiki caches are research references only. They must
  not live under this repo or become official pipeline inputs.
- Missing release-blocking parity facts belong in `source_gaps.json` until
  replaced with RuneC-owned content, native cache extraction, or reviewed
  first-release snapshot authority. An empty list means no formal source gap is
  currently blocking runtime-data publication.

Validate with:

```bash
python3 tools/validate_sources.py
```
