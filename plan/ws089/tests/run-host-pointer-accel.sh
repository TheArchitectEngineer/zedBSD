#!/bin/sh
# ws089-p024: builds and runs the host test of a mouse's pointer acceleration (host-pointer-accel.c with
# userland/desktop/wayland/pointer-accel.c).  Last line: host-pointer-accel: PASS.
#   sh plan/ws089/tests/run-host-pointer-accel.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -std=gnu89 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I. \
    plan/ws089/tests/host-pointer-accel.c userland/desktop/wayland/pointer-accel.c -o "$work/host-pointer-accel"
UBSAN_OPTIONS=halt_on_error=1 "$work/host-pointer-accel"
