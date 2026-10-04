#!/bin/sh
# Builds and runs the host test of BUG-169 (the zedBSD backend's reading of networkd's state, included unchanged),
# with ASan and UBSan.
#   sh plan/ws033/tests/host-bug169.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws033-host-bug169}
mkdir -p "$out"
${CC:-cc} -std=gnu99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wno-unused-function -Wno-format-truncation -I. -Iinclude \
	-fsanitize=address,undefined -fno-sanitize-recover=all -ffunction-sections -fdata-sections plan/ws033/tests/host-bug169.c userland/base/net/protocol.c -Wl,--gc-sections -o "$out/host-bug169"
timeout 30 "$out/host-bug169"
