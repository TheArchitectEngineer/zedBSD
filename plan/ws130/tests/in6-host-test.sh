#!/bin/sh
# The host test of IPv6's pure parts (ws130-p002): src/kern/net/in6.c, in6-address.c, in6-route.c and
# in6-neighbor.c with plan/ws130/tests/in6-host-test.c, under ASan and UBSan.
#   plan/ws130/tests/in6-host-test.sh     (from the repository's top; OUT= to choose the build folder)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
OUT=${OUT:-build/ws130-host}
mkdir -p "$OUT"
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I. -Iinclude \
	-o "$OUT/in6-host-test" plan/ws130/tests/in6-host-test.c src/kern/net/in6.c src/kern/net/in6-address.c \
	src/kern/net/in6-route.c src/kern/net/in6-neighbor.c
timeout 60 "$OUT/in6-host-test"
