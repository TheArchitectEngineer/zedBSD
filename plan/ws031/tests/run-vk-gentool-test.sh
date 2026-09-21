#!/bin/sh
# WS031 E-128: the EU encoder and the compiler's kernels against Mesa's assembler / disassembler.
# Needs gentool (plan/ws031/mesa-refs/mesa/build-gentool).  usage: run-vk-gentool-test.sh [-v]
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
gentool=${GENTOOL:-$repo/plan/ws031/mesa-refs/mesa/build-gentool/src/intel/compiler/gen/gentool}
work=$(mktemp -d "${TMPDIR:-/tmp}/ws031-gentool.XXXXXX")
trap "rm -rf -- \"$work\"" EXIT HUP INT TERM
base="-std=gnu11 -Wall -Wextra -Werror -DKERN_USER_ABI_LP64 -I$repo/include -I$repo -idirafter $repo/libc/include"

judge() {	# name binary
	"$gentool" disasm -p adl -v "$2" > "$work/$1.asm" 2>&1
	[ "${VERBOSE:-0}" = 1 ] && cat "$work/$1.asm"
	if grep -q 'ERROR\|illegal' "$work/$1.asm"; then
		echo "$1: gentool rejects an instruction:"; grep -B1 'ERROR\|illegal' "$work/$1.asm"; exit 1
	fi
	# Assembling the disassembly must give a program that reads the same.  (Not the same bytes: gentool's
	# assembler spells a null operand and sync.nop differently from Mesa's compiler, whose spelling --
	# the one in the reference kernels -- is the one eu.c emits.)
	"$gentool" asm -p adl -o "$work/$1.re" "$work/$1.asm"
	"$gentool" disasm -p adl -v "$work/$1.re" > "$work/$1.re.asm" 2>&1
	cmp -s "$work/$1.asm" "$work/$1.re.asm" || { echo "$1: the re-assembled program reads differently"; diff "$work/$1.asm" "$work/$1.re.asm" | head; exit 1; }
	echo "$1: $(($(wc -c < "$2") / 16)) instructions accepted; re-assembled program reads the same"
}

VERBOSE=0; [ "${1:-}" = -v ] && VERBOSE=1
cc $base -O1 "$repo/plan/ws031/tests/i915-vk-gentool-eu.c" -o "$work/eu"
"$work/eu" "$work/eu.bin"
judge encoder "$work/eu.bin"

cc $base -O1 "$repo/plan/ws031/tests/i915-vk-eudump.c" -o "$work/eudump" -lm
"$work/eudump" vertex "$repo/userland/base/vkdemo/shaders/cuboid.vert.spv" "$work/vs.bin"
judge vkdemo-vertex "$work/vs.bin"
"$work/eudump" fragment "$repo/userland/base/vkdemo/shaders/cuboid.frag.spv" "$work/fs.bin"
judge vkdemo-fragment "$work/fs.bin"
echo "WS031 gentool test PASS"
