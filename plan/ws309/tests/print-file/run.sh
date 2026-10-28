#!/bin/sh
# Builds and runs the host test of the printers' settings file on the host's thread (libkeiland-backend/print/print.c)
# under ASan+UBSan and TSan.
#   sh plan/ws309/tests/print-file/run.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../../.."
out=${1:-build/ws309/host-print-file}
mkdir -p "$out"
for sanitizer in address,undefined thread; do
	${CC:-cc} -std=gnu99 -Wall -Wextra -Werror -D_GNU_SOURCE -I. -fsanitize=$sanitizer -g -pthread \
	    userland/desktop/libkeiland-backend/print/print.c plan/ws309/tests/print-file/host-print-file.c \
	    -o "$out/host-print-file-${sanitizer%%,*}"
	echo "== sanitizer $sanitizer"
	TEST_DIR="$(pwd)/$out" timeout 60 "$out/host-print-file-${sanitizer%%,*}"
done
