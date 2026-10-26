#!/bin/sh
# ws245: one regression run on the bare Latitude 5330, end to end, printing only a short summary.  From the 5330's
# Linux (ssh alias solaris10-man) it writes a whole image to the USB stick (--image) or puts a kernel in place of
# the USB's vmunix, keeping the one before as vmunix.old (--kernel), boots the USB once through UEFI BootNext (the
# boot order is not changed; the entry for the stick's EFI partition is made when there is none), waits for zedBSD's
# SSH, lets the session settle, takes the evidence (5330-survey.sh), the screen as the compositor shows it
# (keiland-shot, an image built with plan/tools/hw5330/config-5330.mk), reboots back to Linux and compares the survey
# with a reference (5330-survey-diff.py).
#   sh plan/tools/hw5330/5330-run.sh --image IMG | --kernel VMUNIX | --none  NAME [REFERENCE]
#   sh plan/tools/hw5330/5330-run.sh --nvme-image IMG | --nvme-kernel VMUNIX | --nvme-none  NAME [REFERENCE]
# The --nvme modes (q1612, WS298) start zedBSD from the NVMe's own zedBSD partitions instead of the stick, through the
# UEFI entry "zedBSD NVMe" of \EFI\zedbsd\BOOTX64.EFI on the Debian ESP (made when missing).  5330-nvme-write.sh, run
# on the 5330's Debian, writes them (the image's root, swap, kernel, logo and loader; or only the kernel).  They need
# a kernel with the VMD driver (WS298).  DEBUG and CFG_ADD then edit the NVMe's zedbsd.cfg (p4).
# REFERENCE: a reference kernel log or survey .md (default the 2026-10-17 merged main, evidence/t1229i21-...).
# DEBUG=1 adds i915.debug=display to the stick's zedbsd.cfg for this start and puts the file back afterwards;
# CFG_ADD='WORD ...' adds those words the same way (for example CFG_ADD=pci.vmd=off, WS298).
# Output: plan/tools/hw5330/evidence/NAME.md (the survey), LOGS (default build/hw5330/logs, not in git) holds the
# whole logs, NAME.png and NAME-diff.txt.  The last line is "5330-run: PASS|FAIL|...".  Hold no lock yourself: the
# script takes /tmp/i915-hw.lock.  Nothing is removed on either machine.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
MODE=${1:?usage: 5330-run.sh --[nvme-]image IMG | --[nvme-]kernel VMUNIX | --[nvme-]none NAME [REFERENCE]}
case "$MODE" in
--none | --nvme-none) FILE= ; shift ;;
*) FILE=${2:?the file}; shift 2 ;;
esac
# The boot FAT the configuration lines go to: the stick's, or the NVMe's p4 (5330-nvme-write.sh) and its ESP.
NV_BOOT=/dev/disk/by-partuuid/d4dbe962-cc37-4f27-8529-bb7446730178
NV_ESP_UUID=08ab575f-f718-4eff-a3c9-10d80b1217ea
case "$MODE" in
--nvme-*) BOOTFAT=$NV_BOOT ;;
*) BOOTFAT=/dev/sda2 ;;
esac
NAME=${1:?NAME}
cd "$(dirname "$0")/../../.."
REF=${2:-plan/tools/hw5330/evidence/t1229i21-main-merged-20261017.md}
LOGS=${LOGS:-build/hw5330/logs}
HOST=solaris10-man
ZB=10.0.30.3
SO='-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 -o LogLevel=ERROR'
mkdir -p "$LOGS"
exec 9> /tmp/i915-hw.lock
flock 9

lin() {
	timeout "${T:-120}" ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST" "$1"
}
zb() {
	timeout "${T:-60}" sshpass -p kei ssh $SO "kei@$ZB" "$1"
}
say() {
	echo "5330-run: $*"
}

case "$MODE" in
--nvme-*)
	# The NVMe's zedBSD partitions must be where and what the helper expects.
	T=60 timeout 60 scp -q plan/tools/hw5330/5330-nvme-write.sh "$HOST:ws245/5330-nvme-write.sh" || { say "FAIL: copy of the helper"; exit 1; }
	lin 'sudo -n sh $HOME/ws245/5330-nvme-write.sh check' > /dev/null || { say "FAIL: the NVMe's zedBSD partitions are not as expected"; exit 1; }
	;;
