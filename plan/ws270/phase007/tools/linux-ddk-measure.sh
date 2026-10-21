#!/bin/sh
# zedBSD
# Copyright (C) 2026 Awe Morris
#
# SPDX-License-Identifier: Zlib
#
# linux-ddk-measure.sh (ws270-p007, q1547): the measurements of the IMG DDK
# on Linux on the VisionFive 2, in the order of the plan
# (plan-linux-measure-fable-20261021.md): the environment, the perf cases
# twice, ftrace, PERF_3D_CYCLE from two debug_dumps while drawing, the
# ioctl shim's commands, the pipeline caches, the firmware trace (last, it
# disturbs the timing), then the collection.  Run as root on the board:
#
#   sh linux-ddk-measure.sh <out-dir> [<dir with vkmatrix and pvr-ioctl-shim.so>]
#
# Every output goes into <out-dir>, one file a step and case; the steps
# write what they did and what they skipped to <out-dir>/log.txt.  An
# apphint or a trace directory that is missing skips its step.  devmem is
# never used (reading the GPU's registers while it is off hangs the board).
# The shim's commands and the caches are the proprietary driver's output:
# keep them out of git.

set -u

OUT=${1:?usage: linux-ddk-measure.sh <out-dir> [<tool-dir>]}
TOOLS=${2:-$(dirname "$0")}
VKMATRIX=${VKMATRIX:-$TOOLS/vkmatrix}
SHIM=${SHIM:-$TOOLS/pvr-ioctl-shim.so}
export VK_ICD_FILENAMES=${VK_ICD_FILENAMES:-/etc/vulkan/icd.d/icdconf.json}

# The perf set (step 2), the cases traced, dumped through the shim and cached.
PERF_CASES="perf-clear perf-immediate perf-flat perf-alu-pix perf-texture perf-texture-nob perf-uniform perf-load-draw perf-load-small perf-clear-64 perf-load-64 perf-quarter perf-draws4 perf-alu-pix-sq perf-alu-pix-sq32 perf-flat-blend"
TRACE_CASES=${TRACE_CASES:-"perf-clear perf-clear-64 perf-flat perf-flat-blend perf-texture perf-texture-nob perf-load-small"}
SHIM_CASES=${SHIM_CASES:-"perf-clear perf-clear-64 perf-flat perf-flat-blend perf-texture perf-texture-nob perf-alu-pix-sq32"}
CACHE_CASES=${CACHE_CASES:-"perf-clear perf-flat perf-flat-blend perf-texture perf-texture-nob perf-alu-pix-sq perf-alu-pix-sq32 perf-uniform"}
FW_CASES=${FW_CASES:-"perf-clear perf-clear-64 perf-flat perf-texture"}

# The longest one vkmatrix run may take, in seconds.
LIMIT=${LIMIT:-180}

mkdir -p "$OUT" "$OUT/cache" || exit 1
LOG=$OUT/log.txt
: >>"$LOG"

log() {
	echo "$(date '+%H:%M:%S') $*" | tee -a "$LOG"
}

# The debugfs of the DDK, mounted if it is not.
DEBUG=/sys/kernel/debug
if [ ! -d "$DEBUG/pvr" ]; then
	mount -t debugfs none "$DEBUG" 2>/dev/null
fi
PVR=$DEBUG/pvr
DEBUG_DUMP=$(find "$PVR" -name debug_dump 2>/dev/null | head -n 1)
FW_TRACE=$(find "$PVR" -name firmware_trace 2>/dev/null | head -n 1)
POWER_STATS=$(find "$PVR" -name power_timing_stats 2>/dev/null | head -n 1)

# Finds an apphint's file (the device's first, then the driver's), empty for none.
apphint() {
	find "$PVR" -path '*apphint*' -name "$1" 2>/dev/null | head -n 1
}

# Sets an apphint, logging what it did; returns 1 when it has no file.
set_apphint() {
	hint=$(apphint "$1")
	if [ -z "$hint" ]; then
		log "apphint $1: no file, skipped"
		return 1
	fi
	echo "$2" >"$hint" 2>>"$LOG"
	log "apphint $1 ($hint) = $2, reads $(cat "$hint" 2>/dev/null | tr '\n' ' ')"
	return 0
}

# Runs vkmatrix with its arguments, its output to a file, under the time limit.
run_vkmatrix() {
	file=$1
	shift
	timeout "$LIMIT" "$VKMATRIX" "$@" >"$file" 2>&1
	status=$?
	log "vkmatrix $* -> $(basename "$file") exit $status"
}

log "start: out $OUT vkmatrix $VKMATRIX shim $SHIM icd $VK_ICD_FILENAMES"
if [ ! -x "$VKMATRIX" ]; then
	log "no vkmatrix at $VKMATRIX: stop"
	exit 1
fi

