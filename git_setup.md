# RuneC Git and Data Setup Plan

This document describes the recommended Git/data layout for RuneC_v4. The goal is:

- keep the main Git repo small, reviewable, and clone-friendly;
- make the runtime data easy for users to obtain;
- keep historical tracking for data changes;
- avoid mixing OSRS b237-derived data with private-server or wrong-game sources.

## Short Recommendation

Use a two-layer model:

1. **Main RuneC repo, normal Git**
   - Tracks code, tools, docs, source manifests, schemas, curated OSRS content, tests, and small fixtures.
   - Does not track raw caches, loose generated assets, or large runtime packs.

2. **Versioned RuneC runtime data releases**
   - Publishes generated `dist-data/manifest.json` and `dist-data/packs/*.pak` as release assets, or in a separate data-release repo.
   - Users run one setup command to download the exact data version required by the checked-out code.
   - Every release is checksum-verified and tied back to source manifests, exporter versions, and a RuneC commit.

This is the best fit for RuneC because current generated data is already much larger than normal Git handles well. In the current RuneC_v4 workspace, `data/` is roughly gigabytes of loose generated output, `dist-data/` is hundreds of megabytes, and several individual files are larger than GitHub's normal 100 MiB file limit.

## Why Not Commit All Data to Main Git?

Git is excellent for source code, small text content, schemas, manifests, and reviewed curated data. It is a poor fit for large generated binary assets that change in large chunks.

GitHub's own limits matter here:

- GitHub warns when files are over 50 MiB.
- GitHub blocks normal Git pushes for files over 100 MiB.
- GitHub recommends keeping repositories ideally under 1 GiB and strongly under 5 GiB.
- Git LFS stores pointers in Git and large content separately, but users must have LFS available and storage/bandwidth limits can become part of the project.

Sources:

- GitHub large files: https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github
- Git LFS: https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-git-large-file-storage

RuneC has generated model, region, object, collision, and pack data that crosses these thresholds. Committing all of it directly to the main repo would make clones slower, reviews noisy, history larger, and cleanup harder.

## What Should Be Tracked in Main Git?

Track these in the main RuneC repo:

- `src/`, `rc-*`, `tools/`, build files, tests, docs.
- `content/` and other human-authored OSRS content files.
- `schemas/` for generated data formats.
- `data-sources/sources.lock` or equivalent source lock files.
- `runtime-data.lock` or `data-version.toml`, pointing to the exact runtime data release.
- Exporter scripts and validation/reporting scripts.
- Small test fixtures.
- Small hand-authored overrides, with source and rationale comments.

Do not track these in the main RuneC repo:

- `data/`
- `dist-data/`
- raw b237 cache directories
- wiki cache snapshots
- large generated binary reports
- loose generated models, regions, sprites, animations, object placement tables, or collision tables
- private-server or wrong-game source mirrors such as VoidPS, 2011Scape, Near-Reality, or Zenyte

The current `.gitignore` policy should continue to ignore generated data directories while explicitly allowing important planning docs such as this file and `data_cleanup.md`.

## Source Authority Policy

RuneC should treat the OSRS b237 cache as the primary source for renderable game data.

Allowed source families:

- OSRS b237 cache and derived dumps from the exact b237 source set.
- RuneC-native cache decoders, tools, schemas, and generated reports.
- RuneC-authored curated OSRS content and overrides when the source, reason, and gap are documented.
- Off-repo research references such as RuneLite or RSMod only after the needed behavior has been rebuilt into RuneC-owned code/data.

Blocked source families for generated runtime data:

- VoidPS, because it targets original RuneScape 2011-era behavior and data.
- 2011Scape, for the same wrong-game/wrong-cache reason.
- Near-Reality/Zenyte, because it is private-server content and not an OSRS b237 authority.
- Any other private-server repo unless explicitly approved as a temporary, quarantined reference.
- Cloned external repo checkouts under the RuneC root, including RuneLite, RSMod, osrsreboxed, and data_osrs.

These blocked sources can still be read as historical references while designing systems, but they should not feed generated runtime assets, gameplay tables, visual tables, or area definitions.

## Recommended Runtime Data Shape

Generated runtime data should be packed into release artifacts:

```text
dist-data/
  manifest.json
  packs/
    runec-defs-v1.pak
    runec-regions-varrock-v1.pak
    runec-models-npcs-v1-000.pak
    runec-models-projectiles-v1.pak
    runec-sprites-v1.pak
    runec-anims-v1.pak
```

The release manifest should include:

- RuneC code commit SHA.
- Data build ID.
- OSRS cache revision and cache source ID.
- Source lock hash.
- Content catalog hash.
- Exporter/tool versions.
- Pack names, sizes, SHA-256 hashes, and content domains.
- Required packs vs optional packs.
- Generated report hashes.
- Blocked-source audit result.

