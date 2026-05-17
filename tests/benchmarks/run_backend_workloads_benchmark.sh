#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${RC_BUILD_DIR:-$ROOT/build}"
JOBS="${RC_BUILD_JOBS:-2}"
OUT="$BUILD_DIR/runec_backend_workloads_benchmark"

if [[ ! -d "$BUILD_DIR" ]]; then
    cmake -S "$ROOT" -B "$BUILD_DIR"
fi

cmake --build "$BUILD_DIR" -j "$JOBS" --target rc-core rc-content

cc -O3 -DNDEBUG -std=c11 \
    -DRC_TEST_SOURCE_DIR="\"$ROOT\"" \
    -I"$ROOT/rc-core" \
    -I"$ROOT/rc-content" \
    "$ROOT/tests/benchmarks/backend_workloads_benchmark.c" \
    "$BUILD_DIR/librc-core.a" \
    "$BUILD_DIR/librc-content.a" \
    -lz -lm \
    -o "$OUT"

"$OUT" "$@"
