#!/bin/sh
# ws089-p022: builds and runs the host test of the Ethernet page's editor (host-wired.c with
# userland/desktop/settings/wired.c).  Last line: host-wired: PASS.
#   sh plan/ws089/tests/run-host-wired.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
# The work directory stays in build/tmp (2026-10-06 user: deleting is Q1's step; plan/tools/q1-clean.sh removes it).
mkdir -p build/tmp
work=$(mktemp -d "$(pwd)/build/tmp/run-host-wired.XXXXXX")
mkdir -p "$work/include"
for header in truetype.h keiland.h; do
	ln -sf "$(pwd)/userland/desktop/keiland/$header" "$work/include/$header"
done
cc -std=gnu89 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -D_GNU_SOURCE -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$work/include" -Iuserland/desktop/settings -I. \
    plan/ws089/tests/host-wired.c userland/desktop/settings/wired.c -o "$work/host-wired"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$work/host-wired"
