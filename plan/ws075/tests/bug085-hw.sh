#!/bin/sh
# ws075-p015 (BUG-085): one capture run of a zdesktop image on the 5330's i915 passthrough, with a stall watcher and a
# gdbstub, from this host.  The image is one plan/ws031/tests/vkloop-hw.sh built (e.g. CAPTURE=zdesktop-egltest
# KEILAND_APP=egltest6 BUILD=build/b085-e6 VKLOOP_BUILD_ONLY=1 plan/ws031/tests/vkloop-hw.sh zdesktop): the
# compositor, two wl_shm windows, the wlkill client (wltest killed while it draws) and egltest's scenes.
#
#   plan/ws075/tests/bug085-hw.sh IMAGE OUTDIR [SCENARIO]      (SCENARIO of i915-capture.py, default zdesktop-egltest)
#
# Takes the machine (flock /tmp/i915-hw.lock) for the run.  On the 5330, bug085/b085-qemu.sh runs QEMU (with a
# gdbstub on 127.0.0.1:1235), the capture harness and bug085/b085-watch.py.  A normal run ends a few seconds after
# init's power-off.  When the captured frames stop (the file "stall") or the kernel faults ("fault") and the run has
# not ended by B085_END_SECONDS (default 420) after the start, QEMU is left running and the lock held: the script
# prints how to attach gdb (ssh solaris10-man, gdb ~/bigbang/b085/vmunix, target remote 127.0.0.1:1235) and waits
# until OUTDIR/release exists, then ends QEMU.  OUTDIR receives kernel.log (debugcon), serial.log, qemu.log,
# watch.log, harness.out, capture/ (result.json and the images) and guest-logs.txt (the guest's own logs from its
# disk: zdesktop.log, wlkill.log, mview.log, dmesg.log, vk.log).
# I915_HOST names the 5330 (default solaris10-man).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
[ $# -ge 2 ] || { echo "usage: $0 IMAGE OUTDIR [SCENARIO]"; exit 2; }
image=$1
out=$2
scenario=${3:-zdesktop-egltest}
cd "$(dirname -- "$0")/../../.."
host=${I915_HOST:-solaris10-man}
remote=bigbang/b085
end_seconds=${B085_END_SECONDS:-420}
rm -rf "$out"
mkdir -p "$out"
vmunix=$(dirname -- "$image")/vmunix

# The machine, for the whole run.
exec 9>/tmp/i915-hw.lock
flock 9
ssh "$host" bigbang/igpu-mode.sh vfio > /dev/null || { echo "iGPU is not on vfio-pci"; exit 1; }
ssh "$host" "mkdir -p $remote"
scp -q "$image" "$host:$remote/guest.img" || exit 1
scp -q "$vmunix" plan/ws075/tests/bug085/b085-qemu.sh plan/ws075/tests/bug085/b085-watch.py \
	plan/ws031/tests/i915-capture.py "$host:$remote/" || exit 1
ssh -n -f "$host" "cd $remote && setsid nohup bash b085-qemu.sh /home/awe/$remote/guest.img $scenario < /dev/null > /dev/null 2>&1 &"
start=$(date +%s)
echo "bug085-hw: started $(date '+%H:%M:%S') ($image, $scenario)"

# Waits for the end, or for a stall or a fault that outlives a normal run.
state=running
while :; do
	sleep 5
	marks=$(ssh "$host" "cd $remote && ls end stall fault 2>/dev/null | tr '\n' ' '")
	case " $marks " in *" end "*) state=ended; break ;; esac
	now=$(date +%s)
	if [ $((now - start)) -ge "$end_seconds" ]; then
		case " $marks " in *" stall "*|*" fault "*) state=held; break ;; esac
	fi
	if [ $((now - start)) -ge 1500 ]; then
		state=timeout
		break
	fi
done
if [ "$state" = held ]; then
	ssh "$host" "cd $remote && cat stall fault 2>/dev/null"
	echo "bug085-hw: QEMU kept for the debugger: ssh $host; gdb ~/$remote/vmunix -ex 'target remote 127.0.0.1:1235'"
	echo "bug085-hw: touch $out/release to end the run"
	until [ -f "$out/release" ]; do sleep 5; done
	ssh "$host" "sudo -n pkill -TERM -f 'qemu-system-x86_6[4].*b085/guest'; sleep 3; true"
	until ssh "$host" "test -f $remote/end"; do sleep 3; done
fi

# What the run left.
scp -q "$host:$remote/run.log" "$out/kernel.log"
scp -q "$host:$remote/serial.log" "$out/serial.log"
for f in qemu.log watch.log harness.out stall fault poweroff; do
	scp -q "$host:$remote/$f" "$out/$f" 2>/dev/null
done
scp -qr "$host:$remote/capture-out" "$out/capture" 2>/dev/null
rm -f "$out/capture/pmem.bin"
scp -q plan/ws031/tests/ufs-cat.py tools/build/check-ufs-image.py "$host:bigbang/"
ssh "$host" "python3 bigbang/ufs-cat.py $remote/guest.img /var/log/zdesktop.log /var/log/wlkill.log /var/log/mview.log /var/log/dmesg.log /var/log/vk.log" \
	> "$out/guest-logs.txt" 2>&1
flock -u 9
echo "bug085-hw: $state after $(( $(date +%s) - start )) s; $out"
