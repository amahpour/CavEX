#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_PC:-$ROOT_DIR/build_pc}"
RUN_DIR="$BUILD_DIR/run"

# Build the binary first (build_pc.sh honours BUILD_PC / JOBS too).
bash "$ROOT_DIR/scripts/build_pc.sh"

# The binary loads config.json and its texture pack (which holds the shaders)
# relative to the current directory, so stage a run dir and launch from inside
# it. Each step is idempotent, so re-running just picks up changes.
echo
echo "==> Stage run directory ($RUN_DIR)"
mkdir -p "$RUN_DIR"
ln -sfn "$ROOT_DIR/assets" "$RUN_DIR/assets"                 # textures + vertex/fragment shaders
cp "$ROOT_DIR/config_pc.json" "$RUN_DIR/config.json"        # always refresh so keybind changes (e.g. creative_page) are picked up
[ -d "$RUN_DIR/saves/world" ] || python3 "$ROOT_DIR/gen_world.py" "$RUN_DIR/saves"

echo
echo "==> Launch"
cd "$RUN_DIR"
exec "$BUILD_DIR/cavex" "$@"
