#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build_test}"
TEST_BIN="$BUILD_DIR/tests/cavex_tests"
OBJ_DIR="$BUILD_DIR/tests/CMakeFiles/cavex_testlib.dir"
OBJECTS=(
	"__/source/util.c.o"
	"__/source/stack.c.o"
	"__/source/block/aabb.c.o"
	"__/source/block/blocks_util.c.o"
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
