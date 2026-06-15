#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build_test}"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
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

if [[ ! -d "$OBJ_DIR" ]]; then
	echo "error: object directory not found at $OBJ_DIR" >&2
	exit 1
fi

clear_coverage() {
	find "$BUILD_DIR" -name '*.gcda' -delete
}

collect_covered_lines() {
	local obj gcov_file src line key

	(
		cd "$OBJ_DIR"
		for obj in "${OBJECTS[@]}"; do
			gcov -b "$obj" >/dev/null 2>&1 || true
		done
	)

	for obj in "${OBJECTS[@]}"; do
		gcov_file="$OBJ_DIR/$(basename "${obj%.o}").gcov"
		if [[ ! -f "$gcov_file" ]]; then
			continue
		fi

		src="$(grep -i 'source:' "$gcov_file" | head -1 | sed 's/.*Source://' | tr -d '[:space:]')"
		if [[ -z "$src" ]]; then
			src="$ROOT_DIR/source/$(basename "${obj%.o}")"
		fi

		while IFS= read -r line; do
			if [[ "$line" =~ ^[[:space:]]*([0-9]+):[[:space:]]*([0-9]+): ]]; then
				key="${src}:${BASH_REMATCH[2]}"
				echo "$key"
			fi
		done <"$gcov_file"
	done | sort -u
}

mapfile -t TESTS < <("$TEST_BIN" --list)
if [[ ${#TESTS[@]} -eq 0 ]]; then
	echo "error: no tests registered" >&2
	exit 1
fi

declare -A CUMULATIVE=()
FAILED=0

echo "Coverage gate: ${#TESTS[@]} tests"

for test_name in "${TESTS[@]}"; do
	clear_coverage
	if ! "$TEST_BIN" --run "$test_name" >/dev/null; then
		echo "FAIL: test crashed: $test_name" >&2
		exit 1
	fi

	mapfile -t LINES < <(collect_covered_lines)
	NEW=0

	for line in "${LINES[@]}"; do
		if [[ -z "${CUMULATIVE[$line]+x}" ]]; then
			CUMULATIVE["$line"]=1
			NEW=$((NEW + 1))
		fi
	done

	if [[ "$NEW" -eq 0 ]]; then
		echo "DROP: $test_name (adds 0 new covered lines)"
		FAILED=1
	else
		echo "KEEP: $test_name (+$NEW lines, cumulative ${#CUMULATIVE[@]})"
	fi
done

echo
echo "Summary: ${#TESTS[@]} tests, ${#CUMULATIVE[@]} unique lines covered"

if [[ "$FAILED" -ne 0 ]]; then
	echo "Coverage gate failed: remove or rewrite tests that add no new lines." >&2
	exit 1
fi

exit 0
