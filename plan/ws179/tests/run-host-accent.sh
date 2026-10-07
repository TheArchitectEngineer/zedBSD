#!/bin/sh
# ws179-p001: builds and runs the host test of the accent colours (host-accent.c) with libkeiland's theme, Settings'
# palette and Files' palette compiled in.  Last line: HOST-ACCENT PASS.
#   sh plan/ws179/tests/run-host-accent.sh [OUTPUT]   (default build/ws179/host-accent)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws179/host-accent}
mkdir -p "$(dirname "$out")"
cc -std=gnu89 -O1 -g -Wall -Wextra -Werror -D_GNU_SOURCE -I. -Iuserland/desktop/include \
    plan/ws179/tests/host-accent.c userland/desktop/libkeiland/ui/theme.c userland/desktop/settings/palette.c \
    userland/desktop/files/palette.c -lm -o "$out"
timeout 30 "$out"
