#!/bin/sh
# ws134-p005: the kernel's per-CPU times (sysctl hw.cputimes) on a running zedBSD guest (any image of this tree's
# kernel with sysctl, top and awk, for example the System Monitor's: plan/ws134/tests/build-monitor-image.sh BUILD, then
# plan/tools/files/files-guest.sh start BUILD/hdd-image.img; the guest has 4 CPUs).  Over SSH, never the console:
#  1. The header: hz, and as many CPUs as hw.ncpu.
#  2. Quiet, 5 seconds: every count only grows; the ticks of all CPUs add up to 5 s x hz x CPUs within 10%; mostly
#     idle (over half).
#  3. One busy loop (awk), 5 seconds: the user ticks grow by at least 60% of one CPU's 5 seconds; the sum within 10%.
#  4. As many busy loops as CPUs, 5 seconds: the user ticks at least 60% of all the CPUs' 5 seconds, and at least two
#     CPUs over half busy (the loops spread); the sum within 10%.
#  5. top -b -n 2 -d 1: two "%Cpu(s):" rows with the user, system, idle and other shares.
#
#   plan/ws134/tests/cputimes-p005.sh [OUTDIR]
# Prints "cputimes-p005: PASS" or "cputimes-p005: FAIL".
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws134-p005}
mkdir -p "$out"
status=0
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }

# Judges two readings of hw.cputimes taken 5 seconds apart (a file: the first reading, a line "--", the second) with
# the step's rule: quiet, one or all.
judge() {
	python3 - "$out/$1" "$2" <<'EOF' || status=1
import sys
path, rule = sys.argv[1], sys.argv[2]
text = open(path).read()
ok = True
def expect(condition, what):
    global ok
    print(("ok: " if condition else "FAILED: ") + rule + ": " + what)
    ok = ok and condition
parts = text.split("--")
def parse(part):
    hz = cpus = None
    rows = {}
    for line in part.splitlines():
        if not line.startswith("hw.cputimes:"):
            continue
        fields = dict(f.split("=", 1) for f in line.split()[1:] if "=" in f)
        if "hz" in fields:
            hz, cpus = int(fields["hz"]), int(fields["cpus"])
        elif "cpu" in fields:
            rows[int(fields["cpu"])] = [int(fields[k]) for k in ("user", "system", "idle", "other")]
    return hz, cpus, rows
if len(parts) != 2:
    expect(False, "two readings")
    sys.exit(1)
hz, cpus, first = parse(parts[0])
hz2, cpus2, second = parse(parts[1])
expect(hz is not None and cpus is not None and len(first) == cpus and len(second) == cpus, "both readings whole")
if not ok:
    sys.exit(1)
grew = all(b >= a for cpu in first for a, b in zip(first[cpu], second[cpu]))
expect(grew, "every count only grows")
delta = {cpu: [b - a for a, b in zip(first[cpu], second[cpu])] for cpu in first}
total = sum(sum(d) for d in delta.values())
user = sum(d[0] for d in delta.values())
idle = sum(d[2] for d in delta.values())
want = 5 * hz * cpus
expect(abs(total - want) <= want * 0.10, "the ticks add up to 5 s x hz x CPUs: %d of %d" % (total, want))
if rule == "quiet":
    expect(idle > total / 2, "mostly idle: %d of %d" % (idle, total))
elif rule == "one":
    expect(user >= 0.6 * 5 * hz, "one CPU busy in user mode: %d user ticks (want %d or more)" % (user, int(0.6 * 5 * hz)))
else:
    expect(user >= 0.6 * want, "every CPU busy in user mode: %d user ticks (want %d or more)" % (user, int(0.6 * want)))
    busy = sum(1 for d in delta.values() if sum(d) and d[0] > sum(d) / 2)
    expect(busy >= min(2, cpus), "the loops spread: %d CPUs over half busy" % busy)
for cpu in sorted(delta):
    print("  cpu %d: user %d system %d idle %d other %d" % ((cpu,) + tuple(delta[cpu])))
sys.exit(0 if ok else 1)
EOF
}

# 1. The header and the CPUs.
guest 'sysctl hw.ncpu; sysctl hw.cputimes' > "$out/header.txt"
ncpu=$(sed -n 's/^hw.ncpu: \([0-9]*\).*/\1/p' "$out/header.txt")
set -- $(sed -n 's/^hw.cputimes: hz=\([0-9]*\) cpus=\([0-9]*\).*/\1 \2/p' "$out/header.txt")
if [ -n "${1:-}" ] && [ "${2:-x}" = "${ncpu:-y}" ] && [ "$(grep -c 'hw.cputimes: cpu=' "$out/header.txt")" = "$ncpu" ]; then
	echo "ok: header hz=$1 cpus=$2 (hw.ncpu $ncpu), a line a CPU"
else
	echo "FAILED: header (see $out/header.txt)"
	status=1
fi

# 2. Quiet.
guest 'sysctl hw.cputimes; echo --; sleep 5; sysctl hw.cputimes' > "$out/quiet.txt"
judge quiet.txt quiet

# 3. One busy loop.
guest "awk 'BEGIN { for (;;) ; }' & p=\$!; sleep 1; sysctl hw.cputimes; echo --; sleep 5; sysctl hw.cputimes; kill \$p" > "$out/one.txt"
judge one.txt one

# 4. A busy loop a CPU.
guest "n=\$(sysctl hw.ncpu | sed 's/.*: //'); i=0; pids=; while [ \$i -lt \$n ]; do awk 'BEGIN { for (;;) ; }' & pids=\"\$pids \$!\"; i=\$((i+1)); done; sleep 1; sysctl hw.cputimes; echo --; sleep 5; sysctl hw.cputimes; kill \$pids" > "$out/all.txt"
judge all.txt all

# 5. top's CPU row.
guest 'top -b -n 2 -d 1 | grep "%Cpu"' > "$out/top.txt"
rows=$(grep -cE '^%Cpu\(s\): +[0-9.]+ us, +[0-9.]+ sy, +[0-9.]+ id, +[0-9.]+ ot$' "$out/top.txt")
if [ "$rows" = 2 ]; then
	echo "ok: top: two %Cpu(s) rows"
	sed 's/^/  /' "$out/top.txt"
else
	echo "FAILED: top: $rows %Cpu(s) rows (see $out/top.txt)"
	status=1
fi

# The guest keeps no loop running.
guest 'for p in $(ps -A -o pid,args | grep "[a]wk BEGIN" | awk "{print \$1}"); do kill $p; done' >/dev/null
[ $status -eq 0 ] && echo "cputimes-p005: PASS" || echo "cputimes-p005: FAIL"
exit $status
