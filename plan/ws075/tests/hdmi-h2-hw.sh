#!/bin/sh
# ws075-p012 (H2): Keiland on the resident display of the 5330's i915 passthrough, with boot parameters of the run's own
# (display=hdmi, display.mode=...), and screenshots read from the scanout buffers.
#
# Builds the zdesktop image (plan/ws031/tests/vkloop-hw.sh zdesktop, KEILAND_APP=home: App Home, no client) with the
# serial mirror (plan/ws075/tests/config-test-hw.mk), the compositor at the display's preferred size
# (plan/ws075/tests/hdmi/run-zdesktop-full.sh) and ZEDBSD_BOOT_EXTRA_LINES in its zedbsd.cfg; then, holding the machine's
# lock, runs it with QMP and the USB poller (hdmi/usb-poll.sh), and saves both resident buffers with the monitor's
# memsave at the given seconds after the buffers exist (hdmi/shot.py).  OUTDIR gets serial.log, guest-logs.txt,
# usb-poll.log, usb-new/, shots/ (raw, PNG, sheet.png) and run.log.
#
#   plan/ws075/tests/hdmi-h2-hw.sh OUTDIR "BOOT LINES" [SECONDS...]
#       e.g. plan/ws075/tests/hdmi-h2-hw.sh build/ws075-h2/hdmi "display=hdmi" 40 120
#   BUILD (default build/h2-img) and I915_HOST (default solaris10-man) pass through.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
[ $# -ge 2 ] || { echo "usage: $0 OUTDIR \"BOOT LINES\" [SECONDS...]"; exit 2; }
out=$1
lines=$2
shift 2
times=${*:-40 120}
cd "$(dirname -- "$0")/../../.."
rm -rf "$out"
mkdir -p "$out"
BUILD=${BUILD:-build/h2-img}
ZEDBSD_CONFIG=plan/ws075/tests/config-test-hw.mk
I915_HOST=${I915_HOST:-solaris10-man}
KEILAND_APP=home
KEILAND_ZDESKTOP_SH=plan/ws075/tests/hdmi/run-zdesktop-full.sh
ZEDBSD_BOOT_EXTRA_LINES=$lines
VKLOOP_BUILD_ONLY=1
export BUILD ZEDBSD_CONFIG I915_HOST KEILAND_APP KEILAND_ZDESKTOP_SH ZEDBSD_BOOT_EXTRA_LINES VKLOOP_BUILD_ONLY

# The image, outside the lock.
plan/ws031/tests/vkloop-hw.sh zdesktop > "$out/build.out" 2>&1 || { cat "$out/build.out"; exit 1; }
cp "$BUILD/resident-build.log" "$out/build.log" 2>/dev/null
echo "hdmi-h2-hw: image $BUILD/hdd-image.img, boot lines: $lines"

# The machine, for the whole run.
exec 9>/tmp/i915-hw.lock
flock 9
ssh $I915_HOST bigbang/igpu-mode.sh vfio >/dev/null || { echo "iGPU is not on vfio-pci"; flock -u 9; exit 1; }
scp -q "$BUILD/hdd-image.img" $I915_HOST:bigbang/guest-parity.img || { flock -u 9; exit 1; }
scp -q plan/ws075/tests/hdmi/shot.py $I915_HOST:bigbang/h2-shot.py
scp -q plan/ws075/tests/hdmi/usb-poll.sh $I915_HOST:bigbang/usb-poll.sh
ssh $I915_HOST 'rm -f bigbang/run-parity-serial.log; nohup sh bigbang/usb-poll.sh >/dev/null 2>&1 &'
ssh $I915_HOST 'cd ~/bigbang && rm -f run-parity-serial.log && QMP=1 ./run-parity-vk.sh >/dev/null 2>&1; cp run-parity-serial.log vkloop-h2.log' &
launcher=$!
sleep 5
ssh $I915_HOST "sudo -n rm -rf bigbang/h2-shots; sudo -n python3 bigbang/h2-shot.py bigbang/run-parity-serial.log bigbang/qmp.sock /home/awe/bigbang/h2-shots $times; sudo -n chown -R awe: bigbang/h2-shots" > "$out/run.log" 2>&1
wait $launcher
ssh $I915_HOST 'touch bigbang/usb-poll-stop'
sleep 2
scp -q $I915_HOST:bigbang/vkloop-h2.log "$out/serial.log"
scp -qr $I915_HOST:bigbang/h2-shots "$out/shots"
scp -q $I915_HOST:bigbang/usb-poll.log "$out/usb-poll.log"
scp -qr $I915_HOST:bigbang/usb-new "$out/usb-new"
scp -q plan/ws031/tests/ufs-cat.py tools/build/check-ufs-image.py $I915_HOST:bigbang/
ssh $I915_HOST 'python3 bigbang/ufs-cat.py bigbang/guest-parity.img /var/log/zdesktop.log /var/log/dmesg.log' > "$out/guest-logs.txt" 2>&1
flock -u 9

# The screenshots as PNG.
python3 plan/ws075/tests/hdmi/raw2png.py "$out/shots" >> "$out/run.log" 2>&1
cat "$out/run.log"
grep -aE 'display output|resident display: (HDMI|buffers|first frame|ended)' "$out/serial.log" | cut -c1-220
echo "new USB devices: $(ls "$out/usb-new" 2>/dev/null | tr '\n' ' ')"
