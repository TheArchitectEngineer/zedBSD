#!/bin/sh
# Builds and runs the host test of the file I/O thread's writing of desktop.conf (wayland/settings-store.c on a lane)
# under ASan+UBSan and TSan, with fsync wrapped to be slow.
#   sh plan/ws309/tests/io-file/run.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../../.."
out=${1:-build/ws309/host-io-file}
mkdir -p "$out"
for sanitizer in address,undefined thread; do
	${CC:-cc} -std=gnu89 -Wall -Wextra -Werror -D_GNU_SOURCE -I. -Iuserland/desktop/wayland -fsanitize=$sanitizer -g -pthread \
	    -Wl,--wrap=fsync \
	    userland/desktop/wayland/lane.c userland/desktop/wayland/threads.c userland/desktop/wayland/settings-store.c \
	    userland/desktop/settings-keys/settings-keys.c plan/ws309/tests/io-file/host-io-file.c -o "$out/host-io-file-${sanitizer%%,*}"
	echo "== sanitizer $sanitizer"
	TEST_DIR="$(pwd)/$out" timeout 60 "$out/host-io-file-${sanitizer%%,*}"
done
