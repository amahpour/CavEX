#!/usr/bin/env bash
set -euo pipefail

# Per-test coverage gate.
#
# Each registered test is run in isolation and must contribute at least one
# newly-executed source line over the cumulative set of all preceding tests.
# Tests that add nothing are reported as DROP and fail the gate.
#
# The expensive part (running each test and collecting its gcov line set) is
# embarrassingly parallel, so it is fanned out across CPUs. Isolation between
# concurrent runs is achieved with GCOV_PREFIX, which redirects each run's
# .gcda output into a private directory (GCOV_PREFIX_STRIP=99 flattens the
# baked-in object path down to a bare filename -- safe here because every
# measured object has a unique basename). The order-dependent cumulative
# bookkeeping is then done sequentially, so the KEEP/DROP decisions are
# identical to a fully serial run.

BUILD_DIR="${1:-build_test}"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TEST_BIN="$BUILD_DIR/tests/cavex_tests"
OBJ_DIR="$BUILD_DIR/tests/CMakeFiles/cavex_testlib.dir"
JOBS="${COVERAGE_JOBS:-$(nproc 2>/dev/null || echo 4)}"
OBJECTS=(
	"__/source/util.c.o"
	"__/source/stack.c.o"
	"__/source/block/aabb.c.o"
	"__/source/block/blocks_util.c.o"
	"__/source/block/block_candle.c.o"
	"__/source/block/block_bubble_column.c.o"
	"__/source/block/face_occlusion.c.o"
	"__/source/cNBT/buffer.c.o"
	"__/source/cNBT/nbt_parsing.c.o"
	"__/source/cNBT/nbt_loading.c.o"
	"__/source/cNBT/nbt_treeops.c.o"
	"__/source/cNBT/nbt_util.c.o"
	"__/source/daytime.c.o"
	"__/source/config.c.o"
	"__/source/parson/parson.c.o"
	"__/source/lighting.c.o"
	"__/source/item/recipe.c.o"
	"__/source/item/tool.c.o"
	"__/source/item/inventory.c.o"
	"__/source/item/window_container.c.o"
	"__/source/network/inventory_logic.c.o"
	"__/source/network/level_archive.c.o"
	"__/source/network/region_archive.c.o"
	"__/source/chunk_storage.c.o"
	"__/source/entity/entity_id.c.o"
	"__/source/entity/entity_local_player.c.o"
)

if [[ ! -x "$TEST_BIN" ]]; then
	echo "error: test binary not found at $TEST_BIN" >&2
	exit 1
fi

if [[ ! -d "$OBJ_DIR" ]]; then
	echo "error: object directory not found at $OBJ_DIR" >&2
	exit 1
fi

# Absolute so that symlinks created under /tmp resolve correctly.
OBJ_DIR="$(cd "$OBJ_DIR" && pwd)"

# basenames of the .gcno files we care about, e.g. "util.c.gcno"
GCNO_NAMES=()
for obj in "${OBJECTS[@]}"; do
	GCNO_NAMES+=("$(basename "${obj%.o}").gcno")
done

mapfile -t TESTS < <("$TEST_BIN" --list)
if [[ ${#TESTS[@]} -eq 0 ]]; then
	echo "error: no tests registered" >&2
	exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Shared template of gcno symlinks (flattened); workers symlink from here.
TEMPLATE="$WORK/gcno"
mkdir -p "$TEMPLATE"
find "$OBJ_DIR" -name '*.gcno' -exec ln -sf {} "$TEMPLATE/" \;

LINES_DIR="$WORK/lines"
mkdir -p "$LINES_DIR"

TEST_BIN_ABS="$(cd "$(dirname "$TEST_BIN")" && pwd)/$(basename "$TEST_BIN")"

# Collect the executed-line set for a single test, in its own gcda sandbox.
collect_one() {
	local test_name="$1"
	local d
	d="$(mktemp -d "$WORK/run.XXXXXX")"

	# private, flat working dir: gcno symlinks + (about to be written) gcda
	ln -sf "$TEMPLATE"/*.gcno "$d/"

	if ! GCOV_PREFIX="$d" GCOV_PREFIX_STRIP=99 \
		"$TEST_BIN_ABS" --run "$test_name" >/dev/null 2>&1; then
		echo "CRASH" >"$LINES_DIR/$test_name.crash"
		rm -rf "$d"
		return 0
	fi

	(
		cd "$d"
		for g in "${GCNO_NAMES[@]}"; do
			gcov "$g" >/dev/null 2>&1 || true
		done
		for g in "${GCNO_NAMES[@]}"; do
			gf="${g%.gcno}.gcov"
			[[ -f "$gf" ]] || continue
			src="$(grep -m1 'Source:' "$gf" | sed 's/.*Source://' | tr -d '[:space:]')"
			[[ -n "$src" ]] || src="$g"
			awk -F: -v s="$src" '
				{ c = $1; gsub(/ /, "", c);
				  if (c ~ /^[0-9]+$/) { l = $2; gsub(/ /, "", l); print s ":" l } }
			' "$gf"
		done | sort -u >"$LINES_DIR/$test_name.lines"
	)

	rm -rf "$d"
}
export -f collect_one
export WORK TEMPLATE LINES_DIR TEST_BIN_ABS
export GCNO_NAMES_STR="${GCNO_NAMES[*]}"

# Re-hydrate the array inside the subshells xargs spawns.
run_worker() {
	# shellcheck disable=SC2206
	GCNO_NAMES=($GCNO_NAMES_STR)
	collect_one "$1"
}
export -f run_worker

echo "Coverage gate: ${#TESTS[@]} tests (parallel x$JOBS)"

printf '%s\n' "${TESTS[@]}" \
	| xargs -P "$JOBS" -I {} bash -c 'run_worker "$@"' _ {}

# Sequential, order-dependent cumulative bookkeeping (fast, pure bash).
declare -A CUMULATIVE=()
FAILED=0

for test_name in "${TESTS[@]}"; do
	if [[ -f "$LINES_DIR/$test_name.crash" ]]; then
		echo "FAIL: test crashed: $test_name" >&2
		exit 1
	fi

	if [[ ! -f "$LINES_DIR/$test_name.lines" ]]; then
		echo "FAIL: no coverage collected for $test_name" >&2
		exit 1
	fi

	NEW=0
	while IFS= read -r line; do
		if [[ -z "${CUMULATIVE[$line]+x}" ]]; then
			CUMULATIVE["$line"]=1
			NEW=$((NEW + 1))
		fi
	done <"$LINES_DIR/$test_name.lines"

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
