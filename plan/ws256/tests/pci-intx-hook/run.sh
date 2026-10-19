#!/bin/sh
# Host check of the PCI core's INTx hook for a host that demultiplexes its
# own pins (ws256-p021): the real src/drivers/pci/pci.c against fake hosts,
# once plainly and once with ASan/UBSan.  No emulator.
#   plan/ws256/tests/pci-intx-hook/run.sh OUTDIR
# OUTDIR is the caller's (for example one made by plan/tools/fresh-out.sh
# under build/tmp/); the script removes nothing.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws256/tests/pci-intx-hook
out=$1
mkdir -p "$out"
cc=${CC:-cc}
flags="-std=c11 -D_POSIX_C_SOURCE=200809L -pthread -I$root/include -I$root -I$root/src -Wall -Wextra -Werror"

# shellcheck disable=SC2086
$cc $flags -O2 "$root/src/drivers/pci/pci.c" "$here/pci-intx-hook-test.c" \
	-o "$out/pci-intx-hook-test"
"$out/pci-intx-hook-test"

# shellcheck disable=SC2086
$cc $flags -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
	"$root/src/drivers/pci/pci.c" "$here/pci-intx-hook-test.c" \
	-o "$out/pci-intx-hook-test-sanitize"
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 "$out/pci-intx-hook-test-sanitize"

echo "pci intx hook: ordinary, ASan/UBSan PASS"
