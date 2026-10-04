#!/bin/sh
# Host test of the BCM2711 graphics driver's stage marks (ws141-p002).
# Usage: plan/ws141/tests/stage-host-test.sh [build-dir]  (from the tree root)
set -eu
out=${1:-build/ws141-host}
mkdir -p "$out"
cc -std=c99 -D_POSIX_C_SOURCE=200809L -include time.h -Wall -Wextra -Werror -Iinclude -Isrc -I. \
	plan/ws141/tests/stage-host-test.c src/drivers/gpu/bcm2711/stage.c \
	-o "$out/stage-host-test"
"$out/stage-host-test"
