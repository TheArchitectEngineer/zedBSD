#!/bin/sh
# ws075: one capture run on the i915 passthrough of the 5330 (plan/ws031/tests/vkloop-hw.sh MODE with CAPTURE=SCENARIO,
# the capture scenarios of plan/ws031/tests/i915-capture.py).  Takes the machine's lock for the whole run and copies what
# the run leaves in /tmp (shared by every user of the machine) into OUTDIR before the lock is released: capture/
# (result.json, the PPM images, sheet.png), guest-logs.txt (the guest's own logs from its disk, when the mode leaves
# them) and run.log (vkloop-hw.sh's output).
#
#   plan/ws075/tests/capture-hw.sh SCENARIO MODE OUTDIR
#       e.g. plan/ws075/tests/capture-hw.sh zdesktop zdesktop build/ws075-p001/hw-zdesktop
#   I915_HOST as vkloop-hw.sh (default here awe@10.0.30.3); other variables (KEILAND_APP, MVIEW_ARGS, BUILD) pass through.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
[ $# -eq 3 ] || { echo "usage: $0 SCENARIO MODE OUTDIR"; exit 2; }
scenario=$1
mode=$2
out=$3
cd "$(dirname -- "$0")/../../.."
rm -rf "$out"
mkdir -p "$out"
I915_HOST=${I915_HOST:-awe@10.0.30.3}
export I915_HOST

# A zdesktop run builds one image per KEILAND_APP: vkloop-hw.sh rebuilds only when an input file is newer than the
# image, so a changed file list (run-home.sh in place of run-mview.sh, the terminal's font) would keep the old image.
if [ "$mode" = zdesktop ] && [ -z "${BUILD:-}" ]; then
	BUILD=build/resident-zdesktop-${KEILAND_APP:-mview}
	export BUILD
fi

# The machine, for the whole run; what the run left in /tmp is copied before the lock goes.
exec 9>/tmp/i915-hw.lock
flock 9
rm -rf /tmp/capture-last /tmp/zdesktop-guest-logs.txt
CAPTURE=$scenario plan/ws031/tests/vkloop-hw.sh "$mode" > "$out/run.log" 2>&1
status=$?
[ -d /tmp/capture-last ] && cp -r /tmp/capture-last "$out/capture"
[ -f /tmp/zdesktop-guest-logs.txt ] && cp /tmp/zdesktop-guest-logs.txt "$out/guest-logs.txt"
flock -u 9
echo "capture-hw: $scenario ($mode) vkloop-hw.sh exit=$status; $out/capture/result.json"
exit $status
