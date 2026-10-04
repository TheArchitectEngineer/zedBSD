#!/bin/sh
# ws099-p032: builds and runs the host test of the system bar's network details (network-info.c) under the sanitizers.
#   sh plan/ws099/tests/host-network-info.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws099-host}
mkdir -p "$out"
${CC:-cc} -std=gnu89 -Wall -Wextra -Werror -D_GNU_SOURCE -I. -fsanitize=address,undefined -g \
    userland/desktop/wayland/network-info.c plan/ws099/tests/host-network-info.c -o "$out/host-network-info"
exec "$out/host-network-info"
