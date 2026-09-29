#!/bin/sh
# ws089-p007: builds and runs the host test of the desktop's preferences (host-preferences.c) in a home of its own,
# build/ws089-host/prefs-home (never the real home).
#
#   plan/ws089/tests/host-preferences.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=$(pwd)/build/ws089-host
mkdir -p "$out/include"
ln -sf "$(pwd)/include/libc/keiland.h" "$out/include/keiland.h"
${CC:-cc} -std=gnu89 -Wall -Wextra -Werror -D_GNU_SOURCE -I"$out/include" \
    userland/desktop/libkeiland/preferences.c plan/ws089/tests/host-preferences.c -o "$out/host-preferences"
rm -rf "$out/prefs-home"
mkdir -p "$out/prefs-home"
exec "$out/host-preferences" "$out/prefs-home"
