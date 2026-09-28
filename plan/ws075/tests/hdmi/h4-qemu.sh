#!/bin/bash
# ws075-p013 (H4): runs the demonstration image on the 5330's i915 passthrough for a long time, on the 5330 (copied to
# ~/bigbang/h4/ by plan/ws075/tests/hdmi-h4-hw.sh).  The reference conditions of ~/bigbang/run-parity-vk.sh (q35,
# KVM, 4 GiB, 4 vCPUs, the iGPU with vfio-pci, OVMF, NVMe) with the standard VGA as the firmware's display (the
# loader's splash is taken from it with screendump), two QMP sockets (qmp.sock for the screenshots and the input,
# qmp-load.sock for the periodic load) and a USB tablet and keyboard.  No end-of-run watcher: the run ends when the
# guest has halted and h4-ctl.py quits QEMU, or after H4_MINUTES (default 60).  The kernel's log (debugcon, every
# record, also on a quiet boot) is in run.log, the serial port in serial.log.  From the start, the standard VGA is
# taken every 250 ms until the resident display has its buffers (h4-ctl.py splash; shots/splash-*.ppm, splash.log).
#
#   h4-qemu.sh IMAGE
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
image=$1
dir=/home/awe/bigbang/h4
cd "$dir" || exit 1
cp -f /usr/share/OVMF/OVMF_VARS_4M.fd vars.fd
rm -f qmp.sock qmp-load.sock run.log serial.log qemu.log
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
	-qmp unix:$dir/qmp-load.sock,server=on,wait=off
	-device qemu-xhci,id=xhci -device usb-tablet,bus=xhci.0 -device usb-kbd,bus=xhci.0
)
sudo -n timeout $(( ${H4_MINUTES:-60} * 60 )) qemu-system-x86_64 "${args[@]}" > qemu.log 2>&1 &
qemu=$!
# The firmware's display from the start: the loader's splash until the resident display exists.
for i in $(seq 1 50); do
	[ -S qmp.sock ] && break
	sleep 0.1
done
sudo -n python3 h4-ctl.py splash 180 250 > splash.log 2>&1
wait $qemu
echo "h4-qemu: QEMU ended (status $?)" >> qemu.log
