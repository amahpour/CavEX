#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build_test}"
TEST_BIN="$BUILD_DIR/tests/cavex_tests"
OBJ_DIR="$BUILD_DIR/tests/CMakeFiles/cavex_testlib.dir"
OBJECTS=(
	"__/source/util.c.o"
	"__/source/stack.c.o"
	"__/source/block/aabb.c.o"
	"__/source/block/face_occlusion.c.o"
	"__/source/cNBT/buffer.c.o"
)

if [[ ! -x "$TEST_BIN" ]]; then
	echo "error: test binary not found at $TEST_BIN" >&2
	exit 1
fi

find "$BUILD_DIR" -name '*.gcda' -delete
"$TEST_BIN" >/dev/null

printf "%-24s %10s %8s\n" "File" "Lines" "Percent"
printf "%-24s %10s %8s\n" "----" "-----" "-------"

(
	cd "$OBJ_DIR"
	for obj in "${OBJECTS[@]}"; do
		label="$(basename "${obj%.o}")"
		summary="$(gcov -b -n "$obj" 2>&1 | grep -E '^Lines executed:' | tail -1 || true)"

		if [[ "$summary" =~ Lines\ executed:([0-9.]+)%\ of\ ([0-9]+) ]]; then
			percent="${BASH_REMATCH[1]}%"
			total="${BASH_REMATCH[2]}"
			hit="$(awk "BEGIN {printf \"%.0f\", ${BASH_REMATCH[1]} * ${BASH_REMATCH[2]} / 100}")"
			printf "%-24s %4s/%-4s %7s\n" "$label" "$hit" "$total" "$percent"
		else
			printf "%-24s %10s %8s\n" "$label" "n/a" "n/a"
		fi
	done
)
