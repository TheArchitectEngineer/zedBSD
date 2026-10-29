#!/bin/sh
# Builds and runs the host test of the render thread's tasks (wayland/render-thread.c)
# under ASan+UBSan and TSan.
#   sh plan/ws309/tests/render-task/run.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../../.."
out=${1:-build/ws309/host-render-task}
mkdir -p "$out"
for sanitizer in address,undefined thread; do
	${CC:-cc} -std=gnu89 -Wall -Wextra -Werror -D_GNU_SOURCE -I. -fsanitize=$sanitizer -g -pthread \
	    userland/desktop/wayland/render-thread.c \
	    plan/ws309/tests/render-task/host-render-task.c -o "$out/host-render-task-${sanitizer%%,*}"
	echo "== sanitizer $sanitizer"
	timeout 60 "$out/host-render-task-${sanitizer%%,*}"
done
