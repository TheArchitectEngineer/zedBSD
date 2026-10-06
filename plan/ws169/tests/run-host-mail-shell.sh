#!/bin/sh
# ws169-p002: builds and runs the host test of the arrivals of mail (host-mail-shell.c): the compositor's
# userland/desktop/wayland/mail-shell.c with a capture client and libkeiland's ring of arrivals
# (userland/desktop/libkeiland/system/system-view.c), under ASan and UBSan.
#   sh plan/ws169/tests/run-host-mail-shell.sh [OUTPUT]   (default build/ws169/host-mail-shell)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws169/host-mail-shell}
mkdir -p "$(dirname -- "$out")/include/keiland"
cp userland/desktop/include/keiland/keiland.h "$(dirname -- "$out")/include/keiland/keiland.h"
${CC:-cc} -std=gnu99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
	-I. -I"$(dirname -- "$out")/include" \
	plan/ws169/tests/host-mail-shell.c userland/desktop/wayland/mail-shell.c \
	userland/desktop/libkeiland/system/system-view.c -o "$out"
timeout 60 "$out"
