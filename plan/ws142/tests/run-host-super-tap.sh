#!/bin/sh
# ws142-p002: builds and runs the host test of the Windows key pressed alone (super-tap.c) under the sanitizers.
#   sh plan/ws142/tests/run-host-super-tap.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws142-host}
mkdir -p "$out"
${CC:-cc} -std=gnu89 -Wall -Wextra -Werror -I. -fsanitize=address,undefined -g \
    userland/desktop/wayland/super-tap.c plan/ws142/tests/host-super-tap.c -o "$out/host-super-tap"
exec "$out/host-super-tap"
