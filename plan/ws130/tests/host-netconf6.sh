#!/bin/sh
# ws130-p005: builds and runs host-netconf6.c with net.conf's reader, writer and reconcile program
# (userland/base/net/netconf.c, reconcile.c) and the host's headers, with ASan and UBSan.
#   sh plan/ws130/tests/host-netconf6.sh [OUTDIR]     (default build/ws130-host)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws130-host}
mkdir -p "$out"
${CC:-cc} -std=gnu99 -O1 -g -Wall -Wextra -Werror -D_GNU_SOURCE -fsanitize=address,undefined -fno-sanitize-recover=all \
	-fno-omit-frame-pointer -I. plan/ws130/tests/host-netconf6.c userland/base/net/netconf.c userland/base/net/reconcile.c -o "$out/host-netconf6"
"$out/host-netconf6"
