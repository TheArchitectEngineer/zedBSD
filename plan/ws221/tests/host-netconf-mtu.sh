#!/bin/sh
# Builds and runs host-netconf-mtu.c with net.conf's reader, writer and reconcile program
# (userland/base/net/netconf.c, reconcile.c) and the host's headers, with ASan and UBSan.
#   sh plan/ws221/tests/host-netconf-mtu.sh [OUTDIR]     (default build/ws221-host)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws221-host}
mkdir -p "$out"
${CC:-cc} -std=gnu99 -O1 -g -Wall -Wextra -Werror -D_GNU_SOURCE -fsanitize=address,undefined -fno-sanitize-recover=all \
	-fno-omit-frame-pointer -I. plan/ws221/tests/host-netconf-mtu.c userland/base/net/netconf.c userland/base/net/reconcile.c \
	-o "$out/host-netconf-mtu"
"$out/host-netconf-mtu"
