#!/bin/sh
# ws217-p004: builds and runs the host test of the displays' angles on the wire and in Settings
# (host-rotation-settings.c with libkeiland's system-view.c and system-protocol.c, against the host's libwayland-client,
# and settings/rotate.c), under ASan and UBSan, in a new directory under build/tmp (nothing is removed here; Q1's
# plan/tools/q1-clean.sh removes old runs).
#   sh plan/ws217/tests/host-rotation-settings.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/tmp/ws217-rotation-settings"
work=$fresh_dir
# Only keiland/keiland.h from the tree: the tree's wayland-client.h is zedBSD's, the host's is the one to link with.
mkdir -p "$work/include/keiland"
cp "$repo/userland/desktop/include/keiland/keiland.h" "$work/include/keiland/keiland.h"
${CC:-cc} -std=gnu99 -D_GNU_SOURCE -Wall -Wextra -Werror -O1 -g \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
	-I"$repo" -I"$work/include" \
	"$repo/plan/ws217/tests/host-rotation-settings.c" \
	"$repo/userland/desktop/libkeiland/system/system-view.c" \
	"$repo/userland/desktop/libkeiland/system/system-protocol.c" \
	"$repo/userland/desktop/settings/rotate.c" \
	$(pkg-config --cflags --libs wayland-client) -o "$work/host-rotation-settings"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 20 "$work/host-rotation-settings"
echo "WS217 p004 rotation wire and Settings host test PASS (ASan/UBSan)"
