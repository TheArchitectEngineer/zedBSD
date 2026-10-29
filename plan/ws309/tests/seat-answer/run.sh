#!/bin/sh
# Builds and runs the host test of the answers the compositor owes the seat (wayland/seat-answer.c) under ASan+UBSan.
#   sh plan/ws309/tests/seat-answer/run.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../../.."
out=${1:-build/ws309/host-seat-answer}
mkdir -p "$out"
${CC:-cc} -std=gnu89 -Wall -Wextra -Werror -D_GNU_SOURCE -I. -fsanitize=address,undefined -g \
    userland/desktop/wayland/seat-answer.c plan/ws309/tests/seat-answer/host-seat-answer.c -o "$out/host-seat-answer"
timeout 60 "$out/host-seat-answer"
