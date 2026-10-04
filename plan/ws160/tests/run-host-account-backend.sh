#!/bin/sh
# ws160-p002: builds and runs the host test of the zedBSD backend's password change (account-zedbsd.c) with a stand-in
# passwd (fake-passwd.sh), under the sanitizers.
#   sh plan/ws160/tests/run-host-account-backend.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws160-host}
mkdir -p "$out"
fake="$(pwd)/plan/ws160/tests/fake-passwd.sh"
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -I. -fsanitize=address,undefined -g \
    "-DACCOUNT_PASSWD_PATH=\"$fake\"" -pthread \
    userland/desktop/libkeiland-backend-zedbsd/account-zedbsd.c plan/ws160/tests/host-account-backend.c \
    -o "$out/host-account-backend"
exec "$out/host-account-backend" "$(pwd)/$out/fake-passwd-lines.txt"
