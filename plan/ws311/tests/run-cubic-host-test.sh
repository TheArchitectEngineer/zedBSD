#!/bin/sh
# Builds and runs the host check of CUBIC's integer window function (src/kern/net/tcp-cubic.c against double).
#   plan/ws311/tests/run-cubic-host-test.sh      OUT= the build folder (default build/ws311-host)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${OUT:-build/ws311-host}
mkdir -p "$out"
${CC:-clang} -std=c11 -O2 -Wall -Wextra -Werror -I. -o "$out/cubic-host-test" plan/ws311/tests/cubic-host-test.c \
    src/kern/net/tcp-cubic.c -lm || { echo "cubic-host-test: build failed"; exit 2; }
exec timeout 120 "$out/cubic-host-test"
