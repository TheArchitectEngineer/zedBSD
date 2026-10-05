#!/bin/sh
# ws174-p002: builds and runs the host test of the boot keys' record rewrite
# (boot-override-host-test.c, O1 to O10): bootloader/common/boot-override.c with
# the host compiler, once plain and once with ASan and UBSan.
#
#   plan/ws174/tests/run-boot-override-host-test.sh [OUTDIR]   (default build/ws174-host)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws174-host}
mkdir -p "$out"
CC=${CC:-cc}
FLAGS="-std=c11 -O1 -g -Wall -Wextra -Werror -I."
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
sources="bootloader/common/boot-override.c plan/ws174/tests/boot-override-host-test.c"
$CC $FLAGS $sources -o "$out/boot-override-host-test"
"$out/boot-override-host-test"
$CC $FLAGS $SAN $sources -o "$out/boot-override-host-test-san"
"$out/boot-override-host-test-san"
