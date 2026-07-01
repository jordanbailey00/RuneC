#!/usr/bin/env python3
"""Validate runtime-data.lock against the current data release gate."""
from __future__ import annotations

import argparse
import json
import sys
import tomllib
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / "runtime-data.lock"
SOURCE_GAPS = ROOT / "data-sources/source_gaps.json"
SUMMARY = ROOT / "generated/reports/data-v1-summary.json"

EXPECTED_FORMAT = "runec-runtime-data-lock-v1"
SOURCE_GAP_STATUS = "not_publishable_source_gaps"
PUBLISHABLE_STATUS = "publishable"


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def load_toml(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as f:
            data = tomllib.load(f)
    except tomllib.TOMLDecodeError as exc:
        raise SystemExit(f"{rel(path)}: invalid TOML: {exc}") from exc
    if not isinstance(data, dict):
        raise SystemExit(f"{rel(path)}: expected TOML table")
    return data


def load_source_gap_datasets(path: Path) -> list[str]:
    if not path.is_file():
        raise SystemExit(f"missing source gap file: {rel(path)}")
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise SystemExit(f"{rel(path)}: invalid JSON: {exc}") from exc
    if not isinstance(data, list):
        raise SystemExit(f"{rel(path)}: expected list")
    datasets: list[str] = []
    for idx, row in enumerate(data):
        if not isinstance(row, dict) or not row.get("dataset"):
            raise SystemExit(f"{rel(path)}: source gap row {idx} missing dataset")
        datasets.append(str(row["dataset"]))
    return sorted(datasets)


def load_summary(path: Path) -> dict[str, Any] | None:
    if not path.is_file():
        return None
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise SystemExit(f"{rel(path)}: invalid JSON: {exc}") from exc
    if not isinstance(data, dict):
        raise SystemExit(f"{rel(path)}: expected object")
    return data


def lock_gap_datasets(lock: dict[str, Any], failures: list[str]) -> list[str]:
    table = lock.get("source_gaps", {})
    if table is None:
        table = {}
    if not isinstance(table, dict):
        failures.append("source_gaps must be a TOML table")
        return []
    raw = table.get("datasets", [])
    if not isinstance(raw, list) or not all(isinstance(row, str) for row in raw):
        failures.append("source_gaps.datasets must be a list of dataset names")
        return []
    return sorted(raw)


def release_fields(lock: dict[str, Any], failures: list[str]) -> dict[str, str]:
    release = lock.get("release", {})
    if not isinstance(release, dict):
        failures.append("release must be a TOML table")
        return {}
    out: dict[str, str] = {}
    for key in ("manifest_url", "manifest_sha256", "packs_base_url", "release_tag"):
        value = release.get(key, "")
        if not isinstance(value, str):
            failures.append(f"release.{key} must be a string")
            value = ""
        out[key] = value
    return out


def notes_text(lock: dict[str, Any]) -> str:
    notes = lock.get("notes", {})
    if not isinstance(notes, dict):
        return ""
    return "\n".join(str(value) for value in notes.values())


def validate_lock(lock: dict[str, Any], source_gaps: list[str], summary: dict[str, Any] | None) -> list[str]:
    failures: list[str] = []

    if lock.get("format") != EXPECTED_FORMAT:
        failures.append(f"format must be {EXPECTED_FORMAT}")
    if not lock.get("data_version"):
        failures.append("data_version must be set")

    status = str(lock.get("status", ""))
    official = bool(lock.get("official_release", False))
    release_status = str(lock.get("release_status", ""))
    lock_datasets = lock_gap_datasets(lock, failures)
    release = release_fields(lock, failures)

    if lock_datasets != source_gaps:
        failures.append(
            "source_gaps.datasets mismatch: "
            f"lock={lock_datasets}, source_gaps={source_gaps}"
        )

    if source_gaps:
        if status != "draft":
            failures.append("runtime-data.lock must remain status=draft while source gaps exist")
        if official:
            failures.append("official_release must be false while source gaps exist")
        if release_status != SOURCE_GAP_STATUS:
            failures.append(
                f"release_status must be {SOURCE_GAP_STATUS} while source gaps exist"
            )
        nonempty_release = [key for key, value in release.items() if value]
        if nonempty_release:
            failures.append(
                "release metadata must stay empty while source gaps exist: "
                + ", ".join(nonempty_release)
            )
        note = notes_text(lock)
        for dataset in source_gaps:
            if dataset not in note:
                failures.append(f"notes must mention current source gap dataset: {dataset}")
    else:
        if release_status == SOURCE_GAP_STATUS:
            failures.append("release_status still reports source gaps, but no source gap rows remain")
        if official:
            if status == "draft":
                failures.append("official releases cannot use status=draft")
            if release_status != PUBLISHABLE_STATUS:
                failures.append("official releases must use release_status=publishable")
            missing_release = [key for key, value in release.items() if not value]
            if missing_release:
                failures.append(
                    "official releases require release metadata: "
                    + ", ".join(missing_release)
                )

    if summary is not None:
        summary_status = str(summary.get("release_status", ""))
        summary_gaps = sorted(str(row) for row in summary.get("source_gap_datasets", []))
        if summary_status and summary_status != release_status:
            failures.append(
                f"release_status mismatch with generated summary: "
                f"lock={release_status}, summary={summary_status}"
            )
        if summary_gaps != source_gaps:
            failures.append(
                "generated summary source_gap_datasets mismatch: "
                f"summary={summary_gaps}, source_gaps={source_gaps}"
            )

    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lock", type=Path, default=LOCK)
    parser.add_argument("--source-gaps", type=Path, default=SOURCE_GAPS)
    parser.add_argument("--summary", type=Path, default=SUMMARY)
    args = parser.parse_args()

    if not args.lock.is_file():
        print(f"missing runtime data lock: {rel(args.lock)}", file=sys.stderr)
        return 1

    lock = load_toml(args.lock)
    source_gaps = load_source_gap_datasets(args.source_gaps)
    summary = load_summary(args.summary)
    failures = validate_lock(lock, source_gaps, summary)
    if failures:
        print("runtime data lock validation failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    status = str(lock.get("release_status", ""))
    official = str(bool(lock.get("official_release", False))).lower()
    print(
        "runtime data lock validation passed: "
        f"official_release={official}, release_status={status}, "
        f"source_gaps={','.join(source_gaps) if source_gaps else 'none'}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
