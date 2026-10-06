#!/bin/sh
# ws102-p003: builds the flick layout (userland/desktop/wayland/keyboard-layout.c) with the host's C compiler and
# host-keyboard.c against it, and runs it.
#   sh plan/ws102/tests/host-keyboard.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=build/ws102-host
mkdir -p "$out"
${CC:-cc} -O2 -g -Wall -Wextra -Werror -I. -Iuserland/desktop/wayland -DKEILAND_DATADIR='"/nonexistent"' -o "$out/host-keyboard" \
    plan/ws102/tests/host-keyboard.c userland/desktop/wayland/keyboard-layout.c userland/desktop/wayland/keyboard-hand.c \
    userland/desktop/wayland/hand-cloud.c -lm || exit 1
"$out/host-keyboard"
