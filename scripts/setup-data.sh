#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${RUNEC_DATA_VERSION:-v1}"
BASE_URL="${RUNEC_DATA_BASE_URL:-https://github.com/jordanbailey00/RuneC/releases/download/data-${VERSION}}"
MANIFEST_URL="${RUNEC_DATA_MANIFEST_URL:-${BASE_URL}/manifest.json}"
DATA_DIR="${RUNEC_DATA_DIR:-${ROOT}/data}"
DOWNLOAD_DIR="${DATA_DIR}/.download"
PACK_DIR="${DATA_DIR}/packs"
MANIFEST_TMP="${DOWNLOAD_DIR}/manifest.json"

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

need curl
need python3
need sha256sum

mkdir -p "${DOWNLOAD_DIR}" "${PACK_DIR}"

download "${MANIFEST_URL}" "${MANIFEST_TMP}"

python3 - "${MANIFEST_TMP}" > "${DOWNLOAD_DIR}/packs.tsv" <<'PY'
import json
import sys

manifest_path = sys.argv[1]
with open(manifest_path, "r", encoding="utf-8") as f:
    manifest = json.load(f)

if manifest.get("format") != "runec-data-manifest-v1":
    raise SystemExit("setup-data: unsupported manifest format")

for pack in manifest.get("packs", []):
    path = pack["path"]
    name = pack.get("name") or path.rsplit("/", 1)[-1]
    sha = pack["sha256"]
    size = pack.get("size", 0)
    print(f"{path}\t{name}\t{sha}\t{size}")
PY

while IFS=$'\t' read -r pack_path pack_name pack_sha pack_size; do
    [ -n "${pack_path}" ] || continue
    pack_url="${BASE_URL}/${pack_name}"
    tmp_pack="${DOWNLOAD_DIR}/${pack_name}"
    final_pack="${PACK_DIR}/${pack_name}"

    if [ -f "${final_pack}" ] && echo "${pack_sha}  ${final_pack}" | sha256sum -c - >/dev/null 2>&1; then
        echo "setup-data: already verified ${pack_name}"
        continue
    fi

    download "${pack_url}" "${tmp_pack}"
    echo "${pack_sha}  ${tmp_pack}" | sha256sum -c -
    if [ "${pack_size}" != "0" ]; then
        actual_size="$(wc -c < "${tmp_pack}")"
        if [ "${actual_size}" != "${pack_size}" ]; then
            echo "setup-data: size mismatch for ${pack_name}: got ${actual_size}, expected ${pack_size}" >&2
            exit 1
        fi
    fi
    mv "${tmp_pack}" "${final_pack}"
done < "${DOWNLOAD_DIR}/packs.tsv"

cp "${MANIFEST_TMP}" "${DATA_DIR}/manifest.json"
echo "setup-data: installed manifest and packs into ${DATA_DIR}"
