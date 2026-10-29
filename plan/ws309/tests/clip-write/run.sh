#!/bin/sh
# Builds and runs the host test of the clipboard's write of the offered text (wayland/clipboard.c) under ASan+UBSan:
# the offer does not wait for the reader, the passes write the rest, a reader that never reads is given up, the
# lock's clear ends a write.
#   sh plan/ws309/tests/clip-write/run.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../../.."
out=${1:-build/ws309/host-clip}
mkdir -p "$out"
${CC:-cc} -std=gnu89 -Wall -Wextra -Werror -D_GNU_SOURCE -I. -Iuserland/desktop/wayland -fsanitize=address,undefined -g \
    userland/desktop/wayland/clipboard.c plan/ws309/tests/clip-write/host-clip.c -o "$out/host-clip"
timeout 60 "$out/host-clip"
