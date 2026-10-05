#!/bin/sh
# ws089-p026: builds and runs the host test of account-admin's pure part (host-account-admin.c with
# userland/base/account-admin/edit.c) under ASan and UBSan.  Last line: host-account-admin: N checks, 0 failed.
#   sh plan/ws089/tests/run-host-account-admin.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=build/ws089-host
mkdir -p "$out"
${CC:-cc} -std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
    -I. plan/ws089/tests/host-account-admin.c userland/base/account-admin/edit.c -o "$out/host-account-admin" ||
	{ echo "host-account-admin: FAIL (build)"; exit 1; }
timeout 60 "$out/host-account-admin"
