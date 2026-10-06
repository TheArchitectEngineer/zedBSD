#!/bin/sh
# ws165-p003: the handwriting face's recognition with the hand-hershey templates (host-hand-keyboard.c): あ, c
# large and small, や small (ゃ) and が.
#   sh plan/ws165/tests/run-host-hand-keyboard.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws165-host
mkdir -p "$out"
make -s ZEDBSD_CONFIG=config/ci/config-amd64.mk hand-hershey >/dev/null
cc -std=c11 -D_DEFAULT_SOURCE -O2 -g -Wall -Wextra -Werror -I. -Iuserland/desktop/wayland -DKEILAND_DATADIR='"/nonexistent"' \
	-o "$out/host-hand-keyboard" plan/ws165/tests/host-hand-keyboard.c userland/desktop/wayland/keyboard-hand.c \
	userland/desktop/wayland/hand-cloud.c -lm
exec timeout 120 "$out/host-hand-keyboard" build/packages/hand-hershey/hershey.txt
