#!/bin/sh
# ws158-p004: builds and runs the host test of account-admin's system-language request (host-admin-language.c with
# userland/base/account-admin/edit.c unchanged) under ASan and UBSan.  Last line: host-admin-language: ok (N checks).
#   sh plan/ws158/tests/run-host-admin-language.sh [BUILD_DIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws158-host}
mkdir -p "$out"
${CC:-cc} -std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
    -I. plan/ws158/tests/host-admin-language.c userland/base/account-admin/edit.c -o "$out/host-admin-language" ||
	{ echo "host-admin-language: FAIL (build)"; exit 1; }
timeout 60 "$out/host-admin-language"
