#!/bin/sh
# ws070 (p005): the System Menu on the i915 passthrough of the 5330 (plan/ws031/tests/vkloop-hw.sh zdesktop
# with ZDESKTOP_APP=home and the capture scenario zdesktop-menu of plan/ws031/tests/i915-capture.py).
# Takes the machine's lock for the whole run and copies what the run leaves in /tmp (shared by every
# user of the machine) into OUTDIR before the lock is released: capture/ (result.json, the PPM images,
# sheet.png), guest-logs.txt (the guest's own logs from its disk) and run.log (vkloop-hw.sh's output).
#
#   plan/tools/titlebar/menu-hw.sh [OUTDIR]        (default build/ws070-hw; I915_HOST as vkloop-hw.sh, default awe@10.0.30.3)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws070-hw}
rm -rf "$out"
mkdir -p "$out"
I915_HOST=${I915_HOST:-awe@10.0.30.3}
export I915_HOST
exec 9>/tmp/i915-hw.lock
flock 9
rm -rf /tmp/capture-last /tmp/zdesktop-guest-logs.txt
CAPTURE=zdesktop-menu ZDESKTOP_APP=home plan/ws031/tests/vkloop-hw.sh zdesktop > "$out/run.log" 2>&1
status=$?
[ -d /tmp/capture-last ] && cp -r /tmp/capture-last "$out/capture"
[ -f /tmp/zdesktop-guest-logs.txt ] && cp /tmp/zdesktop-guest-logs.txt "$out/guest-logs.txt"
flock -u 9
echo "menu-hw: vkloop-hw.sh exit=$status; $out/capture/result.json"
exit $status
