#!/bin/sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=$repo/plan/ws031/phase012; mkdir -p "$out"
sysroot=$repo/build/amd64/sysroot
defines="-DHAL_ARCH_AMD64 -DHAL_BOARD_PCAT -DKERN_USER_ABI_LP64 -DCONFIG_DRIVER_PCI_I915=1"
includes="-nostdinc -isystem $sysroot/usr/include -I$repo/include -I$repo/src -I$repo"
sources="vk cmd res spirv eu compile pipe cmdbuf sync wsi display"
gcc_log=$out/analyzer-gcc.log; : > "$gcc_log"
for name in $sources; do
  echo "=== $name.c" >> "$gcc_log"
  gcc -fanalyzer -fsyntax-only -std=gnu11 -Wall -Wextra -Wno-analyzer-too-complex \
    -ffreestanding $defines $includes "$repo/src/drivers/gpu/i915/vk/$name.c" >> "$gcc_log" 2>&1 || echo "(gcc exit $?)" >> "$gcc_log"
done
echo "gcc analyzer warnings: $(grep -c "warning:" "$gcc_log" || true)"
