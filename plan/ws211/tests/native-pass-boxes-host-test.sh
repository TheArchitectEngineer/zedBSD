#!/bin/sh
# Host test of a native pass narrowed to tile boxes (ws211-p009, src/drivers/gpu/bcm2711/native-pass.c).
# Usage: plan/ws211/tests/native-pass-boxes-host-test.sh [build-dir]  (from the tree root)
set -eu
out=${1:-build/ws211-host}
mkdir -p "$out"
cc -std=c99 -Wall -Wextra -Werror -Iinclude -Isrc -I. \
	plan/ws211/tests/native-pass-boxes-host-test.c src/drivers/gpu/bcm2711/native-pass.c \
	-o "$out/native-pass-boxes-host-test"
"$out/native-pass-boxes-host-test"
