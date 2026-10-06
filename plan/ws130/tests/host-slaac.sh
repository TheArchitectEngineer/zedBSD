#!/bin/sh
# ws130-p006: builds and runs host-slaac.c with networkd's SLAAC (userland/base/networkd/slaac.c) and SHA-256
# (userland/base/common/sha256.c) and the host's headers, with ASan and UBSan.
#   sh plan/ws130/tests/host-slaac.sh [OUTDIR]     (default build/ws130-host)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws130-host}
mkdir -p "$out"
${CC:-cc} -std=gnu99 -O1 -g -Wall -Wextra -Werror -D_GNU_SOURCE -fsanitize=address,undefined -fno-sanitize-recover=all \
	-fno-omit-frame-pointer -I. plan/ws130/tests/host-slaac.c userland/base/networkd/slaac.c userland/base/common/sha256.c \
	-o "$out/host-slaac"
"$out/host-slaac"
