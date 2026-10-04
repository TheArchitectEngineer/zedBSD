#!/bin/sh
# ws089-p022: builds and runs the host test of networkd's wired configuration (host-lan-configure.c with
# userland/base/networkd/lan-configure.c and userland/base/net/netconf.c).  Last line: host-lan-configure: PASS.
#   sh plan/ws089/tests/run-host-lan-configure.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -std=gnu99 -O1 -g -Wall -Wextra -Werror -D_GNU_SOURCE -fsanitize=address,undefined -fno-omit-frame-pointer -I. \
    plan/ws089/tests/host-lan-configure.c userland/base/networkd/lan-configure.c userland/base/net/netconf.c -o "$work/host-lan-configure"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$work/host-lan-configure"
