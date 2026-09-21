#!/bin/sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
# Runs gcc -fanalyzer over the Vulkan executor (src/drivers/gpu/i915/render/)
# and the shader compiler (src/drivers/gpu/i915/compiler/) with the kernel
# configuration, writing the diagnostics to plan/ws031/phase012/.  Fails when a
# source does not compile or the analyzer reports anything.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=$repo/plan/ws031/phase012; mkdir -p "$out"
sysroot=$repo/build/amd64/sysroot
defines="-DHAL_ARCH_AMD64 -DHAL_BOARD_PCAT -DKERN_USER_ABI_LP64 -DCONFIG_DRIVER_PCI_I915=1"
includes="-nostdinc -isystem $sysroot/usr/include -I$repo/include -I$repo/src -I$repo"
driver=$repo/src/drivers/gpu/i915
sources=$(cd "$driver" && ls render/*.c compiler/*.c)
gcc_log=$out/analyzer-gcc.log; : > "$gcc_log"
failed=0
for name in $sources; do
  echo "=== $name" >> "$gcc_log"
  if ! gcc -fanalyzer -fsyntax-only -std=gnu11 -Wall -Wextra -Wno-analyzer-too-complex \
    -ffreestanding $defines $includes "$driver/$name" >> "$gcc_log" 2>&1; then
    echo "(gcc exit)" >> "$gcc_log"
    failed=1
  fi
done
warnings=$(grep -c "warning:" "$gcc_log" || true)
echo "gcc analyzer warnings: $warnings"
if [ "$failed" -ne 0 ] || [ "$warnings" -ne 0 ]; then
  echo "WS031 vk analyzer FAIL: see $gcc_log"
  exit 1
fi
echo "WS031 vk analyzer PASS: $(echo $sources | wc -w) sources in render/ and compiler/"
