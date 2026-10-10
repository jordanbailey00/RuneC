#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${RC_BUILD_DIR:-$ROOT/build}"
JOBS="${RC_BUILD_JOBS:-2}"
OUT="$BUILD_DIR/runec_sps_benchmark"

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=RelWithDebInfo
fi
# An unset or Debug build type compiles rc-core at -O0 with assertions on,
# which understates SPS 2-3x and makes before/after runs incomparable.
BUILD_TYPE="$(sed -n 's/^CMAKE_BUILD_TYPE:[A-Z]*=//p' "$BUILD_DIR/CMakeCache.txt")"
case "$BUILD_TYPE" in
    Release|RelWithDebInfo) ;;
    *)
        printf 'Benchmark requires a Release or RelWithDebInfo build; %s is "%s".\n' \
            "$BUILD_DIR" "${BUILD_TYPE:-unset}" >&2
        exit 2
        ;;
esac

cmake --build "$BUILD_DIR" -j "$JOBS" --target rc-core rc-content

cc -O3 -DNDEBUG -std=c11 \
    -I"$ROOT/rc-core" \
    -I"$ROOT/rc-content" \
    "$ROOT/tests/benchmarks/sps_benchmark.c" \
    "$BUILD_DIR/librc-core.a" \
    "$BUILD_DIR/librc-content.a" \
    -lm -lz \
    -o "$OUT"

printf 'rc-core build type: %s\n' "$BUILD_TYPE"
"$OUT" "$@"
