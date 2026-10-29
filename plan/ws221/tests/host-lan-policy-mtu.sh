#!/bin/sh
# Builds and runs host-lan-policy-mtu.c with the LAN_ENABLE record codec (userland/base/net/protocol.c), the
# Settings edit of net.conf (userland/base/networkd/lan-configure.c, netconf.c) and the wired policy
# (userland/base/networkd/managed-lan.c) on the host, with ASan and UBSan.
#   sh plan/ws221/tests/host-lan-policy-mtu.sh [OUTDIR]     (default build/ws221-host)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws221-host}
mkdir -p "$out"
${CC:-cc} -std=gnu99 -O1 -g -Wall -Wextra -Werror -D_GNU_SOURCE -fsanitize=address,undefined -fno-sanitize-recover=all \
	-fno-omit-frame-pointer -Iplan/ws033/tests/include -Iinclude -I. plan/ws221/tests/host-lan-policy-mtu.c \
	userland/base/net/protocol.c userland/base/net/netconf.c userland/base/networkd/lan-configure.c \
	userland/base/networkd/managed-lan.c -o "$out/host-lan-policy-mtu"
"$out/host-lan-policy-mtu"
