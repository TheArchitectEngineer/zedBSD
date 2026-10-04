#!/bin/sh
# ws132-p004: host test of the backend's volumes on zedBSD (libkeiland-backend-zedbsd/volume-zedbsd.c) against a fake
# volumed, with ASan/UBSan.
#   sh plan/ws132/tests/run-host-volumes.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -std=gnu17 -O1 -g -Wall -Wextra -Werror -D_GNU_SOURCE -fsanitize=address,undefined -fno-omit-frame-pointer -I. \
    -DVOLUME_SOCKET_PATH="\"$work/volumed.sock\"" \
    plan/ws132/tests/host-volumes.c userland/desktop/libkeiland-backend-zedbsd/volume-zedbsd.c -o "$work/host-volumes"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 "$work/host-volumes"
