#!/bin/bash
# ws075-p015 (BUG-085): one capture run of a zdesktop image on the 5330's i915 passthrough, on the 5330 (copied to
# ~/bigbang/b085/ by plan/ws075/tests/bug085-hw.sh).  The reference conditions of ~/bigbang/run-parity-vk.sh (q35,
# KVM, 4 GiB, 4 vCPUs, the iGPU with vfio-pci, OVMF, NVMe) with QMP=1's socket and USB input, plus a gdbstub on
# 127.0.0.1:1235 that is idle until a debugger connects.  The capture harness (i915-capture.py SCENARIO) runs beside
# it, and b085-watch.py ends the run a few seconds after init's power-off, or keeps QEMU alive for the debugger when
# the captured frames stop (the file "stall") or the kernel reports a fault (the file "fault").
#
#   b085-qemu.sh IMAGE SCENARIO          (B085_SECONDS bounds QEMU, default 1500)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
image=$1
scenario=$2
dir=/home/awe/bigbang/b085
cd "$dir" || exit 1
cp -f /usr/share/OVMF/OVMF_VARS_4M.fd vars.fd
sudo -n rm -rf qmp.sock run.log serial.log qemu.log stall fault end watch.log harness.out capture-out
args=(
	-machine q35,accel=kvm,memory-backend=mem
	-cpu host,host-phys-bits-limit=39
	-m 4096
	-smp 4
	-object memory-backend-memfd,id=mem,size=4G,share=on
	-device vfio-pci,host=0000:00:02.0,x-igd-opregion=on,rombar=0
	-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd
	-drive if=pflash,format=raw,file=$dir/vars.fd
	-drive file=$image,format=raw,if=none,id=zd0 -device nvme,drive=zd0,serial=zedbsd0
	-vga std -display none -monitor none -serial file:$dir/serial.log -nic none
	-debugcon file:$dir/run.log -no-reboot
	-qmp unix:$dir/qmp.sock,server=on,wait=off
	-device qemu-xhci,id=xhci -device usb-tablet,bus=xhci.0 -device usb-kbd,bus=xhci.0
	-gdb tcp:127.0.0.1:1235
)
sudo -n timeout "${B085_SECONDS:-1500}" qemu-system-x86_64 "${args[@]}" > qemu.log 2>&1 &
qemu=$!
python3 b085-watch.py > watch.log 2>&1 &
watcher=$!
# The harness, once QEMU's socket is there.
for i in $(seq 1 50); do
	[ -S qmp.sock ] && break
	sleep 0.1
done
sudo -n python3 i915-capture.py "$scenario" --output="$dir/capture-out" --serial="$dir/serial.log" \
	--qmp="$dir/qmp.sock" > harness.out 2>&1
sudo -n chown -R awe: capture-out
wait $qemu
echo "b085-qemu: QEMU ended (status $?)" >> qemu.log
kill $watcher 2>/dev/null
touch end
