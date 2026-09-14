#!/bin/sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
# Runs gcc -fanalyzer and clang --analyze over the i915 driver sources with the
# kernel configuration, writing the diagnostics to plan/ws029/phase007/.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=$repo/plan/ws029/phase007
mkdir -p "$out"
sysroot=$repo/build/amd64/sysroot
defines="-DHAL_ARCH_AMD64 -DHAL_BOARD_PCAT -DKERN_USER_ABI_LP64 \
  -DCONFIG_DRIVER_PCI_I915=1 -DCONFIG_DRIVER_PCI_I915_SELFTEST=1 \
  -DCONFIG_GPU_JOB_RESERVATION_MS=10000 -DCONFIG_GPU_JOB_EXECUTION_MS=10000 \
  -DCONFIG_GPU_JOB_STOP_MS=10000 -DCONFIG_GPU_CONTROL_MS=10000"
includes="-nostdinc -isystem $sysroot/usr/include -I$repo/include -I$repo/src -I$repo"
sources="i915 uncore ggtt ppgtt gem engine lrc request irq selftest"

gcc_log=$out/analyzer-gcc.log
clang_log=$out/analyzer-clang.log
: > "$gcc_log"
: > "$clang_log"

for name in $sources; do
    src=$repo/src/drivers/gpu/i915/$name.c
    echo "=== $name.c" >> "$gcc_log"
    gcc -fanalyzer -fsyntax-only -std=gnu11 -Wall -Wextra \
        -Wno-analyzer-too-complex -ffreestanding $defines $includes "$src" \
        >> "$gcc_log" 2>&1 || echo "(gcc exit $?)" >> "$gcc_log"
    echo "=== $name.c" >> "$clang_log"
    clang --analyze --analyzer-output text -std=gnu11 -ffreestanding \
        $defines $includes "$src" >> "$clang_log" 2>&1 || echo "(clang exit $?)" >> "$clang_log"
done

echo "gcc warnings: $(grep -c "warning:" "$gcc_log" || true)"
echo "clang warnings: $(grep -c "warning:" "$clang_log" || true)"
