#!/bin/sh
# ws160-p001: builds and runs the host test of the accounts' shared core (userland/base/common/account.c) with the host's
# C library and its crypt, under the sanitizers.
#   sh plan/ws160/tests/run-host-account.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws160-host}
mkdir -p "$out"
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -I. -fsanitize=address,undefined -g \
    userland/base/common/account.c plan/ws160/tests/host-account.c -lcrypt -o "$out/host-account"
exec "$out/host-account"
