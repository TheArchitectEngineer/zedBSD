#!/bin/sh
# ws174-p003: builds and runs the host test of the UEFI loader's boot key detection
# (boot-keys-host-test.c, K1 to K3): bootloader/uefi/boot-keys.c against a mock
# system table, with the host compiler, once plain and once with ASan and UBSan.
# The mock's callbacks use the UEFI calling convention (ms_abi), as the loader calls them.
#
#   plan/ws174/tests/run-boot-keys-host-test.sh [OUTDIR]   (default build/ws174-host)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws174-host}
mkdir -p "$out"
CC=${CC:-cc}
FLAGS="-std=c11 -O1 -g -Wall -Wextra -Werror -fshort-wchar -I."
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
sources="bootloader/uefi/boot-keys.c plan/ws174/tests/boot-keys-host-test.c"
$CC $FLAGS $sources -o "$out/boot-keys-host-test"
"$out/boot-keys-host-test"
$CC $FLAGS $SAN $sources -o "$out/boot-keys-host-test-san"
"$out/boot-keys-host-test-san"