# 1. The environment.
log "step 1: environment"
{
	echo "== date"; date
	echo "== uname"; uname -a
	echo "== cmdline"; cat /proc/cmdline
	echo "== cpuinfo"; cat /proc/cpuinfo
	echo "== modules"; lsmod
	echo "== icd"; cat "$VK_ICD_FILENAMES"
	echo "== pvr status"; cat "$PVR/status" 2>&1
	echo "== clocks"; cat /sys/kernel/debug/clk/clk_summary 2>/dev/null | grep -i -E 'gpu|img|pvr|core|sys'
	echo "== devfreq"; for d in /sys/class/devfreq/*; do [ -d "$d" ] && echo "$d $(cat "$d/cur_freq" 2>/dev/null) $(cat "$d/governor" 2>/dev/null)"; done
	echo "== apphints"; find "$PVR" -path '*apphint*' -type f 2>/dev/null | while read -r f; do echo "$f = $(cat "$f" 2>/dev/null | tr '\n' ' ')"; done
} >"$OUT/env.txt" 2>&1
if [ -n "$POWER_STATS" ]; then
	cat "$POWER_STATS" >"$OUT/power-timing-idle.txt" 2>&1
fi
if [ -n "$DEBUG_DUMP" ]; then
	cat "$DEBUG_DUMP" >"$OUT/debug-dump-idle.txt" 2>&1
fi
dmesg >"$OUT/dmesg-before.txt" 2>&1

# 2. The perf set twice, nothing else running (no trace, shim or cache).
log "step 2: perf set x2"
for pass in 1 2; do
	for c in $PERF_CASES; do
		run_vkmatrix "$OUT/perf-$pass-$c.txt" "$c"
	done
	cat "$OUT"/perf-$pass-*.txt | grep 'perf ' >"$OUT/perf-$pass.txt"
done

# 3. ftrace: the rogue events with a marker a case.
log "step 3: ftrace"
TRACING=/sys/kernel/tracing
if [ ! -d "$TRACING/events" ]; then
	TRACING=$DEBUG/tracing
fi
if [ -d "$TRACING/events/rogue" ]; then
	set_apphint EnableFTraceGPU 1
	echo 0 >"$TRACING/tracing_on"
	echo >"$TRACING/trace"
	echo 16384 >"$TRACING/buffer_size_kb" 2>/dev/null
	echo 1 >"$TRACING/events/rogue/enable"
	ls "$TRACING/events/rogue" >"$OUT/ftrace-events.txt"
	echo 1 >"$TRACING/tracing_on"
	for c in $TRACE_CASES; do
		echo "vkmatrix $c begin" >"$TRACING/trace_marker"
		run_vkmatrix "$OUT/ftrace-run-$c.txt" "$c"
		echo "vkmatrix $c end" >"$TRACING/trace_marker"
	done
	echo 0 >"$TRACING/tracing_on"
	cat "$TRACING/trace" >"$OUT/ftrace.txt"
	echo 0 >"$TRACING/events/rogue/enable"
	set_apphint EnableFTraceGPU 0
else
	log "step 3: no $TRACING/events/rogue (the kernel has no rogue trace events), skipped"
fi

# 4. PERF_3D_CYCLE and PERF_TA_CYCLE: two debug_dumps while drawing, under 6 s apart (the 32-bit counters wrap in about 7.2 s).
log "step 4: debug_dump while drawing"
if [ -n "$DEBUG_DUMP" ]; then
	for c in perf-alu-pix-sq32 perf-texture; do
		timeout "$LIMIT" "$VKMATRIX" -r 40 "$c" >"$OUT/cycles-run-$c.txt" 2>&1 &
		child=$!
		sleep 2
		t1=$(date +%s.%N)
		cat "$DEBUG_DUMP" >"$OUT/debug-dump-$c-1.txt" 2>&1
		sleep 3
		t2=$(date +%s.%N)
		cat "$DEBUG_DUMP" >"$OUT/debug-dump-$c-2.txt" 2>&1
		wait "$child"
		{
			echo "$c: dump 1 at $t1, dump 2 at $t2"
			grep -h -E 'PERF_(3D|TA)_CYCLE' "$OUT/debug-dump-$c-1.txt" "$OUT/debug-dump-$c-2.txt"
		} >>"$OUT/cycles.txt"
		log "step 4: $c dumped at $t1 and $t2"
	done
else
	log "step 4: no debug_dump, skipped"
fi

# 5. The ioctl shim: the TA, 3D and 3D PR commands and the HWRT data sets of each case.
log "step 5: ioctl shim"
if [ -f "$SHIM" ]; then
	for c in $SHIM_CASES; do
		PVR_SHIM_OUT=$OUT/shim-$c.txt LD_PRELOAD=$SHIM run_vkmatrix "$OUT/shim-run-$c.txt" "$c"
	done
else
	log "step 5: no shim at $SHIM, skipped"
fi

# 6. The pipeline caches, a case each.
log "step 6: pipeline caches"
for c in $CACHE_CASES; do
	run_vkmatrix "$OUT/cache-run-$c.txt" -c "$OUT/cache/$c.bin" "$c"
done

# 7. The firmware trace, last: the main, mts and pow groups, read after each case.
log "step 7: firmware trace"
if [ -n "$FW_TRACE" ] && set_apphint FirmwareLogType trace; then
	set_apphint EnableLogGroup main,mts,pow
	for c in $FW_CASES; do
		cat "$FW_TRACE" >/dev/null 2>&1
		run_vkmatrix "$OUT/fwtrace-run-$c.txt" "$c"
		cat "$FW_TRACE" >"$OUT/fwtrace-$c.txt" 2>&1
	done
	set_apphint EnableLogGroup none
	set_apphint FirmwareLogType none
else
	log "step 7: no firmware_trace or FirmwareLogType, skipped"
fi

# 8. The collection: power timing, an idle dump, dmesg, and the list of files.
log "step 8: collect"
if [ -n "$POWER_STATS" ]; then
	cat "$POWER_STATS" >"$OUT/power-timing-after.txt" 2>&1
fi
if [ -n "$DEBUG_DUMP" ]; then
	cat "$DEBUG_DUMP" >"$OUT/debug-dump-after.txt" 2>&1
fi
dmesg >"$OUT/dmesg-after.txt" 2>&1
sync
find "$OUT" -type f | sort | while read -r f; do echo "$(wc -c <"$f") $f"; done >"$OUT/manifest.txt"
log "done: $(wc -l <"$OUT/manifest.txt") files, $(du -sk "$OUT" | cut -f1) KiB"
