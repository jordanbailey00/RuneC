#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${RC_BUILD_DIR:-$ROOT/build}"
JOBS="${RC_BUILD_JOBS:-2}"
OUT="$BUILD_DIR/runec_sps_benchmark"

if [[ ! -d "$BUILD_DIR" ]]; then
    cmake -S "$ROOT" -B "$BUILD_DIR"
fi

cmake --build "$BUILD_DIR" -j "$JOBS" --target rc-core rc-content

cc -O3 -DNDEBUG -std=c11 \
    -I"$ROOT/rc-core" \
    -I"$ROOT/rc-content" \
    "$ROOT/tests/benchmarks/sps_benchmark.c" \
    "$BUILD_DIR/librc-core.a" \
    "$BUILD_DIR/librc-content.a" \
    -lm \
    -o "$OUT"

"$OUT" "$@"
