#!/bin/sh
# ws128-p006: the host test of Terminal's Edit > Find and the kept font size and theme (terminal-p006.c).
#   sh plan/ws128/tests/terminal-p006.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws128-p006}
mkdir -p "$out"
timeout 120 gcc -std=gnu17 -O2 -Wall -Wextra -Werror -Wno-format-truncation -D_GNU_SOURCE \
	-DKEILAND_DATADIR='"/opt/keiland/share"' -DKEILAND_BINDIR='"/opt/keiland/bin"' \
	-I. -Iuserland/desktop/include -Iuserland/desktop/libkeiland \
	plan/ws128/tests/terminal-p006.c userland/desktop/terminal/screen.c userland/desktop/terminal/width.c \
	userland/desktop/terminal/search.c userland/desktop/terminal/settings.c \
	userland/desktop/libkeiland/settings-cache.c userland/desktop/libkeiland/settings-app.c \
	userland/desktop/settings-keys/settings-keys.c plan/tools/settings/host-kl-settings.c \
	-o "$out/terminal-p006" || { echo "build: FAIL"; exit 1; }
home=$(mktemp -d)
HOME=$home XDG_CONFIG_HOME= timeout 60 "$out/terminal-p006"
status=$?
rm -rf "$home"
exit $status
