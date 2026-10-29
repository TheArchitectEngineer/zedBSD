#!/bin/sh
# WS307 p010: the installer's model on the host (catalog.c and disk.c with a build's index), with ASan and UBSan.
#   sh plan/ws307/tests/host/installer-model-test.sh [INDEX]   (default build/ws307l-img/installer/payload/index)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname "$0")/../../../.."
index=${1:-build/ws307l-img/installer/payload/index}
mkdir -p build/tmp
out=$(mktemp -d build/tmp/installer-model.XXXXXX)
cc -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-sanitize-recover=all \
	-I. -Iinclude -Iuserland/desktop/include -DINST_INDEX="\"$index\"" \
	-o "$out/test" plan/ws307/tests/host/installer-model-test.c \
	userland/desktop/installer/catalog.c userland/desktop/installer/disk.c
"$out/test"
