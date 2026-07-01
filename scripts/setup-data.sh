#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOCK_FILE="${RUNEC_RUNTIME_DATA_LOCK:-${ROOT}/runtime-data.lock}"
DATA_DIR="${RUNEC_DATA_DIR:-${ROOT}/data}"
PACK_DIR="${RUNEC_PACK_DIR:-${DATA_DIR}/packs}"
DOWNLOAD_DIR="${DATA_DIR}/.download"
UNPACK_DATA="${RUNEC_DATA_UNPACK:-1}"
UNPACK_FORCE="${RUNEC_DATA_UNPACK_FORCE:-0}"
OFFLINE_DIR=""
VERIFY_ONLY=0

usage() {
    cat >&2 <<EOF
Usage: scripts/setup-data.sh [--offline DIST_DATA_DIR] [--verify]

Installs validated RuneC runtime data from runtime-data.lock. Because the
current lock is draft/non-official, default remote downloads are disabled
until an approved release URL is recorded. Use --offline dist-data for local
pipeline output, or set RUNEC_DATA_MANIFEST_URL and RUNEC_DATA_BASE_URL to an
explicit release location.

Environment:
  RUNEC_DATA_DIR              Install root (default: ./data)
  RUNEC_PACK_DIR              Pack install dir (default: RUNEC_DATA_DIR/packs)
  RUNEC_DATA_UNPACK=0         Keep packs only; do not expand loose files
  RUNEC_DATA_MANIFEST_URL     Explicit remote manifest URL
  RUNEC_DATA_BASE_URL         Explicit remote pack base URL
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --offline)
            if [ "$#" -lt 2 ]; then
                echo "setup-data: --offline requires a directory" >&2
                exit 2
            fi
            OFFLINE_DIR="$2"
            shift 2
            ;;
        --verify)
            VERIFY_ONLY=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "setup-data: unknown argument: $1" >&2
            usage
            exit 2
            ;;
    esac
done

need() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "setup-data: missing required command: $1" >&2
        exit 1
    fi
}

download() {
    local url="$1"
    local out="$2"
    echo "setup-data: downloading ${url}"
    curl -fL --retry 3 --retry-delay 2 -o "${out}" "${url}"
}

verify_file() {
    local path="$1"
    local sha="$2"
    local size="$3"
    echo "${sha}  ${path}" | sha256sum -c -
    if [ "${size}" != "0" ]; then
        local actual_size
        actual_size="$(wc -c < "${path}")"
        if [ "${actual_size}" != "${size}" ]; then
            echo "setup-data: size mismatch for ${path}: got ${actual_size}, expected ${size}" >&2
            exit 1
        fi
    fi
}

local_pack_path() {
    local pack_path="$1"
    local pack_name="$2"
    if [ -f "${OFFLINE_DIR}/${pack_path}" ]; then
        printf '%s\n' "${OFFLINE_DIR}/${pack_path}"
    elif [ -f "${OFFLINE_DIR}/packs/${pack_name}" ]; then
        printf '%s\n' "${OFFLINE_DIR}/packs/${pack_name}"
    elif [ -f "${OFFLINE_DIR}/${pack_name}" ]; then
        printf '%s\n' "${OFFLINE_DIR}/${pack_name}"
    else
        return 1
    fi
}

need python3
need sha256sum

if [ ! -f "${LOCK_FILE}" ]; then
    echo "setup-data: missing runtime data lock: ${LOCK_FILE}" >&2
    exit 1
fi

LOCK_INFO="$(python3 - "${LOCK_FILE}" <<'PY'
import shlex
import sys
import tomllib

path = sys.argv[1]
with open(path, "rb") as f:
    data = tomllib.load(f)

release = data.get("release", {})
values = {
    "LOCK_STATUS": data.get("status", ""),
    "LOCK_OFFICIAL_RELEASE": str(bool(data.get("official_release", False))).lower(),
    "LOCK_DATA_VERSION": data.get("data_version", ""),
    "LOCK_MANIFEST_URL": release.get("manifest_url", ""),
    "LOCK_MANIFEST_SHA256": release.get("manifest_sha256", ""),
    "LOCK_PACKS_BASE_URL": release.get("packs_base_url", ""),
}

for key, value in values.items():
    print(f"{key}={shlex.quote(str(value))}")
PY
)"
eval "${LOCK_INFO}"

VERSION="${RUNEC_DATA_VERSION:-${LOCK_DATA_VERSION}}"
MANIFEST_URL="${RUNEC_DATA_MANIFEST_URL:-${LOCK_MANIFEST_URL}}"
BASE_URL="${RUNEC_DATA_BASE_URL:-${LOCK_PACKS_BASE_URL}}"
MANIFEST_SHA="${RUNEC_DATA_MANIFEST_SHA256:-${LOCK_MANIFEST_SHA256}}"
if [ -z "${BASE_URL}" ] && [ -n "${MANIFEST_URL}" ]; then
    BASE_URL="${MANIFEST_URL%/*}"
fi

mkdir -p "${DOWNLOAD_DIR}" "${PACK_DIR}"

