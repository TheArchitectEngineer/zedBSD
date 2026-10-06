#!/bin/sh
# ws139-p002: the 5330's side of plan/ws139/tests/e2-run.sh (run in the directory e2-run.sh made, with PERF_COMMIT
# set): gives the iGPU to the host's i915 (~/bigbang/igpu-mode.sh host), starts the performance image's guest with
# Venus on the host's Intel Vulkan driver (no software GL, unlike plan/ws035/tests/zdesktop-guest.sh), measures twice
# with perf-run.sh --env=E2 (out-1, out-2) and summarizes them.  Whatever happens, it stops the guest and gives the
# iGPU back to vfio-pci (out-1/igpu-after.txt), or every later passthrough test stops.
#  - On battery it refuses (BUG-159: drawing falls to about 5 fps); the governor is recorded.
#  - out-N/E2.txt says whether the guest's Vulkan device was Intel (E2) or llvmpipe (not E2: not for the ledger).
#
#   cd ~/ws139-e2.XXXXXX && PERF_COMMIT=... sh plan/ws139/tests/e2-remote.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
here=$(pwd)
export GUEST_RUNTIME=$here/run
mkdir -p "$here/out-1" "$here/out-2" "$GUEST_RUNTIME"

# Stops the guest, waits for its emulator to end, and gives the iGPU back to vfio-pci.
cleanup() {
	python3 plan/tools/guest/guest.py stop >/dev/null 2>&1
	i=0
	while pgrep -f '^qemu-system-x86_64' >/dev/null 2>&1 && [ $i -lt 30 ]; do
		sleep 1
		i=$((i+1))
	done
	"$HOME/bigbang/igpu-mode.sh" vfio > "$here/out-1/igpu-after.txt" 2>&1
	"$HOME/bigbang/igpu-mode.sh" show >> "$here/out-1/igpu-after.txt" 2>&1
}
trap cleanup EXIT
trap 'exit 1' INT TERM

# On AC only; the governor for the record.
online=0
for supply in /sys/class/power_supply/*/online; do
	[ -f "$supply" ] || continue
	[ "$(cat "$supply")" = 1 ] && online=1
done
{
	echo "ac=$online"
	echo "governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)"
	echo "host=$(hostname) kernel=$(uname -r)"
} > "$here/out-1/host.txt"
if [ "$online" != 1 ]; then
	echo "e2-remote: FAIL on battery (BUG-159)"
	exit 1
fi

# The iGPU to the host's i915.
"$HOME/bigbang/igpu-mode.sh" host || { echo "e2-remote: FAIL igpu-mode.sh host"; exit 1; }

# The host's Intel Vulkan driver, the strict-queue Venus renderer, and the guest.
icd=$(ls /usr/share/vulkan/icd.d/intel_icd*.json 2>/dev/null | head -1)
[ -n "$icd" ] || { echo "e2-remote: FAIL no Intel ICD in /usr/share/vulkan/icd.d"; exit 1; }
export VK_DRIVER_FILES=$icd
renderer=$HOME/zedbsd-q306-venus/dependencies/q312-quiesce/install
export RENDER_SERVER_EXEC_PATH=$renderer/libexec/virgl_render_server
export LD_LIBRARY_PATH=$renderer/lib/x86_64-linux-gnu
. plan/tools/guest/venus-hostmem.sh
python3 plan/tools/guest/guest.py start image.img --qemu-extra "-object memory-backend-memfd,id=mem,size=8G,share=on -machine memory-backend=mem -device virtio-gpu-gl-pci,id=venus,venus=on,blob=on,hostmem=$VENUS_HOSTMEM,max_outputs=1 -display egl-headless,rendernode=/dev/dri/renderD128 -vnc unix:$GUEST_RUNTIME/vnc.sock,display=venus -device usb-tablet,bus=xhci.0,port=4" \
	|| { echo "e2-remote: FAIL the guest did not start (see $GUEST_RUNTIME/qemu.log)"; exit 1; }

# Measured twice, each summarized, each marked E2 only when the guest drew on the Intel device.
status=0
for run in 1 2; do
	PERF_COMMIT=${PERF_COMMIT:-} sh plan/ws139/tests/perf-run.sh --env=E2 "out-$run" || status=1
	python3 plan/ws139/tests/perf-summary.py "out-$run" || status=1
	if grep -qi 'llvmpipe' "out-$run/device.txt" 2>/dev/null; then
		echo "E2=no device=llvmpipe" > "out-$run/E2.txt"
		status=1
	elif grep -qi 'intel' "out-$run/device.txt" 2>/dev/null; then
		echo "E2=yes" > "out-$run/E2.txt"
	else
		echo "E2=unknown" > "out-$run/E2.txt"
		status=1
	fi
done

echo "e2-remote: status=$status"
exit $status
