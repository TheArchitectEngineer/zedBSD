#!/bin/sh
# ws160-p001: builds and runs the host test of who may trace whom (cred_may_trace, cred_ids_differ of src/kern/cred.c,
# compiled freestanding like the kernel; the rest of cred.c is left out by the linker).
#   sh plan/ws160/tests/run-host-cred-trace.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws160-host}
mkdir -p "$out"
cc=${CC:-clang}
$cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -ffreestanding -nostdlibinc -fno-builtin -ffunction-sections \
	-fdata-sections -D__ZEDBSD__ -DKERN_USER_ABI_LP64 -I include -I src -c src/kern/cred.c -o "$out/cred.o"
$cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -c plan/ws160/tests/host-cred-trace.c -o "$out/host-cred-trace.o"
$cc -Wl,--gc-sections "$out/host-cred-trace.o" "$out/cred.o" -o "$out/host-cred-trace"
exec "$out/host-cred-trace"
