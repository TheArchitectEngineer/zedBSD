#!/bin/sh
# ws135-p003: builds and runs the host tests of libkeiland's settings cache and application files
# (libkeiland/settings-cache.c, settings-app.c) with the settings' table, on Linux, under ASan and UBSan.
#   sh plan/ws135/tests/host-settings.sh [OUTPUT]   (default build/ws135/host-settings)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
out=${1:-build/ws135/host-settings}
mkdir -p "$(dirname "$out")"
${CC:-clang} -std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wdeclaration-after-statement \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -I. -Iuserland/desktop/keiland \
	-Iuserland/desktop/libkeiland plan/ws135/tests/host-settings.c userland/desktop/libkeiland/settings-cache.c \
	userland/desktop/libkeiland/settings-app.c userland/desktop/settings-keys/settings-keys.c -o "$out"
"$out"
