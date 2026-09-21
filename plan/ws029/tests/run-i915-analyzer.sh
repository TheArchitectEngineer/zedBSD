#!/bin/sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
# Runs gcc -fanalyzer and clang --analyze over the i915 core sources (the
# files directly under src/drivers/gpu/i915/; render/ and compiler/ have
# plan/ws031/tests/run-vk-analyzer.sh) with the kernel configuration, writing
# the diagnostics to plan/ws029/phase007/.  Fails only when a source does not
# compile; the warning counts are reported for review.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=$repo/plan/ws029/phase007
mkdir -p "$out"
sysroot=$repo/build/amd64/sysroot
defines="-DHAL_ARCH_AMD64 -DHAL_BOARD_PCAT -DKERN_USER_ABI_LP64 \
  -DCONFIG_DRIVER_PCI_I915=1 \
  -DCONFIG_GPU_JOB_RESERVATION_MS=10000 -DCONFIG_GPU_JOB_EXECUTION_MS=10000 \
  -DCONFIG_GPU_JOB_STOP_MS=10000 -DCONFIG_GPU_CONTROL_MS=10000"
includes="-nostdinc -isystem $sysroot/usr/include -I$repo/include -I$repo/src -I$repo"
driver=$repo/src/drivers/gpu/i915
sources=$(cd "$driver" && ls *.c)

gcc_log=$out/analyzer-gcc.log
clang_log=$out/analyzer-clang.log
: > "$gcc_log"
: > "$clang_log"
failed=0

for name in $sources; do
    src=$driver/$name
    echo "=== $name" >> "$gcc_log"
    if ! gcc -fanalyzer -fsyntax-only -std=gnu11 -Wall -Wextra \
        -Wno-analyzer-too-complex -ffreestanding $defines $includes "$src" \
        >> "$gcc_log" 2>&1; then
        echo "(gcc exit)" >> "$gcc_log"
        failed=1
    fi
    echo "=== $name" >> "$clang_log"
    if ! clang --analyze --analyzer-output text -std=gnu11 -ffreestanding \
        $defines $includes "$src" -o /dev/null >> "$clang_log" 2>&1; then
        echo "(clang exit)" >> "$clang_log"
        failed=1
    fi
done

echo "gcc warnings: $(grep -c "warning:" "$gcc_log" || true)"
echo "clang warnings: $(grep -c "warning:" "$clang_log" || true)"
if [ "$failed" -ne 0 ]; then
    echo "i915 analyzer FAIL: a source did not compile; see $out"
    exit 1
fi
echo "i915 analyzer PASS: $(echo $sources | wc -w) sources analyzed"
