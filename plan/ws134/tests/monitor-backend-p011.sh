#!/bin/sh
# ws134-p011: the System Monitor's backend area on Linux and FreeBSD (libkeiland-backend-linux/monitor-linux.c and
# libkeiland-backend-freebsd/monitor-freebsd.c) through the probe userland/tests/monitor-probe/main.c built natively,
# sampling twice 3 s apart while one busy loop runs:
#   linux     on this Linux host (gcc): the probe and its judgement
#   freebsd   in the WS137 FreeBSD guest (plan/tools/keiland-freebsd/guest.py; started when it is not running): the
#             tree's files copied, the probe built with the base cc against the backend's sources, run and judged
# Judged: as many CPUs as the system says, a disk and a link or more (no loopback), valid has the CPUs, the memory,
# the swap, the links and the disks (0x1f), the CPUs' ticks grew by 3 s x hz x CPUs within 15%, the user ticks by 60%
# of one CPU's 3 s or more, the memory's total above 0 with the free and the caches within it.  The probe's lines are
# OUT/<os>.txt.
#
#   plan/ws134/tests/monitor-backend-p011.sh linux|freebsd|all [OUT]
# Prints "monitor-backend-p011 <os>: PASS" or FAIL for each.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
which=${1:-all}
out=${2:-build/ws134-p011}
mkdir -p "$out"
status=0

# Judges a probe's output (FILE, the CPUs the system says).
judge() {
	python3 - "$1" "$2" <<'EOF'
import sys
lines = open(sys.argv[1]).read().splitlines()
system_cpus = int(sys.argv[2] or 0)
ok = True
def expect(condition, what):
    global ok
    print(("ok: " if condition else "FAILED: ") + what)
    ok = ok and condition
def fields(line):
    return dict(f.split("=", 1) for f in line.split()[2:] if "=" in f)
info = next((fields(l) for l in lines if l.startswith("MPROBE INFO")), None)
disks = [fields(l) for l in lines if l.startswith("MPROBE DISK")]
links = [fields(l) for l in lines if l.startswith("MPROBE LINK")]
samples = [fields(l) for l in lines if l.startswith("MPROBE SAMPLE")]
expect("MPROBE DONE" in lines, "the probe ran to its end")
expect(info is not None and int(info["cpus"]) == system_cpus, "CPUs %s (the system's %d)" % (info and info["cpus"], system_cpus))
expect(len(disks) >= 1, "disks: %s" % [(d["name"], d["kind"]) for d in disks])
expect(len(links) >= 1 and not any(l["name"].startswith("lo") for l in links), "links without the loopback: %s" % [l["name"] for l in links])
expect(len(samples) == 2, "two samples")
if len(samples) == 2:
    a, b = samples
    n = lambda s, k: int(s[k], 0)
    expect(n(b, "valid") & 0x1f == 0x1f, "valid has the CPUs, memory, swap, links, disks: %s" % b["valid"])
    seconds = (n(b, "time_ns") - n(a, "time_ns")) / 1e9
    hz, cpus = n(b, "cpu_hz"), n(b, "cpus")
    ticks = sum(n(b, k) - n(a, k) for k in ("user", "system", "idle", "other"))
    want = seconds * hz * cpus
    expect(abs(ticks - want) <= 0.15 * want, "the CPUs' ticks %d (want %.0f: %.2f s x %d Hz x %d)" % (ticks, want, seconds, hz, cpus))
    user = n(b, "user") - n(a, "user")
    expect(user >= 0.6 * seconds * hz, "user ticks %d (one busy loop)" % user)
    total = n(b, "mem_total")
    expect(total > 0 and n(b, "mem_free") <= total and n(b, "cache") <= total, "memory total %d, free %s, caches %s" % (total, b["mem_free"], b["cache"]))
sys.exit(0 if ok else 1)
EOF
}

# Linux: the probe built here and run under a busy loop.
if [ "$which" = linux ] || [ "$which" = all ]; then
	mkdir -p "$out/linux-bin"
	if gcc -std=gnu17 -D_GNU_SOURCE -O1 -Wall -Wextra -Werror -Wno-format-truncation -I. -Iuserland/desktop/include \
	    userland/tests/monitor-probe/main.c userland/desktop/libkeiland-backend-linux/monitor-linux.c \
	    userland/desktop/libkeiland-backend-linux/network-link-linux.c userland/desktop/libkeiland-backend/wpa/network-wpa.c \
	    userland/desktop/libkeiland-backend/wpa/network-config-wpa.c -o "$out/linux-bin/probe" > "$out/linux-build.log" 2>&1; then
		awk 'BEGIN { for (;;) ; }' &
		busy=$!
		sleep 1
		"$out/linux-bin/probe" 2 3000 > "$out/linux.txt" 2>&1
		kill "$busy"
		if judge "$out/linux.txt" "$(getconf _NPROCESSORS_CONF)"; then
			echo "monitor-backend-p011 linux: PASS"
		else
			echo "monitor-backend-p011 linux: FAIL"
			status=1
		fi
	else
		echo "monitor-backend-p011 linux: FAIL (build, $out/linux-build.log)"
		status=1
	fi
fi

# FreeBSD: the probe built in the guest with its base cc and run under a busy loop.
if [ "$which" = freebsd ] || [ "$which" = all ]; then
	guest() { python3 plan/tools/keiland-freebsd/guest.py "$@"; }
	started=0
	if ! guest status | grep -q '^ssh: ready'; then
		guest start
		started=1
	fi
	src=/root/keiland-p011
	guest copy "$src" include userland/desktop/include userland/desktop/libkeiland-backend userland/desktop/libkeiland-backend-freebsd \
	    userland/tests/monitor-probe > "$out/freebsd-copy.log" 2>&1
	guest ssh "cd $src && mkdir -p build && cc -std=gnu17 -Wall -Wextra -Werror -I. -Iuserland/desktop/include -I/usr/local/include \
	    userland/tests/monitor-probe/main.c userland/desktop/libkeiland-backend-freebsd/monitor-freebsd.c \
	    userland/desktop/libkeiland-backend-freebsd/network-link-freebsd.c userland/desktop/libkeiland-backend/wpa/network-wpa.c \
	    userland/desktop/libkeiland-backend/wpa/network-config-wpa.c -o build/probe" > "$out/freebsd-build.log" 2>&1
	guest ssh "cd $src && sysctl -n hw.ncpu > build/ncpu && (awk 'BEGIN { for (;;) ; }' & p=\$!; sleep 1; build/probe 2 3000; kill \$p)" > "$out/freebsd.txt" 2>&1
	cpus=$(guest ssh "cat $src/build/ncpu" 2>/dev/null | tr -dc 0-9)
	if judge "$out/freebsd.txt" "${cpus:-0}"; then
		echo "monitor-backend-p011 freebsd: PASS"
	else
		echo "monitor-backend-p011 freebsd: FAIL (build log $out/freebsd-build.log)"
		status=1
	fi
	[ "$started" = 1 ] && guest stop
fi
exit $status
