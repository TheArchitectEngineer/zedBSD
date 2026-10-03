#!/bin/bash
# Boots the scratch kernel with the uncached-view probe in QEMU raspi4b and
# reads the probe through the QEMU monitor (virtual and physical reads).
set -euo pipefail
dir=/tmp/claude-1000/-home-awe-zedBSD-rpi4/3cdd62d7-28a8-5eca-949e-9a1decb1273f/scratchpad/halprop
qmp=/home/awe/zedBSD-rpi4/.claude/worktrees/agent-a3e93588c0558be61/plan/tools/qmp.py
cp /home/awe/zedBSD-rpi4/build/ws053-rpi4-full/hdd-image.img "$dir/sd.img"
rm -f "$dir/qmp.sock"
qemu-system-aarch64 -machine raspi4b -m 2G -kernel "$dir/tree/build/arm64/vmunix" \
	-dtb /home/awe/zedBSD-rpi4/vendor/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb \
	-drive "if=sd,format=raw,file=$dir/sd.img" -display none -serial null -serial null \
	-no-reboot -qmp "unix:$dir/qmp.sock,server,nowait" >"$dir/qemu.out" 2>&1 &
pid=$!
trap 'kill $pid 2>/dev/null || true' EXIT
sleep 60
hmp() { python3 "$qmp" "$dir/qmp.sock" human-monitor-command "{\"command-line\": \"$1\"}"; }
hmp "x /6gx 0xffff0000007cf350"
probe=$(hmp "x /6gx 0xffff0000007cf350")
echo "$probe"
hmp "x /1gx 0xffff000000739018"
l0=$(hmp "x /2gx 0xffff000000734000")
echo "L0: $l0"
l1=$(echo "$l0" | grep -o '0x[0-9a-f]\{16\}' | tail -1)
l1=$(( l1 & 0x0000fffffffff000 ))
l1e=$(hmp "xp /1gx $l1")
echo "L1[0]: $l1e"
l2=$(echo "$l1e" | grep -o '0x[0-9a-f]\{16\}' | tail -1)
l2=$(( l2 & 0x0000fffffffff000 ))
l2e=$(hmp "xp /1gx $l2")
echo "L2[0]: $l2e"
l3=$(echo "$l2e" | grep -o '0x[0-9a-f]\{16\}' | tail -1)
l3=$(( l3 & 0x0000fffffffff000 ))
echo "L3 entries:"; hmp "xp /4gx $l3"
hmp "gva2gpa 0xffff008000000000"
hmp "gva2gpa 0xffff008000001000"
hmp "gva2gpa 0xffff008000002000" || true
hmp "x /1wx 0xffff008000001000"
hmp "x /1wx 0xffff008000001004"
