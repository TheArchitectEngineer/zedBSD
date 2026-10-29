#!/bin/sh
# Builds and runs the host test of the event loop's calls while the compositor's threads are slow (host-slow.c,
# wayland/lane.c and threads.c) under ASan+UBSan and TSan.
#   sh plan/ws309/tests/slow/run.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../../.."
out=${1:-build/ws309/host-slow}
mkdir -p "$out"
for sanitizer in address,undefined thread; do
	${CC:-cc} -std=gnu89 -Wall -Wextra -Werror -D_GNU_SOURCE -I. -fsanitize=$sanitizer -g -pthread \
	    userland/desktop/wayland/lane.c userland/desktop/wayland/threads.c \
	    plan/ws309/tests/slow/host-slow.c -o "$out/host-slow-${sanitizer%%,*}"
	echo "== sanitizer $sanitizer"
	timeout 60 "$out/host-slow-${sanitizer%%,*}"
done