Suggested release tag format:

```text
data-vYYYY.MM.DD-b237.N
data-v0.1.0-b237
```

The exact naming matters less than immutability. Once a release tag is used by a committed `runtime-data.lock`, do not mutate it. Publish a new data release instead.

## Recommended User Setup

The user-facing flow should be simple:

```bash
git clone https://github.com/<owner>/RuneC.git
cd RuneC
./scripts/setup-data.sh
cmake -S . -B build
cmake --build build
./build/rc-viewer
```

`scripts/setup-data.sh` should:

- read `runtime-data.lock`;
- download the matching `manifest.json` and packs;
- verify SHA-256 checksums;
- install them into the expected local runtime directory;
- print a direct fix if data is missing, stale, or corrupt.

Useful setup flags:

```bash
./scripts/setup-data.sh --minimal
./scripts/setup-data.sh --full
./scripts/setup-data.sh --verify
./scripts/setup-data.sh --offline /path/to/dist-data
```

Normal user setup should download RuneC-published runtime packs only. Raw cache
or research-source acquisition is a maintainer workflow and should use explicit
paths outside the RuneC repo root.

The app itself should also check for data at startup. If data is missing, it should print the exact setup command instead of failing with a low-level file error.

## Recommended Maintainer Flow

The maintainer flow should be reproducible but separate from the casual user flow.

```bash
export RUNEC_B237_CACHE=/path/outside/RuneC_v4/b237/cache
python3 tools/data_pipeline.py validate-repo-self-contained
python3 tools/data_pipeline.py validate-source-authority
python3 tools/data_pipeline.py validate-content
python3 tools/data_pipeline.py export-content
python3 tools/data_pipeline.py export-cache-derived-assets
python3 tools/data_pipeline.py export-defs
python3 tools/data_pipeline.py export-render-assets
python3 tools/data_pipeline.py pack-runtime-data
python3 tools/data_pipeline.py generate-reports
python3 tools/data_pipeline.py validate-reports
```

Maintainers should commit:

- source code changes;
- exporter changes;
- schemas;
- curated OSRS content;
- source lock changes;
- runtime data lock changes;
- release notes or data changelog entries.

Maintainers should not commit:

- `data/`;
- `dist-data/`;
- downloaded cache files;
- generated pack files;
- generated loose frontend assets.

## Historical Tracking

The project gets history from three places:

1. **Git history**
   - tracks code, tools, curated content, source locks, and runtime data locks.

2. **Immutable data releases**
   - track generated binary outputs, pack checksums, and manifests.

3. **Generated reports**
   - explain what changed between data builds, what sources were used, and what gaps remain.

Recommended files:

```text
runtime-data.lock
data-sources/sources.lock
docs/data-changelog.md
generated/reports/source_authority_report.json
generated/reports/data_diff_summary.md
```

Only commit generated reports when they are intentionally curated summaries. Full generated reports can live as release artifacts.

## Optional Data Repo

A separate repo such as `RuneC-Data` can be useful, but it should be treated as an implementation detail.

Good uses for a separate data repo:

- hosting GitHub Release assets;
- storing Git LFS artifacts for maintainers;
- keeping binary data history outside the code repo;
- mirroring release manifests and reports.

Bad uses for a separate data repo:

- requiring every casual user to understand submodules;
- requiring Git LFS for a basic clone unless there is no simpler download path;
- storing unversioned loose generated files without a manifest.

If a data repo exists, the main RuneC repo should still have one command that fetches the correct data for users.

## Alternatives Considered

### Commit Everything to Main Git

Not recommended.

This is easy to understand at first, but it quickly makes the repo too large, violates normal GitHub file limits, and creates noisy binary history.

### Use Git LFS in Main Repo

Acceptable only for a small, stable data set.

Git LFS is workable, but it adds setup requirements and storage/bandwidth concerns. For RuneC, it is better as an optional separate data repo mechanism than as the default main-repo path.

### Use a Separate Git LFS Data Repo

Useful for maintainers.

This gives binary history without bloating the source repo. It still should be hidden behind `scripts/setup-data.sh` for normal users.

### Use GitHub Releases or Object Storage

Recommended default.

Release assets are easy for users to download, easy to checksum, and easy to tie to a source commit. This keeps the main repo clean while still giving users an easy setup path.

### Use DVC, git-annex, or S3

Useful later if the data pipeline becomes very large or needs frequent binary diffs.

These tools are powerful but add operational complexity. RuneC should start with release artifacts and checksummed manifests.

## How Similar RuneScape Projects Handle Data

These examples are about distribution patterns. They are not all source-authority recommendations for RuneC.

### RSMod

RSMod keeps the code repo clone-friendly and downloads required game files during setup/run. Its README says the first server run automatically downloads required game files, and its normal flow is Gradle-based.

