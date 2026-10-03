#!/bin/sh
# ws135-p002: builds and runs the host tests of the compositor's settings store (wayland/settings-store.c) and the
# settings' table (settings-keys/settings-keys.c), on Linux, under ASan and UBSan.
#   sh plan/tools/settings/host-store.sh [OUTPUT]   (default build/ws135/host-store)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
out=${1:-build/ws135/host-store}
mkdir -p "$(dirname "$out")"
${CC:-clang} -std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Wdeclaration-after-statement \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -I. -Iuserland/desktop/wayland \
	plan/tools/settings/host-store.c userland/desktop/wayland/settings-store.c userland/desktop/settings-keys/settings-keys.c \
	-pthread -o "$out"
"$out"
