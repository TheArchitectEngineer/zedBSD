#!/bin/sh
# ws035-p078: host test of zdesktop's XKB keymap with the host's libxkbcommon (what toolkits use):
# userland/desktop/wayland/keymap.c compiled for the host with plan/ws035/tests/p078/keymap-host.c.
#
#   plan/ws035/tests/p078/run-host.sh [OUTDIR]     (default build/ws035-p078-host)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../../.."
out=${1:-build/ws035-p078-host}
mkdir -p "$out"
cc -std=c99 -Wall -Wextra -Werror -I. -D_POSIX_C_SOURCE=200809L -o "$out/keymap-host" \
    plan/ws035/tests/p078/keymap-host.c userland/desktop/wayland/keymap.c -lxkbcommon
if "$out/keymap-host" > "$out/keymap-host.log" 2>&1; then
	cat "$out/keymap-host.log"
	echo "p078-host: PASS"
else
	cat "$out/keymap-host.log"
	echo "p078-host: FAIL"
	exit 1
fi
