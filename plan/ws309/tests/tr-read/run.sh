#!/bin/sh
# Builds and runs the host test of libkeiland's catalogs read on another thread (translate.c's kl_tr_read, kl_tr_use, kl_tr_discard)
# under ASan+UBSan and TSan, with catalogs of its own in the build directory.
#   sh plan/ws309/tests/tr-read/run.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../../.."
out=${1:-build/ws309/host-tr-read}
data="$(pwd)/$out/data"
mkdir -p "$data/keiland/locale/ja"
printf 'msg\tOpen\tOPEN-JA\n' > "$data/keiland/locale/ja/wayland.tr"
printf 'msg\tShared\tSHARED-JA\n' > "$data/keiland/locale/ja/keiland.tr"
for sanitizer in address,undefined thread; do
	${CC:-cc} -std=gnu99 -Wall -Wextra -Werror -D_GNU_SOURCE -I. -Iuserland/desktop/include "-DKEILAND_DATADIR=\"$data\"" \
	    -fsanitize=$sanitizer -g -pthread \
	    userland/desktop/libkeiland/translate.c plan/ws309/tests/tr-read/host-tr-read.c -o "$out/host-tr-read-${sanitizer%%,*}"
	echo "== sanitizer $sanitizer"
	timeout 60 "$out/host-tr-read-${sanitizer%%,*}"
done