if [ "${VERIFY_ONLY}" = "1" ]; then
    MANIFEST_TMP="${DATA_DIR}/manifest.json"
    if [ ! -f "${MANIFEST_TMP}" ]; then
        echo "setup-data: cannot verify; missing ${MANIFEST_TMP}" >&2
        exit 1
    fi
elif [ -n "${OFFLINE_DIR}" ]; then
    MANIFEST_TMP="${OFFLINE_DIR}/manifest.json"
    if [ ! -f "${MANIFEST_TMP}" ]; then
        echo "setup-data: offline manifest missing: ${MANIFEST_TMP}" >&2
        exit 1
    fi
else
    if [ "${LOCK_OFFICIAL_RELEASE}" != "true" ] \
            && [ -z "${RUNEC_DATA_MANIFEST_URL:-}" ]; then
        echo "setup-data: runtime-data.lock is ${LOCK_STATUS:-unknown}/non-official; refusing default remote download" >&2
        echo "setup-data: run scripts/setup-data.sh --offline dist-data, or set RUNEC_DATA_MANIFEST_URL and RUNEC_DATA_BASE_URL" >&2
        exit 1
    fi
    if [ -z "${MANIFEST_URL}" ] || [ -z "${BASE_URL}" ]; then
        echo "setup-data: missing release URLs in runtime-data.lock or environment" >&2
        exit 1
    fi
    need curl
    MANIFEST_TMP="${DOWNLOAD_DIR}/manifest.json"
    download "${MANIFEST_URL}" "${MANIFEST_TMP}"
fi

if [ -n "${MANIFEST_SHA}" ]; then
    echo "${MANIFEST_SHA}  ${MANIFEST_TMP}" | sha256sum -c -
fi

python3 - "${MANIFEST_TMP}" > "${DOWNLOAD_DIR}/packs.tsv" <<'PY'
import json
import sys

manifest_path = sys.argv[1]
with open(manifest_path, "r", encoding="utf-8") as f:
    manifest = json.load(f)

if manifest.get("format") != "runec-data-manifest-v1":
    raise SystemExit("setup-data: unsupported manifest format")

required = manifest.get("required_logical_paths")
if not isinstance(required, list) or not required:
    raise SystemExit("setup-data: manifest missing required_logical_paths")

assets = {
    entry.get("path")
    for entry in manifest.get("assets", [])
    if isinstance(entry, dict)
}
missing = [path for path in required if path not in assets]
if missing:
    raise SystemExit(
        "setup-data: manifest missing required assets: " + ", ".join(missing)
    )

for pack in manifest.get("packs", []):
    path = pack["path"]
    name = pack.get("name") or path.rsplit("/", 1)[-1]
    sha = pack["sha256"]
    size = pack.get("size", 0)
    print(f"{path}\t{name}\t{sha}\t{size}")
PY

while IFS=$'\t' read -r pack_path pack_name pack_sha pack_size; do
    [ -n "${pack_path}" ] || continue
    final_pack="${PACK_DIR}/${pack_name}"

    if [ "${VERIFY_ONLY}" = "1" ]; then
        if [ ! -f "${final_pack}" ]; then
            echo "setup-data: missing installed pack: ${final_pack}" >&2
            exit 1
        fi
        verify_file "${final_pack}" "${pack_sha}" "${pack_size}"
        continue
    fi

    if [ -f "${final_pack}" ] && echo "${pack_sha}  ${final_pack}" | sha256sum -c - >/dev/null 2>&1; then
        echo "setup-data: already verified ${pack_name}"
        continue
    fi

    if [ -n "${OFFLINE_DIR}" ]; then
        source_pack="$(local_pack_path "${pack_path}" "${pack_name}")" || {
            echo "setup-data: offline pack missing: ${pack_name}" >&2
            exit 1
        }
        verify_file "${source_pack}" "${pack_sha}" "${pack_size}"
        cp "${source_pack}" "${final_pack}"
    else
        tmp_pack="${DOWNLOAD_DIR}/${pack_name}"
        download "${BASE_URL}/${pack_name}" "${tmp_pack}"
        verify_file "${tmp_pack}" "${pack_sha}" "${pack_size}"
        mv "${tmp_pack}" "${final_pack}"
    fi
done < "${DOWNLOAD_DIR}/packs.tsv"

if [ "${VERIFY_ONLY}" = "1" ]; then
    echo "setup-data: verified manifest and packs in ${DATA_DIR}"
    exit 0
fi

if ! cmp -s "${MANIFEST_TMP}" "${DATA_DIR}/manifest.json"; then
    cp "${MANIFEST_TMP}" "${DATA_DIR}/manifest.json"
fi
echo "setup-data: installed manifest and packs into ${DATA_DIR}"

if [ "${UNPACK_DATA}" != "0" ]; then
    unpack_args=(
        "${ROOT}/tools/unpack_runtime_data.py"
        --data-dir "${DATA_DIR}"
        --manifest "${DATA_DIR}/manifest.json"
        --packs-dir "${PACK_DIR}"
    )
    if [ "${UNPACK_FORCE}" != "0" ]; then
        unpack_args+=(--force)
    fi
    echo "setup-data: expanding packs into loose runtime data"
    python3 "${unpack_args[@]}"
else
    echo "setup-data: skipping loose runtime data extraction because RUNEC_DATA_UNPACK=0"
fi