*)
	# The stick must be a removable USB disk, not the Linux disk.
	lin 'test "$(lsblk -dn -o TRAN,RM /dev/sda | tr -s " ")" = "usb 1"' || { say "FAIL: /dev/sda is not a removable USB disk"; exit 1; }
	;;
esac

case "$MODE" in
--image)
	base=$(basename "$FILE")
	T=1200 timeout 1200 scp -q "$FILE" "$HOST:ws245/$NAME-$base" || { say "FAIL: copy"; exit 1; }
	T=1500 lin "sudo -n dd if=\$HOME/ws245/$NAME-$base of=/dev/sda bs=4M conv=fsync status=none && sync && sudo -n partprobe /dev/sda" 2> /dev/null ||
	    { say "FAIL: dd"; exit 1; }
	say "image written ($(sha256sum "$FILE" | cut -c1-16))"
	;;
--kernel)
	T=120 timeout 120 scp -q "$FILE" "$HOST:ws245/vmunix-$NAME" || { say "FAIL: copy"; exit 1; }
	lin "sudo -n mount /dev/sda2 /mnt/zb && sudo -n cp /mnt/zb/vmunix /mnt/zb/vmunix.old && sudo -n cp \$HOME/ws245/vmunix-$NAME /mnt/zb/vmunix && sync && sudo -n umount /mnt/zb" ||
	    { say "FAIL: kernel install"; exit 1; }
	say "kernel installed ($(sha256sum "$FILE" | cut -c1-16)), the one before is vmunix.old"
	;;
--nvme-image)
	base=$(basename "$FILE")
	T=1200 timeout 1200 scp -q "$FILE" "$HOST:ws245/$NAME-$base" || { say "FAIL: copy"; exit 1; }
	T=1500 lin "sudo -n sh \$HOME/ws245/5330-nvme-write.sh image \$HOME/ws245/$NAME-$base" > /dev/null || { say "FAIL: NVMe write"; exit 1; }
	say "image written to the NVMe partitions ($(sha256sum "$FILE" | cut -c1-16))"
	;;
--nvme-kernel)
	T=120 timeout 120 scp -q "$FILE" "$HOST:ws245/vmunix-$NAME" || { say "FAIL: copy"; exit 1; }
	lin "sudo -n sh \$HOME/ws245/5330-nvme-write.sh kernel \$HOME/ws245/vmunix-$NAME" > /dev/null || { say "FAIL: kernel install"; exit 1; }
	say "kernel installed on the NVMe ($(sha256sum "$FILE" | cut -c1-16)), the one before is vmunix.old"
	;;
esac

case "$MODE" in
--nvme-*)
	# The UEFI entry of the zedBSD loader on the Debian ESP (made when missing).
	find_entry() {
		lin "sudo -n efibootmgr -v" | grep -i "$NV_ESP_UUID" | grep -i 'zedbsd.BOOTX64' | sed -n 's/^Boot\([0-9A-F]\{4\}\).*/\1/p' | head -1
	}
	entry=$(find_entry)
	if [ -z "$entry" ]; then
		lin 'sudo -n efibootmgr --create-only -d /dev/nvme0n1 -p 1 -L "zedBSD NVMe" -l "\EFI\zedbsd\BOOTX64.EFI"' > /dev/null
		entry=$(find_entry)
	fi
	[ -n "$entry" ] || { say "FAIL: no UEFI entry for the NVMe"; exit 1; }
	;;
*)
	# The UEFI entry of the stick's EFI partition (made when missing).
	uuid=$(lin 'lsblk -no PARTUUID /dev/sda1')
	entry=$(lin "sudo -n efibootmgr" | grep -i "$uuid" | sed -n 's/^Boot\([0-9A-F]\{4\}\).*/\1/p' | head -1)
	if [ -z "$entry" ]; then
		lin 'sudo -n efibootmgr --create-only -d /dev/sda -p 1 -L "zedBSD USB ws245" -l "\EFI\BOOT\BOOTX64.EFI"' > /dev/null
		entry=$(lin "sudo -n efibootmgr" | grep -i "$uuid" | sed -n 's/^Boot\([0-9A-F]\{4\}\).*/\1/p' | head -1)
	fi
	[ -n "$entry" ] || { say "FAIL: no UEFI entry for the stick"; exit 1; }
	;;