Source:

- https://raw.githubusercontent.com/rsmod/rsmod/master/README.md

Pattern to copy:

- code in Git;
- required game files fetched automatically;
- setup hidden behind normal developer commands.

RuneC equivalent:

```bash
./scripts/setup-data.sh
./build/rc-viewer
```

or eventually first-run auto-download if the data is missing.

### 2009Scape

2009Scape uses Git LFS as part of its setup. Its README tells users to run `git lfs pull` once after cloning. It also has tooling for editing content data.

Source:

- https://gitlab.com/2009scape/2009scape/-/raw/master/README.md

Pattern to learn from:

- large project data can be versioned with LFS;
- content contributors can edit structured data in repo/tooling;
- users need one extra setup step after clone.

RuneC should probably not require Git LFS in the main repo, but a separate `RuneC-Data` LFS repo is a reasonable maintainer option.

### RuneLite

RuneLite is mostly a client/tooling/code repository. Its README describes a `cache` module for reading and writing cache files and cache data, but RuneLite does not treat its main repo as a giant dump of generated game assets.

Source:

- https://raw.githubusercontent.com/runelite/runelite/master/README.md

Pattern to copy:

- keep cache-reading logic and client behavior in source control;
- do not turn the source repo into the cache artifact store.

RuneC equivalent:

- track cache decoders/exporters;
- track schemas and manifests;
- publish generated RuneC packs separately.

### OpenRS2

OpenRS2 separates code from non-free cache/client data. Its README says original RuneScape client code, data, and keys cannot be legally distributed with the project and are kept under ignored `nonfree` paths. OpenRS2 Archive separately provides a cache and XTEA archive with provenance and content-addressed storage.

Sources:

- https://raw.githubusercontent.com/openrs2/openrs2/master/README.md
- https://archive.openrs2.org/

Pattern to copy:

- treat raw cache/client data as external source material;
- keep source manifests and reproducible import paths;
- avoid committing raw non-free inputs into the source repo.

RuneC equivalent:

- pin b237 source IDs and checksums;
- rebuild derived packs from locked sources;
- keep raw cache inputs out of main Git.

### Lost City

Lost City uses a higher-level repository and scripts to make setup easier. Its README notes that it avoids normal Git submodules for broad usability because GitHub ZIP downloads do not include submodules, and it provides start scripts that guide users through setup.

Source:

- https://raw.githubusercontent.com/LostCityRS/Server/main/README.md

Pattern to copy:

- optimize for one-command setup;
- avoid exposing casual users to repo wiring details;
- wrap multi-repo or multi-artifact setup in scripts.

RuneC equivalent:

- do not make users manually fetch cache files, data packs, and manifests from several places;
- make `scripts/setup-data.sh` the one supported entry point.

## RuneC Mental Model

Think of RuneC data in four layers:

1. **Sources**
   - b237 cache plus RuneC-owned source tables, schemas, decoders, and
     curated OSRS overrides.
   - Raw b237 input is supplied outside the repo root for maintainer rebuilds
     and locked by checksum/source manifest.
   - RuneLite, RSMod, osrsreboxed, data_osrs, and similar projects are
     off-repo research references only; needed behavior must be rebuilt into
     RuneC-native code/data before it enters the pipeline.

2. **Curated Content**
   - RuneC-authored OSRS content, fixes, overrides, and gap fillers.
   - Tracked in normal Git.

3. **Generated Work Area**
   - `data/`, generated reports, loose exported assets.
   - Ignored by Git.

4. **Runtime Distribution**
   - packed `dist-data` release artifacts.
   - downloaded by users and verified by checksum.

This gives users an easy clone-and-run path while giving maintainers a reproducible audit trail.

## Implementation Checklist

- Add `runtime-data.lock`.
- Add `scripts/setup-data.sh`.
- Add startup data validation with a clear missing-data message.
- Add source lock validation.
- Add blocked-source validation for VoidPS, 2011Scape, Near-Reality, Zenyte, and other private-server sources.
- Add data release manifest generation.
- Add data release upload instructions.
- Add data diff report generation.
- Keep `data/`, `dist-data/`, and raw cache/source corpora ignored in main Git.

## Recommended Final State

For users:

```bash
git clone https://github.com/<owner>/RuneC.git
cd RuneC
./scripts/setup-data.sh
cmake -S . -B build
cmake --build build
./build/rc-viewer
```

For maintainers:

```bash
git checkout -b data/b237-refresh
./scripts/setup-sources.sh
python3 tools/data_pipeline.py all
./scripts/publish-data-release.sh data-v0.1.0-b237
git add runtime-data.lock data-sources/sources.lock docs/data-changelog.md
git commit -m "Update RuneC b237 runtime data"
```

That keeps RuneC easy to clone, easy to audit, and much less likely to drift into mixed private-server data.
