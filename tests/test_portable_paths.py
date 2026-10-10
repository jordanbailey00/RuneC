"""Portable bundle launchers and machine-specific dependency diagnostics."""
from __future__ import annotations

from argparse import Namespace
from contextlib import redirect_stdout
import io
import json
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from frontend_validation import build_asset_bundle as bundle
import validate_no_external_repos as guard
import validate_sources as sources
import validate_schemas as schemas


class PortablePathsTests(unittest.TestCase):
    def test_current_runtime_schema_contracts_match_validator(self):
        with redirect_stdout(io.StringIO()):
            self.assertEqual(schemas.main(), 0)

    def test_pack_spec_is_in_docs_and_missing_or_invalid_docs_fail(self):
        spec = ROOT / "docs/runec_pack_v1.md"
        self.assertTrue(spec.is_file(), "required pack specification missing from docs/")
        with TemporaryDirectory() as tmp, patch.object(schemas, "ROOT", Path(tmp)):
            path = Path(tmp) / "docs/runec_pack_v1.md"
            path.parent.mkdir()
            failures = []
            schemas.validate_pack_doc(failures)
            self.assertEqual(failures, ["pack format doc missing"])
            path.write_text("Incomplete specification\n")
            failures = []
            schemas.validate_pack_doc(failures)
            self.assertEqual(len(failures), 3)
            path.write_text(spec.read_text())
            failures = []
            schemas.validate_pack_doc(failures)
            self.assertEqual(failures, [])

    def test_source_lock_does_not_depend_on_ignored_local_notes(self):
        with TemporaryDirectory() as tmp, patch.object(sources, "ROOT", Path(tmp)):
            root = Path(tmp)
            (root / "definition.toml").write_text("version = 1\n")
            (root / "README.md").write_text("Schema docs\n")
            before = sources.tree_sha256(root)
            (root / "local.md").write_text("Ignored maintainer notes\n")
            self.assertEqual(sources.tree_sha256(root), before)
            (root / "definition.toml").write_text("version = 2\n")
            self.assertNotEqual(sources.tree_sha256(root), before)
            before = sources.tree_sha256(root)
            (root / "README.md").write_text("Changed tracked docs\n")
            self.assertNotEqual(sources.tree_sha256(root), before)

    def test_bundle_manifest_uses_relative_paths(self):
        with TemporaryDirectory() as tmp, patch.object(bundle, "ROOT", Path(tmp)):
            root = Path(tmp)
            source = root / "data/regions/test.objects"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"mesh")
            visuals = root / "visuals.tsv"
            visuals.write_text("kind|key|style\nweapon|1|melee\n")
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({
                "schema": "runec.asset_manifest.v1", "name": "portable",
                "scene": {"prefix": "data/regions/test"},
            }))
            output = root / "generated/bundle"
            args = Namespace(manifest=manifest, visuals=visuals, output=output,
                             dry_run=False, link_mode="copy", force=False, no_validate=True)
            with patch.object(bundle, "default_assets", return_value={}):
                with redirect_stdout(io.StringIO()):
                    bundle.build_bundle(args)
            resolved = json.loads((output / "bundle_manifest.json").read_text())
            self.assertEqual((output / resolved["source_manifest"]).resolve(), manifest)
            asset = resolved["assets"][0]
            self.assertEqual((output / asset["source"]).resolve(), source)
            self.assertEqual(asset["staged"], "regions/test.objects")
            self.assertNotIn(str(root), json.dumps(resolved))

    def test_bundle_survives_checkout_move_and_spaces(self):
        for mode in ("copy", "symlink"):
            with self.subTest(mode=mode), TemporaryDirectory() as tmp:
                root = Path(tmp) / "original checkout"
                source = root / "data/item.models"
                source.parent.mkdir(parents=True)
                source.write_text("item model\n")
                output = root / "generated/test bundle"
                output.mkdir(parents=True)
                bundle.stage_file(source, output / "item.models", mode, False)
                bundle.write_env_file(output / "run.env", bundle.env_from_manifest({}))
                bundle.write_runner(output / "run_viewer.sh", output / "run.env")
                for name in ("run.env", "run_viewer.sh"):
                    self.assertNotIn(str(root), (output / name).read_text())
                if mode == "symlink":
                    self.assertFalse((output / "item.models").readlink().is_absolute())
                viewer = root / "build/rc-viewer"
                viewer.parent.mkdir()
                viewer.write_text(
                    '#!/usr/bin/env bash\n'
                    'printf "%s\\n" "$RUNEC_DATA_ROOT" "$@"\n'
                    'cat "$RUNEC_DATA_ROOT/item.models"\n'
                )
                viewer.chmod(0o755)
                moved = Path(tmp) / "renamed checkout"
                root.rename(moved)
                runner = moved / "generated/test bundle/run_viewer.sh"
                env = dict(os.environ)
                env.pop("RUNEC_VIEWER", None)
                result = subprocess.run([str(runner), "argument with spaces"],
                                        cwd=moved, env=env, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.splitlines(),
                                 [str(runner.parent), "argument with spaces", "item model"])
                env["RUNEC_VIEWER"] = str(moved / "build/rc-viewer")
                result = subprocess.run([str(runner)], cwd=tmp, env=env,
                                        text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                env["RUNEC_VIEWER"] = str(moved / "missing-viewer")
                result = subprocess.run([str(runner)], cwd=tmp, env=env,
                                        text=True, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("viewer not executable", result.stderr)

    def test_scene_paths_are_logical_not_external_destinations(self):
        with TemporaryDirectory() as tmp, patch.object(bundle, "ROOT", Path(tmp)):
            self.assertEqual(bundle.data_logical_path(Path(tmp) / "data/regions/a.objects"),
                             "regions/a.objects")
            with self.assertRaisesRegex(SystemExit, "scene assets must be inside"):
                bundle.data_logical_path(Path(tmp) / "outside.objects")

    def test_guard_reports_machine_paths_in_code_and_configuration(self):
        paths = (
            "/" + "home/alice/work/repo/data",
            "/" + "Users/alice/work/repo/data",
            "C:" + "\\Users\\alice\\repo",
            "../RuneC_" + "v2/data",
            "/usr/" + "lib/x86_64-linux-gnu/libGL.so.1",
        )
        with TemporaryDirectory() as tmp, patch.object(guard, "ROOT", Path(tmp)):
            root = Path(tmp)
            (root / "tools").mkdir()
            (root / ".github/workflows").mkdir(parents=True)
            (root / "tools/bad.py").write_text("\n".join(paths))
            (root / ".github/workflows/test.yml").write_text(paths[0])
            (root / "README.md").write_text(paths[0])
            findings = guard.scan(guard.MACHINE_PATH_PATTERNS, skip_markdown=True)
            self.assertEqual(len(findings), len(paths) + 1)
            self.assertEqual({row[0] for row in findings},
                             {"tools/bad.py", ".github/workflows/test.yml"})
            with patch.object(sys, "argv", ["guard"]), redirect_stdout(io.StringIO()) as out:
                self.assertEqual(guard.main(), 1)
            self.assertIn("hardcoded home directory", out.getvalue())

    def test_guard_accepts_runtime_paths_and_os_conventions(self):
        with TemporaryDirectory() as tmp, patch.object(guard, "ROOT", Path(tmp)):
            path = Path(tmp) / "tools/portable.py"
            path.parent.mkdir()
            path.write_text('ROOT = Path(__file__).resolve().parents[1]\n'
                            'cache = os.environ["RUNEC_B237_CACHE"]\n'
                            '#!/usr/bin/env python3\n/tmp/runec-test\n')
            self.assertEqual(guard.scan(guard.MACHINE_PATH_PATTERNS, skip_markdown=True), [])
            with patch.object(sys, "argv", ["guard"]), redirect_stdout(io.StringIO()):
                self.assertEqual(guard.main(), 0)


if __name__ == "__main__":
    unittest.main()
