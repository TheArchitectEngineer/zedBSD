#!/bin/sh
# ws254: the DHCP retry of networkd's wired policy, on the host (no guest).
#   sh plan/ws254/tests/dhcp-retry-host-test.sh [OUTDIR]   (default build/ws254/host-tests)
# Builds userland/base/networkd/managed-lan.c with plan/ws254/tests/dhcp-retry-host-test.c and runs it.
# The <net/if.h> and <net/route.h> stand-ins are WS033's (plan/ws033/tests/include), read only.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws254/host-tests}
mkdir -p "$out"
${HOSTCC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -Iplan/ws033/tests/include -Iinclude -I. \
	userland/base/networkd/managed-lan.c plan/ws254/tests/dhcp-retry-host-test.c \
	-o "$out/dhcp-retry"
exec "$out/dhcp-retry"
