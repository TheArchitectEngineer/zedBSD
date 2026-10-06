#!/bin/sh
# Builds and runs the host test of what the lid does (ws132-p008): lid.c and backend-host.c (compiled unchanged; the
# parts of backend-host.c the test does not reach are dropped by the linker), with ASan and UBSan.
#   sh plan/ws132/tests/run-host-lid.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws132-host-lid}
mkdir -p "$out/include"
cp userland/desktop/keiland/keiland.h "$out/include/keiland.h"
${CC:-cc} -std=gnu99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -I. -I"$out/include" \
	plan/ws132/tests/host-lid.c userland/desktop/wayland/lid.c userland/desktop/wayland/backend-host.c \
	-Wl,--gc-sections -o "$out/host-lid"
timeout 60 "$out/host-lid"
