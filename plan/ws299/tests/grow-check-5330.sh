#!/bin/sh
# ws299: checks the 5330's grown NVMe root (5330-nvme-write.sh image with ufs-grow.py) from zedBSD, under
# /tmp/i915-hw.lock: boots zedBSD once from the NVMe (~/boot-zedbsd.sh), prints df of /, writes GIB GiB of zeros and
# a 256 MiB random file (in kei's home) past the old 1 GiB, reads both back with cksum after a sync, and reboots to Debian.
#   sh plan/ws299/tests/grow-check-5330.sh [GIB]       (default 3)
# The last line is "grow-check: done|FAIL ...".  The files stay on the zedBSD root (the next image write replaces it).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
GIB=${1:-3}
HOST=solaris10-man
ZB=10.0.30.3
SO='-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 -o LogLevel=ERROR'

say() {
	echo "grow-check: $*"
}
lin() {
	timeout "${TO:-120}" ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST" "$1"
}
zb() {
	timeout "${TO:-60}" sshpass -p kei ssh $SO "kei@$ZB" "$1"
}
wait_for() {
	start=$(date +%s)
	until eval "$1" > /dev/null 2>&1; do
		if [ $(($(date +%s) - start)) -gt 300 ]; then
			say "FAIL: $2 did not come up in 5 minutes"
			exit 2
		fi
		sleep 5
	done
}

exec 9> /tmp/i915-hw.lock
flock 9
wait_for 'TO=8 lin hostname | grep -q chaos' Debian
lin '$HOME/boot-zedbsd.sh' > /dev/null 2>&1
sleep 20
wait_for 'TO=8 zb "uname -s" | grep -q zedBSD' zedBSD
TO=30 zb 'df -k /'
TO=1800 zb "dd if=/dev/zero of=grow-zero.bin bs=1024k count=$((GIB * 1024)) && dd if=/dev/urandom of=grow-rand.bin bs=1024k count=256 && sync && cksum grow-rand.bin && df -k /"
TO=1800 zb 'echo kei | sudo -S dd if=/dev/nvme0n1 of=/dev/null bs=1024k count=2048 2> /dev/null; cksum grow-rand.bin; cksum grow-zero.bin'
zb 'echo kei | sudo -S reboot' > /dev/null 2>&1
wait_for 'TO=8 lin hostname | grep -q chaos' Debian
say done
