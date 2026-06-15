#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_PC:-$ROOT_DIR/build_pc}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

echo "==> Configure PC build"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug

echo
echo "==> Build"
cmake --build "$BUILD_DIR" -j"$JOBS"

echo
echo "Built $BUILD_DIR/cavex"
