#!/bin/sh
# ws139-p001: the ledger's P-01 (C5, the first frames of App Home and Wiseview), P-02 (direct typing latency) and P-03
# (the compositor's cost of a frame under the System Monitor's simulation), one after another on one guest of the
# performance image (plan/ws139/tests/build-perf-image.sh).  Measured, not judged: a step that fails or times out is
# recorded and the next one runs.
#  1. wait   the guest answers SSH (guest.py wait); otherwise "perf-run: FAIL guest not up"
#  2. env    OUT/env.txt: the commit (PERF_COMMIT, else git), the image and its SHA-256, the environment's mark (--env,
#            E1 by default), the date, the host's CPUs, load, IO pressure and memory, KVM (query-kvm: TCG stops the run,
#            "perf-run: FAIL no KVM"), the guest's wallpaper files, the host's Mesa shader cache
#  3. monitor (P-03) zdesktop --glass --log-frames and /bin/monitor --source=sim --seed=9 --cpus=16 --gpus=2, 20 s; the
#            ZWL PERF and ZMON FRAME lines to OUT/monitor.out, the device lines to OUT/device.txt
#  4. c5     (P-01) C5_ROUNDS=5 plan/ws099/tests/c5-transitions.sh OUT/c5 > OUT/c5.out
#  5. latency (P-02) plan/ws139/tests/type-only.sh OUT/latency > OUT/latency.out
#  6. the compositor and the applications stopped
# Then plan/ws139/tests/perf-summary.py OUT makes OUT/summary.tsv and the table.
#
#   GUEST_RUNTIME=... plan/ws089/tests/settings-guest.sh start BUILD/hdd-image.img
#   GUEST_RUNTIME=... plan/ws139/tests/perf-run.sh [--env=E1|E2] OUT
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws139-run}"
export GUEST_RUNTIME
mark=E1
case "${1:-}" in
--env=*)
	mark=${1#--env=}
	shift
	;;
esac
[ $# -eq 1 ] || { echo "usage: perf-run.sh [--env=E1|E2] OUT" >&2; exit 2; }
out=$1
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[m]onitor|[k]eiland-ime|[t]extedit|[t]erminal|[p]opup-probe" | awk "{print \$1}"); do kill $p; done; sleep 1'
steps=$out/steps.txt
: > "$steps"

# Runs one step under a time limit and records how it ended (ok, the exit status, or TIMEOUT).
step() {
	name=$1
	limit=$2
	shift 2
	timeout "$limit" "$@"
	result=$?
	if [ $result -eq 124 ]; then
		echo "$name TIMEOUT" >> "$steps"
	else
		echo "$name exit=$result" >> "$steps"
	fi
}

# 1. The guest answers.
if ! timeout 300 python3 plan/tools/guest/guest.py wait --timeout 240 >/dev/null 2>&1; then
	echo "perf-run: FAIL guest not up"
	exit 1
fi

# 2. The environment.
commit=${PERF_COMMIT:-$(git rev-parse HEAD 2>/dev/null || echo unknown)}
image=$(python3 -c "import json, sys; print(json.load(open(sys.argv[1])).get('image', ''))" "$GUEST_RUNTIME/session.json" 2>/dev/null)
kvm=$(timeout 30 python3 plan/tools/qmp.py "$GUEST_RUNTIME/qmp.sock" query-kvm 2>&1)
{
	echo "commit: $commit"
	echo "image: $image"
	[ -n "$image" ] && [ -f "$image" ] && echo "image_sha256: $(sha256sum "$image" | cut -d' ' -f1)"
	echo "image_config: config-amd64-perf.mk"
	echo "env: $mark"
	echo "date: $(date -Is)"
	echo "host_cpus: $(nproc)"
	echo "host_load: $(uptime)"
	[ -r /proc/pressure/io ] && echo "host_io_pressure: $(tr '\n' ' ' < /proc/pressure/io)"
	echo "host_memory_gb: $(free -g | awk '/^Mem:/ {print $2 " total, " $7 " available"}')"
	echo "kvm: $kvm"
	echo "guest_wallpaper: $(guest 'ls /usr/share/keiland/wallpaper.* 2>&1' | tr '\n' ' ')"
	echo "host_mesa_cache: $(du -sh "$HOME"/.cache/mesa_shader_cache* 2>/dev/null | tr '\n' ' ')"
} > "$out/env.txt"
case "$kvm" in
*'"enabled": true'*) ;;
*)
	echo "perf-run: FAIL no KVM"
	exit 1
	;;
esac

# 3. The compositor's cost of a frame while the System Monitor's simulation draws (P-03).
monitor_step() {
	guest "$stop_all" >/dev/null
	guest "export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=600 --width=1280 --height=800 --glass --log-frames \$picture > /tmp/zdesktop.log 2>&1 </dev/null & for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q ZWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; sleep 1
/bin/monitor --timeout-s=300 --source=sim --seed=9 --cpus=16 --gpus=2 --token=c > /tmp/monitor.log 2>&1 </dev/null & echo started" >/dev/null
	sleep 20
	guest "grep -E 'ZWL PERF' /tmp/zdesktop.log; grep 'ZMON FRAME' /tmp/monitor.log" > "$out/monitor.out"
	guest "grep -E 'ZWL DISPLAY device=' /tmp/zdesktop.log; grep 'ZMON READY' /tmp/monitor.log" > "$out/device.txt"
}
# Each guest call has its own 90 s limit (a shell function cannot run under timeout), so the step is bounded.
monitor_step
lines=$(grep -c 'ZWL PERF compose' "$out/monitor.out" 2>/dev/null)
echo "monitor compose_lines=${lines:-0}" >> "$steps"

# 4. The first frames of App Home and Wiseview, five rounds (P-01).
step c5 300 env C5_ROUNDS=5 sh plan/ws099/tests/c5-transitions.sh "$out/c5" > "$out/c5.out" 2>&1

# 5. The direct typing latency (P-02).
step latency 240 sh plan/ws139/tests/type-only.sh "$out/latency" > "$out/latency.out" 2>&1

# 6. Everything stopped.
guest "$stop_all" >/dev/null
echo "perf-run: done ($(tr '\n' ' ' < "$steps"))"
exit 0
