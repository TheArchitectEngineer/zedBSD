#!/bin/sh
# Prepares the RTX 2070 host (10.0.30.5, Core i5-13400F, Debian 13, no iGPU) for the nvrtx passthrough tests
# (WS037 方式 B): QEMU, OVMF, the IOMMU on the kernel command line, nouveau kept off, and every function of
# the NVIDIA card bound to vfio-pci at boot.  Run it once after Debian is installed; the host then needs one
# reboot, after which `plan/tools/hwrtx/prepare.sh --check` must print only "ok" lines.  The host has no other
# GPU, so after the reboot its own screen goes dark; it is used over ssh only.
#
#   plan/tools/hwrtx/prepare.sh [--check] [HOST]      HOST default kei@10.0.30.5 (NVRTX_HOST)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
check=0
[ "${1:-}" = --check ] && { check=1; shift; }
host=${1:-${NVRTX_HOST:-kei@10.0.30.5}}
ssh="timeout 300 ssh -o BatchMode=yes -o ConnectTimeout=10 $host"

if [ $check = 0 ]; then
	$ssh 'sudo -n sh -s' <<'EOF2' || { echo "prepare: failed on $host"; exit 1; }
set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq qemu-system-x86 qemu-utils ovmf pciutils >/dev/null
ids=$(lspci -nn -d 10de: | sed -n 's/.*\[\(10de:[0-9a-f]\{4\}\)\].*/\1/p' | sort -u | paste -sd,)
[ -n "$ids" ] || { echo "no NVIDIA function found"; exit 1; }
printf 'options vfio-pci ids=%s disable_vga=1\nsoftdep nouveau pre: vfio-pci\nsoftdep snd_hda_intel pre: vfio-pci\nsoftdep xhci_pci pre: vfio-pci\n' "$ids" > /etc/modprobe.d/nvrtx-vfio.conf
printf 'blacklist nouveau\noptions nouveau modeset=0\n' > /etc/modprobe.d/nvrtx-no-nouveau.conf
printf 'vfio\nvfio_iommu_type1\nvfio_pci\n' > /etc/modules-load.d/nvrtx-vfio.conf
grep -q 'intel_iommu=on' /etc/default/grub ||
	sed -i 's/^GRUB_CMDLINE_LINUX_DEFAULT="\(.*\)"/GRUB_CMDLINE_LINUX_DEFAULT="\1 intel_iommu=on iommu=pt"/' /etc/default/grub
update-initramfs -u >/dev/null
update-grub >/dev/null 2>&1
echo "vfio-pci ids: $ids"
echo "prepared; reboot the host, then run prepare.sh --check"
EOF2
	exit 0
fi

$ssh 'sh -s' <<'EOF2'
c() { if [ "$1" = 0 ]; then echo "ok   $2"; else echo "FAIL $2"; fi; }
grep -q intel_iommu=on /proc/cmdline; c $? "intel_iommu=on on the command line"
[ -d /sys/kernel/iommu_groups/0 ]; c $? "IOMMU groups exist"
for d in $(lspci -D -d 10de: | cut -d' ' -f1); do
	drv=$(basename "$(readlink /sys/bus/pci/devices/$d/driver 2>/dev/null)" 2>/dev/null)
	[ "$drv" = vfio-pci ]; c $? "$d $(lspci -n -s $d | cut -d' ' -f3) driver ${drv:-none} (want vfio-pci)"
	echo "     IOMMU group of $d: $(ls /sys/bus/pci/devices/$d/iommu_group/devices | paste -sd' ')"
done
[ -c /dev/kvm ]; c $? "/dev/kvm"
[ -f /usr/share/OVMF/OVMF_CODE_4M.fd ]; c $? "OVMF_CODE_4M.fd"
qemu-system-x86_64 --version >/dev/null 2>&1; c $? "QEMU $(qemu-system-x86_64 --version 2>/dev/null | head -1)"
sudo -n true 2>/dev/null; c $? "sudo -n"
lsmod | grep -q '^nouveau'; [ $? != 0 ]; c $? "nouveau not loaded"
EOF2
