#!/bin/sh
# ws101-p016: the parts of the times of the scene S13 (mix.nct, N integers, ROUNDS calls) on a running guest (the
# Venus guest of plan/ws035/tests/zdesktop-guest.sh, an image with /usr/share/gpudemo): the CPU run, then the GPU run
# with KEI_GLES_COMPUTE_TRACE=2 (libEGL and libGLESv2 time their steps), both logs copied out and split by
# time-split.py.  BIN's libEGL.so and libGLESv2.so are put in first when BIN is given, and NOCT (a Noct built with the
# accelerator, default /bin/noct) is put in as /tmp/noct when given.
#   GUEST_RUNTIME=... [BIN=build/amd64] [NOCT=path] plan/ws101/tests/time-split.sh OUTDIR [N [ROUNDS]]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=$1
n=${2:-4000000}
rounds=${3:-6}
mkdir -p "$out"
guest() { timeout 600 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
put() { timeout 120 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 </dev/null || { echo "put $1: FAILED"; exit 1; }; }

# The libraries and the interpreter under test.
if [ -n "${BIN:-}" ]; then
	put "$BIN/dynamic/libEGL.so" /lib/libEGL.so
	put "$BIN/dynamic/libGLESv2.so" /lib/libGLESv2.so
fi
noct=/bin/noct
if [ -n "${NOCT:-}" ]; then
	put "$NOCT" /tmp/noct
	guest 'chmod 755 /tmp/noct' >/dev/null
	noct=/tmp/noct
fi

# The two runs, as s13.sh runs them.
flags="-O2 -j --gc-tenure-size=100000000"
guest "cd /tmp; $noct $flags /usr/share/gpudemo/mix.nct $n $rounds > /tmp/split-cpu.log 2>&1; echo cpu=\$?" | tail -1
guest "cd /tmp; KEI_GLES_COMPUTE_TRACE=2 $noct $flags --gpu /usr/share/gpudemo/mix.nct $n $rounds > /tmp/split-gpu.log 2>&1; echo gpu=\$?" | tail -1
guest 'cat /tmp/split-cpu.log' > "$out/cpu.log"
guest 'cat /tmp/split-gpu.log' > "$out/gpu.log"
grep '^MIX first_ms\|^MIX check' "$out/cpu.log" "$out/gpu.log"
python3 plan/ws101/tests/time-split.py "$out/gpu.log" "$out/cpu.log" | tee "$out/split.txt"
