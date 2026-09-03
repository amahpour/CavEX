#!/usr/bin/env bash
# Turn the "Invalid read/write ... PC = 0x8xxxxxxx" lines Dolphin logs into
# function + source line, using the CavEX.elf from the same build.
#
# addr2line cannot do this here: the Wii build is -flto with DWARF 5, and
# binutils addr2line returns ??:? for the LTO clones (server_world_set_block.isra.0
# etc.). gdb and objdump read the same tables fine, so this uses gdb.
#
#   scripts/wii_symbolize.sh [dolphin-run.log] [CavEX.elf]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOG="${1:-$ROOT/dolphin-run.log}"
ELF="${2:-$ROOT/CavEX.elf}"
export PATH="/opt/devkitpro/devkitPPC/bin:$PATH"

[ -f "$LOG" ] || { echo "no log: $LOG" >&2; exit 1; }
[ -f "$ELF" ] || { echo "no elf: $ELF (build with make; -g is on)" >&2; exit 1; }

echo "log: $LOG"
echo "elf: $ELF ($(stat -c %y "$ELF" | cut -c1-19))"
echo
printf '%8s  %-10s  %s\n' "count" "PC" "function / line"
grep -oE 'PC = 0x[0-9a-f]+' "$LOG" | sort | uniq -c | sort -rn | while read -r n _ _ addr; do
	line="$(powerpc-eabi-gdb -batch -ex "info line *$addr" "$ELF" 2>/dev/null | tail -1 \
		| sed -E "s|$ROOT/||; s/ and ends at.*//; s/starts at address //")"
	printf '%8s  %-10s  %s\n' "$n" "$addr" "${line:-?}"
done
echo
echo "first fault:"; grep -m1 -E 'Invalid (read|write)' "$LOG" | cut -c1-140
echo "last fault: "; grep -E 'Invalid (read|write)' "$LOG" | tail -1 | cut -c1-140
