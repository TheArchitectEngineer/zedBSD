#!/bin/sh
# Puts what the i915 passthrough tests run on the Let's Note SV7 (I915_MACHINE=sv7) into its ~/bigbang/ and checks
# the machine without starting QEMU: the launcher and the iGPU switch (the 5330's, plan/ws031/tests/host/), the
# iGPU 0000:00:02.0 (8086:5917) on vfio-pci alone in its IOMMU group, KVM, OVMF, QEMU, sudo without a password,
# 39 physical address bits (the launcher's host-phys-bits-limit) and room for the image.  Run it again after
# plan/ws031/tests/host/ changes or the machine is reinstalled.
#
#   plan/tools/hwsv7/setup.sh [HOST]        HOST default awe@10.0.10.27 (I915_HOST)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
host=${1:-${I915_HOST:-awe@10.0.10.27}}
ssh="timeout 60 ssh -o BatchMode=yes -o ConnectTimeout=10 $host"
status=0

$ssh 'mkdir -p bigbang' || { echo "setup: cannot reach $host"; exit 1; }
timeout 120 scp -q plan/ws031/tests/host/run-parity-vk.sh plan/ws031/tests/host/igpu-mode.sh "$host:bigbang/" ||
	{ echo "setup: copy failed"; exit 1; }
$ssh 'chmod 755 bigbang/run-parity-vk.sh bigbang/igpu-mode.sh'
for f in run-parity-vk.sh igpu-mode.sh; do
	$ssh "cat bigbang/$f" | cmp -s - "plan/ws031/tests/host/$f" && echo "ok   bigbang/$f" ||
		{ echo "FAIL bigbang/$f differs from plan/ws031/tests/host/$f"; status=1; }
done

# One line per check: "ok" or "FAIL", then what was seen.
checks=$($ssh 'sh -s' <<'EOF'
d=/sys/bus/pci/devices/0000:00:02.0
check() { if [ "$1" = 0 ]; then echo "ok   $2"; else echo "FAIL $2"; fi; }
id=$(cat $d/vendor $d/device 2>/dev/null | sed 's/^0x//' | paste -sd:)
[ "$id" = 8086:5917 ]; check $? "iGPU 00:02.0 is $id (want 8086:5917)"
drv=$(basename "$(readlink $d/driver 2>/dev/null)" 2>/dev/null)
[ "$drv" = vfio-pci ]; check $? "iGPU driver $drv (want vfio-pci)"
group=$(ls $d/iommu_group/devices 2>/dev/null | paste -sd' ')
[ "$group" = 0000:00:02.0 ]; check $? "IOMMU group of the iGPU: $group (want it alone)"
[ -c /dev/kvm ]; check $? "/dev/kvm"
[ -f /usr/share/OVMF/OVMF_CODE_4M.fd ] && [ -f /usr/share/OVMF/OVMF_VARS_4M.fd ]; check $? "OVMF_CODE_4M.fd and OVMF_VARS_4M.fd"
v=$(qemu-system-x86_64 --version 2>/dev/null | head -1)
[ -n "$v" ]; check $? "QEMU: $v"
sudo -n true 2>/dev/null; check $? "sudo -n"
bits=$(sed -n 's/^address sizes[^0-9]*\([0-9]*\) bits physical.*/\1/p' /proc/cpuinfo | head -1)
[ "${bits:-0}" -ge 39 ]; check $? "physical address bits $bits (want >= 39)"
mem=$(awk '/^MemAvailable/ { print int($2 / 1048576) }' /proc/meminfo)
[ "${mem:-0}" -ge 6 ]; check $? "memory available ${mem} GiB (the guest takes 4)"
room=$(df -Pk "$HOME" | awk 'NR == 2 { print int($4 / 1048576) }')
[ "${room:-0}" -ge 8 ]; check $? "free space in ~ ${room} GiB (the image is 2 to 3)"
pgrep -f '^qemu-system-x86_64' >/dev/null; [ $? != 0 ]; check $? "no QEMU running"
echo "igpu-mode: $(bigbang/igpu-mode.sh show)"
EOF
)
echo "$checks"
case $checks in *FAIL*|'') status=1 ;; esac
[ $status = 0 ] && echo "setup: $host ready" || echo "setup: $host NOT ready"
exit $status
