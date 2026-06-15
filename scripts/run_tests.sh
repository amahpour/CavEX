#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_TEST:-$ROOT_DIR/build_test}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

echo "==> Configure test build"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
	-DCMAKE_BUILD_TYPE=Debug \
	-DCAVEX_TESTS_ONLY=ON \
	-DCAVEX_COVERAGE=ON

echo
echo "==> Build"
cmake --build "$BUILD_DIR" -j"$JOBS"

echo
echo "==> Run tests"
ctest --test-dir "$BUILD_DIR" --output-on-failure

echo
echo "==> Per-test coverage gate"
bash "$ROOT_DIR/scripts/check_test_coverage.sh" "$BUILD_DIR"

echo
echo "==> Coverage summary"
bash "$ROOT_DIR/scripts/print_coverage_summary.sh" "$BUILD_DIR"

echo
echo "All tests passed with coverage gate."
