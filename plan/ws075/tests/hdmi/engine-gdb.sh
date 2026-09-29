#!/bin/sh
# ws075-p018/p021: which GPU sessions keep the render engine busy, on the running H4 run, through QEMU's gdbstub
# (no kernel log): the worker's per-context engine time (struct i915_worker_context engine_ns / engine_runs, worker.c)
# is read twice SECONDS apart while h4-ctl.py rate moves the pointer (at RATE_XY "X Y" when set), and each session's
# runs per second, share of the engine and time per run are printed, then the rate line.  The worker is found at a
# hardware breakpoint on i915_worker_run(); the context table's layout (first record, stride, 32 records) is read
# from VMUNIX's disassembly.
#   plan/ws075/tests/hdmi/engine-gdb.sh VMUNIX SECONDS
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../../.."
vmunix=$1
seconds=$2
work=$(mktemp -d)
run=$(nm "$vmunix" | awk '/ i915_worker_run$/{print $1}')
stride=$(gdb -batch -ex "disassemble i915_worker_run" "$vmunix" 2>/dev/null | grep -m1 -oE 'add +\$0x[0-9a-f]+,%r1[23]' | grep -oE '0x[0-9a-f]+')
[ -n "$run" ] && [ -n "$stride" ] || { echo "engine-gdb: layout not found"; exit 1; }
cat > "$work/eng.gdb" <<GDB
set pagination off
set confirm off
target remote :1234
hbreak *0x$run
continue
set \$w = \$rdi
delete
set \$i = 0
while \$i < 32
  set \$r = \$w + 0x10 + $stride * \$i
  if *(unsigned long *)\$r != 0
    printf "CTX %d session=%lx ns=%lu runs=%u\n", \$i, *(unsigned long *)\$r - 0x18, *(unsigned long *)(\$r+8), *(unsigned *)(\$r+0x10)
  end
  set \$i = \$i + 1
end
detach
GDB
timeout 30 plan/ws075/tests/hdmi-h4-hw.sh ctl hmp gdbserver tcp:127.0.0.1:1234 > /dev/null
ssh -f -N -o ExitOnForwardFailure=yes -L 1234:127.0.0.1:1234 "${I915_HOST:-solaris10-man}"
sleep 1
(timeout $((seconds + 30)) plan/ws075/tests/hdmi-h4-hw.sh ctl rate A $((seconds + 4)) ${RATE_XY:-} > "$work/rate.txt" 2>&1 &)
timeout 30 gdb -batch -x "$work/eng.gdb" "$vmunix" 2>&1 | grep CTX > "$work/eng1.txt"
t0=$(date +%s.%N)
sleep "$seconds"
timeout 30 gdb -batch -x "$work/eng.gdb" "$vmunix" 2>&1 | grep CTX > "$work/eng2.txt"
t1=$(date +%s.%N)
pid=$(pgrep -f '^ssh -f -N -o ExitOnForwardFailure=yes -L 1234')
[ -n "$pid" ] && kill $pid
python3 - "$work" "$t0" "$t1" <<'PY'
import re, sys
work, elapsed = sys.argv[1], float(sys.argv[3]) - float(sys.argv[2])
def read(path):
    table = {}
    for line in open(path):
        m = re.search(r'CTX (\d+) session=(\w+) ns=(\d+) runs=(\d+)', line)
        if m:
            table[m.group(2)] = (int(m.group(3)), int(m.group(4)))
    return table
first, second = read(work + '/eng1.txt'), read(work + '/eng2.txt')
total = 0
for session, (ns, runs) in second.items():
    if session not in first:
        continue
    dn, dr = ns - first[session][0], runs - first[session][1]
    total += dn
    if dr:
        print(f'session {session}: {dr / elapsed:.1f} runs/s, engine {dn / 1e7 / elapsed:.1f}%, {dn / 1e6 / dr:.2f} ms/run')
print(f'elapsed {elapsed:.1f} s, engine busy {total / 1e7 / elapsed:.1f}%')
PY
sleep 5
tail -1 "$work/rate.txt"
rm -rf "$work"