esac

if [ "${DEBUG:-0}" = 1 ] || [ -n "${CFG_ADD:-}" ]; then
	add=${CFG_ADD:-}
	[ "${DEBUG:-0}" = 1 ] && add="$add i915.debug=display"
	lin "sudo -n mount $BOOTFAT /mnt/zb && sudo -n cp /mnt/zb/zedbsd.cfg \$HOME/ws245/zedbsd.cfg.$NAME && for w in $add; do grep -qx \"\$w\" /mnt/zb/zedbsd.cfg || echo \"\$w\" | sudo -n tee -a /mnt/zb/zedbsd.cfg > /dev/null; done && sync && sudo -n umount /mnt/zb" ||
	    { say "FAIL: cfg lines"; exit 1; }
	say "cfg lines added for this start:$add"
fi
lin "sudo -n efibootmgr -n $entry > /dev/null && sudo -n systemctl reboot" > /dev/null 2>&1
say "BootNext $entry, rebooting"
sleep 20
start=$(date +%s)
until zb 'uname -s' 2> /dev/null | grep -qi zedbsd; do
	if [ $(($(date +%s) - start)) -gt 300 ]; then
		say "FAIL: zedBSD did not answer in 5 minutes (power cycle may be needed)"
		exit 2
	fi
	sleep 5
done
say "zedBSD up after $(($(date +%s) - start + 20)) s"
sleep "${SETTLE:-60}"
T=150 timeout 150 sh plan/tools/hw5330/5330-survey.sh "$ZB" "$NAME" > /dev/null 2>&1 ||
    say "survey failed"
zb "echo kei | sudo -S /bin/keiland-shot /tmp/$NAME.png" > "$LOGS/$NAME-shot.txt" 2>&1
timeout 60 sshpass -p kei scp $SO "kei@$ZB:/tmp/$NAME.png" "$LOGS/$NAME.png" > /dev/null 2>&1 || say "no screen shot ($(tail -1 "$LOGS/$NAME-shot.txt"))"
[ -n "${EXTRA:-}" ] && zb "$EXTRA" > "$LOGS/$NAME-extra.txt" 2>&1
zb 'echo kei | sudo -S reboot' > /dev/null 2>&1
start=$(date +%s)
until T=8 lin hostname 2> /dev/null | grep -q chaos; do
	if [ $(($(date +%s) - start)) -gt 300 ]; then
		say "Linux did not come back in 5 minutes"
		break
	fi
	sleep 5
done
if [ "${DEBUG:-0}" = 1 ] || [ -n "${CFG_ADD:-}" ]; then
	lin "sudo -n mount $BOOTFAT /mnt/zb && sudo -n cp \$HOME/ws245/zedbsd.cfg.$NAME /mnt/zb/zedbsd.cfg && sync && sudo -n umount /mnt/zb" || say "cfg lines not put back"
fi

# The summary.
md=plan/tools/hw5330/evidence/$NAME.md
dm=$LOGS/$NAME-dmesg.txt
[ -s "$dm" ] || { say "FAIL: no kernel log"; exit 3; }
grep -E '^(kern.boot.login|lease 1 claimed|picture up|device lost|error or warn lines):' "$md"
grep -cE 'SESSION start' "$md" | sed 's/^/session starts: /'
grep -E 'i915: survey: ' "$dm" | head -2
grep -iE 'i915:.*(fail|timeout|timed out|not lock|did not)' "$dm" | head -8
python3 -I plan/tools/hw5330/5330-survey-diff.py "$REF" "$dm" > "$LOGS/$NAME-diff.txt" 2>&1
grep -vE '^\s*$' "$LOGS/$NAME-diff.txt" | tail -6
[ -s "$LOGS/$NAME.png" ] && echo "screen: $LOGS/$NAME.png"
verdict=$(tail -1 "$LOGS/$NAME-diff.txt")
case "$verdict" in
*PASS*) say PASS ;;
*) say "FAIL (survey diff: $verdict)" ;;
esac
