#!/bin/sh
# ws089-p017: builds and runs the host test of the light and the dark appearance's text contrast (host-dark.c), with
# libkeiland's themes, Settings' palette and Files' palette compiled in.  Last line: HOST-DARK PASS.
#   sh plan/ws089/tests/run-host-dark.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
# The work directory stays in build/tmp (2026-10-06 user: deleting is Q1's step; plan/tools/q1-clean.sh removes it).
mkdir -p build/tmp
work=$(mktemp -d "$(pwd)/build/tmp/run-host-dark.XXXXXX")
cc -std=gnu89 -O1 -g -Wall -Wextra -Werror -D_GNU_SOURCE -I. -Iuserland/desktop/keiland \
    plan/ws089/tests/host-dark.c userland/desktop/libkeiland/ui/theme.c userland/desktop/settings/palette.c \
    userland/desktop/files/palette.c -lm -o "$work/host-dark"
timeout 30 "$work/host-dark"
