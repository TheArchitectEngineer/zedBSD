#!/bin/sh
# BUG-220 (q803): builds and runs the host test of Files' column-width keys (settings-keys.c) with libkeiland's
# settings cache and application file (settings-cache.c, settings-app.c), on Linux, under ASan and UBSan.
#   sh plan/ws127/tests/host-column-widths.sh [FOLDER]   (default build/ws127/host-column-widths; a home is made in it)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
out=${1:-build/ws127/host-column-widths}
mkdir -p "$out"
${CC:-clang} -std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wdeclaration-after-statement \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -I. -Iuserland/desktop/include \
	-Iuserland/desktop/libkeiland plan/ws127/tests/host-column-widths.c userland/desktop/libkeiland/settings-cache.c \
	userland/desktop/libkeiland/settings-app.c userland/desktop/settings-keys/settings-keys.c -o "$out/host-column-widths"
"$out/host-column-widths" "$out"
