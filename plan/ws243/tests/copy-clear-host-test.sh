#!/bin/sh
# Host test of the copy engine's clear of the VRAM (nvrtx_push_copy_clear: the rows and the launch).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws243/tests/copy-clear-host-test.sh   (from anywhere)
# The binary goes in a new directory below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/ws243-clear.XXXXXX")
src=$repo/src/drivers/gpu/nvrtx
cc -std=gnu11 -O1 -g -DKERN_USER_ABI_LP64 -Wall -Wextra -Werror -Wdeclaration-after-statement \
	-fsanitize=address,undefined -fno-sanitize=alignment -ffunction-sections -fdata-sections -Wl,--gc-sections \
	-I"$repo/include" -I"$repo/src" -I"$repo" \
	-o "$out/copy-clear-host-test" \
	"$repo/plan/ws243/tests/copy-clear-host-test.c" \
	"$src/objects.c" "$src/wire.c"
"$out/copy-clear-host-test"
