RuneC frontend validation tools
===============================

This directory contains viewer/frontend validation and regression tooling.
Keep these scripts out of rc-core, rc-content, and normal runtime code paths.

Tools:

- validate_combat_visuals.py
  Validates combat visual profile rows against spotanim, model, and animation
  bundles.

- build_asset_bundle.py
  Builds scoped loose-asset viewer bundles from assets/manifests/*.json.

- capture_regression_screenshots.py
  Captures repeatable viewer screenshots from scoped bundles and writes hashes,
  logs, and reports for local regression comparison.

- validate_promoted_defaults.py
  Runs the Phase 8 default-path checks: source-level render-profile default
  check, strict Jad visual validation, scoped Fight Caves and Varrock bundle
  builds, and a dry-run screenshot plan. Pass --with-screenshots from a desktop
  session when you want actual screenshot artifacts.

Default frontend path:

- rc-viewer defaults to RUNEC_RENDER_PROFILE=osrs.
- RUNEC_RENDER_PROFILE=legacy remains the explicit rollback path.
- Broad NPC/item render exporters expose --model-lighting client|unlit, but
  their default remains unlit until client-lit broad exports are manually
  approved across representative scenes.
- Scoped test manifests can request client lighting metadata, but manifests are
  validation buckets, not production content routing.

Adding new visual content:

1. Implement or update backend combat/content logic so the game event is
   semantically correct.
2. Add or regenerate the combat visual profile row that describes how the event
   should look: animation, primitive, launch/travel/impact effects, timing, and
   attachment rules.
3. Add a scoped manifest only when the content needs a reproducible regression
   bucket, such as a boss arena, terrain stress area, or representative world
   slice.
4. Run strict validation for the scoped profile and broad validation for overall
   coverage.
5. Capture screenshots after manual visual approval and use them as regression
   references.
6. Do not add fight-specific branches to rc-viewer unless a reusable primitive
   is missing; add the primitive once, then select it from data.

Typical Phase 7 command:

python3 tools/frontend_validation/capture_regression_screenshots.py \
  --scene all \
  --repeat 2 \
  --compare-repeat \
  --write-baseline build/visual-regression/baseline.json

Phase 8 default-path command:

python3 tools/frontend_validation/validate_promoted_defaults.py

Optional Phase 8 screenshot capture:

python3 tools/frontend_validation/validate_promoted_defaults.py --with-screenshots

Generated screenshots, logs, and baselines are local artifacts under build/ by
default and are not committed.
