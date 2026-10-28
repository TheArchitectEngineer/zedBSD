#!/bin/sh
# Builds and runs the host test of the zedBSD backend's finding of the input devices on a compositor lane
# (input-zedbsd.c kl_backend_input_find on wayland/lane.c), /dev/input faked by the linker's --wrap,
# under ASan+UBSan and TSan.
#   sh plan/ws309/tests/input-find/run.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../../.."
out=${1:-build/ws309/host-input-find}
mkdir -p "$out"
wraps="-Wl,--wrap=opendir -Wl,--wrap=readdir -Wl,--wrap=closedir -Wl,--wrap=open -Wl,--wrap=ioctl"
for sanitizer in address,undefined thread; do
	${CC:-cc} -std=gnu89 -Wall -Wextra -Werror -D_GNU_SOURCE -I. -Iinclude -fsanitize=$sanitizer -g -pthread \
	    userland/desktop/libkeiland-backend-zedbsd/input-zedbsd.c \
	    userland/desktop/wayland/lane.c userland/desktop/wayland/threads.c \
	    plan/ws309/tests/input-find/host-input-find.c $wraps -o "$out/host-input-find-${sanitizer%%,*}"
	echo "== sanitizer $sanitizer"
	timeout 60 "$out/host-input-find-${sanitizer%%,*}"
done
