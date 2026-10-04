#!/bin/sh
# Builds and runs the host test of a USB storage detach's decision (BUG-192), with ASan and UBSan.
#   sh plan/ws132/tests/run-host-storage-removal.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws132-host-storage-removal}
mkdir -p "$out"
${CC:-cc} -std=gnu11 -O1 -g -Wall -Wextra -Werror -DKERN_UAPI_NATIVE -Iinclude -Iinclude/uapi \
	-fsanitize=address,undefined -fno-sanitize-recover=all plan/ws132/tests/host-storage-removal.c -o "$out/host-storage-removal"
timeout 30 "$out/host-storage-removal"
