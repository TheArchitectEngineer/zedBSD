#!/bin/sh
# ws130-p007: builds and runs host-dhcp6.c with the DHCPv6 messages of dhcpc -6 (userland/base/net/dhcp6.c) and the
# host's headers, with ASan and UBSan.
#   sh plan/ws130/tests/host-dhcp6.sh [OUTDIR]     (default build/ws130-host)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws130-host}
mkdir -p "$out"
${CC:-cc} -std=gnu99 -O1 -g -Wall -Wextra -Werror -D_GNU_SOURCE -fsanitize=address,undefined -fno-sanitize-recover=all \
	-fno-omit-frame-pointer -I. plan/ws130/tests/host-dhcp6.c userland/base/net/dhcp6.c -o "$out/host-dhcp6"
"$out/host-dhcp6"
