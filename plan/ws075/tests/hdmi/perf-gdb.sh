#!/bin/sh
# ws075-p018: reads the i915 frame-timing totals of three windows (about 5 s each) from the running H4 run through
# QEMU's gdbstub, without the kernel log: a hardware breakpoint where drv_i915_perf_report() starts a new window
# (the store of window_start, address found in VMUNIX's disassembly), and the totals (microseconds / count per stage:
# 0 submit, 1 run, 2 gpu, 3 present, 4 copy, 5 marker wait, 6 marker run, 7 publish, 8 flip) printed then.
#   plan/ws075/tests/hdmi/perf-gdb.sh VMUNIX
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../../.."
vmunix=$1
addr=$(gdb -batch -ex "disassemble drv_i915_perf_report" "$vmunix" 2>/dev/null | grep -m1 'mov    %r12,0x20(%r15)' | awk '{print $1}')
[ -n "$addr" ] || { echo "perf-gdb: no window_start store found"; exit 1; }
script=$(mktemp)
cat > "$script" <<GDB
set pagination off
set confirm off
target remote :1234
hbreak *$addr
set \$k = 0
while \$k < 3
  continue
  set \$p = \$r15
  printf "WIN elapsed_ms=%lu", (\$r12 - *(unsigned long *)(\$p+0x20)) / 1000000
  set \$i = 0
  while \$i < 9
    printf " s%d=%lu/%u", \$i, *(unsigned long *)(\$p+0x28+8*\$i) / 1000, *(unsigned *)(\$p+0x70+4*\$i)
    set \$i = \$i + 1
  end
  printf "\n"
  set \$k = \$k + 1
end
delete
detach
GDB
timeout 30 plan/ws075/tests/hdmi-h4-hw.sh ctl hmp gdbserver tcp:127.0.0.1:1234 > /dev/null 2>&1
ssh -f -N -o ExitOnForwardFailure=yes -L 1234:127.0.0.1:1234 "${I915_HOST:-solaris10-man}"
sleep 1
timeout 60 gdb -batch -x "$script" "$vmunix" 2>&1 | grep '^WIN'
pid=$(pgrep -f '^ssh -f -N -o ExitOnForwardFailure=yes -L 1234')
[ -n "$pid" ] && kill $pid
rm -f "$script"
