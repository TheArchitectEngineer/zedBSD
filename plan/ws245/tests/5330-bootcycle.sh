#!/bin/sh
# ws245: one bare start of the Latitude 5330 with one kernel (ws.md "5330 の素の起動での段ごとの回帰の手順"): from the
# 5330's Linux (ssh alias solaris10-man), copies ~/ws245/<kernel> over the USB's ZEDBSD partition's vmunix, boots it
# once through the UEFI entry ENTRY (BootNext; the boot order is not changed), waits for zedBSD's ssh, lets the session
# settle for 60 s, takes the evidence with 5330-survey.sh as <name>, and reboots back to Linux.
#   ENTRY=0008 sh plan/ws245/tests/5330-bootcycle.sh <kernel file in ~/ws245 on the 5330> <name>
# EARLY / EARLY_OUT: a script run right after the first ssh answer, its output file; EXTRA / EXTRA_OUT: one run after
# the 60 s.  DEBUG=1 adds i915.debug=display to the USB's zedbsd.cfg for this start (the whole list of the display
# writes) and puts the file back afterwards.  Hold /tmp/i915-hw.lock around it (flock).  Exit 2: zedBSD did not answer
# within 5 minutes (the machine may need its power cycled, or DHCP may have failed); 3: Linux did not come back.
# Nothing is removed on either machine.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
KERNEL=${1:?usage: 5330-bootcycle.sh <kernel> <name>}
NAME=${2:?usage: 5330-bootcycle.sh <kernel> <name>}
ENTRY=${ENTRY:?ENTRY: the UEFI entry of the USB (efibootmgr)}
cd "$(dirname "$0")/../../.."
SO='-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 -o LogLevel=ERROR'
CFG="sudo -n cp \$HOME/ws245/zedbsd.cfg.orig /mnt/zb/zedbsd.cfg"
if [ "${DEBUG:-0}" = 1 ]; then
	CFG="sudo -n cp /mnt/zb/zedbsd.cfg \$HOME/ws245/zedbsd.cfg.orig && (grep -q '^i915.debug=' /mnt/zb/zedbsd.cfg || echo i915.debug=display | sudo -n tee -a /mnt/zb/zedbsd.cfg > /dev/null)"
fi
timeout 90 ssh -o BatchMode=yes solaris10-man "sudo -n mount /dev/sda2 /mnt/zb && { test -f \$HOME/ws245/zedbsd.cfg.orig || sudo -n cp /mnt/zb/zedbsd.cfg \$HOME/ws245/zedbsd.cfg.orig; } && $CFG && sudo -n cp \$HOME/ws245/$KERNEL /mnt/zb/vmunix && sync && sha256sum /mnt/zb/vmunix && cat /mnt/zb/zedbsd.cfg && sudo -n umount /mnt/zb && sudo -n efibootmgr -n $ENTRY | grep BootNext && sudo -n systemctl reboot" || { echo INSTALL-FAILED; exit 1; }
date
sleep 20
start=$(date +%s)
until timeout 6 sshpass -p kei ssh $SO kei@10.0.30.3 'uname -s' 2>/dev/null | grep -qi zedbsd; do
	if [ $(( $(date +%s) - start )) -gt 300 ]; then echo ZEDBSD-TIMEOUT; date; exit 2; fi
	sleep 5
done
echo "zedBSD up after $(( $(date +%s) - start + 20 ))s"; date
[ -n "${EARLY:-}" ] && sh "$EARLY" > "$EARLY_OUT" 2>&1
sleep 60
[ -n "${EXTRA:-}" ] && sh "$EXTRA" > "$EXTRA_OUT" 2>&1
timeout 150 sh plan/ws245/tests/5330-survey.sh 10.0.30.3 "$NAME" 2>/dev/null | tail -1
timeout 15 sshpass -p kei ssh $SO kei@10.0.30.3 'echo kei | sudo -S reboot' > /dev/null 2>&1
start=$(date +%s)
until timeout 8 ssh -o BatchMode=yes -o ConnectTimeout=5 solaris10-man hostname 2>/dev/null | grep -q chaos; do
	if [ $(( $(date +%s) - start )) -gt 300 ]; then echo LINUX-TIMEOUT; exit 3; fi
	sleep 5
done
# A debug start's boot line is put back.
if [ "${DEBUG:-0}" = 1 ]; then
	timeout 60 ssh -o BatchMode=yes solaris10-man "sudo -n mount /dev/sda2 /mnt/zb && sudo -n cp \$HOME/ws245/zedbsd.cfg.orig /mnt/zb/zedbsd.cfg && sync && sudo -n umount /mnt/zb" || echo CFG-RESTORE-FAILED
fi
echo "Linux back"; date
