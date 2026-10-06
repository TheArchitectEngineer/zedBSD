#!/bin/sh
# ws130-p004: builds and runs host-libc6.c with the resolver's DNS part (userland/base/libc/resolver-dns.c) and the
# host's headers, plain and with ASan and UBSan.
#   sh plan/ws130/tests/host-libc6.sh [OUTDIR]     (default build/ws130-host)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws130-host}
mkdir -p "$out"
for variant in plain asan; do
	flags="-std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -I."
	[ "$variant" = asan ] && flags="$flags -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
	${CC:-cc} $flags -o "$out/host-libc6-$variant" plan/ws130/tests/host-libc6.c userland/base/libc/resolver-dns.c
	"$out/host-libc6-$variant" | tail -1
done
