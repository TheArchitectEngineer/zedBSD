#!/bin/sh
# BUG-090: compiles the C library's sorts (src/libc/sort.c) for the host, plain, with ASan and with UBSan,
# checks them against the host's qsort (qsort-host.c) and times the old and the new qsort.
#   sh plan/ws073/tests/qsort-host.sh [OUT]      (OUT defaults to build/ws073-p026/qsort-host)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws073-p026/qsort-host}
cc=${CC:-cc}
mkdir -p "$out"
status=0
for variant in plain asan ubsan; do
	flags="-O2 -g -Wall -Wextra -Werror -I."
	[ "$variant" = asan ] && flags="$flags -fsanitize=address -fno-omit-frame-pointer"
	[ "$variant" = ubsan ] && flags="$flags -fsanitize=undefined,alignment -fno-sanitize-recover=all"
	# The library file is held to C89 as it is in the tree; the test uses C99 for its printf formats.
	"$cc" $flags -std=c89 -pedantic -c plan/ws073/tests/qsort-host-sort.c -o "$out/sort-$variant.o"
	"$cc" $flags -std=gnu99 plan/ws073/tests/qsort-host.c "$out/sort-$variant.o" -o "$out/qsort-host-$variant"
done
for variant in plain asan ubsan; do
	echo "== $variant"
	if ! timeout 1200 "$out/qsort-host-$variant" check > "$out/check-$variant.txt" 2>&1; then
		status=1
	fi
	tail -8 "$out/check-$variant.txt"
done
echo "== time"
timeout 1200 "$out/qsort-host-plain" time | tee "$out/time.txt"
[ "$status" = 0 ] && echo "qsort-host: ok" || echo "qsort-host: FAILED"
exit "$status"
