#!/bin/sh
# Updates the kernel of a ZYBO that boots from its SD card (WS289 ws.md) over
# SSH, and reboots it into the new one: U-Boot SPL reads the kernel from the
# card's FAT, which the running system has at /boot/boot0.
#   plan/tools/zybo/deploy.sh KERNEL [--host HOST] [--port PORT]
#       [--dtb FILE] [--wait S] [--no-reboot]
# KERNEL is the kernel's ELF (such as build/zybo/vmunix) with its raw image
# vmunix.bin beside it.  The FIT vmunix.itb is made beside them with
# platform/armv7/tools/make-zybo-boot-files.py, which refuses a kernel that
# SPL could not load below the device tree.  It is copied to
# /boot/boot0/vmunix.itb.new and checked there, and only then the running
# one becomes vmunix.itb.old and the new one vmunix.itb; --dtb puts a
# system.dtb (make-zybo-boot-files.py dtb) in the same way.  The board then
# reboots (the SLCR's reset, after which the boot ROM reads the card again);
# --no-reboot leaves it running.  If the board does not come back, the
# boot-mode jumper goes back to JTAG and jtag-boot.sh boots it (the .old
# files are still on the card).
# HOST is the board's address (default zybo), PORT its SSH port (default
# 22).  --wait keeps trying to reach the board for up to S seconds, for a
# board still booting: its SSH server starts only once the first boot has
# made the host keys, which takes minutes.  The login is root's: with SSHPASS set in the environment the
# password is taken from it (sshpass -e), otherwise ssh asks or uses a key.
# The board's host key is not recorded: a new card makes a new one.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
root=$(cd "$(dirname -- "$0")/../../.." && pwd)
tool=$root/platform/armv7/tools/make-zybo-boot-files.py

kernel= host=zybo port=22 dtb= wait=0 reboot=1
while [ $# -gt 0 ]; do
	case "$1" in
	--host) host=$2; shift 2 ;;
	--port) port=$2; shift 2 ;;
	--dtb) dtb=$2; shift 2 ;;
	--wait) wait=$2; shift 2 ;;
	--no-reboot) reboot=; shift ;;
	-*) echo "deploy: unknown option $1" >&2; exit 2 ;;
	*) kernel=$1; shift ;;
	esac
done
if [ -z "$kernel" ]; then
	echo "usage: deploy.sh KERNEL [--host HOST] [--port PORT] [--dtb FILE] [--wait S] [--no-reboot]" >&2
	exit 2
fi
raw=$kernel.bin
[ -f "$kernel" ] && [ -f "$raw" ] || { echo "deploy: no $kernel and $raw" >&2; exit 2; }
[ -z "$dtb" ] || [ -f "$dtb" ] || { echo "deploy: no $dtb" >&2; exit 2; }

options="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=10"
login=
if [ -n "${SSHPASS:-}" ]; then
	command -v sshpass >/dev/null || { echo "deploy: SSHPASS is set but sshpass is not installed" >&2; exit 2; }
	login="sshpass -e"
fi
board_run() {
	# shellcheck disable=SC2086
	timeout 120 $login ssh $options -p "$port" "root@$host" "$@"
}
board_copy() {
	# shellcheck disable=SC2086
	timeout 600 $login scp $options -P "$port" "$1" "root@$host:$2"
}

# Makes the FIT and checks the kernel's layout on the way.
fit=$(dirname -- "$kernel")/vmunix.itb
python3 -I "$tool" fit --kernel "$raw" --elf "$kernel" "$fit" || exit 1

# The board must be the ZYBO booted from its card: armv7, with the boot
# files on /boot/boot0.  A board that does not answer is tried again every
# ten seconds while --wait allows.
start=$(date +%s)
until machine=$(board_run 'uname -m; test -f /boot/boot0/vmunix.itb && echo boot0-ok'); do
	if [ $(($(date +%s) - start)) -ge "$wait" ]; then
		echo "deploy: cannot reach root@$host:$port" >&2
		exit 1
	fi
	sleep 10
done
case "$machine" in
armv7*boot0-ok*) ;;
*) echo "deploy: $host is not a ZYBO with /boot/boot0/vmunix.itb: $machine" >&2; exit 1 ;;
esac

# Puts one boot file in place: copied beside the running one, checked and
# written out there, then the running one kept as .old and the new one
# renamed over it.  The card holds a bootable file throughout, save between
# the two renames.
deploy_file() {
	deploy_source=$1 deploy_name=$2
	deploy_sum=$(cksum < "$deploy_source")
	board_copy "$deploy_source" "/boot/boot0/$deploy_name.new" ||
		{ echo "deploy: copying $deploy_name failed" >&2; return 1; }
	deploy_remote=$(board_run "cksum < /boot/boot0/$deploy_name.new && sync") ||
		{ echo "deploy: checking $deploy_name on the board failed" >&2; return 1; }
	[ "$deploy_remote" = "$deploy_sum" ] ||
		{ echo "deploy: $deploy_name on the board is $deploy_remote, not $deploy_sum" >&2; return 1; }
	board_run "cd /boot/boot0 && if test -f $deploy_name; then mv $deploy_name $deploy_name.old; fi && mv $deploy_name.new $deploy_name && sync" ||
		{ echo "deploy: renaming $deploy_name on the board failed" >&2; return 1; }
	echo "deploy: /boot/boot0/$deploy_name is $deploy_sum (the former is $deploy_name.old)"
}
deploy_file "$fit" vmunix.itb || exit 1
if [ -n "$dtb" ]; then
	deploy_file "$dtb" system.dtb || exit 1
fi

# Reboots into the new kernel; the connection drops as the board resets.
if [ -n "$reboot" ]; then
	board_run reboot
	echo "deploy: rebooting $host"
fi
exit 0
