#!/bin/sh
#
# Builds and runs console-ascii-test.c against one bootx64.c.
#
# usage: console-ascii-run.sh BOOTX64_C OUT_BINARY

set -eu

source=$1
out=$2
top=$(cd "$(dirname "$0")/../../.." && pwd)

clang -std=c11 -fsanitize=address -g -w \
	-ffunction-sections -fdata-sections -Wl,--gc-sections \
	-I"$top/bootloader/uefi" -I"$top" \
	-DBOOTX64_SOURCE="\"$source\"" \
	"$top/plan/ws262/tests/console-ascii-test.c" -o "$out"
ASAN_OPTIONS=handle_segv=0 "$out"
