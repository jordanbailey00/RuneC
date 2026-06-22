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

Typical Phase 7 command:

python3 tools/frontend_validation/capture_regression_screenshots.py \
  --scene all \
  --repeat 2 \
  --compare-repeat \
  --write-baseline build/visual-regression/baseline.json

Generated screenshots, logs, and baselines are local artifacts under build/ by
default and are not committed.
