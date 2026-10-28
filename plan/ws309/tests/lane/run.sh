#!/bin/sh
# Builds and runs the host test of the compositor's lanes and thread register (wayland/lane.c, threads.c)
# under ASan+UBSan and TSan.
#   sh plan/ws309/tests/lane/run.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../../.."
out=${1:-build/ws309/host-lane}
mkdir -p "$out"
for sanitizer in address,undefined thread; do
	${CC:-cc} -std=gnu89 -Wall -Wextra -Werror -D_GNU_SOURCE -I. -fsanitize=$sanitizer -g -pthread \
	    userland/desktop/wayland/lane.c userland/desktop/wayland/threads.c \
	    plan/ws309/tests/lane/host-lane.c -o "$out/host-lane-${sanitizer%%,*}"
	echo "== sanitizer $sanitizer"
	timeout 60 "$out/host-lane-${sanitizer%%,*}"
done
